// =============================================================================
// On The Air ESP — Main Arduino Sketch (ESP32 NodeMCU-32S)
// =============================================================================
// HOW TO USE:
//   1. Write YOUR application code right below in myAppSetup() and myAppLoop().
//   2. Wi-Fi & Arduino IDE credentials live in "config.h":
//        • Wi-Fi SSID / IDE Port : MTR-OTA
//        • Wi-Fi / IDE Password  : mtr-ota007
//   3. Upload once via USB (Board: NodeMCU-32S).
//   4. After that, flash wirelessly anytime:
//        • Inside Arduino IDE: Tools -> Port -> MTR-OTA -> Upload (pass: mtr-ota007)
//        • Or double-click flash.bat (auto-compiles + flashes over Wi-Fi)
//        • Or connect to "MTR-OTA" Wi-Fi (Web Console pops up automatically!)
// =============================================================================

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "app_params.h"
#include "ota_manager.h"

AppParamsStore g_paramsStore;
AppParams      g_params;
OtaManager     g_ota;
static bool    g_outputsArmed = false;

// =============================================================================
// >>> PUT YOUR APPLICATION CODE HERE <<<
// =============================================================================

// Called ONCE when booting into Normal Run Mode (Wi-Fi & Bluetooth are OFF).
void myAppSetup() {
  Serial.println(F("[APP] Normal Run Mode ready! Active NVS parameters:"));
  Serial.println("  " + AppParamsStore::toJson(g_params));

  // Solid LED = Normal Run Mode active
  digitalWrite(PIN_STATUS_LED, HIGH);

  // Initialize your sensors / actuators / PWM channels here.
  // Set g_outputsArmed = true once actuators are armed.
}

// Called repeatedly during Normal Run Mode.
// Read live-tuned values from the Web Console directly:
//   g_params.speedNormal      (0..255)
//   g_params.speedMax         (0..255)
//   g_params.turnSpeed        (0..255)
//   g_params.sensorThreshold  (0..4095)
//   g_params.rangeThreshold   (5..150 cm)
//   g_params.startDelayMs     (0..10000 ms)
//   g_params.mode             (0=Default, 1..3=Profiles)
void myAppLoop() {
  delay(10);
}

// Called immediately at boot & before any OTA flash to guarantee outputs stop.
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

// =============================================================================
// SYSTEM INFRASTRUCTURE (No need to edit below this line)
// =============================================================================

static bool isDeviceArmed() {
  return g_outputsArmed;
}

static void initWatchdog() {
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t cfg = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) {
    esp_task_wdt_init(&cfg);
  }
#else
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
  esp_task_wdt_add(NULL);
}

void setup() {
  safeDisarmOutputs();

  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.printf(" %s (%s) — Firmware v%s\n", DEVICE_ID, DEVICE_BOARD, FW_VERSION);
  Serial.println(F("========================================"));

  initWatchdog();
  g_paramsStore.load(g_params);
  g_ota.begin(&g_params, &g_paramsStore, safeDisarmOutputs, isDeviceArmed);
  Serial.printf("[OTA] Boot state: %s\n", g_ota.getOtaStateString());

  if (g_ota.shouldEnterMaintenanceMode()) {
    g_ota.startMaintenanceMode();
  } else {
    g_ota.disableRadiosForRunMode();
    myAppSetup();
  }
}

void loop() {
  esp_task_wdt_reset();
  g_ota.serviceRollbackConfirmation();

  if (g_ota.isMaintenanceActive()) {
    g_ota.handleClient();
  } else {
    myAppLoop();
  }
}
