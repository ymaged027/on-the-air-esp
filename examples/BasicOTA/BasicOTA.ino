// =============================================================================
// OnTheAirESP — Minimal 1-Line Example (ESP32 NodeMCU-32S)
// =============================================================================
// Credentials (defaults from library):
//   • Wi-Fi SSID / Arduino IDE Port : MTR-OTA
//   • Wi-Fi / IDE / Web Password    : mtr-ota007
//
// Enter Maintenance Mode:
//   Hold BOOT (GPIO 0) right after power-on while the blue LED fast-blinks,
//   OR connect a switch/jumper between GPIO 33 and GND.
// =============================================================================

#include <OnTheAirESP.h>

void setup() {
  OnTheAirESP.begin();

  // --- Your normal setup() code goes below (Wi-Fi & BT are 100% OFF) ---
  Serial.println(F("[APP] Running with NVS params:"));
  Serial.println("  " + AppParamsStore::toJson(OnTheAirESP.params()));
}

void loop() {
  // --- Your normal loop() code goes below ---
  // Live-tuned values: OnTheAirESP.params().speedMax, .sensorThreshold, .mode, etc.
  delay(10);
}
