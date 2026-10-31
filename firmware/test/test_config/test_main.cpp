// Config + kenttärekisteri (docs/firmware.md §4.4, S5a). Rekisteri on NVS:n,
// huoltosivun ja POST-käsittelyn yhteinen totuus: väärin kytketty avain
// tallentaisi arvon väärään jäseneen hiljaa, ja puuttuva kenttä olisi
// säätämätön laiturilla. Testit tarkistavat siksi jokaisen kentän erikseen.

#include <unity.h>

#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include "core/config.h"
#include <json_validate.h>

using sbx::Config;
using sbx::FieldInfo;
using sbx::FieldType;
using sbx::SetResult;

void setUp() {}
void tearDown() {}

// Shimin EQUAL_INT vertaa kokonaislukuja: enum class -> int.
static int R(SetResult r) { return static_cast<int>(r); }

// ---------------------------------------------------------------------------
// Aggregaatin kenttien määrä käännösaikana (Boost.PFR:n tekniikka): montako
// "mihin tahansa muuntuvaa" alustajaa T{...} hyväksyy. Näin uusi jäsen
// types.h:ssa ilman rekisterirviä kaataa testin eikä jää huomaamatta.
// ---------------------------------------------------------------------------
struct AnyField {
  template <typename T>
  operator T() const;  // vain decltype-kontekstissa, ei määritelmää
};
template <typename T, typename... A>
constexpr auto bracesOk(int) -> decltype(T{std::declval<A>()...}, true) { return true; }
template <typename T, typename... A>
constexpr bool bracesOk(...) { return false; }
template <typename T, typename... A>
constexpr size_t aggregateFields() {
  if constexpr (bracesOk<T, A..., AnyField>(0)) {
    return aggregateFields<T, A..., AnyField>();
  } else {
    return sizeof...(A);
  }
}

static int fieldsInGroup(const char* group) {
  int n = 0;
  for (int i = 0; i < sbx::configFieldCount(); ++i) {
    if (std::strcmp(sbx::configField(i).group, group) == 0) ++n;
  }
  return n;
}

// Jokainen types.h:n jäsen avaimineen, lukijoineen ja koearvoineen (eri kuin
// oletus, sallitulla alueella ja ristiintarkistukset rikkomatta).
struct Expect {
  const char* key;
  float probe;
  float (*read)(const Config&);
};
static const Expect kExpect[] = {
    {"v_zero_mv", 2650.0f, [](const Config& c) { return c.sensor.v_zero_mv; }},
    {"mv_per_kpa", 24.0f, [](const Config& c) { return c.sensor.mv_per_kpa; }},
    {"fault_lo_mv", 120.0f, [](const Config& c) { return static_cast<float>(c.sensor.fault_lo_mv); }},
    {"fault_hi_mv", 3100.0f, [](const Config& c) { return static_cast<float>(c.sensor.fault_hi_mv); }},
    {"fault_hold_s", 1.5f, [](const Config& c) { return c.sensor.fault_hold_s; }},
    {"sat_mv", 2650.0f, [](const Config& c) { return static_cast<float>(c.sensor.sat_mv); }},
    {"rail_comp", 1.0f, [](const Config& c) { return c.sensor.rail_comp ? 1.0f : 0.0f; }},
    {"rail_nominal_mv", 3250.0f, [](const Config& c) { return c.sensor.rail_nominal_mv; }},
    {"lp_cutoff_hz", 35.0f, [](const Config& c) { return c.dsp.lp_cutoff_hz; }},
    {"level_tau_s", 0.75f, [](const Config& c) { return c.dsp.level_tau_s; }},
    {"hyst_frac", 0.25f, [](const Config& c) { return c.dsp.hyst_frac; }},
    {"hyst_min_kpa", 0.4f, [](const Config& c) { return c.dsp.hyst_min_kpa; }},
    {"avg_cycles", 12.0f, [](const Config& c) { return static_cast<float>(c.dsp.avg_cycles); }},
    {"rpm_min", 250.0f, [](const Config& c) { return c.dsp.rpm_min; }},
    {"rpm_max", 7000.0f, [](const Config& c) { return c.dsp.rpm_max; }},
    {"pulse_timeout_s", 1.25f, [](const Config& c) { return c.dsp.pulse_timeout_s; }},
    {"rpm_hist", 24.0f, [](const Config& c) { return static_cast<float>(c.dsp.rpm_hist); }},
    {"master", 2.0f, [](const Config& c) { return static_cast<float>(c.status.master); }},
    {"tol_green_kpa", 1.25f, [](const Config& c) { return c.status.tol_green_kpa; }},
    {"tol_yellow_kpa", 2.5f, [](const Config& c) { return c.status.tol_yellow_kpa; }},
    {"off_kpa", 6.0f, [](const Config& c) { return c.status.off_kpa; }},
    {"off_hold_s", 4.0f, [](const Config& c) { return c.status.off_hold_s; }},
    {"hose_off_kpa", 1.5f, [](const Config& c) { return c.status.hose_off_kpa; }},
    {"unstable_cv", 0.2f, [](const Config& c) { return c.status.unstable_cv; }},
    {"stable_hold_s", 2.0f, [](const Config& c) { return c.status.stable_hold_s; }},
    {"sync_rpm_min", 600.0f, [](const Config& c) { return c.status.sync_rpm_min; }},
    {"sync_rpm_max", 2500.0f, [](const Config& c) { return c.status.sync_rpm_max; }},
    {"drift_rpm", 40.0f, [](const Config& c) { return c.status.drift_rpm; }},
    {"drift_window_s", 3.0f, [](const Config& c) { return c.status.drift_window_s; }},
    {"auto_zero", 0.0f, [](const Config& c) { return c.status.auto_zero ? 1.0f : 0.0f; }},
    {"hose_hyst_kpa", 0.5f, [](const Config& c) { return c.status.hose_hyst_kpa; }},
    {"drift_tau_s", 1.0f, [](const Config& c) { return c.status.drift_tau_s; }},
    {"zero_settle_kpa", 0.2f, [](const Config& c) { return c.status.zero_settle_kpa; }},
    {"source", 1.0f, [](const Config& c) { return static_cast<float>(static_cast<uint8_t>(c.source)); }},
    {"push_rate_hz", 10.0f, [](const Config& c) { return static_cast<float>(c.push_rate_hz); }},
};
constexpr int kExpectCount = static_cast<int>(sizeof(kExpect) / sizeof(kExpect[0]));

