#pragma once
// Suckboxx core - ajonaikainen konfiguraatio ja kenttärekisteri (WP-3, S5a,
// docs/firmware.md §4.4).
//
// S5a: kaikki kynnykset ja suodatinvakiot säädetään laiturilla huoltosivulta
// eikä uudelleenkääntämällä. Rekisteri on se yksi paikka, josta
//  - huoltosivun lomake rakentuu (configToJson: nimi, yksikkö, ryhmä, alue, oletus),
//  - NVS-liima tallentaa ja lataa kentät avain kerrallaan (avain = NVS-avain),
//  - lomakkeen POST tulkitaan (configSetText).
// Uusi kenttä = yksi rivi SBX_CONFIG_FIELDS-listaan; liimaa tai UI:ta ei muuteta.
//
// Avaimet ovat pysyviä: ne ovat NVS-avaimia (ESP-IDF:n raja 15 merkkiä) ja
// HTML-lomakkeen nimiä. Avaimen vaihtaminen hukkaa laitteelle tallennetun
// arvon hiljaa, joten avain = types.h:n jäsenen nimi eikä sitä muuteta.

#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

#include "json_writer.h"
#include "types.h"

namespace sbx {

struct Config {
  SensorParams sensor;
  DspParams dsp;
  StatusParams status;
  SourceKind source = SourceKind::Sim;  // D14: simulaattori, kunnes anturit on kytketty
  uint8_t push_rate_hz = 15;            // D9
};
constexpr uint16_t kConfigSchemaVersion = 1;

enum class FieldType : uint8_t { Float, U8, U16, Bool };
struct FieldInfo {
  const char* key;    // ≤ 15 merkkiä (NVS-avaimen raja), [a-z0-9_]
  const char* label;  // suomeksi, UI:hin
  const char* unit;   // "kPa", "Hz", "s", "" ...
  const char* group;  // "Synkronointi", "Signaali", "Anturi", "Järjestelmä"
  FieldType type;
  float min, max;
};

inline constexpr char kGroupSync[] = "Synkronointi";
inline constexpr char kGroupSignal[] = "Signaali";
inline constexpr char kGroupSensor[] = "Anturi";
inline constexpr char kGroupSystem[] = "Järjestelmä";

// ---------------------------------------------------------------------------
// Kenttälista: F(tunniste, jäsen, avain, tyyppi, min, max, ryhmä, yksikkö, nimi)
//
// Yksi lista tuottaa rekisterin, lukija/kirjoittaja-switchin ja käännösaikaiset
// tarkistukset (tyyppi vastaa jäsentä, avain ≤ 15, oletus alueella), joten
// mikään niistä ei voi jäädä jälkeen kun kenttä lisätään.
//
// Alueet ovat fysikaalisia järkevyysrajoja - ne estävät kirjoitusvirheen
// (esim. 150 kun tarkoitettiin 1,50), eivät suosittele arvoa:
//  Synkronointi
//   - tol_*: D2:n 1,5/3,0 kPa. Alle 0,1 kPa on anturin kalibroidun
//     resoluution alla (D2: N=16 -> 0,046 kPa); yli 10/20 kPa ei ole enää
//     synkronointia (koko joutokäyntialipaine ~55 kPa).
//   - off_kpa/hose_off_kpa: D10. Ylärajalla 20 kPa marginaali käyvän moottorin
//     −55 kPa:iin säilyy; alaraja anturin kohinan yläpuolella.
//   - sync_rpm_*: D12 500–3000; alaraja 100 (sammuva kone), yläraja sama kuin
//     syklintunnistuksen katto.
//   - unstable_cv: rivi 2:n 15 %. 1 % on jo tavallisen joutokäynnin hajonta.
//   - hose_hyst_kpa 0…5 (WP-9): 0 = ei hystereesiä (WP-4:n käytös). Yli 5 kPa
//     poistumisraja olisi jo off_kpa:n (D10) tuntumassa.
//   - drift_tau_s 0…5 s (WP-9): 0 = suodattamaton vertailu. Aikavakio pidentää
//     tunnistusviivettä ~aikavakion verran, joten yli 5 s (> 2 x ikkuna) ei
//     enää kerro säädönaikaisesta ajautumisesta.
//   - zero_settle_kpa 0,05…2 (WP-9): alaraja ADC-kohinan yläpuolella (taso-EMA:n
//     1 s:n vaihteluväli enintään 0,04 kPa 4 mV RMS:n kohinalla, calibration.h);
//     yli 2 kPa portti ei enää erota vuotavaa alipainetta (taso-EMA laskee
//     off_kpa:sta nollaan ~1-2 s:ssa).
//  Signaali (DspParams)
//   - lp_cutoff_hz: vähintään 5 Hz, jotta 600 rpm:n pulssitaajuus (rpm/120 = 5 Hz)
//     ei vaimene. Yläraja fs/4 = 250 Hz on vain digitaalisuotimen järkevä katto:
//     resonanssin (90–215 Hz) poisto vaatii käytännössä alle ~60 Hz (oletus 40).
//   - avg_cycles 1…32 ja rpm_hist 2…64: dsp.h:n kiinteät puskurit
//     (Dsp::kMaxAvgCycles, Dsp::kMaxRpmHist).
//   - rpm_min 60…2000 (simulaattorin alaraja 60); rpm_max ≤ 12000, jolloin
//     syklissä on vielä 10 näytettä 1 kHz:llä (SUUNNITTELU.md §2.2).
//  Anturi (docs/rauta.md §1.2, §1.6)
//   - v_zero_mv 1000…3300: GN 2700 mV, GPN 1450 mV, 5 V -versio jakajalla 2700.
//   - mv_per_kpa 5…50: GN 25, 5 V/jakaja 24, GPN 12,5. Alaraja estää myös
//     nollalla jakamisen nominalKpa:ssa.
//   - fault_lo_mv 50…500 ja fault_hi_mv 2500…3250 (rivi 7). Rikkinäinen kanava
//     (anturi irti, oikosulku GND:hen tai kiskoon) jää ADC:n ääripäähän 0 tai
//     ~3300 mV, ja dsp.h vertaa tiukasti mv < fault_lo_mv / mv > fault_hi_mv
//     uint16-arvoilla. Raja 0 tai 3300 ei siksi koskaan laukeaisi: rivi 7
//     olisi hiljaa pois päältä ja laite voisi näyttää vihreää epäluotettavalla
//     kanavalla (§8:n suunnittelusääntö). 50 mV:n marginaali ADC:n kumpaankin
//     reunaan; simulaattorin SensorLow (≤ 30 mV) ja SensorHigh (≥ 3270 mV)
//     osuvat aina. Ylärajat: 500 mV = −88 kPa (syvempää alipainetta ei
//     joutokäynnillä ole), 2500 mV = −8 kPa (täysi kaasu, GPN:llä jo ylipaine).
//   - rail_nominal_mv 2500…5500: 3,3 V LDO tai 5 V -anturi (R6).
//  Järjestelmä
//   - push_rate_hz ≥ 4: snapshotin wave.s on enintään 16 riviä 62,5 Hz:n
//     desimoidusta datasta, 62,5 / 16 = 3,9 Hz. Yläraja 30 Hz (D9: 10–20 Hz riittää).
// ---------------------------------------------------------------------------
#define SBX_CONFIG_FIELDS(F)                                                                \
  F(Master, status.master, "master", U8, 0.0f, 2.0f,                                        \
    kGroupSync, "", "Master-sylinteri (0 = SYL 1, 1 = SYL 2, 2 = SYL 3)")                   \
  F(TolGreenKpa, status.tol_green_kpa, "tol_green_kpa", Float, 0.1f, 10.0f,                 \
    kGroupSync, "kPa", "Synkronoitu, kun ero enintään")                                     \
  F(TolYellowKpa, status.tol_yellow_kpa, "tol_yellow_kpa", Float, 0.2f, 20.0f,              \
    kGroupSync, "kPa", "Lähellä, kun ero enintään")                                         \
  F(OffKpa, status.off_kpa, "off_kpa", Float, 0.5f, 20.0f,                                  \
    kGroupSync, "kPa", "Ei signaalia: kaikki kanavat alle")                                 \
  F(OffHoldS, status.off_hold_s, "off_hold_s", Float, 0.5f, 60.0f,                          \
    kGroupSync, "s", "Ei signaalia: kesto ennen nollausta")                                 \
  F(HoseOffKpa, status.hose_off_kpa, "hose_off_kpa", Float, 0.2f, 20.0f,                    \
    kGroupSync, "kPa", "Letkuvika: kanavan taso alle")                                      \
  F(HoseHystKpa, status.hose_hyst_kpa, "hose_hyst_kpa", Float, 0.0f, 5.0f,                  \
    kGroupSync, "kPa", "Letkuvika: poistumisen hystereesi")                                 \
  F(UnstableCv, status.unstable_cv, "unstable_cv", Float, 0.01f, 1.0f,                      \
    kGroupSync, "", "Epävakaa käynti: rpm-hajonta yli (osuus)")                             \
  F(StableHoldS, status.stable_hold_s, "stable_hold_s", Float, 0.0f, 30.0f,                 \
    kGroupSync, "s", "Vakaata käyntiä ennen arviota")                                       \
  F(SyncRpmMin, status.sync_rpm_min, "sync_rpm_min", Float, 100.0f, 3000.0f,                \
    kGroupSync, "rpm", "Arvio alkaen kierrosluvusta")                                       \
  F(SyncRpmMax, status.sync_rpm_max, "sync_rpm_max", Float, 500.0f, 12000.0f,               \
    kGroupSync, "rpm", "Arvio enintään kierroslukuun")                                      \
  F(DriftRpm, status.drift_rpm, "drift_rpm", Float, 5.0f, 1000.0f,                          \
    kGroupSync, "rpm", "Kierrosluku ajautuu: muutos yli")                                   \
  F(DriftWindowS, status.drift_window_s, "drift_window_s", Float, 0.5f, 30.0f,              \
    kGroupSync, "s", "Kierrosluku ajautuu: aikaikkuna")                                     \
  F(DriftTauS, status.drift_tau_s, "drift_tau_s", Float, 0.0f, 5.0f,                        \
    kGroupSync, "s", "Kierrosluku ajautuu: suodatuksen aikavakio")                          \
  F(AutoZero, status.auto_zero, "auto_zero", Bool, 0.0f, 1.0f,                              \
    kGroupSync, "", "Automaattinen nollaus moottorin ollessa sammuksissa")                  \
  F(ZeroSettleKpa, status.zero_settle_kpa, "zero_settle_kpa", Float, 0.05f, 2.0f,           \
    kGroupSync, "kPa", "Nollaus: tason vaihtelu 1 s:n aikana enintään")                     \
  F(LpCutoffHz, dsp.lp_cutoff_hz, "lp_cutoff_hz", Float, 5.0f, 250.0f,                      \
    kGroupSignal, "Hz", "Alipäästösuodattimen rajataajuus")                                 \
  F(LevelTauS, dsp.level_tau_s, "level_tau_s", Float, 0.05f, 10.0f,                         \
    kGroupSignal, "s", "Tasolukeman aikavakio")                                             \
  F(HystFrac, dsp.hyst_frac, "hyst_frac", Float, 0.05f, 0.9f,                               \
    kGroupSignal, "", "Syklirajan hystereesi (osuus huippu–huipusta)")                      \
  F(HystMinKpa, dsp.hyst_min_kpa, "hyst_min_kpa", Float, 0.05f, 10.0f,                      \
    kGroupSignal, "kPa", "Syklirajan hystereesi vähintään")                                 \
  F(AvgCycles, dsp.avg_cycles, "avg_cycles", U8, 1.0f, 32.0f,                               \
    kGroupSignal, "", "Keskiarvoistettavat syklit")                                         \
  F(RpmMin, dsp.rpm_min, "rpm_min", Float, 60.0f, 2000.0f,                                  \
    kGroupSignal, "rpm", "Syklintunnistus: alin kierrosluku")                               \
  F(RpmMax, dsp.rpm_max, "rpm_max", Float, 1000.0f, 12000.0f,                               \
    kGroupSignal, "rpm", "Syklintunnistus: ylin kierrosluku")                               \
  F(PulseTimeoutS, dsp.pulse_timeout_s, "pulse_timeout_s", Float, 0.2f, 10.0f,              \
    kGroupSignal, "s", "Pulssi kadonnut, kun ei sykliä")                                    \
  F(RpmHist, dsp.rpm_hist, "rpm_hist", U8, 2.0f, 64.0f,                                     \
    kGroupSignal, "", "Jaksoja kierroslukuhajontaan")                                       \
  F(VZeroMv, sensor.v_zero_mv, "v_zero_mv", Float, 1000.0f, 3300.0f,                        \
    kGroupSensor, "mV", "Anturin jännite 0 kPa:ssa")                                        \
  F(MvPerKpa, sensor.mv_per_kpa, "mv_per_kpa", Float, 5.0f, 50.0f,                          \
    kGroupSensor, "mV/kPa", "Anturin herkkyys")                                             \
  F(FaultLoMv, sensor.fault_lo_mv, "fault_lo_mv", U16, 50.0f, 500.0f,                       \
    kGroupSensor, "mV", "Anturivika: jännite alle")                                         \
  F(FaultHiMv, sensor.fault_hi_mv, "fault_hi_mv", U16, 2500.0f, 3250.0f,                    \
    kGroupSensor, "mV", "Anturivika: jännite yli")                                          \
  F(FaultHoldS, sensor.fault_hold_s, "fault_hold_s", Float, 0.1f, 10.0f,                    \
    kGroupSensor, "s", "Anturivika: kesto ennen hälytystä")                                 \
  F(SatMv, sensor.sat_mv, "sat_mv", U16, 1000.0f, 3300.0f,                                  \
    kGroupSensor, "mV", "Saturaatiovaroitus: jännite vähintään")                            \
  F(RailComp, sensor.rail_comp, "rail_comp", Bool, 0.0f, 1.0f,                              \
    kGroupSensor, "", "Kiskojännitteen korjaus (R6)")                                       \
  F(RailNominalMv, sensor.rail_nominal_mv, "rail_nominal_mv", Float, 2500.0f, 5500.0f,      \
    kGroupSensor, "mV", "Anturikiskon nimellisjännite")                                     \
  F(Source, source, "source", U8, 0.0f, 2.0f,                                               \
    kGroupSystem, "", "Signaalilähde (0 = simulaattori, 1 = ADC, 2 = HX710B)")              \
  F(PushRateHz, push_rate_hz, "push_rate_hz", U8, 4.0f, 30.0f,                              \
    kGroupSystem, "Hz", "Näytön päivitystaajuus")

enum class ConfigFieldId : uint8_t {
#define SBX_CFG_ID(id, member, key, type, mn, mx, group, unit, label) id,
  SBX_CONFIG_FIELDS(SBX_CFG_ID)
#undef SBX_CFG_ID
  Count
};
constexpr int kConfigFieldCount = static_cast<int>(ConfigFieldId::Count);

namespace detail {

// Jäsenen C++-tyyppi -> rekisterin FieldType. SourceKind on enum : uint8_t,
// joten se kulkee rekisterissä U8:na (NVS, lomake ja JSON näkevät luvun).
template <typename T> struct CfgTypeOf;
template <> struct CfgTypeOf<float> { static constexpr FieldType value = FieldType::Float; };
template <> struct CfgTypeOf<uint8_t> { static constexpr FieldType value = FieldType::U8; };
template <> struct CfgTypeOf<uint16_t> { static constexpr FieldType value = FieldType::U16; };
template <> struct CfgTypeOf<bool> { static constexpr FieldType value = FieldType::Bool; };
template <> struct CfgTypeOf<SourceKind> { static constexpr FieldType value = FieldType::U8; };

constexpr float cfgLoad(float v) { return v; }
constexpr float cfgLoad(uint8_t v) { return static_cast<float>(v); }
constexpr float cfgLoad(uint16_t v) { return static_cast<float>(v); }
constexpr float cfgLoad(bool v) { return v ? 1.0f : 0.0f; }
constexpr float cfgLoad(SourceKind v) { return static_cast<float>(static_cast<uint8_t>(v)); }

// Arvo on jo tarkistettu (äärellinen, kokonaisluku tyypin mukaan, alueella).
inline void cfgStore(float& m, float v) { m = v; }
inline void cfgStore(uint8_t& m, float v) { m = static_cast<uint8_t>(v); }
inline void cfgStore(uint16_t& m, float v) { m = static_cast<uint16_t>(v); }
inline void cfgStore(bool& m, float v) { m = (v != 0.0f); }
inline void cfgStore(SourceKind& m, float v) { m = static_cast<SourceKind>(static_cast<uint8_t>(v)); }

constexpr bool keyCharsOk(const char* k) {
  for (; *k != '\0'; ++k) {
    const char ch = *k;
    if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) return false;
  }
  return true;
}

// Käännösaikaiset tarkistukset jokaiselle kentälle.
#define SBX_CFG_CHECK(id, member, key, type, mn, mx, group, unit, label)                       \
  static_assert(CfgTypeOf<decltype(std::declval<Config&>().member)>::value == FieldType::type, \
                "rekisterin tyyppi ei vastaa jäsenen tyyppiä: " key);                         \
  static_assert(sizeof(key) - 1 <= 15 && sizeof(key) > 1, "NVS-avain 1..15 merkkiä: " key);   \
  static_assert(keyCharsOk(key), "avaimessa sallittu vain [a-z0-9_]: " key);                  \
  static_assert((mn) < (mx), "min < max: " key);                                              \
  static_assert(cfgLoad(Config{}.member) >= (mn) && cfgLoad(Config{}.member) <= (mx),         \
                "oletusarvo alueen ulkopuolella: " key);
SBX_CONFIG_FIELDS(SBX_CFG_CHECK)
#undef SBX_CFG_CHECK

// ASCII-kirjainkoosta riippumaton vertailu ("TRUE", "True", "true").
inline bool ieq(const char* a, const char* b) {
  for (; *a != '\0' && *b != '\0'; ++a, ++b) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return *a == *b;
}

// Luku virheilmoitukseen suomalaisittain desimaalipilkulla (kuten status-tekstit).
inline const char* fiNum(char* out, size_t n, float v) {
  if (formatFixed(out, n, v, 3, true) == 0) return "?";
  for (char* p = out; *p != '\0'; ++p) {
    if (*p == '.') *p = ',';
  }
  return out;
}

// Merkitseviä numeroita 6 (floatin tarkkuus ~7): 2700.1 -> 2 desimaalia,
// 0.15 -> 5 desimaalia, ja trimmaus poistaa loppunollat.
inline int jsonDecimals(float v) {
  float a = std::fabs(v);
  int digits = 1;
  while (a >= 10.0f && digits < 6) {
    a /= 10.0f;
    ++digits;
  }
  return 6 - digits;
}

inline void jsonNumber(JsonWriter& w, FieldType t, float v) {
  switch (t) {
    case FieldType::Float: w.valueTrim(v, jsonDecimals(v)); break;
    case FieldType::U8:
    case FieldType::U16: w.value(static_cast<long>(v)); break;
    case FieldType::Bool: w.value(v != 0.0f); break;
  }
}

constexpr const char* typeName(FieldType t) {  // constexpr: kConfigJsonSize (S9)
  switch (t) {
    case FieldType::Float: return "float";
    case FieldType::U8: return "u8";
    case FieldType::U16: return "u16";
    case FieldType::Bool: return "bool";
  }
  return "float";
}

}  // namespace detail

