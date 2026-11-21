#include "OnTheAirESP.h"
#include <esp_task_wdt.h>
#include <esp_timer.h>

OnTheAirEspFacade OnTheAirESP;

namespace {
esp_timer_handle_t g_rollbackTimer = nullptr;

void rollbackTimerCallback(void* arg) {
  auto* mgr = static_cast<OtaManager*>(arg);
  if (mgr) {
    mgr->confirmRollbackNow();
  }
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
  // Use Arduino's enableLoopWDT() so main.cpp automatically feeds the watchdog
  // on every loop() iteration — prevents false WDT crashes if user omits handle(),
  // while still catching real infinite loops / freezes and triggering auto-rollback!
  enableLoopWDT();
}

void scheduleBackgroundRollbackConfirm(OtaManager* mgr) {
  if (g_rollbackTimer) return;
  esp_timer_create_args_t args = {};
  args.callback = &rollbackTimerCallback;
  args.arg      = mgr;
  args.name     = "ota_confirm";
  if (esp_timer_create(&args, &g_rollbackTimer) == ESP_OK) {
    // Fire strictly AFTER WDT_TIMEOUT_SEC (12s > 10s) so any infinite loop in
    // setup()/loop() gets caught by the watchdog and rolled back FIRST!
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

  Serial.println();
  Serial.println(F("========================================"));
  Serial.printf(" %s (%s) — Firmware v%s\n",
                ota_.getDeviceId(), DEVICE_BOARD, ota_.getFirmwareVersion());
  Serial.println(F("========================================"));

  initWatchdog();
  paramsStore_.load(params_);
  ota_.begin(&params_, &paramsStore_, disarmFn_, isArmedFn_);
  Serial.printf("[OTA] Boot state: %s\n", ota_.getOtaStateString());

  if (ota_.shouldEnterMaintenanceMode()) {
    ota_.startMaintenanceMode();
    if (blockInOtaMode) {
      // Dedicated Maintenance Loop: keeps WDT fed, serves Captive Portal,
      // Web Console, and Arduino IDE Network Port indefinitely without ever
      // running user setup()/loop() code!
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

  if (ota_.isMaintenanceActive()) {
    ota_.handleClient();
    return true;
  }
  return false;
}
