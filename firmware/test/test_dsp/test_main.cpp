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
#include <cstdint>
#include <cstdio>

// Testikoukku (dsp.h, recordCycle): jokainen kirjattu sykli talteen, myös
// saman näytteen aikana kirjatut. avg_cycles = 1 -näyttö näki vain viimeisen,
// ja hypänneen syklin näyttö ei päivity lainkaan, joten yksittäisten syklien
// tarkistus tehdään näistä.
struct CycleRecord {
  int ch;
  float period;       // näytteinä
  uint32_t end_int;   // loppumerkki (pulssin keskikohta) Dsp:n näyteindeksinä
  float end_frac;
  long t;             // testin näyte, jolla sykli kirjattiin
};
static constexpr int kMaxCycleRecords = 4096;
static CycleRecord g_cycles[kMaxCycleRecords];
static int g_cycle_count = 0;
static bool g_capture = false;
static long g_capture_t = 0;
static void captureCycle(int ch, float period, uint32_t end_int, float end_frac) {
  if (!g_capture || g_cycle_count >= kMaxCycleRecords) return;
  g_cycles[g_cycle_count++] = CycleRecord{ch, period, end_int, end_frac, g_capture_t};
}
#define SBX_DSP_CYCLE_HOOK(ch, period, end_int, end_frac) captureCycle((ch), (period), (end_int), (end_frac))

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

  // Skaalatun rajan varassa (suodatettu p2p ~0,6 kPa) hyväksytään vain
  // tasavälinen jono: samat 2 kPa:n, 6 ms:n kuopat 25 ms:n välein lukittuvat,
  // mutta vuorotellen 21 ja 29 ms:n välein (suhde 1,38, kuin kohinaa) eivät.
  // Kuopat kiertävät kanavat sylinterien tavoin (kanava c jakson c/3:n
  // kohdalla), joten omistajuus ja lomitus hyväksyvät molemmat jonot ja
  // ainoa ero on jaksosuhde (kSmallPulseRatio). Sama kuoppa kaikilla
  // kanavilla yhtä aikaa ei olisi kenenkään oma pulssi.
  const int gaps[2][2] = {{25, 25}, {21, 29}};
  for (int k = 0; k < 2; ++k) {
    Dsp dsp;
    float kpa[kChannels];
    RawFrame raw;
    long next = 100, start = -1000, len = 25;
    int n = 0;
    for (long t = 0; t < 4000; ++t) {
      if (t == next) {
        start = t;
        len = gaps[k][n++ % 2];
        next += len;
      }
      for (int c = 0; c < kChannels; ++c) {
        const long u = t - start - (static_cast<long>(c) * len) / 3;
        float v = -10.0f;
        if (u >= 0 && u < 6) {
          const float sn = std::sin(3.14159265f * static_cast<float>(u) / 6.0f);
          v -= 2.0f * sn * sn;
        }
        kpa[c] = v;
        raw.mv[c] = static_cast<uint16_t>(2700.0f + 25.0f * v);
      }
      dsp.push(kpa, raw);
    }
    if (k == 0) {
      TEST_ASSERT_TRUE(dsp.channel(0).has_pulse);
      TEST_ASSERT_LESS_THAN_FLOAT(0.9f, dsp.channel(0).p2p_kpa);
    } else {
      TEST_ASSERT_EQUAL_UINT32(0, dsp.channel(0).cycles);
    }
  }
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
  // 164 ms), joutokäynnillä ~3,3 sykliä. 6000 rpm:llä (20 ms) suotimen
  // esitäyttö on välttämätön: ilman sitä -55 kPa:n askel soi asettumisajan
  // yli ja ensimmäinen amplitudiarvio on liian suuri.
  const float rpms[] = {600.0f, 850.0f, 1500.0f, 3000.0f, 6000.0f};
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
    // Yhden syklin jakso 6000 rpm:llä on 20 näytettä: keskikohdan ~0,3 ms:n
    // epävarmuus on jo 1,5 % (keskiarvoistettuna < 1 %, test_accuracy_6000_rpm).
    TEST_ASSERT_LESS_THAN_FLOAT(rpm > 5000.0f ? 0.025f : 0.015f, worst.rpm_err);
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

  // Sammutus ja uusi käynnistys pienemmällä pulssilla: sammutuksen 55 kPa:n
  // nousu jää haun amplitudiarvioon, ellei vanha segmentti vanhene (pisimmän
  // jakson jälkeen ilman rajaa). Muuten 4 kPa:n pulssit eivät koskaan
  // laukaisisi, eikä kanava lukittuisi uudelleen. Käynnistysaskeleen
  // suodatettu ylitys (~6 kPa) vanhenee samoin: mitattu 1,16 s.
  SynthParams p = idleParams();
  g_rig.begin(p);
  g_rig.run(2.0f);
  p.engine_on = false;
  g_rig.syn.setParams(p);
  g_rig.run(1.5f);
  for (int c = 0; c < kChannels; ++c) TEST_ASSERT_FALSE(g_rig.dsp.channel(c).has_pulse);
  p.engine_on = true;
  p.depth_kpa = 4.0f;
  g_rig.syn.setParams(p);
  int relock = -1;
  for (int i = 0; i < 3000 && relock < 0; ++i) {
    g_rig.step();
    bool all = true;
    for (int c = 0; c < kChannels; ++c) all = all && g_rig.dsp.channel(c).has_pulse;
    if (all) relock = i;
  }
  msg("uudelleenkäynnistys 4 kPa:n pulssilla: lukitus %.0f ms", static_cast<double>(relock));
  TEST_ASSERT_GREATER_OR_EQUAL(0, relock);
  TEST_ASSERT_LESS_OR_EQUAL(1500, relock);
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

