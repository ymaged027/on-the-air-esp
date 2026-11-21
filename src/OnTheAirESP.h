#pragma once

#include <Arduino.h>
#include "config.h"
#include "app_params.h"
#include "ota_manager.h"

// =============================================================================
// OnTheAirESP — 1-Line Plug-and-Play OTA Facade for ANY ESP32 Arduino Sketch
// =============================================================================
// Minimal usage (literally ONE line!):
//   #include <OnTheAirESP.h>
//
//   void setup() {
//     OnTheAirESP.begin();
//     // your normal setup() code (never runs in Maintenance Mode!)
//   }
//
//   void loop() {
//     // your normal loop() code (WDT & rollback handled automatically!)
//   }
// =============================================================================

class OnTheAirEspFacade {
public:
  // Optional overrides (call BEFORE begin()). Defaults are MTR-OTA / mtr-ota007.
  void setCredentials(const char* ssid, const char* password, const char* ideHostname = nullptr);
  void setDeviceId(const char* deviceId, const char* version = FW_VERSION);
  void setDisarmCallback(DisarmCallback fn);
  void setArmedCallback(IsArmedCallback fn);

  // Initialize Hardware Watchdog, load live NVS params, and enter either:
  //   - Maintenance Mode (Wi-Fi AP + Captive Portal + Arduino IDE OTA).
  //     When blockInOtaMode=true (default), begin() stays in its built-in
  //     Maintenance Loop forever until flashed or rebooted, so your normal
  //     setup()/loop() never accidentally execute and WDT never triggers!
  //   - Normal Run Mode (Wi-Fi & Bluetooth 100% OFF, enableLoopWDT() active).
  // Pass serialBaud = 0 if you call Serial.begin() yourself.
  void begin(unsigned long serialBaud = 115200, bool blockInOtaMode = true);

  // Optional in loop() (not required when blockInOtaMode=true, kept for compatibility
  // and for non-blocking mode).
  bool handle();

  // True when running in Maintenance / OTA Mode (Wi-Fi active, outputs disarmed).
  bool isOtaMode() const { return ota_.isMaintenanceActive(); }

  // Access live-tuned parameters stored in NVS flash.
  AppParams& params() { return params_; }
  const AppParams& params() const { return params_; }
  AppParamsStore& paramsStore() { return paramsStore_; }

  // Direct access to the underlying OtaManager if needed.
  OtaManager& manager() { return ota_; }

private:
  AppParamsStore paramsStore_;
  AppParams      params_;
  OtaManager     ota_;
  DisarmCallback disarmFn_   = nullptr;
  IsArmedCallback isArmedFn_ = nullptr;
};

extern OnTheAirEspFacade OnTheAirESP;
