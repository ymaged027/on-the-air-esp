#include "OnTheAirESP.h"
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

OnTheAirEspFacade OnTheAirESP;

namespace {
esp_timer_handle_t g_rollbackTimer = nullptr;
TaskHandle_t       g_otaBgTask     = nullptr;

void rollbackTimerCallback(void* arg) {
  auto* mgr = static_cast<OtaManager*>(arg);
  if (mgr) {
    mgr->confirmRollbackNow();
  }
}

void otaBackgroundTask(void* arg) {
  auto* mgr = static_cast<OtaManager*>(arg);
  for (;;) {
    if (mgr) {
      mgr->handleClient();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void startBackgroundOtaTask(OtaManager* mgr) {
  if (g_otaBgTask != nullptr) return;
  // Pin OTA HTTP/mDNS/ArduinoOTA/Monitor server to Core 0 (alongside Wi-Fi stack)
  // so user setup()/loop() on Core 1 run smoothly without calling handle() or pressing BOOT!
  xTaskCreatePinnedToCore(
      otaBackgroundTask,
      "ota_bg",
      8192,
      mgr,
      1,
      &g_otaBgTask,
      0);
}

void initWatchdog() {
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t cfg = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) {
    esp_task_wdt_init(&cfg);
  }
#else
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
  enableLoopWDT();
}

void scheduleBackgroundRollbackConfirm(OtaManager* mgr) {
  if (g_rollbackTimer) return;
  esp_timer_create_args_t args = {};
  args.callback = &rollbackTimerCallback;
  args.arg      = mgr;
  args.name     = "ota_confirm";
  if (esp_timer_create(&args, &g_rollbackTimer) == ESP_OK) {
    // Fires strictly AFTER WDT_TIMEOUT_SEC (12s > 10s) so any crash or infinite
    // loop in setup()/loop() triggers WDT reboot + automatic rollback FIRST!
    const uint64_t delayUs = (uint64_t)(WDT_TIMEOUT_SEC + 2) * 1000000ULL;
    esp_timer_start_once(g_rollbackTimer, delayUs);
  }
}
}  // namespace

void OnTheAirEspFacade::setCredentials(const char* ssid,
                                       const char* password,
                                       const char* ideHostname) {
  ota_.setCredentials(ssid, password, ideHostname);
}

void OnTheAirEspFacade::setExternalWiFi(const char* staSsid, const char* staPassword) {
  ota_.setExternalWiFi(staSsid, staPassword);
}

void OnTheAirEspFacade::setDeviceId(const char* deviceId, const char* version) {
  ota_.setDeviceId(deviceId, version);
}

void OnTheAirEspFacade::setDisarmCallback(DisarmCallback fn) {
  disarmFn_ = fn;
}

void OnTheAirEspFacade::setArmedCallback(IsArmedCallback fn) {
  isArmedFn_ = fn;
}

void OnTheAirEspFacade::begin(unsigned long serialBaud, bool blockInOtaMode) {
  if (disarmFn_) {
    disarmFn_();
  }

  if (serialBaud > 0) {
    Serial.begin(serialBaud);
    delay(150);
  }

  OtaMonitor.println();
  OtaMonitor.println(F("========================================"));
  OtaMonitor.printf(" %s (%s) — Firmware v%s\n",
                    ota_.getDeviceId(), DEVICE_BOARD, ota_.getFirmwareVersion());
  OtaMonitor.println(F("========================================"));

  initWatchdog();
  paramsStore_.load(params_);
  ota_.begin(&params_, &paramsStore_, disarmFn_, isArmedFn_);
  OtaMonitor.printf("[OTA] Boot state: %s\n", ota_.getOtaStateString());

  // 1. Physical GPIO 33 recovery switch always enters isolated Safe Maintenance Mode
  if (ota_.isRecoverySwitchActive()) {
    OtaMonitor.println(F("[OTA] Recovery switch ACTIVE -> Safe Maintenance Mode (user sketch paused)."));
    ota_.startMaintenanceMode();
    if (blockInOtaMode) {
      for (;;) {
        feedLoopWDT();
        ota_.serviceRollbackConfirmation();
        ota_.handleClient();
        delay(2);
      }
    }
    return;
  }

  // 2. Always-On Mode (default): ZERO buttons needed!
  //    Starts MTR-OTA Wi-Fi, wireless flashing & OTA Monitor on Core 0 immediately,
  //    then returns straight into your setup() & loop() on Core 1.
  if (alwaysOn_) {
    OtaMonitor.println(F("[OTA] Always-On Mode: MTR-OTA & OTA Monitor active on Core 0 (no buttons needed!)."));
    ota_.startWirelessServices(false);
    startBackgroundOtaTask(&ota_);
    scheduleBackgroundRollbackConfirm(&ota_);
    return;
  }

  // 3. Strict RF-Off Mode (opt-in via setAlwaysOn(false)):
  //    Wi-Fi stays 100% OFF unless BOOT is held during the boot window.
  if (ota_.shouldEnterMaintenanceMode()) {
    ota_.startMaintenanceMode();
    if (blockInOtaMode) {
      for (;;) {
        feedLoopWDT();
        ota_.serviceRollbackConfirmation();
        ota_.handleClient();
        delay(2);
      }
    }
  } else {
    ota_.disableRadiosForRunMode();
    digitalWrite(PIN_STATUS_LED, HIGH);
    scheduleBackgroundRollbackConfirm(&ota_);
  }
}

bool OnTheAirEspFacade::handle() {
  feedLoopWDT();
  ota_.serviceRollbackConfirmation();

  // Only service inline when no background Core 0 task is running
  if (g_otaBgTask == nullptr && ota_.isWirelessActive()) {
    ota_.handleClient();
  }
  return ota_.isMaintenanceActive();
}