// --- rekisterin kattavuus ----------------------------------------------------

void test_registry_covers_every_struct_member() {
  // Rakenteiden jäsenmäärät suoraan types.h:sta -> ryhmien koot rekisterissä.
  constexpr size_t n_sensor = aggregateFields<sbx::SensorParams>();
  constexpr size_t n_dsp = aggregateFields<sbx::DspParams>();
  constexpr size_t n_status = aggregateFields<sbx::StatusParams>();
  static_assert(n_sensor == 8 && n_dsp == 9 && n_status == 16,
                "types.h muuttui: lisää uusi kenttä SBX_CONFIG_FIELDS-listaan ja tähän testiin");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(n_sensor), fieldsInGroup(sbx::kGroupSensor));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(n_dsp), fieldsInGroup(sbx::kGroupSignal));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(n_status), fieldsInGroup(sbx::kGroupSync));
  TEST_ASSERT_EQUAL_INT(2, fieldsInGroup(sbx::kGroupSystem));  // source + push_rate_hz
  TEST_ASSERT_EQUAL_INT(static_cast<int>(n_sensor + n_dsp + n_status + 2), sbx::configFieldCount());
  TEST_ASSERT_EQUAL_INT(kExpectCount, sbx::configFieldCount());
}

void test_every_key_maps_to_its_own_member() {
  // Asetetaan kenttä kerrallaan avaimella ja tarkistetaan, että juuri oikea
  // jäsen muuttui eikä mikään muu (ristiin kytketty avain jäisi muuten piiloon,
  // koska get/set-kierros onnistuisi silti).
  for (int e = 0; e < kExpectCount; ++e) {
    const Expect& x = kExpect[e];
    const Config def;
    Config c;
    const int idx = sbx::configFindField(x.key);
    TEST_ASSERT_TRUE_MESSAGE(idx >= 0, x.key);
    TEST_ASSERT_TRUE_MESSAGE(x.read(def) != x.probe, x.key);  // koearvo eroaa oletuksesta
    char text[32];
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(x.probe));
    TEST_ASSERT_EQUAL_INT_MESSAGE(R(SetResult::Ok),
                                  R(sbx::configSetText(c, x.key, text)), x.key);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(x.probe, x.read(c), x.key);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(x.probe, sbx::configGet(c, idx), x.key);
    for (int i = 0; i < sbx::configFieldCount(); ++i) {
      if (i == idx) continue;
      TEST_ASSERT_EQUAL_FLOAT_MESSAGE(sbx::configGet(def, i), sbx::configGet(c, i),
                                      sbx::configField(i).key);
    }
    char err[160];
    TEST_ASSERT_TRUE_MESSAGE(sbx::configValidate(c, err, sizeof(err)), err);
  }
}

