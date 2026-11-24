#include "ota_manager.h"
#include "ota_monitor.h"
#include "config.h"
#include "web_ui.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <esp_bt.h>

// =============================================================================
// CRITICAL: Override Arduino-ESP32's weak verifyRollbackLater()
// =============================================================================
extern "C" bool verifyRollbackLater() {
  return true;
}

// Compiled into .rodata of the .bin so ota_flash.py & Web UI can verify target ID.
const char EMBEDDED_FW_META[] = FW_META_STRING;

namespace {
constexpr const char* WIFI_NVS_NS = "mtr_wifi";
WebServer g_server(80);
DNSServer g_dnsServer;
OtaManager* g_instance = nullptr;

inline void safeFeedWdt() {
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
}

bool isSafeGpioPin(int pin) {
  if (pin < 0 || pin > 39) return false;
  // Block SPI flash pins (6..11) and unbonded ESP32 pins
  if (pin >= 6 && pin <= 11) return false;
  if (pin == 20 || pin == 24 || (pin >= 28 && pin <= 31)) return false;
  return true;
}

bool isInputOnlyPin(int pin) {
  return pin >= 34 && pin <= 39;
}

bool isAdc1Pin(int pin) {
  return pin >= 32 && pin <= 39;
}

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    switch (c) {
      case '\"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c >= 0x20) out += c;
        break;
    }
  }
  return out;
}
}  // namespace

void OtaManager::setCredentials(const char* ssid, const char* password, const char* ideHostname) {
  if (ssid && ssid[0]) {
    apSsid_   = ssid;
    mdnsHost_ = (ideHostname && ideHostname[0]) ? ideHostname : ssid;
  } else if (ideHostname && ideHostname[0]) {
    mdnsHost_ = ideHostname;
  }
  if (password) {
    apPass_    = password;
    authToken_ = password;
  }
}

void OtaManager::setExternalWiFi(const char* staSsid, const char* staPassword) {
  extSsid_ = staSsid ? staSsid : "";
  extPass_ = staPassword ? staPassword : "";
}

void OtaManager::setDeviceId(const char* deviceId, const char* version) {
  if (deviceId && deviceId[0]) deviceId_ = deviceId;
  if (version && version[0])   fwVersion_ = version;
}

bool OtaManager::loadSavedStaCredentials(String& ssidOut, String& passOut) const {
  if (extSsid_.length() > 0) {
    ssidOut = extSsid_;
    passOut = extPass_;
    return true;
  }
  Preferences prefs;
  if (!prefs.begin(WIFI_NVS_NS, true)) return false;
  ssidOut = prefs.getString("ssid", "");
  passOut = prefs.getString("pass", "");
  prefs.end();
  return ssidOut.length() > 0;
}

bool OtaManager::saveStaCredentials(const String& ssid, const String& pass) {
  Preferences prefs;
  if (!prefs.begin(WIFI_NVS_NS, false)) return false;
  if (ssid.length() == 0) {
    prefs.clear();
  } else {
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
  }
  prefs.end();
  return true;
}

void OtaManager::begin(AppParams* params,
                       AppParamsStore* store,
                       DisarmCallback disarmFn,
                       IsArmedCallback isArmedFn) {
  params_    = params;
  store_     = store;
  disarmFn_  = disarmFn;
  isArmedFn_ = isArmedFn;
  bootMs_    = millis();
  g_instance = this;

  OtaMonitor.printf("[OTA] %s\n", EMBEDDED_FW_META);
  OtaMonitor.onCommand([](const String& cmd) {
    if (g_instance) g_instance->handleMonitorCommand(cmd);
  });

  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_OTA_SWITCH, INPUT_PULLUP);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
    rollbackConfirmed_ = (state != ESP_OTA_IMG_PENDING_VERIFY);
  } else {
    rollbackConfirmed_ = true;
  }
}

bool OtaManager::isRecoverySwitchActive() const {
  if (digitalRead(PIN_OTA_SWITCH) == LOW) {
    delay(25);
    return digitalRead(PIN_OTA_SWITCH) == LOW;
  }
  return false;
}

