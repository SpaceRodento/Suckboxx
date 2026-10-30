// WP-6: sim_api.h -testit (docs/firmware.md §4.8 ja §5:n sim-avaimet).
//
// Viite on tools/mock_server.py:n SIM_KEYS ja sim_apply: UI:n simulaattorisivu
// (web/index.html) lähettää minkä tahansa osajoukon avaimista ja lukee GET
// /api/sim:n lukuina. Liima yhdistää POSTin sim.params():iin, joten
// muunnoksen on muutettava vain annetut avaimet.

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/engine_sim.h"
#include "core/sim_api.h"

#if __has_include("json_validate.h")
#include "json_validate.h"  // test/support/ (WP-34b siirtää sen sinne)
#else
#include "../test_json_writer/json_validate.h"
#endif

using sbx::EngineSim;
using sbx::FormPair;
using sbx::SensorParams;
using sbx::SimFault;
using sbx::SimParams;
using sbx::SimSetResult;
using sbx::kChannels;
using sbx::kSimKeyCount;

namespace {

char g_err[sbx::kFormErrLen];
char g_json[sbx::kSimJsonSize];

int F(SimFault f) { return static_cast<int>(f); }
int R(SimSetResult r) { return static_cast<int>(r); }

// Pitkä JSON verrataan strcmp:llä ja näytetään kokonaan eron sattuessa
// (shimin EQUAL_STRING laukaisisi 512 tavun puskurilla -Wformat-truncationin).
void assertJson(const char* expect, const char* got) {
  if (std::strcmp(expect, got) != 0) {
    TEST_MESSAGE(got);
    TEST_FAIL_MESSAGE("GET /api/sim -JSON eroaa odotetusta");
  }
}

bool apply(SimParams& p, const FormPair* pairs, int n, bool& rpm_set) {
  return sbx::simApplyForm(p, pairs, n, rpm_set, g_err, sizeof(g_err));
}

// Kaikki SimParamsin kentät samoja (myös ne, joilla ei ole /api/sim-avainta).
bool sameParams(const SimParams& a, const SimParams& b) {
  bool same = a.engine_on == b.engine_on && a.rpm == b.rpm && a.rpm_jitter == b.rpm_jitter &&
              a.rpm_drift == b.rpm_drift && a.base_kpa == b.base_kpa && a.depth_kpa == b.depth_kpa &&
              a.intake_frac == b.intake_frac && a.res_kpa == b.res_kpa &&
              a.res_decay_s == b.res_decay_s && a.xtalk == b.xtalk && a.peak_kpa == b.peak_kpa &&
              a.noise_mv == b.noise_mv && a.seed == b.seed;
  for (int i = 0; i < kChannels; ++i) {
    same = same && a.sync_err_kpa[i] == b.sync_err_kpa[i] && a.res_hz[i] == b.res_hz[i] &&
           a.sensor_offset_kpa[i] == b.sensor_offset_kpa[i] &&
           a.sensor_gain_err[i] == b.sensor_gain_err[i] && a.fault[i] == b.fault[i];
  }
  return same;
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Avaimet ja GET /api/sim
// ---------------------------------------------------------------------------

void test_keys_in_section5_order() {
  // §5: engine_on, rpm, jitter, drift, base, err1..3, depth, res, xtalk, peak,
  // noise, fault1..3 (sama järjestys kuin mock_server.py SIM_KEYS).
  const char* expect[] = {"engine_on", "rpm", "jitter", "drift", "base", "err1", "err2", "err3",
                          "depth", "res", "xtalk", "peak", "noise", "fault1", "fault2", "fault3"};
  TEST_ASSERT_EQUAL_INT(16, kSimKeyCount);
  for (int i = 0; i < kSimKeyCount; ++i) {
    TEST_ASSERT_EQUAL_STRING(expect[i], sbx::simKey(i).key);
    TEST_ASSERT_EQUAL_INT(i, sbx::simFindKey(expect[i]));
  }
  TEST_ASSERT_EQUAL_INT(-1, sbx::simFindKey("seed"));    // ei UI-avain
  TEST_ASSERT_EQUAL_INT(-1, sbx::simFindKey(nullptr));
  TEST_ASSERT_EQUAL_STRING("", sbx::simKey(kSimKeyCount).key);
  TEST_ASSERT_TRUE(std::isnan(sbx::simGet(SimParams(), -1)));
}

void test_get_json_defaults_match_mock() {
  // mock default_sim(): engine_on ja vikakoodit kokonaislukuina (UI:n isNum).
  const size_t n = sbx::simToJson(SimParams(), g_json, sizeof(g_json));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(n));
  TEST_ASSERT_TRUE(jsoncheck::valid(g_json));
  assertJson(
      "{\"engine_on\":1,\"rpm\":850,\"jitter\":0.01,\"drift\":0,\"base\":-55,\"err1\":0,"
      "\"err2\":0,\"err3\":0,\"depth\":20,\"res\":3,\"xtalk\":0,\"peak\":0,\"noise\":2,"
      "\"fault1\":0,\"fault2\":0,\"fault3\":0}",
      g_json);
}

void test_get_json_maps_every_field() {
  SimParams p;
  p.engine_on = false;
  p.rpm = 912.346f;
  p.rpm_jitter = 0.025f;
  p.rpm_drift = -60.0f;
  p.base_kpa = -48.5f;
  p.sync_err_kpa[0] = 0.5f;
  p.sync_err_kpa[1] = -2.5f;
  p.sync_err_kpa[2] = 4.0f;
  p.depth_kpa = 18.0f;
  p.res_kpa = 1.5f;
  p.xtalk = 0.3f;
  p.peak_kpa = 12.0f;
  p.noise_mv = 3.5f;
  p.fault[0] = SimFault::HoseOff;
  p.fault[1] = SimFault::SensorLow;
  p.fault[2] = SimFault::SensorHigh;
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(sbx::simToJson(p, g_json, sizeof(g_json))));
  assertJson(
      "{\"engine_on\":0,\"rpm\":912.346,\"jitter\":0.025,\"drift\":-60,\"base\":-48.5,"
      "\"err1\":0.5,\"err2\":-2.5,\"err3\":4,\"depth\":18,\"res\":1.5,\"xtalk\":0.3,"
      "\"peak\":12,\"noise\":3.5,\"fault1\":1,\"fault2\":2,\"fault3\":3}",
      g_json);
}

