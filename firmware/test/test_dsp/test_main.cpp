// WP-2: dsp.h -testit.
//
// Totuus tulee test/support/synth_signal.h:sta, jonka jokaisen syklin
// keskiarvo on analyyttisesti tunnettu (base + sync_err). Siksi keskiarvo- ja
// rpm-virheet mitataan absoluuttisesti eikä "näyttää järkevältä" -tasolla.
// Oletussignaali: 3 kPa:n letkuresonanssi eri taajuudella joka kanavalla
// (90/135/180 Hz, R1 rikottu tarkoituksella), kohina 0,2 kPa, kanavien
// synkronointivirheet 0 / +1,0 / -0,7 kPa.

#include <unity.h>

#include <cmath>
#include <cstdio>

#include "core/dsp.h"
#include "synth_signal.h"

using sbx::ChannelState;
using sbx::Dsp;
using sbx::DspParams;
using sbx::RawFrame;
using sbx::RpmEstimate;
using sbx::SensorParams;
using sbx::kChannels;
using sbxtest::SynthParams;
using sbxtest::SynthSignal;

namespace {

struct Rig {
  Dsp dsp;
  SynthSignal syn;
  SensorParams sp;
  float kpa[kChannels] = {0.0f, 0.0f, 0.0f};
  RawFrame raw;

  void begin(const SynthParams& p, const DspParams& dp = DspParams()) {
    syn.reset(p);
    dsp.configure(dp, sp);
  }
  void step() {
    syn.next(kpa, raw);
    dsp.push(kpa, raw);
  }
  void run(float seconds) {
    const int n = static_cast<int>(seconds * 1000.0f + 0.5f);
    for (int i = 0; i < n; ++i) step();
  }
};

Rig g_rig;

SynthParams idleParams() {
  SynthParams p;
  p.rpm = 850.0f;
  p.jitter = 0.002f;
  p.base_kpa = -55.0f;
  p.depth_kpa = 20.0f;
  p.sync_err_kpa[0] = 0.0f;
  p.sync_err_kpa[1] = 1.0f;
  p.sync_err_kpa[2] = -0.7f;
  p.res_kpa = 3.0f;
  p.noise_kpa = 0.2f;
  p.start_phase = 0.37f;
  p.seed = 7;
  return p;
}

void msg(const char* fmt, double a = 0.0, double b = 0.0, double c = 0.0, double d = 0.0, double e = 0.0,
         double f = 0.0) {
  char buf[200];
  std::snprintf(buf, sizeof(buf), fmt, a, b, c, d, e, f);
  TEST_MESSAGE(buf);
}

// Virheiden maksimit mittausjakson aikana (näyte 20 ms välein ~ näyttötahti).
struct Accuracy {
  float rpm_err_rel = 0.0f;   // |rpm - totuus| / totuus, RpmEstimate
  float mean_err = 0.0f;      // max |mean_kpa - totuus| yli kanavien
  float diff_err = 0.0f;      // max virhe kanavaerossa (D3: mitä laite näyttää)
  bool always_pulse = true;
  bool always_valid = true;
};

Accuracy measure(Rig& r, float seconds) {
  Accuracy a;
  const int n = static_cast<int>(seconds * 1000.0f);
  for (int i = 0; i < n; ++i) {
    r.step();
    if (i % 20 != 0) continue;
    const RpmEstimate e = r.dsp.rpm();
    if (!e.valid) a.always_valid = false;
    const float re = std::fabs(e.rpm - r.syn.trueRpm()) / r.syn.trueRpm();
    if (re > a.rpm_err_rel) a.rpm_err_rel = re;
    for (int c = 0; c < kChannels; ++c) {
      const ChannelState& s = r.dsp.channel(c);
      if (!s.has_pulse) a.always_pulse = false;
      const float me = std::fabs(s.mean_kpa - r.syn.trueMean(c));
      if (me > a.mean_err) a.mean_err = me;
    }
    for (int c = 1; c < kChannels; ++c) {
      const float shown = r.dsp.channel(0).mean_kpa - r.dsp.channel(c).mean_kpa;
      const float truth = r.syn.trueMean(0) - r.syn.trueMean(c);
      const float de = std::fabs(shown - truth);
      if (de > a.diff_err) a.diff_err = de;
    }
  }
  return a;
}

// Amplitudi: max |y| suodattimen läpi asettumisen jälkeen.
float lpGain(float f_hz, float fc_hz) {
  sbx::ButterLp4 lp;
  lp.setup(fc_hz, 1000.0f);
  lp.prime(0.0f);
  float peak = 0.0f;
  for (int n = 0; n < 4000; ++n) {
    const float y = lp.process(std::sin(2.0f * 3.14159265f * f_hz * static_cast<float>(n) / 1000.0f));
    if (n >= 2000 && std::fabs(y) > peak) peak = std::fabs(y);
  }
  return peak;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- suodin ------------------------------------------------------------------

void test_lowpass_butterworth_response() {
  // 4. kert. Butterworth: |H| = 1/sqrt(1 + (f/fc)^8).
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, lpGain(7.0f, 40.0f));        // joutokäynnin perustaajuus
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.7071f, lpGain(40.0f, 40.0f));    // -3 dB tasan fc:ssä (prewarp)
  TEST_ASSERT_FLOAT_WITHIN(0.006f, 0.0390f, lpGain(90.0f, 40.0f));   // 1 m letku: 3 kPa -> 0,12 kPa
  TEST_ASSERT_LESS_THAN_FLOAT(0.004f, lpGain(180.0f, 40.0f));        // 0,5 m letku
  // prime(): vakiotulo ei tuota transienttia (muuten -55 kPa näkyisi askeleena).
  sbx::ButterLp4 lp;
  lp.setup(40.0f, 1000.0f);
  lp.prime(-55.0f);
  for (int i = 0; i < 100; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-3f, -55.0f, lp.process(-55.0f));
}

// --- tarkkuus ----------------------------------------------------------------

