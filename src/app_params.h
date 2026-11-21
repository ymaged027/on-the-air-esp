#pragma once

#include <Arduino.h>

// =============================================================================
// Live-Tunable Device Parameters (Stored in ESP32 NVS Flash)
// =============================================================================
// Tweak speeds, thresholds, delays, and operating mode over Wi-Fi in 2 seconds
// without recompiling or flashing. Values survive reboots and OTA updates.

enum OperatingMode : uint8_t {
  MODE_DEFAULT  = 0,
  MODE_PROFILE1 = 1,
  MODE_PROFILE2 = 2,
  MODE_PROFILE3 = 3
};

struct AppParams {
  uint8_t  speedNormal;      // 0..255 PWM base/cruise speed
  uint8_t  speedMax;         // 0..255 PWM maximum/boost speed
  uint8_t  turnSpeed;        // 0..255 PWM turn/maneuver speed
  uint16_t sensorThreshold;  // 0..4095 ADC sensor threshold
  uint16_t rangeThreshold;   // 5..150 cm distance threshold
  uint16_t startDelayMs;     // 0..10000 ms startup delay
  uint8_t  mode;             // OperatingMode enum (0..3)
};

class AppParamsStore {
public:
  // Load parameters from NVS (falls back to safe defaults if unset).
  bool load(AppParams& out);

  // Validate, clamp to safe bounds, and save parameters to NVS.
  bool save(const AppParams& in, AppParams& savedOut);

  // Restore factory defaults in NVS.
  bool resetDefaults(AppParams& out);

  // Fill struct with compile-time defaults.
  static AppParams defaults();

  // Serialize parameters to a JSON object string.
  static String toJson(const AppParams& p);
};
