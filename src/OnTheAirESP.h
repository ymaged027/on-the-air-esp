#pragma once

#include <Arduino.h>
#include "config.h"
#include "app_params.h"
#include "ota_manager.h"

// =============================================================================
// OnTheAirESP — 3-Line Plug-and-Play OTA Facade for ANY ESP32 Arduino Sketch
// =============================================================================
// Minimal usage:
//   #include <OnTheAirESP.h>
//
//   void setup() {
//     OnTheAirESP.begin();
//     if (OnTheAirESP.isOtaMode()) return;
//     // your normal setup() code...
//   }
//
//   void loop() {
//     if (OnTheAirESP.handle()) return;
//     // your normal loop() code...
//   }
// =============================================================================

class OnTheAirEspFacade {
public:
  // Optional overrides (call BEFORE begin()). Defaults are MTR-OTA / mtr-ota007.
  void setCredentials(const char* ssid, const char* password, const char* ideHostname = nullptr);
  void setDeviceId(const char* deviceId, const char* version = FW_VERSION);
  void setDisarmCallback(DisarmCallback fn);
  void setArmedCallback(IsArmedCallback fn);

  // Initialize watchdog, load live NVS params, and enter either:
  //   - Maintenance Mode (Wi-Fi AP + Captive Portal + Arduino IDE OTA)
  //   - Normal Run Mode  (Wi-Fi & Bluetooth 100% OFF)
  // Pass serialBaud = 0 if you call Serial.begin() yourself.
  void begin(unsigned long serialBaud = 115200);

  // Call at the top of loop().
  // Feeds Hardware Watchdog, confirms OTA rollback after stability window,
  // and services HTTP/Captive Portal/ArduinoOTA when in Maintenance Mode.
  // Returns true if in Maintenance Mode (so your loop can simply `return;`).
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
  DisarmCallback disarmFn_  = nullptr;
  IsArmedCallback isArmedFn_ = nullptr;
};

extern OnTheAirEspFacade OnTheAirESP;
