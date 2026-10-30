#pragma once
// Suckboxx core - /api/sim-avaimet <-> SimParams (WP-6, docs/firmware.md §4.8, §5).
//
// Simulaattori on liiman omistama (näytteenottotaski), mutta muunnos on
// corea, jotta se on testattavissa. Viitetoteutus on tools/mock_server.py:n
// SIM_KEYS ja sim_apply:
//   - POST on kaikki tai ei mitään: virheellinen avain tai arvo hylkää koko
//     pyynnön, eikä mikään muutu.
//   - Arvo rajataan avaimen alueelle (EngineSim rajaa itsekin), sitä ei hylätä.
//   - Kokonaislukuavain (engine_on, fault1..3) ei pyöristä: "0,5" hylätään.
// Liima yhdistää POSTin nykyisiin parametreihin, jolloin vain annetut avaimet
// muuttuvat ja driftin jälkeinen kierrosluku säilyy (engine_sim.h configure):
//     SimParams p = sim.params();
//     bool rpm_set = false;
//     if (simApplyForm(p, pairs, n, rpm_set, err, sizeof(err)))
//       sim.configure(p, proc.config().sensor, rpm_set);
// rpm_set = POST sisältää rpm-avaimen: kesken driftin "drift=0&rpm=850"
// ankkuroi 850:een, pelkkä "drift=0" jää nykyiseen kierroslukuun.
// GET /api/sim = simToJson(sim.params()).

#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "engine_sim.h"
#include "form.h"
#include "json_writer.h"
#include "types.h"

namespace sbx {

struct SimKeyInfo {
  const char* key;
  float min, max;   // rajaus, ei hylkäys (mock_server.py SIM_KEYS)
  bool integer;     // 0/1 tai vikakoodi (SimFault)
};

enum class SimKey : uint8_t {
  EngineOn, Rpm, Jitter, Drift, Base, Err1, Err2, Err3,
  Depth, Res, Xtalk, Peak, Noise, Fault1, Fault2, Fault3, Count
};
constexpr int kSimKeyCount = static_cast<int>(SimKey::Count);

// Avaimet §5:n järjestyksessä. Alueet ovat EngineSimin omat rajat (yksityiset
// vakiot kommenteissa), joten rajaus ei muuta sitä, mitä simulaattori tekisi.
// Indeksi alueen ulkopuolella -> tyhjä vartija.
inline const SimKeyInfo& simKey(int i) {
  static constexpr SimKeyInfo kKeys[] = {
      {"engine_on", 0.0f, 1.0f, true},
      {"rpm", EngineSim::kRpmMin, EngineSim::kRpmMax, false},
      {"jitter", 0.0f, 0.4f, false},          // kJitterMax
      {"drift", -2000.0f, 2000.0f, false},    // rpm/s (rivi 5)
      {"base", -100.0f, 0.0f, false},         // kSensorSpanKpa: GN −100…0 kPa
      {"err1", -30.0f, 30.0f, false},
      {"err2", -30.0f, 30.0f, false},
      {"err3", -30.0f, 30.0f, false},
      {"depth", 0.0f, 60.0f, false},
      {"res", 0.0f, 50.0f, false},            // kResKpaMax
      {"xtalk", 0.0f, 1.0f, false},
      {"peak", 0.0f, 60.0f, false},
      {"noise", 0.0f, 1000.0f, false},        // kNoiseMvMax
      {"fault1", 0.0f, 3.0f, true},           // SimFault: 0 ei, 1 letku, 2 alas, 3 ylös
      {"fault2", 0.0f, 3.0f, true},
      {"fault3", 0.0f, 3.0f, true},
  };
  static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == static_cast<size_t>(kSimKeyCount),
                "SimKey ja avaintaulukko eri mittaisia");
  static constexpr SimKeyInfo kNone = {"", 0.0f, 0.0f, false};
  if (i < 0 || i >= kSimKeyCount) return kNone;
  return kKeys[i];
}

inline int simFindKey(const char* key) {
  if (key == nullptr) return -1;
  for (int i = 0; i < kSimKeyCount; ++i) {
    if (std::strcmp(simKey(i).key, key) == 0) return i;
  }
  return -1;
}