void test_accuracy_idle_to_3000_rpm() {
  // Jokainen rivi: rpm, keskiarvo, pulssin syvyys - karkeasti imusarjan
  // käyttäytyminen kaasun avautuessa (alipaine ja pulssi pienenevät).
  const float table[][3] = {{600.0f, -58.0f, 22.0f},
                            {850.0f, -55.0f, 20.0f},
                            {1500.0f, -50.0f, 18.0f},
                            {3000.0f, -35.0f, 14.0f}};
  for (const auto& row : table) {
    SynthParams p = idleParams();
    p.rpm = row[0];
    p.base_kpa = row[1];
    p.depth_kpa = row[2];
    g_rig.begin(p);
    g_rig.run(3.0f);
    const Accuracy a = measure(g_rig, 10.0f);
    msg("rpm %.0f: rpm-virhe %.3f %%, keskiarvo %.3f kPa, D3-ero %.3f kPa", static_cast<double>(row[0]),
        static_cast<double>(a.rpm_err_rel * 100.0f), static_cast<double>(a.mean_err),
        static_cast<double>(a.diff_err));
    TEST_ASSERT_TRUE_MESSAGE(a.always_pulse, "pulssi katosi kesken");
    TEST_ASSERT_TRUE_MESSAGE(a.always_valid, "rpm ei voimassa");
    TEST_ASSERT_LESS_THAN_FLOAT(0.005f, a.rpm_err_rel);
    TEST_ASSERT_LESS_THAN_FLOAT(0.1f, a.mean_err);
    TEST_ASSERT_LESS_THAN_FLOAT(0.1f, a.diff_err);
  }
}

void test_accuracy_6000_rpm() {
  // Täysi kaasu: perustaajuus 50 Hz on jo yli 40 Hz:n rajan, sykli vain 20
  // näytettä ja imu 5 ms - murto-osainen raja on tässä välttämätön.
  SynthParams p = idleParams();
  p.rpm = 6000.0f;
  p.base_kpa = -20.0f;
  p.depth_kpa = 14.0f;
  g_rig.begin(p);
  g_rig.run(2.0f);
  const Accuracy a = measure(g_rig, 10.0f);
  msg("rpm 6000: rpm-virhe %.3f %%, keskiarvo %.3f kPa, D3-ero %.3f kPa",
      static_cast<double>(a.rpm_err_rel * 100.0f), static_cast<double>(a.mean_err),
      static_cast<double>(a.diff_err));
  TEST_ASSERT_TRUE(a.always_pulse);
  TEST_ASSERT_TRUE(a.always_valid);
  TEST_ASSERT_LESS_THAN_FLOAT(0.01f, a.rpm_err_rel);
  TEST_ASSERT_LESS_THAN_FLOAT(0.5f, a.mean_err);
}

void test_fractional_boundary_single_cycle_mean() {
  // avg_cycles = 1 eristää yhden syklin keskiarvon. 5500 rpm = 21,8 näytettä
  // per sykli, eli raja osuu joka kerta eri kohtaan näytteiden väliin.
  // Kokonaisiin näytteisiin pyöristetty raja antaisi tässä ~0,2 kPa virheen
  // per sykli (rajalla signaali on ~10 kPa keskiarvon alla, ±0,5 ms / 21,8 ms).
  SynthParams p = idleParams();
  p.rpm = 5500.0f;
  p.base_kpa = -12.0f;
  p.depth_kpa = 8.0f;
  p.jitter = 0.0f;
  p.noise_kpa = 0.0f;
  p.res_kpa = 0.0f;
  DspParams dp;
  dp.avg_cycles = 1;
  g_rig.begin(p, dp);
  g_rig.run(1.0f);
  float worst = 0.0f;
  uint32_t last = g_rig.dsp.channel(0).cycles;
  int checked = 0;
  for (int i = 0; i < 3000; ++i) {
    g_rig.step();
    for (int c = 0; c < kChannels; ++c) {
      const float e = std::fabs(g_rig.dsp.channel(c).mean_kpa - g_rig.syn.trueMean(c));
      if (e > worst) worst = e;
    }
    if (g_rig.dsp.channel(0).cycles != last) {
      last = g_rig.dsp.channel(0).cycles;
      ++checked;
    }
  }
  msg("yhden syklin keskiarvon maksimivirhe %.4f kPa (%.0f sykliä)", static_cast<double>(worst),
      static_cast<double>(checked));
  TEST_ASSERT_GREATER_THAN(130, checked);  // 3 s / 21,8 ms
  TEST_ASSERT_LESS_THAN_FLOAT(0.05f, worst);
}

// --- ristivuoto ----------------------------------------------------------------

void test_crosstalk_does_not_triple_rpm() {
  const float rpms[] = {600.0f, 850.0f, 1500.0f, 3000.0f};
  for (float rpm : rpms) {
    SynthParams p = idleParams();
    p.rpm = rpm;
    p.xtalk = 0.3f;
    p.noise_kpa = 0.2f;
    g_rig.begin(p);

    // Varmistetaan, että signaali todella on haastava: naiivi ilmaisin
    // (suodatettu signaali ylittää keskiarvonsa alaspäin) laskee
    // vuotopulssit omiksi sykleikseen -> ~3 per moottorisykli. 3000 rpm:ssä
    // vuotopulssien perustaajuus on 75 Hz, jonka 40 Hz:n alipäästö jo
    // vaimentaa, joten haaste koskee vain alempia kierroksia.
    sbx::ButterLp4 lp;
    lp.setup(40.0f, 1000.0f);
    bool primed = false;
    bool above = true;
    int naive = 0;
    const uint32_t c0 = g_rig.syn.cyclesStarted();
    for (int i = 0; i < 5000; ++i) {
      g_rig.step();
      if (!primed) {
        lp.prime(g_rig.kpa[0]);
        primed = true;
      }
      const float y = lp.process(g_rig.kpa[0]);
      const float m = g_rig.syn.trueMean(0);
      if (above && y < m - 0.3f) {
        above = false;
        ++naive;
      } else if (!above && y > m + 0.3f) {
        above = true;
      }
    }
    const int engine = static_cast<int>(g_rig.syn.cyclesStarted() - c0);
    if (rpm <= 1500.0f) TEST_ASSERT_GREATER_THAN(engine * 5 / 2, naive);

    const uint32_t start_cycles = g_rig.dsp.channel(1).cycles;
    const uint32_t start_engine = g_rig.syn.cyclesStarted();
    const Accuracy a = measure(g_rig, 8.0f);
    const int counted = static_cast<int>(g_rig.dsp.channel(1).cycles - start_cycles);
    const int truth = static_cast<int>(g_rig.syn.cyclesStarted() - start_engine);
    msg("xtalk 0,3 @ %.0f rpm: naiivi %.0f / %.0f, DSP %.0f / %.0f sykliä, rpm-virhe %.3f %%",
        static_cast<double>(rpm), static_cast<double>(naive), static_cast<double>(engine),
        static_cast<double>(counted), static_cast<double>(truth), static_cast<double>(a.rpm_err_rel * 100.0f));
    TEST_ASSERT_TRUE(a.always_pulse);
    TEST_ASSERT_INT_WITHIN(2, truth, counted);
    TEST_ASSERT_LESS_THAN_FLOAT(0.005f, a.rpm_err_rel);
    TEST_ASSERT_LESS_THAN_FLOAT(0.1f, a.mean_err);
    TEST_ASSERT_LESS_THAN_FLOAT(0.1f, a.diff_err);
  }
}

