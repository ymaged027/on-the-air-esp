// =============================================================================
// OnTheAirESP — Minimal 1-Line, Zero-Button Example (ESP32 NodeMCU-32S)
// =============================================================================
// Credentials (defaults from library):
//   • Wi-Fi SSID / Arduino IDE Port : MTR-OTA
//   • Wi-Fi / IDE / Web Password    : mtr-ota007
//
// Zero buttons to press! MTR-OTA Wi-Fi, wireless upload, and OTA Monitor run
// automatically in the background on Core 0 while your sketch runs on Core 1.
// =============================================================================

#include <OnTheAirESP.h>

void setup() {
  OnTheAirESP.begin();

  // --- Your normal setup() code goes below ---
  OtaMonitor.println(F("[APP] Ready! Live NVS params:"));
  OtaMonitor.println("  " + AppParamsStore::toJson(OnTheAirESP.params()));
}

void loop() {
  // --- Your normal loop() code goes below ---
  // Print wirelessly anytime (visible in Arduino IDE Monitor + http://192.168.4.1):
  //   OtaMonitor.println("Hello from ESP32!");
  delay(10);
}
