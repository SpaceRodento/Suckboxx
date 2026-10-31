// WP-6: snapshot.h -testit (docs/firmware.md §5, SSE-tapahtuma "snap").
//
// Viite on tools/mock_server.py:n Device.snapshot(): web/index.html on
// kehitetty sitä vasten, joten laitteen viestin on oltava rakenteeltaan sama.
// Rakenne tarkistetaan muotomerkkijonona: avaimet järjestyksessä ja arvon
// tyyppi (n = luku, s = merkkijono, b = totuusarvo, z = null), esim.
// {v:n,t:n,...}. Odotetut muodot on poimittu ajamalla mock samoissa tiloissa.

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/engine_sim.h"
#include "core/processor.h"
#include "core/snapshot.h"

#if __has_include("json_validate.h")
#include "json_validate.h"  // test/support/ (WP-34b siirtää sen sinne)
#else
#include "../test_json_writer/json_validate.h"
#endif

using sbx::CalStatus;
using sbx::Calibration;
using sbx::ChannelState;
using sbx::Config;
using sbx::EngineSim;
using sbx::Processor;
using sbx::RawFrame;
using sbx::RawSample;
using sbx::SimParams;
using sbx::SnapshotMeta;
using sbx::SnapshotView;
using sbx::StatusResult;
using sbx::kChannels;
using sbx::kSnapshotJsonSize;

namespace {

constexpr size_t kRingCap = 2048;
RawSample g_mem[kRingCap];
char g_buf[kSnapshotJsonSize];

// --- muotomerkkijono -------------------------------------------------------

struct Shape {
  char out[1024];
  size_t len = 0;
  bool ok = true;
  void put(char c) {
    if (len + 1 < sizeof(out)) {
      out[len++] = c;
      out[len] = '\0';
    } else {
      ok = false;
    }
  }
  void puts(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) put(s[i]);
  }
};

void skipWs(const char*& p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
}

// Merkkijonon loppuun (escapet huomioiden); palauttaa sisällön alun ja pituuden.
bool scanString(const char*& p, const char** start, size_t* n) {
  if (*p != '"') return false;
  ++p;
  const char* s = p;
  while (*p != '"') {
    if (*p == '\0') return false;
    if (*p == '\\') ++p;
    ++p;
  }
  if (start != nullptr) *start = s;
  if (n != nullptr) *n = static_cast<size_t>(p - s);
  ++p;
  return true;
}

bool shapeOf(const char*& p, Shape& sh) {
  skipWs(p);
  if (*p == '{') {
    ++p;
    sh.put('{');
    skipWs(p);
    bool first = true;
    while (*p != '}') {
      if (!first) {
        if (*p != ',') return false;
        ++p;
        sh.put(',');
      }
      first = false;
      skipWs(p);
      const char* k = nullptr;
      size_t kn = 0;
      if (!scanString(p, &k, &kn)) return false;
      sh.puts(k, kn);
      skipWs(p);
      if (*p != ':') return false;
      ++p;
      sh.put(':');
      if (!shapeOf(p, sh)) return false;
      skipWs(p);
    }
    ++p;
    sh.put('}');
    return true;
  }
  if (*p == '[') {
    ++p;
    sh.put('[');
    skipWs(p);
    bool first = true;
    while (*p != ']') {
      if (!first) {
        if (*p != ',') return false;
        ++p;
        sh.put(',');
      }
      first = false;
      if (!shapeOf(p, sh)) return false;
      skipWs(p);
    }
    ++p;
    sh.put(']');
    return true;
  }
  if (*p == '"') {
    sh.put('s');
    return scanString(p, nullptr, nullptr);
  }
  if (std::strncmp(p, "true", 4) == 0 || std::strncmp(p, "false", 5) == 0) {
    p += (*p == 't') ? 4 : 5;
    sh.put('b');
    return true;
  }
  if (std::strncmp(p, "null", 4) == 0) {
    p += 4;
    sh.put('z');
    return true;
  }
  const char* q = p;
  while ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E') ++p;
  if (p == q) return false;
  sh.put('n');
  return true;
}