// --- hajonta (rivi 2) ----------------------------------------------------------

void test_cv_follows_jitter_5pct() {
  SynthParams p = idleParams();
  p.jitter = 0.05f;
  g_rig.begin(p);
  g_rig.run(3.0f);
  float sum = 0.0f, lo = 1.0f, hi = 0.0f;
  int n = 0;
  for (int i = 0; i < 20000; ++i) {
    g_rig.step();
    if (i % 100 != 0) continue;
    const RpmEstimate e = g_rig.dsp.rpm();
    TEST_ASSERT_TRUE(e.valid);
    sum += e.cv;
    if (e.cv < lo) lo = e.cv;
    if (e.cv > hi) hi = e.cv;
    ++n;
  }
  const float avg = sum / static_cast<float>(n);
  msg("jitter 5 %%: cv ka %.4f, vaihteluväli %.4f...%.4f", static_cast<double>(avg),
      static_cast<double>(lo), static_cast<double>(hi));
  // 16 jakson otoshajonnan suhteellinen keskivirhe on ~18 %, joten yksittäinen
  // lukema vaihtelee; keskiarvo on tiukka, ääripäät väljemmät.
  TEST_ASSERT_GREATER_THAN_FLOAT(0.03f, avg);
  TEST_ASSERT_LESS_THAN_FLOAT(0.08f, avg);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.02f, lo);
  TEST_ASSERT_LESS_THAN_FLOAT(0.10f, hi);
}

void test_cv_small_with_jitter_half_pct() {
  SynthParams p = idleParams();
  p.jitter = 0.005f;
  g_rig.begin(p);
  g_rig.run(3.0f);
  float hi = 0.0f;
  for (int i = 0; i < 10000; ++i) {
    g_rig.step();
    if (i % 100 != 0) continue;
    const RpmEstimate e = g_rig.dsp.rpm();
    TEST_ASSERT_TRUE(e.valid);
    if (e.cv > hi) hi = e.cv;
    TEST_ASSERT_FLOAT_WITHIN(0.01f * e.rpm, e.sd_rpm / e.cv, e.rpm);  // cv = sd / rpm
  }
  msg("jitter 0,5 %%: cv max %.4f", static_cast<double>(hi));
  TEST_ASSERT_LESS_THAN_FLOAT(0.02f, hi);
}

// --- has_pulse -----------------------------------------------------------------

void test_has_pulse_within_three_cycles_of_plug_in() {
  // DSP käynnistyy käyvän moottorin signaaliin satunnaisessa vaiheessa, myös
  // keskeltä vuotopulssia (xtalk). Ensimmäisen hyväksytyn syklin on oltava
  // oikea: väärä ensimmäinen sykli näkyisi 8 syklin ajan keskiarvossa.
  // Pahin tapaus ~3,4 sykliä: ensimmäinen oma pulssi osuu suotimen
  // asettumisaikaan (37,5 ms), ja lukitus vaatii kolme samankokoista pulssia.
  const float xt[] = {0.0f, 0.3f};
  int worst = 0;
  for (float x : xt) {
    for (int k = 0; k < 20; ++k) {
      const float ph = 0.05f * static_cast<float>(k);
      SynthParams p = idleParams();
      p.start_phase = ph;
      p.xtalk = x;
      g_rig.begin(p);
      const float period_ms = 120000.0f / p.rpm;
      int first[kChannels] = {-1, -1, -1};
      for (int i = 0; i < 1000; ++i) {
        g_rig.step();
        for (int c = 0; c < kChannels; ++c) {
          const ChannelState& s = g_rig.dsp.channel(c);
          if (first[c] < 0 && s.has_pulse) {
            first[c] = i;
            TEST_ASSERT_FLOAT_WITHIN(0.02f * p.rpm, p.rpm, s.rpm);
            TEST_ASSERT_FLOAT_WITHIN(0.3f, g_rig.syn.trueMean(c), s.mean_kpa);
            TEST_ASSERT_EQUAL_UINT32(1, s.cycles);
          }
        }
      }
      for (int c = 0; c < kChannels; ++c) {
        TEST_ASSERT_GREATER_OR_EQUAL(0, first[c]);
        TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(3.5f * period_ms), first[c]);
        if (first[c] > worst) worst = first[c];
      }
    }
  }
  msg("lukitus käyvään signaaliin: pahin %.0f ms = %.2f sykliä (850 rpm)", static_cast<double>(worst),
      static_cast<double>(worst) / (120000.0 / 850.0));
}