inline int configFieldCount() { return kConfigFieldCount; }

// Rekisteri funktion staattisena taulukkona: inline-funktion staattinen on
// yksi ja sama kaikissa käännösyksiköissä, ja taulukko on flashissa (constexpr).
// Indeksi alueen ulkopuolella -> tyhjä vartija (key ""), jota mikään avain ei vastaa.
inline const FieldInfo& configField(int i) {
  static constexpr FieldInfo kFields[] = {
#define SBX_CFG_INFO(id, member, key, type, mn, mx, group, unit, label) \
  {key, label, unit, group, FieldType::type, mn, mx},
      SBX_CONFIG_FIELDS(SBX_CFG_INFO)
#undef SBX_CFG_INFO
  };
  static_assert(sizeof(kFields) / sizeof(kFields[0]) == static_cast<size_t>(kConfigFieldCount),
                "rekisteri ja ConfigFieldId eri mittaisia");
  static constexpr FieldInfo kNone = {"", "", "", "", FieldType::Float, 0.0f, 0.0f};
  if (i < 0 || i >= kConfigFieldCount) return kNone;
  return kFields[i];
}

inline int configFindField(const char* key) {
  if (key == nullptr) return -1;
  for (int i = 0; i < kConfigFieldCount; ++i) {
    if (std::strcmp(configField(i).key, key) == 0) return i;
  }
  return -1;
}