// --- kierrosluvun ja alipaineen muutokset EngineSimillä (laitteen oma simulaattori) --
//
// SynthSignal luo imuhetket ~2 sykliä etukäteen, joten sen kierrosluku seuraa
// käskyä viiveellä. Nämä testit ajavat saman ketjun kuin laite: EngineSim ->
// mV -> (mv - v_zero) / mv_per_kpa -> Dsp, oletusparametrein (resonanssi,
// kohina, 1 %:n jitter).
//
// Totuus tulee kaksoissimulaattorista: sama siemen ja samat kierroslukukomennot,
// mutta ilman kohinaa, resonanssia, ristivuotoa ja letkuvikoja ja vakiopulssilla.
// EngineSim arpoo jitterin samassa järjestyksessä parametreista riippumatta,
// joten kaksosen imut osuvat täsmälleen samoihin hetkiin; sen sin^2-kuopan
// painopiste on imun keskikohta ja pinta-ala kertoo leveyden.
//
// Tiukka syklitarkistus: JOKAINEN kirjattu sykli (testikoukku, ks. alku)
// tarkistetaan. Syklin loppumerkki ja alkumerkki (loppu - jakso) liitetään
// lähimpään MINKÄ TAHANSA sylinterin imun keskikohtaan (merkin viive =
// suotimen ryhmäviive, mitataan muutosta edeltävistä sykleistä). Sykli on
// väärä, jos kumpikaan merkki osuu toisen sylinterin imuun (vuotopulssin raja)
// tai merkit eivät ole saman sylinterin peräkkäiset imut (ohitettu pulssi, 2T).
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

  // Lähin imu (sylinteri, indeksi) hetkeen t.
  void nearest(double t, int& cyl, int& idx) const {
    double best = 1.0e18;
    cyl = idx = -1;
    for (int c = 0; c < kChannels; ++c) {
      for (int k = 0; k < count[c]; ++k) {
        const double d = std::fabs(mid[c][k] - t);
        if (d < best) {
          best = d;
          cyl = c;
          idx = k;
        }
      }
    }
  }

  // Kanavan c toteutunut kierrosluku n viimeisimmän jakson yli (viimeisin imu <= t).
  float realised(int c, double t, int n) const {
    int k = count[c] - 1;
    while (k >= 0 && mid[c][k] > t) --k;
    if (k - n < 0) return 0.0f;
    return static_cast<float>(120000.0 * n / (mid[c][k] - mid[c][k - n]));
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

struct Strict {
  int records = 0;
  int wrong = 0;
  double worst = 0.0;           // pahin jaksovirhe oikeissa sykleissä (ei askeleen ylittävää)
  double worst_straddle = 0.0;  // hetkellisen muutoksen ylittävä sykli
};

// Merkin viive imun keskikohdasta (suotimen ryhmäviive ~11,5 ms): mediaani
// muutosta edeltävistä sykleistä.
double markerDelay(long t_from, long t_to) {
  double r[256];
  int n = 0;
  for (int i = 0; i < g_cycle_count && n < 256; ++i) {
    const CycleRecord& rc = g_cycles[i];
    if (rc.t < t_from || rc.t >= t_to) continue;
    const double te = static_cast<double>(rc.end_int) + static_cast<double>(rc.end_frac) - 1.0;
    double best = 1.0e9;
    for (int k = 0; k < g_truth.count[rc.ch]; ++k) {
      const double d = te - g_truth.mid[rc.ch][k];
      if (d >= 0.0 && d < 30.0 && d < best) best = d;
    }
    if (best < 1.0e9) r[n++] = best;
  }
  if (n < 3) return 11.5;
  for (int i = 1; i < n; ++i) {
    const double v = r[i];
    int j = i - 1;
    while (j >= 0 && r[j] > v) {
      r[j + 1] = r[j];
      --j;
    }
    r[j + 1] = v;
  }
  return r[n / 2];
}

// Ajon viimeiset 150 ms jätetään pois: Dsp kirjaa syklin ~18 ms imun
// keskikohdan jälkeen, mutta kaksosen kuoppa valmistuu vasta imun lopussa
// (600 rpm: 25 ms), joten viimeisen imun totuus voi puuttua.
// swap_t >= 0: letkut 2 ja 3 vaihdettu tästä hetkestä (kanavat 1 <-> 2 eri
// sylintereihin); vaihdon ylittävää sykliä ei tarkisteta (puolet kummastakin).
Strict strictCheck(long t_from, long cal_from, long cal_to, long t_until, double step_t, double swap_t) {
  Strict s;
  const double delay = markerDelay(cal_from, cal_to);
  for (int i = 0; i < g_cycle_count; ++i) {
    const CycleRecord& rc = g_cycles[i];
    if (rc.t < t_from || rc.t > t_until) continue;
    const double te = static_cast<double>(rc.end_int) + static_cast<double>(rc.end_frac) - 1.0 - delay;
    const double ts = te - static_cast<double>(rc.period);
    if (swap_t >= 0.0 && ts < swap_t && te >= swap_t) continue;
    const int cyl = swap_t >= 0.0 && ts >= swap_t && rc.ch > 0 ? 3 - rc.ch : rc.ch;
    ++s.records;
    int ce = -1, ke = -1, cs = -1, ks = -1;
    g_truth.nearest(te, ce, ke);
    g_truth.nearest(ts, cs, ks);
    if (ce != cyl || cs != cyl || ke - ks != 1) {
      ++s.wrong;
      continue;
    }
    const double pm = g_truth.mid[cyl][ke] - g_truth.mid[cyl][ks];
    const double e = std::fabs(static_cast<double>(rc.period) - pm) / pm;
    const double end_e = 2.0 * g_truth.mid[cyl][ke] - g_truth.start[cyl][ke];  // imun loppu
    const bool straddle = step_t >= 0.0 && step_t >= g_truth.start[cyl][ks] - 2.0 && step_t <= end_e + 2.0;
    if (straddle) {
      if (e > s.worst_straddle) s.worst_straddle = e;
    } else if (e > s.worst) {
      s.worst = e;
    }
  }
  return s;
}

struct SimRun {
  long max_gap = 0;            // pisin väli kirjattujen syklien välillä (ms), muutoksen yli
  bool dropped = false;        // has_pulse putosi jollain (kytketyllä) kanavalla
  bool finite = true;          // kaikki ChannelState- ja RpmEstimate-luvut äärellisiä joka näytteellä
  long lock_ms = -1;           // käynnistyksestä (off_t2) kunnes seuratut kanavat pulssivat, -1 = ei
  float est_err = 0.0f;        // max |RpmEstimate - nimellis| / nimellis hetkestä est_from (vakio-rpm)
  float est_at_err = 0.0f;     // |RpmEstimate - nimellis| / nimellis hetkellä est_at
  float first_disp_err = 0.0f; // ensimmäinen uuden käynnin kanava-rpm (< 1,3 x nimellis) vs nimellis
  int wrong = 0;               // väärät syklit (tiukka tarkistus)
  double worst_cycle = 0.0;    // pahin jaksovirhe (ei askeleen ylittävää sykliä)
  double worst_straddle = 0.0; // hetkellisen askeleen ylittävä sykli
  float disp_err = 0.0f;       // RpmEstimate vs toteutunut, kun muutoksen lopusta >= 1 s
  float peak_shown = 0.0f;     // näytetty huippu-rpm muutoksen aikana
  float mean_err = 0.0f;       // keskiarvovirhe lopussa
  float ch_rpm_err = 0.0f;     // jokaisen kanavan rpm vs toteutunut 8, t >= check_from
  float ch_mean_err = 0.0f;    // jokaisen kanavan mean_kpa vs totuus, t >= check_from
  float rpm_ratio = 0.0f;      // suurin kanavan rpm / nimellis-rpm (t >= 2000)
  float p2p_lo = 1.0e9f;       // pienin ja suurin kanavan p2p_kpa hetkellä p2p_at
  float p2p_hi = -1.0e9f;
};

struct SimOpts {
  long t0 = 2500, t_end = 2500, total = 5000;  // muutos t0..t_end, ajo total ms
  double step_t = -1.0;     // hetkellisen muutoksen hetki (askeleen ylittävä sykli)
  long check_from = -1;     // kanavien rpm/mean-tarkistus tästä hetkestä (-1 = ei)
  int hose_mask = 0;        // letku irti näillä kanavilla (bitti c) hetkestä hose_t
  long hose_t = 0;
  long hose_t2 = -1;        // letkut takaisin tällä hetkellä (-1 = ei)
  long off_t1 = -1, off_t2 = -1;  // moottori seis [off_t1, off_t2): 0 = kylmäkäynnistys
  long watch_from = 2000;   // tiukka tarkistus ja välit tästä hetkestä
  int sens_ch = -1;         // anturi alas (SensorLow) kanavalla sens_ch [sens_t1, sens_t2)
  long sens_t1 = 0, sens_t2 = 0;
  int weak_ch = -1;         // heikko sylinteri: sen pulssi (oma ja vuoto) x weak_scale
  float weak_scale = 1.0f;
  long est_from = -1;       // RpmEstimate vs nimellis tästä hetkestä (-1 = ei)
  long est_at = -1;         // RpmEstimate vs nimellis juuri tällä hetkellä (-1 = ei)
  bool first_disp = false;  // kanavan ensimmäinen uuden käynnin näyttöarvo (t0:n jälkeen)
  long swap_t = -1;         // letkut 2 ja 3 vaihdetaan tällä hetkellä (kanavat 1 <-> 2, -1 = ei)
  long p2p_at = -1;         // kanavien p2p_kpa talteen tällä hetkellä (-1 = ei)
};

bool finiteState(const ChannelState& s) {
  return std::isfinite(s.level_kpa) && std::isfinite(s.mean_kpa) && std::isfinite(s.min_kpa) &&
         std::isfinite(s.p2p_kpa) && std::isfinite(s.rpm) && std::isfinite(s.last_kpa);
}

// Muutos: sched(t, p) asettaa parametrit hetkellä t (ms) ja palauttaa true,
// jos ne muuttuivat. Seuranta alkaa 2 s:sta.
template <typename F>
SimRun runSim(sbx::SimParams p, const SimOpts& o, F sched) {
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
  SensorParams sp;
  sim = EngineSim();
  twin = EngineSim();
  sim.configure(p, sp, true);
  twin.configure(q, sp, true);
  dsp.configure(DspParams(), sp);
  g_truth.clear();
  g_cycle_count = 0;
  g_capture = true;
  SimRun r;
  uint32_t prev[kChannels] = {0, 0, 0};
  long last[kChannels] = {o.watch_from, o.watch_from, o.watch_from};
  // Seuraamatta: irrotetut letkut, vikaantuva anturi ja heikko sylinteri (sen
  // kanava saa jäädä ilman has_pulsea, mutta ei saa kirjata väärää sykliä).
  int skip = o.hose_mask;
  if (o.sens_ch >= 0) skip |= 1 << o.sens_ch;
  if (o.weak_ch >= 0) skip |= 1 << o.weak_ch;
  int first_done = 0;
  for (long t = 0; t < o.total; ++t) {
    bool reconf = sched(t, p);
    if (o.hose_mask != 0 && (t == o.hose_t || t == o.hose_t2)) {
      for (int c = 0; c < kChannels; ++c) {
        if (o.hose_mask & (1 << c)) p.fault[c] = t == o.hose_t ? sbx::SimFault::HoseOff : sbx::SimFault::None;
      }
      reconf = true;
    }
    const bool on = !(t >= o.off_t1 && t < o.off_t2);
    if (on != p.engine_on) {
      p.engine_on = on;
      reconf = true;
    }
    if (o.sens_ch >= 0 && (t == o.sens_t1 || t == o.sens_t2)) {
      p.fault[o.sens_ch] = t == o.sens_t1 ? sbx::SimFault::SensorLow : sbx::SimFault::None;
      reconf = true;
    }
    if (reconf) {
      q.rpm = p.rpm;
      q.engine_on = p.engine_on;
      sim.configure(p, sp);
      twin.configure(q, sp);
    }
    RawFrame f = sim.next();
    const RawFrame tw = twin.next();
    g_truth.add(tw, t);
    if (o.weak_ch >= 0 && p.engine_on) {
      // Heikko sylinteri mV-erotuksena: kaksosen kanavasta saadaan saman
      // sylinterin pulssimuoto s^2 (kuoppa 1000 mV, ks. SimTruth::add), ja sen
      // osuus (1 - scale) x (oma 1, muille xtalk) x syvyys poistetaan. s^2:n
      // keskiarvo syklin yli on intake_frac / 2, joten keskiarvo ei muutu.
      const double s2 = (1575.0 - static_cast<double>(tw.mv[o.weak_ch])) / 1000.0;
      const double shape = (s2 > 0.0 ? s2 : 0.0) - 0.5 * static_cast<double>(p.intake_frac);
      for (int c = 0; c < kChannels; ++c) {
        if (p.fault[c] != sbx::SimFault::None) continue;
        const double g = c == o.weak_ch ? 1.0 : static_cast<double>(p.xtalk);
        const double mv = static_cast<double>(f.mv[c]) + static_cast<double>(sp.mv_per_kpa) * g *
                                                              (1.0 - static_cast<double>(o.weak_scale)) *
                                                              static_cast<double>(p.depth_kpa) * shape;
        f.mv[c] = static_cast<uint16_t>(mv < 0.0 ? 0.0 : (mv > 3300.0 ? 3300.0 : mv + 0.5));
      }
    }
    if (o.swap_t >= 0 && t >= o.swap_t) {
      const uint16_t m = f.mv[1];
      f.mv[1] = f.mv[2];
      f.mv[2] = m;
    }
    float kpa[kChannels];
    for (int c = 0; c < kChannels; ++c) kpa[c] = (static_cast<float>(f.mv[c]) - sp.v_zero_mv) / sp.mv_per_kpa;
    g_capture_t = t;
    dsp.push(kpa, f);
    const RpmEstimate est = dsp.rpm();
    if (!std::isfinite(est.rpm) || !std::isfinite(est.sd_rpm) || !std::isfinite(est.cv)) r.finite = false;
    for (int c = 0; c < kChannels; ++c) {
      if (!finiteState(dsp.channel(c))) r.finite = false;
    }
    if (o.off_t2 >= 0 && t >= o.off_t2 && r.lock_ms < 0) {
      bool all = true;
      for (int c = 0; c < kChannels; ++c) {
        if (!(skip & (1 << c)) && !dsp.channel(c).has_pulse) all = false;
      }
      if (all) r.lock_ms = t - o.off_t2;
    }
    if (o.est_from >= 0 && t >= o.est_from) {
      const float e = std::fabs(est.rpm - p.rpm) / p.rpm;
      if (e > r.est_err) r.est_err = e;
    }
    if (t == o.est_at) r.est_at_err = std::fabs(est.rpm - p.rpm) / p.rpm;
    if (t == o.p2p_at) {
      for (int c = 0; c < kChannels; ++c) {
        if (skip & (1 << c)) continue;
        const float v = dsp.channel(c).p2p_kpa;
        if (v < r.p2p_lo) r.p2p_lo = v;
        if (v > r.p2p_hi) r.p2p_hi = v;
      }
    }
    if (o.first_disp && t > o.t0) {
      for (int c = 0; c < kChannels; ++c) {
        const float cr = dsp.channel(c).rpm;
        if (first_done & (1 << c) || cr != cr || !(cr > 0.0f && cr < 1.3f * p.rpm)) continue;
        first_done |= 1 << c;
        const float e = std::fabs(cr - p.rpm) / p.rpm;
        if (e > r.first_disp_err) r.first_disp_err = e;
      }
    }
    // Pysähdyksestä uuteen lukitukseen ei seurata katkoja eikä välejä.
    const bool quiet = o.off_t1 >= 0 && t >= o.off_t1 && (r.lock_ms < 0 || t < o.off_t2 + r.lock_ms);
    if (t < o.watch_from || quiet) {
      for (int c = 0; c < kChannels; ++c) {
        prev[c] = dsp.channel(c).cycles;
        last[c] = t;
      }
      continue;
    }
    for (int c = 0; c < kChannels; ++c) {
      if (skip & (1 << c)) continue;
      const ChannelState& s = dsp.channel(c);
      if (!s.has_pulse) r.dropped = true;
      if (s.cycles != prev[c]) {
        prev[c] = s.cycles;
        if (t >= o.t0 && t - last[c] > r.max_gap) r.max_gap = t - last[c];
        last[c] = t;
      }
      if (s.has_pulse && s.rpm / sim.trueRpm() > r.rpm_ratio) r.rpm_ratio = s.rpm / sim.trueRpm();
      if (o.check_from >= 0 && t >= o.check_from && t % 10 == 0) {
        // Kirjaus tulee ~15-20 ms imun keskikohdan jälkeen: verrataan kahteen
        // viimeisimpään 8 syklin ikkunaan ja otetaan parempi.
        float best = 1.0f;
        for (int sh = 0; sh <= 30; sh += 30) {
          const float rr = g_truth.realised(c, static_cast<double>(t - sh), 8);
          if (rr > 0.0f && std::fabs(s.rpm - rr) / rr < best) best = std::fabs(s.rpm - rr) / rr;
        }
        if (best > r.ch_rpm_err) r.ch_rpm_err = best;
        const float me = std::fabs(s.mean_kpa - sim.trueMeanKpa(c));
        if (me > r.ch_mean_err) r.ch_mean_err = me;
      }
    }
    const RpmEstimate e = dsp.rpm();
    if (t >= o.t0 && t <= o.t_end + 1000 && e.rpm > r.peak_shown) r.peak_shown = e.rpm;
    if (o.hose_mask == 0 && t >= o.t_end + 1000 && t % 20 == 0) {
      // Näyttö on avg_cycles (8) syklin keskiarvo, joka alkaa alusta, kun
      // jakso muuttuu kerralla yli 1,5-kertaiseksi. 850 rpm:llä 8 sykliä on
      // 1,13 s, joten liukuvan muutoksen jälkeen ikkunassa on 1 s kohdalla
      // vielä muutoksen loppua: verrataan samojen 8 toteutuneen syklin
      // keskiarvoon tai, jos näyttö alkoi alusta, muutoksen jälkeisiin.
      const float r8 = g_truth.realisedSince(-1, t);
      const float rs = g_truth.realisedSince(o.t_end, t);
      const float e8 = std::fabs(e.rpm - r8) / r8;
      const float es = std::fabs(e.rpm - rs) / rs;
      const float de = e8 < es ? e8 : es;
      if (de > r.disp_err) r.disp_err = de;
    }
  }
  g_capture = false;
  // Merkin viive kalibroidaan ennen muutosta; käynnistyksessä vasta lukituksen
  // jälkeen kirjatuista sykleistä (kaikki kirjatut tarkistetaan silti).
  const long cal_from = o.off_t1 >= 0 && o.off_t1 < o.watch_from ? o.off_t2 + 600 : o.watch_from;
  const long cal_to = o.off_t1 >= 0 && o.off_t1 < o.watch_from ? o.off_t2 + 2000 : o.t0;
  const Strict st = strictCheck(o.watch_from, cal_from, cal_to, o.total - 150, o.step_t, static_cast<double>(o.swap_t));
  r.wrong = st.wrong;
  r.worst_cycle = st.worst;
  r.worst_straddle = st.worst_straddle;
  for (int c = 0; c < kChannels; ++c) {
    if (skip & (1 << c)) continue;
    if (o.total - last[c] > r.max_gap) r.max_gap = o.total - last[c];
    const float me = std::fabs(dsp.channel(c).mean_kpa - sim.trueMeanKpa(c));
    if (me > r.mean_err) r.mean_err = me;
  }
  return r;
}

void worstOf(SimRun& w, const SimRun& r) {
  if (r.max_gap > w.max_gap) w.max_gap = r.max_gap;
  w.dropped = w.dropped || r.dropped;
  w.finite = w.finite && r.finite;
  if (r.lock_ms < 0 || w.lock_ms == -2) {
    w.lock_ms = -2;  // jokin ajo ei lukittunut
  } else if (r.lock_ms > w.lock_ms) {
    w.lock_ms = r.lock_ms;
  }
  if (r.est_err > w.est_err) w.est_err = r.est_err;
  if (r.est_at_err > w.est_at_err) w.est_at_err = r.est_at_err;
  if (r.first_disp_err > w.first_disp_err) w.first_disp_err = r.first_disp_err;
  w.wrong += r.wrong;
  if (r.worst_cycle > w.worst_cycle) w.worst_cycle = r.worst_cycle;
  if (r.worst_straddle > w.worst_straddle) w.worst_straddle = r.worst_straddle;
  if (r.disp_err > w.disp_err) w.disp_err = r.disp_err;
  if (r.mean_err > w.mean_err) w.mean_err = r.mean_err;
  if (r.ch_rpm_err > w.ch_rpm_err) w.ch_rpm_err = r.ch_rpm_err;
  if (r.ch_mean_err > w.ch_mean_err) w.ch_mean_err = r.ch_mean_err;
  if (r.rpm_ratio > w.rpm_ratio) w.rpm_ratio = r.rpm_ratio;
  if (r.p2p_lo < w.p2p_lo) w.p2p_lo = r.p2p_lo;
  if (r.p2p_hi > w.p2p_hi) w.p2p_hi = r.p2p_hi;
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

// Pikablipit: reps kertaa idle -> rpm1 (rise ms), pito hold, paluu fall ja
// tauko gap, alipaine -55 -> -25 kPa ja syvyys 20 -> d1 samassa tahdissa.
struct Blips {
  float rpm1;
  long t0, rise, hold, fall;
  int reps;
  float idle = 850.0f;
  float d1 = 12.0f;
  long gap = 0;
  bool operator()(long t, sbx::SimParams& p) const {
    const long len = rise + hold + fall + gap;
    if (t < t0 || t > t0 + reps * len - gap) return false;
    const long u = (t - t0) % len;
    float a = 1.0f;
    if (t == t0 + reps * len - gap || u >= rise + hold + fall) {
      a = 0.0f;
    } else if (u <= rise) {
      a = static_cast<float>(u) / static_cast<float>(rise);
    } else if (u > rise + hold) {
      a = 1.0f - static_cast<float>(u - rise - hold) / static_cast<float>(fall);
    }
    p.rpm = idle + (rpm1 - idle) * a;
    p.base_kpa = -55.0f + 30.0f * a;
    p.depth_kpa = 20.0f + (d1 - 20.0f) * a;
    return true;
  }
};

// Askel/ramppi vakiokierroksilla vaiheen ph/nph kohdalla syklistä.
SimOpts phased(float rpm, int ph, int nph, long ramp, long tail) {
  SimOpts o;
  o.t0 = 2500 + static_cast<long>(120000.0f / rpm * static_cast<float>(ph) / static_cast<float>(nph));
  o.t_end = o.t0 + ramp;
  o.total = o.t_end + tail;
  if (ramp == 0) o.step_t = static_cast<double>(o.t0);
  return o;
}

}  // namespace

void test_rpm_step_down_keeps_lock() {
  // /api/sim-askel alas (D14: jokainen rpm:n lasku on askel). Kaikki kanavat
  // putoavat samaan aikaan, joten omistajuus erottaa uuden käynnin
  // vuotopulssit (T/3) omista, ja lomitus (muiden kanavien omat pulssit
  // jaksossa 1/3:n ja 2/3:n kohdalla) hyväksyy uuden jakson heti ilman
  // viitettä. Askeleen ylittävä sykli (pulssi puoliksi vanhalla nopeudella)
  // kirjataan, mutta näytön keskiarvo alkaa sen jälkeen alusta.
  const float from[] = {3000.0f, 2000.0f, 6000.0f};
  for (float r0 : from) {
    SimRun worst;
    for (int xi = 0; xi < 2; ++xi) {
      for (int ph = 0; ph < 3; ++ph) {
        sbx::SimParams p;
        p.rpm = r0;
        p.xtalk = xi ? 0.3f : 0.0f;
        p.seed = static_cast<uint32_t>(1 + ph);
        const SimOpts o = phased(r0, ph, 3, 0, 2500);
        worstOf(worst, runSim(p, o, Ramp{r0, 850.0f, -55.0f, -55.0f, 20.0f, 20.0f, o.t0, 0}));
      }
    }
    msg("rpm-askel %.0f -> 850: väärät %.0f, pisin väli %.0f ms, syklin jakso %.2f %% (ylittävä %.2f %%), näyttö 1 s %.2f %%",
        static_cast<double>(r0), static_cast<double>(worst.wrong), static_cast<double>(worst.max_gap),
        worst.worst_cycle * 100.0, worst.worst_straddle * 100.0, static_cast<double>(worst.disp_err * 100.0f));
    TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi");
    TEST_ASSERT_EQUAL_INT(0, worst.wrong);
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(4.0f * periodMs(850.0f)) + 60, static_cast<int>(worst.max_gap));
    TEST_ASSERT_LESS_THAN_FLOAT(0.03f, static_cast<float>(worst.worst_cycle));
    TEST_ASSERT_LESS_THAN_FLOAT(0.13f, static_cast<float>(worst.worst_straddle));
    TEST_ASSERT_LESS_THAN_FLOAT(0.03f, worst.disp_err);
  }
}