void test_has_pulse_after_engine_start_from_atmosphere() {
  // Moottori seis (ilmanpaine + kohina) -> käynnistys suoraan -55 kPa:iin.
  // Askel on verhokäyrälle vaikein tapaus: se on 2,5x pulssin korkuinen, ja
  // vanha verhokäyrä vanhenee vasta pisimmän sallitun jakson (rpm_min) jälkeen.
  // Oikea käynnistys on loivempi (startti pyörittää ensin ~300 rpm).
  int worst = 0;
  for (int k = 0; k < 10; ++k) {
    SynthParams p = idleParams();
    p.engine_on = false;
    p.xtalk = (k & 1) ? 0.3f : 0.0f;
    p.seed = 100u + static_cast<uint32_t>(k);
    g_rig.begin(p);
    for (int i = 0; i < 3000 + 13 * k; ++i) {
      g_rig.step();
      for (int c = 0; c < kChannels; ++c) TEST_ASSERT_FALSE(g_rig.dsp.channel(c).has_pulse);
    }
    p.engine_on = true;
    p.start_phase = 0.1f * static_cast<float>(k);
    g_rig.syn.setParams(p);
    int first = -1;
    for (int i = 0; i < 2000 && first < 0; ++i) {
      g_rig.step();
      bool all = true;
      for (int c = 0; c < kChannels; ++c) all = all && g_rig.dsp.channel(c).has_pulse;
      if (all) first = i;
    }
    TEST_ASSERT_GREATER_OR_EQUAL(0, first);
    TEST_ASSERT_LESS_THAN(1000, first);
    if (first > worst) worst = first;
    g_rig.run(2.0f);
    for (int c = 0; c < kChannels; ++c) {
      TEST_ASSERT_FLOAT_WITHIN(0.01f * p.rpm, p.rpm, g_rig.dsp.channel(c).rpm);
      TEST_ASSERT_FLOAT_WITHIN(0.1f, g_rig.syn.trueMean(c), g_rig.dsp.channel(c).mean_kpa);
    }
  }
  msg("käynnistys ilmanpaineesta: kaikki kanavat pulssivat viimeistään %.0f ms:n jälkeen",
      static_cast<double>(worst));
}

void test_pulse_timeout_when_signal_goes_flat() {
  // Seurataan, milloin viimeinen sykli hyväksyttiin; has_pulse putoaa
  // tasan pulse_timeout_s (1000 näytettä) sen jälkeen. Indeksi 0 = pysähdys.
  SynthParams p = idleParams();
  g_rig.begin(p);
  int last_cycle[kChannels];
  int fell[kChannels];
  uint32_t prev[kChannels];
  for (int c = 0; c < kChannels; ++c) {
    last_cycle[c] = -100000;
    fell[c] = -1;
    prev[c] = 0;
  }
  for (int i = -3000; i < 6000; ++i) {
    if (i == 0) {
      TEST_ASSERT_TRUE(g_rig.dsp.rpm().valid);
      p.engine_on = false;  // ilmanpaine + kohina (rivi 1 / 8)
      g_rig.syn.setParams(p);
    }
    g_rig.step();
    for (int c = 0; c < kChannels; ++c) {
      const ChannelState& s = g_rig.dsp.channel(c);
      if (s.cycles != prev[c]) {
        prev[c] = s.cycles;
        last_cycle[c] = i;
      }
      if (i >= 0 && fell[c] < 0 && !s.has_pulse) fell[c] = i;
    }
  }
  for (int c = 0; c < kChannels; ++c) {
    // Viimeinen sykli päättyy viimeisen pulssin rajalle (suotimen viive ~15 ms).
    TEST_ASSERT_GREATER_THAN(-200, last_cycle[c]);
    TEST_ASSERT_LESS_THAN(50, last_cycle[c]);
    TEST_ASSERT_EQUAL_INT(last_cycle[c] + 1000, fell[c]);
    const ChannelState& s = g_rig.dsp.channel(c);
    TEST_ASSERT_FALSE(s.has_pulse);  // eikä kohina herättänyt sitä 5 s:n aikana
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.rpm);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, s.level_kpa);
  }
  TEST_ASSERT_FALSE(g_rig.dsp.rpm().valid);
}

void test_noise_and_resonance_alone_never_pulse() {
  // Letku irti tai moottori seis, mutta kaksinkertainen kohina ja jatkuva
  // 112 Hz:n 3 kPa:n soiminen: rivit 1/6/8 vaativat, että has_pulse pysyy
  // epätotena (muuten letkuvika peittyisi).
  Dsp dsp;
  uint32_t rng = 12345u;
  float spare = 0.0f;
  bool has_spare = false;
  auto gauss = [&]() -> float {
    if (has_spare) {
      has_spare = false;
      return spare;
    }
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    const float u1 = (static_cast<float>(rng >> 8) + 1.0f) / 16777217.0f;
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    const float u2 = static_cast<float>(rng >> 8) / 16777216.0f;
    const float r = std::sqrt(-2.0f * std::log(u1));
    spare = r * std::sin(6.2831853f * u2);
    has_spare = true;
    return r * std::cos(6.2831853f * u2);
  };
  float kpa[kChannels];
  RawFrame raw;
  for (int i = 0; i < 20000; ++i) {
    const float t = static_cast<float>(i) / 1000.0f;
    for (int c = 0; c < kChannels; ++c) {
      kpa[c] = -0.3f * static_cast<float>(c) + 3.0f * std::sin(6.2831853f * 112.0f * t) + 0.4f * gauss();
      raw.mv[c] = static_cast<uint16_t>(2700.0f + 25.0f * kpa[c]);
    }
    dsp.push(kpa, raw);
    for (int c = 0; c < kChannels; ++c) TEST_ASSERT_FALSE(dsp.channel(c).has_pulse);
  }
  for (int c = 0; c < kChannels; ++c) TEST_ASSERT_EQUAL_UINT32(0, dsp.channel(c).cycles);
}

// --- taso, viat, saturaatio ------------------------------------------------------