void test_keys_labels_groups_ranges_wellformed() {
  const char* groups[] = {sbx::kGroupSync, sbx::kGroupSignal, sbx::kGroupSensor, sbx::kGroupSystem};
  for (int i = 0; i < sbx::configFieldCount(); ++i) {
    const FieldInfo& f = sbx::configField(i);
    const size_t len = std::strlen(f.key);
    TEST_ASSERT_TRUE_MESSAGE(len >= 1 && len <= 15, f.key);  // NVS_KEY_NAME_MAX_SIZE - 1
    for (size_t k = 0; k < len; ++k) {
      const char ch = f.key[k];
      TEST_ASSERT_TRUE_MESSAGE((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_', f.key);
    }
    for (int j = i + 1; j < sbx::configFieldCount(); ++j) {
      TEST_ASSERT_TRUE_MESSAGE(std::strcmp(f.key, sbx::configField(j).key) != 0, f.key);
    }
    TEST_ASSERT_EQUAL_INT(i, sbx::configFindField(f.key));
    TEST_ASSERT_NOT_NULL(f.label);
    TEST_ASSERT_TRUE_MESSAGE(std::strlen(f.label) >= 5, f.key);
    TEST_ASSERT_NOT_NULL(f.unit);
    bool group_ok = false;
    for (const char* g : groups) group_ok = group_ok || std::strcmp(f.group, g) == 0;
    TEST_ASSERT_TRUE_MESSAGE(group_ok, f.key);
    TEST_ASSERT_TRUE_MESSAGE(f.min < f.max, f.key);
    if (f.type == FieldType::Bool) {
      TEST_ASSERT_EQUAL_FLOAT(0.0f, f.min);
      TEST_ASSERT_EQUAL_FLOAT(1.0f, f.max);
    }
    if (f.type == FieldType::U8) TEST_ASSERT_TRUE_MESSAGE(f.min >= 0.0f && f.max <= 255.0f, f.key);
    if (f.type == FieldType::U16) TEST_ASSERT_TRUE_MESSAGE(f.min >= 0.0f && f.max <= 65535.0f, f.key);
    if (f.type != FieldType::Float) {
      TEST_ASSERT_TRUE_MESSAGE(std::floor(f.min) == f.min && std::floor(f.max) == f.max, f.key);
    }
  }
  TEST_ASSERT_EQUAL_INT(-1, sbx::configFindField("ei_ole"));
  TEST_ASSERT_EQUAL_INT(-1, sbx::configFindField(""));
  TEST_ASSERT_EQUAL_INT(-1, sbx::configFindField(nullptr));
  TEST_ASSERT_EQUAL_INT(-1, sbx::configFindField("MASTER"));  // avaimet ovat kirjainkooltaan tarkkoja
  TEST_ASSERT_EQUAL_STRING("", sbx::configField(-1).key);
  TEST_ASSERT_EQUAL_STRING("", sbx::configField(sbx::configFieldCount()).key);
}

void test_ranges_match_physics_and_dsp_capacity() {
  auto range = [](const char* key, float lo, float hi) {
    const FieldInfo& f = sbx::configField(sbx::configFindField(key));
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(lo, f.min, key);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(hi, f.max, key);
  };
  range("avg_cycles", 1.0f, 32.0f);    // Dsp::kMaxAvgCycles
  range("rpm_hist", 2.0f, 64.0f);      // Dsp::kMaxRpmHist
  range("master", 0.0f, 2.0f);         // kChannels
  range("source", 0.0f, 2.0f);         // SourceKind
  range("push_rate_hz", 4.0f, 30.0f);  // wave.s ≤ 16 riviä 62,5 Hz:stä
  // Kaikki rauta.md §1.2:n anturiversiot mahtuvat: GN 2700 mV / 25 mV/kPa,
  // GPN 1450 / 12,5, 5 V + jakaja 2700 / 24.
  const FieldInfo& vz = sbx::configField(sbx::configFindField("v_zero_mv"));
  const FieldInfo& k = sbx::configField(sbx::configFindField("mv_per_kpa"));
  TEST_ASSERT_TRUE(vz.min <= 1450.0f && vz.max >= 2700.0f && vz.max <= 3300.0f);
  TEST_ASSERT_TRUE(k.min <= 12.5f && k.max >= 25.0f && k.min > 0.0f);
  // Rivi 7 ei saa olla kytkettävissä pois alueen sisällä: rikkinäinen kanava
  // jää ADC:n ääripäähän (0 / 3300 mV), simulaattorin SensorLow ≤ 30 mV ja
  // SensorHigh ≥ 3270 mV. Jokaisen sallitun rajan on osuttava niihin
  // vähintään 20 mV:n marginaalilla, ja oletusten on oltava alueella.
  range("fault_lo_mv", 50.0f, 500.0f);
  range("fault_hi_mv", 2500.0f, 3250.0f);
  const FieldInfo& lo = sbx::configField(sbx::configFindField("fault_lo_mv"));
  const FieldInfo& hi = sbx::configField(sbx::configFindField("fault_hi_mv"));
  TEST_ASSERT_GREATER_THAN_FLOAT(30.0f + 20.0f - 0.5f, lo.min);
  TEST_ASSERT_LESS_THAN_FLOAT(3270.0f - 20.0f + 0.5f, hi.max);
  // Normaali käyttöalue on rajojen sisällä: GN:n −75 kPa:n pulssi (825 mV)
  // ja ilmanpaine (2700 mV) eivät ole vikoja millään sallitulla rajalla.
  TEST_ASSERT_LESS_THAN_FLOAT(825.0f, lo.max);
  TEST_ASSERT_GREATER_THAN_FLOAT(2700.0f - 200.0f - 0.5f, hi.min);
}

void test_wp9_proposed_fields_registered() {
  // WP-9 (31.10.2026): kolme uutta kynnystä ovat S5a:n mukaan huoltosivulla
  // säädettäviä. Oletukset ovat EHDOTUKSIA omistajalle (eivät päätöslokissa),
  // mittausperusteet status.h:ssa ja calibration.h:ssa. Testi lukitsee ne,
  // jotta muutos on tietoinen.
  auto range = [](const char* key, float lo, float hi) {
    const int i = sbx::configFindField(key);
    TEST_ASSERT_TRUE_MESSAGE(i >= 0, key);
    const FieldInfo& f = sbx::configField(i);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(lo, f.min, key);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(hi, f.max, key);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(sbx::kGroupSync, f.group, key);
  };
  // 0 = ominaisuus pois eli WP-4:n käytös (valittavissa, jos ehdotus hylätään).
  range("hose_hyst_kpa", 0.0f, 5.0f);
  range("drift_tau_s", 0.0f, 5.0f);
  // Alaraja ADC-kohinan yläpuolella: 0 tekisi nollauksesta mahdottoman.
  range("zero_settle_kpa", 0.05f, 2.0f);
  const Config c;
  TEST_ASSERT_EQUAL_FLOAT(0.2f, c.status.hose_hyst_kpa);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, c.status.drift_tau_s);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, c.status.zero_settle_kpa);
  Config off;
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(sbx::configSetText(off, "hose_hyst_kpa", "0")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(sbx::configSetText(off, "drift_tau_s", "0")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(sbx::configSetText(off, "zero_settle_kpa", "0")));
  char err[160];
  TEST_ASSERT_TRUE_MESSAGE(sbx::configValidate(off, err, sizeof(err)), err);
}

// --- oletukset ja get/set ----------------------------------------------------

void test_defaults_in_range_and_valid() {
  const Config c;
  for (int i = 0; i < sbx::configFieldCount(); ++i) {
    const FieldInfo& f = sbx::configField(i);
    const float v = sbx::configGet(c, i);
    TEST_ASSERT_TRUE_MESSAGE(v >= f.min && v <= f.max, f.key);
  }
  char err[160] = "x";
  TEST_ASSERT_TRUE_MESSAGE(sbx::configValidate(c, err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_STRING("", err);
  // Päätösloki: D2, D7, D9, D10, D12, D14.
  TEST_ASSERT_EQUAL_FLOAT(1.5f, sbx::configGet(c, sbx::configFindField("tol_green_kpa")));
  TEST_ASSERT_EQUAL_FLOAT(3.0f, sbx::configGet(c, sbx::configFindField("tol_yellow_kpa")));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sbx::configGet(c, sbx::configFindField("master")));
  TEST_ASSERT_EQUAL_FLOAT(15.0f, sbx::configGet(c, sbx::configFindField("push_rate_hz")));
  TEST_ASSERT_EQUAL_FLOAT(5.0f, sbx::configGet(c, sbx::configFindField("off_kpa")));
  TEST_ASSERT_EQUAL_FLOAT(500.0f, sbx::configGet(c, sbx::configFindField("sync_rpm_min")));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sbx::configGet(c, sbx::configFindField("source")));  // Sim
}

void test_get_set_round_trip_every_field() {
  for (int i = 0; i < sbx::configFieldCount(); ++i) {
    const FieldInfo& f = sbx::configField(i);
    float mid = 0.5f * (f.min + f.max);
    if (f.type != FieldType::Float) mid = std::floor(mid);
    const float probes[] = {f.min, f.max, mid};
    for (float v : probes) {
      Config c;
      TEST_ASSERT_EQUAL_INT_MESSAGE(R(SetResult::Ok),
                                    R(sbx::configSet(c, i, v)), f.key);
      TEST_ASSERT_EQUAL_FLOAT_MESSAGE(v, sbx::configGet(c, i), f.key);
    }
  }
}

void test_set_rejects_and_leaves_value_untouched() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  for (int i = 0; i < sbx::configFieldCount(); ++i) {
    const FieldInfo& f = sbx::configField(i);
    Config c;
    const float before = sbx::configGet(c, i);
    const float step = f.type == FieldType::Float ? 0.001f * (f.max - f.min) : 1.0f;
    TEST_ASSERT_EQUAL_INT_MESSAGE(R(SetResult::OutOfRange),
                                  R(sbx::configSet(c, i, f.max + step)), f.key);
    if (f.min - step >= -1000.0f) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(R(SetResult::OutOfRange),
                                    R(sbx::configSet(c, i, f.min - step)), f.key);
    }
    TEST_ASSERT_EQUAL_INT(R(SetResult::Invalid), R(sbx::configSet(c, i, nan)));
    TEST_ASSERT_EQUAL_INT(R(SetResult::Invalid), R(sbx::configSet(c, i, inf)));
    TEST_ASSERT_EQUAL_INT(R(SetResult::Invalid), R(sbx::configSet(c, i, -inf)));
    if (f.type != FieldType::Float) {
      // Kokonaislukukenttää ei pyöristetä hiljaa.
      TEST_ASSERT_EQUAL_INT_MESSAGE(R(SetResult::Invalid),
                                    R(sbx::configSet(c, i, f.min + 0.5f)), f.key);
    }
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(before, sbx::configGet(c, i), f.key);
  }
  Config c;
  TEST_ASSERT_EQUAL_INT(R(SetResult::UnknownKey), R(sbx::configSet(c, -1, 1.0f)));
  TEST_ASSERT_EQUAL_INT(R(SetResult::UnknownKey),
                        R(sbx::configSet(c, sbx::configFieldCount(), 1.0f)));
  TEST_ASSERT_TRUE(std::isnan(sbx::configGet(c, -1)));
  TEST_ASSERT_TRUE(std::isnan(sbx::configGet(c, sbx::configFieldCount())));
}