// Validoi (RFC 8259, json_validate.h) ja palauttaa muodon.
const char* shapeOfJson(const char* json, Shape& sh) {
  TEST_ASSERT_TRUE_MESSAGE(jsoncheck::valid(json), json);
  const char* p = json;
  TEST_ASSERT_TRUE(shapeOf(p, sh));
  TEST_ASSERT_TRUE(sh.ok);
  return sh.out;
}

// Mockin muoto (python3 tools/mock_server.py, Device.snapshot()):
// ch-kanavan avaimet ja kolme kanavaa. act ja cond: WP-10 (firmware.md §5).
#define CH_RUN "{d:n,mean:n,level:n,min:n,p2p:n,rpm:n,mv:n,pulse:b,fault:b,sat:b,cond:s}"
#define CH_OFF "{d:n,mean:z,level:n,min:z,p2p:z,rpm:z,mv:n,pulse:b,fault:b,sat:b,cond:s}"
#define HEAD "{v:n,t:n,src:s,state:s,fault_ch:n,bars:b,verdict:b,head:s,advice:s,"
#define FLAGS "flags:{sat:b,uncal_zero:b,uncal_gain:b,drift:b},master:n,tol:[n,n],"
#define TAIL "cal:{busy:b,zero:b,gain:b,msg:s},diag:{fs:n,gaps:n,heap:n,frozen:b}}"

const char* const kShapeOff =
    HEAD "act:z," FLAGS "rpm:z,rpm_sd:z,rpm_cv:z,spread:n,worst:n,ch:[" CH_OFF "," CH_OFF ","
    CH_OFF "],wave:{fs:n,s:[]}," TAIL;
const char* const kShapeRunning =
    HEAD "act:z," FLAGS "rpm:n,rpm_sd:n,rpm_cv:n,spread:n,worst:n,ch:[" CH_RUN "," CH_RUN ","
    CH_RUN "],wave:{fs:n,s:[]}," TAIL;
// Ruuvineuvo annettu (rivit 4a/4b): act on objekti.
const char* const kShapeAdvice =
    HEAD "act:{ch:n,open:b}," FLAGS "rpm:n,rpm_sd:n,rpm_cv:n,spread:n,worst:n,ch:[" CH_RUN ","
    CH_RUN "," CH_RUN "],wave:{fs:n,s:[]}," TAIL;

// --- syöte -----------------------------------------------------------------

struct Rig {
  Processor p{g_mem, kRingCap};
  EngineSim sim;
  uint32_t n = 0;

  void begin(const SimParams& sp) { sim.configure(sp, p.config().sensor); }
  void run(float seconds) {
    const int k = static_cast<int>(seconds * 1000.0f + 0.5f);
    for (int i = 0; i < k; ++i) {
      p.push(sim.next());
      if (++n % 50 == 0) p.tick(static_cast<float>(n) / 1000.0f);
    }
  }
  void dropWave() {
    float rows[Processor::kWaveRows][kChannels];
    while (p.takeWave(rows, Processor::kWaveRows) > 0) {
    }
  }
  const char* snap(const SnapshotMeta& m = SnapshotMeta()) {
    const size_t len = sbx::snapshotJson(p, m, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(len));
    TEST_ASSERT_EQUAL_size_t(std::strlen(g_buf), len);
    return g_buf;
  }
};

bool contains(const char* hay, const char* needle) { return std::strstr(hay, needle) != nullptr; }

// Kanavan i ch[].cond-arvo tekstinä (avain on vain kanavaobjekteissa).
const char* condOf(const char* js, int i, char (&out)[16]) {
  out[0] = '\0';
  const char* p = std::strstr(js, "\"ch\":[");
  for (int k = 0; p != nullptr && k <= i; ++k) {
    p = std::strstr(p + 1, "\"cond\":\"");
  }
  if (p == nullptr) return out;
  p += 8;
  size_t n = 0;
  while (p[n] != '"' && p[n] != '\0' && n + 1 < sizeof(out)) {
    out[n] = p[n];
    ++n;
  }
  out[n] = '\0';
  return out;
}