void test_level_ema_tracks_step_with_tau() {
  Dsp dsp;
  DspParams dp;
  dp.level_tau_s = 0.5f;
  dsp.configure(dp, SensorParams());
  float kpa[kChannels] = {-40.0f, -40.0f, -40.0f};
  RawFrame raw;
  raw.mv[0] = raw.mv[1] = raw.mv[2] = 1700;
  dsp.push(kpa, raw);
  // Ensimmäinen näyte asettaa tason suoraan - ei ryömintää nollasta.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -40.0f, dsp.channel(0).level_kpa);
  for (int i = 0; i < 999; ++i) dsp.push(kpa, raw);
  for (int c = 0; c < kChannels; ++c) kpa[c] = -10.0f;
  for (int i = 0; i < 500; ++i) dsp.push(kpa, raw);
  // tau:n kohdalla 63,2 % askeleesta: -40 + 30 * (1 - e^-1) = -21,04
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -21.04f, dsp.channel(1).level_kpa);
  for (int i = 0; i < 2000; ++i) dsp.push(kpa, raw);  // 5 tau yhteensä
  TEST_ASSERT_FLOAT_WITHIN(0.25f, -10.2f, dsp.channel(2).level_kpa);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -10.0f, dsp.channel(2).last_kpa);
}

void test_sensor_fault_after_hold_and_clears() {
  Dsp dsp;
  SensorParams sp;  // 100...3150 mV, 1,0 s
  dsp.configure(DspParams(), sp);
  float kpa[kChannels] = {-55.0f, -55.0f, -55.0f};
  RawFrame raw;
  raw.mv[0] = 1325;
  raw.mv[1] = 50;    // alle fault_lo_mv: anturi irti / GND
  raw.mv[2] = 3200;  // yli fault_hi_mv: kiinni kiskossa
  for (int i = 1; i <= 999; ++i) {
    dsp.push(kpa, raw);
    TEST_ASSERT_FALSE(dsp.channel(1).sensor_fault);
    TEST_ASSERT_FALSE(dsp.channel(2).sensor_fault);
  }
  dsp.push(kpa, raw);  // 1000. näyte = 1,0 s yhtäjaksoisesti
  TEST_ASSERT_FALSE(dsp.channel(0).sensor_fault);
  TEST_ASSERT_TRUE(dsp.channel(1).sensor_fault);
  TEST_ASSERT_TRUE(dsp.channel(2).sensor_fault);
  TEST_ASSERT_EQUAL_UINT16(50, dsp.channel(1).last_mv);

  raw.mv[1] = 100;  // rajalla = alueella
  dsp.push(kpa, raw);
  TEST_ASSERT_FALSE(dsp.channel(1).sensor_fault);
  TEST_ASSERT_TRUE(dsp.channel(2).sensor_fault);

  // Katkonainen vika (yksi hyvä näyte välissä) aloittaa pidon alusta.
  raw.mv[1] = 20;
  for (int i = 0; i < 600; ++i) dsp.push(kpa, raw);
  raw.mv[1] = 1500;
  dsp.push(kpa, raw);
  raw.mv[1] = 20;
  for (int i = 0; i < 999; ++i) dsp.push(kpa, raw);
  TEST_ASSERT_FALSE(dsp.channel(1).sensor_fault);
  dsp.push(kpa, raw);
  TEST_ASSERT_TRUE(dsp.channel(1).sensor_fault);

  // Pidon pituus on parametri.
  sp.fault_hold_s = 0.25f;
  dsp.configure(DspParams(), sp);
  for (int i = 0; i < 249; ++i) dsp.push(kpa, raw);
  TEST_ASSERT_FALSE(dsp.channel(1).sensor_fault);
  dsp.push(kpa, raw);
  TEST_ASSERT_TRUE(dsp.channel(1).sensor_fault);
}

void test_saturation_flag_lasts_one_second() {
  Dsp dsp;
  dsp.configure(DspParams(), SensorParams());  // sat_mv 2680
  float kpa[kChannels] = {-1.0f, -1.0f, -1.0f};
  RawFrame raw;
  raw.mv[0] = raw.mv[1] = raw.mv[2] = 2675;
  for (int i = 0; i < 100; ++i) dsp.push(kpa, raw);
  TEST_ASSERT_FALSE(dsp.channel(0).saturated);
  raw.mv[0] = 2680;  // >= sat_mv: positiivinen paluuaalto leikkautuu (rivi 10)
  dsp.push(kpa, raw);
  TEST_ASSERT_TRUE(dsp.channel(0).saturated);
  TEST_ASSERT_FALSE(dsp.channel(1).saturated);
  raw.mv[0] = 2600;
  for (int i = 0; i < 999; ++i) dsp.push(kpa, raw);
  TEST_ASSERT_TRUE(dsp.channel(0).saturated);  // 999 ms myöhemmin vielä 1 s ikkunassa
  dsp.push(kpa, raw);
  TEST_ASSERT_FALSE(dsp.channel(0).saturated);  // 1000 ms = ikkunan ulkopuolella
}

// --- konfiguraatio --------------------------------------------------------------

void test_configure_resets_state() {
  g_rig.begin(idleParams());
  g_rig.run(3.0f);
  TEST_ASSERT_TRUE(g_rig.dsp.channel(0).has_pulse);
  TEST_ASSERT_GREATER_THAN(10, static_cast<int>(g_rig.dsp.channel(0).cycles));
  g_rig.dsp.configure(DspParams(), SensorParams());
  for (int c = 0; c < kChannels; ++c) {
    const ChannelState& s = g_rig.dsp.channel(c);
    TEST_ASSERT_FALSE(s.has_pulse);
    TEST_ASSERT_EQUAL_UINT32(0, s.cycles);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.rpm);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.mean_kpa);
  }
  TEST_ASSERT_FALSE(g_rig.dsp.rpm().valid);
  // reset() samoin, ja mittaus toipuu normaalisti.
  g_rig.run(2.0f);
  TEST_ASSERT_TRUE(g_rig.dsp.channel(0).has_pulse);
  g_rig.dsp.reset();
  TEST_ASSERT_FALSE(g_rig.dsp.channel(0).has_pulse);
  TEST_ASSERT_EQUAL_UINT32(0, g_rig.dsp.channel(0).cycles);
  g_rig.run(2.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, g_rig.syn.trueMean(1), g_rig.dsp.channel(1).mean_kpa);
}

