#pragma once

#include <Arduino.h>
#include <Print.h>
#include "config.h"

// Callback invoked when a complete command line is received over OTA Monitor
// (either from Arduino IDE's Monitor input box or from the Web OTA Monitor).
using MonitorCommandCallback = void (*)(const String& line);

class OtaMonitorClass : public Print {
public:
  OtaMonitorClass();

  // Start the TCP OTA Monitor server (called automatically in Maintenance / Debug Mode).
  void beginServer(uint16_t port = OTA_MONITOR_PORT);

  // Stop the TCP OTA Monitor server (called in Normal Run Mode when Wi-Fi is OFF).
  void stopServer();

  // Service connected TCP clients and incoming commands. Called automatically by OnTheAirESP.
  void handle();

  // Print interface: writes to USB Serial, RAM ring buffer (Web OTA Monitor),
  // AND all connected TCP OTA Monitor clients (Arduino IDE Monitor).
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buffer, size_t size) override;

  // Clear the RAM log ring buffer.
  void clearBuffer();

  // Extract new log bytes written after `sinceOffset`, returning JSON-escaped chunk
  // and updating `nextOffset` to the current total byte count.
  String readLogChunk(uint32_t sinceOffset, uint32_t& nextOffset) const;

  // Push a command received from the Web OTA Monitor into the input handler.
  void injectCommand(const String& line);

  // Register a callback for incoming OTA Monitor lines (built-in or user commands).
  void onCommand(MonitorCommandCallback cb) { cmdCallback_ = cb; }

  // Read the last received line (polling API for user sketches).
  bool hasLine() const { return hasPendingLine_; }
  String readLine();

  uint32_t totalBytes() const { return totalBytes_; }
  bool isServerRunning() const { return serverRunning_; }

private:
  void appendToRing(uint8_t b);
  void replayRingToClient(uint8_t idx);
  void dispatchLine(const String& rawLine);

  uint8_t  ring_[OTA_MONITOR_BUF_SIZE];
  uint32_t totalBytes_     = 0;
  bool     serverRunning_  = false;
  uint16_t port_           = OTA_MONITOR_PORT;

  MonitorCommandCallback cmdCallback_ = nullptr;
  String   pendingLine_;
  bool     hasPendingLine_ = false;
  String   clientRx_[2];
};

extern OtaMonitorClass OtaMonitor;