// Snapshotin cond = statuksen ch_cond jokaisella kanavalla (snapshot ei päättele itse).
void assertCondMirrorsStatus(const char* js, const StatusResult& s) {
  char got[16];
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_EQUAL_STRING(sbx::channelCondKey(s.ch_cond[i]), condOf(js, i, got));
  }
}

// Leveimmät luvut: |v| < 99999 ja jokainen desimaali merkitsevä
// (99998.1171875 = 99998 + 15/128 on tarkka float, "%.4f" -> "99998.1172").
constexpr float kWide = -99998.1171875f;

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Rakenne = mock (§5)
// ---------------------------------------------------------------------------

void test_shape_engine_off_matches_mock() {
  // Rivit 1+8 (D10): ei pulssia -> ch[].mean/min/p2p/rpm ja rpm-luvut null.
  Rig r;
  SimParams sp;
  sp.engine_on = false;
  r.begin(sp);
  r.run(1.0f);
  r.dropWave();
  Shape sh;
  TEST_ASSERT_EQUAL_STRING(kShapeOff, shapeOfJson(r.snap(), sh));
  TEST_ASSERT_TRUE(contains(g_buf, "\"state\":\"no_signal\""));
  TEST_ASSERT_TRUE(contains(g_buf, "\"head\":\"Ei alipainesignaalia\""));
  TEST_ASSERT_TRUE(contains(g_buf, "\"advice\":\"Moottori sammuksissa tai letkut irti\""));
  // §5 (WP-10): ei ruuvineuvoa -> act null; kaikki hiljaa -> idle (D10).
  TEST_ASSERT_TRUE(contains(g_buf, "\"advice\":\"Moottori sammuksissa tai letkut irti\",\"act\":null,"));
  char c[16];
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_STRING("idle", condOf(g_buf, i, c));
  assertCondMirrorsStatus(g_buf, r.p.status());
}

void test_shape_running_matches_mock() {
  Rig r;
  r.begin(SimParams());
  r.run(5.0f);
  r.dropWave();
  Shape sh;
  TEST_ASSERT_EQUAL_STRING(kShapeRunning, shapeOfJson(r.snap(), sh));
  // Ilman nollausta tuomio on enintään Near (D11).
  TEST_ASSERT_TRUE(contains(g_buf, "\"state\":\"near\""));
  TEST_ASSERT_TRUE(contains(g_buf, "\"head\":\"Lähellä – kalibroimaton\""));
  TEST_ASSERT_TRUE(contains(g_buf, "\"bars\":true,\"verdict\":true"));
  TEST_ASSERT_TRUE(contains(g_buf, "\"flags\":{\"sat\":false,\"uncal_zero\":true,\"uncal_gain\":true,"));
  // D11: ero rajan sisällä ilman nollausta -> pelkkä nollausohje, ei ruuvineuvoa.
  TEST_ASSERT_TRUE(contains(g_buf, "\"act\":null,"));
  char c[16];
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_STRING("ok", condOf(g_buf, i, c));
  assertCondMirrorsStatus(g_buf, r.p.status());
}

