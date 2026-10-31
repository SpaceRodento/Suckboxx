// WP-2: dsp.h -testit.
//
// Totuus tulee test/support/synth_signal.h:sta, jonka jokaisen syklin
// keskiarvo on analyyttisesti tunnettu (base + sync_err) ja joka tallentaa
// toteutuneet imuhetket. Siksi keskiarvo- ja rpm-virheet mitataan
// absoluuttisesti eikä "näyttää järkevältä" -tasolla: rpm verrataan
// toteutuneisiin jaksoihin, ei nimellisarvoon, koska 8 syklin keskiarvon oma
// otantavaihtelu on 1 %:n jitterillä jo samaa luokkaa kuin 0,5 %:n tavoite.
// Oletussignaali: 3 kPa:n letkuresonanssi eri taajuudella joka kanavalla
// (90/135/180 Hz, R1 rikottu tarkoituksella), kohina 0,2 kPa, kanavien
// synkronointivirheet 0 / +1,0 / -0,7 kPa.

#include <unity.h>

#include <cmath>
#include <cstdio>

#include "core/dsp.h"
#include "core/engine_sim.h"
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

float periodMs(float rpm) { return 120000.0f / rpm; }

void msg(const char* fmt, double a = 0.0, double b = 0.0, double c = 0.0, double d = 0.0, double e = 0.0,
         double f = 0.0, double g = 0.0) {
  char buf[200];
  std::snprintf(buf, sizeof(buf), fmt, a, b, c, d, e, f, g);
  TEST_MESSAGE(buf);
}

float median3(float a, float b, float c) {
  if (a > b) {
    const float t = a;
    a = b;
    b = t;
  }
  if (b > c) b = c;
  return a > b ? a : b;
}

// Seuraa hyväksyttyjä syklejä näyte kerrallaan: välit, pulssin katkot ja
// jokaisen syklin hetkellä kanavan rpm toteutunutta vastaan. Hyväksyntä
// tapahtuu, kun syklin päättänyt pulssi on nähty pohjaan asti, jolloin
// viimeisin toteutunut imu on juuri se, joka päätti syklin.
struct Watch {
  int avg_n = 8;
  long n = 0;
  uint32_t prev[kChannels] = {0, 0, 0};
  long last[kChannels] = {-1, -1, -1};
  long max_gap = 0;          // pisin väli kahden hyväksytyn syklin välillä, ms
  bool dropped = false;      // has_pulse putosi jollain kanavalla
  float worst_rpm = 0.0f;    // max |kanavan rpm - toteutunut| / toteutunut
  float truth[kChannels] = {0.0f, 0.0f, 0.0f};
  int records = 0;

  void start(const Rig& r, int avg_cycles) {
    avg_n = avg_cycles;
    n = 0;
    for (int c = 0; c < kChannels; ++c) {
      prev[c] = r.dsp.channel(c).cycles;
      last[c] = 0;
    }
  }
  void sample(const Rig& r) {
    ++n;
    for (int c = 0; c < kChannels; ++c) {
      const ChannelState& s = r.dsp.channel(c);
      if (!s.has_pulse) dropped = true;
      if (s.cycles == prev[c]) continue;
      prev[c] = s.cycles;
      if (n - last[c] > max_gap) max_gap = n - last[c];
      last[c] = n;
      truth[c] = r.syn.realisedRpm(c, avg_n);
      if (truth[c] > 0.0f) {
        const float e = std::fabs(s.rpm - truth[c]) / truth[c];
        if (e > worst_rpm) worst_rpm = e;
        ++records;
      }
    }
  }
};

// Virheiden maksimit mittausjakson aikana (keskiarvot 20 ms välein ~
// näyttötahti, rpm jokaisen syklin hetkellä toteutunutta vastaan).
struct Accuracy {
  float rpm_err_rel = 0.0f;   // RpmEstimate vs kanavien toteutuneiden mediaani
  float ch_rpm_err = 0.0f;    // kanavan rpm vs sen toteutunut
  float nominal_err = 0.0f;   // RpmEstimate vs nimellis-rpm (vain tiedoksi)
  float mean_err = 0.0f;      // max |mean_kpa - totuus| yli kanavien
  float diff_err = 0.0f;      // max virhe kanavaerossa (D3: mitä laite näyttää)
  bool always_pulse = true;
  bool always_valid = true;
};