// Kentän arvo floatina (U8/U16/Bool/SourceKind tarkasti: kaikki < 2^24).
// Tuntematon indeksi -> NaN, jota ei voi sekoittaa oikeaan arvoon.
inline float configGet(const Config& c, int i) {
  if (i < 0 || i >= kConfigFieldCount) return std::numeric_limits<float>::quiet_NaN();
  switch (static_cast<ConfigFieldId>(i)) {
#define SBX_CFG_GET(id, member, key, type, mn, mx, group, unit, label) \
  case ConfigFieldId::id: return detail::cfgLoad(c.member);
    SBX_CONFIG_FIELDS(SBX_CFG_GET)
#undef SBX_CFG_GET
    case ConfigFieldId::Count: break;
  }
  return std::numeric_limits<float>::quiet_NaN();
}

enum class SetResult : uint8_t { Ok, UnknownKey, OutOfRange, Invalid };

// Alue- ja tyyppitarkistus; kenttien välinen ristiintarkistus on configValidate.
// Hylätty arvo ei muuta konfiguraatiota.
inline SetResult configSet(Config& c, int i, float v) {
  if (i < 0 || i >= kConfigFieldCount) return SetResult::UnknownKey;
  const FieldInfo& f = configField(i);
  if (!std::isfinite(v)) return SetResult::Invalid;
  // Kokonaislukukenttään ei pyöristetä hiljaa: "15,5" syklille on kirjoitusvirhe.
  if (f.type != FieldType::Float && std::floor(v) != v) return SetResult::Invalid;
  if (v < f.min || v > f.max) return SetResult::OutOfRange;
  switch (static_cast<ConfigFieldId>(i)) {
#define SBX_CFG_SET(id, member, key, type, mn, mx, group, unit, label) \
  case ConfigFieldId::id: detail::cfgStore(c.member, v); break;
    SBX_CONFIG_FIELDS(SBX_CFG_SET)
#undef SBX_CFG_SET
    case ConfigFieldId::Count: return SetResult::UnknownKey;
  }
  return SetResult::Ok;
}

