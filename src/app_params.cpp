#include "app_params.h"
#include <Preferences.h>

namespace {
constexpr const char* NVS_NAMESPACE = "mtr_ota_cfg";

template <typename T>
T clampVal(T val, T lo, T hi) {
  if (val < lo) return lo;
  if (val > hi) return hi;
  return val;
}
}  // namespace

AppParams AppParamsStore::defaults() {
  AppParams p;
  p.speedNormal     = 140;
  p.speedMax        = 255;
  p.turnSpeed       = 175;
  p.sensorThreshold = 1800;
  p.rangeThreshold  = 45;
  p.startDelayMs    = 3000;
  p.mode            = MODE_DEFAULT;
  return p;
}

bool AppParamsStore::load(AppParams& out) {
  out = defaults();
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    return false;
  }

  out.speedNormal     = prefs.getUChar("spdNorm", out.speedNormal);
  out.speedMax        = prefs.getUChar("spdMax", out.speedMax);
  out.turnSpeed       = prefs.getUChar("spdTurn", out.turnSpeed);
  out.sensorThreshold = prefs.getUShort("sensThr", out.sensorThreshold);
  out.rangeThreshold  = prefs.getUShort("rngThr", out.rangeThreshold);
  out.startDelayMs    = prefs.getUShort("startDly", out.startDelayMs);
  out.mode            = prefs.getUChar("mode", out.mode);
  prefs.end();

  out.sensorThreshold = clampVal<uint16_t>(out.sensorThreshold, 0, 4095);
  out.rangeThreshold  = clampVal<uint16_t>(out.rangeThreshold, 5, 150);
  out.startDelayMs    = clampVal<uint16_t>(out.startDelayMs, 0, 10000);
  out.mode            = clampVal<uint8_t>(out.mode, 0, 3);
  return true;
}

bool AppParamsStore::save(const AppParams& in, AppParams& savedOut) {
  savedOut = in;
  savedOut.sensorThreshold = clampVal<uint16_t>(savedOut.sensorThreshold, 0, 4095);
  savedOut.rangeThreshold  = clampVal<uint16_t>(savedOut.rangeThreshold, 5, 150);
  savedOut.startDelayMs    = clampVal<uint16_t>(savedOut.startDelayMs, 0, 10000);
  savedOut.mode            = clampVal<uint8_t>(savedOut.mode, 0, 3);

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    return false;
  }

  prefs.putUChar("spdNorm", savedOut.speedNormal);
  prefs.putUChar("spdMax", savedOut.speedMax);
  prefs.putUChar("spdTurn", savedOut.turnSpeed);
  prefs.putUShort("sensThr", savedOut.sensorThreshold);
  prefs.putUShort("rngThr", savedOut.rangeThreshold);
  prefs.putUShort("startDly", savedOut.startDelayMs);
  prefs.putUChar("mode", savedOut.mode);
  prefs.end();
  return true;
}

bool AppParamsStore::resetDefaults(AppParams& out) {
  AppParams def = defaults();
  return save(def, out);
}

String AppParamsStore::toJson(const AppParams& p) {
  String s;
  s.reserve(192);
  s += '{';
  s += "\"speedNormal\":" + String(p.speedNormal) + ',';
  s += "\"speedMax\":" + String(p.speedMax) + ',';
  s += "\"turnSpeed\":" + String(p.turnSpeed) + ',';
  s += "\"sensorThreshold\":" + String(p.sensorThreshold) + ',';
  s += "\"rangeThreshold\":" + String(p.rangeThreshold) + ',';
  s += "\"startDelayMs\":" + String(p.startDelayMs) + ',';
  s += "\"mode\":" + String(p.mode);
  s += '}';
  return s;
}
