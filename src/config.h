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

// Optional compile-time default Home/Lab Wi-Fi (runs Dual AP+STA simultaneously
// so your PC keeps internet while MTR-OTA hotspot ALSO stays active!).
// Tip: Leave "" here and configure your Wi-Fi directly in the Web Portal (saved in NVS)!
#define OTA_USE_STA_FIRST  false
#define OTA_STA_SSID       ""
#define OTA_STA_PASS       ""
#define OTA_STA_TIMEOUT_MS 3500

// --- 3. Operating Mode & Hardware Pins (ESP32 NodeMCU-32S) -------------------
// true  (default): ZERO buttons needed! MTR-OTA Wi-Fi, wireless upload, and
//                  OTA Monitor stay active in the background on Core 0 while
//                  your setup()/loop() run normally on Core 1.
// false          : Strict RF-Off mode (Wi-Fi stays OFF unless GPIO 33 switch
//                  is closed or BOOT is pressed during BOOT_WINDOW_MS).
#define OTA_ALWAYS_ON      true

#define PIN_STATUS_LED     2    // Built-in blue LED on NodeMCU-32S (active HIGH)

// Safe Recovery / Maintenance Switch: wire a switch or jumper from GPIO 33 to GND.
//   Closed (LOW) at boot -> Safe Recovery Mode (ONLY OTA runs; skips user sketch)
#define PIN_OTA_SWITCH     33

// Used only when OTA_ALWAYS_ON is false (or OnTheAirESP.setAlwaysOn(false)):
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

// --- 4. OTA Monitor (Wireless Serial Monitor over Web + Arduino IDE) ---------
#define OTA_MONITOR_PORT         23    // TCP port for Arduino IDE OTA Monitor & CLI
#define OTA_MONITOR_BUF_SIZE     4096  // Ring buffer size (bytes) replayed on connect

// --- 5. Watchdog & Automatic Anti-Brick Rollback -----------------------------
#define WDT_TIMEOUT_SEC          10    // Reboot if firmware freezes for 10 seconds
#define OTA_SELF_TEST_WINDOW_MS  4000  // Auto-confirm new firmware after 4s of stability