// --- tekstisyöte (web-lomake) ----------------------------------------------

static SetResult setText(Config& c, const char* key, const char* text) {
  return sbx::configSetText(c, key, text);
}

void test_set_text_accepts_finnish_comma_and_point() {
  Config c;
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "tol_green_kpa", "1,75")));
  TEST_ASSERT_EQUAL_FLOAT(1.75f, c.status.tol_green_kpa);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "tol_green_kpa", "1.25")));
  TEST_ASSERT_EQUAL_FLOAT(1.25f, c.status.tol_green_kpa);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "unstable_cv", " 0,12 ")));
  TEST_ASSERT_EQUAL_FLOAT(0.12f, c.status.unstable_cv);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "tol_yellow_kpa", "+3,5\r\n")));
  TEST_ASSERT_EQUAL_FLOAT(3.5f, c.status.tol_yellow_kpa);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "v_zero_mv", "2.7e3")));
  TEST_ASSERT_EQUAL_FLOAT(2700.0f, c.sensor.v_zero_mv);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "stable_hold_s", ",5")));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, c.status.stable_hold_s);
}

void test_set_text_independent_of_locale() {
  // strtof noudattaa LC_NUMERICia. Jos PC-työkalu vaihtaa pilkkulokaaliin,
  // sekä "1,75" että "1.25" on yhä jäsennettävä (formatFixed on jo
  // lokaaliriippumaton toiseen suuntaan). ESP32:lla locale on aina "C".
  const char* candidates[] = {"fi_FI.UTF-8", "fi_FI.utf8", "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8"};
  const char* got = nullptr;
  for (const char* loc : candidates) {
    if (std::setlocale(LC_NUMERIC, loc) != nullptr) {
      got = loc;
      break;
    }
  }
  if (got == nullptr) {
    TEST_MESSAGE("ei pilkkulokaalia asennettuna - ohitetaan lokaalitarkistus");
    std::setlocale(LC_NUMERIC, "C");
    return;
  }
  const bool comma_locale = std::localeconv()->decimal_point[0] == ',';
  Config c;
  const SetResult r1 = setText(c, "tol_green_kpa", "1,75");
  const float v1 = c.status.tol_green_kpa;
  const SetResult r2 = setText(c, "tol_green_kpa", "1.25");
  const float v2 = c.status.tol_green_kpa;
  const SetResult r3 = setText(c, "tol_green_kpa", "1,2.5");
  const SetResult r4 = setText(c, "v_zero_mv", "2,7e3");
  const float v4 = c.sensor.v_zero_mv;
  // Palautetaan ennen tarkistuksia: epäonnistunut assert hyppää funktiosta ulos.
  std::setlocale(LC_NUMERIC, "C");
  TEST_MESSAGE(got);
  TEST_ASSERT_TRUE(comma_locale);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(r1));
  TEST_ASSERT_EQUAL_FLOAT(1.75f, v1);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(r2));
  TEST_ASSERT_EQUAL_FLOAT(1.25f, v2);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Invalid), R(r3));
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(r4));
  TEST_ASSERT_EQUAL_FLOAT(2700.0f, v4);
}