Accuracy measure(Rig& r, float seconds, int avg_cycles = 8) {
  Accuracy a;
  Watch w;
  w.start(r, avg_cycles);
  const int n = static_cast<int>(seconds * 1000.0f);
  for (int i = 0; i < n; ++i) {
    r.step();
    w.sample(r);
    if (i % 20 != 0) continue;
    const RpmEstimate e = r.dsp.rpm();
    if (!e.valid) a.always_valid = false;
    const float nom = std::fabs(e.rpm - r.syn.trueRpm()) / r.syn.trueRpm();
    if (nom > a.nominal_err) a.nominal_err = nom;
    if (w.truth[0] > 0.0f && w.truth[1] > 0.0f && w.truth[2] > 0.0f) {
      const float t = median3(w.truth[0], w.truth[1], w.truth[2]);
      const float re = std::fabs(e.rpm - t) / t;
      if (re > a.rpm_err_rel) a.rpm_err_rel = re;
    }
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
  a.ch_rpm_err = w.worst_rpm;
  if (w.records == 0) a.ch_rpm_err = 1.0f;  // ei yhtään vertailukelpoista sykliä
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

// Lukitusaika (ms) ja ensimmäisen syklin virheet käyvään signaaliin
// liitettäessä, pahin yli aloitusvaiheiden ja kanavien.
struct Lock {
  int worst_ms = 0;
  float rpm_err = 0.0f;
  float mean_err = 0.0f;
  bool all_locked = true;
  bool first_is_one_cycle = true;
};

Lock plugIn(SynthParams p, int phases, int max_ms) {
  Lock l;
  for (int k = 0; k < phases; ++k) {
    p.start_phase = static_cast<float>(k) / static_cast<float>(phases);
    g_rig.begin(p);
    int first[kChannels] = {-1, -1, -1};
    for (int i = 0; i < max_ms; ++i) {
      g_rig.step();
      for (int c = 0; c < kChannels; ++c) {
        const ChannelState& s = g_rig.dsp.channel(c);
        if (first[c] >= 0 || !s.has_pulse) continue;
        first[c] = i;
        const float re = std::fabs(s.rpm - p.rpm) / p.rpm;
        const float me = std::fabs(s.mean_kpa - g_rig.syn.trueMean(c));
        if (re > l.rpm_err) l.rpm_err = re;
        if (me > l.mean_err) l.mean_err = me;
        if (s.cycles != 1) l.first_is_one_cycle = false;
      }
    }
    for (int c = 0; c < kChannels; ++c) {
      if (first[c] < 0) l.all_locked = false;
      if (first[c] > l.worst_ms) l.worst_ms = first[c];
    }
  }
  return l;
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
  // Dsp::lpGain (pulssirajan skaalaus) vastaa todellista suodinta myös
  // 6000 rpm:n perustaajuudella 50 Hz, jossa vaimennus on jo 0,38.
  Dsp dsp;
  const float freqs[] = {7.0f, 25.0f, 40.0f, 50.0f, 90.0f};
  for (float f : freqs) TEST_ASSERT_FLOAT_WITHIN(0.01f, lpGain(f, 40.0f), dsp.lpGain(f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.38f, dsp.lpGain(50.0f));
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
  // Jitter 1 % (EngineSimin oletus): rpm verrataan toteutuneisiin jaksoihin.
  const float table[][3] = {{600.0f, -58.0f, 22.0f},
                            {850.0f, -55.0f, 20.0f},
                            {1500.0f, -50.0f, 18.0f},
                            {3000.0f, -35.0f, 14.0f}};
  for (const auto& row : table) {
    SynthParams p = idleParams();
    p.rpm = row[0];
    p.base_kpa = row[1];
    p.depth_kpa = row[2];
    p.jitter = 0.01f;
    g_rig.begin(p);
    g_rig.run(3.0f);
    const Accuracy a = measure(g_rig, 8.0f);
    msg("rpm %.0f: rpm-virhe %.3f %% (kanava %.3f %%, nimellisestä %.2f %%), keskiarvo %.3f kPa, D3-ero %.3f kPa",
        static_cast<double>(row[0]), static_cast<double>(a.rpm_err_rel * 100.0f),
        static_cast<double>(a.ch_rpm_err * 100.0f), static_cast<double>(a.nominal_err * 100.0f),
        static_cast<double>(a.mean_err), static_cast<double>(a.diff_err));
    TEST_ASSERT_TRUE_MESSAGE(a.always_pulse, "pulssi katosi kesken");
    TEST_ASSERT_TRUE_MESSAGE(a.always_valid, "rpm ei voimassa");
    TEST_ASSERT_LESS_THAN_FLOAT(0.005f, a.rpm_err_rel);
    TEST_ASSERT_LESS_THAN_FLOAT(0.005f, a.ch_rpm_err);
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
  p.jitter = 0.01f;
  g_rig.begin(p);
  g_rig.run(2.0f);
  const Accuracy a = measure(g_rig, 8.0f);
  msg("rpm 6000: rpm-virhe %.3f %% (kanava %.3f %%), keskiarvo %.3f kPa, D3-ero %.3f kPa",
      static_cast<double>(a.rpm_err_rel * 100.0f), static_cast<double>(a.ch_rpm_err * 100.0f),
      static_cast<double>(a.mean_err), static_cast<double>(a.diff_err));
  TEST_ASSERT_TRUE(a.always_pulse);
  TEST_ASSERT_TRUE(a.always_valid);
  TEST_ASSERT_LESS_THAN_FLOAT(0.01f, a.rpm_err_rel);
  TEST_ASSERT_LESS_THAN_FLOAT(0.5f, a.mean_err);
}

void test_high_rpm_small_pulses_detected() {
  // Täydellä kaasulla alipaine on pieni (SUUNNITTELU §2.4: ~-8 kPa) ja
  // pulssi samoin. 40 Hz:n alipäästö vaimentaa 6000 rpm:n perustaajuuden
  // 0,38-kertaiseksi ja ristivuoto vielä (1 - xtalk):lla, joten 6 kPa:n
  // pulssi näkyy suodatettuna vain ~0,7 kPa:na - alle kiinteän 0,9 kPa:n
  // rajan. Rivi 9 (D13) vaatii silti rpm:n näkyviin. Rivit: rpm, pulssin
  // syvyys, xtalk, imutahdin osuus (katselmoinnin EngineSim-tapaukset, jotka
  // hylättiin aiemmin kokonaan).
  const float cases[][4] = {{4500.0f, 4.0f, 0.3f, 0.2f}, {6000.0f, 6.0f, 0.3f, 0.25f}, {6000.0f, 5.0f, 0.0f, 0.25f}};
  for (const auto& row : cases) {
    SynthParams p = idleParams();
    p.rpm = row[0];
    p.depth_kpa = row[1];
    p.xtalk = row[2];
    p.intake_frac = row[3];
    p.base_kpa = -10.0f;
    p.jitter = 0.005f;
    p.noise_kpa = 0.1f;
    for (int c = 0; c < kChannels; ++c) p.res_hz[c] = 112.0f;  // docs/rauta.md §4.1
    g_rig.begin(p);
    g_rig.run(1.5f);
    const uint32_t c0 = g_rig.dsp.channel(0).cycles;
    const uint32_t e0 = g_rig.syn.cyclesStarted();
    const Accuracy a = measure(g_rig, 5.0f);
    const int counted = static_cast<int>(g_rig.dsp.channel(0).cycles - c0);
    const int engine = static_cast<int>(g_rig.syn.cyclesStarted() - e0);
    msg("%.0f rpm, pulssi %.1f kPa, xtalk %.1f: suodatettu p2p %.2f kPa, sykleistä %.0f / %.0f, rpm-virhe %.2f %%",
        static_cast<double>(row[0]), static_cast<double>(row[1]), static_cast<double>(row[2]),
        static_cast<double>(g_rig.dsp.channel(0).p2p_kpa), static_cast<double>(counted),
        static_cast<double>(engine), static_cast<double>(a.rpm_err_rel * 100.0f));
    TEST_ASSERT_TRUE(a.always_pulse);
    TEST_ASSERT_TRUE(a.always_valid);
    TEST_ASSERT_LESS_THAN_FLOAT(0.9f, g_rig.dsp.channel(0).p2p_kpa);  // todella kiinteän rajan alla
    TEST_ASSERT_GREATER_OR_EQUAL(engine * 97 / 100, counted);
    TEST_ASSERT_LESS_THAN_FLOAT(0.015f, a.rpm_err_rel);
  }
}

void test_amplitude_gate_scales_with_filter_gain() {
  // Sama ~0,8 kPa:n suodatettu pulssi: 6000 rpm:ssä (|H(50 Hz)| = 0,38)
  // se on oikea pulssi, mutta 1500 rpm:ssä (|H(12,5 Hz)| = 1) se on alle
  // kohinarajan 3 x hyst_min_kpa = 0,9 kPa. Raja koskee siis
  // suodattamatonta perustaajuutta, ei suodatettua p2p:tä.
  SynthParams p = idleParams();
  p.base_kpa = -10.0f;
  p.noise_kpa = 0.05f;
  p.res_kpa = 0.0f;
  p.rpm = 6000.0f;
  p.depth_kpa = 3.5f;
  g_rig.begin(p);
  g_rig.run(2.0f);
  const float p2p_hi = g_rig.dsp.channel(0).p2p_kpa;
  TEST_ASSERT_TRUE(g_rig.dsp.channel(0).has_pulse);
  TEST_ASSERT_LESS_THAN_FLOAT(0.9f, p2p_hi);

  p.rpm = 1500.0f;
  p.depth_kpa = 0.8f;  // suodatettu ~0,8 kPa, kuten yllä
  g_rig.begin(p);
  g_rig.run(3.0f);
  for (int c = 0; c < kChannels; ++c) TEST_ASSERT_FALSE(g_rig.dsp.channel(c).has_pulse);
  // Sama 1500 rpm:n signaali kelpaa, kun raja lasketaan (hyst_min 0,2 -> 0,6 kPa).
  DspParams dp;
  dp.hyst_min_kpa = 0.2f;
  g_rig.begin(p, dp);
  g_rig.run(3.0f);
  TEST_ASSERT_TRUE(g_rig.dsp.channel(0).has_pulse);
  msg("6000 rpm: p2p %.2f kPa hyväksytty; 1500 rpm: p2p %.2f kPa hyväksytty vain alemmalla rajalla",
      static_cast<double>(p2p_hi), static_cast<double>(g_rig.dsp.channel(0).p2p_kpa));
  TEST_ASSERT_LESS_THAN_FLOAT(0.9f, g_rig.dsp.channel(0).p2p_kpa);
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

void test_d1_mean_is_not_delayed_by_lowpass() {
  // D1 lasketaan suodattamattomista näytteistä: suodatin päättää vain,
  // MISSÄ syklin rajat ovat. Jaksollisella signaalilla suodatetun ja
  // suodattamattoman syklikeskiarvon ero on nolla (DC-vahvistus 1), joten
  // ero näkyy vain tason muuttuessa: suodatettu integraali jäisi
  // ryhmäviiveen verran jälkeen. Rampilla (25 kPa/s) näytetty keskiarvo on
  // aina jäljessä (syklin puoliväli + vahvistusviive); kun rajataajuus
  // puolitetaan 40 -> 20 Hz, viive saa kasvaa vain rajantunnistuksen
  // ajoituksen verran, ei ryhmäviiveen erotuksen 2,613/(2 pi) x (1/20 - 1/40)
  // = 10,4 ms verran (mitattu: 4,4 ms; suodatetusta integroiva 14,9 ms).
  const float rate = 25.0f;  // kPa/s
  float lag_ms[2];
  const float cut[2] = {40.0f, 20.0f};
  for (int k = 0; k < 2; ++k) {
    SynthParams p = idleParams();
    p.jitter = 0.0f;
    p.noise_kpa = 0.05f;
    p.base_kpa = -70.0f;
    for (int c = 0; c < kChannels; ++c) p.res_hz[c] = 112.0f;
    DspParams dp;
    dp.lp_cutoff_hz = cut[k];
    dp.avg_cycles = 1;
    g_rig.begin(p, dp);
    g_rig.run(2.0f);
    uint32_t prev = g_rig.dsp.channel(0).cycles;
    double sum = 0.0;
    int n = 0;
    for (int i = 0; i < 1600; ++i) {
      p.base_kpa = -70.0f + rate * 0.001f * static_cast<float>(i);
      g_rig.syn.setParams(p);
      g_rig.step();
      const ChannelState& s = g_rig.dsp.channel(0);
      if (s.cycles == prev) continue;
      prev = s.cycles;
      if (i < 1200) continue;  // ramppi tasaantunut (yksi sykli on 141 ms)
      sum += static_cast<double>((p.base_kpa - s.mean_kpa) / rate * 1000.0f);
      ++n;
    }
    TEST_ASSERT_GREATER_OR_EQUAL(2, n);
    lag_ms[k] = static_cast<float>(sum / n);
  }
  const float d_group_ms = 2.613f / (2.0f * 3.14159265f) * (1.0f / 20.0f - 1.0f / 40.0f) * 1000.0f;
  msg("keskiarvon viive rampilla: 40 Hz %.1f ms, 20 Hz %.1f ms (ryhmäviiveen ero %.1f ms)",
      static_cast<double>(lag_ms[0]), static_cast<double>(lag_ms[1]), static_cast<double>(d_group_ms));
  TEST_ASSERT_LESS_THAN_FLOAT(0.75f * d_group_ms, lag_ms[1] - lag_ms[0]);
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

// --- has_pulse ja lukitus --------------------------------------------------------

void test_has_pulse_within_three_cycles_of_plug_in() {
  // DSP käynnistyy käyvän moottorin signaaliin satunnaisessa vaiheessa, myös
  // keskeltä vuotopulssia (xtalk). Ensimmäisen hyväksytyn syklin on oltava
  // oikea: väärä ensimmäinen sykli näkyisi avg_cycles syklin ajan
  // keskiarvossa. Lukitus vaatii kolme samankokoista pulssia ja suotimen
  // asettumisen (37,5 ms), joten raja on 3,5 sykliä + 40 ms: suurilla
  // kierroksilla asettuminen on jo ~1 sykli (3000 rpm: mitattu 4,1 sykliä =
  // 164 ms), joutokäynnillä ~3,3 sykliä.
  const float rpms[] = {600.0f, 850.0f, 1500.0f, 3000.0f};
  for (float rpm : rpms) {
    Lock worst;
    const float xt[] = {0.0f, 0.3f};
    for (float x : xt) {
      SynthParams p = idleParams();
      p.rpm = rpm;
      p.xtalk = x;
      const Lock l = plugIn(p, 16, static_cast<int>(5.0f * periodMs(rpm)) + 100);
      TEST_ASSERT_TRUE(l.all_locked);
      TEST_ASSERT_TRUE(l.first_is_one_cycle);
      if (l.worst_ms > worst.worst_ms) worst.worst_ms = l.worst_ms;
      if (l.rpm_err > worst.rpm_err) worst.rpm_err = l.rpm_err;
      if (l.mean_err > worst.mean_err) worst.mean_err = l.mean_err;
    }
    msg("lukitus %.0f rpm: pahin %.0f ms = %.2f sykliä; 1. syklin rpm-virhe %.2f %%, keskiarvo %.3f kPa",
        static_cast<double>(rpm), static_cast<double>(worst.worst_ms),
        static_cast<double>(static_cast<float>(worst.worst_ms) / periodMs(rpm)),
        static_cast<double>(worst.rpm_err * 100.0f), static_cast<double>(worst.mean_err));
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(3.5f * periodMs(rpm)) + 40, worst.worst_ms);
    TEST_ASSERT_LESS_THAN_FLOAT(0.015f, worst.rpm_err);
    TEST_ASSERT_LESS_THAN_FLOAT(0.15f, worst.mean_err);
  }
}

void test_startup_on_resonance_with_small_pulse() {
  // Suotimen esitäyttö (prime) ja asettumisaika: ensimmäinen näyte osuu
  // satunnaiseen kohtaan voimakasta (6 kPa) letkuresonanssia, ja pulssi on
  // pieni (2,5 kPa). Ilman esitäyttöä suodin lähtisi nollasta (-55 kPa:n
  // askel) ja ilman asettumisaikaa sen soiminen kasvattaisi ensimmäistä
  // amplitudiarviota: mitattu ilman asettumista lukitus 7-25 sykliä ja
  // ensimmäisen syklin rpm 4-15 % pielessä, ilman esitäyttöä 1,4-2,3 %.
  const float rpms[] = {600.0f, 850.0f, 1500.0f};
  for (float rpm : rpms) {
    SynthParams p = idleParams();
    p.rpm = rpm;
    p.depth_kpa = 2.5f;
    p.res_kpa = 6.0f;
    p.noise_kpa = 0.1f;
    p.seed = 3;
    const Lock l = plugIn(p, 25, static_cast<int>(5.0f * periodMs(rpm)) + 100);
    msg("resonanssi 6 kPa, pulssi 2,5 kPa, %.0f rpm: lukitus %.2f sykliä, 1. sykli rpm %.2f %%, ka %.3f kPa",
        static_cast<double>(rpm), static_cast<double>(static_cast<float>(l.worst_ms) / periodMs(rpm)),
        static_cast<double>(l.rpm_err * 100.0f), static_cast<double>(l.mean_err));
    TEST_ASSERT_TRUE(l.all_locked);
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(3.5f * periodMs(rpm)) + 40, l.worst_ms);
    TEST_ASSERT_LESS_THAN_FLOAT(0.0125f, l.rpm_err);
    TEST_ASSERT_LESS_THAN_FLOAT(0.05f, l.mean_err);
  }
}

void test_has_pulse_after_engine_start_from_atmosphere() {
  // Moottori seis (ilmanpaine + kohina) -> käynnistys suoraan -55 kPa:iin.
  // Askel on 2,5x pulssin korkuinen; absoluuttinen verhokäyrä jäi siihen
  // kiinni vanhenemisajaksi (~0,9 s). Amplitudiarvio haussa = suurin nousu,
  // jota alipaineen kasvu ei kasvata, joten lukitus on sama kuin käyvään
  // signaaliin liitettäessä. Oikea käynnistys on loivempi (startti
  // pyörittää ensin ~300 rpm).
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
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(3.5f * periodMs(p.rpm)) + 40, first);
    if (first > worst) worst = first;
    g_rig.run(2.0f);
    for (int c = 0; c < kChannels; ++c) {
      TEST_ASSERT_FLOAT_WITHIN(0.01f * p.rpm, p.rpm, g_rig.dsp.channel(c).rpm);
      TEST_ASSERT_FLOAT_WITHIN(0.1f, g_rig.syn.trueMean(c), g_rig.dsp.channel(c).mean_kpa);
    }
  }
  msg("käynnistys ilmanpaineesta: kaikki kanavat pulssivat viimeistään %.0f ms:n jälkeen (%.2f sykliä)",
      static_cast<double>(worst), static_cast<double>(static_cast<float>(worst) / periodMs(850.0f)));
}

// --- tason ja pulssin muutokset käydessä (kaasu, /api/sim) --------------------------

// Muutos kesken lukituksen: has_pulse ei saa pudota, hyväksyttyjen syklien
// väli saa olla enintään max_gap_periods jaksoa, eikä yhtään väärää sykliä
// saa hyväksyä: jokaisen syklin hetkellä kanavan rpm on toteutuneen 2 %:n
// sisällä. Väärä jakso (2T/3, askeleen katkaisema sykli) näkyy 8 syklin
// keskiarvossa 4-8 %:n virheenä; hylätyn syklin jälkeen DSP:n 8 sykliä
// eivät ole enää peräkkäiset, mikä selittää ~1 %:n erot 1 %:n jitterillä.
// Lopuksi keskiarvo on asettunut uuteen totuuteen.
struct Change {
  float rpm, base0, depth0, base1, depth1;
  int ramp_ms;
  float xtalk;
  float max_gap_periods;
};

void checkChange(const Change& ch, int offset_ms = 0, bool verbose = true) {
  SynthParams p = idleParams();
  p.rpm = ch.rpm;
  p.base_kpa = ch.base0;
  p.depth_kpa = ch.depth0;
  p.xtalk = ch.xtalk;
  p.jitter = 0.01f;
  g_rig.begin(p);
  g_rig.run(1.5f + 0.001f * static_cast<float>(offset_ms));
  // Seuranta alkaa jo ennen muutosta, jotta muutoksen yli ulottuva väli
  // mitataan edellisestä hyväksytystä syklistä asti.
  Watch w;
  w.start(g_rig, 8);
  for (int i = 0; i < 1000; ++i) {
    g_rig.step();
    w.sample(g_rig);
  }
  const int ramp = ch.ramp_ms > 0 ? ch.ramp_ms : 1;
  for (int i = 1; i <= ramp; ++i) {
    const float a = static_cast<float>(i) / static_cast<float>(ramp);
    p.base_kpa = ch.base0 + (ch.base1 - ch.base0) * a;
    p.depth_kpa = ch.depth0 + (ch.depth1 - ch.depth0) * a;
    g_rig.syn.setParams(p);
    g_rig.step();
    w.sample(g_rig);
  }
  for (int i = 0; i < 2000; ++i) {
    g_rig.step();
    w.sample(g_rig);
  }
  float mean_err = 0.0f;
  for (int c = 0; c < kChannels; ++c) {
    const float e = std::fabs(g_rig.dsp.channel(c).mean_kpa - g_rig.syn.trueMean(c));
    if (e > mean_err) mean_err = e;
  }
  const float gap = static_cast<float>(w.max_gap) / periodMs(ch.rpm);
  if (verbose) {
    msg("%.0f rpm %.0f/%.0f -> %.0f/%.0f kPa", static_cast<double>(ch.rpm), static_cast<double>(ch.base0),
        static_cast<double>(ch.depth0), static_cast<double>(ch.base1), static_cast<double>(ch.depth1));
    msg("  ramppi %.0f ms, xtalk %.1f: pisin väli %.2f jaksoa, rpm-virhe max %.2f %%, keskiarvo lopuksi %.3f kPa",
        static_cast<double>(ch.ramp_ms), static_cast<double>(ch.xtalk), static_cast<double>(gap),
        static_cast<double>(w.worst_rpm * 100.0f), static_cast<double>(mean_err));
  }
  TEST_ASSERT_FALSE_MESSAGE(w.dropped, "has_pulse putosi");
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(ch.max_gap_periods * periodMs(ch.rpm)), static_cast<int>(w.max_gap));
  TEST_ASSERT_LESS_THAN_FLOAT(0.02f, w.worst_rpm);
  TEST_ASSERT_LESS_THAN_FLOAT(0.1f, mean_err);
}

void test_vacuum_steps_and_ramps_keep_lock() {
  // Absoluuttiset kynnykset jumittuivat 0,8-1 s:ksi, kun taso siirtyi yli
  // ~0,35 x p2p syklin aikana (600 rpm: has_pulse putosi). Suhteellinen
  // Schmitt seuraa tasoa syklin sisällä, joten rampit (kaasun liike) eivät
  // maksa yhtään sykliä. Hetkellinen askel (/api/sim) voi osua pulssin
  // kohdalle: ylöspäin se kumoaa osan pulssin laskusta (pulssi jää
  // kynnyksen alle, 1 sykli), alaspäin se voi yhdessä vuotopulssin kanssa
  // laukaista rajan itse (askelsykli ja siitä alkava hylätään, 2 sykliä).
  // Väli on siis askeleella enintään ~3 jaksoa, rampilla ~1, eikä has_pulse
  // putoa kummassakaan (pulse_timeout_s 1 s = 5 jaksoa 600 rpm:ssä).
  const Change cases[] = {
      {600.0f, -58.0f, 22.0f, -50.0f, 18.0f, 0, 0.0f, 3.2f},     // askel ylös 8 kPa (katselmointi)
      {600.0f, -58.0f, 22.0f, -45.0f, 18.0f, 200, 0.0f, 2.2f},   // 13 kPa / 200 ms
      {600.0f, -58.0f, 22.0f, -43.0f, 22.0f, 0, 0.3f, 3.2f},     // askel ylös 15 kPa, ristivuoto
      {600.0f, -45.0f, 22.0f, -55.0f, 22.0f, 0, 0.0f, 3.2f},     // askel alas 10 kPa
      {600.0f, -43.0f, 22.0f, -58.0f, 22.0f, 0, 0.0f, 3.2f},     // askel alas 15 kPa
      {850.0f, -55.0f, 20.0f, -20.0f, 12.0f, 1000, 0.0f, 2.2f},  // kaasu auki 1 s
      {850.0f, -20.0f, 12.0f, -55.0f, 20.0f, 700, 0.3f, 2.2f},   // kaasu kiinni 0,7 s
      {850.0f, -55.0f, 20.0f, -40.0f, 20.0f, 300, 0.3f, 2.2f},   // 15 kPa / 300 ms
      {850.0f, -40.0f, 20.0f, -55.0f, 20.0f, 300, 0.0f, 2.2f},   // -15 kPa / 300 ms
      {850.0f, -55.0f, 20.0f, -45.0f, 20.0f, 0, 0.0f, 3.2f},     // askel ylös 10 kPa
      {850.0f, -45.0f, 20.0f, -55.0f, 20.0f, 0, 0.3f, 3.2f},     // askel alas 10 kPa
      {850.0f, -40.0f, 20.0f, -55.0f, 20.0f, 0, 0.0f, 3.2f},     // askel alas 15 kPa
  };
  for (const Change& ch : cases) checkChange(ch);
}

void test_vacuum_step_at_any_phase_accepts_no_wrong_cycle() {
  // Alipaineen kasvu ristivuodon kanssa: askel + vuotopulssi ylittää
  // kynnyksen satunnaisessa kohdassa sykliä. Raja jakaa oikean syklin
  // kahteen osaan (tau + T - tau). Kumpaakaan ei saa hyväksyä: lyhyt jäännös
  // hylätään jaksosuhteella, ja jos jäännös ei ole lyhyt, askeleen "pulssi"
  // ei nouse takaisin (nousu < 0,7 x lasku). Askeleen hetki käydään läpi
  // koko syklin yli 12 kohdassa, myös askel ylöspäin (pulssin peittyminen).
  const Change cases[] = {
      {850.0f, -45.0f, 20.0f, -55.0f, 20.0f, 0, 0.3f, 3.2f},
      {600.0f, -43.0f, 22.0f, -58.0f, 22.0f, 0, 0.3f, 3.2f},
      {850.0f, -55.0f, 20.0f, -45.0f, 20.0f, 0, 0.0f, 3.2f},
  };
  for (const Change& ch : cases) {
    const int period = static_cast<int>(periodMs(ch.rpm));
    for (int k = 0; k < 12; ++k) checkChange(ch, k * period / 12, false);
  }
}

void test_pulse_depth_changes_keep_lock() {
  // Pulssin puolittuminen kerralla: seuraava pulssi jää 0,65 x A -kynnyksen
  // alle, ja amplitudiarvio vanhenee 1,8 jakson jälkeen -> väli 3 jaksoa.
  // Syveneminen 2,5-kertaiseksi ristivuodon kanssa: vuotopulssit kasvavat
  // vanhan kynnyksen yli ennen kuin uutta omaa pulssia on nähty, ja 2T/3:n
  // "sykli" hylätään (kSplitSuspect), eikä rpm hyppää ~4 %.
  const Change cases[] = {
      {850.0f, -55.0f, 20.0f, -55.0f, 10.0f, 0, 0.0f, 3.2f},
      {850.0f, -55.0f, 10.0f, -55.0f, 25.0f, 0, 0.3f, 3.2f},
      {600.0f, -58.0f, 22.0f, -58.0f, 12.0f, 0, 0.3f, 3.2f},
  };
  for (const Change& ch : cases) checkChange(ch);
}

// --- kierrosluvun muutokset EngineSimillä (laitteen oma simulaattori) ------------------
//
// SynthSignal luo imuhetket ~2 sykliä etukäteen, joten sen kierrosluku seuraa
// käskyä viiveellä, ja kaasunlisäys näytti DSP:lle loivemmalta kuin laitteen
// oletuslähde (D14). Nämä testit ajavat saman ketjun kuin laite: EngineSim ->
// mV -> (mv - v_zero) / mv_per_kpa -> Dsp, oletusparametrein (resonanssi,
// kohina, 1 %:n jitter).
//
// Totuus jaksoille tulee kaksoissimulaattorista: sama siemen ja samat
// kierroslukukomennot, mutta ilman kohinaa, resonanssia ja ristivuotoa ja
// vakiopulssilla. EngineSim arpoo jitterin samassa järjestyksessä
// parametreista riippumatta, joten kaksosen imut osuvat täsmälleen samoihin
// hetkiin; sen sin^2-kuopan painopiste on imun keskikohta ja pinta-ala kertoo
// leveyden (alku = keskikohta - leveys / 2).
namespace {

struct SimTruth {
  static constexpr int kMaxDips = 1024;
  double mid[kChannels][kMaxDips];
  double start[kChannels][kMaxDips];
  int count[kChannels] = {0, 0, 0};
  double s0[kChannels] = {0.0, 0.0, 0.0}, s1[kChannels] = {0.0, 0.0, 0.0};

  void clear() {
    for (int c = 0; c < kChannels; ++c) {
      count[c] = 0;
      s0[c] = s1[c] = 0.0;
    }
  }

  // Kaksosen näyte t: kuoppa 1000 mV (40 kPa) tasosta 1575 mV (-45 kPa:
  // base -50 miinus pulssimuodon keskiarvo -5).
  void add(const RawFrame& f, long t) {
    for (int c = 0; c < kChannels; ++c) {
      const double d = 1575.0 - static_cast<double>(f.mv[c]);
      if (d > 0.0) {
        s0[c] += d;
        s1[c] += d * static_cast<double>(t);
      } else if (s0[c] > 0.0) {
        if (count[c] < kMaxDips) {
          mid[c][count[c]] = s1[c] / s0[c];
          start[c][count[c]] = mid[c][count[c]] - s0[c] / 1000.0;  // ala = 1000 x W / 2
          ++count[c];
        }
        s0[c] = s1[c] = 0.0;
      }
    }
  }

  // Kanavan c syklin jakso P (ms), hyväksytty hetkellä t (arvioidaan ajon
  // jälkeen, kun kaikki kuopat on nähty). Sykli kirjataan, kun sen
  // päättänyt pulssi on nähty pohjaan asti, joten se on toteutunut sykli,
  // joka päättyi viimeisimpään ennen t:tä olleeseen imuun - tai sitä
  // edeltävään, jos pari kirjattiin vasta seuraavan pulssin jälkeen
  // (yli 1,8-kertainen pudotus). Muita ehdokkaita ei hyväksytä: ohitetun
  // pulssin kahden syklin väli voi osua kiihdytyksessä lähelle jotakin
  // vanhempaa sykliä. Toteutunut jakso on väli [alusta alkuun,
  // keskikohdasta keskikohtaan]: rajan voi määritellä missä tahansa
  // pulssin kohdassa, ja kierrosluvun muuttuessa pulssin leveys muuttuu.
  double cycleError(int c, long t, double p) const {
    int k = count[c] - 1;
    while (k >= 0 && mid[c][k] > static_cast<double>(t)) --k;
    double best = 1.0;
    for (int j = k; j >= 1 && j >= k - 1; --j) {
      const double ps = start[c][j] - start[c][j - 1];
      const double pm = mid[c][j] - mid[c][j - 1];
      const double lo = ps < pm ? ps : pm;
      const double hi = ps < pm ? pm : ps;
      double e = 0.0;
      if (p < lo) e = (lo - p) / ps;
      if (p > hi) e = (p - hi) / ps;
      if (e < best) best = e;
    }
    return best;
  }

  // Toteutunut kierrosluku hetken t_from jälkeen alkaneiden ja ennen t
  // päättyneiden syklien yli (enintään 8), kanavien mediaani. t_from < 0:
  // 8 viimeisintä sykliä (sama kuin näytön keskiarvo).
  float realisedSince(long t_from, long t) const {
    float r[kChannels];
    for (int c = 0; c < kChannels; ++c) {
      int k = count[c] - 1;
      while (k >= 0 && mid[c][k] > static_cast<double>(t)) --k;
      int n = 0;
      while (n < 8 && k - n - 1 >= 0 && mid[c][k - n - 1] >= static_cast<double>(t_from)) ++n;
      if (n < 1) n = 1;
      if (k - n < 0) return 0.0f;
      r[c] = static_cast<float>(120000.0 * n / (mid[c][k] - mid[c][k - n]));
    }
    return median3(r[0], r[1], r[2]);
  }
};

SimTruth g_truth;

struct SimRun {
  long max_gap = 0;          // pisin väli hyväksyttyjen syklien välillä (ms), muutoksen yli
  bool dropped = false;      // has_pulse putosi jollain kanavalla
  double worst_cycle = 0.0;  // pahin yksittäisen syklin jaksovirhe (avg_cycles 1)
  float disp_err = 0.0f;     // RpmEstimate vs toteutunut, kun muutoksen lopusta >= 1 s
  float peak_shown = 0.0f;   // näytetty huippu-rpm muutoksen aikana
  float mean_err = 0.0f;     // keskiarvovirhe lopussa
  int cycles = 0;
};

// Muutos: sched(t, p) asettaa parametrit hetkellä t (ms) ja palauttaa true,
// jos ne muuttuivat. t0..t_end = muutos, seurataan 2 s:sta loppuun.
template <typename F>
SimRun runSim(sbx::SimParams p, long t0, long t_end, long total, F sched) {
  using sbx::EngineSim;
  sbx::SimParams q = p;  // kaksonen: vain ajoitus
  q.base_kpa = -50.0f;
  q.depth_kpa = 40.0f;
  q.xtalk = 0.0f;
  q.res_kpa = 0.0f;
  q.noise_mv = 0.0f;
  q.peak_kpa = 0.0f;
  static EngineSim sim;
  static EngineSim twin;
  static Dsp dsp;
  static Dsp one;
  // Yksittäiset syklit (kanava, hetki, jakso) arvioidaan ajon jälkeen.
  // Pidetty sykli kirjataan samalla näytteellä kuin sen ratkaissut, ja
  // avg_cycles 1 näyttää vain jälkimmäisen; pidetyn jakso on kuitenkin
  // hyväksytty vain, jos se on jatkoa jälkimmäiselle (classify).
  static constexpr int kMaxEv = 2048;
  static int ev_c[kMaxEv];
  static long ev_t[kMaxEv];
  static double ev_p[kMaxEv];
  int n_ev = 0;
  SensorParams sp;
  sim = EngineSim();
  twin = EngineSim();
  sim.configure(p, sp, true);
  twin.configure(q, sp, true);
  dsp.configure(DspParams(), sp);
  DspParams d1;
  d1.avg_cycles = 1;
  one.configure(d1, sp);
  g_truth.clear();
  SimRun r;
  uint32_t prev[kChannels] = {0, 0, 0};
  uint32_t prev1[kChannels] = {0, 0, 0};
  long last[kChannels] = {2000, 2000, 2000};
  for (long t = 0; t < total; ++t) {
    if (sched(t, p)) {
      q.rpm = p.rpm;
      sim.configure(p, sp);
      twin.configure(q, sp);
    }
    const RawFrame f = sim.next();
    g_truth.add(twin.next(), t);
    float kpa[kChannels];
    for (int c = 0; c < kChannels; ++c) kpa[c] = (static_cast<float>(f.mv[c]) - sp.v_zero_mv) / sp.mv_per_kpa;
    dsp.push(kpa, f);
    one.push(kpa, f);
    if (t < 2000) {
      for (int c = 0; c < kChannels; ++c) {
        prev[c] = dsp.channel(c).cycles;
        prev1[c] = one.channel(c).cycles;
      }
      continue;
    }
    for (int c = 0; c < kChannels; ++c) {
      const ChannelState& s = dsp.channel(c);
      if (!s.has_pulse) r.dropped = true;
      if (s.cycles != prev[c]) {
        prev[c] = s.cycles;
        if (t >= t0 && t - last[c] > r.max_gap) r.max_gap = t - last[c];
        last[c] = t;
        ++r.cycles;
      }
      const ChannelState& s1 = one.channel(c);
      if (s1.cycles != prev1[c]) {
        prev1[c] = s1.cycles;
        if (n_ev < kMaxEv) {
          ev_c[n_ev] = c;
          ev_t[n_ev] = t;
          ev_p[n_ev] = 120000.0 / static_cast<double>(s1.rpm);
          ++n_ev;
        }
      }
    }
    const RpmEstimate e = dsp.rpm();
    if (t >= t0 && t <= t_end + 1000 && e.rpm > r.peak_shown) r.peak_shown = e.rpm;
    if (t >= t_end + 1000 && t % 20 == 0) {
      // Näyttö on avg_cycles (8) syklin keskiarvo, joka alkaa alusta, kun
      // jakso muuttuu kerralla yli 1,5-kertaiseksi. 850 rpm:llä 8 sykliä on
      // 1,13 s, joten liukuvan muutoksen jälkeen ikkunassa on 1 s kohdalla
      // vielä muutoksen loppua: verrataan samojen 8 toteutuneen syklin
      // keskiarvoon tai, jos näyttö alkoi alusta, muutoksen jälkeisiin.
      const float r8 = g_truth.realisedSince(-1, t);
      const float rs = g_truth.realisedSince(t_end, t);
      const float e8 = std::fabs(e.rpm - r8) / r8;
      const float es = std::fabs(e.rpm - rs) / rs;
      const float de = e8 < es ? e8 : es;
      if (de > r.disp_err) r.disp_err = de;
    }
  }
  for (int i = 0; i < n_ev; ++i) {
    const double e = g_truth.cycleError(ev_c[i], ev_t[i], ev_p[i]);
    if (e > r.worst_cycle) r.worst_cycle = e;
  }
  for (int c = 0; c < kChannels; ++c) {
    if (total - last[c] > r.max_gap) r.max_gap = total - last[c];
    const float me = std::fabs(dsp.channel(c).mean_kpa - sim.trueMeanKpa(c));
    if (me > r.mean_err) r.mean_err = me;
  }
  return r;
}

// Lineaarinen muutos (r0, b0, d0) -> (r1, b1, d1) hetkestä t0 ramp ms:ssa
// (ramp 0 = /api/sim-askel).
struct Ramp {
  float r0, r1, b0, b1, d0, d1;
  long t0, ramp;
  bool operator()(long t, sbx::SimParams& p) const {
    if (t < t0 || t > t0 + ramp) return false;
    const float a = ramp > 0 ? static_cast<float>(t - t0) / static_cast<float>(ramp) : 1.0f;
    p.rpm = r0 + (r1 - r0) * a;
    p.base_kpa = b0 + (b1 - b0) * a;
    p.depth_kpa = d0 + (d1 - d0) * a;
    return true;
  }
};

// Kaasunlisäys: 850 -> rpm1, -55 -> -25 kPa, syvyys 20 -> 12, nousu rise ms,
// pito 300 ms, paluu 700 ms.
struct Blip {
  float rpm1;
  long t0, rise;
  bool operator()(long t, sbx::SimParams& p) const {
    const long hold = 300, fall = 700;
    if (t < t0 || t > t0 + rise + hold + fall) return false;
    float a = 1.0f;
    if (t <= t0 + rise) a = static_cast<float>(t - t0) / static_cast<float>(rise);
    if (t > t0 + rise + hold) a = 1.0f - static_cast<float>(t - t0 - rise - hold) / static_cast<float>(fall);
    p.rpm = 850.0f + (rpm1 - 850.0f) * a;
    p.base_kpa = -55.0f + 30.0f * a;
    p.depth_kpa = 20.0f - 8.0f * a;
    return true;
  }
};

}  // namespace

void test_rpm_step_down_keeps_lock() {
  // /api/sim-askel alas (D14: jokainen rpm:n lasku on askel). Pyöristetty
  // versio laski lukituksen 1 s:ksi: vanhenemisaika 1,8 x vanha jakso
  // katkaisi jokaisen uuden välin, eikä viite koskaan vaihtunut. Nyt:
  // has_pulse ei putoa, väli <= 4 uutta jaksoa + vahvistus, näyttö uudessa
  // kierrosluvussa 1 s kuluttua. Askeleen ylittävä sykli (pulssi puoliksi
  // vanhalla, puoliksi uudella nopeudella) saa heittää <= 10 %; väärä jakso
  // (vuotopulssien T/3, puuttuvan pulssin 2T) heittäisi >= 30 %.
  const float from[] = {3000.0f, 2000.0f, 6000.0f};
  for (float r0 : from) {
    SimRun worst;
    for (int xi = 0; xi < 2; ++xi) {
      for (int ph = 0; ph < 3; ++ph) {
        sbx::SimParams p;
        p.rpm = r0;
        p.xtalk = xi ? 0.3f : 0.0f;
        p.seed = static_cast<uint32_t>(1 + ph);
        const long t0 = 2500 + static_cast<long>(static_cast<float>(ph) * 120000.0f / r0 / 3.0f);
        const SimRun r = runSim(p, t0, t0, t0 + 2500, Ramp{r0, 850.0f, -55.0f, -55.0f, 20.0f, 20.0f, t0, 0});
        TEST_ASSERT_FALSE_MESSAGE(r.dropped, "has_pulse putosi");
        if (r.max_gap > worst.max_gap) worst.max_gap = r.max_gap;
        if (r.worst_cycle > worst.worst_cycle) worst.worst_cycle = r.worst_cycle;
        if (r.disp_err > worst.disp_err) worst.disp_err = r.disp_err;
      }
    }
    msg("rpm-askel %.0f -> 850: pisin väli %.0f ms, pahin syklin jakso %.2f %%, näyttö 1 s jälkeen %.2f %%",
        static_cast<double>(r0), static_cast<double>(worst.max_gap), worst.worst_cycle * 100.0,
        static_cast<double>(worst.disp_err * 100.0f));
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(4.0f * periodMs(850.0f)) + 85, static_cast<int>(worst.max_gap));
    TEST_ASSERT_LESS_THAN_FLOAT(0.10f, static_cast<float>(worst.worst_cycle));
    TEST_ASSERT_LESS_THAN_FLOAT(0.03f, worst.disp_err);
  }
}