bool OtaManager::shouldEnterMaintenanceMode() {
  if (isRecoverySwitchActive()) {
    OtaMonitor.println(F("[OTA] Recovery switch (GPIO 33) ACTIVE -> Safe Maintenance Mode"));
    return true;
  }

#if BOOT_WINDOW_MS > 0
  OtaMonitor.printf("[OTA] Press BOOT (GPIO %d) within %d ms for Maintenance Mode...\n",
                    PIN_BOOT_BTN, BOOT_WINDOW_MS);
  uint32_t start = millis();
  uint32_t heldSince = 0;

  while (millis() - start < BOOT_WINDOW_MS) {
    safeFeedWdt();
    digitalWrite(PIN_STATUS_LED, ((millis() / 100) % 2) == 0 ? HIGH : LOW);

    if (digitalRead(PIN_BOOT_BTN) == LOW) {
      if (heldSince == 0) heldSince = millis();
      if (millis() - heldSince >= 400) {
        OtaMonitor.println(F("[OTA] BOOT button held -> Maintenance Mode"));
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

void OtaManager::disableRadiosForRunMode() {
  maintenanceActive_ = false;
  wirelessActive_    = false;
  dnsActive_         = false;
  OtaMonitor.stopServer();
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  OtaMonitor.println(F("[OTA] Normal Run Mode: Wi-Fi & Bluetooth completely OFF."));
}

void OtaManager::startMaintenanceMode() {
  maintenanceActive_ = true;
  startWirelessServices(true);
}

void OtaManager::startWirelessServices(bool disarmOutputs) {
  if (disarmOutputs && disarmFn_) {
    disarmFn_();
  }

  String staSsid, staPass;
  const bool wantSta = loadSavedStaCredentials(staSsid, staPass);
  activeStaSsid_ = staSsid;

  // Dual AP+STA mode when external Wi-Fi is configured so MTR-OTA hotspot ALWAYS
  // stays reachable even while connected to home/lab Wi-Fi (with full internet!).
  WiFi.mode(wantSta ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(apSsid_, apPass_, OTA_AP_CHANNEL);
  delay(150);
  IPAddress apIp = WiFi.softAPIP();
  g_dnsServer.start(53, "*", apIp);
  dnsActive_ = true;
  OtaMonitor.printf("[OTA] SoftAP ready: SSID='%s' IP=%s\n",
                    apSsid_, apIp.toString().c_str());

  if (wantSta) {
    OtaMonitor.printf("[OTA] Joining external Wi-Fi '%s' (Dual AP+STA)...\n", staSsid.c_str());
    WiFi.setAutoReconnect(true);
    WiFi.begin(staSsid.c_str(), staPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < OTA_STA_TIMEOUT_MS) {
      safeFeedWdt();
      delay(150);
    }
    if (WiFi.status() == WL_CONNECTED) {
      OtaMonitor.printf("[OTA] Connected to '%s'! IP=%s (http://%s.local)\n",
                        staSsid.c_str(), WiFi.localIP().toString().c_str(), mdnsHost_);
    } else {
      OtaMonitor.printf("[OTA] '%s' not in range yet (will auto-connect in background; SoftAP active).\n",
                        staSsid.c_str());
    }
  }

  registerRoutes();
  g_server.begin();
  startArduinoOta();
  OtaMonitor.beginServer(OTA_MONITOR_PORT);
  wirelessActive_ = true;
  OtaMonitor.printf("[OTA] Web Console (:80), OTA Monitor (:%u) & Arduino IDE Port ready.\n",
                    (unsigned)OTA_MONITOR_PORT);
}

void OtaManager::startArduinoOta() {
  ArduinoOTA.setHostname(mdnsHost_);
  if (isAuthRequired()) {
    ArduinoOTA.setPassword(authToken_);
  }

  ArduinoOTA.onStart([this]() {
    if (isArmedFn_ && isArmedFn_()) {
      OtaMonitor.println(F("[ArduinoOTA] Blocked: device outputs are ARMED!"));
      ESP.restart();
    }
    if (disarmFn_) disarmFn_();
    OtaMonitor.println(F("[ArduinoOTA] Flashing from Arduino IDE..."));
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    safeFeedWdt();
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    vTaskDelay(1);
    (void)progress;
    (void)total;
  });

  ArduinoOTA.onEnd([]() {
    OtaMonitor.println(F("\n[ArduinoOTA] Flash complete! Rebooting..."));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    OtaMonitor.printf("[ArduinoOTA] Error[%u]\n", (unsigned)error);
  });

  ArduinoOTA.begin();
}

void OtaManager::registerRoutes() {
  const char* headerKeys[] = {"X-OTA-Token", "X-Target-Device", "X-Firmware-MD5"};
  g_server.collectHeaders(headerKeys, 3);

  g_server.on("/", HTTP_GET, []() { g_instance->handleRoot(); });
  g_server.on("/api/status", HTTP_GET, []() { g_instance->handleStatus(); });
  g_server.on("/api/params", HTTP_GET, []() { g_instance->handleGetParams(); });
  g_server.on("/api/params", HTTP_POST, []() { g_instance->handlePostParams(); });
  g_server.on("/api/params/reset", HTTP_POST, []() { g_instance->handleResetParams(); });
  g_server.on("/api/wifi/scan", HTTP_GET, []() { g_instance->handleWifiScan(); });
  g_server.on("/api/wifi", HTTP_POST, []() { g_instance->handlePostWifi(); });
  g_server.on("/api/gpio", HTTP_POST, []() { g_instance->handlePostGpio(); });
  g_server.on("/api/logs", HTTP_GET, []() { g_instance->handleGetLogs(); });
  g_server.on("/api/logs/clear", HTTP_POST, []() { g_instance->handleClearLogs(); });
  g_server.on("/api/logs/send", HTTP_POST, []() { g_instance->handleSendLogCmd(); });
  g_server.on(
      "/api/ota",
      HTTP_POST,
      []() { g_instance->handleOtaComplete(); },
      []() { g_instance->handleOtaUpload(); });
  g_server.on("/api/rollback", HTTP_POST, []() { g_instance->handleRollback(); });
  g_server.on("/api/reboot", HTTP_POST, []() { g_instance->handleReboot(); });

  g_server.onNotFound([]() {
    if (g_server.uri().startsWith("/api/")) {
      g_server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
      return;
    }
    g_server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    g_server.send(302, "text/plain", "");
  });
}

void OtaManager::handleClient() {
  if (!wirelessActive_) return;

  if (dnsActive_) {
    g_dnsServer.processNextRequest();
  }
  g_server.handleClient();
  ArduinoOTA.handle();
  OtaMonitor.handle();

  // Announce STA IP transitions cleanly in OTA Monitor
  static bool wasStaConnected = false;
  const bool nowSta = (WiFi.status() == WL_CONNECTED);
  if (nowSta && !wasStaConnected) {
    wasStaConnected = true;
    OtaMonitor.printf("[OTA] External Wi-Fi connected! IP=%s RSSI=%d dBm (http://%s.local)\n",
                      WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), mdnsHost_);
  } else if (!nowSta && wasStaConnected) {
    wasStaConnected = false;
    OtaMonitor.println(F("[OTA] External Wi-Fi disconnected (MTR-OTA SoftAP still active)."));
  }

  if (maintenanceActive_) {
    static uint32_t lastBlink = 0;
    if (millis() - lastBlink >= 600) {
      lastBlink = millis();
      digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    }
  }

  if (pendingRollback_ && millis() >= actionAtMs_) {
    OtaMonitor.println(F("[OTA] Executing rollback & reboot..."));
    Serial.flush();
    esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP.restart();
  }
  if (pendingReboot_ && millis() >= actionAtMs_) {
    OtaMonitor.println(F("[OTA] Rebooting..."));
    Serial.flush();
    ESP.restart();
  }
}

void OtaManager::handleMonitorCommand(const String& cmd) {
  String lower = cmd;
  lower.toLowerCase();

  if (lower == "help" || lower == "?") {
    OtaMonitor.println(F("OTA Monitor commands: help | status | wifi | params | clear | reboot | rollback"));
  } else if (lower == "status") {
    const esp_partition_t* running = esp_ota_get_running_partition();
    OtaMonitor.printf("Device=%s v%s | Slot=%s | State=%s | Heap=%u B | Uptime=%lu s\n",
                      deviceId_,
                      fwVersion_,
                      running ? running->label : "?",
                      getOtaStateString(),
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned long)(millis() / 1000UL));
  } else if (lower == "wifi") {
    const bool staOk = (WiFi.status() == WL_CONNECTED);
    OtaMonitor.printf("AP='%s' (%s, %u clients) | STA='%s' (%s, RSSI=%d dBm)\n",
                      apSsid_,
                      WiFi.softAPIP().toString().c_str(),
                      (unsigned)WiFi.softAPgetStationNum(),
                      activeStaSsid_.c_str(),
                      staOk ? WiFi.localIP().toString().c_str() : "disconnected",
                      staOk ? (int)WiFi.RSSI() : 0);
  } else if (lower == "params" && params_) {
    OtaMonitor.println(AppParamsStore::toJson(*params_));
  } else if (lower == "clear") {
    OtaMonitor.clearBuffer();
  } else if (lower == "reboot") {
    OtaMonitor.println(F("[OTA] Reboot scheduled via OTA Monitor..."));
    pendingReboot_ = true;
    actionAtMs_    = millis() + 300;
  } else if (lower == "rollback") {
    OtaMonitor.println(F("[OTA] Rollback scheduled via OTA Monitor..."));
    pendingRollback_ = true;
    actionAtMs_      = millis() + 300;
  }
}

void OtaManager::serviceRollbackConfirmation() {
  if (rollbackConfirmed_) return;
  if (millis() - bootMs_ >= OTA_SELF_TEST_WINDOW_MS) {
    confirmRollbackNow();
  }
}

void OtaManager::confirmRollbackNow() {
  if (rollbackConfirmed_) return;
  rollbackConfirmed_ = true;

  esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err == ESP_OK) {
    OtaMonitor.println(F("[OTA] Self-test passed! Firmware marked VALID (rollback cancelled)."));
  } else {
    OtaMonitor.printf("[OTA] Warning: mark_app_valid returned %d\n", (int)err);
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

bool OtaManager::isAuthRequired() const {
  return authToken_ && strlen(authToken_) > 0;
}

bool OtaManager::isTokenValid(const String& provided) const {
  if (!isAuthRequired()) return true;
  return provided == authToken_;
}

bool OtaManager::checkAuthAndReply() {
  if (!isTokenValid(g_server.header("X-OTA-Token"))) {
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
  const bool staConn = (WiFi.status() == WL_CONNECTED);

  String json;
  json.reserve(640);
  json += '{';
  json += "\"ok\":true,";
  json += "\"deviceId\":\"" + jsonEscape(String(deviceId_)) + "\",";
  json += "\"board\":\"" DEVICE_BOARD "\",";
  json += "\"version\":\"" + jsonEscape(String(fwVersion_)) + "\",";
  json += "\"chipId\":\"" + String((uint32_t)(mac >> 32), HEX) + String((uint32_t)mac, HEX) + "\",";
  json += "\"partition\":\"" + String(partLabel) + "\",";
  json += "\"otaState\":\"" + String(getOtaStateString()) + "\",";
  json += "\"mdns\":\"" + jsonEscape(String(mdnsHost_)) + ".local\",";
  json += "\"apSsid\":\"" + jsonEscape(String(apSsid_)) + "\",";
  json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"apClients\":" + String(WiFi.softAPgetStationNum()) + ',';
  json += "\"staSsid\":\"" + jsonEscape(activeStaSsid_) + "\",";
  json += "\"staConnected\":" + String(staConn ? "true" : "false") + ',';
  json += "\"staIp\":\"" + (staConn ? WiFi.localIP().toString() : String("")) + "\",";
  json += "\"rssi\":" + String(staConn ? (int)WiFi.RSSI() : 0) + ',';
  json += "\"monitorPort\":" + String(OTA_MONITOR_PORT) + ',';
  json += "\"authRequired\":" + String(isAuthRequired() ? "true" : "false") + ',';
  json += "\"armed\":" + String(armed ? "true" : "false") + ',';
  json += "\"batteryMonitor\":" + String(BATTERY_MONITOR_ENABLED ? "true" : "false") + ',';
  json += "\"batteryV\":" + String(battV, 2) + ',';
  json += "\"cpuMhz\":" + String(ESP.getCpuFreqMHz()) + ',';
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
  g_server.send(200, "application/json", AppParamsStore::toJson(*params_));
}

void OtaManager::handlePostParams() {
  if (!checkAuthAndReply()) return;
  if (!params_ || !store_) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"Params uninitialized\"}");
    return;
  }

  AppParams next = *params_;
  if (g_server.hasArg("speedNormal"))     next.speedNormal     = (uint8_t)constrain(g_server.arg("speedNormal").toInt(), 0, 255);
  if (g_server.hasArg("speedMax"))        next.speedMax        = (uint8_t)constrain(g_server.arg("speedMax").toInt(), 0, 255);
  if (g_server.hasArg("turnSpeed"))       next.turnSpeed       = (uint8_t)constrain(g_server.arg("turnSpeed").toInt(), 0, 255);
  if (g_server.hasArg("sensorThreshold")) next.sensorThreshold = (uint16_t)constrain(g_server.arg("sensorThreshold").toInt(), 0, 4095);
  if (g_server.hasArg("rangeThreshold"))  next.rangeThreshold  = (uint16_t)constrain(g_server.arg("rangeThreshold").toInt(), 5, 150);
  if (g_server.hasArg("startDelayMs"))    next.startDelayMs    = (uint16_t)constrain(g_server.arg("startDelayMs").toInt(), 0, 10000);
  if (g_server.hasArg("mode"))            next.mode            = (uint8_t)constrain(g_server.arg("mode").toInt(), 0, 3);

  AppParams saved;
  if (!store_->save(next, saved)) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
    return;
  }
  *params_ = saved;
  OtaMonitor.println("[OTA] Saved NVS params: " + AppParamsStore::toJson(*params_));
  String resp = "{\"ok\":true,\"params\":" + AppParamsStore::toJson(*params_) + "}";
  g_server.send(200, "application/json", resp);
}

void OtaManager::handleResetParams() {
  if (!checkAuthAndReply()) return;
  if (!params_ || !store_) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"Params uninitialized\"}");
    return;
  }
  AppParams saved;
  if (!store_->resetDefaults(saved)) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS reset failed\"}");
    return;
  }
  *params_ = saved;
  OtaMonitor.println(F("[OTA] Reset NVS params to defaults."));
  String resp = "{\"ok\":true,\"params\":" + AppParamsStore::toJson(*params_) + "}";
  g_server.send(200, "application/json", resp);
}