void test_set_text_rejects_garbage() {
  const char* bad[] = {"",     "   ",   "abc",   "1.5x",   "1,2,3", "1.2.3", "1,5.0", "nan",
                       "inf",  "-inf",  "0x10",  "1 5",    "--1",   "1e",    "e5",    ".",
                       ",",    "-",     "1;2",   "12345678901234567890123456789012345"};
  for (const char* b : bad) {
    Config c;
    const float before = c.status.tol_green_kpa;
    TEST_ASSERT_EQUAL_INT_MESSAGE(R(SetResult::Invalid),
                                  R(setText(c, "tol_green_kpa", b)), b);
    TEST_ASSERT_EQUAL_FLOAT(before, c.status.tol_green_kpa);
  }
  Config c;
  TEST_ASSERT_EQUAL_INT(R(SetResult::Invalid), R(setText(c, "tol_green_kpa", nullptr)));
  TEST_ASSERT_EQUAL_INT(R(SetResult::UnknownKey), R(setText(c, "tol_green", "1")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::UnknownKey), R(setText(c, nullptr, "1")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "tol_green_kpa", "150")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "tol_green_kpa", "-1,5")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "tol_green_kpa", "1e30")));
}

void test_set_text_bool() {
  struct Case { const char* text; SetResult r; bool v; };
  const Case cases[] = {
      {"true", SetResult::Ok, true},   {"false", SetResult::Ok, false}, {"1", SetResult::Ok, true},
      {"0", SetResult::Ok, false},     {"TRUE", SetResult::Ok, true},   {"False", SetResult::Ok, false},
      {"on", SetResult::Ok, true},     {"off", SetResult::Ok, false},   {" true ", SetResult::Ok, true},
      {"1,0", SetResult::Ok, true},    {"2", SetResult::OutOfRange, false},
      {"-1", SetResult::OutOfRange, false}, {"0,5", SetResult::Invalid, false},
      {"yes", SetResult::Invalid, false},   {"truee", SetResult::Invalid, false},
      {"tru", SetResult::Invalid, false},
  };
  for (const Case& k : cases) {
    Config c;
    c.status.auto_zero = !k.v;  // lähtöarvo vastakkainen, jotta muutos näkyy
    const bool before = c.status.auto_zero;
    const SetResult r = setText(c, "auto_zero", k.text);
    TEST_ASSERT_EQUAL_INT_MESSAGE(R(k.r), R(r), k.text);
    TEST_ASSERT_EQUAL_INT_MESSAGE(r == SetResult::Ok ? k.v : before, c.status.auto_zero, k.text);
  }
}

void test_set_text_integer_and_enum_fields() {
  Config c;
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "avg_cycles", "16")));
  TEST_ASSERT_EQUAL_UINT8(16, c.dsp.avg_cycles);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "avg_cycles", "12,0")));
  TEST_ASSERT_EQUAL_UINT8(12, c.dsp.avg_cycles);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Invalid), R(setText(c, "avg_cycles", "12,5")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "avg_cycles", "300")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "avg_cycles", "0")));
  TEST_ASSERT_EQUAL_UINT8(12, c.dsp.avg_cycles);
  // Rivi 7: ADC:n ääripäät (0 / 3300 mV) eivät kelpaa rajoiksi - niillä
  // vikatunnistus ei koskaan laukeaisi (dsp vertaa mv < lo / mv > hi).
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "fault_hi_mv", "3300")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "fault_hi_mv", "3251")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "fault_hi_mv", "3250")));
  TEST_ASSERT_EQUAL_UINT16(3250, c.sensor.fault_hi_mv);
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "fault_lo_mv", "0")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "fault_lo_mv", "49")));
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "fault_lo_mv", "50")));
  TEST_ASSERT_EQUAL_UINT16(50, c.sensor.fault_lo_mv);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "source", "2")));
  TEST_ASSERT_TRUE(c.source == sbx::SourceKind::Hx710b);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "source", "1")));
  TEST_ASSERT_TRUE(c.source == sbx::SourceKind::Adc);
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "source", "3")));
  TEST_ASSERT_TRUE(c.source == sbx::SourceKind::Adc);
  TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(setText(c, "master", "2")));
  TEST_ASSERT_EQUAL_UINT8(2, c.status.master);
  TEST_ASSERT_EQUAL_INT(R(SetResult::OutOfRange), R(setText(c, "master", "3")));
}

