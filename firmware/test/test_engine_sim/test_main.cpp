// WP-1: moottorisimulaattorin testit. Totuusarvot johdetaan fysiikasta ja
// mallin määritelmästä (docs/firmware.md §4.1), ei simulaattorin omasta
// ulostulosta: syklin keskiarvo = base + sync_err, jakso = 120/rpm, sylinterit
// 1/3 syklin välein, anturin siirtofunktio P = (mV - 2700) / 25.

#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include "core/engine_sim.h"

using sbx::EngineSim;
using sbx::kChannels;
using sbx::RawFrame;
using sbx::SensorParams;
using sbx::SimFault;
using sbx::SimParams;

void setUp() {}
void tearDown() {}

namespace {

constexpr float kFs = 1000.0f;

float periodSamples(float rpm) { return 120.0f * kFs / rpm; }

float toKpa(uint16_t mv, const SensorParams& sp) {
  return (static_cast<float>(mv) - sp.v_zero_mv) / sp.mv_per_kpa;
}

struct Rec {
  std::vector<uint16_t> mv[kChannels];
  std::vector<float> kpa[kChannels];
  std::vector<uint32_t> seq;
};

void record(EngineSim& sim, int n, const SensorParams& sp, Rec& r) {
  for (int i = 0; i < n; ++i) {
    const RawFrame f = sim.next();
    r.seq.push_back(f.seq);
    for (int c = 0; c < kChannels; ++c) {
      r.mv[c].push_back(f.mv[c]);
      r.kpa[c].push_back(toKpa(f.mv[c], sp));
    }
  }
}

Rec run(const SimParams& p, int n, const SensorParams& sp = SensorParams()) {
  EngineSim sim;
  sim.configure(p, sp);
  Rec r;
  record(sim, n, sp, r);
  return r;
}

// Kahan-summa: 50 000 näytettä noin -55 kPa:ssa ylittäisi pelkän floatin tarkkuuden.
float meanOf(const std::vector<float>& x, size_t a, size_t b) {
  float sum = 0.0f, comp = 0.0f;
  for (size_t i = a; i < b; ++i) {
    const float y = x[i] - comp;
    const float t = sum + y;
    comp = (t - sum) - y;
    sum = t;
  }
  return sum / static_cast<float>(b - a);
}

float meanMv(const std::vector<uint16_t>& x) {
  std::vector<float> f(x.begin(), x.end());
  return meanOf(f, 0, f.size());
}

float sdMv(const std::vector<uint16_t>& x) {
  const float m = meanMv(x);
  float acc = 0.0f;
  for (uint16_t v : x) acc += (static_cast<float>(v) - m) * (static_cast<float>(v) - m);
  return std::sqrt(acc / static_cast<float>(x.size()));
}

// Keskitetty (nollavaiheinen) liukuva keskiarvo: vaimentaa resonanssin ja kohinan
// syklintunnistusta varten siirtämättä ajoitusta.
std::vector<float> smooth(const std::vector<float>& x, int len) {
  const int h = len / 2;
  const int n = static_cast<int>(x.size());
  std::vector<float> y(x.size());
  for (int i = 0; i < n; ++i) {
    float s = 0.0f;
    int cnt = 0;
    for (int k = i - h; k <= i + h; ++k) {
      if (k >= 0 && k < n) {
        s += x[k];
        ++cnt;
      }
    }
    y[i] = s / static_cast<float>(cnt);
  }
  return y;
}

int smoothLenFor(float rpm) {
  int len = static_cast<int>(periodSamples(rpm) / 8.0f);
  if (len > 9) len = 9;  // 9 näytteen keskiarvon nollakohta 111 Hz ≈ resonanssi
  if (len < 1) len = 1;
  return len | 1;
}

// Imupulssin laskevan reunan ajanhetket (näytteinä, lineaarinen interpolointi).
// Taso = puoliväli min..max, uudelleenviritys 75 %:ssa, jotta resonanssi ja
// ristivuodon pienemmät kuopat eivät tuota ylimääräisiä reunoja.
std::vector<float> cycleStarts(const std::vector<float>& x, int len) {
  const std::vector<float> y = smooth(x, len);
  const int n = static_cast<int>(y.size());
  float lo = std::numeric_limits<float>::max();
  float hi = -std::numeric_limits<float>::max();
  for (int i = len; i < n - len; ++i) {
    if (y[i] < lo) lo = y[i];
    if (y[i] > hi) hi = y[i];
  }
  const float mid = lo + 0.5f * (hi - lo);
  const float arm = lo + 0.75f * (hi - lo);
  std::vector<float> t;
  bool armed = false;
  for (int i = len + 1; i < n - len; ++i) {
    if (y[i] > arm) armed = true;
    if (armed && y[i - 1] > mid && y[i] <= mid) {
      t.push_back(static_cast<float>(i - 1) + (y[i - 1] - mid) / (y[i - 1] - y[i]));
      armed = false;
    }
  }
  return t;
}

void periodStats(const std::vector<float>& t, float& mean, float& sd) {
  std::vector<float> d;
  for (size_t i = 1; i < t.size(); ++i) d.push_back(t[i] - t[i - 1]);
  mean = meanOf(d, 0, d.size());
  float acc = 0.0f;
  for (float v : d) acc += (v - mean) * (v - mean);
  sd = std::sqrt(acc / static_cast<float>(d.size() - 1));
}

// Goertzel-teho taajuudella f (Hz).
float goertzelPower(const std::vector<float>& x, float f) {
  const float w = 2.0f * 3.14159265f * f / kFs;
  const float k = 2.0f * std::cos(w);
  float s1 = 0.0f, s2 = 0.0f;
  for (float v : x) {
    const float s0 = v + k * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return s1 * s1 + s2 * s2 - k * s1 * s2;
}

SimParams quiet() {
  // Puhdas signaali: ei kohinaa, resonanssia eikä jitteriä.
  SimParams p;
  p.noise_mv = 0.0f;
  p.res_kpa = 0.0f;
  p.rpm_jitter = 0.0f;
  return p;
}

bool sameFrame(const RawFrame& a, const RawFrame& b) {
  return a.seq == b.seq && a.rail_mv == b.rail_mv && a.mv[0] == b.mv[0] && a.mv[1] == b.mv[1] &&
         a.mv[2] == b.mv[2];
}

}  // namespace

// ---------------------------------------------------------------------------
// Determinismi ja seq
// ---------------------------------------------------------------------------

void test_same_seed_gives_identical_stream() {
  SimParams p;
  p.seed = 7;
  p.rpm_jitter = 0.05f;
  p.xtalk = 0.2f;
  EngineSim a, b;
  a.configure(p, SensorParams());
  b.configure(p, SensorParams());
  for (int i = 0; i < 20000; ++i) {
    if (!sameFrame(a.next(), b.next())) TEST_FAIL_MESSAGE("sama seed tuotti eri näytteen");
  }
}

void test_different_seed_differs() {
  SimParams p;
  p.seed = 7;
  SimParams q = p;
  q.seed = 8;
  EngineSim a, b;
  a.configure(p, SensorParams());
  b.configure(q, SensorParams());
  int diff = 0;
  for (int i = 0; i < 2000; ++i) {
    const RawFrame fa = a.next();
    const RawFrame fb = b.next();
    for (int c = 0; c < kChannels; ++c) diff += fa.mv[c] != fb.mv[c] ? 1 : 0;
  }
  // 2 mV:n kohina pyöristettynä 1 mV:iin: valtaosa näytteistä eroaa.
  TEST_ASSERT_GREATER_THAN(3000, diff);
}

void test_reset_replays_stream_and_fresh_configure_equals_reset() {
  SimParams p;
  p.seed = 42;
  p.rpm_jitter = 0.05f;
  EngineSim a;
  a.configure(p, SensorParams());  // tuore: seed 42 voimaan heti
  std::vector<RawFrame> first;
  for (int i = 0; i < 3000; ++i) first.push_back(a.next());
  a.reset();
  for (int i = 0; i < 3000; ++i) {
    if (!sameFrame(first[i], a.next())) TEST_FAIL_MESSAGE("reset() ei toistanut virtaa");
  }
  EngineSim b;
  b.configure(p, SensorParams());
  b.reset();
  for (int i = 0; i < 3000; ++i) {
    if (!sameFrame(first[i], b.next())) TEST_FAIL_MESSAGE("configure+reset != tuore configure");
  }
}

void test_seq_increments_by_one() {
  EngineSim sim;
  for (uint32_t i = 0; i < 1000; ++i) TEST_ASSERT_EQUAL_UINT32(i, sim.next().seq);
  SimParams p;
  p.rpm = 1200.0f;
  sim.configure(p, SensorParams());  // ei-tuore configure ei nollaa seq:ia
  TEST_ASSERT_EQUAL_UINT32(1000, sim.next().seq);
  TEST_ASSERT_EQUAL_UINT32(1001, sim.next().seq);
  sim.reset();
  TEST_ASSERT_EQUAL_UINT32(0, sim.next().seq);
  TEST_ASSERT_EQUAL_UINT16(0, sim.next().rail_mv);  // kiskoa ei mallinneta (R6): 0 = ei mitattu
}

// ---------------------------------------------------------------------------
// D1-totuus: syklin keskiarvo
// ---------------------------------------------------------------------------

void test_cycle_mean_equals_truth_with_noise_and_resonance() {
  const float rpms[] = {600.0f, 850.0f, 3000.0f};
  for (float rpm : rpms) {
    SimParams p;
    p.rpm = rpm;
    p.noise_mv = 2.0f;
    p.res_kpa = 3.0f;
    p.res_hz[0] = 95.0f;
    p.res_hz[1] = 112.0f;
    p.res_hz[2] = 140.0f;
    p.rpm_jitter = 0.01f;
    p.xtalk = 0.1f;
    p.sync_err_kpa[1] = 1.2f;
    p.sync_err_kpa[2] = -2.0f;
    p.seed = 11;
    EngineSim sim;
    SensorParams sp;
    sim.configure(p, sp);
    Rec r;
    record(sim, 20000, sp, r);
    for (int c = 0; c < kChannels; ++c) {
      const float truth = p.base_kpa + p.sync_err_kpa[c];
      TEST_ASSERT_EQUAL_FLOAT(truth, sim.trueMeanKpa(c));
      const std::vector<float> t = cycleStarts(r.kpa[c], smoothLenFor(rpm));
      TEST_ASSERT_GREATER_THAN(90, static_cast<int>(t.size()));
      // Kokonainen määrä syklejä samasta vaiheesta samaan vaiheeseen.
      const size_t a = static_cast<size_t>(std::ceil(t.front()));
      const size_t b = static_cast<size_t>(std::ceil(t.back()));
      char msg[96];
      std::snprintf(msg, sizeof(msg), "rpm %.0f kanava %d", static_cast<double>(rpm), c);
      TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.05f, truth, meanOf(r.kpa[c], a, b), msg);
      // DSP:n tapaan 8 syklin lohkot (avg_cycles): jitter ja reunakvantisointi
      // saavat näkyä, mutta keskiarvo pysyy totuudessa.
      for (size_t k = 0; k + 8 < t.size(); k += 8) {
        const size_t ka = static_cast<size_t>(std::ceil(t[k]));
        const size_t kb = static_cast<size_t>(std::ceil(t[k + 8]));
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, truth, meanOf(r.kpa[c], ka, kb), msg);
      }
    }
  }
}