void test_rpm_ramp_and_throttle_chop() {
  // Kierrosluku 3000 -> 850 rampilla (vakioalipaine) ja kaasun katkaisu
  // (alipaine -15 -> -55 kPa samaan aikaan). 40 kPa/300 ms -katkaisussa 3000
  // rpm:n pulssi on suodatettuna vain ~4 kPa ja ramppi 5 kPa per sykli, mikä
  // siirtää pulssin keskikohtaa: yksittäinen jakso heittää <= 10 % (raja ei
  // silti ole väärä). Muualla < 3 %.
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
        SimOpts o;
        o.t0 = 2500 + 17 * ph;
        o.t_end = o.t0 + k.ramp;
        o.total = o.t_end + 2500;
        worstOf(worst, runSim(p, o, Ramp{3000.0f, 850.0f, k.b0, -55.0f, k.d0, 20.0f, o.t0, k.ramp}));
      }
    }
    msg("3000 -> 850 rpm %.0f ms, alipaine %.0f -> -55: väärät %.0f, pisin väli %.0f ms, syklin jakso %.2f %%, ka %.3f kPa",
        static_cast<double>(k.ramp), static_cast<double>(k.b0), static_cast<double>(worst.wrong),
        static_cast<double>(worst.max_gap), worst.worst_cycle * 100.0, static_cast<double>(worst.mean_err));
    TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi");
    TEST_ASSERT_EQUAL_INT(0, worst.wrong);
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(4.0f * periodMs(850.0f)) + 60, static_cast<int>(worst.max_gap));
    TEST_ASSERT_LESS_THAN_FLOAT(k.cycle_tol, static_cast<float>(worst.worst_cycle));
    TEST_ASSERT_LESS_THAN_FLOAT(0.1f, worst.mean_err);
  }
}

void test_rpm_drop_every_channel_recovers() {
  // Pudotus 1200-1500 rpm:iin ristivuodolla: uuden käynnin T/3 osui vanhaan
  // jaksoon, ja kanava saattoi lukittua pysyvästi vuotopulssien rytmiin
  // (rpm 3x, D1-keskiarvo ~1,6 kPa pielessä -> väärä tuomio, D2 1,5 kPa).
  // Jokaisen kanavan rpm ja keskiarvo on oltava oikein 1,5 s muutoksen
  // jälkeen, eikä yhtään väärää sykliä saa kirjata.
  struct Case {
    float r0, r1, b0, b1, d0, d1;
    long ramp;
  };
  const Case cases[] = {{4500.0f, 1500.0f, -55.0f, -55.0f, 20.0f, 20.0f, 0},
                        {3500.0f, 1200.0f, -55.0f, -55.0f, 20.0f, 20.0f, 0},
                        {3000.0f, 1200.0f, -55.0f, -55.0f, 20.0f, 20.0f, 0},
                        {4500.0f, 1500.0f, -10.0f, -50.0f, 8.0f, 20.0f, 150}};
  for (const Case& k : cases) {
    SimRun worst;
    for (int xi = 0; xi < 2; ++xi) {
      for (int ph = 0; ph < 3; ++ph) {
        sbx::SimParams p;
        p.rpm = k.r0;
        p.base_kpa = k.b0;
        p.depth_kpa = k.d0;
        p.xtalk = xi ? 0.3f : 0.0f;
        p.seed = static_cast<uint32_t>(1 + ph);
        SimOpts o = phased(k.r0, ph, 3, k.ramp, 3000);
        o.check_from = o.t_end + 1500;
        worstOf(worst, runSim(p, o, Ramp{k.r0, k.r1, k.b0, k.b1, k.d0, k.d1, o.t0, k.ramp}));
      }
    }
    msg("%.0f -> %.0f rpm (%.0f ms): väärät %.0f, kanavan rpm %.2f %%, keskiarvo %.3f kPa 1,5 s jälkeen, väli %.0f ms",
        static_cast<double>(k.r0), static_cast<double>(k.r1), static_cast<double>(k.ramp),
        static_cast<double>(worst.wrong), static_cast<double>(worst.ch_rpm_err * 100.0f),
        static_cast<double>(worst.ch_mean_err), static_cast<double>(worst.max_gap));
    TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi");
    TEST_ASSERT_EQUAL_INT(0, worst.wrong);
    TEST_ASSERT_LESS_THAN_FLOAT(0.03f, worst.ch_rpm_err);
    TEST_ASSERT_LESS_THAN_FLOAT(0.15f, worst.ch_mean_err);
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(4.0f * periodMs(k.r1)) + 60, static_cast<int>(worst.max_gap));
  }
}

