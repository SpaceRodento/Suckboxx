// WP-6: integraatiotestit EngineSim -> Processor (docs/firmware.md §4.8,
// SUUNNITTELU.md §8).
//
// Jokainen käytösmatriisin rivi (1–12, 4a ja 4b erikseen) on oma testinsä,
// lisäksi D10, D11, D12, R4, §11:n penkkitesti ja H1:n dokumentoiva testi.
// Totuus tulee simulaattorista (trueMeanKpa, trueRpm), ja odotettu tila ja
// säätösuunta lasketaan siitä matriisin säännöillä (truthOf: D1–D3, D11), ei
// mitatuista luvuista. Aika on simuloitua: push 1 kHz ja tick 20 Hz kuten
// liimassa (docs/firmware.md §4.9).
//
// Viive: testit mittaavat, kuinka nopeasti tila asettuu muutoksen jälkeen, ja
// tulostavat sen VIIVE-riveinä. Asettuminen = hetki, josta lähtien tila on
// pysynyt odotettuna mittausikkunan loppuun (välkyntä ei lyhennä sitä).
//
// Tiedetyt punaiset (rinnakkaiset paketit korjaavat, docs/jatko.md):
//   - test_row5_common_mode_step_no_wrong_advice: DSP major 1 (syklintunnistus
//     painemuutoksen jälkeen) + AVOIN: kanavien keskiarvojen porrastus.
//   - test_row6_start_and_stop_never_flash_hose_fault: WP-4 "HoseOff (rivi 6)
//     luokitellaan ilman viivettä".
//   - test_row9_full_throttle_start_locks_within_dsp_spec: DSP major 1 ja 2.
//   - test_row10_no_saturation_flash_on_start_and_stop: WP-4 "Saturaatiolippu
//     välähtää jokaisella sammutuksella ja käynnistyksellä".

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/engine_sim.h"
#include "core/processor.h"
#include "core/snapshot.h"

using sbx::CalResult;
using sbx::Calibration;
using sbx::Config;
using sbx::EngineSim;
using sbx::Processor;
using sbx::RawSample;
using sbx::SimFault;
using sbx::SimParams;
using sbx::StatusParams;
using sbx::StatusResult;
using sbx::SyncState;
using sbx::kChannels;

namespace {

constexpr size_t kRingCap = 8192;  // ESP32:n koko (§4.5)
RawSample g_mem[kRingCap];
RawSample g_mem2[kRingCap];        // toinen penkki samassa testissä (oma rengas)
char g_snap[sbx::kSnapshotJsonSize];

constexpr float kTick = 0.05f;  // 20 Hz (§4.9)

int S(SyncState s) { return static_cast<int>(s); }
uint32_t bit(SyncState s) { return 1u << static_cast<int>(s); }

// --- totuus matriisista ------------------------------------------------------

// Matriisin tuomio simulaattorin totuudesta. D1: lukema = syklin keskiarvo;
// D3: d = master - kanava (positiivinen = imee enemmän -> "avaa", §4.7);
// D2: spread = max(d) - min(d) masterin nolla mukaan lukien; D11: ilman
// nollausta ei vihreää.
struct Truth {
  SyncState state;
  float d[kChannels];
  float spread;
  int worst;
  bool open;
};

Truth truthOf(const EngineSim& sim, int master, const StatusParams& sp, bool zero_valid) {
  Truth t;
  float lo = 0.0f;
  float hi = 0.0f;
  float worst_abs = -1.0f;
  t.worst = -1;
  for (int i = 0; i < kChannels; ++i) {
    t.d[i] = sim.trueMeanKpa(master) - sim.trueMeanKpa(i);
    if (t.d[i] < lo) lo = t.d[i];
    if (t.d[i] > hi) hi = t.d[i];
    if (i != master && std::fabs(t.d[i]) > worst_abs) {
      worst_abs = std::fabs(t.d[i]);
      t.worst = i;
    }
  }
  t.spread = hi - lo;
  if (t.spread <= sp.tol_green_kpa) {
    t.state = zero_valid ? SyncState::Synced : SyncState::Near;
  } else {
    t.state = t.spread <= sp.tol_yellow_kpa ? SyncState::Near : SyncState::OutOfSync;
  }
  t.open = t.d[t.worst] > 0.0f;
  return t;
}

bool startsWith(const char* s, const char* prefix) {
  return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}
bool endsWith(const char* s, const char* suffix) {
  const size_t a = std::strlen(s);
  const size_t b = std::strlen(suffix);
  return a >= b && std::strcmp(s + a - b, suffix) == 0;
}

void report(const char* what, float seconds) {
  char m[160];
  std::snprintf(m, sizeof(m), "VIIVE %s: %.2f s", what, static_cast<double>(seconds));
  TEST_MESSAGE(m);
}

// Float-rajat: Unityn (ja shimin) LESS/GREATER_OR_EQUAL muuntavat kokonaisluvuiksi.
void atMost(float limit, float v, const char* what) {
  char m[160];
  std::snprintf(m, sizeof(m), "%s: %.3f > %.3f", what, static_cast<double>(v),
                static_cast<double>(limit));
  TEST_ASSERT_TRUE_MESSAGE(v <= limit, m);
}
void atLeast(float limit, float v, const char* what) {
  char m[160];
  std::snprintf(m, sizeof(m), "%s: %.3f < %.3f", what, static_cast<double>(v),
                static_cast<double>(limit));
  TEST_ASSERT_TRUE_MESSAGE(v >= limit, m);
}

// Snapshotin ylätason rpm (ch[i].rpm voi olla null, kun kanava ei pulssi).
bool topRpmIsNull(const char* js) {
  const char* p = std::strstr(js, "],\"rpm\":");
  return p != nullptr && std::strncmp(p + 8, "null", 4) == 0;
}

// --- penkki ------------------------------------------------------------------

// Mitä mittausikkunan aikana nähtiin.
struct Watch {
  float settle = -1.0f;        // s ikkunan alusta: tila `want` tästä loppuun; -1 = ei
  int entries = 0;             // montako kertaa `want`iin tultiin (välkyntä)
  uint32_t seen = 0;           // nähdyt tilat (bit())
  bool sat = false;            // flag_saturation jossain tickissä
  bool drift = false;          // flag_drift jossain tickissä
  float last_verdict = -1.0f;  // viimeisin tick, jolla show_verdict (s ikkunan alusta)
  int ticks = 0;
  int bars_while_unstable = 0; // rivi 2: palkit piilossa
};

struct Bench {
  Processor p;
  EngineSim sim;
  uint32_t n = 0;

  explicit Bench(RawSample* mem = g_mem) : p(mem, kRingCap) {
    sim.configure(SimParams(), p.config().sensor);
  }

  float now() const { return static_cast<float>(n) / 1000.0f; }
  const StatusResult& st() const { return p.status(); }
  // POST /api/sim -tapaan: liima yhdistää muutoksen sim.params():iin.
  void set(const SimParams& s) { sim.configure(s, p.config().sensor); }
  SimParams params() const { return sim.params(); }
  Truth truth() const {
    return truthOf(sim, p.master(), p.config().status, p.calibration().zero_valid);
  }

