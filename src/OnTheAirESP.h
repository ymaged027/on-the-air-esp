#pragma once

#include <Arduino.h>
#include "config.h"
#include "app_params.h"
#include "ota_manager.h"
#include "ota_monitor.h"

// =============================================================================
// OnTheAirESP — 1-Line, Zero-Button OTA + OTA Monitor for ESP32 NodeMCU-32S
// =============================================================================
// Minimal usage (NO buttons to press ever!):
//   #include <OnTheAirESP.h>
//
//   void setup() {
//     OnTheAirESP.begin();
//     // your normal setup() code...
//   }
//
//   void loop() {
//     // your normal loop() code...
//     // Print wirelessly anytime: OtaMonitor.println("hello!");
//   }
//
// By default (OTA_ALWAYS_ON = true), MTR-OTA Wi-Fi, wireless flashing, and
// OTA Monitor run automatically in the background on Core 0 while your sketch
// runs at full speed on Core 1 — zero button pressing required!
//
// Need Wi-Fi completely OFF during a strict RF-off run?
//   Call OnTheAirESP.setAlwaysOn(false); before begin().
// =============================================================================

class OnTheAirEspFacade {
public:
  void setCredentials(const char* ssid, const char* password, const char* ideHostname = nullptr);
  void setDeviceId(const char* deviceId, const char* version = FW_VERSION);
  void setDisarmCallback(DisarmCallback fn);
  void setArmedCallback(IsArmedCallback fn);

  // true (default): Always-On OTA + OTA Monitor on Core 0 — no BOOT button needed!
  // false         : Wi-Fi 100% OFF in Normal Run Mode unless BOOT / GPIO 33 is triggered.
  void setAlwaysOn(bool enabled) { alwaysOn_ = enabled; }

  // Alias kept for backwards compatibility.
  void enableDebugMode(bool enabled = true) { alwaysOn_ = enabled; }

  void begin(unsigned long serialBaud = 115200, bool blockInOtaMode = true);
  bool handle();

  bool isOtaMode() const { return ota_.isMaintenanceActive(); }
  AppParams& params() { return params_; }
  const AppParams& params() const { return params_; }
  AppParamsStore& paramsStore() { return paramsStore_; }
  OtaManager& manager() { return ota_; }

private:
  AppParamsStore paramsStore_;
  AppParams      params_;
  OtaManager     ota_;
  DisarmCallback disarmFn_   = nullptr;
  IsArmedCallback isArmedFn_ = nullptr;
  bool           alwaysOn_   = OTA_ALWAYS_ON;
};

extern OnTheAirEspFacade OnTheAirESP;