void OtaManager::handleWifiScan() {
  int n = WiFi.scanNetworks(false, false, false, 150);
  String json = "{\"ok\":true,\"networks\":[";
  for (int i = 0; i < n; ++i) {
    if (i > 0) json += ',';
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ',';
    json += "\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + '}';
  }
  json += "]}";
  WiFi.scanDelete();
  g_server.send(200, "application/json", json);
}

void OtaManager::handlePostWifi() {
  if (!checkAuthAndReply()) return;

  String ssid = g_server.arg("ssid");
  String pass = g_server.arg("password");
  ssid.trim();

  if (!saveStaCredentials(ssid, pass)) {
    g_server.send(500, "application/json", "{\"ok\":false,\"error\":\"Failed to save Wi-Fi to NVS\"}");
    return;
  }

  activeStaSsid_ = ssid;
  if (ssid.length() == 0) {
    WiFi.disconnect(false, true);
    WiFi.mode(WIFI_AP);
    OtaMonitor.println(F("[OTA] Cleared external Wi-Fi (SoftAP-only mode)."));
    g_server.send(200, "application/json", "{\"ok\":true,\"message\":\"External Wi-Fi forgotten (MTR-OTA SoftAP active).\"}");
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  OtaMonitor.printf("[OTA] Saved & connecting to external Wi-Fi '%s'...\n", ssid.c_str());
  g_server.send(200, "application/json",
                "{\"ok\":true,\"message\":\"Saved! Connecting to '" + jsonEscape(ssid) + "' in Dual AP+STA mode...\"}");
}

void OtaManager::handlePostGpio() {
  if (!checkAuthAndReply()) return;

  int pin = g_server.hasArg("pin") ? g_server.arg("pin").toInt() : -1;
  String action = g_server.arg("action");
  action.toLowerCase();

  if (!isSafeGpioPin(pin)) {
    g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"Unsafe or invalid ESP32 GPIO pin\"}");
    return;
  }

  if (action == "dread") {
    pinMode(pin, isInputOnlyPin(pin) ? INPUT : INPUT_PULLUP);
    int val = digitalRead(pin);
    g_server.send(200, "application/json",
                  "{\"ok\":true,\"pin\":" + String(pin) + ",\"value\":" + String(val) +
                  ",\"summary\":\"GPIO " + String(pin) + " digitalRead = " + (val ? "HIGH (1)" : "LOW (0)") + "\"}");
    return;
  }

  if (action == "aread") {
    if (!isAdc1Pin(pin)) {
      g_server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"GPIO " + String(pin) +
                    " is on ADC2 (unavailable while Wi-Fi is active). Use ADC1: GPIO 32-39.\"}");
      return;
    }
    int raw = analogRead(pin);
    uint32_t mv = analogReadMilliVolts(pin);
    g_server.send(200, "application/json",
                  "{\"ok\":true,\"pin\":" + String(pin) + ",\"raw\":" + String(raw) + ",\"mv\":" + String(mv) +
                  ",\"summary\":\"GPIO " + String(pin) + " = " + String(raw) + " (" + String(mv) + " mV)\"}");
    return;
  }

  // Output actions blocked when device is armed or pin is input-only (34..39)
  if (isArmedFn_ && isArmedFn_()) {
    g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"Outputs are ARMED — disarm before testing GPIO\"}");
    return;
  }
  if (isInputOnlyPin(pin)) {
    g_server.send(400, "application/json",
                  "{\"ok\":false,\"error\":\"GPIO " + String(pin) + " is input-only (34-39 cannot output HIGH/LOW/PWM)\"}");
    return;
  }

  if (action == "high" || action == "low") {
    int level = (action == "high") ? HIGH : LOW;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level);
    OtaMonitor.printf("[GPIO] Set GPIO %d -> %s\n", pin, level ? "HIGH" : "LOW");
    g_server.send(200, "application/json",
                  "{\"ok\":true,\"pin\":" + String(pin) + ",\"summary\":\"GPIO " + String(pin) + " set " +
                  (level ? "HIGH" : "LOW") + "\"}");
    return;
  }

  if (action == "pwm") {
    int duty = constrain(g_server.arg("value").toInt(), 0, 255);
    analogWrite(pin, duty);
    OtaMonitor.printf("[GPIO] PWM GPIO %d -> %d/255\n", pin, duty);
    g_server.send(200, "application/json",
                  "{\"ok\":true,\"pin\":" + String(pin) + ",\"duty\":" + String(duty) +
                  ",\"summary\":\"GPIO " + String(pin) + " PWM = " + String(duty) + "/255\"}");
    return;
  }

  g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"Unknown GPIO action\"}");
}