  bool step() {
    p.push(sim.next());
    ++n;
    if (n % 50 != 0) return false;
    p.tick(now());
    return true;
  }
  void run(float seconds) {
    const uint32_t k = static_cast<uint32_t>(seconds * 1000.0f + 0.5f);
    for (uint32_t i = 0; i < k; ++i) step();
  }
  Watch watch(float seconds, SyncState want) {
    Watch w;
    const uint32_t n0 = n;
    const uint32_t k = static_cast<uint32_t>(seconds * 1000.0f + 0.5f);
    bool in_want = false;
    for (uint32_t i = 0; i < k; ++i) {
      if (!step()) continue;
      const StatusResult& r = st();
      const float t = static_cast<float>(n - n0) / 1000.0f;
      ++w.ticks;
      w.seen |= bit(r.state);
      if (r.flag_saturation) w.sat = true;
      if (r.flag_drift) w.drift = true;
      if (r.show_verdict) w.last_verdict = t;
      if (r.state == SyncState::Unstable && r.show_bars) ++w.bars_while_unstable;
      if (r.state == want) {
        if (!in_want) {
          ++w.entries;
          w.settle = t;
        }
        in_want = true;
      } else {
        in_want = false;
        w.settle = -1.0f;
      }
    }
    return w;
  }
  template <typename Pred>
  float runUntil(Pred pred, float max_s) {
    const uint32_t n0 = n;
    const uint32_t k = static_cast<uint32_t>(max_s * 1000.0f + 0.5f);
    for (uint32_t i = 0; i < k; ++i) {
      if (step() && pred()) return static_cast<float>(n - n0) / 1000.0f;
    }
    return -1.0f;
  }
};

// Moottori seis off_hold_s:n yli -> automaattinen nollaus (rivi 1), sitten
// käyntiin parametreilla s. Anturivirheet (s.sensor_*) ovat mukana koko ajan.
void zeroThenStart(Bench& b, SimParams s) {
  SimParams off = s;
  off.engine_on = false;
  b.set(off);
  b.run(4.0f);
  TEST_ASSERT_TRUE_MESSAGE(b.p.calibration().zero_valid, "rivi 1: nollaus ennen käynnistystä");
  s.engine_on = true;
  b.set(s);
}

// Synkronoitu joutokäynti tuomioon asti (rivi 3), jotta muutoksen viive
// mitataan vakaasta tilasta.
void settleSynced(Bench& b, const SimParams& s) {
  zeroThenStart(b, s);
  const Watch w = b.watch(7.0f, SyncState::Synced);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
}

// Tila, d ja säätöneuvo matriisin mukaan (rivit 3/4a/4b).
void assertVerdict(const Bench& b, float d_tol) {
  const Truth t = b.truth();
  const StatusResult& r = b.st();
  TEST_ASSERT_EQUAL_INT(S(t.state), S(r.state));
  TEST_ASSERT_TRUE(r.show_bars);
  TEST_ASSERT_TRUE(r.show_verdict);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(d_tol, t.d[i], r.vac_delta_kpa[i]);
  TEST_ASSERT_FLOAT_WITHIN(d_tol, t.spread, r.spread_kpa);
  if (t.state == SyncState::Synced) {
    TEST_ASSERT_EQUAL_STRING("Synkronoitu", r.headline);
    TEST_ASSERT_TRUE_MESSAGE(startsWith(r.advice, "Valmis: ero "), r.advice);
    return;
  }
  TEST_ASSERT_EQUAL_INT(t.worst, r.worst_ch);
  char head[64];
  char tail[48];
  std::snprintf(head, sizeof(head), "SYL %d: %s läppää – imee ", t.worst + 1,
                t.open ? "avaa" : "sulje");
  std::snprintf(tail, sizeof(tail), " kPa %s kuin SYL %d", t.open ? "enemmän" : "vähemmän",
                b.p.master() + 1);
  TEST_ASSERT_TRUE_MESSAGE(startsWith(r.advice, head), r.advice);
  TEST_ASSERT_TRUE_MESSAGE(endsWith(r.advice, tail), r.advice);
}

const char* snap(Bench& b) {
  const size_t len = sbx::snapshotJson(b.p, sbx::SnapshotMeta(), g_snap, sizeof(g_snap));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(len));
  return g_snap;
}

// Kalibroitu T-haara (R4, docs/rauta.md §7): kaikki letkut samassa
// alipaineessa ilman pulsseja, sitten 2 s vahvistusmittaus.
void gainCalibrateOnTee(Bench& b, float kpa) {
  SimParams s = b.params();
  s.engine_on = true;  // simulaattorissa paine tulee base_kpa:sta
  s.depth_kpa = 0.0f;
  s.res_kpa = 0.0f;
  s.peak_kpa = 0.0f;
  s.xtalk = 0.0f;
  s.base_kpa = kpa;
  for (int i = 0; i < kChannels; ++i) {
    s.sync_err_kpa[i] = 0.0f;
    s.fault[i] = SimFault::None;
  }
  b.set(s);
  b.run(1.0f);
  TEST_ASSERT_TRUE(b.p.startGainCapture());
  b.run(2.2f);
  TEST_ASSERT_FALSE(b.p.calStatus().busy);
  TEST_ASSERT_EQUAL_STRING(sbx::calResultText(CalResult::Ok), b.p.calStatus().msg);
  TEST_ASSERT_TRUE(b.p.calibration().gain_valid);
}

SimParams withErr(float e0, float e1, float e2) {
  SimParams s;
  s.sync_err_kpa[0] = e0;
  s.sync_err_kpa[1] = e1;
  s.sync_err_kpa[2] = e2;
  return s;
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Rivi 1: moottori sammuksissa -> "Ei alipainesignaalia" + automaattinen nollaus
// ---------------------------------------------------------------------------

void test_row1_engine_off_no_signal_and_auto_zero() {
  Bench b;
  SimParams s;
  s.engine_on = false;
  // R4: anturien omat offsetit ennen nollausta (negatiiviset näkyvät ilmanpaineessa, H1).
  s.sensor_offset_kpa[0] = -1.2f;
  s.sensor_offset_kpa[1] = -0.4f;
  s.sensor_offset_kpa[2] = -0.8f;
  b.set(s);
  const Watch w = b.watch(2.9f, SyncState::NoSignal);
  TEST_ASSERT_EQUAL_INT(bit(SyncState::NoSignal), w.seen);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, kTick, w.settle);  // ensimmäinen tick
  TEST_ASSERT_LESS_THAN_FLOAT(0.0f, w.last_verdict);
  TEST_ASSERT_EQUAL_STRING("Ei alipainesignaalia", b.st().headline);
  TEST_ASSERT_EQUAL_STRING("Moottori sammuksissa tai letkut irti", b.st().advice);
  TEST_ASSERT_FALSE_MESSAGE(b.p.calibration().zero_valid, "rivi 1: ei pulssia >= 3 s ennen nollausta");
  const float t_zero = 2.9f + b.runUntil([&] { return b.p.calibration().zero_valid; }, 1.0f);
  report("rivi 1: NoSignal", w.settle);
  report("rivi 1: automaattinen nollaus", t_zero);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 3.05f, t_zero);  // off_hold_s 3 s ensimmäisestä tickistä
  TEST_ASSERT_EQUAL_STRING(Processor::kMsgAutoZero, b.p.calStatus().msg);
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -s.sensor_offset_kpa[i], b.p.calibration().offset_kpa[i]);
  }
  b.run(0.5f);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, b.p.channel(i).level_kpa);
  TEST_ASSERT_EQUAL_INT(S(SyncState::NoSignal), S(b.st().state));
  TEST_ASSERT_FALSE(b.st().flag_uncal_zero);
}

// ---------------------------------------------------------------------------
// Rivi 2: käynnistyy / epävakaa -> "Odota", palkit piilossa
// ---------------------------------------------------------------------------

void test_row2_engine_start_waits_for_stable_run() {
  Bench b;
  zeroThenStart(b, withErr(0.0f, 0.3f, -0.4f));
  const Watch w = b.watch(8.0f, SyncState::Synced);
  report("rivi 2->3: käynnistyksestä tuomioon", w.settle);
  // Rivi 3 / D12: vakaa >= stable_hold_s ennen tuomiota, eli ei ennen 3 s:a.
  // Yläraja: lukitus (~0,5 s) + rpm-hajonnan historia (~1 s) + 3 s + vara.
  atLeast(3.0f, w.settle, "tuomio ennen vakausaikaa");
  atMost(6.0f, w.settle, "tuomio viipyy");
  TEST_ASSERT_EQUAL_INT(1, w.entries);
  // Ennen tuomiota vain "ei signaalia" (DSP lukitsee) tai "odota" (rivi 2).
  TEST_ASSERT_EQUAL_INT(bit(SyncState::NoSignal) | bit(SyncState::Unstable) | bit(SyncState::Synced),
                        w.seen);
  TEST_ASSERT_EQUAL_INT(0, w.bars_while_unstable);
}