// Montako sykliä kanavan 1 keskiarvo tarvitsee asettuakseen 2 kPa:n
// synkronointimuutoksen jälkeen (säätöruuvin kääntö).
int cyclesToSettle(uint8_t avg_cycles) {
  SynthParams p = idleParams();
  p.jitter = 0.0f;
  DspParams dp;
  dp.avg_cycles = avg_cycles;
  g_rig.begin(p, dp);
  g_rig.run(6.0f);
  p.sync_err_kpa[1] += 2.0f;
  g_rig.syn.setParams(p);
  const uint32_t c0 = g_rig.dsp.channel(1).cycles;
  for (int i = 0; i < 8000; ++i) {
    g_rig.step();
    const ChannelState& s = g_rig.dsp.channel(1);
    if (std::fabs(s.mean_kpa - g_rig.syn.trueMean(1)) < 0.1f) return static_cast<int>(s.cycles - c0);
  }
  return -1;
}

float meanNoise(uint8_t avg_cycles) {
  SynthParams p = idleParams();
  p.noise_kpa = 1.0f;  // tarkoituksella suuri, jotta ero erottuu
  DspParams dp;
  dp.avg_cycles = avg_cycles;
  g_rig.begin(p, dp);
  g_rig.run(3.0f);
  float s = 0.0f, s2 = 0.0f;
  int n = 0;
  uint32_t last = g_rig.dsp.channel(0).cycles;
  for (int i = 0; i < 20000; ++i) {
    g_rig.step();
    const ChannelState& c = g_rig.dsp.channel(0);
    if (c.cycles == last) continue;
    last = c.cycles;
    const float e = c.mean_kpa - g_rig.syn.trueMean(0);
    s += e;
    s2 += e * e;
    ++n;
  }
  const float mu = s / static_cast<float>(n);
  return std::sqrt(s2 / static_cast<float>(n) - mu * mu);
}

void test_avg_cycles_sets_response_and_noise() {
  const int n1 = cyclesToSettle(1);
  const int n8 = cyclesToSettle(8);
  const int n32 = cyclesToSettle(32);
  const int n0 = cyclesToSettle(0);      // 0 -> rajataan 1:een
  const int n255 = cyclesToSettle(255);  // yli kapasiteetin -> 32
  msg("asettuminen sykleinä: avg 1 -> %.0f, 8 -> %.0f, 32 -> %.0f, 0 -> %.0f, 255 -> %.0f",
      static_cast<double>(n1), static_cast<double>(n8), static_cast<double>(n32), static_cast<double>(n0),
      static_cast<double>(n255));
  // 0,1 kPa:n päähän 2 kPa:n askeleesta = 95 % uusista sykleistä, eli
  // ceil(0,95 N) kokonaista sykliä; askel osuu kesken syklin, joten +0...1.
  TEST_ASSERT_GREATER_OR_EQUAL(1, n1);
  TEST_ASSERT_LESS_OR_EQUAL(2, n1);
  TEST_ASSERT_GREATER_OR_EQUAL(8, n8);
  TEST_ASSERT_LESS_OR_EQUAL(9, n8);
  TEST_ASSERT_GREATER_OR_EQUAL(31, n32);
  TEST_ASSERT_LESS_OR_EQUAL(32, n32);
  TEST_ASSERT_EQUAL_INT(n1, n0);
  TEST_ASSERT_EQUAL_INT(n32, n255);

  const float sd1 = meanNoise(1);
  const float sd16 = meanNoise(16);
  msg("keskiarvon hajonta kohinalla 1 kPa: avg 1 -> %.4f, avg 16 -> %.4f kPa", static_cast<double>(sd1),
      static_cast<double>(sd16));
  // Valkoisella kohinalla hajonta ~ 1/sqrt(N): 16 sykliä -> ~4x pienempi.
  TEST_ASSERT_GREATER_THAN_FLOAT(2.5f * sd16, sd1);
}

void test_filter_cutoff_changes_min_p2p_but_not_mean() {
  // D1: säätöluku lasketaan suodattamattomasta signaalista, joten
  // alipäästön rajataajuus ei saa siirtää keskiarvoa - vain diagnostiikan
  // min/p2p:tä, jotka tulevat suodatetusta signaalista.
  SynthParams p = idleParams();
  p.rpm = 1500.0f;
  DspParams dp;
  float mean[2], p2p[2], mn[2];
  const float cut[2] = {40.0f, 12.0f};
  for (int k = 0; k < 2; ++k) {
    dp.lp_cutoff_hz = cut[k];
    g_rig.begin(p, dp);
    g_rig.run(3.0f);
    TEST_ASSERT_TRUE(g_rig.dsp.channel(0).has_pulse);
    mean[k] = g_rig.dsp.channel(0).mean_kpa;
    p2p[k] = g_rig.dsp.channel(0).p2p_kpa;
    mn[k] = g_rig.dsp.channel(0).min_kpa;
  }
  msg("lp 40 Hz: p2p %.2f min %.2f ka %.3f | lp 12 Hz: p2p %.2f min %.2f ka %.3f",
      static_cast<double>(p2p[0]), static_cast<double>(mn[0]), static_cast<double>(mean[0]),
      static_cast<double>(p2p[1]), static_cast<double>(mn[1]), static_cast<double>(mean[1]));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, mean[0], mean[1]);
  TEST_ASSERT_LESS_THAN_FLOAT(0.7f * p2p[0], p2p[1]);  // 1500 rpm: 12,5 Hz perustaajuus vaimenee
  TEST_ASSERT_GREATER_THAN_FLOAT(mn[0] + 2.0f, mn[1]);  // pohja nousee
}