void test_rpm_ramp_and_throttle_chop() {
  // Kierrosluku 3000 -> 850 rampilla (vakioalipaine) ja kaasun katkaisu
  // (alipaine -15 -> -55 kPa samaan aikaan). Pyöristetty versio menetti
  // lukituksen jokaisessa 300 ms:n tapauksessa. Rampin yksittäinen sykli
  // vastaa toteutunutta < 3 %; 40 kPa/300 ms -katkaisussa 3000 rpm:n
  // pulssi on xtalk 0,3:lla vain ~3,7 kPa suodatettuna ja ramppi 5 kPa per
  // sykli, mikä vääristää pulssin muotoa (<= 10 %).
  struct Case {
    long ramp;
    float b0, d0, cycle_tol;
  };
  const Case cases[] = {{300, -55.0f, 20.0f, 0.03f}, {1000, -55.0f, 20.0f, 0.03f},
                        {300, -15.0f, 10.0f, 0.10f}, {700, -15.0f, 10.0f, 0.03f}};
  for (const Case& k : cases) {
    SimRun worst;
    for (int xi = 0; xi < 2; ++xi) {
      for (int ph = 0; ph < 2; ++ph) {
        sbx::SimParams p;
        p.rpm = 3000.0f;
        p.base_kpa = k.b0;
        p.depth_kpa = k.d0;
        p.xtalk = xi ? 0.3f : 0.0f;
        p.seed = static_cast<uint32_t>(3 + ph);
        const long t0 = 2500 + 17 * ph;
        const SimRun r = runSim(p, t0, t0 + k.ramp, t0 + k.ramp + 2500,
                                Ramp{3000.0f, 850.0f, k.b0, -55.0f, k.d0, 20.0f, t0, k.ramp});
        TEST_ASSERT_FALSE_MESSAGE(r.dropped, "has_pulse putosi");
        if (r.max_gap > worst.max_gap) worst.max_gap = r.max_gap;
        if (r.worst_cycle > worst.worst_cycle) worst.worst_cycle = r.worst_cycle;
        if (r.mean_err > worst.mean_err) worst.mean_err = r.mean_err;
      }
    }
    msg("3000 -> 850 rpm %.0f ms, alipaine %.0f -> -55: pisin väli %.0f ms, pahin syklin jakso %.2f %%, ka %.3f kPa",
        static_cast<double>(k.ramp), static_cast<double>(k.b0), static_cast<double>(worst.max_gap),
        worst.worst_cycle * 100.0, static_cast<double>(worst.mean_err));
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(4.0f * periodMs(850.0f)) + 85, static_cast<int>(worst.max_gap));
    TEST_ASSERT_LESS_THAN_FLOAT(k.cycle_tol, static_cast<float>(worst.worst_cycle));
    TEST_ASSERT_LESS_THAN_FLOAT(0.1f, worst.mean_err);
  }
}