void test_get_json_buffer() {
  // Pahin tapaus: jokainen liukuluku 6 merkitsevällä numerolla ja negatiivisena.
  // Kentät suoraan (simSet rajaisi): ohjelmallinen kutsuja voi asettaa mitä vain.
  SimParams p;
  p.rpm = p.rpm_jitter = p.rpm_drift = p.base_kpa = p.depth_kpa = p.res_kpa = p.xtalk =
      p.peak_kpa = p.noise_mv = -12345.6f;
  for (int i = 0; i < kChannels; ++i) p.sync_err_kpa[i] = -12345.6f;
  const size_t n = sbx::simToJson(p, g_json, sizeof(g_json));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(n));
  TEST_ASSERT_TRUE(jsoncheck::valid(g_json));
  char msg[64];
  std::snprintf(msg, sizeof(msg), "GET /api/sim pahin tapaus %u tavua", static_cast<unsigned>(n));
  TEST_MESSAGE(msg);
  TEST_ASSERT_LESS_THAN(static_cast<long long>(sbx::kSimJsonSize) - 128, static_cast<long long>(n));
  TEST_ASSERT_EQUAL_size_t(0, sbx::simToJson(p, g_json, n));  // ei katkaistua JSONia
}

// ---------------------------------------------------------------------------
// POST /api/sim: vain annetut avaimet, kaikki tai ei mitään (mock sim_apply)
// ---------------------------------------------------------------------------

