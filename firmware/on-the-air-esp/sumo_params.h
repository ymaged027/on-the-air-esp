#pragma once

#include <Arduino.h>

// =============================================================================
// Live-Tunable Sumobot Parameters (Stored in ESP32 NVS Flash)
// =============================================================================
// Tweak speeds, thresholds, and strategies over Wi-Fi in 2 seconds without
// recompiling or risking a firmware flash! Values survive reboots and OTA updates.

enum SumoStrategy : uint8_t {
  STRATEGY_DIRECT_ATTACK = 0,
  STRATEGY_CIRCLE_SEARCH = 1,
  STRATEGY_WAIT_COUNTER  = 2,
  STRATEGY_SIDE_RUSH     = 3
};

struct SumoParams {
  uint8_t  searchSpeed;      // 0..255 PWM during opponent search
  uint8_t  attackSpeed;      // 0..255 PWM during full-power push
  uint8_t  turnSpeed;        // 0..255 PWM during spin/align
  uint16_t lineThreshold;    // 0..4095 ADC white-edge threshold
  uint16_t opponentDistCm;   // 5..150 cm detection threshold
  uint16_t startDelayMs;     // 0..10000 ms (standard competition = 5000 ms)
  uint8_t  strategy;         // SumoStrategy enum (0..3)
};

class SumoParamsStore {
public:
  // Load parameters from NVS (falls back to safe defaults if unset).
  bool load(SumoParams& out);

  // Validate, clamp to safe bounds, and save parameters to NVS.
  bool save(const SumoParams& in, SumoParams& savedOut);

  // Restore factory defaults in NVS.
  bool resetDefaults(SumoParams& out);

  // Fill struct with compile-time defaults.
  static SumoParams defaults();

  // Serialize parameters to a JSON object string.
  static String toJson(const SumoParams& p);
};