void test_sensor_gain_and_offset_error_applied() {
  SimParams p;
  p.sensor_gain_err[0] = 0.02f;
  p.sensor_gain_err[2] = -0.03f;
  p.sensor_offset_kpa[0] = -0.8f;
  p.sensor_offset_kpa[1] = 0.6f;
  p.seed = 5;
  const SensorParams sp;
  const Rec r = run(p, 20000, sp);
  for (int c = 0; c < kChannels; ++c) {
    // R4: anturi näkee (1 + g)·P + offset, joten nimellinen keskiarvo siirtyy.
    const float expected = (1.0f + p.sensor_gain_err[c]) * p.base_kpa + p.sensor_offset_kpa[c];
    const std::vector<float> t = cycleStarts(r.kpa[c], smoothLenFor(p.rpm));
    const size_t a = static_cast<size_t>(std::ceil(t.front()));
    const size_t b = static_cast<size_t>(std::ceil(t.back()));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, expected, meanOf(r.kpa[c], a, b));
  }
  // Ilmanpaineessa offset näkyy suoraan (automaattisen nollauksen peruste, rivi 1)...
  p.engine_on = false;
  p.noise_mv = 0.0f;
  const Rec off = run(p, 100, sp);
  TEST_ASSERT_EQUAL_UINT16(2680, off.mv[0][50]);  // 2700 + 25·(-0.8)
  // ...paitsi positiivinen offset, joka GN-anturilla leikkautuu 2700 mV:iin (D4/H1).
  TEST_ASSERT_EQUAL_UINT16(2700, off.mv[1][50]);
  TEST_ASSERT_EQUAL_UINT16(2700, off.mv[2][50]);
}