void test_values_follow_status_and_meta() {
  Rig r;
  r.begin(SimParams());
  r.run(5.0f);
  SnapshotMeta m;
  m.t_ms = 123456;
  m.src = "adc";
  m.fs_hz = 999.96f;
  m.heap = 180000;
  const char* js = r.snap(m);
  TEST_ASSERT_TRUE(contains(js, "{\"v\":1,\"t\":123456,\"src\":\"adc\","));
  TEST_ASSERT_TRUE(contains(js, "\"diag\":{\"fs\":1000,\"gaps\":0,\"heap\":180000,\"frozen\":false}}"));
  TEST_ASSERT_TRUE(contains(js, "\"master\":0,\"tol\":[1.5,3],"));
  TEST_ASSERT_TRUE(contains(js, "\"wave\":{\"fs\":62.5,\"s\":[["));
  // Teksti tulee StatusResultista sellaisenaan (JSON-escape ei muuta näitä).
  char want[sbx::kAdviceLen + 16];
  std::snprintf(want, sizeof(want), "\"advice\":\"%s\"", r.p.status().advice);
  TEST_ASSERT_TRUE_MESSAGE(contains(js, want), js);
  // rpm = RpmEstimaten mediaani 1 desimaalilla (mock round(v, 1)), sama tick kuin tila.
  TEST_ASSERT_TRUE(r.p.rpm().valid);
  char num[32];
  char rpm[48];
  sbx::formatFixed(num, sizeof(num), r.p.rpm().rpm, 1, true);
  std::snprintf(rpm, sizeof(rpm), "\"rpm\":%s,\"rpm_sd\":", num);
  TEST_ASSERT_TRUE_MESSAGE(contains(js, rpm), rpm);
  // src puuttuu -> konfiguraation lähde.
  SnapshotMeta none;
  TEST_ASSERT_TRUE(contains(r.snap(none), "\"src\":\"sim\""));
}

void test_delta_sign_and_worst_follow_status() {
  // D3: d = mean_master - mean_ch, positiivinen = imee enemmän. SYL 3 -4 kPa
  // syvempi -> d = +4 (avaa), worst = 2.
  Rig r;
  SimParams sp;
  sp.sync_err_kpa[2] = -4.0f;
  r.begin(sp);
  r.run(6.0f);
  const StatusResult& s = r.p.status();
  TEST_ASSERT_EQUAL_INT(2, s.worst_ch);
  const char* js = r.snap();
  char want[48];
  std::snprintf(want, sizeof(want), "\"worst\":2,");
  TEST_ASSERT_TRUE(contains(js, want));
  TEST_ASSERT_TRUE(contains(js, "\"state\":\"out_of_sync\""));
  // Kolmannen kanavan d on positiivinen ja lähellä 4:ää.
  const char* ch = std::strstr(js, "\"ch\":[");
  TEST_ASSERT_NOT_NULL(ch);
  const char* third = ch;
  for (int k = 0; k < 3; ++k) third = std::strstr(third + 1, "{\"d\":");
  TEST_ASSERT_NOT_NULL(third);
  const float d = std::strtof(third + 5, nullptr);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, s.vac_delta_kpa[2], d);
  TEST_ASSERT_GREATER_THAN_FLOAT(3.5f, d);
  // Ruuvineuvo rakenteisena (WP-10, §4.7: d > 0 -> avaa): sama kanava ja suunta
  // kuin advice-teksti, ja muoto on mockin.
  TEST_ASSERT_TRUE_MESSAGE(contains(js, "\"advice\":\"SYL 3: avaa läppää"), js);
  TEST_ASSERT_TRUE_MESSAGE(contains(js, ",\"act\":{\"ch\":2,\"open\":true},\"flags\":"), js);
  r.dropWave();
  Shape sh;
  TEST_ASSERT_EQUAL_STRING(kShapeAdvice, shapeOfJson(r.snap(), sh));
}

void test_act_close_direction() {
  // SYL 2 imee 4 kPa vähemmän kuin master (läppä avoimempi): d < 0 -> sulje
  // (§4.7). act seuraa statusta, ei d:n etumerkkiä snapshotissa.
  Rig r;
  SimParams sp;
  sp.sync_err_kpa[1] = 4.0f;
  r.begin(sp);
  r.run(6.0f);
  const StatusResult& s = r.p.status();
  TEST_ASSERT_EQUAL_INT(1, s.act_ch);
  TEST_ASSERT_FALSE(s.act_open);
  const char* js = r.snap();
  TEST_ASSERT_TRUE_MESSAGE(contains(js, "\"advice\":\"SYL 2: sulje läppää"), js);
  TEST_ASSERT_TRUE_MESSAGE(contains(js, ",\"act\":{\"ch\":1,\"open\":false},"), js);
}

