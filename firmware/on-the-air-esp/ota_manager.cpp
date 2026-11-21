#include "ota_manager.h"
#include "config.h"
#include "web_ui.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <esp_bt.h>

// =============================================================================
// CRITICAL: Override Arduino-ESP32's weak verifyRollbackLater()
// =============================================================================
// By default, Arduino-ESP32's initArduino() marks a newly-flashed OTA image as
// VALID *before* setup() even runs — which silently disables automatic rollback!
// Returning true here keeps the new partition in ESP_OTA_IMG_PENDING_VERIFY
// until OtaManager::serviceRollbackConfirmation() confirms healthy execution.
extern "C" bool verifyRollbackLater() {
  return true;
}

// Keep metadata string inside the compiled .bin so sumo_flash.py can inspect it.
const char EMBEDDED_FW_META[] = FW_META_STRING;

namespace {
WebServer g_server(80);
OtaManager* g_instance = nullptr;
}  // namespace

void OtaManager::begin(SumoParams* params,
                       SumoParamsStore* store,
                       DisarmCallback disarmFn,
                       IsArmedCallback isArmedFn) {
  params_    = params;
  store_     = store;
  disarmFn_  = disarmFn;
  isArmedFn_ = isArmedFn;
  bootMs_    = millis();
  g_instance = this;

  // Print embedded tag at boot (also guarantees linker keeps it in .rodata of the .bin)
  Serial.printf("[OTA] %s\n", EMBEDDED_FW_META);

  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_OTA_SWITCH, INPUT_PULLUP);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  // Immediately inspect OTA partition state
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
    rollbackConfirmed_ = (state != ESP_OTA_IMG_PENDING_VERIFY);
  } else {
    rollbackConfirmed_ = true;
  }
}

bool OtaManager::shouldEnterMaintenanceMode() {
  // 1. Dedicated Pit/OTA physical switch (LOW = Maintenance Mode)
  if (digitalRead(PIN_OTA_SWITCH) == LOW) {
    delay(25);  // debounce
    if (digitalRead(PIN_OTA_SWITCH) == LOW) {
      Serial.println(F("[OTA] Pit switch ACTIVE -> Maintenance Mode"));
      return true;
    }
  }

  // 2. Optional boot window using the built-in NodeMCU-32S BOOT button (GPIO 0)
  //    (zero extra wiring needed). Hold BOOT after power-on while LED fast-blinks.
#if BOOT_WINDOW_MS > 0
  Serial.printf("[OTA] Hold BOOT (GPIO %d) within %d ms for Maintenance Mode...\n",
                PIN_BOOT_BTN, BOOT_WINDOW_MS);
  uint32_t start = millis();
  uint32_t heldSince = 0;

  while (millis() - start < BOOT_WINDOW_MS) {
    esp_task_wdt_reset();
    digitalWrite(PIN_STATUS_LED, ((millis() / 100) % 2) == 0 ? HIGH : LOW);

    if (digitalRead(PIN_BOOT_BTN) == LOW) {
      if (heldSince == 0) heldSince = millis();
      if (millis() - heldSince >= 400) {
        Serial.println(F("[OTA] BOOT button held -> Maintenance Mode"));
        digitalWrite(PIN_STATUS_LED, HIGH);
        return true;
      }
    } else {
      heldSince = 0;
    }
    delay(15);
  }
  digitalWrite(PIN_STATUS_LED, LOW);
#endif

  return false;
}

void OtaManager::disableRadiosForMatch() {
  maintenanceActive_ = false;
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.println(F("[OTA] Match Mode: Wi-Fi & Bluetooth completely OFF."));
}