// ---------------------------------------------------------------------------
// Ajoitus: jakso, sylinterien vaihe, jitter, drift
// ---------------------------------------------------------------------------

void test_period_is_120_over_rpm() {
  const float rpms[] = {600.0f, 850.0f, 3000.0f, 6000.0f};
  for (float rpm : rpms) {
    SimParams p;
    p.rpm = rpm;
    p.rpm_jitter = 0.0f;
    p.noise_mv = 2.0f;
    p.res_kpa = 3.0f;
    const Rec r = run(p, 20000);
    const std::vector<float> t = cycleStarts(r.kpa[0], smoothLenFor(rpm));
    const float expected = periodSamples(rpm);
    const float measured = (t.back() - t.front()) / static_cast<float>(t.size() - 1);
    char msg[64];
    std::snprintf(msg, sizeof(msg), "rpm %.0f", static_cast<double>(rpm));
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, static_cast<int>(20000.0f / expected),
                                   static_cast<int>(t.size()), msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(expected * 0.001f, expected, measured, msg);
  }
  // Oletusjitterillä (1 %) keskimääräinen jakso on yhä 120/rpm: lognormaalin
  // kertoimen odotusarvo on 1.
  SimParams p;
  p.seed = 3;
  const Rec r = run(p, 60000);
  const std::vector<float> t = cycleStarts(r.kpa[0], smoothLenFor(p.rpm));
  const float measured = (t.back() - t.front()) / static_cast<float>(t.size() - 1);
  TEST_ASSERT_FLOAT_WITHIN(periodSamples(p.rpm) * 0.005f, periodSamples(p.rpm), measured);
}

void test_cylinders_are_one_third_cycle_apart() {
  SimParams p;
  p.rpm_jitter = 0.0f;
  p.res_kpa = 0.0f;
  p.sync_err_kpa[1] = 2.0f;  // eri tasot eivät saa siirtää reunaa
  p.sync_err_kpa[2] = -3.0f;
  const Rec r = run(p, 10000);
  const float T = periodSamples(p.rpm);
  const std::vector<float> t0 = cycleStarts(r.kpa[0], 9);
  const std::vector<float> t1 = cycleStarts(r.kpa[1], 9);
  const std::vector<float> t2 = cycleStarts(r.kpa[2], 9);
  int checked = 0;
  for (float a : t0) {
    float b = -1.0f, c = -1.0f;
    for (float v : t1) {
      if (v > a) {
        b = v;
        break;
      }
    }
    for (float v : t2) {
      if (v > a) {
        c = v;
        break;
      }
    }
    if (b < 0.0f || c < 0.0f) continue;
    TEST_ASSERT_FLOAT_WITHIN(0.3f, T / 3.0f, b - a);         // SYL 2: 240° myöhemmin
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 2.0f * T / 3.0f, c - a);  // SYL 3: 480° myöhemmin
    ++checked;
  }
  TEST_ASSERT_GREATER_THAN(60, checked);
}