void test_hyst_min_sets_pulse_detection_floor() {
  // Pienin hyväksyttävä pulssi = 3 x hyst_min_kpa (kohinasuoja, rivit 1/6).
  SynthParams p = idleParams();
  p.depth_kpa = 4.0f;  // suodatettu p2p ~4 kPa
  p.res_kpa = 0.5f;
  DspParams dp;
  g_rig.begin(p, dp);  // oletus 0,3 kPa -> raja 0,9 kPa
  g_rig.run(3.0f);
  TEST_ASSERT_TRUE(g_rig.dsp.channel(0).has_pulse);
  dp.hyst_min_kpa = 2.0f;  // raja 6 kPa
  g_rig.begin(p, dp);
  g_rig.run(3.0f);
  for (int c = 0; c < kChannels; ++c) TEST_ASSERT_FALSE(g_rig.dsp.channel(c).has_pulse);
}

void test_pulse_timeout_parameter() {
  SynthParams p = idleParams();
  DspParams dp;
  dp.pulse_timeout_s = 0.3f;
  g_rig.begin(p, dp);
  g_rig.run(3.0f);
  p.engine_on = false;
  g_rig.syn.setParams(p);
  uint32_t prev = g_rig.dsp.channel(0).cycles;
  int last = 0;
  int fell = -1;
  for (int i = 0; i < 1000 && fell < 0; ++i) {
    g_rig.step();
    const ChannelState& s = g_rig.dsp.channel(0);
    if (s.cycles != prev) {
      prev = s.cycles;
      last = i;
    }
    if (!s.has_pulse) fell = i;
  }
  // Pysähdyksen jälkeen viimeinen sykli voi vielä valmistua (suotimen viive),
  // joten last >= 0 ei ole varma; pudotus on kuitenkin 300 ms viimeisestä.
  TEST_ASSERT_GREATER_THAN(0, fell);
  TEST_ASSERT_LESS_OR_EQUAL(300, fell);
  if (last > 0) TEST_ASSERT_EQUAL_INT(last + 300, fell);
}

void test_rpm_limits_reject_out_of_range_periods() {
  SynthParams p = idleParams();
  DspParams dp;
  dp.rpm_max = 1000.0f;
  p.rpm = 1500.0f;  // 50 % yli ylärajan
  g_rig.begin(p, dp);
  g_rig.run(3.0f);
  for (int c = 0; c < kChannels; ++c) TEST_ASSERT_FALSE(g_rig.dsp.channel(c).has_pulse);

  dp = DspParams();
  dp.rpm_min = 1000.0f;
  p.rpm = 600.0f;  // 40 % alle alarajan
  g_rig.begin(p, dp);
  g_rig.run(3.0f);
  for (int c = 0; c < kChannels; ++c) TEST_ASSERT_FALSE(g_rig.dsp.channel(c).has_pulse);

  // Täsmälleen rajalla (jitterin kanssa) pitää toimia - toleranssi.
  dp = DspParams();
  dp.rpm_max = 1500.0f;
  p.rpm = 1500.0f;
  p.jitter = 0.01f;
  g_rig.begin(p, dp);
  g_rig.run(2.0f);
  const Accuracy a = measure(g_rig, 3.0f);
  TEST_ASSERT_TRUE(a.always_pulse);
  TEST_ASSERT_LESS_THAN_FLOAT(0.01f, a.rpm_err_rel);
}

// --- RpmEstimate ------------------------------------------------------------------

void test_rpm_median_with_hose_off_and_validity() {
  SynthParams p = idleParams();
  p.hose_off[2] = true;  // rivi 6: kanava 3 näkee ilmanpaineen
  DspParams dp;
  dp.rpm_hist = 16;
  g_rig.begin(p, dp);
  // valid vasta kun rpm_hist/2 = 8 jaksoa on kertynyt.
  bool seen_pulse_invalid = false;
  int valid_at_cycles = -1;
  for (int i = 0; i < 4000 && valid_at_cycles < 0; ++i) {
    g_rig.step();
    const RpmEstimate e = g_rig.dsp.rpm();
    const ChannelState& s0 = g_rig.dsp.channel(0);
    if (s0.has_pulse && !e.valid) seen_pulse_invalid = true;
    if (e.valid) valid_at_cycles = static_cast<int>(s0.cycles);
  }
  TEST_ASSERT_TRUE(seen_pulse_invalid);
  TEST_ASSERT_INT_WITHIN(1, 8, valid_at_cycles);

  g_rig.run(3.0f);
  const ChannelState& off = g_rig.dsp.channel(2);
  TEST_ASSERT_FALSE(off.has_pulse);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, off.rpm);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, off.level_kpa);
  const RpmEstimate e = g_rig.dsp.rpm();
  TEST_ASSERT_TRUE(e.valid);
  // Kahden kanavan mediaani = niiden keskiarvo.
  const float expect = 0.5f * (g_rig.dsp.channel(0).rpm + g_rig.dsp.channel(1).rpm);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, expect, e.rpm);
  TEST_ASSERT_FLOAT_WITHIN(0.005f * p.rpm, p.rpm, e.rpm);

  // Lyhyt historia -> voimassa jo 2 jakson jälkeen.
  dp.rpm_hist = 4;
  g_rig.begin(p, dp);
  valid_at_cycles = -1;
  for (int i = 0; i < 4000 && valid_at_cycles < 0; ++i) {
    g_rig.step();
    if (g_rig.dsp.rpm().valid) valid_at_cycles = static_cast<int>(g_rig.dsp.channel(0).cycles);
  }
  TEST_ASSERT_INT_WITHIN(1, 2, valid_at_cycles);
}