void test_post_changes_only_given_keys() {
  SimParams p;
  p.sensor_offset_kpa[1] = -1.5f;  // ei /api/sim-avainta: ei saa muuttua
  p.intake_frac = 0.3f;
  p.seed = 42;
  SimParams expect = p;
  expect.sync_err_kpa[1] = 2.5f;
  expect.fault[2] = SimFault::HoseOff;
  expect.base_kpa = -54.5f;
  const FormPair form[] = {{"err2", "2.5"}, {"fault3", "1"}, {"base", "-54,5"}};
  bool rpm_set = true;
  TEST_ASSERT_TRUE(apply(p, form, 3, rpm_set));
  TEST_ASSERT_FALSE(rpm_set);
  TEST_ASSERT_EQUAL_STRING("", g_err);
  TEST_ASSERT_TRUE(sameParams(expect, p));
}

void test_post_maps_every_key_to_its_field() {
  struct Case {
    const char* key;
    const char* text;
    float want;
  };
  const Case cases[] = {
      {"engine_on", "0", 0.0f}, {"rpm", "1200", 1200.0f}, {"jitter", "0.05", 0.05f},
      {"drift", "-75", -75.0f}, {"base", "-40", -40.0f},  {"err1", "1,25", 1.25f},
      {"err2", "-3", -3.0f},    {"err3", "7", 7.0f},      {"depth", "15", 15.0f},
      {"res", "4", 4.0f},       {"xtalk", "0.2", 0.2f},   {"peak", "9", 9.0f},
      {"noise", "5", 5.0f},     {"fault1", "1", 1.0f},    {"fault2", "2", 2.0f},
      {"fault3", "3", 3.0f}};
  for (const Case& c : cases) {
    SimParams p;
    TEST_ASSERT_EQUAL_INT(R(SimSetResult::Ok), R(sbx::simSetText(p, c.key, c.text)));
    const int i = sbx::simFindKey(c.key);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, c.want, sbx::simGet(p, i));
    // Muut avaimet ennallaan.
    for (int k = 0; k < kSimKeyCount; ++k) {
      if (k == i) continue;
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, sbx::simGet(SimParams(), k), sbx::simGet(p, k));
    }
  }
  // Suorat kentät (ei simGetin kautta): err1..3 -> sync_err_kpa, fault1..3 -> fault.
  SimParams p;
  sbx::simSetText(p, "err3", "-4");
  sbx::simSetText(p, "fault2", "2");
  sbx::simSetText(p, "engine_on", "0");
  TEST_ASSERT_EQUAL_FLOAT(-4.0f, p.sync_err_kpa[2]);
  TEST_ASSERT_EQUAL_INT(F(SimFault::SensorLow), F(p.fault[1]));
  TEST_ASSERT_FALSE(p.engine_on);
}