void test_set_result_text() {
  const SetResult all[] = {SetResult::Ok, SetResult::UnknownKey, SetResult::OutOfRange, SetResult::Invalid};
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_NOT_NULL(sbx::setResultText(all[i]));
    TEST_ASSERT_GREATER_THAN(1, std::strlen(sbx::setResultText(all[i])));
    for (int j = i + 1; j < 4; ++j) {
      TEST_ASSERT_TRUE(std::strcmp(sbx::setResultText(all[i]), sbx::setResultText(all[j])) != 0);
    }
  }
  TEST_ASSERT_NOT_NULL(sbx::setResultText(static_cast<SetResult>(99)));
}

// --- ristiintarkistukset ---------------------------------------------------

static void expectInvalid(const Config& c, const char* must_contain) {
  char err[200];
  TEST_ASSERT_FALSE_MESSAGE(sbx::configValidate(c, err, sizeof(err)), must_contain);
  TEST_ASSERT_TRUE_MESSAGE(std::strstr(err, must_contain) != nullptr, err);
}

static void expectValid(const Config& c) {
  char err[200];
  TEST_ASSERT_TRUE_MESSAGE(sbx::configValidate(c, err, sizeof(err)), err);
}

void test_validate_cross_checks() {
  {  // D2: vihreä < keltainen (tasan sama ei kelpaa: tila 4a katoaisi)
    Config c;
    c.status.tol_green_kpa = 3.0f;
    expectInvalid(c, "tol_green_kpa");
    c.status.tol_green_kpa = 2.99f;
    expectValid(c);
  }
  {
    Config c;
    c.dsp.rpm_min = 1500.0f;
    c.dsp.rpm_max = 1200.0f;
    expectInvalid(c, "rpm_max");
  }
  {
    Config c;
    c.status.sync_rpm_min = 2500.0f;
    c.status.sync_rpm_max = 2000.0f;
    expectInvalid(c, "sync_rpm_max");
  }
  {  // D12-ikkuna syklintunnistuksen sisällä, molemmista päistä
    Config c;
    c.status.sync_rpm_max = 7000.0f;  // > rpm_max 6000
    expectInvalid(c, "rpm_min…rpm_max");
    c.status.sync_rpm_max = 6000.0f;  // raja kelpaa
    expectValid(c);
    c.status.sync_rpm_min = 250.0f;  // < rpm_min 300
    expectInvalid(c, "sync_rpm_min");
  }
  {  // rpm_min 300 -> pisin jakso 0,4 s; aikakatkaisun pitää olla pidempi
    Config c;
    c.dsp.pulse_timeout_s = 0.4f;
    expectInvalid(c, "0,4 s");
    c.dsp.pulse_timeout_s = 0.41f;
    expectValid(c);
    c.dsp.rpm_min = 100.0f;  // 1,2 s jakso > 0,41 s
    c.status.sync_rpm_min = 500.0f;
    expectInvalid(c, "pulse_timeout_s");
  }
  {  // rivi 7 / 10: fault_lo < sat < fault_hi
    Config c;
    c.sensor.sat_mv = 3200;  // > fault_hi 3150
    expectInvalid(c, "sat_mv");
    c.sensor.sat_mv = 3150;  // = fault_hi
    expectInvalid(c, "3150");
    c.sensor.sat_mv = 2680;
    expectValid(c);
    // Suora kirjoitus ohi configSetin (vanha NVS-arvo): 0 / 3300 mV kytkisi
    // rivin 7 hiljaa pois, joten validointi hylkää sen.
    c.sensor.fault_lo_mv = 0;
    expectInvalid(c, "fault_lo_mv");
    c.sensor.fault_lo_mv = 50;
    expectValid(c);
    c.sensor.fault_hi_mv = 3300;
    expectInvalid(c, "fault_hi_mv");
    c.sensor.fault_hi_mv = 3250;
    expectValid(c);
  }
  {  // ilmanpaine ei saa olla anturivika (rivi 1 vs. rivi 7)
    Config c;
    c.sensor.v_zero_mv = 3200.0f;
    c.sensor.sat_mv = 3100;
    expectInvalid(c, "v_zero_mv");
  }
  {  // D10: hose_off ≤ off
    Config c;
    c.status.hose_off_kpa = 5.5f;
    expectInvalid(c, "hose_off_kpa");
    c.status.hose_off_kpa = 5.0f;
    expectValid(c);
  }
  {  // suoraan rakenteeseen kirjoitettu arvo (ohi configSetin)
    Config c;
    c.status.master = 3;
    expectInvalid(c, "master");
  }
  {
    Config c;
    c.sensor.mv_per_kpa = 0.0f;  // nollalla jako nominalKpa:ssa
    expectInvalid(c, "mv_per_kpa");
  }
  {
    Config c;
    c.status.tol_yellow_kpa = std::numeric_limits<float>::quiet_NaN();
    expectInvalid(c, "NaN");
  }
  {
    Config c;
    c.source = static_cast<sbx::SourceKind>(7);
    expectInvalid(c, "source");
  }
}