void test_row2_rough_running_hides_bars() {
  // Rivi 2: rpm-hajonta > 15 % (unstable_cv). Simulaattorin jitter = jakson
  // suhteellinen keskihajonta, eli 0,25 = 25 %.
  Bench b;
  SimParams s = withErr(0.0f, 0.0f, 0.0f);
  s.rpm_jitter = 0.25f;
  zeroThenStart(b, s);
  b.run(2.0f);  // lukitus
  Watch w;
  int rough = 0;
  const uint32_t n0 = b.n;
  while (b.n - n0 < 10000u) {
    if (!b.step()) continue;
    const StatusResult& r = b.st();
    ++w.ticks;
    w.seen |= bit(r.state);
    if (r.show_bars || r.show_verdict) ++w.bars_while_unstable;
    if (startsWith(r.advice, "Käynti epätasainen: hajonta ")) ++rough;
  }
  TEST_ASSERT_EQUAL_INT(bit(SyncState::Unstable), w.seen);
  TEST_ASSERT_EQUAL_INT(0, w.bars_while_unstable);
  TEST_ASSERT_EQUAL_STRING("Odota, käynti tasaantuu", b.st().headline);
  TEST_ASSERT_GREATER_THAN(w.ticks / 2, rough);
  // rpm näytetään harmaana (UI) eli snapshotissa on luku, kun arvio on voimassa.
  TEST_ASSERT_TRUE(b.p.rpm().valid);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.15f, b.p.rpm().cv);
  TEST_ASSERT_NOT_NULL(std::strstr(snap(b), "\"state\":\"unstable\""));
  TEST_ASSERT_FALSE(topRpmIsNull(g_snap));
  TEST_ASSERT_NOT_NULL(std::strstr(g_snap, "\"bars\":false,\"verdict\":false"));
}

// ---------------------------------------------------------------------------
// Rivi 3: joutokäynti vakaa ja synkronissa -> vihreä
// ---------------------------------------------------------------------------

void test_row3_idle_synced() {
  Bench b;
  zeroThenStart(b, withErr(0.0f, 0.3f, -0.4f));  // totuus: spread 0,7 kPa <= 1,5 (D2)
  const Watch first = b.watch(7.0f, SyncState::Synced);
  report("rivi 3: käynnistyksestä vihreään", first.settle);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, first.settle);
  const Watch w = b.watch(10.0f, SyncState::Synced);
  TEST_ASSERT_EQUAL_INT(bit(SyncState::Synced), w.seen);  // pysyy vihreänä, ei välky
  assertVerdict(b, 0.15f);
  TEST_ASSERT_TRUE(endsWith(b.st().advice, " kPa (raja 1,5 kPa)"));
  // "Δ-luvut, rpm ja ±hajonta": rpm tarkka, hajonta jitteristä (1 % * 850).
  const sbx::RpmEstimate e = b.p.rpm();
  TEST_ASSERT_TRUE(e.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f * b.sim.trueRpm(), b.sim.trueRpm(), e.rpm);
  TEST_ASSERT_FLOAT_WITHIN(6.0f, 8.5f, e.sd_rpm);
  TEST_ASSERT_FALSE(b.st().flag_uncal_zero);
}

// ---------------------------------------------------------------------------
// Rivit 4a ja 4b: lähellä / ei synkronissa -> pahin kanava ja säätösuunta
// ---------------------------------------------------------------------------

void test_row4a_near_names_worst_channel_and_direction() {
  Bench b;
  settleSynced(b, withErr(0.0f, 0.0f, 0.0f));
  SimParams s = b.params();
  s.sync_err_kpa[1] = 2.0f;  // SYL 2 imee 2 kPa vähemmän (läppä auki) -> d = -2 -> "sulje"
  b.set(s);
  const Watch w = b.watch(6.0f, SyncState::Near);
  char m[96];
  std::snprintf(m, sizeof(m), "rivi 4a: vihreästä keltaiseen (%d siirtymää)", w.entries);
  report(m, w.settle);
  // avg_cycles (8) sykliä 850 rpm:llä = 1,13 s + kaksi tickiä.
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
  atMost(1.13f + 2.0f * kTick, w.settle, "rivi 4a: tuomio viipyy");
  assertVerdict(b, 0.15f);
  TEST_ASSERT_EQUAL_STRING("Lähellä", b.st().headline);
  TEST_ASSERT_TRUE_MESSAGE(startsWith(b.st().advice, "SYL 2: sulje läppää"), b.st().advice);
}

void test_row4b_out_of_sync_names_worst_channel_and_direction() {
  Bench b;
  settleSynced(b, withErr(0.0f, 0.0f, 0.0f));
  SimParams s = b.params();
  s.sync_err_kpa[2] = -4.0f;  // SYL 3 imee 4 kPa enemmän (läppä kiinni) -> d = +4 -> "avaa"
  b.set(s);
  const Watch w = b.watch(6.0f, SyncState::OutOfSync);
  char m[96];
  std::snprintf(m, sizeof(m), "rivi 4b: vihreästä punaiseen (%d siirtymää)", w.entries);
  report(m, w.settle);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
  atMost(1.13f + 2.0f * kTick, w.settle, "rivi 4b: tuomio viipyy");
  assertVerdict(b, 0.15f);
  TEST_ASSERT_EQUAL_STRING("Ei synkronissa", b.st().headline);
  TEST_ASSERT_TRUE_MESSAGE(startsWith(b.st().advice, "SYL 3: avaa läppää"), b.st().advice);
  // Korjaus takaisin synkroniin: sama viive toiseen suuntaan.
  s = b.params();
  s.sync_err_kpa[2] = 0.0f;
  b.set(s);
  const Watch back = b.watch(6.0f, SyncState::Synced);
  report("rivi 4b: punaisesta vihreään", back.settle);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, back.settle);
  atMost(1.13f + 2.0f * kTick, back.settle, "rivi 4b: paluu vihreään viipyy");
  assertVerdict(b, 0.15f);
}

// ---------------------------------------------------------------------------
// Rivi 5: kierrosluku ajautuu -> Δ pysyvät näkyvissä
// ---------------------------------------------------------------------------

void test_row5_rpm_drift_keeps_deltas_visible() {
  Bench b;
  settleSynced(b, withErr(0.0f, 0.8f, 0.0f));
  SimParams s = b.params();
  s.rpm_drift = 100.0f;  // > 50 rpm / 2 s (drift_rpm, drift_window_s)
  b.set(s);
  float t_flag = -1.0f;
  uint32_t seen = 0;
  int hidden = 0;  // tickit, joilla palkit piilossa
  const uint32_t n0 = b.n;
  while (b.n - n0 < 6000u) {
    if (!b.step()) continue;
    const StatusResult& r = b.st();
    seen |= bit(r.state);
    if (!r.show_bars) ++hidden;
    if (r.flag_drift && t_flag < 0.0f) t_flag = static_cast<float>(b.n - n0) / 1000.0f;
  }
  report("rivi 5: ajautumislippu", t_flag);
  // 100 rpm/s ylittää 50 rpm:n 0,5 s:ssa; rpm-arvion 8 syklin keskiarvo ja
  // drift_window_s (2 s) rajaavat viiveen.
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, t_flag);
  atMost(2.5f, t_flag, "rivi 5: ajautumislippu viipyy");
  TEST_ASSERT_TRUE(b.st().flag_drift);
  // Δ-luvut pysyvät näkyvissä koko ajautumisen ajan (yhteismuotoinen liike on
  // jo poistettu erotuksesta, D3), ja tuomio seuraa totuutta.
  TEST_ASSERT_EQUAL_INT(bit(SyncState::Synced), seen);
  TEST_ASSERT_EQUAL_INT(0, hidden);
  assertVerdict(b, 0.3f);
  // rpm-arvio on avg_cycles syklin keskiarvo, joten se seuraa ramppia puolen
  // ikkunan viiveellä: 100 rpm/s * 0,5 * 8 * 120 / rpm.
  const float tr = b.sim.trueRpm();
  TEST_ASSERT_FLOAT_WITHIN(0.01f * tr, tr - 100.0f * 0.5f * 8.0f * 120.0f / tr, b.p.rpm().rpm);
  // Ajautuminen loppuu -> lippu laskee ikkunan (2 s) kuluttua.
  s = b.params();
  s.rpm_drift = 0.0f;
  b.set(s);
  const float t_clear = b.runUntil([&] { return !b.st().flag_drift; }, 4.0f);
  report("rivi 5: lippu pois ajautumisen loputtua", t_clear);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, t_clear);
  atMost(3.0f, t_clear, "rivi 5: lippu jää päälle");
}