void OtaManager::startMaintenanceMode() {
  // Always force motors to a safe disarmed state before turning on Wi-Fi
  if (disarmFn_) {
    disarmFn_();
  }

  bool connectedSta = false;

#if OTA_USE_STA_FIRST
  Serial.printf("[OTA] Connecting to Pit Wi-Fi '%s'...\n", OTA_STA_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(OTA_STA_SSID, OTA_STA_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < OTA_STA_TIMEOUT_MS) {
    esp_task_wdt_reset();
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    connectedSta = true;
    Serial.print(F("[OTA] Connected STA IP: "));
    Serial.println(WiFi.localIP());
    if (MDNS.begin(OTA_MDNS_HOST)) {
      Serial.printf("[OTA] mDNS ready: http://%s.local\n", OTA_MDNS_HOST);
    }
  } else {
    Serial.println(F("[OTA] Pit Wi-Fi unavailable, falling back to SoftAP."));
  }
#endif

  if (!connectedSta) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(OTA_AP_SSID, OTA_AP_PASS, OTA_AP_CHANNEL);
    Serial.printf("[OTA] SoftAP started: SSID='%s' IP=%s\n",
                  OTA_AP_SSID, WiFi.softAPIP().toString().c_str());
  }

  registerRoutes();
  g_server.begin();
  maintenanceActive_ = true;
  Serial.println(F("[OTA] Pit HTTP Console running on port 80."));
}

void OtaManager::registerRoutes() {
  // IMPORTANT: Arduino WebServer ignores custom headers unless explicitly collected!
  const char* headerKeys[] = {"X-OTA-Token", "X-Target-Robot", "X-Firmware-MD5"};
  g_server.collectHeaders(headerKeys, 3);

  g_server.on("/", HTTP_GET, []() { g_instance->handleRoot(); });
  g_server.on("/api/status", HTTP_GET, []() { g_instance->handleStatus(); });
  g_server.on("/api/params", HTTP_GET, []() { g_instance->handleGetParams(); });
  g_server.on("/api/params", HTTP_POST, []() { g_instance->handlePostParams(); });
  g_server.on("/api/params/reset", HTTP_POST, []() { g_instance->handleResetParams(); });
  g_server.on(
      "/api/ota",
      HTTP_POST,
      []() { g_instance->handleOtaComplete(); },
      []() { g_instance->handleOtaUpload(); });
  g_server.on("/api/rollback", HTTP_POST, []() { g_instance->handleRollback(); });
  g_server.on("/api/reboot", HTTP_POST, []() { g_instance->handleReboot(); });
  g_server.onNotFound([]() {
    g_server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
  });
}

void OtaManager::handleClient() {
  if (!maintenanceActive_) return;

  g_server.handleClient();

  // Slow heartbeat blink on onboard LED so team clearly sees Maintenance Mode
  static uint32_t lastBlink = 0;
  if (millis() - lastBlink >= 600) {
    lastBlink = millis();
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
  }

  // Execute deferred reboot or rollback after HTTP response finishes sending
  if (pendingRollback_ && millis() >= actionAtMs_) {
    Serial.println(F("[OTA] Executing manual rollback & reboot..."));
    Serial.flush();
    esp_ota_mark_app_invalid_rollback_and_reboot();
    // If rollback fails (no other valid image), reboot normally
    ESP.restart();
  }
  if (pendingReboot_ && millis() >= actionAtMs_) {
    Serial.println(F("[OTA] Rebooting..."));
    Serial.flush();
    ESP.restart();
  }
}

void OtaManager::serviceRollbackConfirmation() {
  if (rollbackConfirmed_) return;

  if (millis() - bootMs_ >= OTA_SELF_TEST_WINDOW_MS) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
      Serial.println(F("[OTA] Self-test passed! New firmware marked VALID (rollback cancelled)."));
    } else {
      Serial.printf("[OTA] Warning: mark_app_valid returned %d\n", (int)err);
    }
    rollbackConfirmed_ = true;
  }
}

float OtaManager::readBatteryVoltage() const {
#if BATTERY_MONITOR_ENABLED
  uint32_t mv = analogReadMilliVolts(PIN_BATTERY_ADC);
  return (mv / 1000.0f) * BATTERY_DIVIDER_RATIO;
#else
  return -1.0f;
#endif
}

const char* OtaManager::getOtaStateString() const {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return "UNKNOWN";
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return "FACTORY_OR_VALID";
  }
  switch (state) {
    case ESP_OTA_IMG_NEW:            return "NEW";
    case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
    case ESP_OTA_IMG_VALID:          return "VALID";
    case ESP_OTA_IMG_INVALID:        return "INVALID";
    case ESP_OTA_IMG_ABORTED:        return "ABORTED";
    default:                         return "UNDEFINED";
  }
}