// Avaimen arvo floatina. Tuntematon indeksi -> NaN.
inline float simGet(const SimParams& p, int i) {
  if (i < 0 || i >= kSimKeyCount) return std::numeric_limits<float>::quiet_NaN();
  switch (static_cast<SimKey>(i)) {
    case SimKey::EngineOn: return p.engine_on ? 1.0f : 0.0f;
    case SimKey::Rpm: return p.rpm;
    case SimKey::Jitter: return p.rpm_jitter;
    case SimKey::Drift: return p.rpm_drift;
    case SimKey::Base: return p.base_kpa;
    case SimKey::Err1: return p.sync_err_kpa[0];
    case SimKey::Err2: return p.sync_err_kpa[1];
    case SimKey::Err3: return p.sync_err_kpa[2];
    case SimKey::Depth: return p.depth_kpa;
    case SimKey::Res: return p.res_kpa;
    case SimKey::Xtalk: return p.xtalk;
    case SimKey::Peak: return p.peak_kpa;
    case SimKey::Noise: return p.noise_mv;
    case SimKey::Fault1: return static_cast<float>(static_cast<uint8_t>(p.fault[0]));
    case SimKey::Fault2: return static_cast<float>(static_cast<uint8_t>(p.fault[1]));
    case SimKey::Fault3: return static_cast<float>(static_cast<uint8_t>(p.fault[2]));
    case SimKey::Count: break;
  }
  return std::numeric_limits<float>::quiet_NaN();
}

// Asettaa avaimen arvon rajattuna alueelle. Kokonaislukuavaimen arvo on
// tarkistettu kutsujalla; ei-äärellinen arvo ei muuta mitään.
inline void simSet(SimParams& p, int i, float v) {
  if (i < 0 || i >= kSimKeyCount || !std::isfinite(v)) return;
  const SimKeyInfo& info = simKey(i);
  if (v < info.min) v = info.min;
  if (v > info.max) v = info.max;
  switch (static_cast<SimKey>(i)) {
    case SimKey::EngineOn: p.engine_on = v != 0.0f; break;
    case SimKey::Rpm: p.rpm = v; break;
    case SimKey::Jitter: p.rpm_jitter = v; break;
    case SimKey::Drift: p.rpm_drift = v; break;
    case SimKey::Base: p.base_kpa = v; break;
    case SimKey::Err1: p.sync_err_kpa[0] = v; break;
    case SimKey::Err2: p.sync_err_kpa[1] = v; break;
    case SimKey::Err3: p.sync_err_kpa[2] = v; break;
    case SimKey::Depth: p.depth_kpa = v; break;
    case SimKey::Res: p.res_kpa = v; break;
    case SimKey::Xtalk: p.xtalk = v; break;
    case SimKey::Peak: p.peak_kpa = v; break;
    case SimKey::Noise: p.noise_mv = v; break;
    case SimKey::Fault1: p.fault[0] = static_cast<SimFault>(static_cast<uint8_t>(v)); break;
    case SimKey::Fault2: p.fault[1] = static_cast<SimFault>(static_cast<uint8_t>(v)); break;
    case SimKey::Fault3: p.fault[2] = static_cast<SimFault>(static_cast<uint8_t>(v)); break;
    case SimKey::Count: break;
  }
}

namespace sim_detail {

// Luku lomakkeelta samoin säännöin kuin configSetText: pilkku tai piste,
// ei nan/inf/heksaa, ei roskaa perässä, LC_NUMERIC ei vaikuta. Ylivuoto
// (esim. "1e99") palautuu äärettömänä, ja kutsuja rajaa sen alueen laitaan
// kuten mockin float() + min/max.
inline bool parseNumber(const char* text, float& out) {
  if (text == nullptr) return false;
  while (*text == ' ' || *text == '\t') ++text;
  size_t len = std::strlen(text);
  while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\r' ||
                     text[len - 1] == '\n')) {
    --len;
  }
  char tmp[32];
  if (len == 0 || len >= sizeof(tmp)) return false;
  std::memcpy(tmp, text, len);
  tmp[len] = '\0';
  const char* dp = std::localeconv()->decimal_point;
  const char sep = (dp != nullptr && dp[0] != '\0' && dp[1] == '\0') ? dp[0] : '.';
  for (size_t k = 0; k < len; ++k) {
    const char ch = tmp[k];
    if (ch == ',' || ch == '.') {
      tmp[k] = sep;
    } else if (!((ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == 'e' || ch == 'E')) {
      return false;
    }
  }
  char* end = nullptr;
  const float v = std::strtof(tmp, &end);
  if (end == tmp || *end != '\0' || std::isnan(v)) return false;
  out = v;
  return true;
}

