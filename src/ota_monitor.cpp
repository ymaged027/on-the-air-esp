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

void OtaMonitorClass::beginServer(uint16_t port) {
  stopServer();
  port_ = port;
  g_monServer = new WiFiServer(port_);
  g_monServer->begin();
  g_monServer->setNoDelay(true);
  serverRunning_ = true;
}

void OtaMonitorClass::stopServer() {
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
  if (!serverRunning_ || !g_monServer) return;

  // Accept incoming OTA Monitor TCP connections (e.g., Arduino IDE Monitor)
  if (g_monServer->hasClient()) {
    WiFiClient incoming = g_monServer->available();
    bool placed = false;
    for (uint8_t i = 0; i < MAX_TCP_CLIENTS; ++i) {
      if (!g_clients[i] || !g_clients[i].connected()) {
        if (g_clients[i]) g_clients[i].stop();
        g_clients[i] = incoming;
        g_clients[i].setNoDelay(true);
        clientRx_[i] = "";
        g_clients[i].println(F("--- Connected to OTA Monitor (type 'help' for commands) ---"));
        replayRingToClient(i);
        placed = true;
        break;
      }
    }
    if (!placed) {
      // Replace oldest client if all slots full
      g_clients[0].stop();
      g_clients[0] = incoming;
      g_clients[0].setNoDelay(true);
      clientRx_[0] = "";
      g_clients[0].println(F("--- Connected to OTA Monitor (type 'help' for commands) ---"));
      replayRingToClient(0);
    }
  }

  // Read incoming lines typed in Arduino IDE Monitor / TCP terminal
  for (uint8_t i = 0; i < MAX_TCP_CLIENTS; ++i) {
    if (!g_clients[i] || !g_clients[i].connected()) continue;
    while (g_clients[i].available()) {
      char c = (char)g_clients[i].read();
      if (c == '\r') continue;
      if (c == '\n') {
        String line = clientRx_[i];
        clientRx_[i] = "";
        line.trim();
        if (line.length() > 0) {
          dispatchLine(line);
        }
      } else if (clientRx_[i].length() < 128) {
        clientRx_[i] += c;
      }
    }
  }
}

size_t OtaMonitorClass::write(uint8_t b) {
  Serial.write(b);
  appendToRing(b);
  if (serverRunning_) {
    for (uint8_t i = 0; i < MAX_TCP_CLIENTS; ++i) {
      if (g_clients[i] && g_clients[i].connected()) {
        g_clients[i].write(b);
      }
    }
  }
  return 1;
}

size_t OtaMonitorClass::write(const uint8_t* buffer, size_t size) {
  if (!buffer || size == 0) return 0;
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
  return size;
}

void OtaMonitorClass::clearBuffer() {
  memset(ring_, 0, sizeof(ring_));
  totalBytes_ = 0;
}

String OtaMonitorClass::readLogChunk(uint32_t sinceOffset, uint32_t& nextOffset) const {
  nextOffset = totalBytes_;
  if (totalBytes_ == 0 || sinceOffset >= totalBytes_) {
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
  pendingLine_    = rawLine;
  hasPendingLine_ = true;
  if (cmdCallback_) {
    cmdCallback_(rawLine);
  }
}

String OtaMonitorClass::readLine() {
  if (!hasPendingLine_) return "";
  hasPendingLine_ = false;
  String out = pendingLine_;
  pendingLine_ = "";
  return out;
}