// Web-lomakkeelta (URL-dekoodattu teksti). Puhelimen numeronäppäimistö antaa
// suomalaisella lokaalilla desimaalipilkun, joten sekä "1,5" että "1.5"
// kelpaavat. Bool: true/false/1/0 (kirjainkoosta riippumatta) sekä on/off,
// jonka HTML-valintaruutu lähettää oletuksena. Kaikki muu hylätään: sallitut
// merkit ovat numerot, etumerkki, yksi erotin ja eksponentti, joten "nan",
// "inf" ja heksadesimaali ("0x10") eivät pääse strtof:n läpi, ja strtof:n
// loppuosoitin varmistaa ettei perään jää roskaa ("1,5,0", "12abc").
// strtof noudattaa LC_NUMERICia. Core ei kutsu setlocalea (ESP32:n newlib on
// aina "C"), mutta PC-työkalu voi: pilkkulokaalissa "1.25" ei jäsentyisi.
// Siksi kumpikin erotin muutetaan C-kirjaston nykyiseksi desimaalimerkiksi -
// sama lokaaliriippumattomuus kuin formatFixed:llä toiseen suuntaan.
inline SetResult configSetText(Config& c, const char* key, const char* text) {
  const int i = configFindField(key);
  if (i < 0) return SetResult::UnknownKey;
  if (text == nullptr) return SetResult::Invalid;
  while (*text == ' ' || *text == '\t') ++text;
  size_t len = std::strlen(text);
  while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\r' ||
                     text[len - 1] == '\n')) {
    --len;
  }
  char tmp[32];
  if (len == 0 || len >= sizeof(tmp)) return SetResult::Invalid;
  std::memcpy(tmp, text, len);
  tmp[len] = '\0';

  if (configField(i).type == FieldType::Bool) {
    if (detail::ieq(tmp, "true") || detail::ieq(tmp, "on")) return configSet(c, i, 1.0f);
    if (detail::ieq(tmp, "false") || detail::ieq(tmp, "off")) return configSet(c, i, 0.0f);
  }
  const char* dp = std::localeconv()->decimal_point;
  // Monitavuinen desimaalimerkki (harvinainen lokaali) -> piste; silloin
  // syöte hylkääntyy eikä tulkitu väärin.
  const char sep = (dp != nullptr && dp[0] != '\0' && dp[1] == '\0') ? dp[0] : '.';
  for (size_t k = 0; k < len; ++k) {
    const char ch = tmp[k];
    if (ch == ',' || ch == '.') {
      tmp[k] = sep;
    } else if (!((ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == 'e' || ch == 'E')) {
      return SetResult::Invalid;
    }
  }
  char* end = nullptr;
  const float v = std::strtof(tmp, &end);
  if (end == tmp || *end != '\0') return SetResult::Invalid;
  return configSet(c, i, v);
}