void test_throttle_blip_engine_sim() {
  // Kaasunlisäys tyhjäkäynniltä ja paluu (synkronoinnissa käyttäjä blippaa ja
  // odottaa tyhjäkäyntiä): 850 -> 1500...3000 rpm, nousu 150/300/600 ms.
  // Pulssi kutistuu nousun aikana suodatettuna 20 -> 4,5...10 kPa; kaikki
  // kanavat ohittavat silloin pulsseja yhtä aikaa, ja ohitetun pulssin 2T-väli
  // voisi näyttää yhdeltä sykliltä (jokaisen kanavan ikkunassa yksi merkki
  // muilta). Lomitus vaatii merkit 1/3:n ja 2/3:n kohdalle. Raja kanavan
  // pisimmälle välille: nousu >= 300 ms -> 300 ms, 150 ms -> 400 ms.
  struct Case {
    float rpm1;
    long rise;
    int gap_ms;
  };
  const Case cases[] = {{1500.0f, 150, 400}, {1500.0f, 300, 300}, {1500.0f, 600, 300},
                        {2000.0f, 150, 400}, {2000.0f, 300, 300}, {2000.0f, 600, 300},
                        {2500.0f, 150, 400}, {2500.0f, 300, 300}, {2500.0f, 600, 300},
                        {3000.0f, 150, 400}, {3000.0f, 300, 300}, {3000.0f, 600, 300}};
  for (const Case& k : cases) {
    SimRun worst;
    float ratio = 10.0f;
    for (int run = 0; run < 7; ++run) {
      // 6 ajoa + toistetapaus: 3000 rpm / 300 ms, xtalk 0,3, siemen 4, alku
      // 2523 ms - kaikki kanavat ohittivat pulsseja, ja vanhentunut muiden
      // kanavien syvyys (ennen tämän kanavan omaa pulssia) piti kynnyksen
      // liian matalana: 2T kirjattiin.
      const bool repro = run == 6;
      if (repro && !(k.rpm1 == 3000.0f && k.rise == 300)) continue;
      const int xi = run % 2, ph = run / 2;
      sbx::SimParams p;
      p.xtalk = (xi || repro) ? 0.3f : 0.0f;
      p.seed = repro ? 4u : static_cast<uint32_t>(1 + 2 * ph + xi);
      SimOpts o;
      o.t0 = repro ? 2523 : 2500 + 47 * ph;
      o.t_end = o.t0 + k.rise + 1000;
      o.total = o.t_end + 2000;
      const SimRun r = runSim(p, o, Blip{k.rpm1, o.t0, k.rise});
      worstOf(worst, r);
      if (r.peak_shown / k.rpm1 < ratio) ratio = r.peak_shown / k.rpm1;
    }
    msg("blip 850 -> %.0f rpm, nousu %.0f ms: väärät %.0f, pisin väli %.0f ms, syklin jakso %.2f %%, huippu %.2f x",
        static_cast<double>(k.rpm1), static_cast<double>(k.rise), static_cast<double>(worst.wrong),
        static_cast<double>(worst.max_gap), worst.worst_cycle * 100.0, static_cast<double>(ratio));
    TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi");
    TEST_ASSERT_EQUAL_INT(0, worst.wrong);
    TEST_ASSERT_LESS_OR_EQUAL(k.gap_ms, static_cast<int>(worst.max_gap));
    TEST_ASSERT_LESS_THAN_FLOAT(0.03f, static_cast<float>(worst.worst_cycle));
    TEST_ASSERT_GREATER_THAN_FLOAT(0.85f, ratio);
  }
}

void test_throttle_blip_tracks_rpm_and_vacuum() {
  // Katselmoinnin blip EngineSimillä: 850 -> 2500 rpm ja -55 -> -25 kPa,
  // syvyys 20 -> 12 (300 ms ylös, 300 ms pito, 700 ms alas). Vanhalla
  // verhokäyrällä rpm ja keskiarvo jäätyivät ~0,9 s.
  long worst_gap = 0;
  float worst_ratio = 10.0f;
  float top_ratio = 0.0f;
  for (uint32_t seed = 1; seed <= 2; ++seed) {
    for (int xi = 0; xi < 2; ++xi) {
      sbx::SimParams p;
      p.seed = seed;
      p.xtalk = xi ? 0.3f : 0.0f;
      SimOpts o;
      o.t0 = 2500;
      o.t_end = 3800;
      o.total = 6300;
      const SimRun r = runSim(p, o, Blip{2500.0f, 2500, 300});
      TEST_ASSERT_FALSE_MESSAGE(r.dropped, "has_pulse putosi");
      TEST_ASSERT_EQUAL_INT(0, r.wrong);
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
  TEST_ASSERT_LESS_OR_EQUAL(300, static_cast<int>(worst_gap));
  // 8 syklin keskiarvo kulkee perässä, mutta ei jäädy, jää vajaaksi eikä
  // ylitä huippua (väärä lyhyt jakso näkyisi ylityksenä).
  TEST_ASSERT_GREATER_THAN_FLOAT(0.9f, worst_ratio);
  TEST_ASSERT_LESS_THAN_FLOAT(1.03f, top_ratio);
}

void test_vacuum_steps_with_crosstalk_engine_sim() {
  // Alipaineen hetkellinen askel ylös ja alas ristivuodolla (xtalk 0,3)
  // vakiokierroksilla, askel 16 kohdassa sykliä: askeleen kanssa samaan aikaan
  // osuva vuotopulssi näytti omalta, ja kanava kirjasi T/3-jaksoja (rpm 3x).
  // Askel on kaikilla kanavilla sama, joten omistajuus ei pidä sitä omana
  // pulssina. Askel voi peittää yhden pulssin (väli <= 3 jaksoa).
  struct Case {
    float rpm, b0, b1;
    int phases;
  };
  const Case cases[] = {{850.0f, -53.0f, -40.0f, 16}, {850.0f, -40.0f, -53.0f, 16}, {850.0f, -55.0f, -30.0f, 16},
                        {850.0f, -30.0f, -55.0f, 16}, {600.0f, -58.0f, -45.0f, 8}};
  // Tarkistajan toistetapaukset (T/3 hyväksyttiin): 600 rpm siemen 1 askel
  // 2512 ms:ssa ja 850 rpm -55 -> -30 siemen 2 askel 2623 ms:ssa.
  struct Repro {
    float rpm, b0, b1;
    uint32_t seed;
    long t0;
  };
  const Repro repro[] = {{600.0f, -58.0f, -45.0f, 1, 2512}, {850.0f, -55.0f, -30.0f, 2, 2623}};
  for (const Case& k : cases) {
    SimRun worst;
    for (int ph = 0; ph <= k.phases; ++ph) {
      sbx::SimParams p;
      p.rpm = k.rpm;
      p.base_kpa = k.b0;
      p.xtalk = 0.3f;
      p.seed = static_cast<uint32_t>(1 + ph % 2);
      SimOpts o = phased(k.rpm, ph, k.phases, 0, 1500);
      bool run = ph < k.phases;
      for (const Repro& rp : repro) {
        if (ph == k.phases && rp.rpm == k.rpm && rp.b0 == k.b0 && rp.b1 == k.b1) {
          p.seed = rp.seed;
          o.t0 = o.t_end = rp.t0;
          o.step_t = static_cast<double>(rp.t0);
          o.total = rp.t0 + 1500;
          run = true;
        }
      }
      if (run) worstOf(worst, runSim(p, o, Ramp{k.rpm, k.rpm, k.b0, k.b1, 20.0f, 20.0f, o.t0, 0}));
    }
    msg("%.0f rpm askel %.0f -> %.0f kPa, xtalk 0,3: väärät %.0f, rpm max %.3f x, pisin väli %.2f jaksoa",
        static_cast<double>(k.rpm), static_cast<double>(k.b0), static_cast<double>(k.b1),
        static_cast<double>(worst.wrong), static_cast<double>(worst.rpm_ratio),
        static_cast<double>(static_cast<float>(worst.max_gap) / periodMs(k.rpm)));
    TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi");
    TEST_ASSERT_EQUAL_INT(0, worst.wrong);
    TEST_ASSERT_LESS_THAN_FLOAT(1.2f, worst.rpm_ratio);
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(3.1f * periodMs(k.rpm)), static_cast<int>(worst.max_gap));
  }
}

void test_fast_vacuum_ramps_with_crosstalk() {
  // Nopea kaasun liike (25 kPa 20-50 ms:ssa) ristivuodolla: sama vuotopulssin
  // ja alipaineen muutoksen yhdistelmä kuin askeleessa, 16 kohdassa sykliä.
  const long ramps[] = {20, 50};
  for (long ramp : ramps) {
    for (int dir = 0; dir < 2; ++dir) {
      const float b0 = dir ? -30.0f : -55.0f;
      const float b1 = dir ? -55.0f : -30.0f;
      SimRun worst;
      for (int ph = 0; ph < 16; ++ph) {
        sbx::SimParams p;
        p.base_kpa = b0;
        p.xtalk = 0.3f;
        p.seed = static_cast<uint32_t>(1 + ph % 2);
        const SimOpts o = phased(850.0f, ph, 16, ramp, 1500);
        worstOf(worst, runSim(p, o, Ramp{850.0f, 850.0f, b0, b1, 20.0f, 20.0f, o.t0, ramp}));
      }
      msg("ramppi %.0f -> %.0f kPa %.0f ms, xtalk 0,3: väärät %.0f, rpm max %.3f x, pisin väli %.2f jaksoa",
          static_cast<double>(b0), static_cast<double>(b1), static_cast<double>(ramp),
          static_cast<double>(worst.wrong), static_cast<double>(worst.rpm_ratio),
          static_cast<double>(static_cast<float>(worst.max_gap) / periodMs(850.0f)));
      TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi");
      TEST_ASSERT_EQUAL_INT(0, worst.wrong);
      TEST_ASSERT_LESS_THAN_FLOAT(1.2f, worst.rpm_ratio);
    }
  }
}

void test_ownership_fallback_hoses_off() {
  // Omistajuus vaatii, että jokin muu kanava kantaa tietoa. Letku irti (rivi
  // 6): sen kanava on tasainen, mutta sylinterin vuoto näkyy yhä muilla
  // kanavilla. Yksi letku irti: kaksi muuta vertaavat toisiaan (vuoto näkyy
  // molemmissa yhtä suurena -> ei omaa). Kaksi irti: varatila, jossa rajapulssien
  // on oltava samankokoiset ja jakson viitteen +-10 % sisällä. Kumpikaan ei
  // saa kirjata väärää sykliä 18 kPa:n alipaineaskelissa (xtalk 0,3 ja 0,5);
  // varatilassa myös 850 -> 2500 rpm:n blipissä (lukitus saa siinä katketa).
  const int masks[] = {4, 6};  // kanava 3 irti; kanavat 2 ja 3 irti
  for (int mask : masks) {
    SimRun worst;
    for (int dir = 0; dir < 4; ++dir) {
      const float b0 = (dir & 1) ? -35.0f : -53.0f;
      const float b1 = (dir & 1) ? -53.0f : -35.0f;
      for (int ph = 0; ph < 8; ++ph) {
        sbx::SimParams p;
        p.base_kpa = b0;
        p.xtalk = (dir & 2) ? 0.5f : 0.3f;
        p.seed = static_cast<uint32_t>(1 + ph % 2);
        SimOpts o = phased(850.0f, ph, 8, 0, 2000);
        o.hose_mask = mask;
        o.check_from = o.t0 + 1500;
        worstOf(worst, runSim(p, o, Ramp{850.0f, 850.0f, b0, b1, 20.0f, 20.0f, o.t0, 0}));
      }
    }
    msg("letkut irti (maski %.0f), askeleet: väärät %.0f, kanavan rpm %.2f %%, ka %.3f kPa, väli %.0f ms",
        static_cast<double>(mask), static_cast<double>(worst.wrong), static_cast<double>(worst.ch_rpm_err * 100.0f),
        static_cast<double>(worst.ch_mean_err), static_cast<double>(worst.max_gap));
    TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi");
    TEST_ASSERT_EQUAL_INT(0, worst.wrong);
    TEST_ASSERT_LESS_THAN_FLOAT(0.03f, worst.ch_rpm_err);
    TEST_ASSERT_LESS_THAN_FLOAT(0.15f, worst.ch_mean_err);
    // Varatilassa (kaksi irti) ohitetun pulssin jälkeinen sykli hylätään
    // vielä, koska edellinen väli oli 2T: yksi jakso enemmän.
    const float lim = mask == 6 ? 4.1f : 3.1f;
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(lim * periodMs(850.0f)), static_cast<int>(worst.max_gap));
  }
  // Yksi letku irti ja rpm-askel 3000 -> 850: kaksi muuta kanavaa vertaavat
  // toisiaan. Sylinterin, jonka letku on irti, vuoto näkyy molemmissa yhtä
  // suurena; ilman marginaalia (kOwnMargin) tasapeli tulkittiin omaksi
  // pulssiksi, ja kanava menetti lukituksen.
  SimRun one_off;
  for (int ph = 0; ph < 6; ++ph) {
    for (uint32_t seed = 1; seed <= 4; ++seed) {
      sbx::SimParams p;
      p.rpm = 3000.0f;
      p.xtalk = 0.3f;
      p.seed = seed;
      SimOpts o = phased(3000.0f, ph, 6, 0, 2000);
      o.hose_mask = 4;
      worstOf(one_off, runSim(p, o, Ramp{3000.0f, 850.0f, -55.0f, -55.0f, 20.0f, 20.0f, o.t0, 0}));
    }
  }
  msg("yksi letku irti, rpm-askel 3000 -> 850: väärät %.0f, pisin väli %.0f ms", static_cast<double>(one_off.wrong),
      static_cast<double>(one_off.max_gap));
  TEST_ASSERT_FALSE_MESSAGE(one_off.dropped, "has_pulse putosi");
  TEST_ASSERT_EQUAL_INT(0, one_off.wrong);

  // Varatilan blip: kaksi letkua irti. Lukitus saa blipissä katketa, mutta
  // RpmEstimaten on oltava oikein 1 s blipin jälkeen. has_pulsen
  // aikakatkaisu tyhjentää omien pulssien historian; muuten haku jatkoi
  // aikakatkaisua edeltävistä pulsseista, lukittui kesken hidastuvan blipin
  // (viite 76 ms), eikä varatilan +-kFallbackWindow seurannut loppua: has_pulse
  // putosi uudelleen ja RpmEstimate oli 1 s blipin jälkeen 0 (viimeiset
  // kaksi ajoa, xtalk 0,3).
  SimRun blip;
  float blip_est = 0.0f;
  for (int run = 0; run < 8; ++run) {
    {
      const int ph = run % 3;
      sbx::SimParams p;
      p.xtalk = run >= 3 ? 0.3f : 0.0f;
      p.seed = static_cast<uint32_t>(run < 6 ? 1 + ph : 2);
      SimOpts o;
      o.t0 = run < 6 ? 2500 + 47 * ph : (run == 6 ? 2594 : 2735);
      o.t_end = o.t0 + 1300;
      o.total = o.t_end + 2000;
      o.hose_mask = 6;
      o.check_from = o.t_end + 1500;
      o.est_at = o.t_end + 1000;
      const SimRun r = runSim(p, o, Blip{2500.0f, o.t0, 300});
      worstOf(blip, r);
      if (r.est_at_err > blip_est) blip_est = r.est_at_err;
    }
  }
  msg("varatilan blip: RpmEstimate 1 s blipin jälkeen %.2f %%", static_cast<double>(blip_est * 100.0f));
  TEST_ASSERT_LESS_THAN_FLOAT(0.03f, blip_est);
  // Varatilan kaasun katkaisu 3000 -> 850 rpm, -15 -> -55 kPa 300 ms:ssa:
  // pulssi kasvaa, ja uuden käynnin vuotopulssi on vanhan oman kokoinen vain,
  // jos rajapulssien samankokoisuutta ei vaadita (tapaukset, joissa se
  // hyväksyttiin). Yhdeltä kanavalta ei voi erottaa kaikkia: ks. dsp.h.
  for (uint32_t seed = 1; seed <= 2; ++seed) {
    for (long t0 = 2500; t0 <= 2506; t0 += 6) {
      sbx::SimParams p;
      p.rpm = 3000.0f;
      p.base_kpa = -15.0f;
      p.depth_kpa = 10.0f;
      p.xtalk = 0.3f;
      p.seed = seed;
      SimOpts o;
      o.t0 = t0;
      o.t_end = t0 + 300;
      o.total = o.t_end + 2500;
      o.hose_mask = 6;
      o.check_from = o.t_end + 2000;
      worstOf(blip, runSim(p, o, Ramp{3000.0f, 850.0f, -15.0f, -55.0f, 10.0f, 20.0f, t0, 300}));
    }
  }
  msg("varatila (kaksi letkua irti), blip ja kaasun katkaisu: väärät %.0f, rpm lopuksi %.2f %%",
      static_cast<double>(blip.wrong), static_cast<double>(blip.ch_rpm_err * 100.0f));
  TEST_ASSERT_EQUAL_INT(0, blip.wrong);
  TEST_ASSERT_LESS_THAN_FLOAT(0.03f, blip.ch_rpm_err);
  // Varatila xtalk 0,5:llä: vuoto on puolet omasta, mutta kasvavan pulssin
  // ja päällekkäisten imujen kanssa mitattu suhde lähestyy 0,7:ää.
  //  - kaasun katkaisu 3000 -> 850 / 300 ms: ilman viitettä ensimmäinen uuden
  //    käynnin vuoto osui vanhaan jaksoon (10/24 ajoa); ilman edellisen välin
  //    ehtoa myös vanhentuneen viitteen 2T kirjattiin;
  //  - blip: kolmikko (oma 13,1, vuoto 9,2, vuoto 8,9) kelpasi, kun vain
  //    naapuriparit verrattiin (9,2 / 13,1 = 0,70) - kaikkien parien on oltava
  //    samankokoisia.
  SimRun fb5;
  for (int run = 0; run < 9; ++run) {
    sbx::SimParams p;
    p.xtalk = 0.5f;
    SimOpts o;
    if (run < 6) {
      const int ph = run < 3 ? 0 : 1 + 2 * (run - 3);
      p.rpm = 3000.0f;
      p.base_kpa = -15.0f;
      p.depth_kpa = 10.0f;
      p.seed = static_cast<uint32_t>(1 + run % 3);
      o.t0 = 2500 + 7 * ph;
      o.t_end = o.t0 + 300;
      o.total = o.t_end + 2500;
      o.hose_mask = 6;
      worstOf(fb5, runSim(p, o, Ramp{3000.0f, 850.0f, -15.0f, -55.0f, 10.0f, 20.0f, o.t0, 300}));
    } else {
      const long t0s[3] = {2500 + static_cast<long>(periodMs(850.0f) * 4.0f / 6.0f),
                           2500 + static_cast<long>(periodMs(850.0f) * 3.0f / 6.0f), 2551};
      p.seed = static_cast<uint32_t>(run == 7 ? 2 : 1);
      o.t0 = t0s[run - 6];
      o.t_end = o.t0 + 1300;
      o.total = o.t_end + 2000;
      o.hose_mask = run == 8 ? 5 : 6;
      worstOf(fb5, runSim(p, o, Blip{run == 8 ? 3000.0f : 1500.0f, o.t0, 300}));
    }
  }
  msg("varatila xtalk 0,5: kaasun katkaisu ja blip, väärät %.0f", static_cast<double>(fb5.wrong));
  TEST_ASSERT_EQUAL_INT(0, fb5.wrong);
  // Letku irrotetaan käynnin aikana: kanava kantaa tietoa vielä has_pulsen
  // ajan, mutta sen omia pulsseja ei enää tule. Muut kanavat eivät saa
  // pudottaa has_pulsea (rivi 6 vaatii, että ne pulssivat).
  SimRun unplug;
  for (int ph = 0; ph < 4; ++ph) {
    sbx::SimParams p;
    p.xtalk = 0.3f;
    p.seed = static_cast<uint32_t>(1 + ph);
    SimOpts o;
    o.t0 = 2500 + 31 * ph;
    o.t_end = o.t0;
    o.total = o.t0 + 2500;
    o.hose_mask = 4;
    o.hose_t = o.t0;
    worstOf(unplug, runSim(p, o, [](long, sbx::SimParams&) { return false; }));
  }
  msg("letku irrotetaan käynnin aikana: muiden pisin väli %.0f ms, väärät %.0f", static_cast<double>(unplug.max_gap),
      static_cast<double>(unplug.wrong));
  TEST_ASSERT_FALSE_MESSAGE(unplug.dropped, "has_pulse putosi muilta kanavilta");
  TEST_ASSERT_EQUAL_INT(0, unplug.wrong);
  TEST_ASSERT_LESS_OR_EQUAL(700, static_cast<int>(unplug.max_gap));
}

