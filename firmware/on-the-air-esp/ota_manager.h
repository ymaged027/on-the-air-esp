#pragma once

#include <Arduino.h>
#include "sumo_params.h"

// Callback provided by the main sketch to guarantee motors are physically stopped.
using DisarmCallback = void (*)();
// Callback returning true if motors/robot are currently armed (blocks OTA!).
using IsArmedCallback = bool (*)();

class OtaManager {
public:
  // Initialize OTA manager (does NOT start Wi-Fi yet).
  void begin(SumoParams* params,
             SumoParamsStore* store,
             DisarmCallback disarmFn,
             IsArmedCallback isArmedFn);

  // Determine whether the robot should boot into Maintenance Mode (Wi-Fi ON)
  // or Match Mode (Wi-Fi 100% OFF). Checks PIN_OTA_SWITCH and optional BOOT_WINDOW_MS.
  bool shouldEnterMaintenanceMode();

  // Explicitly power down Wi-Fi & Bluetooth radios for competition Match Mode.
  void disableRadiosForMatch();

  // Start Maintenance Mode Wi-Fi (SoftAP or STA+SoftAP fallback) and HTTP Pit Server.
  void startMaintenanceMode();

  // Call every loop() iteration in Maintenance Mode.
  void handleClient();

  // Call every loop() iteration in BOTH modes!
  // After OTA_SELF_TEST_WINDOW_MS of stable runtime, marks a PENDING_VERIFY
  // firmware slot as VALID so the bootloader will not roll it back.
  void serviceRollbackConfirmation();

  // Read battery voltage (returns -1.0f if BATTERY_MONITOR_ENABLED is false).
  float readBatteryVoltage() const;

  // Human-readable current OTA state ("VALID", "PENDING_VERIFY", etc.).
  const char* getOtaStateString() const;

  // True if Maintenance Mode server is currently running.
  bool isMaintenanceActive() const { return maintenanceActive_; }

private:
  bool checkAuthAndReply();
  void registerRoutes();

  void handleRoot();
  void handleStatus();
  void handleGetParams();
  void handlePostParams();
  void handleResetParams();
  void handleOtaComplete();
  void handleOtaUpload();
  void handleRollback();
  void handleReboot();

  SumoParams* params_           = nullptr;
  SumoParamsStore* store_       = nullptr;
  DisarmCallback disarmFn_      = nullptr;
  IsArmedCallback isArmedFn_    = nullptr;

  bool maintenanceActive_       = false;
  bool rollbackConfirmed_       = false;
  uint32_t bootMs_              = 0;

  // OTA upload transaction state
  bool otaAuthorized_           = false;
  bool otaSuccess_              = false;
  String otaError_;

  // Deferred reboot/rollback so HTTP 200 JSON finishes transmitting first
  bool pendingReboot_           = false;
  bool pendingRollback_         = false;
  uint32_t actionAtMs_          = 0;
};
