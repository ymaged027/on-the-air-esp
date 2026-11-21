#include "sumo_params.h"
#include <Preferences.h>

namespace {
constexpr const char* NVS_NAMESPACE = "sumo_cfg";

template <typename T>
T clampVal(T val, T lo, T hi) {
  if (val < lo) return lo;
  if (val > hi) return hi;
  return val;
}
}  // namespace

SumoParams SumoParamsStore::defaults() {
  SumoParams p;
  p.searchSpeed    = 140;
  p.attackSpeed    = 255;
  p.turnSpeed      = 175;
  p.lineThreshold  = 1800;
  p.opponentDistCm = 45;
  p.startDelayMs   = 5000;
  p.strategy       = STRATEGY_DIRECT_ATTACK;
  return p;
}

bool SumoParamsStore::load(SumoParams& out) {
  out = defaults();
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    return false;
  }

  out.searchSpeed    = prefs.getUChar("searchSpd", out.searchSpeed);
  out.attackSpeed    = prefs.getUChar("attackSpd", out.attackSpeed);
  out.turnSpeed      = prefs.getUChar("turnSpd", out.turnSpeed);
  out.lineThreshold  = prefs.getUShort("lineThr", out.lineThreshold);
  out.opponentDistCm = prefs.getUShort("oppDist", out.opponentDistCm);
  out.startDelayMs   = prefs.getUShort("startDly", out.startDelayMs);
  out.strategy       = prefs.getUChar("strategy", out.strategy);
  prefs.end();

  // Clamp in case NVS had out-of-range values
  out.lineThreshold  = clampVal<uint16_t>(out.lineThreshold, 0, 4095);
  out.opponentDistCm = clampVal<uint16_t>(out.opponentDistCm, 5, 150);
  out.startDelayMs   = clampVal<uint16_t>(out.startDelayMs, 0, 10000);
  out.strategy       = clampVal<uint8_t>(out.strategy, 0, 3);
  return true;
}

bool SumoParamsStore::save(const SumoParams& in, SumoParams& savedOut) {
  savedOut = in;
  savedOut.lineThreshold  = clampVal<uint16_t>(savedOut.lineThreshold, 0, 4095);
  savedOut.opponentDistCm = clampVal<uint16_t>(savedOut.opponentDistCm, 5, 150);
  savedOut.startDelayMs   = clampVal<uint16_t>(savedOut.startDelayMs, 0, 10000);
  savedOut.strategy       = clampVal<uint8_t>(savedOut.strategy, 0, 3);

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    return false;
  }

  prefs.putUChar("searchSpd", savedOut.searchSpeed);
  prefs.putUChar("attackSpd", savedOut.attackSpeed);
  prefs.putUChar("turnSpd", savedOut.turnSpeed);
  prefs.putUShort("lineThr", savedOut.lineThreshold);
  prefs.putUShort("oppDist", savedOut.opponentDistCm);
  prefs.putUShort("startDly", savedOut.startDelayMs);
  prefs.putUChar("strategy", savedOut.strategy);
  prefs.end();
  return true;
}

bool SumoParamsStore::resetDefaults(SumoParams& out) {
  SumoParams def = defaults();
  return save(def, out);
}

String SumoParamsStore::toJson(const SumoParams& p) {
  String s;
  s.reserve(192);
  s += '{';
  s += "\"searchSpeed\":" + String(p.searchSpeed) + ',';
  s += "\"attackSpeed\":" + String(p.attackSpeed) + ',';
  s += "\"turnSpeed\":" + String(p.turnSpeed) + ',';
  s += "\"lineThreshold\":" + String(p.lineThreshold) + ',';
  s += "\"opponentDistCm\":" + String(p.opponentDistCm) + ',';
  s += "\"startDelayMs\":" + String(p.startDelayMs) + ',';
  s += "\"strategy\":" + String(p.strategy);
  s += '}';
  return s;
}