// Käynnistys ja vikatilat (F): väärää sykliä ei saa kirjata millään kanavalla.
void test_engine_start_and_restart_never_lock_on_leaks() {
  // Käynnistys ilmanpaineesta ja uudelleenkäynnistys 1,1-1,5 s:n pysähdyksen
  // jälkeen (has_pulse ehtinyt pudota). Kun omistajuus vertasi vain
  // lukittuihin kanaviin, käynnistyksessä jokainen pulssi oli ehdokas: kanava
  // saattoi lukittua vuotopulssien rytmiin (3 x rpm 15-30 s), ja sen historia
  // esti lomitusehdolla muiden lukituksen. Heikko sylinteri (0,3-0,5 x)
  // ristivuodolla teki tämän useimmissa käynnistyksissä. Nyt omistajuus
  // vertaa aina fysikaalisesti kaikkiin asettuneisiin kanaviin, eikä kanava
  // ilman omaa pulssia estä muita. Heikon sylinterin kanava saa jäädä
  // lukitsematta; muiden on lukituttava D:n rajassa + 1 jakso, ja
  // RpmEstimate on oikein 1 s käynnistyksestä.
  struct Case {
    float rpm, xt, weak;
    bool restart;
    long delay;    // pysähdyksen kesto (uudelleenkäynnistys)
    uint32_t rseed;  // toistetapaus ensimmäiseksi ajoksi (0 = ei): siemen ja pysähdys
    long roff;
  };
  const Case cases[] = {{850.0f, 0.5f, 1.0f, true, 1500, 0, 0},  {950.0f, 0.5f, 1.0f, true, 1300, 0, 0},
                        {850.0f, 0.3f, 0.4f, false, 0, 0, 0},    {600.0f, 0.3f, 0.4f, false, 0, 0, 0},
                        {600.0f, 0.3f, 0.5f, true, 1200, 0, 0},  {950.0f, 0.2f, 0.3f, false, 0, 0, 0},
                        // Heikko kanava ei saa hidastaa muita (850 rpm: 549 ms, jos sen
                        // pulssia vaadittaisiin aina), eikä se saa omistaa vuotoa, kun
                        // muut eivät vielä ole eläviä (600 rpm, siemen 3, 2510 ms).
                        {850.0f, 0.2f, 0.5f, true, 1220, 3, 2550}, {600.0f, 0.3f, 0.3f, true, 1100, 3, 2510},
                        // Lyhyt pysähdys (600 ms < pulse_timeout_s): has_pulse ei putoa
                        // eikä historia tyhjene; kaikki vaikenivat, joten kaikki vertaavat
                        // yhä toisiinsa, eikä käynnistysaskel ole kenenkään oma pulssi.
                        {600.0f, 0.0f, 1.0f, true, 600, 5, 2621},
                        // 3000 rpm xtalk 0,5 (D: <= 3,5 jaksoa + 40 ms = 180 ms): lukitus
                        // vaatii elävät todistajat, joilla ei vielä ole has_pulsea - jos
                        // liveys vaatisi lukituksen, siemen 1 lukittuisi 221 ms:ssa.
                        {3000.0f, 0.5f, 1.0f, false, 0, 0, 0}};
  for (const Case& k : cases) {
    SimRun worst;
    for (int run = 0; run < 6; ++run) {
      const bool r = run == 0 && k.rseed > 0;
      sbx::SimParams p;
      p.rpm = k.rpm;
      p.xtalk = k.xt;
      p.seed = r ? k.rseed : static_cast<uint32_t>(1 + run % 3);
      const long T = static_cast<long>(periodMs(k.rpm));
      SimOpts o;
      if (k.restart) {
        // Tarkistajan toisto: 850 rpm, siemen 2, pysähdys 2503 ms + 1500 ms.
        o.off_t1 = r ? k.roff : 2503 + (run / 3) * T / 2;
        o.off_t2 = o.off_t1 + k.delay;
        // Hetkellinen sammutus katkaisee kesken olevan imun (simulaattorin
        // artefakti): tarkistus alkaa pysähdyksen jälkeen.
        o.watch_from = o.off_t1 + 200;
      } else {
        o.off_t1 = 0;
        o.off_t2 = 2000 + (run / 3) * T / 2 + 7 * run;
        o.watch_from = o.off_t2;
      }
      o.t0 = o.t_end = o.off_t2;
      o.total = o.off_t2 + 3000;
      o.est_from = o.off_t2 + 1000;
      o.check_from = o.off_t2 + 2500;
      if (k.weak < 1.0f) {
        o.weak_ch = static_cast<int>(p.seed) % 3;
        o.weak_scale = k.weak;
      }
      worstOf(worst, runSim(p, o, [](long, sbx::SimParams&) { return false; }));
    }
    msg(k.restart ? "uudelleenkäynnistys %.0f rpm xtalk %.1f, heikko %.1f: väärät %.0f, lukitus %.0f ms, "
                    "RpmEstimate 1 s jälkeen %.2f %%"
                  : "käynnistys %.0f rpm xtalk %.1f, heikko %.1f: väärät %.0f, lukitus %.0f ms, "
                    "RpmEstimate 1 s jälkeen %.2f %%",
        static_cast<double>(k.rpm), static_cast<double>(k.xt),
        static_cast<double>(k.weak), static_cast<double>(worst.wrong), static_cast<double>(worst.lock_ms),
        static_cast<double>(worst.est_err * 100.0f));
    TEST_ASSERT_EQUAL_INT(0, worst.wrong);
    TEST_ASSERT_TRUE(worst.finite);
    TEST_ASSERT_GREATER_OR_EQUAL(0, worst.lock_ms);
    // Tavoite on D:n raja + 1 jakso; mitattu pysyy D:n rajassa 3,5 jaksoa + 40
    // ms myös heikon sylinterin kanssa. Jos heikon kanavan pulssia vaadittaisiin
    // aina (se on elävä mutta ohittaa pulssejaan), terveet lukittuisivat 600
    // rpm:llä vasta ~770 ms:ssa.
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(3.5f * periodMs(k.rpm)) + 40, static_cast<int>(worst.lock_ms));
    TEST_ASSERT_LESS_THAN_FLOAT(0.03f, worst.est_err);
    TEST_ASSERT_LESS_THAN_FLOAT(0.03f, worst.ch_rpm_err);
  }
}

