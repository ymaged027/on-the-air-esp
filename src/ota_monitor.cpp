#include "ota_monitor.h"
#include <WiFi.h>

OtaMonitorClass OtaMonitor;

namespace {
constexpr uint8_t MAX_TCP_CLIENTS = 2;
WiFiServer* g_monServer = nullptr;
WiFiClient  g_clients[MAX_TCP_CLIENTS];
}  // namespace

OtaMonitorClass::OtaMonitorClass() {
  memset(ring_, 0, sizeof(ring_));
}

void OtaMonitorClass::ensureMutex() const {
  if (!mutex_) {
    mutex_ = xSemaphoreCreateRecursiveMutex();
  }
}

void OtaMonitorClass::lock() const {
  ensureMutex();
  if (mutex_) {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
  }
}

void OtaMonitorClass::unlock() const {
  if (mutex_) {
    xSemaphoreGiveRecursive(mutex_);
  }
}

void OtaMonitorClass::beginServer(uint16_t port) {
  lock();
  stopServer();
  port_ = port;
  g_monServer = new WiFiServer(port_);
  g_monServer->begin();
  g_monServer->setNoDelay(true);
  serverRunning_ = true;
  unlock();
}

void OtaMonitorClass::stopServer() {
  lock();
  for (uint8_t i = 0; i < MAX_TCP_CLIENTS; ++i) {
    if (g_clients[i]) {
      g_clients[i].stop();
    }
    clientRx_[i] = "";
  }
  if (g_monServer) {
    g_monServer->stop();
    delete g_monServer;
    g_monServer = nullptr;
  }
  serverRunning_ = false;
  unlock();
}

void OtaMonitorClass::appendToRing(uint8_t b) {
  ring_[totalBytes_ % OTA_MONITOR_BUF_SIZE] = b;
  totalBytes_++;
}

void OtaMonitorClass::replayRingToClient(uint8_t idx) {
  if (!g_clients[idx] || !g_clients[idx].connected() || totalBytes_ == 0) return;

  const uint32_t stored = (totalBytes_ < OTA_MONITOR_BUF_SIZE) ? totalBytes_ : OTA_MONITOR_BUF_SIZE;
  const uint32_t start  = (totalBytes_ - stored) % OTA_MONITOR_BUF_SIZE;

  if (start + stored <= OTA_MONITOR_BUF_SIZE) {
    g_clients[idx].write(ring_ + start, stored);
  } else {
    const uint32_t firstPart = OTA_MONITOR_BUF_SIZE - start;
    g_clients[idx].write(ring_ + start, firstPart);
    g_clients[idx].write(ring_, stored - firstPart);
  }
}

void OtaMonitorClass::handle() {
  String linesToDispatch[4];
  uint8_t lineCount = 0;

  lock();
  if (serverRunning_ && g_monServer) {
    if (g_monServer->hasClient()) {
      WiFiClient incoming = g_monServer->available();
      uint8_t slot = 0;
      bool placed = false;
      for (uint8_t i = 0; i < MAX_TCP_CLIENTS; ++i) {
        if (!g_clients[i] || !g_clients[i].connected()) {
          slot = i;
          placed = true;
          break;
        }
      }
      if (!placed) {
        slot = 0;
      }
      if (g_clients[slot]) g_clients[slot].stop();
      g_clients[slot] = incoming;
      g_clients[slot].setNoDelay(true);
      clientRx_[slot] = "";
      g_clients[slot].println(F("--- Connected to OTA Monitor (type 'help' for commands) ---"));
      replayRingToClient(slot);
    }

    for (uint8_t i = 0; i < MAX_TCP_CLIENTS; ++i) {
      if (!g_clients[i] || !g_clients[i].connected()) continue;
      while (g_clients[i].available()) {
        char c = (char)g_clients[i].read();
        if (c == '\r') continue;
        if (c == '\n') {
          String line = clientRx_[i];
          clientRx_[i] = "";
          line.trim();
          if (line.length() > 0 && lineCount < 4) {
            linesToDispatch[lineCount++] = line;
          }
        } else if (clientRx_[i].length() < 128) {
          clientRx_[i] += c;
        }
      }
    }
  }
  unlock();

  for (uint8_t i = 0; i < lineCount; ++i) {
    dispatchLine(linesToDispatch[i]);
  }
}

size_t OtaMonitorClass::write(uint8_t b) {
  lock();
  Serial.write(b);
  appendToRing(b);
  if (serverRunning_) {
    for (uint8_t i = 0; i < MAX_TCP_CLIENTS; ++i) {
      if (g_clients[i] && g_clients[i].connected()) {
        g_clients[i].write(b);
      }
    }
  }
  unlock();
  return 1;
}

size_t OtaMonitorClass::write(const uint8_t* buffer, size_t size) {
  if (!buffer || size == 0) return 0;
  lock();
  Serial.write(buffer, size);
  for (size_t i = 0; i < size; ++i) {
    appendToRing(buffer[i]);
  }
  if (serverRunning_) {
    for (uint8_t i = 0; i < MAX_TCP_CLIENTS; ++i) {
      if (g_clients[i] && g_clients[i].connected()) {
        g_clients[i].write(buffer, size);
      }
    }
  }
  unlock();
  return size;
}

void OtaMonitorClass::clearBuffer() {
  lock();
  memset(ring_, 0, sizeof(ring_));
  totalBytes_ = 0;
  unlock();
}

String OtaMonitorClass::readLogChunk(uint32_t sinceOffset, uint32_t& nextOffset) const {
  lock();
  nextOffset = totalBytes_;
  if (totalBytes_ == 0) {
    unlock();
    return "";
  }
  // If client cursor is ahead of totalBytes_, the ESP32 rebooted (or buffer was cleared)
  // -> replay everything from 0 so boot logs & setup() prints are never skipped!
  if (sinceOffset > totalBytes_) {
    sinceOffset = 0;
  }
  if (sinceOffset >= totalBytes_) {
    unlock();
    return "";
  }

  uint32_t avail = totalBytes_ - sinceOffset;
  if (avail > OTA_MONITOR_BUF_SIZE) {
    avail = OTA_MONITOR_BUF_SIZE;
    sinceOffset = totalBytes_ - avail;
  }

  String out;
  out.reserve(avail);
  for (uint32_t i = 0; i < avail; ++i) {
    char c = (char)ring_[(sinceOffset + i) % OTA_MONITOR_BUF_SIZE];
    if (c != '\0') out += c;
  }
  unlock();
  return out;
}

void OtaMonitorClass::injectCommand(const String& line) {
  String trimmed = line;
  trimmed.trim();
  if (trimmed.length() > 0) {
    dispatchLine(trimmed);
  }
}

void OtaMonitorClass::dispatchLine(const String& rawLine) {
  printf("> %s\n", rawLine.c_str());
  lock();
  pendingLine_    = rawLine;
  hasPendingLine_ = true;
  MonitorCommandCallback cb = cmdCallback_;
  unlock();
  if (cb) {
    cb(rawLine);
  }
}

String OtaMonitorClass::readLine() {
  lock();
  if (!hasPendingLine_) {
    unlock();
    return "";
  }
  hasPendingLine_ = false;
  String out = pendingLine_;
  pendingLine_ = "";
  unlock();
  return out;
}