void OtaManager::handleGetLogs() {
  uint32_t since = 0;
  if (g_server.hasArg("since")) {
    since = (uint32_t)strtoul(g_server.arg("since").c_str(), nullptr, 10);
  }
  uint32_t nextOffset = 0;
  String chunk = OtaMonitor.readLogChunk(since, nextOffset);
  String json;
  json.reserve(chunk.length() + 64);
  json += "{\"ok\":true,\"next\":";
  json += String(nextOffset);
  json += ",\"data\":\"";
  json += jsonEscape(chunk);
  json += "\"}";
  g_server.send(200, "application/json", json);
}

void OtaManager::handleClearLogs() {
  OtaMonitor.clearBuffer();
  g_server.send(200, "application/json", "{\"ok\":true}");
}

void OtaManager::handleSendLogCmd() {
  String cmd = g_server.arg("cmd");
  cmd.trim();
  if (cmd.length() == 0) {
    g_server.send(400, "application/json", "{\"ok\":false,\"error\":\"Empty command\"}");
    return;
  }
  OtaMonitor.injectCommand(cmd);
  g_server.send(200, "application/json", "{\"ok\":true}");
}

void OtaManager::handleOtaUpload() {
  HTTPUpload& upload = g_server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaAuthorized_ = false;
    otaSuccess_    = false;
    otaError_      = "";

    if (!isTokenValid(g_server.header("X-OTA-Token"))) {
      otaError_ = "Invalid OTA token";
      OtaMonitor.println(F("[OTA] Rejected: invalid token"));
      return;
    }

    String targetDevice = g_server.header("X-Target-Device");
    if (targetDevice.length() > 0 && targetDevice != deviceId_) {
      otaError_ = "Target mismatch (expected " + String(deviceId_) + ", got " + targetDevice + ")";
      OtaMonitor.println("[OTA] Rejected: " + otaError_);
      return;
    }

    if (isArmedFn_ && isArmedFn_()) {
      otaError_ = "Outputs are ARMED — disarm device before flashing";
      OtaMonitor.println(F("[OTA] Rejected: outputs armed"));
      return;
    }

#if BATTERY_MONITOR_ENABLED
    float v = readBatteryVoltage();
    if (v < BATTERY_MIN_OTA_VOLTS) {
      otaError_ = "Battery too low for safe flash (" + String(v, 2) + "V < " + String(BATTERY_MIN_OTA_VOLTS, 2) + "V)";
      OtaMonitor.println("[OTA] Rejected: " + otaError_);
      return;
    }
#endif

    if (disarmFn_) disarmFn_();
    OtaMonitor.printf("[OTA] Starting flash: %s\n", upload.filename.c_str());

    String md5 = g_server.header("X-Firmware-MD5");
    if (md5.length() == 32) {
      Update.setMD5(md5.c_str());
      OtaMonitor.println("[OTA] MD5 check enabled: " + md5);
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      otaError_ = String("Update.begin failed: ") + Update.errorString();
      OtaMonitor.println("[OTA] " + otaError_);
      return;
    }
    otaAuthorized_ = true;

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    safeFeedWdt();
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    vTaskDelay(1);

    if (!otaAuthorized_) return;
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaError_ = String("Update.write failed: ") + Update.errorString();
      OtaMonitor.println("[OTA] " + otaError_);
      Update.abort();
      otaAuthorized_ = false;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    safeFeedWdt();
    if (!otaAuthorized_) return;

    if (Update.end(true)) {
      otaSuccess_ = true;
      OtaMonitor.printf("[OTA] Flash complete (%u bytes). Scheduling reboot.\n", upload.totalSize);
    } else {
      otaError_ = String("Update.end failed: ") + Update.errorString();
      OtaMonitor.println("[OTA] " + otaError_);
    }

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaAuthorized_ = false;
    otaError_ = "Upload aborted by client";
    OtaMonitor.println(F("[OTA] Upload aborted."));
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