void test_row5_common_mode_step_no_wrong_advice() {
  // Kaasun nosto ja palautus (yhteismuotoinen alipaineen muutos, kaikilla
  // kanavilla sama): totuus pysyy synkronoituna, joten väärä tuomio olisi
  // väärä säätöneuvo ("sulje läppää"), vaikka mitään ei pidä kääntää. Sallittu
  // on vihreä tai "odota". Rivi 5: yhteismuotoinen liike ei saa näkyä Δ:ssä.
  // TIEDETTY PUNAINEN: docs/jatko.md "WP-2 DSP (major) 1": syklintunnistus
  // jättää painemuutoksen jälkeen syklejä väliin, jolloin yhden kanavan
  // keskiarvo jää jälkeen.
  // AVOIN: virhettä on myös ilman DSP-vikaa. Kanavien 8 syklin liukuvat
  // keskiarvot päivittyvät 1/3 syklin välein eri aikaan, joten X kPa:n
  // muutos näkyy hetkellisenä spreadinä X / avg_cycles (20 kPa -> 2,5 kPa)
  // avg_cycles syklin ajan. Suositus: DSP laskee kanavien keskiarvot samasta
  // aikaikkunasta (N kokonaista jaksoa samaan hetkeen päättyen; jaksollisen
  // signaalin keskiarvo ei riipu ikkunan vaiheesta), tai StatusMachine pitää
  // tuomion "odota"-tilassa, kun tason EMA muuttuu nopeasti.
  Bench b;
  settleSynced(b, withErr(0.0f, 0.0f, 0.0f));
  const uint32_t wrong = bit(SyncState::Near) | bit(SyncState::OutOfSync);
  SimParams s = b.params();
  s.base_kpa = -35.0f;
  b.set(s);
  const Watch up = b.watch(3.0f, SyncState::Synced);
  s = b.params();
  s.base_kpa = -55.0f;
  b.set(s);
  const Watch down = b.watch(4.0f, SyncState::Synced);
  report("rivi 5: kaasun nosto, vihreä takaisin", up.settle);
  report("rivi 5: kaasun palautus, vihreä takaisin", down.settle);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, static_cast<int>(up.seen & wrong), "väärä tuomio kaasun nostossa");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, static_cast<int>(down.seen & wrong), "väärä tuomio kaasun palautuksessa");
}

// ---------------------------------------------------------------------------
// Rivi 6: yksi letku irti -> VIKA
// ---------------------------------------------------------------------------

void test_row6_hose_off_shows_hose_fault() {
  Bench b;
  settleSynced(b, withErr(0.0f, 0.3f, -0.4f));
  SimParams s = b.params();
  s.fault[1] = SimFault::HoseOff;
  b.set(s);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, b.sim.trueMeanKpa(1));  // totuus: ilmanpaine
  const Watch w = b.watch(5.0f, SyncState::FaultHose);
  report("rivi 6: letku irti -> VIKA", w.settle);
  report("rivi 6: vanha tuomio näkyi vielä", w.last_verdict);
  // Taso-EMA (tau 0,5 s) putoaa hose_off_kpa:n (2 kPa) alle -55:stä 1,66 s:ssa.
  // Raja jättää tilaa rivin 6 viiveelle, jota WP-4:n korjaus voi lisätä.
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
  atMost(3.2f, w.settle, "rivi 6: letkuvika viipyy");
  TEST_ASSERT_EQUAL_INT(1, w.entries);
  // Vanha tuomio saa näkyä vain kunnes pulssi todetaan kadonneeksi
  // (pulse_timeout_s 1 s, dsp.h) - sen jälkeen ei koskaan vihreää (§8).
  atMost(1.0f + 2.0f * kTick, w.last_verdict, "rivi 6: vanha tuomio näkyy");
  const StatusResult& r = b.st();
  TEST_ASSERT_EQUAL_INT(1, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei alipainetta, tarkista letku", r.headline);
  TEST_ASSERT_EQUAL_STRING("Letku irti tai vuotaa – kytke ja tarkista liitokset", r.advice);
  TEST_ASSERT_FALSE(r.show_verdict);
  // Letku takaisin -> vihreä vakausajan jälkeen (lukitus + stable_hold_s).
  s = b.params();
  s.fault[1] = SimFault::None;
  b.set(s);
  const Watch back = b.watch(7.0f, SyncState::Synced);
  report("rivi 6: letku takaisin -> vihreä", back.settle);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, back.settle);
  atMost(6.0f, back.settle, "rivi 6: paluu vihreään viipyy");
  assertVerdict(b, 0.15f);
}

void test_row6_start_and_stop_never_flash_hose_fault() {
  // Käynnistys ja sammutus eivät ole letkuvika: kanavat saavat tai menettävät
  // pulssin muutaman syklin sisällä toisistaan, ja oikea tila on "ei
  // signaalia" (D10) tai "odota" (rivi 2). Matalalla alipaineella (-8 kPa) taso
  // on hose_off_kpa:n (2 kPa) tuntumassa, kun ensimmäinen kanava saa tai
  // menettää pulssin muiden vielä odottaessa.
  // TIEDETTY PUNAINEN: docs/jatko.md "WP-4 status": HoseOff (rivi 6)
  // luokitellaan ilman viivettä - matalalla alipaineella sammutus näyttää
  // hetken väärää letkuvikaa (sammutuksessa FaultMaster, koska master menettää
  // pulssin ensin; sama välähdys tulee käynnistyksessä täydellä kaasulla).
  const uint32_t false_fault = bit(SyncState::FaultHose) | bit(SyncState::FaultMaster);
  {
    Bench b;
    SimParams s;
    s.rpm = 1500.0f;
    s.base_kpa = -8.0f;
    s.depth_kpa = 10.0f;
    zeroThenStart(b, s);
    const Watch start = b.watch(6.0f, SyncState::Synced);
    s = b.params();
    s.engine_on = false;
    b.set(s);
    const Watch stop = b.watch(3.0f, SyncState::NoSignal);
    report("rivi 6: sammutus -> ei signaalia", stop.settle);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, stop.settle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, static_cast<int>(start.seen & false_fault),
                                  "letkuvika käynnistyksessä (1500 rpm)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, static_cast<int>(stop.seen & false_fault),
                                  "letkuvika sammutuksessa (1500 rpm)");
  }
  {
    Bench b;
    SimParams s;
    s.rpm = 6000.0f;  // täysi kaasu: pieni pulssi, matala alipaine
    s.base_kpa = -8.0f;
    s.depth_kpa = 6.0f;
    zeroThenStart(b, s);
    const Watch start = b.watch(3.0f, SyncState::RpmTooHigh);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, static_cast<int>(start.seen & false_fault),
                                  "letkuvika käynnistyksessä (6000 rpm)");
  }
}

// ---------------------------------------------------------------------------
// Rivi 7: anturi rikki -> VIKA, kun ADC on ääripäässä >= 1 s
// ---------------------------------------------------------------------------