void test_jitter_sets_period_sd_on_every_channel() {
  SimParams p;
  p.rpm_jitter = 0.03f;
  p.noise_mv = 0.5f;
  p.res_kpa = 0.0f;
  p.seed = 9;
  const Rec r = run(p, 60000);
  // Imun alusta imun alkuun jakso on kolmen 240°:n välin summa, ja sen hajonta on
  // tasan rpm_jitter. Reunatunnistin laukeaa kuitenkin kuopan sisällä (taso puolivälissä:
  // sin² = 1/2 -> psi* = w/4), ja kuoppa venyy oman välinsä mukana. Mitattu jakso on
  // T·[(1/3 - psi*)·f_a + f_b/3 + f_c/3 + psi*·f_d], joten sen hajonta on
  // jitter·√(3·((1/3 - psi*)² + 2/9 + psi*²)) ≈ 0.948·jitter. DSP näkee saman.
  const float psi = p.intake_frac / 4.0f;
  const float third = 1.0f / 3.0f;
  const float seen = p.rpm_jitter *
                     std::sqrt(3.0f * ((third - psi) * (third - psi) + 2.0f / 9.0f + psi * psi));
  for (int c = 0; c < kChannels; ++c) {
    const std::vector<float> t = cycleStarts(r.kpa[c], 9);
    TEST_ASSERT_GREATER_THAN(400, static_cast<int>(t.size()));
    float mean = 0.0f, sd = 0.0f;
    periodStats(t, mean, sd);
    // ~420 jaksoa -> hajontaestimaatin suhteellinen virhe ~3.5 %; raja ~14 %.
    TEST_ASSERT_FLOAT_WITHIN(0.004f, seen, sd / mean);
    TEST_ASSERT_FLOAT_WITHIN(periodSamples(p.rpm) * 0.01f, periodSamples(p.rpm), mean);
  }
  // Imun alku suoraan: resonanssi herätetään tasan imun alussa (1 ms:n tarkkuudella),
  // joten resonanssin erotussignaalin herätehetket antavat todellisen jakson.
  SimParams pr = p;
  pr.res_kpa = 3.0f;
  const Rec rr = run(pr, 60000);
  for (int c = 0; c < kChannels; ++c) {
    std::vector<float> exc;
    for (size_t i = 1; i < rr.kpa[c].size(); ++i) {
      const float d0 = rr.kpa[c][i - 1] - r.kpa[c][i - 1];
      const float d1 = rr.kpa[c][i] - r.kpa[c][i];
      if (std::fabs(d0) < 0.1f && std::fabs(d1) > 1.0f) exc.push_back(static_cast<float>(i));
    }
    TEST_ASSERT_GREATER_THAN(400, static_cast<int>(exc.size()));
    float mean = 0.0f, sd = 0.0f;
    periodStats(exc, mean, sd);
    TEST_ASSERT_FLOAT_WITHIN(0.004f, p.rpm_jitter, sd / mean);  // kaikilla kanavilla sama
  }
  // Ilman jitteriä jaksot ovat vakioita (mittausepätarkkuus vain).
  p.rpm_jitter = 0.0f;
  const Rec q = run(p, 20000);
  for (int c = 0; c < kChannels; ++c) {
    float mean = 0.0f, sd = 0.0f;
    periodStats(cycleStarts(q.kpa[c], 9), mean, sd);
    TEST_ASSERT_LESS_THAN_FLOAT(0.002f, sd / mean);
  }
}

void test_drift_changes_true_rpm_and_period() {
  SimParams p = quiet();
  p.rpm = 850.0f;
  p.rpm_drift = 100.0f;  // rpm/s (rivi 5)
  EngineSim sim;
  const SensorParams sp;
  sim.configure(p, sp);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 850.0f, sim.trueRpm());
  Rec r;
  record(sim, 3000, sp, r);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1150.0f, sim.trueRpm());  // 850 + 100 rpm/s · 3 s
  // Mitattu hetkellinen rpm seuraa nimellistä: jakson keskikohdan rpm = 850 + 0.1·t[ms].
  const std::vector<float> t = cycleStarts(r.kpa[0], 9);
  TEST_ASSERT_GREATER_THAN(20, static_cast<int>(t.size()));
  for (size_t k = 1; k < t.size(); ++k) {
    const float mid = 0.5f * (t[k] + t[k - 1]);
    const float expected = 850.0f + 0.1f * mid;
    const float measured = 120.0f * kFs / (t[k] - t[k - 1]);
    TEST_ASSERT_FLOAT_WITHIN(expected * 0.01f, expected, measured);
  }
  // Pelkän driftin muutos jatkaa nykyisestä kierrosluvusta (ei hyppyä 850:een).
  p.rpm_drift = 0.0f;
  sim.configure(p, sp);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1150.0f, sim.trueRpm());
  for (int i = 0; i < 1000; ++i) sim.next();
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1150.0f, sim.trueRpm());
  // Uusi asetusarvo tulee voimaan heti.
  p.rpm = 900.0f;
  sim.configure(p, sp);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 900.0f, sim.trueRpm());
  // Negatiivinen drift ei vie rpm:ää nollaan tai negatiiviseksi.
  p.rpm = 300.0f;
  p.rpm_drift = -1000.0f;
  sim.configure(p, sp);
  for (int i = 0; i < 2000; ++i) sim.next();
  TEST_ASSERT_EQUAL_FLOAT(EngineSim::kRpmMin, sim.trueRpm());
}

