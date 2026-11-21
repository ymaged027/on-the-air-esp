#pragma once

#include <Arduino.h>

// =============================================================================
// On The Air ESP — Robot Configuration (ESP32 NodeMCU-32S)
// =============================================================================
// Edit this file once for each Sumobot in your fleet.

// --- 1. Robot Identity & Version ---------------------------------------------
#define ROBOT_ID           "SUMO-01"          // Unique ID per Sumobot (SUMO-01, SUMO-02...)
#define ROBOT_BOARD        "NODEMCU-32S"
#define FW_VERSION         "1.0.0"

// Embedded into the compiled .bin so flash.bat / sumo_flash.py can block
// accidental flashing of the wrong Sumobot's firmware.
#define FW_META_STRING     "SUMO_META:ID=" ROBOT_ID ";BOARD=" ROBOT_BOARD ";VER=" FW_VERSION ";"

// --- 2. Maintenance Mode Wi-Fi Settings --------------------------------------
// Active ONLY in Maintenance Mode (Pit Mode).
// In Match Mode, Wi-Fi & Bluetooth are 100% powered OFF.
#define OTA_AP_SSID        "SUMO-01-OTA"      // Wi-Fi name shown on phone/laptop
#define OTA_AP_PASS        "sumo1234"         // Min 8 chars (WPA2). CHANGE BEFORE COMPETITION!
#define OTA_AP_CHANNEL     6                  // 1, 6, or 11
#define OTA_MDNS_HOST      "sumo-01"          // Shows up in Arduino IDE Ports as "sumo-01"

// Optional: Connect to a team pit hotspot/router first, then fall back to SoftAP.
#define OTA_USE_STA_FIRST  false
#define OTA_STA_SSID       "TEAM-PIT-WIFI"
#define OTA_STA_PASS       "changeme123"
#define OTA_STA_TIMEOUT_MS 8000

// Security token for Web Console, CLI, and Arduino IDE Network Upload.
// Tip: Set to "" if you want the WPA2 Wi-Fi password alone to protect flashing.
// CHANGE BEFORE COMPETITION!
#define OTA_AUTH_TOKEN     "sumo-pit-2026"

// --- 3. Hardware Pins (ESP32 NodeMCU-32S) ------------------------------------
#define PIN_STATUS_LED     2    // Built-in blue LED on NodeMCU-32S (active HIGH)

// Pit / OTA Mode Switch: wire a switch or jumper from GPIO 33 to GND.
//   Closed (LOW) at boot -> Maintenance Mode (Wi-Fi ON, motors disarmed)
//   Open  (HIGH) at boot -> Match Mode (Wi-Fi 100% OFF)
#define PIN_OTA_SWITCH     33

// Zero-wiring fallback: press onboard BOOT (GPIO 0) right AFTER power-on
// during the 2.5s fast-blink window. Set to 0 if using a physical switch on GPIO 33.
#define PIN_BOOT_BTN       0
#define BOOT_WINDOW_MS     2500

// Optional Battery Monitor (MUST be ADC1: GPIO 34, 35, 36, or 39).
#define BATTERY_MONITOR_ENABLED  false
#define PIN_BATTERY_ADC          34
#define BATTERY_DIVIDER_RATIO    4.0f
#define BATTERY_MIN_OTA_VOLTS    6.8f

// Motor driver pins (forced LOW at boot and during Maintenance Mode).
#define PIN_MOTOR_L_IN1    25
#define PIN_MOTOR_L_IN2    26
#define PIN_MOTOR_R_IN1    27
#define PIN_MOTOR_R_IN2    14

// --- 4. Watchdog & Automatic Anti-Brick Rollback -----------------------------
#define WDT_TIMEOUT_SEC          10    // Reboot if code freezes for 10 seconds
#define OTA_SELF_TEST_WINDOW_MS  4000  // Auto-confirm new firmware after 4s of stability
