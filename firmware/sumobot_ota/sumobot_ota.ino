// =============================================================================
// Sumobot OTA Infrastructure — Main Arduino Sketch (ESP32 NodeMCU-32S)
// =============================================================================
// Board in Arduino IDE:  Tools -> Board -> ESP32 Arduino -> NodeMCU-32S
// Zero external libraries required (uses built-in Arduino-ESP32 core only).
//
// Modes:
//   1. MAINTENANCE MODE (Pit Switch ON or BOOT button held in boot window):
//      - Motors strictly DISARMED.
//      - Wi-Fi SoftAP ON (default http://192.168.4.1).
//      - Wireless .bin OTA flashing + Live NVS parameter tuning + Rollback UI.
//   2. MATCH MODE (Default):
//      - Wi-Fi & Bluetooth 100% POWERED OFF (competition RF compliant, zero jitter).
//      - Loads latest tuned parameters from NVS flash and runs Sumobot logic.
// =============================================================================

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "sumo_params.h"
#include "ota_manager.h"

SumoParamsStore g_paramsStore;
SumoParams      g_params;
OtaManager      g_ota;

static bool g_motorsArmed = false;

// --- Watchdog Helper (Compatible with Arduino-ESP32 Core 2.x and 3.x) --------
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

// --- Motor Safety Callbacks --------------------------------------------------
static void safeDisarmMotors() {
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

static bool isRobotArmed() {
  return g_motorsArmed;
}

// --- Sumobot Match Mode Hooks (Put your Sumobot control code here!) ----------
static void sumoMatchSetup() {
  // Example: initialize line sensors, ToF/ultrasonic sensors, PWM motor channels,
  // and wait for your start module or g_params.startDelayMs.
  Serial.println(F("[SUMO] Match Mode initialized with NVS params:"));
  Serial.println("  " + SumoParamsStore::toJson(g_params));

  // Turn LED solid ON to indicate Match Mode ready
  digitalWrite(PIN_STATUS_LED, HIGH);

  // Arm motors ONLY when ready to compete
  // g_motorsArmed = true;
}

static void sumoMatchLoop() {
  // TODO: Replace with your Sumobot state machine using g_params:
  //   - g_params.searchSpeed
  //   - g_params.attackSpeed
  //   - g_params.turnSpeed
  //   - g_params.lineThreshold
  //   - g_params.opponentDistCm
  //   - g_params.startDelayMs
  //   - g_params.strategy
  delay(10);
}

// --- Arduino Entry Points ----------------------------------------------------
void setup() {
  // 1. Immediately disarm motors before anything else runs
  safeDisarmMotors();

  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.printf(" %s (%s) — Firmware v%s\n", ROBOT_ID, ROBOT_BOARD, FW_VERSION);
  Serial.println(F("========================================"));

  // 2. Enable Hardware Watchdog so crashes/hangs trigger reboot + auto-rollback
  initWatchdog();

  // 3. Load live-tuned Sumobot parameters from NVS flash
  g_paramsStore.load(g_params);

  // 4. Initialize OTA manager (does NOT turn Wi-Fi on yet)
  g_ota.begin(&g_params, &g_paramsStore, safeDisarmMotors, isRobotArmed);
  Serial.printf("[OTA] Current boot state: %s\n", g_ota.getOtaStateString());

  // 5. Decide Mode: Maintenance Mode (Wi-Fi ON) vs Match Mode (Wi-Fi 100% OFF)
  if (g_ota.shouldEnterMaintenanceMode()) {
    g_ota.startMaintenanceMode();
  } else {
    g_ota.disableRadiosForMatch();
    sumoMatchSetup();
  }
}

void loop() {
  // Feed Hardware Watchdog every iteration
  esp_task_wdt_reset();

  // Automatically confirm newly-flashed OTA firmware once stability window passes
  g_ota.serviceRollbackConfirmation();

  if (g_ota.isMaintenanceActive()) {
    g_ota.handleClient();
  } else {
    sumoMatchLoop();
  }
}