void test_missed_pulse_does_not_halve_rpm() {
  // Yksi pulssi jää kynnyksen alle (esim. hetkellinen tukos): raja puuttuu ja
  // jakso on 2T = 425 rpm, mikä on rpm-alueella. Ilman suhdetarkistusta
  // 8 syklin keskiarvo putoaisi ~11 %.
  SynthParams p = idleParams();
  g_rig.begin(p);
  g_rig.run(3.0f);
  const float top = p.base_kpa + p.depth_kpa * (2.0f / 3.14159265f) * p.intake_frac;
  const uint32_t c0 = g_rig.dsp.channel(0).cycles;
  while (g_rig.dsp.channel(0).cycles == c0) g_rig.step();
  // Raja tuli juuri; seuraava pulssi alkaa ~1 jakso tämän jälkeen. Peitetään
  // kanavan 0 signaali yhden jakson ajaksi pulssin yläreunan tasoon.
  float worst = 0.0f;
  float kpa[kChannels];
  for (int i = 0; i < 3000; ++i) {
    g_rig.syn.next(kpa, g_rig.raw);
    if (i >= 60 && i < 60 + 141) kpa[0] = top;
    g_rig.dsp.push(kpa, g_rig.raw);
    const ChannelState& s = g_rig.dsp.channel(0);
    TEST_ASSERT_TRUE(s.has_pulse);
    const float e = std::fabs(s.rpm - p.rpm) / p.rpm;
    if (e > worst) worst = e;
  }
  msg("puuttuva pulssi: kanavan rpm-virhe max %.3f %%", static_cast<double>(worst * 100.0f));
  TEST_ASSERT_LESS_THAN_FLOAT(0.01f, worst);
}

// --- kohinalattia ------------------------------------------------------------------

void test_displayed_mean_noise_floor_at_idle() {
  // D2: vihreän raja 1,5 kPa. Näytetyn luvun oma heilunta joutokäynnillä
  // (1 % jitter, resonanssi, kohina) on oltava siitä murto-osa, muuten
  // tuomio välkkyisi rajalla.
  SynthParams p = idleParams();
  p.jitter = 0.01f;
  g_rig.begin(p);
  g_rig.run(3.0f);
  float s[kChannels] = {0, 0, 0}, s2[kChannels] = {0, 0, 0};
  float d = 0.0f, d2 = 0.0f;
  int n = 0;
  for (int i = 0; i < 10000; ++i) {
    g_rig.step();
    if (i % 20 != 0) continue;  // ~näyttötahti
    for (int c = 0; c < kChannels; ++c) {
      // Poikkeama totuudesta, ei raaka -55 kPa: float-varianssi kumoutuisi.
      const float v = g_rig.dsp.channel(c).mean_kpa - g_rig.syn.trueMean(c);
      s[c] += v;
      s2[c] += v * v;
    }
    const float delta = g_rig.dsp.channel(0).mean_kpa - g_rig.dsp.channel(1).mean_kpa -
                        (g_rig.syn.trueMean(0) - g_rig.syn.trueMean(1));
    d += delta;
    d2 += delta * delta;
    ++n;
  }
  const float fn = static_cast<float>(n);
  float worst = 0.0f;
  for (int c = 0; c < kChannels; ++c) {
    const float mu = s[c] / fn;
    const float var = s2[c] / fn - mu * mu;
    const float sd = std::sqrt(var > 0.0f ? var : 0.0f);
    if (sd > worst) worst = sd;
  }
  const float dmu = d / fn;
  const float dvar = d2 / fn - dmu * dmu;
  const float dsd = std::sqrt(dvar > 0.0f ? dvar : 0.0f);
  msg("näytetyn keskiarvon hajonta joutokäynnillä: kanava max %.4f kPa, D3-ero %.4f kPa",
      static_cast<double>(worst), static_cast<double>(dsd));
  TEST_ASSERT_LESS_THAN_FLOAT(0.1f, worst);
  TEST_ASSERT_LESS_THAN_FLOAT(0.1f, dsd);
}

// --- vikasieto --------------------------------------------------------------------

void test_nan_sample_does_not_poison_state() {
  g_rig.begin(idleParams());
  g_rig.run(2.0f);
  float kpa[kChannels];
  g_rig.syn.next(kpa, g_rig.raw);
  kpa[1] = std::nanf("");
  g_rig.dsp.push(kpa, g_rig.raw);
  g_rig.run(2.0f);
  const ChannelState& s = g_rig.dsp.channel(1);
  TEST_ASSERT_TRUE(std::isfinite(s.level_kpa));
  TEST_ASSERT_TRUE(std::isfinite(s.mean_kpa));
  TEST_ASSERT_TRUE(s.has_pulse);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, g_rig.syn.trueMean(1), s.mean_kpa);
  // Virheellinen kanavaindeksi ei kaadu (palauttaa kanavan 0).
  TEST_ASSERT_EQUAL_FLOAT(0.0f, g_rig.dsp.channel(7).level_kpa - g_rig.dsp.channel(0).level_kpa);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lowpass_butterworth_response);
  RUN_TEST(test_accuracy_idle_to_3000_rpm);
  RUN_TEST(test_accuracy_6000_rpm);
  RUN_TEST(test_fractional_boundary_single_cycle_mean);
  RUN_TEST(test_crosstalk_does_not_triple_rpm);
  RUN_TEST(test_cv_follows_jitter_5pct);
  RUN_TEST(test_cv_small_with_jitter_half_pct);
  RUN_TEST(test_has_pulse_within_three_cycles_of_plug_in);
  RUN_TEST(test_has_pulse_after_engine_start_from_atmosphere);
  RUN_TEST(test_pulse_timeout_when_signal_goes_flat);
  RUN_TEST(test_noise_and_resonance_alone_never_pulse);
  RUN_TEST(test_level_ema_tracks_step_with_tau);
  RUN_TEST(test_sensor_fault_after_hold_and_clears);
  RUN_TEST(test_saturation_flag_lasts_one_second);
  RUN_TEST(test_configure_resets_state);
  RUN_TEST(test_avg_cycles_sets_response_and_noise);
  RUN_TEST(test_filter_cutoff_changes_min_p2p_but_not_mean);
  RUN_TEST(test_hyst_min_sets_pulse_detection_floor);
  RUN_TEST(test_pulse_timeout_parameter);
  RUN_TEST(test_rpm_limits_reject_out_of_range_periods);
  RUN_TEST(test_rpm_median_with_hose_off_and_validity);
  RUN_TEST(test_missed_pulse_does_not_halve_rpm);
  RUN_TEST(test_displayed_mean_noise_floor_at_idle);
  RUN_TEST(test_nan_sample_does_not_poison_state);
  return UNITY_END();
}
