// Suckboxx ESP32-liima - pysyvä tallennus. Ks. nvs_store.h.

#include "hal/nvs_store.h"

#include <Preferences.h>

#include <cmath>

namespace sbx {
namespace hal {
namespace {

constexpr char kNamespace[] = "sbx";
constexpr char kKeyVersion[] = "_v";  // alaviiva: ei voi törmätä rekisterin avaimiin ([a-z0-9_], ei alussa)
constexpr char kKeyGain[kChannels][4] = {"_g0", "_g1", "_g2"};
constexpr char kKeyGainValid[] = "_gv";

}  // namespace

bool nvsLoadConfig(Config& cfg) {
  Preferences p;
  if (!p.begin(kNamespace, true)) return false;  // nimiavaruutta ei vielä ole
  if (p.getUShort(kKeyVersion, 0) != kConfigSchemaVersion) {
    p.end();
    return false;
  }
  for (int i = 0; i < configFieldCount(); ++i) {
    const char* key = configField(i).key;
    if (!p.isKey(key)) continue;  // uusi kenttä: oletus
    const float v = p.getFloat(key, configGet(cfg, i));
    if (std::isfinite(v)) configSet(cfg, i, v);  // alueen ulkopuolinen hylätään (oletus jää)
  }
  p.end();
  return true;
}

void nvsLoadCal(Calibration& cal) {
  Preferences p;
  if (!p.begin(kNamespace, true)) return;
  for (int i = 0; i < kChannels; ++i) cal.gain[i] = p.getFloat(kKeyGain[i], cal.gain[i]);
  cal.gain_valid = p.getBool(kKeyGainValid, false);
  p.end();
}

void nvsSaveConfig(const Config& cfg) {
  Preferences p;
  if (!p.begin(kNamespace, false)) return;
  for (int i = 0; i < configFieldCount(); ++i) {
    const float v = configGet(cfg, i);
    // Sama arvo -> ei kirjoitusta (flashin kuluminen, ytimien pysäytys).
    if (!p.isKey(configField(i).key) || p.getFloat(configField(i).key, NAN) != v) {
      p.putFloat(configField(i).key, v);
    }
  }
  p.putUShort(kKeyVersion, kConfigSchemaVersion);
  p.end();
}

void nvsSaveCal(const Calibration& cal) {
  Preferences p;
  if (!p.begin(kNamespace, false)) return;
  for (int i = 0; i < kChannels; ++i) p.putFloat(kKeyGain[i], cal.gain[i]);
  p.putBool(kKeyGainValid, cal.gain_valid);
  p.end();
}

}  // namespace hal
}  // namespace sbx