// Lyhyt suomenkielinen selitys web-vastaukseen {"ok":false,"err":"…"}.
inline const char* setResultText(SetResult r) {
  switch (r) {
    case SetResult::Ok: return "OK";
    case SetResult::UnknownKey: return "Tuntematon asetus";
    case SetResult::OutOfRange: return "Arvo sallitun alueen ulkopuolella";
    case SetResult::Invalid: return "Virheellinen arvo – anna luku (desimaalierotin pilkku tai piste)";
  }
  return "Virheellinen arvo";
}

// Kenttien keskinäinen johdonmukaisuus. Palauttaa false ja suomenkielisen
// virheen (ensimmäinen löydetty) kun yhdistelmä ei ole järkevä. Tarkistaa myös
// jokaisen kentän alueen, koska rakennetta voi muuttaa suoraan ohi configSetin
// (esim. NVS-liima, joka lataa vanhan skeeman arvon).
inline bool configValidate(const Config& c, char* err, size_t n) {
  const bool have_err = err != nullptr && n > 0;
  auto fail = [&](const char* msg) {
    if (have_err) std::snprintf(err, n, "%s", msg);
    return false;
  };
  if (have_err) err[0] = '\0';
  char a[24], b[24], d[24];

  for (int i = 0; i < kConfigFieldCount; ++i) {
    const FieldInfo& f = configField(i);
    const float v = configGet(c, i);
    if (!(v >= f.min && v <= f.max)) {
      if (have_err) {
        std::snprintf(err, n, "%s (%s): arvo %s ei ole sallitulla välillä %s…%s", f.label, f.key,
                      std::isfinite(v) ? detail::fiNum(a, sizeof(a), v) : "NaN",
                      detail::fiNum(b, sizeof(b), f.min), detail::fiNum(d, sizeof(d), f.max));
      }
      return false;
    }
  }

  const StatusParams& st = c.status;
  const DspParams& dp = c.dsp;
  const SensorParams& se = c.sensor;
  // D2: keltainen alue on vihreän ulkopuolella; muuten tila 4a ei voi syntyä.
  if (!(st.tol_green_kpa < st.tol_yellow_kpa)) {
    return fail("Vihreän rajan (tol_green_kpa) on oltava pienempi kuin keltaisen rajan "
                "(tol_yellow_kpa)");
  }
  if (!(dp.rpm_min < dp.rpm_max)) {
    return fail("Syklintunnistuksen alimman kierrosluvun (rpm_min) on oltava pienempi kuin "
                "ylimmän (rpm_max)");
  }
  if (!(st.sync_rpm_min < st.sync_rpm_max)) {
    return fail("Arvion alarajan (sync_rpm_min) on oltava pienempi kuin ylärajan "
                "(sync_rpm_max)");
  }
  // D12: arvio annetaan vain kierrosluvuilla, joilla syklintunnistus toimii.
  // Muuten rivi 9 (rpm > sync_rpm_max) ei koskaan laukeaisi, koska DSP hylkää
  // jaksot jo ennen sitä, ja kovalla kaasulla näkyisi väärä tila.
  if (!(dp.rpm_min <= st.sync_rpm_min && st.sync_rpm_max <= dp.rpm_max)) {
    return fail("Arvion kierroslukuikkunan (sync_rpm_min…sync_rpm_max) on oltava "
                "syklintunnistuksen alueen (rpm_min…rpm_max) sisällä");
  }
  // Pisin hyväksytty jakso on 120 / rpm_min (yksi imupulssi = 2 kierrosta).
  // Jos aikakatkaisu on lyhyempi, pulssi katoaisi syklien välissä hitaalla käynnillä.
  const float max_period_s = 120.0f / dp.rpm_min;
  if (!(dp.pulse_timeout_s > max_period_s)) {
    if (have_err) {
      std::snprintf(err, n,
                    "Pulssin aikakatkaisun (pulse_timeout_s) on oltava pidempi kuin yksi sykli "
                    "alimmalla kierrosluvulla: 120 / rpm_min = %s s",
                    detail::fiNum(a, sizeof(a), max_period_s));
    }
    return false;
  }
  // Rivi 7 / rivi 10: saturaatio on normaalin alueen yläpää, anturivika sen
  // ulkopuolella. Väärässä järjestyksessä saturaatio näkyisi anturivikana.
  if (!(se.fault_lo_mv < se.sat_mv && se.sat_mv < se.fault_hi_mv)) {
    if (have_err) {
      std::snprintf(err, n,
                    "Jänniterajojen järjestyksen on oltava fault_lo_mv < sat_mv < fault_hi_mv "
                    "(nyt %u / %u / %u mV)",
                    static_cast<unsigned>(se.fault_lo_mv), static_cast<unsigned>(se.sat_mv),
                    static_cast<unsigned>(se.fault_hi_mv));
    }
    return false;
  }
  // Ilmanpaine (moottori seis, rivi 1) ei saa näyttää anturivialta, muuten
  // automaattinen nollaus ei koskaan pääse ajoon.
  if (!(se.fault_lo_mv < se.v_zero_mv && se.v_zero_mv < se.fault_hi_mv)) {
    return fail("Anturin nollapisteen (v_zero_mv) on oltava anturivikarajojen "
                "(fault_lo_mv…fault_hi_mv) välissä – muuten ilmanpaine näkyisi anturivikana");
  }
  // D10: off_kpa tunnistaa ilmanpaineen kalibroimattomanakin (anturin ±2 kPa
  // mukana), hose_off_kpa kalibroidusta arvosta - jälkimmäinen on siksi tiukempi.
  if (!(st.hose_off_kpa <= st.off_kpa)) {
    return fail("Letkuvian raja (hose_off_kpa) saa olla enintään ei signaalia -raja (off_kpa)");
  }
  if (!(st.master < kChannels)) {
    return fail("Master-sylinterin (master) on oltava 0, 1 tai 2");
  }
  return true;
}