void test_throttle_blip_engine_sim() {
  // Kaasunlisäys tyhjäkäynniltä ja paluu (synkronoinnissa käyttäjä blippaa ja
  // odottaa tyhjäkäyntiä): 850 -> 1500...3000 rpm, nousu 150/300/600 ms.
  // Pulssi kutistuu nousun aikana suodatettuna 20 -> 4,5...10 kPa ja tulee
  // aiemmin kuin vanha jakso; vanha versio jäädytti kaksi kanavaa 0,9 s:ksi
  // ja hyväksyi 150 ms:n nousussa ohitetun pulssin kahden syklin välin.
  // Raja kanavan pisimmälle välille: nousu >= 300 ms -> 300 ms, 150 ms ->
  // 400 ms. Poikkeukset (48 ajon mittaus, ks. dsp.h): 3000 rpm / 300 ms
  // xtalk 0,3:lla pahimmillaan 444 ms (pulssi 4,5 kPa juuri ennen pitoa),
  // 2500 rpm / 300 ms 327 ms. Jokainen hyväksytty sykli < 3 % toteutuneesta.
  struct Case {
    float rpm1;
    long rise;
    int gap_ms;
  };
  const Case cases[] = {{1500.0f, 150, 400}, {1500.0f, 300, 300}, {1500.0f, 600, 300},
                        {2000.0f, 150, 400}, {2000.0f, 300, 300}, {2000.0f, 600, 300},
                        {2500.0f, 150, 400}, {2500.0f, 300, 330}, {2500.0f, 600, 300},
                        {3000.0f, 150, 400}, {3000.0f, 300, 450}, {3000.0f, 600, 300}};
  for (const Case& k : cases) {
    SimRun worst;
    float ratio = 10.0f;
    for (int xi = 0; xi < 2; ++xi) {
      for (int ph = 0; ph < 2; ++ph) {
        sbx::SimParams p;
        p.xtalk = xi ? 0.3f : 0.0f;
        p.seed = static_cast<uint32_t>(1 + 2 * ph + xi);
        const long t0 = 2500 + 47 * ph;
        const SimRun r = runSim(p, t0, t0 + k.rise + 1000, t0 + k.rise + 3000, Blip{k.rpm1, t0, k.rise});
        TEST_ASSERT_FALSE_MESSAGE(r.dropped, "has_pulse putosi");
        if (r.max_gap > worst.max_gap) worst.max_gap = r.max_gap;
        if (r.worst_cycle > worst.worst_cycle) worst.worst_cycle = r.worst_cycle;
        if (r.peak_shown / k.rpm1 < ratio) ratio = r.peak_shown / k.rpm1;
      }
    }
    msg("blip 850 -> %.0f rpm, nousu %.0f ms: pisin väli %.0f ms, pahin syklin jakso %.2f %%, huippu %.2f x",
        static_cast<double>(k.rpm1), static_cast<double>(k.rise), static_cast<double>(worst.max_gap),
        worst.worst_cycle * 100.0, static_cast<double>(ratio));
    TEST_ASSERT_LESS_OR_EQUAL(k.gap_ms, static_cast<int>(worst.max_gap));
    TEST_ASSERT_LESS_THAN_FLOAT(0.03f, static_cast<float>(worst.worst_cycle));
    TEST_ASSERT_GREATER_THAN_FLOAT(0.85f, ratio);
  }
}