void test_row7_sensor_fault_after_hold_time() {
  Bench b;
  settleSynced(b, withErr(0.0f, 0.0f, 0.0f));
  SimParams s = b.params();
  s.fault[2] = SimFault::SensorLow;  // ~10 mV < fault_lo_mv 100
  b.set(s);
  const Watch w = b.watch(3.0f, SyncState::FaultSensor);
  report("rivi 7: anturi alas -> VIKA", w.settle);
  // ">= 1 s" (fault_hold_s): ei ennen, ja viimeistään seuraavassa tickissä.
  atLeast(1.0f, w.settle, "rivi 7: vika ennen fault_hold_s:ää");
  atMost(1.0f + kTick + 1e-3f, w.settle, "rivi 7: vika viipyy");
  TEST_ASSERT_EQUAL_INT(1, w.entries);
  atMost(1.0f, w.last_verdict, "rivi 7: vanha tuomio näkyy");
  const StatusResult& r = b.st();
  TEST_ASSERT_EQUAL_INT(2, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 3 – anturivika", r.headline);
  TEST_ASSERT_TRUE_MESSAGE(startsWith(r.advice, "Jännite "), r.advice);
  TEST_ASSERT_TRUE_MESSAGE(endsWith(r.advice, " mV – tarkista anturin johdot tai vaihda"), r.advice);
  TEST_ASSERT_NOT_NULL(std::strstr(snap(b), "\"state\":\"fault_sensor\",\"fault_ch\":2,"));
  // Toinen anturi ylös (~3290 mV > fault_hi_mv 3150): kaksi vikaa -> yhteinen syöttö (R6).
  s = b.params();
  s.fault[1] = SimFault::SensorHigh;
  b.set(s);
  b.run(1.5f);
  TEST_ASSERT_EQUAL_INT(S(SyncState::FaultSensor), S(b.st().state));
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 ja SYL 3 – anturivika", b.st().headline);
  TEST_ASSERT_EQUAL_STRING("Useita anturivikoja – tarkista anturien käyttöjännite", b.st().advice);
  // Korjaus -> vihreä.
  s = b.params();
  s.fault[1] = s.fault[2] = SimFault::None;
  b.set(s);
  const Watch back = b.watch(7.0f, SyncState::Synced);
  report("rivi 7: anturit kunnossa -> vihreä", back.settle);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, back.settle);
}

// ---------------------------------------------------------------------------
// Rivi 8: kaikki letkut irti moottorin käydessä = rivi 1 (D10)
// ---------------------------------------------------------------------------

void test_row8_all_hoses_off_is_no_signal_and_rezeroes() {
  Bench b;
  SimParams s = withErr(0.0f, 0.0f, 0.0f);
  s.sensor_offset_kpa[1] = -1.0f;
  settleSynced(b, s);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, b.p.calibration().offset_kpa[1]);
  // Anturin nolla ryömii käytön aikana (lämpö); seuraava nollaus korjaa sen.
  s = b.params();
  s.sensor_offset_kpa[1] = -1.6f;
  for (int i = 0; i < kChannels; ++i) s.fault[i] = SimFault::HoseOff;
  b.set(s);
  const Watch w = b.watch(4.5f, SyncState::NoSignal);
  report("rivi 8: kaikki letkut irti -> ei signaalia", w.settle);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
  atMost(1.0f + 2.0f * kTick, w.settle, "rivi 8: viipyy yli pulse_timeout_s:n");
  // D10: ei letkuvikaa, kun mikään kanava ei pulssi.
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(w.seen & (bit(SyncState::FaultHose) | bit(SyncState::FaultMaster))));
  TEST_ASSERT_EQUAL_STRING("Ei alipainesignaalia", b.st().headline);
  TEST_ASSERT_EQUAL_STRING("Moottori sammuksissa tai letkut irti", b.st().advice);
  // Automaattinen nollaus on turvallinen myös tässä (anturi näkee ilmanpaineen).
  // Taso-EMA:n on ensin pysyttävä 0,3 kPa:n kaistassa koko off_hold_s:n ajan
  // (status.h: vielä vuotava alipaine ei saa päätyä offsetiksi). -55 kPa:sta
  // jäljellä oleva lasku on 0,3 kPa 2,6 s:n kohdalla, ja juuri sen yli alkanut
  // jakso voi katketa vielä EMA:n hännässä (kohinaraja ~0,003 kPa: tau *
  // ln(0,3 / 0,003) = 2,3 s) -> pahimmillaan 2,6 + 2,3 + off_hold_s 3 s.
  const float t_zero =
      4.5f + b.runUntil([&] { return std::fabs(b.p.calibration().offset_kpa[1] - 1.6f) < 0.05f; }, 6.0f);
  report("rivi 8: uusi automaattinen nollaus", t_zero);
  TEST_ASSERT_GREATER_THAN_FLOAT(4.5f, t_zero);
  atMost(2.6f + 2.3f + 3.0f + 2.0f * kTick, t_zero, "rivi 8: nollaus viipyy");
  TEST_ASSERT_EQUAL_STRING(Processor::kMsgAutoZero, b.p.calStatus().msg);
}

// ---------------------------------------------------------------------------
// Rivi 9: rpm > 3000 -> rpm näkyy, arvio piilossa
// ---------------------------------------------------------------------------

// Rivi 9 ei odota vakautta (piilotus on turvallinen suunta), joten viive =
// DSP:n lukitus (dsp.h: <= 3,5 sykliä + 37,5 ms) + rpm-hajonnan historia
// (rpm_hist / 2 = 8 jaksoa) + kaksi tickiä.
float row9Latency(float rpm) {
  const float period = 120.0f / rpm;
  return 3.5f * period + 0.0375f + 8.0f * period + 2.0f * kTick;
}

void test_row9_rpm_too_high_hides_verdict() {
  struct Case {
    float rpm, base, depth;
  };
  // 4000 rpm puolikaasulla ja 6000 rpm täydellä kaasulla (pieni pulssi,
  // dsp.h:n mitatut rajat: 6000 rpm vaatii ~4 kPa syvyyden). Moottori käy jo
  // käynnistyksessä, joten DSP lukitsee suoraan käyvään signaaliin.
  const Case cases[] = {{4000.0f, -30.0f, 14.0f}, {6000.0f, -8.0f, 6.0f}};
  for (const Case& c : cases) {
    Bench b;
    SimParams s;
    s.rpm = c.rpm;
    s.base_kpa = c.base;
    s.depth_kpa = c.depth;
    b.set(s);
    const Watch w = b.watch(3.0f, SyncState::RpmTooHigh);
    char m[64];
    std::snprintf(m, sizeof(m), "rivi 9: %.0f rpm -> säädä joutokäynnillä", static_cast<double>(c.rpm));
    report(m, w.settle);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
    atMost(row9Latency(c.rpm), w.settle, "rivi 9: viipyy");
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, w.last_verdict);  // ei koskaan tuomiota
    const StatusResult& r = b.st();
    TEST_ASSERT_FALSE(r.show_bars);
    TEST_ASSERT_EQUAL_STRING("Säädä joutokäynnillä", r.headline);
    TEST_ASSERT_TRUE_MESSAGE(startsWith(r.advice, "Kierrosluku "), r.advice);
    TEST_ASSERT_TRUE_MESSAGE(endsWith(r.advice, " rpm (raja 3000) – laske kaasua"), r.advice);
    // rpm näytetään: voimassa ja oikein.
    TEST_ASSERT_TRUE(b.p.rpm().valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f * c.rpm, c.rpm, b.p.rpm().rpm);
    TEST_ASSERT_FALSE(topRpmIsNull(snap(b)));
    TEST_ASSERT_NOT_NULL(std::strstr(g_snap, "\"state\":\"rpm_too_high\""));
  }
}

