#pragma once

#include <Arduino.h>
#include "sumo_params.h"

using DisarmCallback  = void (*)();
using IsArmedCallback = bool (*)();

class OtaManager {
public:
  void begin(SumoParams* params,
             SumoParamsStore* store,
             DisarmCallback disarmFn,
             IsArmedCallback isArmedFn);

  bool shouldEnterMaintenanceMode();
  void disableRadiosForMatch();
  void startMaintenanceMode();
  void handleClient();
  void serviceRollbackConfirmation();

  float readBatteryVoltage() const;
  const char* getOtaStateString() const;
  bool isMaintenanceActive() const { return maintenanceActive_; }

private:
  bool isAuthRequired() const;
  bool isTokenValid(const String& provided) const;
  bool checkAuthAndReply();
  void registerRoutes();
  void startArduinoOta();

  void handleRoot();
  void handleStatus();
  void handleGetParams();
  void handlePostParams();
  void handleResetParams();
  void handleOtaComplete();
  void handleOtaUpload();
  void handleRollback();
  void handleReboot();

  SumoParams* params_        = nullptr;
  SumoParamsStore* store_    = nullptr;
  DisarmCallback disarmFn_   = nullptr;
  IsArmedCallback isArmedFn_ = nullptr;

  bool maintenanceActive_    = false;
  bool dnsActive_            = false;
  bool rollbackConfirmed_    = false;
  uint32_t bootMs_           = 0;

  bool otaAuthorized_        = false;
  bool otaSuccess_           = false;
  String otaError_;

  bool pendingReboot_        = false;
  bool pendingRollback_      = false;
  uint32_t actionAtMs_       = 0;
};
