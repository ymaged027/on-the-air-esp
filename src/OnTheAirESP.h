#pragma once

#include <Arduino.h>
#include "config.h"
#include "app_params.h"
#include "ota_manager.h"
#include "ota_monitor.h"

// =============================================================================
// OnTheAirESP — 1-Line Plug-and-Play OTA + OTA Monitor for ESP32 NodeMCU-32S
// =============================================================================
// Minimal usage (literally ONE line!):
//   #include <OnTheAirESP.h>
//
//   void setup() {
//     OnTheAirESP.begin();
//     // your normal setup() code...
//   }
//
//   void loop() {
//     // your normal loop() code...
//   }
//
// Wireless debugging tip:
//   Call OnTheAirESP.enableDebugMode() BEFORE begin() and print with
//   OtaMonitor.println("...") to stream live logs over Wi-Fi into both
//   Arduino IDE's Serial Monitor AND the Web OTA Monitor (http://192.168.4.1)!
// =============================================================================

class OnTheAirEspFacade {
public:
  void setCredentials(const char* ssid, const char* password, const char* ideHostname = nullptr);
  void setDeviceId(const char* deviceId, const char* version = FW_VERSION);
  void setDisarmCallback(DisarmCallback fn);
  void setArmedCallback(IsArmedCallback fn);

  // Optional: keep MTR-OTA Wi-Fi + OTA Monitor + wireless flashing running
  // alongside your own setup()/loop() for live wireless debugging (call handle() in loop).
  // Default is false (Wi-Fi & Bluetooth 100% OFF in Normal Run Mode).
  void enableDebugMode(bool enabled = true) { debugMode_ = enabled; }

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
  bool           debugMode_  = false;
};

extern OnTheAirEspFacade OnTheAirESP;