void test_validate_error_buffer_edges() {
  Config c;
  c.status.tol_green_kpa = 3.0f;
  char small[10];
  std::memset(small, 'x', sizeof(small));
  TEST_ASSERT_FALSE(sbx::configValidate(c, small, sizeof(small)));
  TEST_ASSERT_LESS_THAN(sizeof(small), std::strlen(small));  // katkaistu, NUL-päätetty
  TEST_ASSERT_FALSE(sbx::configValidate(c, nullptr, 0));
  TEST_ASSERT_TRUE(sbx::configValidate(Config(), nullptr, 0));
  // Range-virhe kertoo arvon ja välin suomalaisittain.
  c = Config();
  c.dsp.hyst_frac = 0.95f;
  char err[200];
  TEST_ASSERT_FALSE(sbx::configValidate(c, err, sizeof(err)));
  TEST_ASSERT_TRUE_MESSAGE(std::strstr(err, "hyst_frac") != nullptr, err);
  TEST_ASSERT_TRUE_MESSAGE(std::strstr(err, "0,95") != nullptr, err);
  TEST_ASSERT_TRUE_MESSAGE(std::strstr(err, "0,05…0,9") != nullptr, err);
}

// --- JSON --------------------------------------------------------------------

static int countOccurrences(const char* hay, const char* needle) {
  int n = 0;
  for (const char* p = std::strstr(hay, needle); p != nullptr; p = std::strstr(p + 1, needle)) ++n;
  return n;
}

// Kentän "value"-teksti JSONista (seuraava ',' tai '}' asti).
static bool jsonValueOf(const char* json, const char* key, char* out, size_t n) {
  char pat[48];
  std::snprintf(pat, sizeof(pat), "{\"key\":\"%s\",", key);
  const char* p = std::strstr(json, pat);
  if (p == nullptr) return false;
  p = std::strstr(p, "\"value\":");
  if (p == nullptr) return false;
  p += 8;
  size_t k = 0;
  while (p[k] != ',' && p[k] != '}' && p[k] != '\0' && k + 1 < n) {
    out[k] = p[k];
    ++k;
  }
  out[k] = '\0';
  return k > 0;
}

static char g_json[sbx::kConfigJsonSize + 64];

void test_json_shape() {
  Config c;
  c.status.tol_green_kpa = 2.25f;
  c.status.auto_zero = false;
  c.source = sbx::SourceKind::Adc;
  const size_t len = sbx::configToJson(c, g_json, sizeof(g_json));
  TEST_ASSERT_GREATER_THAN(0, len);
  TEST_ASSERT_EQUAL_size_t(std::strlen(g_json), len);
  TEST_ASSERT_TRUE(jsoncheck::valid(g_json));
  TEST_ASSERT_EQUAL_INT(0, std::strncmp(g_json, "{\"v\":1,\"fields\":[{", 18));
  TEST_ASSERT_EQUAL_INT(0, std::strcmp(g_json + len - 3, "}]}"));
  TEST_ASSERT_EQUAL_INT(sbx::configFieldCount(), countOccurrences(g_json, "{\"key\":\""));
  const char* attrs[] = {"\"label\":\"", "\"unit\":\"", "\"group\":\"", "\"type\":\"",
                         "\"min\":", "\"max\":", "\"def\":", "\"value\":"};
  for (const char* a : attrs) TEST_ASSERT_EQUAL_INT_MESSAGE(sbx::configFieldCount(), countOccurrences(g_json, a), a);
  // Sulkeet tasapainossa merkkijonojen ulkopuolella (validaattorin lisäksi suoraan).
  int depth = 0, min_depth = 0;
  bool in_str = false;
  for (const char* p = g_json; *p != '\0'; ++p) {
    if (in_str) {
      if (*p == '\\') ++p;
      else if (*p == '"') in_str = false;
      continue;
    }
    if (*p == '"') in_str = true;
    if (*p == '{' || *p == '[') ++depth;
    if (*p == '}' || *p == ']') --depth;
    if (depth < min_depth) min_depth = depth;
  }
  TEST_ASSERT_EQUAL_INT(0, depth);
  TEST_ASSERT_EQUAL_INT(0, min_depth);

  for (int i = 0; i < sbx::configFieldCount(); ++i) {
    char pat[48];
    std::snprintf(pat, sizeof(pat), "{\"key\":\"%s\",", sbx::configField(i).key);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countOccurrences(g_json, pat), sbx::configField(i).key);
  }
  TEST_ASSERT_NOT_NULL(std::strstr(g_json, "\"key\":\"tol_green_kpa\",\"label\":\"Synkronoitu, kun ero enintään\","
                                           "\"unit\":\"kPa\",\"group\":\"Synkronointi\",\"type\":\"float\","
                                           "\"min\":0.1,\"max\":10,\"def\":1.5,\"value\":2.25}"));
  TEST_ASSERT_NOT_NULL(std::strstr(g_json, "\"key\":\"auto_zero\",\"label\":"));
  TEST_ASSERT_NOT_NULL(std::strstr(g_json, "\"type\":\"bool\",\"min\":0,\"max\":1,\"def\":true,\"value\":false}"));
  TEST_ASSERT_NOT_NULL(std::strstr(g_json, "\"key\":\"source\",\"label\":"));
  TEST_ASSERT_NOT_NULL(std::strstr(g_json, "\"type\":\"u8\",\"min\":0,\"max\":2,\"def\":0,\"value\":1}"));
  TEST_ASSERT_NOT_NULL(std::strstr(g_json, "\"group\":\"Järjestelmä\""));
  TEST_ASSERT_NOT_NULL(std::strstr(g_json, "\"type\":\"u16\""));
}

