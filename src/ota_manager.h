#pragma once

#include <Arduino.h>
#include "config.h"
#include "app_params.h"

using DisarmCallback  = void (*)();
using IsArmedCallback = bool (*)();

class OtaManager {
public:
  // Optional runtime overrides (call before begin() / startMaintenanceMode()).
  void setCredentials(const char* ssid, const char* password, const char* ideHostname = nullptr);
  void setDeviceId(const char* deviceId, const char* version = FW_VERSION);

  void begin(AppParams* params,
             AppParamsStore* store,
             DisarmCallback disarmFn,
             IsArmedCallback isArmedFn);

  bool shouldEnterMaintenanceMode();
  void disableRadiosForRunMode();
  void startMaintenanceMode();
  void handleClient();
  void serviceRollbackConfirmation();
  void confirmRollbackNow();

  float readBatteryVoltage() const;
  const char* getOtaStateString() const;
  const char* getDeviceId() const { return deviceId_; }
  const char* getFirmwareVersion() const { return fwVersion_; }
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

  const char* deviceId_      = DEVICE_ID;
  const char* fwVersion_     = FW_VERSION;
  const char* apSsid_        = OTA_AP_SSID;
  const char* apPass_        = OTA_AP_PASS;
  const char* mdnsHost_      = OTA_MDNS_HOST;
  const char* authToken_     = OTA_AUTH_TOKEN;

  AppParams* params_         = nullptr;
  AppParamsStore* store_     = nullptr;
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
