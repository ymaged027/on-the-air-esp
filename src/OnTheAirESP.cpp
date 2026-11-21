#include "OnTheAirESP.h"
#include <esp_task_wdt.h>

OnTheAirEspFacade OnTheAirESP;

namespace {
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
  esp_task_wdt_add(NULL);
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

void OnTheAirEspFacade::begin(unsigned long serialBaud) {
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
  } else {
    ota_.disableRadiosForRunMode();
    digitalWrite(PIN_STATUS_LED, HIGH);
  }
}

bool OnTheAirEspFacade::handle() {
  esp_task_wdt_reset();
  ota_.serviceRollbackConfirmation();

  if (ota_.isMaintenanceActive()) {
    ota_.handleClient();
    return true;
  }
  return false;
}