// {"v":1,"fields":[{"key","label","unit","group","type","min","max","def","value"}...]}
// Huoltosivu rakentaa lomakkeen tästä kokonaan, joten uusi kenttä näkyy
// UI:ssa ilman UI-muutosta. Palauttaa pituuden ilman NULia, 0 = ei mahtunut.
inline size_t configToJson(const Config& c, char* buf, size_t n) {
  const Config def{};
  JsonWriter w(buf, n);
  w.beginObject();
  w.key("v").value(static_cast<int>(kConfigSchemaVersion));
  w.key("fields").beginArray();
  for (int i = 0; i < kConfigFieldCount; ++i) {
    const FieldInfo& f = configField(i);
    w.beginObject();
    w.key("key").value(f.key);
    w.key("label").value(f.label);
    w.key("unit").value(f.unit);
    w.key("group").value(f.group);
    w.key("type").value(detail::typeName(f.type));
    // Bool-kentän min/max ovat lukuja 0/1, arvo ja oletus JSON-totuusarvoja.
    const FieldType range_t = f.type == FieldType::Bool ? FieldType::U8 : f.type;
    w.key("min");
    detail::jsonNumber(w, range_t, f.min);
    w.key("max");
    detail::jsonNumber(w, range_t, f.max);
    w.key("def");
    detail::jsonNumber(w, f.type, configGet(def, i));
    w.key("value");
    detail::jsonNumber(w, f.type, configGet(c, i));
    w.endObject();
  }
  w.endArray();
  w.endObject();
  return w.ok() ? w.length() : 0;
}