void test_throttle_blip_tracks_rpm_and_vacuum() {
  // Katselmoinnin blip EngineSimillä: 850 -> 2500 rpm ja -55 -> -25 kPa,
  // syvyys 20 -> 12 (300 ms ylös, 300 ms pito, 700 ms alas). Vanhalla
  // verhokäyrällä rpm ja keskiarvo jäätyivät ~0,9 s; pyöristetyllä versiolla
  // kaksi kanavaa jäätyi yhä 0,9 s ja näytetty huippu oli 0,43-0,48 x.
  // (Aiempi SynthSignal-versio meni läpi vain siksi, että generaattori
  // luo imut ~2 sykliä etukäteen ja nousu näkyi loivempana.)
  long worst_gap = 0;
  float worst_ratio = 10.0f;
  float top_ratio = 0.0f;
  for (uint32_t seed = 1; seed <= 2; ++seed) {
    for (int xi = 0; xi < 2; ++xi) {
      sbx::SimParams p;
      p.seed = seed;
      p.xtalk = xi ? 0.3f : 0.0f;
      const SimRun r = runSim(p, 2500, 3800, 6300, Blip{2500.0f, 2500, 300});
      TEST_ASSERT_FALSE_MESSAGE(r.dropped, "has_pulse putosi");
      TEST_ASSERT_LESS_THAN_FLOAT(0.03f, static_cast<float>(r.worst_cycle));
      TEST_ASSERT_LESS_THAN_FLOAT(0.1f, r.mean_err);
      TEST_ASSERT_LESS_THAN_FLOAT(0.03f, r.disp_err);
      if (r.max_gap > worst_gap) worst_gap = r.max_gap;
      if (r.peak_shown / 2500.0f < worst_ratio) worst_ratio = r.peak_shown / 2500.0f;
      if (r.peak_shown / 2500.0f > top_ratio) top_ratio = r.peak_shown / 2500.0f;
    }
  }
  msg("blip 850 -> 2500: pisin väli %.0f ms, näytetty huippu %.2f x", static_cast<double>(worst_gap),
      static_cast<double>(worst_ratio));
  // Väli: sama raja kuin test_throttle_blip_engine_sim 2500 rpm / 300 ms.
  TEST_ASSERT_LESS_OR_EQUAL(330, static_cast<int>(worst_gap));
  // 8 syklin keskiarvo kulkee perässä, mutta ei jäädy, jää vajaaksi eikä
  // ylitä huippua (väärä lyhyt jakso näkyisi ylityksenä).
  TEST_ASSERT_GREATER_THAN_FLOAT(0.9f, worst_ratio);
  TEST_ASSERT_LESS_THAN_FLOAT(1.03f, top_ratio);
}