// Pahin tapaus: jokainen liukuluku 6 merkitsevällä numerolla, kokonaisluvut
// maksimissaan. kConfigJsonSize:n on mahdutettava tämä reilulla varalla.
void test_json_worst_case_fits_buffer_constant() {
  Config c;
  for (int i = 0; i < sbx::configFieldCount(); ++i) {
    const FieldInfo& f = sbx::configField(i);
    float v = f.max;
    if (f.type == FieldType::Float) v = f.min + (f.max - f.min) * 0.987654f;
    TEST_ASSERT_EQUAL_INT_MESSAGE(R(SetResult::Ok), R(sbx::configSet(c, i, v)), f.key);
  }
  const size_t len = sbx::configToJson(c, g_json, sizeof(g_json));
  char msg[64];
  std::snprintf(msg, sizeof(msg), "pahin JSON %zu / %zu tavua", len, sbx::kConfigJsonSize);
  TEST_MESSAGE(msg);
  TEST_ASSERT_GREATER_THAN(0, len);
  TEST_ASSERT_TRUE(jsoncheck::valid(g_json));
  // Vähintään 512 tavua varaa uusille kentille (~150 tavua/kenttä).
  TEST_ASSERT_LESS_OR_EQUAL(sbx::kConfigJsonSize - 512, len);
  TEST_ASSERT_EQUAL_size_t(len, sbx::configToJson(c, g_json, sbx::kConfigJsonSize));
  // Ei mahdu (NUL tarvitsee tavun) -> 0; täsmälleen mahtuu -> pituus.
  TEST_ASSERT_EQUAL_size_t(0, sbx::configToJson(c, g_json, len));
  TEST_ASSERT_EQUAL_size_t(len, sbx::configToJson(c, g_json, len + 1));
  TEST_ASSERT_EQUAL_size_t(0, sbx::configToJson(c, g_json, 10));
  TEST_ASSERT_EQUAL_size_t(0, sbx::configToJson(c, nullptr, 0));
}

// UI näyttää JSONin arvon ja lähettää sen muuttamattomana takaisin: arvo ei
// saa ajautua, eikä 6 merkitsevän numeron pyöristys saa pudottaa sitä alueelta.
void test_json_value_round_trips_through_set_text() {
  Config src;
  for (int i = 0; i < sbx::configFieldCount(); ++i) {
    const FieldInfo& f = sbx::configField(i);
    float v = f.min + (f.max - f.min) * 0.3141593f;
    if (f.type != FieldType::Float) v = std::floor(v);
    TEST_ASSERT_EQUAL_INT(R(SetResult::Ok), R(sbx::configSet(src, i, v)));
  }
  // Myös rajat: max on oltava takaisin syötettävissä.
  sbx::configSet(src, sbx::configFindField("unstable_cv"), 1.0f);
  sbx::configSet(src, sbx::configFindField("hyst_frac"), 0.05f);
  TEST_ASSERT_GREATER_THAN(0, sbx::configToJson(src, g_json, sizeof(g_json)));
  for (int i = 0; i < sbx::configFieldCount(); ++i) {
    const FieldInfo& f = sbx::configField(i);
    char text[32];
    TEST_ASSERT_TRUE_MESSAGE(jsonValueOf(g_json, f.key, text, sizeof(text)), f.key);
    Config dst;
    TEST_ASSERT_EQUAL_INT_MESSAGE(R(SetResult::Ok),
                                  R(sbx::configSetText(dst, f.key, text)), text);
    const float a = sbx::configGet(src, i);
    const float b = sbx::configGet(dst, i);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5f * (std::fabs(a) > 1.0f ? std::fabs(a) : 1.0f), a, b, f.key);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_registry_covers_every_struct_member);
  RUN_TEST(test_every_key_maps_to_its_own_member);
  RUN_TEST(test_keys_labels_groups_ranges_wellformed);
  RUN_TEST(test_ranges_match_physics_and_dsp_capacity);
  RUN_TEST(test_wp9_proposed_fields_registered);
  RUN_TEST(test_defaults_in_range_and_valid);
  RUN_TEST(test_get_set_round_trip_every_field);
  RUN_TEST(test_set_rejects_and_leaves_value_untouched);
  RUN_TEST(test_set_text_accepts_finnish_comma_and_point);
  RUN_TEST(test_set_text_independent_of_locale);
  RUN_TEST(test_set_text_rejects_garbage);
  RUN_TEST(test_set_text_bool);
  RUN_TEST(test_set_text_integer_and_enum_fields);
  RUN_TEST(test_set_result_text);
  RUN_TEST(test_validate_cross_checks);
  RUN_TEST(test_validate_error_buffer_edges);
  RUN_TEST(test_json_shape);
  RUN_TEST(test_json_worst_case_fits_buffer_constant);
  RUN_TEST(test_json_value_round_trips_through_set_text);
  return UNITY_END();
}