// ---------------------------------------------------------------------------
// configToJsonin puskurin koko johdetaan kenttälistasta käännösaikana (S9).
//
// Ennen S9:ää koko oli käsin valittu 6144, ja pahin tapaus oli 5470 tavua
// 35 kentällä (WP-9, 31.10.2026), joten muutama uusi kenttä olisi vaatinut
// noston. Nyt jokainen SBX_CONFIG_FIELDS-rivi kasvattaa kConfigJsonSizea oman
// ylärajansa verran: merkkijonot lasketaan tarkasti escapauksineen ja luvut
// tyypin ja alueen [min, max] leveimmän muotoilun mukaan. test_config todistaa
// kentittäin, ettei configToJson ylitä rajaa millään alueen arvolla, joten
// ylivuoto jää testiin eikä pääse laitteelle.
//
// Raja koskee arvoja alueella [min, max]. Processor ottaa käyttöön vain
// configValidaten hyväksymän Configin, ja configValidate tarkistaa jokaisen
// kentän alueen, joten laitteen GET /api/config ei saa koon takia 0:aa (500).
// ---------------------------------------------------------------------------
namespace detail {

// JsonWriter::writeStringin tuottama pituus lainausmerkkeineen: " ja \ kahtena,
// ohjausmerkki < 0x20 kuutena (\u00XX), muut tavut (myös UTF-8) sellaisenaan.
constexpr size_t jsonStringLength(const char* s) {
  size_t n = 2;
  for (; *s != '\0'; ++s) {
    const unsigned char c = static_cast<unsigned char>(*s);
    n += (c == '"' || c == '\\') ? 2u : (c < 0x20 ? 6u : 1u);
  }
  return n;
}

// "%ld"-muotoillun kokonaisluvun pituus etumerkkeineen.
constexpr size_t jsonIntWidth(long v) {
  size_t n = v < 0 ? 2u : 1u;
  for (long a = v < 0 ? -v : v; a >= 10; a /= 10) ++n;
  return n;
}

// jsonNumber(Float) = valueTrim(v, 6 - k), jossa k on kokonaisosan numerot
// (jsonDecimals, enintään 6). Ennen trimmausta "%.*f" antaa k numeroa, +1 jos
// pyöristys kantaa (9.999996 -> "10.00000"), sekä pisteen ja 6 - k desimaalia:
// k < 6 -> enintään 8 merkkiä, k >= 6 -> ei desimaaleja, enintään k + 1.
// jsonDecimalsin float-jako voi arvioida k:n vain yläkanttiin (kymmenen potenssit
// ovat floatina tarkkoja ja pyöristys monotoninen), mikä vähentää desimaaleja;
// trimmaus ja "-0" -> "0" vain lyhentävät. Miinusmerkki +1.
constexpr size_t jsonFloatWidth(float mn, float mx) {
  float m = mn < 0.0f ? -mn : mn;
  if ((mx < 0.0f ? -mx : mx) > m) m = mx < 0.0f ? -mx : mx;
  size_t k = 1;
  for (; m >= 10.0f; m /= 10.0f) ++k;
  return (mn < 0.0f ? 1u : 0u) + (k + 1 > 8 ? k + 1 : 8u);
}

// Luvun leveys, kun arvo on alueella [mn, mx]. U8/U16 kirjoitetaan "%ld":llä,
// joten leveämpi päätepiste on raja. Bool: "false".
constexpr size_t jsonNumberWidth(FieldType t, float mn, float mx) {
  switch (t) {
    case FieldType::Float: return jsonFloatWidth(mn, mx);
    case FieldType::U8:
    case FieldType::U16: {
      const size_t a = jsonIntWidth(static_cast<long>(mn));
      const size_t b = jsonIntWidth(static_cast<long>(mx));
      return a > b ? a : b;
    }
    case FieldType::Bool: return sizeof("false") - 1;
  }
  return 0;
}

// Yhden kenttäobjektin yläraja ilman erottavaa pilkkua. Kiinteä osa vastaa
// configToJsonin key()-kutsuja järjestyksessä; min/max kirjoitetaan Bool-kentällä
// U8:na (0/1) kuten configToJsonissa.
constexpr size_t configFieldJsonBound(const char* key, const char* label, const char* unit,
                                      const char* group, FieldType t, float mn, float mx) {
  const size_t range_w = jsonNumberWidth(t == FieldType::Bool ? FieldType::U8 : t, mn, mx);
  const size_t value_w = jsonNumberWidth(t, mn, mx);
  return sizeof("{\"key\":,\"label\":,\"unit\":,\"group\":,\"type\":,"
                "\"min\":,\"max\":,\"def\":,\"value\":}") - 1 +
         jsonStringLength(key) + jsonStringLength(label) + jsonStringLength(unit) +
         jsonStringLength(group) + jsonStringLength(typeName(t)) + 2 * range_w + 2 * value_w;
}

// Kenttäkohtaiset ylärajat rekisterin järjestyksessä (test_config vertaa
// jokaista mitattuun). Vain käännösaikainen käyttö laitteella: ei flashia.
inline constexpr size_t kConfigFieldJsonBound[] = {
#define SBX_CFG_JSON_BOUND(id, member, key, type, mn, mx, group, unit, label) \
  configFieldJsonBound(key, label, unit, group, FieldType::type, mn, mx),
    SBX_CONFIG_FIELDS(SBX_CFG_JSON_BOUND)
#undef SBX_CFG_JSON_BOUND
};
static_assert(sizeof(kConfigFieldJsonBound) / sizeof(kConfigFieldJsonBound[0]) ==
                  static_cast<size_t>(kConfigFieldCount),
              "kenttärajoja eri määrä kuin kenttiä");

// {"v":<versio>,"fields":[<kentät pilkuin erotettuina>]} + NUL.
constexpr size_t configJsonBound() {
  size_t n = sizeof("{\"v\":,\"fields\":[]}") - 1 + jsonIntWidth(kConfigSchemaVersion);
  for (int i = 0; i < kConfigFieldCount; ++i) n += kConfigFieldJsonBound[i] + (i > 0 ? 1u : 0u);
  return n + 1;
}

}  // namespace detail