// ---------------------------------------------------------------------------
// Moottori seis ja vikatilat (rivit 1, 6, 7)
// ---------------------------------------------------------------------------

void test_engine_off_reads_atmosphere() {
  SimParams p;
  p.engine_on = false;
  EngineSim sim;
  const SensorParams sp;
  sim.configure(p, sp);
  Rec r;
  record(sim, 5000, sp, r);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sim.trueRpm());
  for (int c = 0; c < kChannels; ++c) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sim.trueMeanKpa(c));
    // 0 kPa gauge = 2700 mV; kohina symmetrinen anturin leikkauksen jälkeen.
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 2700.0f, meanMv(r.mv[c]));
    const float sd = sdMv(r.mv[c]);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 2.0f, sd);  // pelkkä ADC-kohina, ei pulsseja
    for (uint16_t v : r.mv[c]) TEST_ASSERT_INT_WITHIN(7, 2700, v);  // Irwin-Hall ±3.46σ
  }
  p.noise_mv = 0.0f;
  const Rec q = run(p, 500);
  for (int c = 0; c < kChannels; ++c) {
    for (uint16_t v : q.mv[c]) TEST_ASSERT_EQUAL_UINT16(2700, v);
  }
}

void test_engine_restart_resumes_without_reset() {
  SimParams p = quiet();
  EngineSim sim;
  const SensorParams sp;
  sim.configure(p, sp);
  for (int i = 0; i < 1000; ++i) sim.next();
  p.engine_on = false;
  sim.configure(p, sp);
  const RawFrame off = sim.next();
  TEST_ASSERT_EQUAL_UINT16(2700, off.mv[0]);
  p.engine_on = true;
  sim.configure(p, sp);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 850.0f, sim.trueRpm());
  Rec r;
  record(sim, 3000, sp, r);
  float mean = 0.0f, sd = 0.0f;
  periodStats(cycleStarts(r.kpa[0], 9), mean, sd);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, periodSamples(850.0f), mean);
}

void test_hose_off_channel_sees_atmosphere_others_pulse() {
  SimParams p;
  p.fault[1] = SimFault::HoseOff;
  EngineSim sim;
  const SensorParams sp;
  sim.configure(p, sp);
  Rec r;
  record(sim, 5000, sp, r);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sim.trueMeanKpa(1));
  TEST_ASSERT_EQUAL_FLOAT(p.base_kpa, sim.trueMeanKpa(0));
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 2700.0f, meanMv(r.mv[1]));
  for (uint16_t v : r.mv[1]) TEST_ASSERT_INT_WITHIN(7, 2700, v);
  for (int c = 0; c < kChannels; c += 2) {
    uint16_t lo = 0xFFFF, hi = 0;
    for (uint16_t v : r.mv[c]) {
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
    }
    // Pulssi 20 kPa · 25 mV/kPa: huippu-huippu selvästi yli 15 kPa.
    TEST_ASSERT_GREATER_THAN(15 * 25, hi - lo);
  }
}

void test_sensor_low_and_high_faults() {
  const float noises[] = {2.0f, 30.0f};
  for (float noise : noises) {
    SimParams p;
    p.noise_mv = noise;
    p.fault[0] = SimFault::SensorLow;
    p.fault[2] = SimFault::SensorHigh;
    const SensorParams sp;
    EngineSim sim;
    sim.configure(p, sp);
    Rec r;
    record(sim, 3000, sp, r);
    for (uint16_t v : r.mv[0]) {
      TEST_ASSERT_LESS_OR_EQUAL(30, v);
      TEST_ASSERT_LESS_THAN(sp.fault_lo_mv, v);  // rivi 7 laukeaa
    }
    for (uint16_t v : r.mv[2]) {
      TEST_ASSERT_GREATER_OR_EQUAL(3270, v);
      TEST_ASSERT_LESS_OR_EQUAL(3300, v);
      TEST_ASSERT_GREATER_THAN(sp.fault_hi_mv, v);
    }
    // Paine on yhä olemassa, anturi vain ei näe sitä.
    TEST_ASSERT_EQUAL_FLOAT(p.base_kpa, sim.trueMeanKpa(0));
    // Ehjä kanava 1 pulssii normaalisti.
    uint16_t lo = 0xFFFF, hi = 0;
    for (uint16_t v : r.mv[1]) {
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
    }
    TEST_ASSERT_GREATER_THAN(15 * 25, hi - lo);
  }
}

// ---------------------------------------------------------------------------
// Anturin leikkautuminen (rivi 10 / H1)
// ---------------------------------------------------------------------------