// 6 merkitsevää numeroa (kuten configToJson): 850 -> "850", 0.01 -> "0.01".
inline int decimals6(float v) {
  float a = std::fabs(v);
  int digits = 1;
  while (a >= 10.0f && digits < 6) {
    a /= 10.0f;
    ++digits;
  }
  return 6 - digits;
}

}  // namespace sim_detail

enum class SimSetResult : uint8_t { Ok, UnknownKey, Invalid };

// Yksi avain tekstistä. Hylätty arvo ei muuta p:tä.
inline SimSetResult simSetText(SimParams& p, const char* key, const char* text) {
  const int i = simFindKey(key);
  if (i < 0) return SimSetResult::UnknownKey;
  float v = 0.0f;
  if (!sim_detail::parseNumber(text, v)) return SimSetResult::Invalid;
  const SimKeyInfo& info = simKey(i);
  if (std::isinf(v)) v = v > 0.0f ? info.max : info.min;
  if (info.integer && std::floor(v) != v) return SimSetResult::Invalid;
  simSet(p, i, v);
  return SimSetResult::Ok;
}

// POST /api/sim: kaikki tai ei mitään. true = p päivitetty ja rpm_set kertoo,
// oliko rpm-avain mukana. false = p ja rpm_set ennallaan, err:ssä mockin
// sim_apply-muotoinen teksti {"ok":false,"err":err}-vastaukseen:
//   "Tuntematon simulaattoriparametri: avain" | "Virheellinen arvo: avain=arvo".
// Tyhjä lomake on kelvollinen (ei muutoksia), kuten mockissa.
inline bool simApplyForm(SimParams& p, const FormPair* pairs, int count, bool& rpm_set, char* err,
                         size_t err_len) {
  form_detail::formatText(err, err_len, "%s", "");
  SimParams q = p;
  bool has_rpm = false;
  for (int k = 0; pairs != nullptr && k < count; ++k) {
    const char* key = pairs[k].key != nullptr ? pairs[k].key : "";
    const char* text = pairs[k].value != nullptr ? pairs[k].value : "";
    const SimSetResult r = simSetText(q, key, text);
    if (r == SimSetResult::UnknownKey) {
      form_detail::formatText(err, err_len, "Tuntematon simulaattoriparametri: %s", key);
      return false;
    }
    if (r == SimSetResult::Invalid) {
      form_detail::formatText(err, err_len, "Virheellinen arvo: %s=%s", key, text);
      return false;
    }
    if (std::strcmp(key, "rpm") == 0) has_rpm = true;
  }
  p = q;
  rpm_set = has_rpm;
  return true;
}

// GET /api/sim: kaikki avaimet yhtenä objektina §5:n järjestyksessä.
// Kokonaislukuavaimet lukuina (UI:n isNum), muut 6 merkitsevällä numerolla.
// Palauttaa pituuden ilman NULia, 0 = ei mahtunut.
inline size_t simToJson(const SimParams& p, char* buf, size_t n) {
  JsonWriter w(buf, n);
  w.beginObject();
  for (int i = 0; i < kSimKeyCount; ++i) {
    const SimKeyInfo& info = simKey(i);
    const float v = simGet(p, i);
    w.key(info.key);
    if (info.integer && std::isfinite(v)) {
      w.value(static_cast<int>(v));
    } else {
      w.valueTrim(v, sim_detail::decimals6(v));
    }
  }
  w.endObject();
  return w.ok() ? w.length() : 0;
}

// Pahin tapaus (kaikki liukuluvut 6 merkitsevällä numerolla ja negatiivisina)
// on mitattu test_sim_api:ssa; puskurissa on reilusti varaa.
constexpr size_t kSimJsonSize = 512;

}  // namespace sbx