void test_post_is_atomic_with_mock_error_texts() {
  SimParams p;
  const SimParams before = p;
  bool rpm_set = false;
  const FormPair unknown[] = {{"rpm", "1500"}, {"foo", "1"}};
  TEST_ASSERT_FALSE(apply(p, unknown, 2, rpm_set));
  TEST_ASSERT_EQUAL_STRING("Tuntematon simulaattoriparametri: foo", g_err);
  TEST_ASSERT_TRUE(sameParams(before, p));
  TEST_ASSERT_FALSE(rpm_set);
  const FormPair bad[] = {{"base", "-50"}, {"rpm", "abc"}};
  TEST_ASSERT_FALSE(apply(p, bad, 2, rpm_set));
  TEST_ASSERT_EQUAL_STRING("Virheellinen arvo: rpm=abc", g_err);
  TEST_ASSERT_TRUE(sameParams(before, p));
  // Kokonaislukuavain ei pyöristä (mock: floor(v) != v).
  const FormPair frac[] = {{"fault2", "1.5"}};
  TEST_ASSERT_FALSE(apply(p, frac, 1, rpm_set));
  TEST_ASSERT_EQUAL_STRING("Virheellinen arvo: fault2=1.5", g_err);
  const FormPair on_frac[] = {{"engine_on", "0,5"}};
  TEST_ASSERT_FALSE(apply(p, on_frac, 1, rpm_set));
  TEST_ASSERT_EQUAL_STRING("Virheellinen arvo: engine_on=0,5", g_err);
  const FormPair whole[] = {{"engine_on", "1.0"}, {"fault1", " 2 "}};
  TEST_ASSERT_TRUE(apply(p, whole, 2, rpm_set));
  TEST_ASSERT_EQUAL_INT(F(SimFault::SensorLow), F(p.fault[0]));
  // Tyhjä lomake on kelvollinen eikä muuta mitään (mock: sim_apply([])).
  SimParams q;
  TEST_ASSERT_TRUE(apply(q, nullptr, 0, rpm_set));
  TEST_ASSERT_FALSE(rpm_set);
  TEST_ASSERT_TRUE(sameParams(SimParams(), q));
}

void test_post_rejects_non_numbers() {
  // Kuten configSetText: ei nan/inf/heksaa, ei roskaa perässä, ei tyhjää.
  const char* bad[] = {"nan", "inf", "-inf", "0x10", "1,5,0", "12abc", "", "   ", "+", "e5",
                       "1 2", "123456789012345678901234567890123"};
  for (const char* t : bad) {
    SimParams p;
    TEST_ASSERT_EQUAL_INT_MESSAGE(R(SimSetResult::Invalid), R(sbx::simSetText(p, "depth", t)), t);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, p.depth_kpa);
  }
  SimParams p;
  TEST_ASSERT_EQUAL_INT(R(SimSetResult::Invalid), R(sbx::simSetText(p, "depth", nullptr)));
  TEST_ASSERT_EQUAL_INT(R(SimSetResult::UnknownKey), R(sbx::simSetText(p, nullptr, "1")));
  TEST_ASSERT_EQUAL_INT(R(SimSetResult::Ok), R(sbx::simSetText(p, "depth", "1.5e1")));
  TEST_ASSERT_EQUAL_FLOAT(15.0f, p.depth_kpa);
}

void test_post_clamps_to_engine_sim_limits() {
  // mock: v = min(max(v, mn), mx) - EngineSim rajaa itse, ei hylkää.
  struct Case {
    const char* key;
    const char* text;
    float want;
  };
  const Case cases[] = {
      {"rpm", "20000", EngineSim::kRpmMax}, {"rpm", "10", EngineSim::kRpmMin},
      {"jitter", "0.9", 0.4f},              {"base", "10", 0.0f},
      {"base", "-150", -100.0f},            {"err1", "45", 30.0f},
      {"depth", "-5", 0.0f},                {"noise", "5000", 1000.0f},
      {"engine_on", "5", 1.0f},             {"fault1", "7", 3.0f},
      {"fault3", "-2", 0.0f},               {"rpm", "1e99", EngineSim::kRpmMax},
      {"drift", "-1e99", -2000.0f}};
  for (const Case& c : cases) {
    SimParams p;
    TEST_ASSERT_EQUAL_INT_MESSAGE(R(SimSetResult::Ok), R(sbx::simSetText(p, c.key, c.text)), c.text);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-6f, c.want, sbx::simGet(p, sbx::simFindKey(c.key)), c.key);
  }
}

void test_rpm_set_only_when_rpm_posted() {
  SimParams p;
  bool rpm_set = false;
  const FormPair with_rpm[] = {{"drift", "0"}, {"rpm", "850"}};
  TEST_ASSERT_TRUE(apply(p, with_rpm, 2, rpm_set));
  TEST_ASSERT_TRUE(rpm_set);
  const FormPair without[] = {{"drift", "0"}};
  TEST_ASSERT_TRUE(apply(p, without, 1, rpm_set));
  TEST_ASSERT_FALSE(rpm_set);
}