void test_return_wave_clips_at_sensor_ceiling() {
  // Lähes täysi kaasu: base -10 kPa, positiivinen paluuaalto 20 kPa ylittää 0 kPa:n.
  SimParams p = quiet();
  p.base_kpa = -10.0f;
  p.depth_kpa = 8.0f;
  p.peak_kpa = 20.0f;
  const SensorParams sp;
  const Rec r = run(p, 10000, sp);
  for (int c = 0; c < kChannels; ++c) {
    int at_ceiling = 0;
    for (uint16_t v : r.mv[c]) {
      TEST_ASSERT_LESS_OR_EQUAL(2700, v);
      at_ceiling += v == 2700 ? 1 : 0;
    }
    // Leikkautuu ~6 % syklistä (bumpin yläosa) -> ~600 näytettä 10 s:ssa.
    TEST_ASSERT_GREATER_THAN(400, at_ceiling);
  }
  // Kohinan kanssa huiput pysyvät sat_mv:n (2680) yläpuolella -> DSP:n rivi 10.
  p.noise_mv = 2.0f;
  const Rec n = run(p, 10000, sp);
  uint16_t hi = 0;
  for (uint16_t v : n.mv[0]) hi = v > hi ? v : hi;
  TEST_ASSERT_LESS_OR_EQUAL(2707, hi);
  TEST_ASSERT_GREATER_OR_EQUAL(sp.sat_mv, hi);
  // Ilman paluuaaltoa sama käyntitila ei saturoidu: maksimi -10 + 1 kPa = 2475 mV.
  p.peak_kpa = 0.0f;
  const Rec z = run(p, 10000, sp);
  hi = 0;
  for (uint16_t v : z.mv[0]) hi = v > hi ? v : hi;
  TEST_ASSERT_LESS_THAN(sp.sat_mv, hi);
  TEST_ASSERT_INT_WITHIN(8, 2475, hi);
}

void test_deep_pulse_clips_at_sensor_floor() {
  // Pulssin pohja -95 - 20 + 2.5 = -112.5 kPa < anturin -100 kPa:n alaraja.
  SimParams p = quiet();
  p.base_kpa = -95.0f;
  const SensorParams sp33;
  const Rec r = run(p, 3000, sp33);
  uint16_t lo = 0xFFFF;
  for (uint16_t v : r.mv[0]) lo = v < lo ? v : lo;
  TEST_ASSERT_EQUAL_UINT16(200, lo);  // 3,3 V: 2700 - 100·25
  // 5 V -anturi jakajan kautta: 24 mV/kPa -> alaraja 300 mV (docs/rauta.md §1.6).
  SensorParams sp5;
  sp5.mv_per_kpa = 24.0f;
  const Rec r5 = run(p, 3000, sp5);
  lo = 0xFFFF;
  for (uint16_t v : r5.mv[0]) lo = v < lo ? v : lo;
  TEST_ASSERT_EQUAL_UINT16(300, lo);
  // Ja 5 V -anturin tasot muunnetaan samoilla parametreilla oikein.
  SimParams q = quiet();
  const Rec m = run(q, 1, sp5);
  // Vaihe 0: SYL 1:n kuoppa alkaa (muoto = -keskiarvo = +2.5 kPa) -> -52.5 kPa.
  TEST_ASSERT_FLOAT_WITHIN(0.03f, -52.5f, m.kpa[0][0]);
}

// ---------------------------------------------------------------------------
// Ristivuoto ja resonanssi
// ---------------------------------------------------------------------------

void test_crosstalk_gives_smaller_secondary_dips() {
  // 600 rpm ilman jitteriä: sykli tasan 200 näytettä, SYL 2/3:n kuopat ±67/133.
  // Analyyttiset arvot (depth 20, w 0.25 -> muodon keskiarvo -2.5):
  //  oma kuoppa  base - 17.5 + x·(2.5 + 2.5)      = base - 16  (x = 0.3)
  //  vuotokuoppa base + 2.5 + x·(-17.5 + 2.5)     = base - 2
  //  kuoppien väli base + 2.5 + x·(2.5 + 2.5)     = base + 4
  SimParams p = quiet();
  p.rpm = 600.0f;
  p.xtalk = 0.3f;
  const float base = p.base_kpa;
  const Rec r = run(p, 2400);
  for (int c = 0; c < kChannels; ++c) {
    for (int k = 1; k <= 10; ++k) {
      const int s = 200 * k;
      int i0 = s;
      float hi = -1e9f;
      for (int i = s; i < s + 200; ++i) {
        if (r.kpa[c][i] < r.kpa[c][i0]) i0 = i;
        if (r.kpa[c][i] > hi) hi = r.kpa[c][i];
      }
      const float prim = r.kpa[c][i0];
      float sec[2] = {1e9f, 1e9f};
      for (int j = 0; j < 2; ++j) {
        const int centre = i0 + (j == 0 ? 67 : 133);
        for (int i = centre - 20; i <= centre + 20; ++i) {
          if (r.kpa[c][i] < sec[j]) sec[j] = r.kpa[c][i];
        }
      }
      TEST_ASSERT_FLOAT_WITHIN(0.1f, base - 16.0f, prim);
      TEST_ASSERT_FLOAT_WITHIN(0.1f, base + 4.0f, hi);
      for (int j = 0; j < 2; ++j) {
        TEST_ASSERT_FLOAT_WITHIN(0.1f, base - 2.0f, sec[j]);
        // Vuotokuopan suhteellinen syvyys = xtalk.
        TEST_ASSERT_FLOAT_WITHIN(0.015f, 0.3f, (hi - sec[j]) / (hi - prim));
      }
    }
  }
  // Ilman ristivuotoa samoissa kohdissa ei ole kuoppaa (taso base + 2.5).
  p.xtalk = 0.0f;
  const Rec z = run(p, 2400);
  float sec_min = 1e9f;
  for (int i = 200 + 25 + 67 - 20; i <= 200 + 25 + 67 + 20; ++i) {
    sec_min = z.kpa[0][i] < sec_min ? z.kpa[0][i] : sec_min;
  }
  TEST_ASSERT_FLOAT_WITHIN(0.05f, base + 2.5f, sec_min);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, base - 17.5f, z.kpa[0][225]);  // oma kuoppa, vaihe 0.125
}