void test_cond_follows_status_hose_sensor_and_sat() {
  // ch[i].cond on statuksen luokitus sellaisenaan (WP-10). Arvot: §8 rivit 6, 7
  // ja 10 (firmware.md §5).
  char c[16];
  {
    Rig r;   // rivi 6: SYL 2:n letku irti -> pitoajan jälkeen "hose", muut mittaavat
    SimParams sp;
    sp.fault[1] = sbx::SimFault::HoseOff;
    r.begin(sp);
    r.run(4.0f);
    const char* js = r.snap();
    TEST_ASSERT_TRUE_MESSAGE(contains(js, "\"state\":\"fault_hose\""), js);
    TEST_ASSERT_EQUAL_STRING("ok", condOf(js, 0, c));
    TEST_ASSERT_EQUAL_STRING("hose", condOf(js, 1, c));
    TEST_ASSERT_EQUAL_STRING("ok", condOf(js, 2, c));
    TEST_ASSERT_TRUE(contains(js, "\"act\":null,"));
    assertCondMirrorsStatus(js, r.p.status());
  }
  {
    Rig r;   // rivi 7: SYL 3:n anturi alhaalla
    SimParams sp;
    sp.fault[2] = sbx::SimFault::SensorLow;
    r.begin(sp);
    r.run(4.0f);
    const char* js = r.snap();
    TEST_ASSERT_TRUE_MESSAGE(contains(js, "\"state\":\"fault_sensor\""), js);
    TEST_ASSERT_EQUAL_STRING("sensor", condOf(js, 2, c));
    TEST_ASSERT_EQUAL_STRING("ok", condOf(js, 0, c));
    assertCondMirrorsStatus(js, r.p.status());
  }
  {
    Rig r;   // rivi 10: paluuaalto leikkautuu kaikissa kanavissa (H1)
    SimParams sp;
    sp.base_kpa = -30.0f;
    sp.peak_kpa = 40.0f;
    r.begin(sp);
    r.run(5.0f);
    const char* js = r.snap();
    TEST_ASSERT_TRUE_MESSAGE(contains(js, "\"flags\":{\"sat\":true,"), js);
    for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_STRING("sat", condOf(js, i, c));
    assertCondMirrorsStatus(js, r.p.status());
  }
}

// ---------------------------------------------------------------------------
// Aaltomuoto (§5: 16 näytteen lohkot, 62,5 Hz, enintään 16 riviä, tyhjenee)
// ---------------------------------------------------------------------------

// wave.s:n rivit: sisäkkäisyystason 2 taulukot (rivillä on vain lukuja).
int waveRows(const char* js) {
  const char* s = std::strstr(js, "\"s\":[");
  if (s == nullptr) return -1;
  int depth = 0;
  int rows = 0;
  for (const char* q = s + 4; *q != '\0'; ++q) {
    if (*q == '[') {
      if (++depth == 2) ++rows;
    } else if (*q == ']') {
      if (--depth == 0) break;
    }
  }
  return rows;
}

void test_wave_rows_drain_and_cap() {
  Rig r;
  SimParams sp;
  sp.engine_on = false;
  r.begin(sp);
  r.run(0.064f);  // 4 lohkoa
  TEST_ASSERT_EQUAL_INT(4, waveRows(r.snap()));
  TEST_ASSERT_EQUAL_INT(0, waveRows(r.snap()));  // edellinen viesti tyhjensi
  TEST_ASSERT_TRUE(contains(g_buf, "\"s\":[]"));
  r.run(10.0f);  // ei asiakkaita: jono pitää uusimmat 16
  TEST_ASSERT_EQUAL_INT(16, waveRows(r.snap()));
  // Rivi = kolme kalibroitua kPa-arvoa: ilmanpaine ~0 (kohina 2 mV = 0,08 kPa).
  const char* row = std::strstr(g_buf, "\"s\":[[") + 6;
  for (int c = 0; c < kChannels; ++c) {
    const float v = std::strtof(row, const_cast<char**>(&row));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, v);
    ++row;  // ',' tai ']'
  }
}