void test_row9_full_throttle_start_locks_within_dsp_spec() {
  // Sama rivi 9, mutta moottori käynnistyy ilmanpaineesta suoraan täydelle
  // kaasulle: keskitaso hyppää 0 -> -8 kPa, ja pulssi (6 kPa @ 50 Hz) on
  // suodatettuna vain ~2,3 kPa. Odotus on sama DSP:n lukitusaika kuin yllä.
  // TIEDETTY PUNAINEN: docs/jatko.md "WP-2 DSP (major)" 1 (syklintunnistus
  // jumittuu painemuutoksen jälkeen) ja 2 (korkeilla kierroksilla pieni
  // suodatettu pulssi): yksi kanava lukittuu vasta ~0,9 s:ssa.
  Bench b;
  SimParams s;
  s.rpm = 6000.0f;
  s.base_kpa = -8.0f;
  s.depth_kpa = 6.0f;
  zeroThenStart(b, s);
  const Watch w = b.watch(3.0f, SyncState::RpmTooHigh);
  report("rivi 9: käynnistys täydelle kaasulle -> säädä joutokäynnillä", w.settle);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
  atMost(row9Latency(s.rpm), w.settle, "rivi 9: lukitus painemuutoksen jälkeen viipyy");
}

// ---------------------------------------------------------------------------
// Rivi 10: signaali saturoituu -> varoitus (H1)
// ---------------------------------------------------------------------------

void test_row10_saturation_flag_on_clipped_peaks() {
  // Paluuaalto nostaa pulssin huipun ilmanpaineen yli, ja GN-anturi leikkaa
  // 2700 mV:iin (>= sat_mv 2680). Lippu on tilasta riippumaton.
  Bench b;
  SimParams s;
  s.rpm = 1500.0f;
  s.base_kpa = -10.0f;
  s.depth_kpa = 10.0f;
  s.peak_kpa = 15.0f;
  zeroThenStart(b, s);
  b.run(2.0f);
  int flagged = 0;
  int ticks = 0;
  const uint32_t n0 = b.n;
  while (b.n - n0 < 3000u) {
    if (!b.step()) continue;
    ++ticks;
    if (b.st().flag_saturation) ++flagged;
  }
  TEST_ASSERT_EQUAL_INT(ticks, flagged);
  TEST_ASSERT_NOT_NULL(std::strstr(snap(b), "\"flags\":{\"sat\":true,"));
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_TRUE(b.p.channel(i).saturated);
    TEST_ASSERT_TRUE(b.p.channel(i).has_pulse);
  }
}

void test_row10_no_saturation_flash_on_start_and_stop() {
  // Normaali joutokäynti (-55 kPa) ei leikkaudu koskaan. GN-anturi on
  // ilmanpaineessa leikkausrajallaan (2700 mV), joten käynnistys ja sammutus
  // eivät saa näyttää saturaatiovaroitusta (§4.7: lippu vain pulssin huipusta).
  // TIEDETTY PUNAINEN: docs/jatko.md "WP-4 status": saturaatiolippu välähtää
  // jokaisella sammutuksella ja käynnistyksellä (saturated ja has_pulse
  // pysyvät molemmat 1 s); sama välähdys tulee letkun irrotessa.
  Bench b;
  zeroThenStart(b, withErr(0.0f, 0.0f, 0.0f));
  const Watch start = b.watch(6.0f, SyncState::Synced);
  SimParams s = b.params();
  s.engine_on = false;
  b.set(s);
  const Watch stop = b.watch(3.0f, SyncState::NoSignal);
  TEST_ASSERT_FALSE_MESSAGE(start.sat, "saturaatiovaroitus käynnistyksessä");
  TEST_ASSERT_FALSE_MESSAGE(stop.sat, "saturaatiovaroitus sammutuksessa");
}

// ---------------------------------------------------------------------------
// Rivi 11: vahvistuskalibrointi puuttuu -> keltainen varoitus (D11)
// ---------------------------------------------------------------------------

void test_row11_missing_gain_is_warning_only() {
  Bench b;
  settleSynced(b, withErr(0.0f, 0.3f, -0.4f));
  // D11: nollaus riittää vihreään; puuttuva vahvistus on vain varoitus.
  TEST_ASSERT_EQUAL_INT(S(SyncState::Synced), S(b.st().state));
  TEST_ASSERT_TRUE(b.st().flag_uncal_gain);
  TEST_ASSERT_NOT_NULL(std::strstr(snap(b), "\"uncal_gain\":true"));
  // Ohjattu kalibrointi T-haaralla poistaa varoituksen.
  gainCalibrateOnTee(b, -50.0f);
  TEST_ASSERT_TRUE(b.p.takeCalDirty());  // NVS (§4.9)
  b.set(withErr(0.0f, 0.3f, -0.4f));
  const Watch w = b.watch(7.0f, SyncState::Synced);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
  TEST_ASSERT_FALSE(b.st().flag_uncal_gain);
  assertVerdict(b, 0.15f);
  // Käynnistys NVS:n vahvistuksella: ei varoitusta, mutta nollaus tarvitaan (D11).
  Bench c(g_mem2);
  c.p.setCalibration(b.p.calibration());
  c.set(withErr(0.0f, 0.3f, -0.4f));
  c.run(6.0f);
  TEST_ASSERT_FALSE(c.st().flag_uncal_gain);
  TEST_ASSERT_TRUE(c.st().flag_uncal_zero);
  TEST_ASSERT_EQUAL_INT(S(SyncState::Near), S(c.st().state));
}

// ---------------------------------------------------------------------------
// Rivi 12: master itse vialla -> "valitse toinen" (D7)
// ---------------------------------------------------------------------------

void test_row12_master_fault_then_switch_master() {
  Bench b;
  settleSynced(b, withErr(0.0f, 0.0f, 0.0f));
  SimParams s = b.params();
  s.fault[0] = SimFault::HoseOff;  // master = SYL 1 (D7 oletus)
  b.set(s);
  const Watch w = b.watch(5.0f, SyncState::FaultMaster);
  report("rivi 12: masterin letku irti -> VIKA", w.settle);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
  atMost(3.2f, w.settle, "rivi 12: master-vika viipyy");  // kuten rivi 6
  atMost(1.0f + 2.0f * kTick, w.last_verdict, "rivi 12: vanha tuomio näkyy");
  const StatusResult& r = b.st();
  TEST_ASSERT_EQUAL_INT(0, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("Master (SYL 1) ei kelpaa viitteeksi – valitse toinen", r.headline);
  TEST_ASSERT_EQUAL_STRING("SYL 1: ei alipainetta – tarkista letku", r.advice);
  // Erot masteriin ovat merkityksettömiä: ei palkkeja eikä lukuja.
  TEST_ASSERT_FALSE(r.show_bars);
  TEST_ASSERT_EQUAL_INT(-1, r.worst_ch);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_FLOAT(0.0f, r.vac_delta_kpa[i]);
  // Uusi master (D7): vika jää, mutta nyt tavallisena letkuvikana (rivi 6).
  // Masterin vaihto nollaa statuksen ajastimet, joten letkuvian pitoaika
  // (kHoseHoldS) alkaa alusta.
  TEST_ASSERT_TRUE(b.p.setMaster(1));
  b.run(sbx::StatusMachine::kHoseHoldS + kTick);
  TEST_ASSERT_EQUAL_INT(S(SyncState::FaultHose), S(b.st().state));
  TEST_ASSERT_EQUAL_INT(0, b.st().fault_ch);
  // Letku takaisin: tuomio uutta masteria vasten. SYL 3 imee 2 kPa vähemmän
  // kuin SYL 2 -> d = -2 -> "sulje ... kuin SYL 2".
  s = b.params();
  s.fault[0] = SimFault::None;
  s.sync_err_kpa[2] = 2.0f;
  b.set(s);
  const Watch near = b.watch(7.0f, SyncState::Near);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, near.settle);
  assertVerdict(b, 0.15f);
  TEST_ASSERT_TRUE_MESSAGE(endsWith(b.st().advice, " kuin SYL 2"), b.st().advice);
  // Uuden masterin anturivika -> taas rivi 12, anturin jännitteellä.
  s = b.params();
  s.fault[1] = SimFault::SensorLow;
  b.set(s);
  b.run(1.5f);
  TEST_ASSERT_EQUAL_INT(S(SyncState::FaultMaster), S(b.st().state));
  TEST_ASSERT_EQUAL_STRING("Master (SYL 2) ei kelpaa viitteeksi – valitse toinen", b.st().headline);
  TEST_ASSERT_TRUE_MESSAGE(startsWith(b.st().advice, "SYL 2: anturivika (jännite "), b.st().advice);
}