void test_glue_merge_keeps_drifted_rpm() {
  // jatko.md / engine_sim.h: liima yhdistää POSTin sim.params():iin ja antaa
  // rpm_set = (rpm-avain mukana). Pelkkä "drift=0" pysäyttää nykyiseen
  // kierroslukuun; "drift=0&rpm=850" palaa 850:een.
  EngineSim sim;
  const SensorParams s;
  sim.configure(SimParams(), s);
  auto post = [&](const FormPair* form, int n) {
    SimParams p = sim.params();
    bool rpm_set = false;
    TEST_ASSERT_TRUE(sbx::simApplyForm(p, form, n, rpm_set, g_err, sizeof(g_err)));
    sim.configure(p, s, rpm_set);
  };
  const FormPair drift[] = {{"drift", "100"}};
  post(drift, 1);
  for (int i = 0; i < 2000; ++i) sim.next();  // 2 s: 850 -> 1050 rpm
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 1050.0f, sim.trueRpm());
  const FormPair stop[] = {{"drift", "0"}};
  post(stop, 1);
  for (int i = 0; i < 1000; ++i) sim.next();
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 1050.0f, sim.trueRpm());
  // GET /api/sim näyttää kierrosluvun, jossa simulaattori oikeasti on.
  sbx::simToJson(sim.params(), g_json, sizeof(g_json));
  TEST_ASSERT_TRUE_MESSAGE(std::strstr(g_json, "\"rpm\":1050") != nullptr, g_json);
  const FormPair back[] = {{"drift", "0"}, {"rpm", "850"}};
  post(back, 2);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 850.0f, sim.trueRpm());
  // Toinen avain ei koske kierroslukuun.
  const FormPair other[] = {{"err2", "1.5"}};
  post(other, 1);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 850.0f, sim.trueRpm());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, sim.params().sync_err_kpa[1]);
}

void test_error_text_truncates_on_utf8_boundary() {
  SimParams p;
  bool rpm_set = false;
  char key[300];
  std::memset(key, 0, sizeof(key));
  for (int i = 0; i + 2 < 290; i += 2) {  // "ää..." (2 tavua per merkki)
    key[i] = static_cast<char>(0xC3);
    key[i + 1] = static_cast<char>(0xA4);
  }
  const FormPair form[] = {{key, "1"}};
  char err[41];
  TEST_ASSERT_FALSE(sbx::simApplyForm(p, form, 1, rpm_set, err, sizeof(err)));
  const size_t len = std::strlen(err);
  TEST_ASSERT_LESS_THAN(41, static_cast<long long>(len));
  // "Tuntematon simulaattoriparametri: " on 34 tavua, loput kokonaisia ä-merkkejä.
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>((len - 34) % 2));
  TEST_ASSERT_EQUAL_INT(0xA4, static_cast<unsigned char>(err[len - 1]));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_keys_in_section5_order);
  RUN_TEST(test_get_json_defaults_match_mock);
  RUN_TEST(test_get_json_maps_every_field);
  RUN_TEST(test_get_json_buffer);
  RUN_TEST(test_post_changes_only_given_keys);
  RUN_TEST(test_post_maps_every_key_to_its_field);
  RUN_TEST(test_post_is_atomic_with_mock_error_texts);
  RUN_TEST(test_post_rejects_non_numbers);
  RUN_TEST(test_post_clamps_to_engine_sim_limits);
  RUN_TEST(test_rpm_set_only_when_rpm_posted);
  RUN_TEST(test_glue_merge_keeps_drifted_rpm);
  RUN_TEST(test_error_text_truncates_on_utf8_boundary);
  return UNITY_END();
}