// --- pulssin katoaminen ------------------------------------------------------------

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
    // Viimeinen sykli päättyy viimeisen pulssin rajalle ja vahvistuu, kun
    // pulssi on nähty pohjaan (suotimen viive ~15 ms + pulssin lasku).
    TEST_ASSERT_GREATER_THAN(-200, last_cycle[c]);
    TEST_ASSERT_LESS_THAN(80, last_cycle[c]);
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
  // epätotena (muuten letkuvika peittyisi). Oletus hyst_min_kpa 0,3 kestää
  // ~0,4 kPa:n raakakohinan; 0,6 kPa:n kohinalla tarvitaan ~0,5 (S5a).
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
  // Vanhat historiataulukot jäävät muistiin (reset ei tyhjennä niitä), mutta
  // laskurit rajaavat niiden käytön: ensimmäinen uusi keskiarvo tulee vain
  // uusista sykleistä. Testataan vaihtamalla totuus resetin hetkellä.
  SynthParams p = idleParams();
  p.base_kpa = -30.0f;
  g_rig.syn.setParams(p);
  uint32_t seen = 0;
  for (int i = 0; i < 2000; ++i) {
    g_rig.step();
    const ChannelState& s = g_rig.dsp.channel(1);
    if (s.cycles == 0 || s.cycles == seen) continue;
    seen = s.cycles;
    TEST_ASSERT_FLOAT_WITHIN(0.3f, g_rig.syn.trueMean(1), s.mean_kpa);
  }
  TEST_ASSERT_GREATER_THAN(8, static_cast<int>(seen));
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
  // ceil(0,95 N) kokonaista sykliä; askel osuu kesken syklin (+0...1), ja
  // sykli hyväksytään vasta kun sen päättänyt pulssi on nähty pohjaan, joten
  // ennen askelta päättynyt sykli voi tulla laskuriin vasta askeleen jälkeen (+0...1).
  TEST_ASSERT_GREATER_OR_EQUAL(1, n1);
  TEST_ASSERT_LESS_OR_EQUAL(3, n1);
  TEST_ASSERT_GREATER_OR_EQUAL(8, n8);
  TEST_ASSERT_LESS_OR_EQUAL(10, n8);
  TEST_ASSERT_GREATER_OR_EQUAL(31, n32);
  TEST_ASSERT_LESS_OR_EQUAL(33, n32);
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