// ---------------------------------------------------------------------------
// D10: rivit 1 ja 8 ovat sama tila
// ---------------------------------------------------------------------------

void test_D10_engine_off_and_all_hoses_off_are_identical() {
  SimParams off;
  off.engine_on = false;
  SimParams hoses;
  for (int i = 0; i < kChannels; ++i) hoses.fault[i] = SimFault::HoseOff;
  SimParams* both[] = {&off, &hoses};
  for (SimParams* s : both) {
    s->sensor_offset_kpa[0] = -0.7f;
    s->sensor_offset_kpa[2] = -1.3f;
  }
  Bench a;
  Bench h(g_mem2);
  a.set(off);
  h.set(hoses);
  a.run(4.0f);
  h.run(4.0f);
  const StatusResult& ra = a.st();
  const StatusResult& rh = h.st();
  TEST_ASSERT_EQUAL_INT(S(SyncState::NoSignal), S(ra.state));
  TEST_ASSERT_EQUAL_INT(S(ra.state), S(rh.state));
  TEST_ASSERT_EQUAL_STRING(ra.headline, rh.headline);
  TEST_ASSERT_EQUAL_STRING(ra.advice, rh.advice);
  TEST_ASSERT_EQUAL_INT(ra.fault_ch, rh.fault_ch);
  TEST_ASSERT_EQUAL_INT(ra.show_bars, rh.show_bars);
  TEST_ASSERT_EQUAL_INT(ra.show_verdict, rh.show_verdict);
  // Nollaus on turvallinen molemmissa ja tuottaa saman tuloksen.
  TEST_ASSERT_TRUE(a.p.calibration().zero_valid);
  TEST_ASSERT_TRUE(h.p.calibration().zero_valid);
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -off.sensor_offset_kpa[i], a.p.calibration().offset_kpa[i]);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, a.p.calibration().offset_kpa[i], h.p.calibration().offset_kpa[i]);
  }
}

// ---------------------------------------------------------------------------
// D11: ilman tämän käynnistyksen nollausta ei koskaan vihreää
// ---------------------------------------------------------------------------

void test_D11_never_synced_without_zero() {
  // (a) Moottori käy jo käynnistyksessä: ilmanpainetta ei nähdä, nollausta ei tule.
  {
    Bench b;
    b.set(withErr(0.0f, 0.2f, -0.2f));  // totuus: synkronissa
    const Watch w = b.watch(20.0f, SyncState::Near);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(w.seen & bit(SyncState::Synced)));
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
    TEST_ASSERT_EQUAL_STRING("Lähellä – kalibroimaton", b.st().headline);
    TEST_ASSERT_EQUAL_STRING("Nollaus puuttuu – sammuta moottori hetkeksi, laite nollaa itse",
                             b.st().advice);
    TEST_ASSERT_TRUE(b.st().flag_uncal_zero);
    TEST_ASSERT_TRUE(b.st().show_verdict);  // enintään keltainen, ei piiloon
  }
  // (b) Automaattinen nollaus pois (S5a): sammutuskaan ei nollaa.
  {
    Bench b;
    Config c;
    c.status.auto_zero = false;
    TEST_ASSERT_TRUE(b.p.configure(c));
    SimParams s = withErr(0.0f, 0.2f, -0.2f);
    s.engine_on = false;
    b.set(s);
    b.run(5.0f);
    TEST_ASSERT_FALSE(b.p.calibration().zero_valid);
    s.engine_on = true;
    b.set(s);
    const Watch w = b.watch(10.0f, SyncState::Near);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(w.seen & bit(SyncState::Synced)));
    TEST_ASSERT_EQUAL_STRING("Nollaus puuttuu – irrota letkut ja tee nollaus", b.st().advice);
    // Käsin tehty nollaus ilmanpaineessa avaa vihreän. Odotetaan taso-EMA:n
    // asettuminen (4 s = 8 tau): vielä laskeva taso kirjautuisi offsetiksi
    // (processor.h AVOIN: manuaalinen nollaus ei tarkista vakautta).
    s = b.params();
    s.engine_on = false;
    b.set(s);
    b.run(4.0f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CalResult::Ok), static_cast<int>(b.p.zeroNow()));
    s.engine_on = true;
    b.set(s);
    const Watch g = b.watch(7.0f, SyncState::Synced);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, g.settle);
  }
  // (c) NVS:stä ladattu "nollattu" kalibrointi ei kelpaa (setCalibration).
  {
    Bench b;
    Calibration cal;
    cal.zero_valid = true;
    cal.gain_valid = true;
    b.p.setCalibration(cal);
    b.set(withErr(0.0f, 0.2f, -0.2f));
    const Watch w = b.watch(10.0f, SyncState::Near);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(w.seen & bit(SyncState::Synced)));
  }
}

// ---------------------------------------------------------------------------
// D12: arvio 500–3000 rpm, kun käynti on ollut vakaa >= 3 s
// ---------------------------------------------------------------------------

void test_D12_verdict_window_500_to_3000_rpm() {
  const float inside[] = {520.0f, 1000.0f, 2000.0f, 2900.0f};
  for (float rpm : inside) {
    Bench b;
    SimParams s = withErr(0.0f, 0.3f, -0.4f);
    s.rpm = rpm;
    zeroThenStart(b, s);
    const Watch w = b.watch(8.0f, SyncState::Synced);
    char m[64];
    std::snprintf(m, sizeof(m), "D12: %.0f rpm käynnistyksestä vihreään", static_cast<double>(rpm));
    report(m, w.settle);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
    atLeast(3.0f, w.settle, "D12: tuomio ennen vakausaikaa");  // vakaa >= 3 s
    assertVerdict(b, 0.15f);
  }
  {
    // Alle 500 rpm käynti on sammumassa -> "odota" (rivi 2 / D12).
    Bench b;
    SimParams s = withErr(0.0f, 0.0f, 0.0f);
    s.rpm = 450.0f;
    zeroThenStart(b, s);
    const Watch w = b.watch(8.0f, SyncState::Unstable);
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, w.last_verdict);
    TEST_ASSERT_TRUE_MESSAGE(startsWith(b.st().advice, "Kierrosluku liian matala: "), b.st().advice);
    TEST_ASSERT_TRUE_MESSAGE(endsWith(b.st().advice, " rpm (raja 500)"), b.st().advice);
  }
  {
    // Yli 3000 rpm -> rivi 9.
    Bench b;
    SimParams s = withErr(0.0f, 0.0f, 0.0f);
    s.rpm = 3100.0f;
    zeroThenStart(b, s);
    const Watch w = b.watch(5.0f, SyncState::RpmTooHigh);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, w.last_verdict);
  }
}

// ---------------------------------------------------------------------------
// R4: anturien offset- ja gain-virheet -> kalibroinnin jälkeen spread = totuus
// ---------------------------------------------------------------------------

namespace {

// Anturivirheet datasheetin ±2 kPa / muutama % -alueella. Offsetit
// negatiivisia: GN-anturi ei näytä ilmanpainetta korkeampaa (H1, ks. alla).
SimParams sensorErrors(SimParams s) {
  s.sensor_offset_kpa[0] = -2.0f;
  s.sensor_offset_kpa[1] = -1.0f;
  s.sensor_offset_kpa[2] = -0.5f;
  s.sensor_gain_err[0] = 0.02f;
  s.sensor_gain_err[1] = -0.015f;
  s.sensor_gain_err[2] = 0.01f;
  return s;
}

}  // namespace