// ---------------------------------------------------------------------------
// Kalibrointiosio ja diagnostiikka
// ---------------------------------------------------------------------------

void test_cal_section_tracks_capture_and_messages() {
  Rig r;
  SimParams sp;
  sp.engine_on = false;
  r.begin(sp);
  r.run(4.0f);  // automaattinen nollaus (rivi 1)
  TEST_ASSERT_TRUE(contains(r.snap(),
                            "\"cal\":{\"busy\":false,\"zero\":true,\"gain\":false,"
                            "\"msg\":\"Automaattinen nollaus tehty (moottori sammuksissa)\"}"));
  TEST_ASSERT_TRUE(r.p.startGainCapture());
  r.run(0.1f);
  TEST_ASSERT_TRUE(contains(r.snap(), "\"cal\":{\"busy\":true,\"zero\":true,\"gain\":false,"));
  r.run(2.0f);  // T-haaraa ei ole: ilmanpaine -> TooSmall
  char want[256];
  std::snprintf(want, sizeof(want), "\"busy\":false,\"zero\":true,\"gain\":false,\"msg\":\"%s\"}",
                sbx::calResultText(sbx::CalResult::TooSmall));
  TEST_ASSERT_TRUE_MESSAGE(contains(r.snap(), want), g_buf);
}

void test_diag_frozen_and_gaps_from_ring() {
  Rig r;
  r.begin(SimParams());
  r.run(0.2f);
  r.p.ring().freeze();
  TEST_ASSERT_TRUE(contains(r.snap(), "\"frozen\":true}"));
  r.p.ring().unfreeze();
  // Pudonnut näyte (seq-hyppy) näkyy diag.gaps:issa (§4.9).
  RawFrame f = r.sim.next();
  f.seq += 5;
  r.p.push(f);
  TEST_ASSERT_TRUE(contains(r.snap(), "\"gaps\":1,"));
}

// ---------------------------------------------------------------------------
// Rajat: null-käytäntö, rajaus, puskuri
// ---------------------------------------------------------------------------

namespace {

// Näkymä ilman Processoria: pahin tapaus ja erikoisarvot.
struct ViewRig {
  StatusResult s;
  ChannelState ch[kChannels];
  Config cfg;
  Calibration cal;
  CalStatus cs;
  float rows[Processor::kWaveRows][kChannels];
  SnapshotView v;

  ViewRig() {
    for (int i = 0; i < kChannels; ++i) v.ch[i] = &ch[i];
    v.status = &s;
    v.cfg = &cfg;
    v.cal = &cal;
    v.cal_status = &cs;
    v.rows = rows;
    for (auto& row : rows) {
      for (float& x : row) x = 0.0f;
    }
  }
};

}  // namespace

void test_non_finite_becomes_null_and_numbers_are_clamped() {
  ViewRig g;
  g.ch[0].has_pulse = true;
  g.ch[0].mean_kpa = std::nanf("");
  g.ch[0].level_kpa = INFINITY;
  g.ch[1].has_pulse = true;
  g.ch[1].mean_kpa = 1.0e9f;      // rikkinäinen lähde: rajataan, ei pidennä viestiä
  g.ch[1].min_kpa = -1.0e9f;
  g.s.spread_kpa = 2.345f;
  g.v.rpm.valid = true;
  g.v.rpm.rpm = 851.24f;
  g.v.rpm.cv = 0.011234f;
  g.v.rpm.sd_rpm = std::nanf("");
  g.v.nrows = 1;
  g.rows[0][0] = -55.126f;
  const size_t len = sbx::snapshotWrite(g.v, SnapshotMeta(), g_buf, sizeof(g_buf));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(len));
  TEST_ASSERT_TRUE_MESSAGE(jsoncheck::valid(g_buf), g_buf);
  TEST_ASSERT_TRUE(contains(g_buf, "{\"d\":0,\"mean\":null,\"level\":null,"));
  TEST_ASSERT_TRUE(contains(g_buf, "\"mean\":99999,\"level\":0,\"min\":-99999,"));
  // Desimaalit kuten mockissa: spread 2, rpm 1, rpm_cv 4, aalto 2.
  TEST_ASSERT_TRUE(contains(g_buf, "\"rpm\":851.2,\"rpm_sd\":null,\"rpm_cv\":0.0112,\"spread\":2.35,"));
  TEST_ASSERT_TRUE(contains(g_buf, "\"s\":[[-55.13,0,0]]"));
  TEST_ASSERT_TRUE_MESSAGE(std::strchr(g_buf, '\n') == nullptr, "SSE data = yksi rivi");
}

