// =============================================================================
// On The Air ESP — Main Arduino Sketch (ESP32 NodeMCU-32S Sumobot)
// =============================================================================
// HOW TO USE:
//   1. Edit YOUR Sumobot code right below in mySumoSetup() and mySumoLoop().
//   2. Change Wi-Fi name/password/pins in the "config.h" tab.
//   3. Upload once via USB (Board: NodeMCU-32S).
//   4. After that, flash wirelessly anytime:
//        • Inside Arduino IDE: Tools -> Port -> sumo-01 -> Upload
//        • Or double-click flash.bat (auto-compiles + flashes over Wi-Fi)
//        • Or connect to "SUMO-01-OTA" Wi-Fi (Pit Console pops up automatically!)
// =============================================================================

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "sumo_params.h"
#include "ota_manager.h"

SumoParamsStore g_paramsStore;
SumoParams      g_params;
OtaManager      g_ota;
static bool     g_motorsArmed = false;

// =============================================================================
// >>> PUT YOUR SUMOBOT CODE HERE <<<
// =============================================================================

// Called ONCE when booting into Match Mode (Wi-Fi & Bluetooth are already OFF).
void mySumoSetup() {
  Serial.println(F("[SUMO] Match Mode ready! Active NVS parameters:"));
  Serial.println("  " + SumoParamsStore::toJson(g_params));

  // Solid LED = Match Mode active
  digitalWrite(PIN_STATUS_LED, HIGH);

  // Initialize your sensors / PWM channels here.
  // Arm motors only when ready for the match:
  // g_motorsArmed = true;
}

// Called repeatedly during Match Mode.
// Use live-tuned values from the Pit Console directly:
//   g_params.searchSpeed      (0..255)
//   g_params.attackSpeed      (0..255)
//   g_params.turnSpeed        (0..255)
//   g_params.lineThreshold    (0..4095)
//   g_params.opponentDistCm   (5..150 cm)
//   g_params.startDelayMs     (0..10000 ms)
//   g_params.strategy         (0=Direct, 1=Circle, 2=Counter, 3=SideRush)
void mySumoLoop() {
  delay(10);
}

// Called immediately at boot & before any OTA flash to guarantee motors stop.
void safeDisarmMotors() {
  g_motorsArmed = false;
  pinMode(PIN_MOTOR_L_IN1, OUTPUT);
  pinMode(PIN_MOTOR_L_IN2, OUTPUT);
  pinMode(PIN_MOTOR_R_IN1, OUTPUT);
  pinMode(PIN_MOTOR_R_IN2, OUTPUT);
  digitalWrite(PIN_MOTOR_L_IN1, LOW);
  digitalWrite(PIN_MOTOR_L_IN2, LOW);
  digitalWrite(PIN_MOTOR_R_IN1, LOW);
  digitalWrite(PIN_MOTOR_R_IN2, LOW);
}

// =============================================================================
// SYSTEM INFRASTRUCTURE (No need to edit below this line)
// =============================================================================

static bool isRobotArmed() {
  return g_motorsArmed;
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
  safeDisarmMotors();

  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.printf(" %s (%s) — Firmware v%s\n", ROBOT_ID, ROBOT_BOARD, FW_VERSION);
  Serial.println(F("========================================"));

  initWatchdog();
  g_paramsStore.load(g_params);
  g_ota.begin(&g_params, &g_paramsStore, safeDisarmMotors, isRobotArmed);
  Serial.printf("[OTA] Boot state: %s\n", g_ota.getOtaStateString());

  if (g_ota.shouldEnterMaintenanceMode()) {
    g_ota.startMaintenanceMode();
  } else {
    g_ota.disableRadiosForMatch();
    mySumoSetup();
  }
}

void loop() {
  esp_task_wdt_reset();
  g_ota.serviceRollbackConfirmation();

  if (g_ota.isMaintenanceActive()) {
    g_ota.handleClient();
  } else {
    mySumoLoop();
  }
}