void test_R4_calibration_removes_sensor_errors() {
  const SimParams engine = sensorErrors(withErr(0.0f, 1.0f, -0.7f));  // totuus: spread 1,7 -> Near
  // Ilman kalibrointia (auto_zero pois) virhe on R4:n mukaan suurempi kuin D2:n raja.
  float uncal_err = 0.0f;
  {
    Bench u;
    Config c;
    c.status.auto_zero = false;
    TEST_ASSERT_TRUE(u.p.configure(c));
    u.set(engine);
    u.run(8.0f);
    const Truth t = u.truth();
    for (int i = 0; i < kChannels; ++i) {
      const float e = std::fabs(u.st().vac_delta_kpa[i] - t.d[i]);
      if (e > uncal_err) uncal_err = e;
    }
    TEST_ASSERT_GREATER_THAN_FLOAT(1.5f, uncal_err);
  }
  // Nollaus (rivi 1) + vahvistus T-haaralla -50 kPa:ssa + moottori.
  Bench b;
  SimParams off = engine;
  off.engine_on = false;
  b.set(off);
  b.run(4.0f);
  TEST_ASSERT_TRUE(b.p.calibration().zero_valid);
  gainCalibrateOnTee(b, -50.0f);
  b.set(engine);
  const Watch w = b.watch(8.0f, SyncState::Near);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, w.settle);
  assertVerdict(b, 0.15f);
  const Truth t = b.truth();
  float cal_err = 0.0f;
  for (int i = 0; i < kChannels; ++i) {
    const float e = std::fabs(b.st().vac_delta_kpa[i] - t.d[i]);
    if (e > cal_err) cal_err = e;
  }
  char m[96];
  std::snprintf(m, sizeof(m), "R4: Δ-virhe kalibroimatta %.2f kPa, kalibroituna %.3f kPa",
                static_cast<double>(uncal_err), static_cast<double>(cal_err));
  TEST_MESSAGE(m);
  TEST_ASSERT_LESS_THAN_FLOAT(0.1f, cal_err);
}

// ---------------------------------------------------------------------------
// §11: penkkitesti - kolme anturia samassa alipaineessa -> spread < 0,3 kPa
// ---------------------------------------------------------------------------

void test_bench_three_sensors_same_vacuum_spread_below_0_3() {
  Bench b;
  SimParams off = sensorErrors(SimParams());
  off.engine_on = false;
  b.set(off);
  b.run(4.0f);  // nollaus
  gainCalibrateOnTee(b, -50.0f);
  // T-haara eri alipaineissa, myös kalibrointipisteen ulkopuolella.
  const float levels[] = {-20.0f, -40.0f, -60.0f, -80.0f};
  float worst = 0.0f;
  for (float kpa : levels) {
    SimParams s = b.params();
    s.base_kpa = kpa;
    b.set(s);
    b.run(3.0f);  // taso-EMA (tau 0,5 s) asettuu
    float lo = 1e9f;
    float hi = -1e9f;
    for (int i = 0; i < kChannels; ++i) {
      const float v = b.p.channel(i).level_kpa;
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
      TEST_ASSERT_FLOAT_WITHIN(2.0f, kpa, v);  // yhteinen skaala datasheetin varassa (R4)
    }
    if (hi - lo > worst) worst = hi - lo;
  }
  // Sama moottorilla: identtinen signaali kaikissa kanavissa -> tilan spread.
  b.set(sensorErrors(withErr(0.0f, 0.0f, 0.0f)));
  b.run(8.0f);
  TEST_ASSERT_EQUAL_INT(S(SyncState::Synced), S(b.st().state));
  char m[112];
  std::snprintf(m, sizeof(m), "§11: T-haaran suurin kanavaero %.3f kPa, moottorilla spread %.3f kPa",
                static_cast<double>(worst), static_cast<double>(b.st().spread_kpa));
  TEST_MESSAGE(m);
  TEST_ASSERT_LESS_THAN_FLOAT(0.3f, worst);
  TEST_ASSERT_LESS_THAN_FLOAT(0.3f, b.st().spread_kpa);
}

// ---------------------------------------------------------------------------
// H1/R4: positiivinen anturin offset
// ---------------------------------------------------------------------------

void test_H1_positive_offset_invisible_at_atmosphere() {
  // AVOIN H1/R4: dokumentoi simulaattorin oletuksen, ei päätetty käytös.
  // Simulaattori leikkaa GN-anturin ulostulon nimelliseen nollaansa 2700 mV:iin
  // (engine_sim.h, sensor_hi_mv_). Positiivinen offset (anturi näyttää +2 kPa
  // liikaa) ei siksi näy ilmanpaineessa, nollaus ei voi poistaa sitä, ja virhe
  // jää käyvään moottoriin. Oikean anturin käytös ilmanpaineessa (leikkaako vai
  // antaako > 2700 mV) ratkeaa vasta raudalla (H1). Tämä testi ei lukitse
  // tuomiota: se tarkistaa vain mekanismin ja raportoi tilan.
  Bench b;
  SimParams s = withErr(0.0f, 2.5f, 2.5f);  // totuus: spread 2,5 kPa -> Near
  s.sensor_offset_kpa[0] = 2.0f;
  zeroThenStart(b, s);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, b.p.calibration().offset_kpa[0]);  // offset ei näkynyt
  b.run(8.0f);
  const Truth t = b.truth();
  const float residual = b.p.channel(0).mean_kpa - b.sim.trueMeanKpa(0);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 2.0f, residual);  // virhe jäi lukemaan
  char m[200];
  std::snprintf(m, sizeof(m),
                "H1: totuus %s (spread %.2f), laite näyttää %s (spread %.2f, \"%s\")",
                sbx::syncStateKey(t.state), static_cast<double>(t.spread),
                sbx::syncStateKey(b.st().state), static_cast<double>(b.st().spread_kpa),
                b.st().headline);
  TEST_MESSAGE(m);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_row1_engine_off_no_signal_and_auto_zero);
  RUN_TEST(test_row2_engine_start_waits_for_stable_run);
  RUN_TEST(test_row2_rough_running_hides_bars);
  RUN_TEST(test_row3_idle_synced);
  RUN_TEST(test_row4a_near_names_worst_channel_and_direction);
  RUN_TEST(test_row4b_out_of_sync_names_worst_channel_and_direction);
  RUN_TEST(test_row5_rpm_drift_keeps_deltas_visible);
  RUN_TEST(test_row5_common_mode_step_no_wrong_advice);
  RUN_TEST(test_row6_hose_off_shows_hose_fault);
  RUN_TEST(test_row6_start_and_stop_never_flash_hose_fault);
  RUN_TEST(test_row7_sensor_fault_after_hold_time);
  RUN_TEST(test_row8_all_hoses_off_is_no_signal_and_rezeroes);
  RUN_TEST(test_row9_rpm_too_high_hides_verdict);
  RUN_TEST(test_row9_full_throttle_start_locks_within_dsp_spec);
  RUN_TEST(test_row10_saturation_flag_on_clipped_peaks);
  RUN_TEST(test_row10_no_saturation_flash_on_start_and_stop);
  RUN_TEST(test_row11_missing_gain_is_warning_only);
  RUN_TEST(test_row12_master_fault_then_switch_master);
  RUN_TEST(test_D10_engine_off_and_all_hoses_off_are_identical);
  RUN_TEST(test_D11_never_synced_without_zero);
  RUN_TEST(test_D12_verdict_window_500_to_3000_rpm);
  RUN_TEST(test_R4_calibration_removes_sensor_errors);
  RUN_TEST(test_bench_three_sensors_same_vacuum_spread_below_0_3);
  RUN_TEST(test_H1_positive_offset_invisible_at_atmosphere);
  return UNITY_END();
}