void test_dead_channel_during_blip() {
  // Kanava kuolee kaasunlisäyksen aikana (anturi alas 400 ms - lyhyempi kuin
  // fault_hold_s, joten vikaa ei vielä ilmoiteta - tai letku irrotetaan) tai
  // letku on irti koko ajan. Kaikki kanavat ohittavat nopeassa lisäyksessä
  // pulsseja; jos ainoa elävä todistaja hyväksyy merkin kumman tahansa paikan
  // (1/3 tai 2/3) kohdalla, ohitetun pulssin 2T-väli kirjataan. Opittu
  // sytytysjärjestys sitoo todistajan omaan paikkaansa, ja tauko edellisestä
  // hyväksytystä syklistä vaatii kaikki elävät todistajat (hold-vaiheessa vanha
  // 850 rpm:n viite sai 4T-välin näyttämään tavalliselta). Kuollut kanava ei
  // saa venyttää muiden välejä: anturi tunnistetaan heti mV:stä (ja sen
  // palautumisen ylitys ei näy pudotuksena), irrotettu letku lakkaa
  // todistamasta kahden syklin jälkeen nykyisellä kierrosluvulla, eikä toisen
  // kanavan nousu (letku irti -> ilmanpaine) ole pudotus. Raja: B:n raja +
  // 150 ms (xtalk 0,5: 450 + 150).
  struct Case {
    int mode;     // 0 = anturi alas (dur ms), 1 = letku irti koko ajan, 2 = letku irrotetaan
    float rpm1, xt;
    long rise;
    long pull;    // tila 0/2: vian alku lisäyksen alusta (ms)
    int rep[4][2];  // toistetapaukset (siemen, vaihe /12), siemen 0 = ei
    long dur;     // tila 0: anturivian kesto
    int lim;      // terveiden välin raja (ms), 0 = B:n raja + 150 ms
  };
  const Case cases[] = {
      {0, 3000.0f, 0.0f, 150, 0, {{0, 0}}, 400, 0},
      // Liveyttä ja asettumista mittaavat tapaukset (mitattu 412 ja 273 ms):
      // asettumaton kanava syvyysviitteenä (peerDepth) nosti ensimmäisen 533
      // ms:iin, 8 näytteen asettuminen anturivian jälkeen jälkimmäisen 328
      // ms:iin (palautumisen ylitys näytti pudotukselta).
      {0, 3000.0f, 0.5f, 150, 0, {{0, 0}}, 400, 470},
      {0, 2500.0f, 0.0f, 300, 100, {{0, 0}}, 100, 300},
      // 482 ms, kun asettumaton (vikaantunut) kanava oli vertailukanava:
      {0, 3000.0f, 0.3f, 300, 0, {{3, 8}, {0, 0}}, 400, 0},
      {0, 2000.0f, 0.0f, 300, 0, {{0, 0}}, 400, 0},
      {1, 2500.0f, 0.3f, 150, 0, {{0, 0}}, 0, 0},
      {1, 3000.0f, 0.5f, 150, 0, {{0, 0}}, 0, 0},
      {1, 2500.0f, 0.5f, 150, 0, {{0, 0}}, 0, 0},
      // 4T ennen tauko-ehtoa ja 805 ms:n väli, kun irrotettu letku todisti
      // pisimmän jakson ajan:
      {2, 3000.0f, 0.5f, 150, 0, {{5, 1}, {6, 6}, {6, 4}, {7, 8}}, 0, 0},
      // 502-634 ms, kun pudotus mitattiin keskikohdan yli (letkun nousu) tai
      // irrotettu letku todisti pisimmän jakson ajan:
      {2, 3000.0f, 0.3f, 300, 150, {{8, 1}, {1, 7}, {0, 0}}, 0, 0},
  };
  for (const Case& k : cases) {
    SimRun worst;
    for (int run = 0; run < 12; ++run) {
      const bool r = run < 4 && k.rep[run][0] > 0;
      sbx::SimParams p;
      p.xtalk = k.xt;
      p.seed = static_cast<uint32_t>(r ? k.rep[run][0] : 1 + run % 4);
      const float ph = static_cast<float>(r ? k.rep[run][1] : run);
      SimOpts o;
      o.t0 = 2500 + static_cast<long>(periodMs(850.0f) * (ph + 0.3f) / 12.0f);
      o.t_end = o.t0 + k.rise + 1000;
      o.total = o.t_end + 2000;
      if (k.mode == 0) {
        o.sens_ch = static_cast<int>(p.seed) % 3;
        o.sens_t1 = o.t0 + k.pull;
        o.sens_t2 = o.sens_t1 + k.dur;
      } else {
        o.hose_mask = 1 << (static_cast<int>(p.seed) % 3);
        o.hose_t = k.mode == 2 ? o.t0 + k.pull : 0;
      }
      worstOf(worst, runSim(p, o, Blip{k.rpm1, o.t0, k.rise}));
    }
    const char* fmt[3] = {"anturi alas, blip 850 -> %.0f rpm nousu %.0f ms, xtalk %.1f: väärät %.0f, väli %.0f ms",
                          "letku irti alusta, blip 850 -> %.0f rpm nousu %.0f ms, xtalk %.1f: väärät %.0f, väli %.0f ms",
                          "letku irti lisäyksessä, blip 850 -> %.0f rpm nousu %.0f ms, xtalk %.1f: väärät %.0f, väli %.0f ms"};
    msg(fmt[k.mode], static_cast<double>(k.rpm1), static_cast<double>(k.rise), static_cast<double>(k.xt),
        static_cast<double>(worst.wrong), static_cast<double>(worst.max_gap));
    TEST_ASSERT_EQUAL_INT(0, worst.wrong);
    TEST_ASSERT_TRUE(worst.finite);
    TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi elävältä kanavalta");
    if (k.mode != 1) {
      const int lim = k.lim > 0 ? k.lim : (k.rise >= 300 ? 300 : (k.xt > 0.4f ? 450 : 400)) + 150;
      TEST_ASSERT_LESS_OR_EQUAL(lim, static_cast<int>(worst.max_gap));
    }
  }
}

void test_reconnected_hose_is_not_a_witness() {
  // Letku irti 50-400 ms ja takaisin (F1). Irrotettu kanava on ilmanpaineessa,
  // ja takaisin kytkettäessä sen signaali putoaa koko alipaineen verran
  // (joutokäynnillä 55 kPa, blipin huipulla 25 kPa). 5a45dc7 piti tätä
  // tasoaskelta omana pulssina: kanava kirjasi vuotosyklin ja todisti terveen
  // kanavan 2T-välin (tarkistajan toistot alla). Nyt kanava tunnistetaan
  // irrotetuksi tasostaan (ilmanpaineessa ja muiden yläpuolella) ja asettuu
  // takaisin kytkettäessä kuten anturivian jälkeen: askel ei ole oma pulssi
  // eikä todistaja. Väärää sykliä ei saa kirjata millään kanavalla (myöskään
  // takaisin kytketyllä); terveiden väli <= B:n raja + 150 ms.
  struct Case {
    float idle, rpm1;
    int reps;      // 0 = Blip (pito 300, paluu 700), muuten pikablipit 150/0/300
    float xt;
    uint32_t seed;
    long t0, off, dur;  // letku irti [t0 + off, t0 + off + dur)
    int ch;
  };
  const Case cases[] = {
      {950.0f, 2500.0f, 2, 0.0f, 1, 2523, 120, 400, 0},  // toisto 1: 2T kanavalla 2 (t0 + 617)
      {850.0f, 3000.0f, 0, 0.0f, 3, 2571, -60, 50, 0},   // toisto 2: vuotosykli ja 2T
      {850.0f, 3000.0f, 1, 0.0f, 1, 2512, -60, 50, 0},   {850.0f, 3000.0f, 1, 0.5f, 2, 2547, 120, 150, 2},
      {950.0f, 2500.0f, 2, 0.3f, 3, 2531, -60, 50, 0},   {950.0f, 2500.0f, 2, 0.5f, 4, 2562, -60, 50, 1},
      {850.0f, 3000.0f, 1, 0.3f, 5, 2539, 0, 400, 1},    {950.0f, 3000.0f, 0, 0.3f, 6, 2519, -60, 50, 2},
  };
  SimRun worst, worst5;
  for (const Case& k : cases) {
    sbx::SimParams p;
    p.rpm = k.idle;
    p.xtalk = k.xt;
    p.seed = k.seed;
    SimOpts o;
    o.t0 = k.t0;
    o.t_end = o.t0 + (k.reps == 0 ? 1150 : 450 * k.reps);
    o.total = o.t_end + 1500;
    o.hose_mask = 1 << k.ch;
    o.hose_t = o.t0 + k.off;
    o.hose_t2 = o.hose_t + k.dur;
    const SimRun r = k.reps == 0 ? runSim(p, o, Blip{k.rpm1, o.t0, 150})
                                 : runSim(p, o, Blips{k.rpm1, o.t0, 150, 0, 300, k.reps, k.idle});
    worstOf(k.xt > 0.4f ? worst5 : worst, r);
  }
  msg("letku irti 50-400 ms blipin ympärillä: väärät %.0f, terveiden väli %.0f ms (xtalk 0,5: väärät %.0f, väli %.0f ms)",
      static_cast<double>(worst.wrong), static_cast<double>(worst.max_gap), static_cast<double>(worst5.wrong),
      static_cast<double>(worst5.max_gap));
  TEST_ASSERT_EQUAL_INT(0, worst.wrong + worst5.wrong);
  TEST_ASSERT_TRUE(worst.finite && worst5.finite);
  TEST_ASSERT_FALSE_MESSAGE(worst.dropped || worst5.dropped, "has_pulse putosi terveeltä kanavalta");
  TEST_ASSERT_LESS_OR_EQUAL(450 + 150, static_cast<int>(worst.max_gap));
  TEST_ASSERT_LESS_OR_EQUAL(750 + 150, static_cast<int>(worst5.max_gap));
}

