#pragma once

#include <Arduino.h>

// =============================================================================
// Sumobot OTA Infrastructure — Robot Configuration (ESP32 NodeMCU-32S)
// =============================================================================
// Edit this file for each Sumobot in your fleet before flashing.

// --- Robot Identity & Firmware Version ---------------------------------------
#define ROBOT_ID           "SUMO-01"          // Unique ID per Sumobot (e.g. SUMO-01, SUMO-02)
#define ROBOT_BOARD        "NODEMCU-32S"
#define FW_VERSION         "1.0.0"

// Embedded metadata tag compiled into the .bin binary so tools/sumo_flash.py
// can verify you are flashing the right firmware onto the right Sumobot.
#define FW_META_STRING     "SUMO_META:ID=" ROBOT_ID ";BOARD=" ROBOT_BOARD ";VER=" FW_VERSION ";"

// --- Maintenance Mode Wi-Fi Settings -----------------------------------------
// SoftAP SSID broadcast ONLY when the robot enters Maintenance/Pit Mode.
// During Match Mode, Wi-Fi is completely powered OFF (competition RF compliant).
#define OTA_AP_SSID        "SUMO-01-OTA"      // Change per robot
#define OTA_AP_PASS        "sumo1234"         // Minimum 8 chars (WPA2). CHANGE BEFORE COMPETITION!
#define OTA_AP_CHANNEL     6                  // 1, 6, or 11 (non-overlapping 2.4 GHz channels)
#define OTA_MDNS_HOST      "sumo-01"          // Accessible at http://sumo-01.local (in STA mode)

// Optional: Connect to a team pit hotspot/router first, then fall back to SoftAP.
// Leave false for simplest zero-router pit operation (direct SoftAP at 192.168.4.1).
#define OTA_USE_STA_FIRST  false
#define OTA_STA_SSID       "TEAM-PIT-WIFI"
#define OTA_STA_PASS       "changeme123"
#define OTA_STA_TIMEOUT_MS 8000

// Security token required for firmware upload, parameter writes, and rollback.
// CHANGE THIS BEFORE COMPETITION!
#define OTA_AUTH_TOKEN     "sumo-pit-2026"

// --- Hardware Pins (ESP32 NodeMCU-32S) ---------------------------------------
// Status LED: NodeMCU-32S onboard blue LED is on GPIO 2 (active HIGH).
#define PIN_STATUS_LED     2

// Maintenance Mode Trigger #1: Dedicated physical Pit/OTA toggle switch or jumper.
// Wire a switch/jumper between PIN_OTA_SWITCH and GND (uses internal pull-up).
// Switch CLOSED (LOW) at boot -> Maintenance Mode (Wi-Fi ON, motors disarmed).
// Switch OPEN  (HIGH) at boot -> Match Mode (Wi-Fi 100% OFF).
#define PIN_OTA_SWITCH     33

// Maintenance Mode Trigger #2 (Zero extra hardware):
// Press the onboard BOOT button (GPIO 0) AFTER power-on during the short boot window.
// NOTE: Do NOT hold GPIO 0 while pressing EN/Reset (that enters ESP32 ROM flasher).
// Set BOOT_WINDOW_MS to 0 when using a dedicated PIN_OTA_SWITCH for instant match boot.
#define PIN_BOOT_BTN       0
#define BOOT_WINDOW_MS     2500

// Optional Battery Voltage Divider (MUST use ADC1: GPIO 34, 35, 36, or 39 —
// ADC2 cannot be read while Wi-Fi is active!).
// Set BATTERY_MONITOR_ENABLED to true once a divider is wired to PIN_BATTERY_ADC.
#define BATTERY_MONITOR_ENABLED  false
#define PIN_BATTERY_ADC          34
#define BATTERY_DIVIDER_RATIO    4.0f   // e.g. 30k / 10k divider -> (30+10)/10 = 4.0
#define BATTERY_MIN_OTA_VOLTS    6.8f   // Block OTA flash if battery below this (e.g. 2S LiPo)

// Example Motor Driver Pins (adjust to match your H-bridge / ESC wiring).
// All motor pins are forced LOW whenever the robot is disarmed or in Maintenance Mode.
#define PIN_MOTOR_L_IN1    25
#define PIN_MOTOR_L_IN2    26
#define PIN_MOTOR_R_IN1    27
#define PIN_MOTOR_R_IN2    14
#define PIN_MOTOR_STBY     12   // Optional standby/enable pin (avoid pulling HIGH at boot!)

// --- Watchdog & Anti-Brick Rollback Settings ---------------------------------
// Hardware Task Watchdog timeout (seconds). If firmware freezes, ESP32 reboots.
#define WDT_TIMEOUT_SEC          10

// How long a newly-flashed firmware must stay healthy (in ms) before it is
// permanently marked VALID and rollback is cancelled.
// If it crashes/reboots before this window finishes, the ESP32 bootloader
// automatically rolls back to the previous working firmware!
#define OTA_SELF_TEST_WINDOW_MS  4000