void test_resonance_frequency_decay_and_zero_mean() {
  // Erotus resonanssilla ja ilman (sama seed, sama PRNG-kulutus) = pelkkä resonanssi.
  SimParams p = quiet();
  p.rpm = 600.0f;
  // Kuoppien välinen taso -55 + 2.5 kPa osuisi tasan 1387,5 mV:iin, jolloin
  // resonanssin mikroskooppinen häntä kääntäisi pyöristystä vain yhteen suuntaan.
  p.base_kpa = -55.013f;
  p.res_hz[0] = 90.0f;
  p.res_hz[1] = 112.0f;
  p.res_hz[2] = 150.0f;
  p.res_kpa = 3.0f;
  p.res_decay_s = 0.02f;
  SimParams q = p;
  q.res_kpa = 0.0f;
  const int n = 4000;
  const Rec a = run(p, n);
  const Rec b = run(q, n);
  for (int c = 0; c < kChannels; ++c) {
    std::vector<float> d(n);
    for (int i = 0; i < n; ++i) d[i] = a.kpa[c][i] - b.kpa[c][i];
    // Herätteen kokonaisvaste on nolla -> ei siirrä D1-keskiarvoa.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, meanOf(d, 0, d.size()));
    // Spektrin huippu letkun taajuudella (viivat 5 Hz:n välein, 600 rpm).
    float best_f = 0.0f, best_p = -1.0f;
    for (float f = 60.0f; f <= 200.0f; f += 1.0f) {
      const float pw = goertzelPower(d, f);
      if (pw > best_p) {
        best_p = pw;
        best_f = f;
      }
    }
    TEST_ASSERT_FLOAT_WITHIN(5.0f, p.res_hz[c], best_f);
    // Herätteet: hiljaisuuden jälkeinen hyppy (τ = 20 ms, sykli 200 ms -> e^-10).
    int found = 0;
    for (int i = 1; i + 40 < n; ++i) {
      if (!(std::fabs(d[i - 1]) < 0.1f && std::fabs(d[i]) > 1.0f)) continue;
      ++found;
      // Ensimmäinen näyte on negatiivinen: paine putoaa imuventtiilin auetessa.
      TEST_ASSERT_LESS_THAN_FLOAT(0.0f, d[i]);
      float peak = 0.0f, e1 = 0.0f, e2 = 0.0f;
      for (int k = 0; k < 10; ++k) peak = std::fabs(d[i + k]) > peak ? std::fabs(d[i + k]) : peak;
      for (int k = 0; k < 20; ++k) e1 += d[i + k] * d[i + k];
      for (int k = 20; k < 40; ++k) e2 += d[i + k] * d[i + k];
      TEST_ASSERT_FLOAT_WITHIN(0.5f, 2.7f, peak);  // amplitudi 3 kPa, vaimenee hiukan
      // Verhokäyrä e^(-t/τ): 20 ms myöhemmin RMS e^-1 -kertainen.
      TEST_ASSERT_FLOAT_WITHIN(0.06f, std::exp(-1.0f), std::sqrt(e2 / e1));
    }
    TEST_ASSERT_GREATER_OR_EQUAL(18, found);  // 20 sykliä 4 s:ssa
  }
}

// ---------------------------------------------------------------------------
// configure(): ei vaihehyppyä
// ---------------------------------------------------------------------------

void test_configure_with_identical_params_keeps_stream() {
  SimParams p;
  p.seed = 21;
  p.rpm_jitter = 0.05f;
  p.rpm_drift = 30.0f;
  const SensorParams sp;
  EngineSim a, b;
  a.configure(p, sp);
  b.configure(p, sp);
  for (int i = 0; i < 1234; ++i) {
    if (!sameFrame(a.next(), b.next())) TEST_FAIL_MESSAGE("ero ennen configurea");
  }
  a.configure(p, sp);  // samat parametrit kesken ajon
  for (int i = 0; i < 5000; ++i) {
    if (!sameFrame(a.next(), b.next())) TEST_FAIL_MESSAGE("configure() katkaisi virran");
  }
  TEST_ASSERT_EQUAL_FLOAT(b.trueRpm(), a.trueRpm());
}