bool OtaManager::checkAuthAndReply() {
  String tok = g_server.header("X-OTA-Token");
  if (tok != OTA_AUTH_TOKEN) {
    g_server.send(401, "application/json", "{\"ok\":false,\"error\":\"Invalid OTA token\"}");
    return false;
  }
  return true;
}

void OtaManager::handleRoot() {
  g_server.send_P(200, "text/html", WEB_UI_HTML);
}

void OtaManager::handleStatus() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const char* partLabel = running ? running->label : "unknown";
  bool armed = isArmedFn_ ? isArmedFn_() : false;
  float battV = readBatteryVoltage();
  uint64_t mac = ESP.getEfuseMac();

  String json;
  json.reserve(384);
  json += '{';
  json += "\"ok\":true,";
  json += "\"robotId\":\"" ROBOT_ID "\",";
  json += "\"board\":\"" ROBOT_BOARD "\",";
  json += "\"version\":\"" FW_VERSION "\",";
  json += "\"chipId\":\"" + String((uint32_t)(mac >> 32), HEX) + String((uint32_t)mac, HEX) + "\",";
  json += "\"partition\":\"" + String(partLabel) + "\",";
  json += "\"otaState\":\"" + String(getOtaStateString()) + "\",";
  json += "\"armed\":" + String(armed ? "true" : "false") + ',';
  json += "\"batteryMonitor\":" + String(BATTERY_MONITOR_ENABLED ? "true" : "false") + ',';
  json += "\"batteryV\":" + String(battV, 2) + ',';
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ',';
  json += "\"uptimeSec\":" + String(millis() / 1000UL);
  json += '}';
  g_server.send(200, "application/json", json);
}

void OtaManager::handleGetParams() {
  if (!params_) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"Params uninitialized\"}");
    return;
  }
  g_server.send(200, "application/json", SumoParamsStore::toJson(*params_));
}

void OtaManager::handlePostParams() {
  if (!checkAuthAndReply()) return;
  if (!params_ || !store_) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"Params uninitialized\"}");
    return;
  }

  SumoParams next = *params_;
  if (g_server.hasArg("searchSpeed"))    next.searchSpeed    = (uint8_t)constrain(g_server.arg("searchSpeed").toInt(), 0, 255);
  if (g_server.hasArg("attackSpeed"))    next.attackSpeed    = (uint8_t)constrain(g_server.arg("attackSpeed").toInt(), 0, 255);
  if (g_server.hasArg("turnSpeed"))      next.turnSpeed      = (uint8_t)constrain(g_server.arg("turnSpeed").toInt(), 0, 255);
  if (g_server.hasArg("lineThreshold"))  next.lineThreshold  = (uint16_t)constrain(g_server.arg("lineThreshold").toInt(), 0, 4095);
  if (g_server.hasArg("opponentDistCm")) next.opponentDistCm = (uint16_t)constrain(g_server.arg("opponentDistCm").toInt(), 5, 150);
  if (g_server.hasArg("startDelayMs"))   next.startDelayMs   = (uint16_t)constrain(g_server.arg("startDelayMs").toInt(), 0, 10000);
  if (g_server.hasArg("strategy"))       next.strategy       = (uint8_t)constrain(g_server.arg("strategy").toInt(), 0, 3);

  SumoParams saved;
  if (!store_->save(next, saved)) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
    return;
  }
  *params_ = saved;
  String resp = "{\"ok\":true,\"params\":" + SumoParamsStore::toJson(*params_) + "}";
  g_server.send(200, "application/json", resp);
}

void OtaManager::handleResetParams() {
  if (!checkAuthAndReply()) return;
  if (!params_ || !store_) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"Params uninitialized\"}");
    return;
  }
  SumoParams saved;
  if (!store_->resetDefaults(saved)) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS reset failed\"}");
    return;
  }
  *params_ = saved;
  String resp = "{\"ok\":true,\"params\":" + SumoParamsStore::toJson(*params_) + "}";
  g_server.send(200, "application/json", resp);
}