void test_cond_keys_and_act_range() {
  // firmware.md §5: cond-arvot (WP-10). tools/mock_server.py --check-snapshot
  // vertaa mockin arvojoukkoa channelCondKey:hin.
  using sbx::ChannelCond;
  TEST_ASSERT_EQUAL_STRING("ok", sbx::channelCondKey(ChannelCond::Ok));
  TEST_ASSERT_EQUAL_STRING("sat", sbx::channelCondKey(ChannelCond::Sat));
  TEST_ASSERT_EQUAL_STRING("idle", sbx::channelCondKey(ChannelCond::Idle));
  TEST_ASSERT_EQUAL_STRING("no_pulse", sbx::channelCondKey(ChannelCond::NoPulse));
  TEST_ASSERT_EQUAL_STRING("hose", sbx::channelCondKey(ChannelCond::Hose));
  TEST_ASSERT_EQUAL_STRING("blocked", sbx::channelCondKey(ChannelCond::Blocked));
  TEST_ASSERT_EQUAL_STRING("sensor", sbx::channelCondKey(ChannelCond::Sensor));
  // Tuntematon arvo ei ole koskaan "ok" (UI näyttäisi kanavan mittaavana).
  TEST_ASSERT_EQUAL_STRING("sensor", sbx::channelCondKey(static_cast<ChannelCond>(200)));
  // act_ch kanava-alueen ulkopuolella -> null, ei kanavaa, jota ei ole.
  ViewRig g;
  g.s.act_ch = kChannels;
  g.s.act_open = true;
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(sbx::snapshotWrite(g.v, SnapshotMeta(), g_buf,
                                                                       sizeof(g_buf))));
  TEST_ASSERT_TRUE_MESSAGE(contains(g_buf, "\"act\":null,"), g_buf);
}