void test_single_witness_needs_rhythm() {
  // Yksi todistaja (F2): toinen kanava kuollut (letku irti) tai vaiennut
  // (heikko sylinteri, ohitettu pulssi). nT-välissä todistajan paikat
  // (k + 1/3) / n ja (k + 2/3) / n osuvat 1/3:n tai 2/3:n lähelle (4T
  // täsmälleen), joten yksi paikallaan oleva merkki ei riitä. Kierrosnopeuden
  // muutos siirtää todistajan paikkaa ennustettavasti: hidastuessa kampi
  // kulkee syklin alun nopeammin, joten paikka aikaistuu (1 - jakso /
  // edellinen) / 9. Yhden todistajan syklissä paikan on osuttava tähän
  // korjattuun kohtaan 0,08:n sisään. Toistot (5a45dc7 kirjasi 2T-5T:n):
  //  - letku irti, pikablip 850 -> 3000, xtalk 0,5: 5T-6T (262-338 ms);
  //  - heikko sylinteri 0,5 x, pikablip 850 -> 2000: 2T kanavalla 1, todistaja
  //    u = 0,234 (kiihdytys siirsi 1/6:n 1/3:n toleranssiin);
  //  - jitter 3 %, pikablip 850 -> 2500 (syvyys 20), xtalk 0,3: sama 2T.
  // Tarkistus ei koske sykliä, joka on pitkä vain tauon vuoksi (jakso
  // tavallinen, edellinen hylätty): siinä nopeuden muutos edellisestä omasta
  // välistä ei kerro todistajan paikkaa, ja xtalk 0,5:n tuplablipissä (letku
  // irti) has_pulse putosi (viimeinen tapaus: 1,3 s:n väli).
  struct Case {
    float rpm1, xt, jitter, d1;
    uint32_t seed;
    long t0;
    int reps, hose_ch, weak_ch;
    float idle;
    long gap;
  };
  const Case cases[] = {
      {3000.0f, 0.5f, 0.01f, 12.0f, 8, 2583, 1, 2, -1, 850.0f, 0},
      {3000.0f, 0.5f, 0.01f, 12.0f, 8, 2630, 1, 0, -1, 850.0f, 0},
      {3000.0f, 0.5f, 0.01f, 12.0f, 5, 2558, 1, 1, -1, 850.0f, 0},
      {2000.0f, 0.0f, 0.01f, 12.0f, 1, 2537, 1, -1, 2, 850.0f, 0},
      {2000.0f, 0.3f, 0.01f, 12.0f, 1, 2537, 2, -1, 2, 850.0f, 0},
      {2500.0f, 0.3f, 0.03f, 20.0f, 6, 2561, 1, -1, -1, 850.0f, 0},
      {2500.0f, 0.3f, 0.03f, 20.0f, 6, 2561, 2, -1, -1, 850.0f, 0},
      {3000.0f, 0.5f, 0.01f, 12.0f, 1, 2512, 2, 2, -1, 950.0f, 100},
  };
  SimRun worst;
  for (const Case& k : cases) {
    sbx::SimParams p;
    p.xtalk = k.xt;
    p.rpm_jitter = k.jitter;
    p.seed = k.seed;
    SimOpts o;
    o.t0 = k.t0;
    p.rpm = k.idle;
    o.t_end = o.t0 + (450 + k.gap) * k.reps - k.gap;
    o.total = o.t_end + 1500;
    if (k.hose_ch >= 0) o.hose_mask = 1 << k.hose_ch;
    if (k.weak_ch >= 0) {
      o.weak_ch = k.weak_ch;
      o.weak_scale = 0.5f;
    }
    const long hold = k.gap > 0 ? 1 : 0;  // tarkistajan tuplablippi: 150/1/299, tauko 100
    worstOf(worst, runSim(p, o, Blips{k.rpm1, o.t0, 150, hold, 300 - hold, k.reps, k.idle, k.d1, k.gap}));
  }
  msg("yksi todistaja pikablipeissä (letku irti, heikko sylinteri, jitter 3 %%): väärät %.0f, terveiden väli %.0f ms",
      static_cast<double>(worst.wrong), static_cast<double>(worst.max_gap));
  TEST_ASSERT_EQUAL_INT(0, worst.wrong);
  TEST_ASSERT_TRUE(worst.finite);
  TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi terveeltä kanavalta");
}

void test_hose_swap_relearns_firing_order() {
  // Sytytysjärjestys ja letkujen kytkentä opitaan äänestämällä (order_,
  // kyllästys kOrderMax). Letkut 2 ja 3 vaihdetaan (F3):
  //  - käynnin aikana: väärä järjestys hylkää syklit, kunnes äänet kääntyvät
  //    (~1,5 sykliä); ilman kyllästystä kääntyminen kesti sekunteja ja
  //    has_pulse putosi (850 rpm: 3 s:n väli);
  //  - pysähdyksen aikana: kun yhdelläkään kanavalla ei ole has_pulsea,
  //    opittu järjestys unohdetaan, joten uusi käynti lukittuu D:n rajassa
  //    (ilman nollausta 850 rpm: 613 ms, 600 rpm: 856 ms).
  //    Vaihdon ylittävää sykliä ei tarkisteta (puolet kummastakin kytkennästä).
  SimRun run, restart;
  for (int i = 0; i < 8; ++i) {
    const float rpm = i < 4 ? 850.0f : 600.0f;
    const long T = static_cast<long>(periodMs(rpm));
    sbx::SimParams p;
    p.rpm = rpm;
    p.xtalk = (i & 1) ? 0.5f : 0.0f;
    p.seed = static_cast<uint32_t>(1 + i);
    SimOpts o;
    o.t0 = 3000 + T * (i % 4) / 4;
    o.t_end = o.t0;
    o.total = o.t0 + 2500;
    o.swap_t = o.t0;
    const SimRun a = runSim(p, o, [](long, sbx::SimParams&) { return false; });
    worstOf(run, a);
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(4.0f * periodMs(rpm)), static_cast<int>(a.max_gap));
    SimOpts q;
    q.off_t1 = 2400 + T * (i % 4) / 4;
    q.off_t2 = q.off_t1 + 1300;
    q.swap_t = q.off_t1 + 600;
    q.watch_from = q.off_t1 + 200;
    q.t0 = q.t_end = q.off_t2;
    q.total = q.off_t2 + 2000;
    const SimRun r = runSim(p, q, [](long, sbx::SimParams&) { return false; });
    worstOf(restart, r);
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(3.5f * periodMs(rpm)) + 40, static_cast<int>(r.lock_ms));
  }
  msg("letkujen vaihto käynnissä: väärät %.0f, pisin väli %.0f ms; pysähdyksessä: väärät %.0f, lukitus %.0f ms",
      static_cast<double>(run.wrong), static_cast<double>(run.max_gap), static_cast<double>(restart.wrong),
      static_cast<double>(restart.lock_ms));
  TEST_ASSERT_EQUAL_INT(0, run.wrong);
  TEST_ASSERT_EQUAL_INT(0, restart.wrong);
  TEST_ASSERT_FALSE_MESSAGE(run.dropped, "has_pulse putosi letkujen vaihdossa");
  TEST_ASSERT_GREATER_OR_EQUAL(0, restart.lock_ms);
}

void test_short_fault_does_not_bias_channel_mean() {
  // Lyhyt anturivika (mV alueen ulkopuolella) tai letku irti 40 ms kahden
  // oman pulssin välissä: kanava asettuu uudelleen kuten käynnistyksessä,
  // myös D1-integraalin ketju katkeaa. Muuten vian ylittävä sykli (ei
  // ohitettua pulssia, todistajat paikoillaan) hyväksyttiin, ja sen
  // integraaliin tuli 40 ms -108 kPa:ta tai ilmanpainetta: kanavan näyttö oli
  // 8 syklin ajan ~2 kPa pielessä (status: väärä synkronointituomio).
  float worst = 0.0f;
  for (int kind = 0; kind < 2; ++kind) {
    for (int ph = 0; ph < 6; ++ph) {
      SynthParams p = idleParams();
      g_rig.begin(p);
      g_rig.run(2.5f);
      const long t1 = 2500 + static_cast<long>(periodMs(850.0f) * static_cast<float>(ph) / 6.0f);
      const float truth = p.base_kpa + p.sync_err_kpa[1];
      for (long t = 2500; t < 5500; ++t) {
        const bool f = t >= t1 && t < t1 + 40;
        if (kind == 1 && (t == t1 || t == t1 + 40)) {
          p.hose_off[1] = f;
          g_rig.syn.setParams(p);
        }
        g_rig.syn.next(g_rig.kpa, g_rig.raw);
        if (kind == 0 && f) {
          g_rig.raw.mv[1] = 0;
          g_rig.kpa[1] = -p.v_zero_mv / p.mv_per_kpa;
        }
        g_rig.dsp.push(g_rig.kpa, g_rig.raw);
        const ChannelState& c = g_rig.dsp.channel(1);
        if (t > t1 && c.has_pulse && std::fabs(c.mean_kpa - truth) > worst) worst = std::fabs(c.mean_kpa - truth);
      }
    }
  }
  msg("anturivika tai letku irti 40 ms: vikaantuneen kanavan näytön suurin virhe %.2f kPa", static_cast<double>(worst));
  TEST_ASSERT_LESS_THAN_FLOAT(0.3f, worst);
}

void test_hose_pulled_at_vacuum_step() {
  // Letku irti 400 ms alipaineen askeleen (13 kPa ylös, 600 rpm) kohdalla:
  // askel maksaa pulssin, ja takaisin kytketyn letkun 55 kPa:n lasku osui
  // seuraavan oman pulssin ikkunaan ja esti sen. Kaksi peräkkäistä menetettyä
  // pulssia pudotti terveen kanavan has_pulsen (1,4 s:n väli). Irrotettu
  // kanava on heti kuollut (ilmanpaineessa) ja asettuu takaisin kytkettäessä,
  // joten väli on askeleen oma (<= 4,1 jaksoa).
  // 850 rpm, -55 -> -47 kPa, letku 3 irti 150 ms askeleesta + 120 ms
  // (tarkistajan toisto): 5a45dc7 piti irrotettua kanavaa elävänä, koska
  // terveen kanavan oma historia oli askeleen jäljiltä vanha, ja sen
  // takaisinkytkennän lasku esti seuraavan oman pulssin (5 jaksoa, 707 ms).
  {
    SimRun w850;
    for (int run = 0; run < 4; ++run) {
      sbx::SimParams p;
      p.seed = static_cast<uint32_t>(2 + run);
      SimOpts o;
      o.t0 = run == 0 ? 2540 : 2500 + 37 * run;
      o.t_end = o.t0;
      o.total = o.t0 + 2000;
      o.step_t = static_cast<double>(o.t0);
      o.hose_mask = 4;
      o.hose_t = o.t0 + 120;
      o.hose_t2 = o.hose_t + 150;
      worstOf(w850, runSim(p, o, Ramp{850.0f, 850.0f, -55.0f, -47.0f, 20.0f, 20.0f, o.t0, 0}));
    }
    msg("letku irti 150 ms askeleessa (850 rpm): väärät %.0f, muiden pisin väli %.0f ms", static_cast<double>(w850.wrong),
        static_cast<double>(w850.max_gap));
    TEST_ASSERT_EQUAL_INT(0, w850.wrong);
    TEST_ASSERT_FALSE(w850.dropped);
    TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(4.1f * periodMs(850.0f)), static_cast<int>(w850.max_gap));
  }
  SimRun worst;
  for (int run = 0; run < 6; ++run) {
    sbx::SimParams p;
    p.rpm = 600.0f;
    p.seed = static_cast<uint32_t>(run < 2 ? 2 + 2 * run : 1 + run % 3);  // 2 ja 4: toistetapaukset
    SimOpts o;
    o.t0 = 2500 + static_cast<long>(periodMs(600.0f) * 3.3f / 6.0f) + (run < 2 ? 0 : 13 * run);
    o.t_end = o.t0;
    o.total = o.t0 + 2500;
    o.step_t = static_cast<double>(o.t0);
    o.hose_mask = 1 << (static_cast<int>(p.seed) % 3);
    o.hose_t = o.t0 + 10;
    o.hose_t2 = o.hose_t + 400;
    worstOf(worst, runSim(p, o, Ramp{600.0f, 600.0f, -55.0f, -42.0f, 20.0f, 20.0f, o.t0, 0}));
  }
  msg("letku irti 400 ms askeleessa (600 rpm): väärät %.0f, muiden pisin väli %.0f ms", static_cast<double>(worst.wrong),
      static_cast<double>(worst.max_gap));
  TEST_ASSERT_EQUAL_INT(0, worst.wrong);
  TEST_ASSERT_TRUE(worst.finite);
  TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi elävältä kanavalta");
  TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(4.1f * periodMs(600.0f)), static_cast<int>(worst.max_gap));
}

