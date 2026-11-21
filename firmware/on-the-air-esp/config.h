#pragma once

#include <Arduino.h>

// =============================================================================
// On The Air ESP — Device Configuration (ESP32 NodeMCU-32S)
// =============================================================================
// Edit this file once per ESP32 NodeMCU-32S board.

// --- 1. Device Identity & Version --------------------------------------------
#define DEVICE_ID          "MTR-OTA"          // Unique ID per board/device
#define DEVICE_BOARD       "NODEMCU-32S"
#define FW_VERSION         "1.0.0"

// Embedded into the compiled .bin so flash.bat / ota_flash.py can verify
// you are flashing the right firmware onto the right device.
#define FW_META_STRING     "OTA_META:ID=" DEVICE_ID ";BOARD=" DEVICE_BOARD ";VER=" FW_VERSION ";"

// --- 2. Maintenance Mode Wi-Fi & Arduino IDE Credentials ---------------------
// Active ONLY in Maintenance Mode (OTA Mode).
// In Normal Run Mode, Wi-Fi & Bluetooth are 100% powered OFF.
#define OTA_AP_SSID        "MTR-OTA"          // Wi-Fi AP SSID
#define OTA_AP_PASS        "mtr-ota007"       // Wi-Fi WPA2 password (min 8 chars)
#define OTA_AP_CHANNEL     6                  // 1, 6, or 11

// Arduino IDE Network Port hostname & password + Web/CLI auth token
#define OTA_MDNS_HOST      "MTR-OTA"          // Appears in Arduino IDE Tools -> Port as "MTR-OTA"
#define OTA_AUTH_TOKEN     "mtr-ota007"       // Arduino IDE OTA password, Web Console & CLI token

// Optional: Connect to an existing team/lab Wi-Fi router first, then fall back to SoftAP.
#define OTA_USE_STA_FIRST  false
#define OTA_STA_SSID       "LAB-WIFI"
#define OTA_STA_PASS       "changeme123"
#define OTA_STA_TIMEOUT_MS 8000

// --- 3. Hardware Pins (ESP32 NodeMCU-32S) ------------------------------------
#define PIN_STATUS_LED     2    // Built-in blue LED on NodeMCU-32S (active HIGH)

// Maintenance / OTA Mode Switch: wire a switch or jumper from GPIO 33 to GND.
//   Closed (LOW) at boot -> Maintenance Mode (Wi-Fi ON, outputs disarmed)
//   Open  (HIGH) at boot -> Normal Run Mode  (Wi-Fi 100% OFF)
#define PIN_OTA_SWITCH     33

// Zero-wiring fallback: press onboard BOOT (GPIO 0) right AFTER power-on
// during the 2.5s fast-blink window. Set to 0 when using a physical switch on GPIO 33.
#define PIN_BOOT_BTN       0
#define BOOT_WINDOW_MS     2500

// Optional Battery Voltage Monitor (MUST use ADC1: GPIO 34, 35, 36, or 39).
#define BATTERY_MONITOR_ENABLED  false
#define PIN_BATTERY_ADC          34
#define BATTERY_DIVIDER_RATIO    4.0f
#define BATTERY_MIN_OTA_VOLTS    6.8f

// Actuator / output pins forced LOW at boot and during Maintenance Mode.
#define PIN_OUT_1          25
#define PIN_OUT_2          26
#define PIN_OUT_3          27
#define PIN_OUT_4          14

// --- 4. Watchdog & Automatic Anti-Brick Rollback -----------------------------
#define WDT_TIMEOUT_SEC          10    // Reboot if firmware freezes for 10 seconds
#define OTA_SELF_TEST_WINDOW_MS  4000  // Auto-confirm new firmware after 4s of stability