// Laskeeko DSP vuotopulssit omiksi sykleikseen annetuilla parametreilla?
// Palauttaa kanavan 0 hyväksytyt syklit / moottorin syklit 4 s:n aikana.
float cycleRatioWithLeak(float xtalk, float hyst_frac, float hyst_min) {
  SynthParams p = idleParams();
  p.rpm = 600.0f;
  p.depth_kpa = 4.0f;
  p.xtalk = xtalk;
  p.res_kpa = 0.0f;
  p.noise_kpa = 0.05f;
  DspParams dp;
  dp.hyst_frac = hyst_frac;
  dp.hyst_min_kpa = hyst_min;
  g_rig.begin(p, dp);
  g_rig.run(2.0f);
  const uint32_t c0 = g_rig.dsp.channel(0).cycles;
  const uint32_t e0 = g_rig.syn.cyclesStarted();
  g_rig.run(4.0f);
  const float engine = static_cast<float>(g_rig.syn.cyclesStarted() - e0);
  return static_cast<float>(g_rig.dsp.channel(0).cycles - c0) / engine;
}

void test_hysteresis_sets_trigger_level() {
  // Raja vaatii pudotuksen (A + h) / 2 huipusta, h = max(hyst_min_kpa,
  // hyst_frac x A). Vuotopulssi (xtalk x A) laukaisee rajan, jos
  // x > 0,5 + h / (2 A). Kummankin parametrin vaikutus erikseen, A ~ 4 kPa:
  //  - hyst_frac 0 -> h = hyst_min: 1,0 kPa:lla vuotopulssin raja on
  //    x > 0,625, joten 0,57:n vuoto ei laukaise; ilman alarajaa (h = 0)
  //    raja olisi x > 0,5 ja vuoto laukaisisi.
  //  - hyst_min pieni -> h = hyst_frac x A: 0,3:lla raja x > 0,65 (0,6:n
  //    vuoto ei laukaise), 0,1:llä x > 0,55 (laukaisee).
  const float floor_on = cycleRatioWithLeak(0.57f, 0.0f, 1.0f);
  const float floor_off = cycleRatioWithLeak(0.57f, 0.0f, 0.05f);
  const float frac_hi = cycleRatioWithLeak(0.6f, 0.3f, 0.05f);
  const float frac_lo = cycleRatioWithLeak(0.6f, 0.1f, 0.05f);
  msg("syklit / moottorin syklit: hyst_min 1,0 %.2f vs 0,05 %.2f; hyst_frac 0,3 %.2f vs 0,1 %.2f",
      static_cast<double>(floor_on), static_cast<double>(floor_off), static_cast<double>(frac_hi),
      static_cast<double>(frac_lo));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, floor_on);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, frac_hi);
  // Vuotopulssin laukaisemat rajat rikkovat syklit: niitä ei hyväksytä
  // oikeina (kolminkertainen rpm), mutta oikeitakaan ei enää saada.
  TEST_ASSERT_LESS_THAN_FLOAT(0.5f, floor_off);
  TEST_ASSERT_LESS_THAN_FLOAT(0.5f, frac_lo);
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
  TEST_ASSERT_LESS_OR_EQUAL(380, fell);
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
  TEST_ASSERT_LESS_THAN_FLOAT(0.005f, a.rpm_err_rel);
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
  RUN_TEST(test_high_rpm_small_pulses_detected);
  RUN_TEST(test_amplitude_gate_scales_with_filter_gain);
  RUN_TEST(test_fractional_boundary_single_cycle_mean);
  RUN_TEST(test_d1_mean_is_not_delayed_by_lowpass);
  RUN_TEST(test_crosstalk_does_not_triple_rpm);
  RUN_TEST(test_cv_follows_jitter_5pct);
  RUN_TEST(test_cv_small_with_jitter_half_pct);
  RUN_TEST(test_has_pulse_within_three_cycles_of_plug_in);
  RUN_TEST(test_startup_on_resonance_with_small_pulse);
  RUN_TEST(test_has_pulse_after_engine_start_from_atmosphere);
  RUN_TEST(test_vacuum_steps_and_ramps_keep_lock);
  RUN_TEST(test_vacuum_step_at_any_phase_accepts_no_wrong_cycle);
  RUN_TEST(test_pulse_depth_changes_keep_lock);
  RUN_TEST(test_rpm_step_down_keeps_lock);
  RUN_TEST(test_rpm_ramp_and_throttle_chop);
  RUN_TEST(test_throttle_blip_engine_sim);
  RUN_TEST(test_throttle_blip_tracks_rpm_and_vacuum);
  RUN_TEST(test_pulse_timeout_when_signal_goes_flat);
  RUN_TEST(test_noise_and_resonance_alone_never_pulse);
  RUN_TEST(test_level_ema_tracks_step_with_tau);
  RUN_TEST(test_sensor_fault_after_hold_and_clears);
  RUN_TEST(test_saturation_flag_lasts_one_second);
  RUN_TEST(test_configure_resets_state);
  RUN_TEST(test_avg_cycles_sets_response_and_noise);
  RUN_TEST(test_filter_cutoff_changes_min_p2p_but_not_mean);
  RUN_TEST(test_hyst_min_sets_pulse_detection_floor);
  RUN_TEST(test_hysteresis_sets_trigger_level);
  RUN_TEST(test_pulse_timeout_parameter);
  RUN_TEST(test_rpm_limits_reject_out_of_range_periods);
  RUN_TEST(test_rpm_median_with_hose_off_and_validity);
  RUN_TEST(test_missed_pulse_does_not_halve_rpm);
  RUN_TEST(test_displayed_mean_noise_floor_at_idle);
  RUN_TEST(test_nan_sample_does_not_poison_state);
  return UNITY_END();
}