void OtaManager::handleOtaUpload() {
  HTTPUpload& upload = g_server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaAuthorized_ = false;
    otaSuccess_    = false;
    otaError_      = "";

    // 1. Auth token check
    if (g_server.header("X-OTA-Token") != OTA_AUTH_TOKEN) {
      otaError_ = "Invalid OTA token";
      Serial.println(F("[OTA] Rejected: invalid token"));
      return;
    }

    // 2. Target robot ID mismatch check
    String targetRobot = g_server.header("X-Target-Robot");
    if (targetRobot.length() > 0 && targetRobot != ROBOT_ID) {
      otaError_ = "Target mismatch (expected " ROBOT_ID ", got " + targetRobot + ")";
      Serial.println("[OTA] Rejected: " + otaError_);
      return;
    }

    // 3. Armed safety interlock
    if (isArmedFn_ && isArmedFn_()) {
      otaError_ = "Motors are ARMED — disarm robot before flashing";
      Serial.println(F("[OTA] Rejected: motors armed"));
      return;
    }

    // 4. Battery voltage floor check
#if BATTERY_MONITOR_ENABLED
    float v = readBatteryVoltage();
    if (v < BATTERY_MIN_OTA_VOLTS) {
      otaError_ = "Battery too low for safe flash (" + String(v, 2) + "V < " + String(BATTERY_MIN_OTA_VOLTS, 2) + "V)";
      Serial.println("[OTA] Rejected: " + otaError_);
      return;
    }
#endif

    // Ensure motors are explicitly disarmed before flash erase/write starts
    if (disarmFn_) disarmFn_();

    Serial.printf("[OTA] Starting flash: %s\n", upload.filename.c_str());

    // Optional end-to-end MD5 integrity check supplied by sumo_flash.py
    String md5 = g_server.header("X-Firmware-MD5");
    if (md5.length() == 32) {
      Update.setMD5(md5.c_str());
      Serial.println("[OTA] MD5 verification enabled: " + md5);
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      otaError_ = String("Update.begin failed: ") + Update.errorString();
      Serial.println("[OTA] " + otaError_);
      return;
    }
    otaAuthorized_ = true;

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // Feed watchdog while streaming flash writes!
    esp_task_wdt_reset();
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));

    if (!otaAuthorized_) return;
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaError_ = String("Update.write failed: ") + Update.errorString();
      Serial.println("[OTA] " + otaError_);
      Update.abort();
      otaAuthorized_ = false;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    esp_task_wdt_reset();
    if (!otaAuthorized_) return;

    if (Update.end(true)) {
      otaSuccess_ = true;
      Serial.printf("[OTA] Flash complete (%u bytes). Scheduling reboot.\n", upload.totalSize);
    } else {
      otaError_ = String("Update.end failed: ") + Update.errorString();
      Serial.println("[OTA] " + otaError_);
    }

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaAuthorized_ = false;
    otaError_ = "Upload aborted by client";
    Serial.println(F("[OTA] Upload aborted."));
  }
}

void OtaManager::handleOtaComplete() {
  if (otaSuccess_) {
    g_server.send(200, "application/json",
                  "{\"ok\":true,\"message\":\"Firmware flashed. Rebooting into new partition.\"}");
    pendingReboot_ = true;
    actionAtMs_    = millis() + 600;
  } else {
    String err = otaError_.length() ? otaError_ : String("Unknown OTA error");
    err.replace("\"", "'");
    int code = (err == "Invalid OTA token") ? 401 : 400;
    g_server.send(code, "application/json", "{\"ok\":false,\"error\":\"" + err + "\"}");
  }
}

void OtaManager::handleRollback() {
  if (!checkAuthAndReply()) return;

  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  if (!next) {
    g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"No fallback OTA partition\"}");
    return;
  }

  g_server.send(200, "application/json",
                "{\"ok\":true,\"message\":\"Rolling back to previous firmware...\"}");
  pendingRollback_ = true;
  actionAtMs_      = millis() + 600;
}

void OtaManager::handleReboot() {
  if (!checkAuthAndReply()) return;
  g_server.send(200, "application/json", "{\"ok\":true,\"message\":\"Rebooting...\"}");
  pendingReboot_ = true;
  actionAtMs_    = millis() + 500;
}