void test_weak_cylinder_quick_blips() {
  // Heikko sylinteri (0,5 x, epätahdissa oleva kaasutin) ja kaksi peräkkäistä
  // pikablippiä (150 ms ylös, 300 ms alas): heikon kanavan pulssit jäävät usein
  // tunnistamatta, joten muiden syklejä vahvistaa usein vain yksi todistaja.
  // Kun kaikki ohittavat pulsseja, vanha viite saa 2T-välin näyttämään
  // tavalliselta; yksi todistaja ei riitä, kun edellisestä hyväksytystä on
  // pitkä tauko. Heikon kanavan has_pulse saa pudota, muiden ei.
  // Toistetapaukset: (siemen, alku ms, heikko kanava); 5673ef7 kirjasi 2T:n
  // (2, 2570, 0) ja (3, 2517, 1), ilman tauko-ehtoa (1, 2622, 1).
  const long rep[3][3] = {{2, 2570, 0}, {3, 2517, 1}, {1, 2622, 1}};
  const float pk[] = {3000.0f, 2500.0f};
  for (float rpm1 : pk) {
    SimRun worst;
    for (int run = 0; run < 9; ++run) {
      const bool r = run < 3;
      sbx::SimParams p;
      p.xtalk = 0.3f;
      p.seed = static_cast<uint32_t>(r ? rep[run][0] : 1 + run % 4);
      SimOpts o;
      o.t0 = r ? rep[run][1] : 2500 + static_cast<long>(periodMs(850.0f) * static_cast<float>(run - 3) / 6.0f);
      o.t_end = o.t0 + 2 * 450;
      o.total = o.t_end + 2000;
      o.weak_ch = r ? static_cast<int>(rep[run][2]) : run % 3;
      o.weak_scale = 0.5f;
      worstOf(worst, runSim(p, o, Blips{rpm1, o.t0, 150, 0, 300, 2}));
    }
    msg("heikko sylinteri 0,5 x, kaksi pikablippiä 850 -> %.0f rpm, xtalk 0,3: väärät %.0f, muiden väli %.0f ms",
        static_cast<double>(rpm1), static_cast<double>(worst.wrong), static_cast<double>(worst.max_gap));
    TEST_ASSERT_EQUAL_INT(0, worst.wrong);
    TEST_ASSERT_TRUE(worst.finite);
    TEST_ASSERT_FALSE_MESSAGE(worst.dropped, "has_pulse putosi terveeltä kanavalta");
  }
}

void test_display_right_after_rpm_jump() {
  // Näyttö (avg_cycles-keskiarvo) alkaa alusta, kun jakso hyppää yli 1,5 x
  // tai alle kMinPeriodRatio x ikkunan keskijakson, ja hypännyt sykli
  // (puoliksi vanhaa käyntiä) jätetään pois. Mitattu:
  //  - ylös 850 -> 6000 / 600 -> 2000: ilman alarajaa näytössä on 0,5 s
  //    jälkeen yhä vanhoja syklejä (virhe 43-47 %), alarajan kanssa < 1 %;
  //  - alas 6000/3000 -> 850: hypännyt sykli ensimmäisenä uutena
  //    näyttöarvona heittää 13-16 %, ilman sitä ensimmäinen arvo < 2,5 %;
  //  - tyhjäksi jäänyt ikkuna ei saa julkaista 0 / 0 = NaN:ia: kaikki
  //    luvut äärellisiä joka näytteellä.
  struct Case {
    float r0, r1;
  };
  const Case ups[] = {{850.0f, 6000.0f}, {600.0f, 2000.0f}};
  const Case downs[] = {{6000.0f, 850.0f}, {3000.0f, 850.0f}};
  for (int dir = 0; dir < 2; ++dir) {
    for (int ci = 0; ci < 2; ++ci) {
      const Case& k = dir ? downs[ci] : ups[ci];
      SimRun worst;
      for (int run = 0; run < 4; ++run) {
        sbx::SimParams p;
        p.rpm = k.r0;
        p.xtalk = (run & 1) ? 0.3f : 0.0f;
        p.seed = static_cast<uint32_t>(1 + run);
        SimOpts o = phased(k.r0, run, 4, 0, 1500);
        o.est_at = o.t0 + 500;
        o.first_disp = dir == 1;
        worstOf(worst, runSim(p, o, Ramp{k.r0, k.r1, -55.0f, -55.0f, 20.0f, 20.0f, o.t0, 0}));
      }
      msg("rpm-askel %.0f -> %.0f: RpmEstimate 0,5 s jälkeen %.2f %%, ensimmäinen uusi kanava-arvo %.2f %%",
          static_cast<double>(k.r0), static_cast<double>(k.r1), static_cast<double>(worst.est_at_err * 100.0f),
          static_cast<double>(worst.first_disp_err * 100.0f));
      TEST_ASSERT_TRUE(worst.finite);
      TEST_ASSERT_EQUAL_INT(0, worst.wrong);
      TEST_ASSERT_LESS_THAN_FLOAT(0.03f, worst.est_at_err);
      if (dir == 1) TEST_ASSERT_LESS_THAN_FLOAT(0.05f, worst.first_disp_err);
    }
  }
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

void test_p2p_not_inflated_by_vacuum_step() {
  // p2p_kpa (docs §4.2) = pulssin nousu edellisen oman pulssin pohjasta,
  // lukittuna min(nousu, lasku): alipaineen askel kasvattaa syklissä vain
  // toista (askel ylös nousua, alas laskua), joten minimi ei paisu. Pelkkä
  // lasku näytti 10 kPa:n askeleen alaspäin +11 kPa:na, pelkkä nousu saman
  // ylöspäin. (Minimi voi askeleessa pienentyä: diagnostiikka, ei D1.)
  for (float b1 : {-45.0f, -65.0f}) {
    float steady = 0.0f, hi = 0.0f;
    for (int ph = 0; ph < 12; ++ph) {
      SynthParams p = idleParams();
      p.res_kpa = 0.0f;
      p.noise_kpa = 0.05f;
      DspParams dp;
      dp.avg_cycles = 1;
      g_rig.begin(p, dp);
      g_rig.run(2.0f + 0.012f * static_cast<float>(ph));
      steady = g_rig.dsp.channel(0).p2p_kpa;
      p.base_kpa = b1;
      g_rig.syn.setParams(p);
      for (int i = 0; i < 1500; ++i) {
        g_rig.step();
        for (int c = 0; c < kChannels; ++c) {
          if (g_rig.dsp.channel(c).p2p_kpa > hi) hi = g_rig.dsp.channel(c).p2p_kpa;
        }
      }
    }
    msg("askel -55 -> %.0f kPa: p2p tasaisesti %.2f, askeleen jälkeen enintään %.2f kPa", static_cast<double>(b1),
        static_cast<double>(steady), static_cast<double>(hi));
    TEST_ASSERT_LESS_THAN_FLOAT(1.1f * steady, hi);
  }
  // EngineSim: 1,5 s kaasun katkaisun (3000 -> 850 rpm, -15 -> -55 kPa, 300
  // ms), rpm-askeleen ja alipaineen askeleen jälkeen p2p on tasaisen käynnin
  // arvossa +-2 kPa molempiin suuntiin. Nousu mitataan edellisen oman pulssin
  // pohjasta; jos pohjaa ei päivitettäisi, p2p jäisi vanhan käynnin pohjaan
  // (katkaisun jälkeen -32 kPa, rpm-askeleen jälkeen 10 kPa).
  SimOpts so;
  so.t0 = so.t_end = 2500;
  so.total = 3500;
  so.p2p_at = 3400;
  const SimRun st = runSim(sbx::SimParams(), so, [](long, sbx::SimParams&) { return false; });
  const float ref = 0.5f * (st.p2p_lo + st.p2p_hi);
  SimRun after;
  for (int run = 0; run < 6; ++run) {
    sbx::SimParams p;
    p.seed = static_cast<uint32_t>(1 + run);
    p.xtalk = (run & 1) ? 0.3f : 0.0f;
    const int kind = run / 2;
    p.rpm = kind == 2 ? 850.0f : 3000.0f;
    p.base_kpa = kind == 0 ? -15.0f : (kind == 1 ? -55.0f : -30.0f);
    p.depth_kpa = kind == 0 ? 10.0f : 20.0f;
    SimOpts o;
    o.t0 = 2500 + 7 * run;
    o.t_end = o.t0 + (kind == 0 ? 300 : 0);
    o.total = o.t_end + 1600;
    o.p2p_at = o.t_end + 1500;
    worstOf(after, runSim(p, o, Ramp{p.rpm, 850.0f, p.base_kpa, -55.0f, p.depth_kpa, 20.0f, o.t0, o.t_end - o.t0}));
  }
  msg("p2p 1,5 s katkaisun ja askelten jälkeen %.2f...%.2f kPa, tasaisesti %.2f kPa", static_cast<double>(after.p2p_lo),
      static_cast<double>(after.p2p_hi), static_cast<double>(ref));
  TEST_ASSERT_FLOAT_WITHIN(2.0f, ref, after.p2p_lo);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, ref, after.p2p_hi);
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
  // Kaksi letkua irti: varatilassa (ei muita eläviä kanavia) jokainen
  // laukaisu on ehdokas, joten laukaisutaso ratkaisee, tuleeko vuodosta raja.
  // Kolmella kanavalla omistajuus hylkää vuotopulssin laukaisun tasosta
  // riippumatta (ks. test_crosstalk_does_not_triple_rpm).
  p.hose_off[1] = true;
  p.hose_off[2] = true;
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
  //  - hyst_min pieni (0,2 < hyst_frac x A) -> h = hyst_frac x A: 0,3:lla
  //    raja x > 0,65 (0,6:n vuoto ei laukaise), 0,1:llä x > 0,55 (laukaisee).
  //    (0,05:llä kohina laukaisisi tasaisillakin kanavilla, ja varatilan
  //    kolmikko lukittuisi vasta sattumalta.)
  const float floor_on = cycleRatioWithLeak(0.57f, 0.0f, 1.0f);
  const float floor_off = cycleRatioWithLeak(0.57f, 0.0f, 0.05f);
  const float frac_hi = cycleRatioWithLeak(0.6f, 0.3f, 0.2f);
  const float frac_lo = cycleRatioWithLeak(0.6f, 0.1f, 0.2f);
  msg("syklit / moottorin syklit: hyst_min 1,0 %.2f vs 0,05 %.2f; hyst_frac 0,3 %.2f vs 0,1 %.2f",
      static_cast<double>(floor_on), static_cast<double>(floor_off), static_cast<double>(frac_hi),
      static_cast<double>(frac_lo));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, floor_on);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, frac_hi);
  // Varatilassa vuotopulssin laukaisemat rajat rikkovat syklit: niitä ei
  // hyväksytä oikeina (kolminkertainen rpm), mutta oikeitakaan ei enää saada.
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
  RUN_TEST(test_rpm_drop_every_channel_recovers);
  RUN_TEST(test_throttle_blip_engine_sim);
  RUN_TEST(test_throttle_blip_tracks_rpm_and_vacuum);
  RUN_TEST(test_vacuum_steps_with_crosstalk_engine_sim);
  RUN_TEST(test_fast_vacuum_ramps_with_crosstalk);
  RUN_TEST(test_ownership_fallback_hoses_off);
  RUN_TEST(test_engine_start_and_restart_never_lock_on_leaks);
  RUN_TEST(test_dead_channel_during_blip);
  RUN_TEST(test_reconnected_hose_is_not_a_witness);
  RUN_TEST(test_short_fault_does_not_bias_channel_mean);
  RUN_TEST(test_hose_pulled_at_vacuum_step);
  RUN_TEST(test_single_witness_needs_rhythm);
  RUN_TEST(test_hose_swap_relearns_firing_order);
  RUN_TEST(test_weak_cylinder_quick_blips);
  RUN_TEST(test_display_right_after_rpm_jump);
  RUN_TEST(test_pulse_timeout_when_signal_goes_flat);
  RUN_TEST(test_noise_and_resonance_alone_never_pulse);
  RUN_TEST(test_level_ema_tracks_step_with_tau);
  RUN_TEST(test_sensor_fault_after_hold_and_clears);
  RUN_TEST(test_saturation_flag_lasts_one_second);
  RUN_TEST(test_configure_resets_state);
  RUN_TEST(test_avg_cycles_sets_response_and_noise);
  RUN_TEST(test_filter_cutoff_changes_min_p2p_but_not_mean);
  RUN_TEST(test_p2p_not_inflated_by_vacuum_step);
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