void test_configure_rpm_change_keeps_phase() {
  SimParams p = quiet();
  const SensorParams sp;
  EngineSim sim;
  sim.configure(p, sp);
  Rec r;
  record(sim, 1070, sp, r);  // keskellä sykliä
  uint16_t prev = r.mv[0].back();
  p.rpm = 870.0f;
  sim.configure(p, sp);
  const RawFrame f = sim.next();
  // Normaali näytteiden välinen muutos: suurin kuopan reunalla
  // depth·π/w / T ≈ 20·π/0.25/141 ≈ 1.8 kPa ≈ 45 mV.
  int max_step = 0;
  for (size_t i = 1; i < r.mv[0].size(); ++i) {
    const int s = std::abs(static_cast<int>(r.mv[0][i]) - static_cast<int>(r.mv[0][i - 1]));
    max_step = s > max_step ? s : max_step;
  }
  TEST_ASSERT_LESS_OR_EQUAL(max_step + 1, std::abs(static_cast<int>(f.mv[0]) - static_cast<int>(prev)));
  r.mv[0].push_back(f.mv[0]);
  r.kpa[0].push_back(toKpa(f.mv[0], sp));
  record(sim, 3000, sp, r);
  // Jos vaihe olisi nollautunut, muutoskohdan ympärille syntyisi lyhyt jakso.
  const std::vector<float> t = cycleStarts(r.kpa[0], 9);
  const float t_lo = periodSamples(870.0f) - 0.3f;
  const float t_hi = periodSamples(850.0f) + 0.3f;
  for (size_t k = 1; k < t.size(); ++k) {
    const float d = t[k] - t[k - 1];
    TEST_ASSERT_TRUE_MESSAGE(d > t_lo && d < t_hi, "jakso muutoksen ympärillä väärä");
  }
  // Muutoksen jälkeen jakso on uuden rpm:n mukainen.
  TEST_ASSERT_FLOAT_WITHIN(0.2f, periodSamples(870.0f), t.back() - t[t.size() - 2]);
  TEST_ASSERT_EQUAL_FLOAT(870.0f, sim.trueRpm());
}

// ---------------------------------------------------------------------------
// Robustius: UI:n arvot eivät saa rikkoa ulostuloa
// ---------------------------------------------------------------------------

void test_extreme_params_stay_in_range() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  SimParams p;
  p.rpm = nan;
  p.rpm_jitter = 5.0f;
  p.rpm_drift = inf;
  p.depth_kpa = -10.0f;
  p.intake_frac = 3.0f;
  p.res_hz[0] = 10000.0f;
  p.res_hz[1] = nan;
  p.res_decay_s = -1.0f;
  p.xtalk = 7.0f;
  p.peak_kpa = nan;
  p.noise_mv = inf;
  p.base_kpa = nan;
  EngineSim sim;
  sim.configure(p, SensorParams());
  for (int i = 0; i < 3000; ++i) {
    const RawFrame f = sim.next();
    for (int c = 0; c < kChannels; ++c) TEST_ASSERT_LESS_OR_EQUAL(3300, f.mv[c]);
  }
  const float rpm = sim.trueRpm();
  TEST_ASSERT_TRUE(rpm >= EngineSim::kRpmMin && rpm <= EngineSim::kRpmMax);
  // Suurin jitter ja kierrosluku: vaihe etenee eikä jumitu.
  SimParams q;
  q.rpm = EngineSim::kRpmMax;
  q.rpm_jitter = 0.4f;
  q.noise_mv = 0.0f;
  EngineSim s2;
  s2.configure(q, SensorParams());
  uint16_t lo = 0xFFFF, hi = 0;
  for (int i = 0; i < 3000; ++i) {
    const RawFrame f = s2.next();
    for (int c = 0; c < kChannels; ++c) {
      TEST_ASSERT_LESS_OR_EQUAL(3300, f.mv[c]);
      lo = f.mv[0] < lo ? f.mv[0] : lo;
      hi = f.mv[0] > hi ? f.mv[0] : hi;
    }
  }
  TEST_ASSERT_GREATER_THAN(200, hi - lo);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_same_seed_gives_identical_stream);
  RUN_TEST(test_different_seed_differs);
  RUN_TEST(test_reset_replays_stream_and_fresh_configure_equals_reset);
  RUN_TEST(test_seq_increments_by_one);
  RUN_TEST(test_cycle_mean_equals_truth_with_noise_and_resonance);
  RUN_TEST(test_sensor_gain_and_offset_error_applied);
  RUN_TEST(test_period_is_120_over_rpm);
  RUN_TEST(test_cylinders_are_one_third_cycle_apart);
  RUN_TEST(test_jitter_sets_period_sd_on_every_channel);
  RUN_TEST(test_drift_changes_true_rpm_and_period);
  RUN_TEST(test_engine_off_reads_atmosphere);
  RUN_TEST(test_engine_restart_resumes_without_reset);
  RUN_TEST(test_hose_off_channel_sees_atmosphere_others_pulse);
  RUN_TEST(test_sensor_low_and_high_faults);
  RUN_TEST(test_return_wave_clips_at_sensor_ceiling);
  RUN_TEST(test_deep_pulse_clips_at_sensor_floor);
  RUN_TEST(test_crosstalk_gives_smaller_secondary_dips);
  RUN_TEST(test_resonance_frequency_decay_and_zero_mean);
  RUN_TEST(test_configure_with_identical_params_keeps_stream);
  RUN_TEST(test_configure_rpm_change_keeps_phase);
  RUN_TEST(test_extreme_params_stay_in_range);
  return UNITY_END();
}