// Puskuri configToJson:lle (GET /api/config, ei näytepolulla; test_config
// tulostaa koon). web.cpp varaa sen keosta pyynnön ajaksi async_tcp-taskissa,
// ja AsyncBasicResponse kopioi JSONin vielä omaan Stringiinsä (ESPAsyncWebServer
// 3.12.1, WebResponses.cpp:260-263), joten hetkellinen huippu on ~2 x koko.
constexpr size_t kConfigJsonSize = detail::configJsonBound();

// Keon kestävyyden katto (S9:n ehdotus, ei D-päätös). Vapaa keko on laitteella
// ~89 kt (jatko.md), ja laitetestin raja minimille on 40 kt. 8 kt:n puskuri ja
// sen kopio (~16 kt hetkellisesti) mahtuvat siihen väliin. Katto tulee vastaan
// noin 13 uuden kentän jälkeen (~167 t/kenttä); silloin päätetään erikseen,
// kasvatetaanko kattoa vai tiivistetäänkö JSONia (rajapintamuutos).
constexpr size_t kConfigJsonMaxSize = 8192;
static_assert(kConfigJsonSize <= kConfigJsonMaxSize,
              "konfiguraatio-JSON kasvoi yli keon katon kConfigJsonMaxSize: ks. config.h (S9)");

}  // namespace sbx
