// =============================================================================
// On The Air ESP — Starter Sketch (ESP32 NodeMCU-32S)
// =============================================================================
// First-time setup: just double-click setup-usb.bat in the repo root!
// Wireless update:  save this file, connect to "MTR-OTA" (pass: mtr-ota007),
//                   and double-click flash.bat OR upload via Arduino IDE Port "MTR-OTA".
// =============================================================================

#include <OnTheAirESP.h>

static bool g_outputsArmed = false;

// Called automatically at boot & before any OTA flash to guarantee outputs stop.
void safeDisarmOutputs() {
  g_outputsArmed = false;
  pinMode(PIN_OUT_1, OUTPUT);
  pinMode(PIN_OUT_2, OUTPUT);
  pinMode(PIN_OUT_3, OUTPUT);
  pinMode(PIN_OUT_4, OUTPUT);
  digitalWrite(PIN_OUT_1, LOW);
  digitalWrite(PIN_OUT_2, LOW);
  digitalWrite(PIN_OUT_3, LOW);
  digitalWrite(PIN_OUT_4, LOW);
}

bool isDeviceArmed() {
  return g_outputsArmed;
}

void setup() {
  OnTheAirESP.setDisarmCallback(safeDisarmOutputs);
  OnTheAirESP.setArmedCallback(isDeviceArmed);
  OnTheAirESP.begin();

  // --- PUT YOUR SETUP CODE BELOW (Wi-Fi & Bluetooth are 100% OFF) ---
  Serial.println(F("[APP] Normal Run Mode ready! Active NVS parameters:"));
  Serial.println("  " + AppParamsStore::toJson(OnTheAirESP.params()));
}

void loop() {
  // --- PUT YOUR LOOP CODE BELOW ---
  // Live-tuned params available anytime:
  //   OnTheAirESP.params().speedNormal      (0..255)
  //   OnTheAirESP.params().speedMax         (0..255)
  //   OnTheAirESP.params().turnSpeed        (0..255)
  //   OnTheAirESP.params().sensorThreshold  (0..4095)
  //   OnTheAirESP.params().rangeThreshold   (5..150 cm)
  //   OnTheAirESP.params().startDelayMs     (0..10000 ms)
  //   OnTheAirESP.params().mode             (0..3)
  delay(10);
}