void test_worst_case_fits_buffer() {
  // Pahin tapaus: leveimmät luvut, pisimmät tekstit, pisin src ja tila,
  // pulssi päällä (luvut ovat pidempiä kuin null) ja muut liput false
  // ("false" > "true"), act-objekti ("open":false), pisin cond ("no_pulse"),
  // 16 aaltoriviä ja kymmennumeroiset kokonaisluvut.
  ViewRig g;
  g.s.state = sbx::SyncState::RpmTooHigh;  // "rpm_too_high" = pisimpien joukossa
  std::memset(g.s.headline, 'x', sizeof(g.s.headline) - 1);
  std::memset(g.s.advice, 'x', sizeof(g.s.advice) - 1);
  g.s.fault_ch = -1;
  g.s.worst_ch = -1;
  g.s.act_ch = 2;
  g.s.act_open = false;
  g.s.spread_kpa = kWide;
  for (int i = 0; i < kChannels; ++i) {
    g.s.ch_cond[i] = sbx::ChannelCond::NoPulse;
    g.s.vac_delta_kpa[i] = kWide;
    ChannelState& c = g.ch[i];
    c.has_pulse = true;
    c.mean_kpa = c.level_kpa = c.min_kpa = c.p2p_kpa = c.rpm = kWide;
    c.last_mv = 65535;
  }
  g.cfg.status.tol_green_kpa = g.cfg.status.tol_yellow_kpa = kWide;
  g.v.rpm.valid = true;
  g.v.rpm.rpm = g.v.rpm.sd_rpm = g.v.rpm.cv = kWide;
  g.v.master = 2;
  g.v.gaps = 4294967295u;
  g.v.nrows = Processor::kWaveRows;
  for (auto& row : g.rows) {
    for (float& x : row) x = kWide;
  }
  // Pisin kalibrointiviesti kaikista staattisista teksteistä.
  const char* longest = "";
  const char* msgs[] = {Processor::kMsgAutoZero, Processor::kMsgGainStart, Processor::kMsgGainBusy,
                        Processor::kMsgGainReset, Processor::kMsgGainAborted};
  for (const char* t : msgs) {
    if (std::strlen(t) > std::strlen(longest)) longest = t;
  }
  for (int k = 0; k <= static_cast<int>(sbx::CalResult::NoZero); ++k) {
    const char* t = sbx::calResultText(static_cast<sbx::CalResult>(k));
    if (std::strlen(t) > std::strlen(longest)) longest = t;
  }
  g.cs.msg = longest;
  SnapshotMeta m;
  m.t_ms = 4294967295u;
  m.heap = 4294967295u;
  m.src = "hx710b";
  m.fs_hz = kWide;
  const size_t len = sbx::snapshotWrite(g.v, m, g_buf, sizeof(g_buf));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(len));
  TEST_ASSERT_TRUE(jsoncheck::valid(g_buf));
  char msg[96];
  std::snprintf(msg, sizeof(msg), "pahin tapaus %u tavua (puskuri %u)", static_cast<unsigned>(len),
                static_cast<unsigned>(kSnapshotJsonSize));
  TEST_MESSAGE(msg);
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<long long>(kSnapshotJsonSize) - 128, static_cast<long long>(len));
  // Puskuri tavun liian pieni -> 0, ei katkaistua viestiä.
  TEST_ASSERT_EQUAL_size_t(0, sbx::snapshotWrite(g.v, m, g_buf, len));
  TEST_ASSERT_EQUAL_size_t(len, sbx::snapshotWrite(g.v, m, g_buf, len + 1));
}

void test_typical_size_and_missing_parts() {
  Rig r;
  r.begin(SimParams());
  r.run(5.0f);
  const size_t full = std::strlen(r.snap());  // 16 aaltoriviä (jono täynnä)
  r.run(0.0667f);                             // 15 Hz (D9): ~4 lohkoa per viesti
  const size_t len = std::strlen(r.snap());
  char msg[96];
  std::snprintf(msg, sizeof(msg), "joutokäynti %u tavua 15 Hz:llä, %u tavua täydellä jonolla",
                static_cast<unsigned>(len), static_cast<unsigned>(full));
  TEST_MESSAGE(msg);
  TEST_ASSERT_LESS_THAN(static_cast<long long>(full), static_cast<long long>(len));
  SnapshotView v;  // puuttuvat osat -> 0, ei puolikasta viestiä
  TEST_ASSERT_EQUAL_size_t(0, sbx::snapshotWrite(v, SnapshotMeta(), g_buf, sizeof(g_buf)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_shape_engine_off_matches_mock);
  RUN_TEST(test_shape_running_matches_mock);
  RUN_TEST(test_values_follow_status_and_meta);
  RUN_TEST(test_delta_sign_and_worst_follow_status);
  RUN_TEST(test_act_close_direction);
  RUN_TEST(test_cond_follows_status_hose_sensor_and_sat);
  RUN_TEST(test_wave_rows_drain_and_cap);
  RUN_TEST(test_cal_section_tracks_capture_and_messages);
  RUN_TEST(test_diag_frozen_and_gaps_from_ring);
  RUN_TEST(test_non_finite_becomes_null_and_numbers_are_clamped);
  RUN_TEST(test_cond_keys_and_act_range);
  RUN_TEST(test_worst_case_fits_buffer);
  RUN_TEST(test_typical_size_and_missing_parts);
  return UNITY_END();
}
