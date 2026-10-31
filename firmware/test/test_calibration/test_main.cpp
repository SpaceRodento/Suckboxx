// Kalibrointi (docs/firmware.md §4.3, R4, R6, docs/rauta.md §1.2 ja §7).
//
// Totuusarvot tulevat datasheetin siirtofunktiosta (V = 2700 + 25·P mV) ja
// fysikaalisesta anturimallista: jokaisella anturilla on oma offset (±2 kPa,
// R4) ja herkkyysvirhe (muutama %). Kaksipistekalibroinnin jälkeen kolmen
// lineaarisen anturin on luettava samaa KAIKILLA paineilla - se on koko
// tuotteen ydinlupaus (roadmap: penkkitesti spread < 0,3 kPa).

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "core/calibration.h"

using sbx::Calibration;
using sbx::CalCapture;
using sbx::CalResult;
using sbx::kChannels;
using sbx::SensorParams;

void setUp() {}
void tearDown() {}

static void assertCalEqual(const Calibration& a, const Calibration& b) {
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(a.offset_kpa[i] == b.offset_kpa[i], "offset muuttui");
    TEST_ASSERT_TRUE_MESSAGE(a.gain[i] == b.gain[i], "gain muuttui");
  }
  TEST_ASSERT_EQUAL_INT(a.zero_valid, b.zero_valid);
  TEST_ASSERT_EQUAL_INT(a.gain_valid, b.gain_valid);
}

static int asInt(CalResult r) { return static_cast<int>(r); }

// --- siirtofunktio -----------------------------------------------------------

void test_transfer_function_matches_datasheet_table() {
  // docs/rauta.md §1.2, XGZP6847A100KPGN33 @ 3,3 V
  const SensorParams sp;
  struct Row { uint16_t mv; float kpa; };
  const Row table[] = {
      {200, -100.0f},   // anturin alaraja
      {825, -75.0f},    // joutokäynti, syvä pulssi
      {1325, -55.0f},   // joutokäynti, keskiarvo
      {2500, -8.0f},    // täysi kaasu
      {2700, 0.0f},     // moottori seis / letku irti
  };
  for (const Row& r : table) {
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, r.kpa, sbx::nominalKpa(r.mv, 0, sp));
  }
  // 1 mV = 0,04 kPa: ADC:n resoluutio kPa:na.
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.04f, sbx::nominalKpa(1326, 0, sp) - sbx::nominalKpa(1325, 0, sp));
  // Leikkautunut GN-anturi (2700 mV) ja ADC:n ääripäät antavat äärellisen luvun.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 24.0f, sbx::nominalKpa(3300, 0, sp));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -108.0f, sbx::nominalKpa(0, 0, sp));
}

void test_transfer_function_other_sensor_variants() {
  // 5 V -anturi 20k/30k-jakajalla: 2700 mV, 24 mV/kPa (rauta.md §1.6).
  SensorParams sp5;
  sp5.mv_per_kpa = 24.0f;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -55.0f, sbx::nominalKpa(1380, 0, sp5));
  // GPN (−100…+100 kPa): nolla 1450 mV, 12,5 mV/kPa (rauta.md §1.3).
  SensorParams gpn;
  gpn.v_zero_mv = 1450.0f;
  gpn.mv_per_kpa = 12.5f;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -54.0f, sbx::nominalKpa(775, 0, gpn));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, sbx::nominalKpa(1512, 0, gpn) + 0.04f);  // +5 kPa paluuaalto
}

// R6: ratiometrinen anturi. Kiskon notkahdus skaalaa ulostulon; korjaus
// palauttaa sen nimelliskiskolle.
static uint16_t ratiometricMv(float kpa, float rail_mv, const SensorParams& sp) {
  const float mv_nominal = sp.v_zero_mv + sp.mv_per_kpa * kpa;
  return static_cast<uint16_t>(std::lround(mv_nominal * rail_mv / sp.rail_nominal_mv));
}

void test_rail_compensation_r6() {
  SensorParams sp;
  sp.rail_comp = true;
  const float truths[] = {-55.0f, -75.0f, 0.0f};
  const float rails[] = {3250.0f, 3350.0f};  // Wi-Fi-piikki ±50 mV
  for (float p : truths) {
    for (float rail : rails) {
      const uint16_t mv = ratiometricMv(p, rail, sp);
      const uint16_t rail_mv = static_cast<uint16_t>(rail);
      const float fixed = sbx::nominalKpa(mv, rail_mv, sp);
      // Jäljelle jää vain 1 mV:n kvantisointi (0,04 kPa).
      TEST_ASSERT_FLOAT_WITHIN(0.05f, p, fixed);
      SensorParams off = sp;
      off.rail_comp = false;
      const float raw = sbx::nominalKpa(mv, rail_mv, off);
      if (p == -55.0f) {
        // Ilman korjausta virhe on R6:n kokoluokkaa (−55 kPa: 1325·50/3300 mV ≈ 0,8 kPa).
        TEST_ASSERT_GREATER_THAN_FLOAT(0.7f, std::fabs(raw - p));
      }
      if (p == 0.0f) {
        // 0 kPa:ssa virhe on suurin: 2700·50/3300 mV = 41 mV ≈ 1,6 kPa.
        TEST_ASSERT_GREATER_THAN_FLOAT(1.5f, std::fabs(raw - p));
      }
    }
  }
  // rail_mv = 0 (ei mitattu) tai rail_comp pois -> ei korjausta.
  const uint16_t mv = ratiometricMv(-55.0f, 3250.0f, sp);
  SensorParams off = sp;
  off.rail_comp = false;
  TEST_ASSERT_EQUAL_FLOAT(sbx::nominalKpa(mv, 0, off), sbx::nominalKpa(mv, 0, sp));
  TEST_ASSERT_EQUAL_FLOAT(sbx::nominalKpa(mv, 0, off), sbx::nominalKpa(mv, 3250, off));
}

void test_rail_compensation_ignores_broken_monitor() {
  // Kiskomonitorin jakaja irti / oikosulussa: lukema ei ole kiskon jännite.
  SensorParams sp;
  sp.rail_comp = true;
  SensorParams off = sp;
  off.rail_comp = false;
  const uint16_t mv = 1325;
  const uint16_t broken[] = {1, 1000, 2639, 3961, 4095, 65535};
  for (uint16_t r : broken) {
    TEST_ASSERT_EQUAL_FLOAT(sbx::nominalKpa(mv, 0, off), sbx::nominalKpa(mv, r, sp));
  }
  // ±20 %:n rajalla korjaus vielä tehdään.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, (1325.0f * 3300.0f / 2640.0f - 2700.0f) / 25.0f, sbx::nominalKpa(mv, 2640, sp));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, (1325.0f * 3300.0f / 3960.0f - 2700.0f) / 25.0f, sbx::nominalKpa(mv, 3960, sp));
}

void test_apply_cal() {
  Calibration cal;
  TEST_ASSERT_EQUAL_FLOAT(-55.0f, sbx::applyCal(-55.0f, 0, cal));  // oletus = identiteetti
  cal.gain[1] = 1.1f;
  cal.offset_kpa[1] = -2.0f;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -57.0f, sbx::applyCal(-50.0f, 1, cal));
  TEST_ASSERT_EQUAL_FLOAT(-50.0f, sbx::applyCal(-50.0f, 0, cal));  // muut kanavat ennallaan
  TEST_ASSERT_EQUAL_FLOAT(-50.0f, sbx::applyCal(-50.0f, -1, cal));
  TEST_ASSERT_EQUAL_FLOAT(-50.0f, sbx::applyCal(-50.0f, kChannels, cal));
}

// --- nollapiste ----------------------------------------------------------------

void test_zero_ok_preserves_gain() {
  Calibration cal;
  cal.gain[0] = 1.05f;
  cal.gain[1] = 0.95f;
  cal.gain[2] = 1.0f;
  cal.gain_valid = true;
  const float level[kChannels] = {0.8f, -1.2f, 1.9f};  // anturien ±2 kPa offsetit (R4)
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeZero(level, 5.0f, cal)));
  TEST_ASSERT_TRUE(cal.zero_valid);
  TEST_ASSERT_TRUE(cal.gain_valid);
  TEST_ASSERT_EQUAL_FLOAT(1.05f, cal.gain[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.95f, cal.gain[1]);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, cal.gain[2]);
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, sbx::applyCal(level[i], i, cal));  // ilmanpaine -> 0
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -cal.gain[i] * level[i], cal.offset_kpa[i]);
  }
}

void test_zero_boundary_and_not_atmosphere() {
  {
    Calibration cal;
    const float level[kChannels] = {5.0f, -5.0f, 0.0f};  // tasan rajalla kelpaa
    TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeZero(level, 5.0f, cal)));
  }
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float bad[][kChannels] = {
      {0.1f, -0.2f, -55.0f},  // yksi kanava kiinni käyvässä moottorissa
      {-6.0f, 0.0f, 0.0f},    // pumpussa vielä alipainetta
      {0.0f, 5.01f, 0.0f},    // positiivinen paine
      {0.0f, 0.0f, nan},      // rikkinäinen lukema
  };
  for (const auto& lv : bad) {
    Calibration cal;
    cal.offset_kpa[0] = 0.3f;
    cal.gain[2] = 1.02f;
    cal.gain_valid = true;
    const Calibration before = cal;
    TEST_ASSERT_EQUAL_INT(asInt(CalResult::NotAtmosphere), asInt(sbx::computeZero(lv, 5.0f, cal)));
    assertCalEqual(before, cal);  // hylätty ei koske kalibrointiin
    TEST_ASSERT_FALSE(cal.zero_valid);
  }
}

// --- nollauksen vakausportti (WP-9, jatko.md 8) ------------------------------------

void test_zero_settle_gate_order_and_edges() {
  const float inf = std::numeric_limits<float>::infinity();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float level[kChannels] = {0.8f, -1.2f, 1.9f};
  {
    Calibration cal;
    const float range[kChannels] = {0.0f, 0.099f, 0.05f};   // raja on tiukka <
    TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeZero(level, range, 5.0f, 0.1f, cal)));
    TEST_ASSERT_TRUE(cal.zero_valid);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.8f, cal.offset_kpa[0]);
  }
  const float bad_range[][kChannels] = {
      {0.0f, 0.1f, 0.0f},   // tasan rajalla ei kelpaa (raja on "alle")
      {0.0f, 0.0f, 0.3f},   // yksikin kanava liikkeessä
      {inf, 0.0f, 0.0f},    // historiaa ei koko ikkunaa (LevelSettle::range)
      {0.0f, nan, 0.0f},    // rikkinäinen taso
  };
  for (const auto& rg : bad_range) {
    Calibration cal;
    cal.offset_kpa[1] = 0.4f;
    const Calibration before = cal;
    TEST_ASSERT_EQUAL_INT(asInt(CalResult::NotSettled), asInt(sbx::computeZero(level, rg, 5.0f, 0.1f, cal)));
    assertCalEqual(before, cal);  // hylätty ei koske kalibrointiin
  }
  // Ilmanpaine-ehto tarkistetaan ensin: alipaineessa teksti kertoo sen eikä "odota".
  Calibration cal;
  const float vac[kChannels] = {0.0f, -6.0f, 0.0f};
  const float moving[kChannels] = {0.0f, 2.0f, 0.0f};
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::NotAtmosphere), asInt(sbx::computeZero(vac, moving, 5.0f, 0.1f, cal)));
}

// LevelSettle: vaihteluväli liukuvassa 1 s:n ikkunassa, 10-50 Hz ja epätasainen dt.
void test_level_settle_window() {
  const float W = sbx::kZeroSettleWindowS;
  sbx::LevelSettle s;
  const float inf = std::numeric_limits<float>::infinity();
  TEST_ASSERT_FALSE(s.full());
  TEST_ASSERT_TRUE(s.range(0) == inf);
  // 20 Hz, kanava 1 nousee 0,05 kPa/tick (1 kPa/s), muut paikallaan.
  for (int k = 0; k <= 30; ++k) {
    const float t = 0.05f * static_cast<float>(k);
    const float lv[kChannels] = {0.0f, 0.05f * static_cast<float>(k), -0.2f};
    s.add(lv, t);
    TEST_ASSERT_EQUAL_INT(k >= 20, s.full());   // historiaa >= 1 s
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, s.range(0));
  TEST_ASSERT_FLOAT_WITHIN(0.06f, 1.0f, s.range(1));   // 1 kPa/s x 1 s (lokeroresoluutio)
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, s.range(2));
  // Liike loppuu: ikkunan jälkeen vaihteluväli 0.
  const float stop[kChannels] = {0.0f, 1.5f, -0.2f};
  for (int k = 31; k <= 51; ++k) s.add(stop, 0.05f * static_cast<float>(k));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, s.range(1));
  TEST_ASSERT_TRUE(s.range(-1) == inf);
  TEST_ASSERT_TRUE(s.range(kChannels) == inf);
  // 50 Hz: lokeroita harvennetaan (alkujen väli >= ikkuna / kSlots), mutta
  // jokainen näyte on mukana: lokeroon yhdistetty yksittäinen piikki näkyy.
  s.reset();
  TEST_ASSERT_FALSE(s.full());
  for (int k = 0; k < 75; ++k) {
    const float spike = (k == 61) ? 0.3f : 0.0f;   // pariton k = ei lokeron alku
    const float lv[kChannels] = {spike, 0.0f, 0.0f};
    s.add(lv, 0.02f * static_cast<float>(k));
  }
  TEST_ASSERT_TRUE(s.full());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.3f, s.range(0));
  // 10 Hz ja epätasainen dt: kapasiteetti riittää (ikkuna aina historiassa).
  s.reset();
  uint32_t lcg = 5u;
  float t = 0.0f;
  for (int k = 0; k < 400; ++k) {
    lcg = lcg * 1664525u + 1013904223u;
    const float dt = 0.02f + 0.08f * static_cast<float>(lcg >> 8) / 16777216.0f;
    const float lv[kChannels] = {0.0f, 0.0f, 0.0f};
    s.add(lv, t);
    if (t >= W + 0.1f) TEST_ASSERT_TRUE(s.full());
    t += dt;
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.range(1));
}

// --- vahvistus: fysikaalinen anturimalli -----------------------------------------

// Anturi i: nimellinen = (1 + e_i)·P + o_i, mitattuna 1 mV:n ADC:llä.
struct SensorModel {
  float e[kChannels];
  float o[kChannels];
  uint16_t mv(int ch, float p, const SensorParams& sp) const {
    const float nominal = (1.0f + e[ch]) * p + o[ch];
    return static_cast<uint16_t>(std::lround(sp.v_zero_mv + sp.mv_per_kpa * nominal));
  }
};

static uint32_t g_lcg = 12345u;
static float noiseMv() {  // tasajakauma ±2 mV, deterministinen
  g_lcg = g_lcg * 1664525u + 1013904223u;
  return static_cast<float>((g_lcg >> 8) & 0xFFFFu) / 65535.0f * 4.0f - 2.0f;
}

static void capture(const SensorModel& m, float p, const SensorParams& sp, int n, float (&mean)[kChannels],
                    float (&sd)[kChannels]) {
  CalCapture cc;
  cc.reset();
  for (int k = 0; k < n; ++k) {
    float nom[kChannels];
    for (int ch = 0; ch < kChannels; ++ch) {
      const float mv = static_cast<float>(m.mv(ch, p, sp)) + noiseMv();
      nom[ch] = sbx::nominalKpa(static_cast<uint16_t>(std::lround(mv)), 0, sp);
    }
    cc.add(nom);
  }
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(n), cc.count());
  cc.result(mean, sd);
}

static float calibrated(const SensorModel& m, int ch, float p, const SensorParams& sp, const Calibration& cal) {
  return sbx::applyCal(sbx::nominalKpa(m.mv(ch, p, sp), 0, sp), ch, cal);
}

static float spreadAt(const SensorModel& m, float p, const SensorParams& sp, const Calibration& cal) {
  float lo = 1e9f, hi = -1e9f;
  for (int ch = 0; ch < kChannels; ++ch) {
    const float v = calibrated(m, ch, p, sp, cal);
    lo = v < lo ? v : lo;
    hi = v > hi ? v : hi;
  }
  return hi - lo;
}

void test_two_point_calibration_makes_channels_agree_everywhere() {
  const SensorParams sp;
  // Offsetit ja herkkyysvirheet samaan suuntaan (R4:n pahin tapaus): −55 kPa:ssa
  // SYL 1 = 1,03·(−55) − 1,5 = −58,15 ja SYL 2 = 0,98·(−55) + 0,7 = −53,2.
  const SensorModel m = {{0.03f, -0.02f, 0.0f}, {-1.5f, 0.7f, 0.2f}};
  Calibration cal;

  // R4: kalibroimattomana synkronoitu moottori näyttäisi yli D2:n keltaisen rajan.
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 4.95f, spreadAt(m, -55.0f, sp, cal));

  // 1) Nolla: moottori seis, kaikki ilmanpaineessa (2 s keskiarvo).
  float mean0[kChannels], sd0[kChannels];
  capture(m, 0.0f, sp, 2000, mean0, sd0);
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeZero(mean0, 5.0f, cal)));
  // Pelkkä nollaus: offset pois, gain-ero jää (D11: 5 % ero −55 kPa:ssa ≈ 2,75 kPa).
  TEST_ASSERT_FLOAT_WITHIN(0.06f, 0.0f, spreadAt(m, 0.0f, sp, cal));
  TEST_ASSERT_GREATER_THAN_FLOAT(2.0f, spreadAt(m, -55.0f, sp, cal));

  // 2) Vahvistus: T-haara, käsipumppu −50 kPa:iin.
  float mean[kChannels], sd[kChannels];
  capture(m, -50.0f, sp, 2000, mean, sd);
  for (int ch = 0; ch < kChannels; ++ch) {
    TEST_ASSERT_LESS_THAN_FLOAT(0.1f, sd[ch]);  // ±2 mV tasajakauma ≈ 0,046 kPa
    TEST_ASSERT_GREATER_THAN_FLOAT(0.02f, sd[ch]);
  }
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeGain(mean, sd, cal)));
  TEST_ASSERT_TRUE(cal.gain_valid);
  TEST_ASSERT_TRUE(cal.zero_valid);
  // Gain kumoaa herkkyysvirheen: g_i·(1+e_i) = vakio.
  for (int ch = 0; ch < kChannels; ++ch) {
    TEST_ASSERT_FLOAT_WITHIN(0.002f, cal.gain[0] * (1.0f + m.e[0]), cal.gain[ch] * (1.0f + m.e[ch]));
  }
  // Referenssi = kanavien keskiarvo: (1 + ē)·P = 1,00333·(−50).
  for (int ch = 0; ch < kChannels; ++ch) {
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -50.1667f, calibrated(m, ch, -50.0f, sp, cal));
  }
  // 3) Kanavat täsmäävät koko käyttöalueella, ei vain kalibrointipisteessä.
  //    Jäljelle jää 1 mV:n kvantisointi (±0,02 kPa kanavaa kohti).
  const float pressures[] = {-100.0f, -75.0f, -55.0f, -30.0f, -8.0f, 0.0f};
  for (float p : pressures) {
    TEST_ASSERT_LESS_THAN_FLOAT(0.12f, spreadAt(m, p, sp, cal));
  }
  // Ilmanpaine on edelleen nolla (nollapiste säilyi).
  for (int ch = 0; ch < kChannels; ++ch) {
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, calibrated(m, ch, 0.0f, sp, cal));
  }
}

void test_rezero_after_gain_keeps_gain_and_agreement() {
  // Nollapiste ajautuu (lämpötila, R5) -> automaattinen nollaus seuraavalla
  // käynnistyksellä. Gain säilyy, ja kanavat täsmäävät taas.
  const SensorParams sp;
  SensorModel m = {{0.04f, -0.03f, 0.01f}, {-1.0f, 0.6f, 1.8f}};
  Calibration cal;
  float mean[kChannels], sd[kChannels];
  capture(m, 0.0f, sp, 500, mean, sd);
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeZero(mean, 5.0f, cal)));
  capture(m, -45.0f, sp, 2000, mean, sd);
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeGain(mean, sd, cal)));
  const Calibration after_gain = cal;

  m.o[0] += 0.8f;  // offset ajautuu
  m.o[2] -= 0.5f;
  TEST_ASSERT_GREATER_THAN_FLOAT(1.0f, spreadAt(m, -55.0f, sp, cal));
  capture(m, 0.0f, sp, 500, mean, sd);
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeZero(mean, 5.0f, cal)));
  for (int ch = 0; ch < kChannels; ++ch) {
    TEST_ASSERT_TRUE_MESSAGE(cal.gain[ch] == after_gain.gain[ch], "nollaus muutti gainia");
  }
  TEST_ASSERT_TRUE(cal.gain_valid);
  TEST_ASSERT_LESS_THAN_FLOAT(0.12f, spreadAt(m, -55.0f, sp, cal));
  TEST_ASSERT_LESS_THAN_FLOAT(0.12f, spreadAt(m, -75.0f, sp, cal));
}

void test_gain_preserves_zero_point_exactly() {
  // Puhdas laskenta: nollapisteet z_i nimellisasteikolla, gain-kalibrointi
  // ei saa siirtää niitä.
  Calibration cal;
  const float z[kChannels] = {0.8f, -1.2f, 1.9f};
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeZero(z, 5.0f, cal)));
  const float c[kChannels] = {-48.0f, -52.0f, -50.5f};  // nollakorjatut lukemat
  float mean[kChannels];
  const float sd[kChannels] = {0.05f, 0.05f, 0.05f};
  for (int i = 0; i < kChannels; ++i) mean[i] = z[i] + c[i];
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeGain(mean, sd, cal)));
  const float ref = (c[0] + c[1] + c[2]) / 3.0f;
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, sbx::applyCal(z[i], i, cal));      // nolla säilyi
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, ref, sbx::applyCal(mean[i], i, cal));    // kaikki lukevat ref
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, ref / c[i], cal.gain[i]);
  }
  // Toistettu kalibrointi samoilla lukemilla on idempotentti (gain ei kerrostu).
  const Calibration first = cal;
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeGain(mean, sd, cal)));
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, first.gain[i], cal.gain[i]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, first.offset_kpa[i], cal.offset_kpa[i]);
  }
}

void test_gain_positive_pressure_source() {
  // GPN-anturilla myös ylipaine kelpaa referenssiksi: vain |ref| ratkaisee.
  Calibration cal;
  cal.zero_valid = true;  // nollapiste nimellisasteikon nollassa
  const float mean[kChannels] = {40.0f, 41.0f, 39.0f};
  const float sd[kChannels] = {0.1f, 0.1f, 0.1f};
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeGain(mean, sd, cal)));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 40.0f / 41.0f, cal.gain[1]);
}

// --- vahvistus: jokainen hylkäyspolku, kalibrointi ennallaan --------------------

static Calibration someCal() {
  Calibration cal;
  cal.offset_kpa[0] = 0.4f;
  cal.offset_kpa[1] = -0.3f;
  cal.offset_kpa[2] = 0.1f;
  cal.gain[0] = 1.01f;
  cal.zero_valid = true;
  return cal;
}

static void expectGain(CalResult want, const float (&mean)[kChannels], const float (&sd)[kChannels]) {
  Calibration cal = someCal();
  const Calibration before = cal;
  TEST_ASSERT_EQUAL_INT(asInt(want), asInt(sbx::computeGain(mean, sd, cal)));
  if (want != CalResult::Ok) {
    assertCalEqual(before, cal);
    TEST_ASSERT_FALSE(cal.gain_valid);
  }
}

void test_gain_too_small() {
  const float sd[kChannels] = {0.05f, 0.05f, 0.05f};
  expectGain(CalResult::TooSmall, {-10.0f, -10.2f, -9.9f}, sd);   // pumppu tuskin painettu
  expectGain(CalResult::TooSmall, {0.0f, 0.0f, 0.0f}, sd);        // ilmanpaine
  // Raja nollakorjatusta keskiarvosta: someCal-nollapisteet z = -offset/gain.
  Calibration probe = someCal();
  float z[kChannels];
  for (int i = 0; i < kChannels; ++i) z[i] = -probe.offset_kpa[i] / probe.gain[i];
  expectGain(CalResult::TooSmall, {z[0] - 14.9f, z[1] - 14.9f, z[2] - 14.9f}, sd);
  expectGain(CalResult::Ok, {z[0] - 15.01f, z[1] - 15.01f, z[2] - 15.01f}, sd);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  expectGain(CalResult::TooSmall, {nan, -50.0f, -50.0f}, sd);
}

void test_gain_not_stable() {
  const float mean[kChannels] = {-50.0f, -50.3f, -49.8f};
  expectGain(CalResult::NotStable, mean, {0.1f, 0.6f, 0.1f});   // vuotava T-haara
  expectGain(CalResult::NotStable, mean, {0.1f, 0.1f, 0.5f});   // raja on tiukka <
  expectGain(CalResult::Ok, mean, {0.49f, 0.49f, 0.49f});
  const float inf = std::numeric_limits<float>::infinity();
  expectGain(CalResult::NotStable, mean, {inf, inf, inf});      // CalCapture < 2 näytettä
  const float nan = std::numeric_limits<float>::quiet_NaN();
  expectGain(CalResult::NotStable, mean, {0.1f, nan, 0.1f});
}

// Nollakorjatut lukemat c -> nimelliset keskiarvot someCal-nollapisteillä
// (z = −offset/gain), jotta testi puhuu suoraan siitä, mitä kanavat näkivät.
static void expectGainC(CalResult want, float c0, float c1, float c2) {
  const Calibration probe = someCal();
  const float c[kChannels] = {c0, c1, c2};
  float mean[kChannels];
  for (int i = 0; i < kChannels; ++i) mean[i] = c[i] - probe.offset_kpa[i] / probe.gain[i];
  const float sd[kChannels] = {0.05f, 0.05f, 0.05f};
  expectGain(want, mean, sd);
}

void test_gain_mismatch() {
  const float sd[kChannels] = {0.05f, 0.05f, 0.05f};
  expectGain(CalResult::Mismatch, {-50.0f, -50.0f, 0.0f}, sd);    // letku irti T:stä
  expectGain(CalResult::Mismatch, {-50.0f, -50.0f, -35.0f}, sd);  // vuoto: 30 % vähemmän
  expectGain(CalResult::Mismatch, {-50.0f, -50.0f, 5.0f}, sd);    // väärä etumerkki
  expectGain(CalResult::Mismatch, {-30.0f, -50.0f, -70.0f}, sd);  // kaikki eri paineissa
  // Yksi kanava 20–25 % muista: tyypillinen vuotava letku T:llä. Kanavat
  // ovat 15 %:n sisällä KESKIARVOSTA, mutta eivät toisistaan -> Mismatch eikä
  // GainOutOfRange ("tarkista anturityyppi" olisi väärä neuvo).
  expectGainC(CalResult::Mismatch, -50.0f, -50.0f, -60.0f);   // 20 %
  expectGainC(CalResult::Mismatch, -50.0f, -50.0f, -62.0f);   // 24 %
  expectGainC(CalResult::Mismatch, -50.0f, -50.0f, -41.0f);   // 22 % (50/41)
  expectGainC(CalResult::Mismatch, -57.0f, -46.5f, -46.5f);   // 22,6 %
  expectGainC(CalResult::Mismatch, -50.0f, -50.0f, -40.0f);   // 25 %
  expectGainC(CalResult::Mismatch, -43.0f, -53.5f, -53.5f);   // 24 %
  // 15 %:n raja pareittain, suhteessa pienempään itseisarvoon:
  // 50 · 1,15 = 57,5 ja 43,5 · 1,15 = 50,025.
  expectGainC(CalResult::Ok, -50.0f, -50.0f, -57.4f);
  expectGainC(CalResult::Mismatch, -50.0f, -50.0f, -57.6f);
  expectGainC(CalResult::Ok, -50.0f, -50.0f, -43.6f);
  expectGainC(CalResult::Mismatch, -50.0f, -50.0f, -43.4f);
  expectGainC(CalResult::Ok, -50.0f, -53.7f, -57.4f);         // kaikki eri, 14,8 % päästä päähän
  expectGainC(CalResult::Mismatch, -50.0f, -53.8f, -57.6f);
  expectGainC(CalResult::Ok, 40.0f, 45.9f, 43.0f);            // ylipaine (GPN) samoin
  expectGainC(CalResult::Mismatch, 40.0f, 46.1f, 43.0f);
}

void test_gain_out_of_range_is_defensive_only() {
  // Pareittaisen 15 %:n ehdon jälkeen gain = ref / c on aina 0,913…1,10
  // (ref on 1,05…1,10 × pienin |c|), joten GainOutOfRange ei voi laueta.
  // Pyyhkäistään koko lähialue: jokainen hylkäys on Mismatch, ja jokainen
  // hyväksytty kalibrointi saa kanavat lukemaan referenssiä marginaalilla
  // [0,85, 1,15]-rajaan.
  int ok = 0;
  int mismatch = 0;
  for (int a = -30; a <= 30; ++a) {
    for (int b = -30; b <= 30; ++b) {
      const float c1 = -50.0f * (1.0f + 0.01f * static_cast<float>(a));
      const float c2 = -50.0f * (1.0f + 0.01f * static_cast<float>(b));
      const float mean[kChannels] = {-50.0f, c1, c2};
      const float sd[kChannels] = {0.05f, 0.05f, 0.05f};
      Calibration cal;
      cal.zero_valid = true;
      const CalResult r = sbx::computeGain(mean, sd, cal);
      TEST_ASSERT_TRUE(r == CalResult::Ok || r == CalResult::Mismatch);
      // Itseisarvojen ääripäät (kaikki negatiivisia).
      const float mags[kChannels] = {50.0f, -c1, -c2};
      float lo = mags[0], hi = mags[0];
      for (float v : mags) {
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
      }
      if (r == CalResult::Ok) {
        ++ok;
        TEST_ASSERT_LESS_THAN_FLOAT(1.15f * lo + 1e-3f, hi);  // pareittain ≤ 15 %
        for (int i = 0; i < kChannels; ++i) {
          TEST_ASSERT_GREATER_THAN_FLOAT(0.91f, cal.gain[i]);
          TEST_ASSERT_LESS_THAN_FLOAT(1.101f, cal.gain[i]);
        }
      } else {
        ++mismatch;
        TEST_ASSERT_GREATER_THAN_FLOAT(1.15f * lo - 1e-3f, hi);
      }
    }
  }
  TEST_ASSERT_GREATER_THAN(100, ok);
  TEST_ASSERT_GREATER_THAN(100, mismatch);
}

void test_gain_requires_zero() {
  // Arvostelijan skenaario (R4): identtinen herkkyys, offsetit −1,5 / +0,7 /
  // +0,2 kPa. Ilman nollausta offsetit menisivät gainiin (4 % @ −50 kPa) ja
  // tallentuisivat NVS:ään; myöhemmän nollauksen jälkeen synkronoitu moottori
  // näyttäisi −55 kPa:ssa 2,4 kPa eroa eli ei koskaan vihreää (D2).
  const SensorParams sp;
  const SensorModel m = {{0.0f, 0.0f, 0.0f}, {-1.5f, 0.7f, 0.2f}};
  Calibration cal;
  float mean[kChannels], sd[kChannels];
  capture(m, -50.0f, sp, 2000, mean, sd);
  const Calibration before = cal;
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::NoZero), asInt(sbx::computeGain(mean, sd, cal)));
  assertCalEqual(before, cal);
  TEST_ASSERT_FALSE(cal.gain_valid);
  // Gain-kalibroitu mutta tämän käynnistyksen nolla puuttuu (zero_valid ei
  // tallennu NVS:ään, D11) -> sama vaatimus, vanha gain säilyy.
  cal.gain[1] = 1.02f;
  cal.gain_valid = true;
  const Calibration before2 = cal;
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::NoZero), asInt(sbx::computeGain(mean, sd, cal)));
  assertCalEqual(before2, cal);
  // Oikea järjestys: nolla ilmanpaineessa, sitten vahvistus -> kanavat täsmäävät.
  Calibration good;
  float mean0[kChannels], sd0[kChannels];
  capture(m, 0.0f, sp, 500, mean0, sd0);
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeZero(mean0, 5.0f, good)));
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeGain(mean, sd, good)));
  for (int ch = 0; ch < kChannels; ++ch) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, good.gain[ch]);  // herkkyys oli jo sama
  }
  TEST_ASSERT_LESS_THAN_FLOAT(0.12f, spreadAt(m, -55.0f, sp, good));
}

void test_gain_check_precedence() {
  // Järjestys: NoZero, TooSmall, NotStable, Mismatch, GainOutOfRange - karkein vika ensin.
  {
    Calibration cal;  // zero_valid = false
    const float mean[kChannels] = {-5.0f, -5.0f, 0.0f};
    const float sd[kChannels] = {1.0f, 1.0f, 1.0f};
    TEST_ASSERT_EQUAL_INT(asInt(CalResult::NoZero), asInt(sbx::computeGain(mean, sd, cal)));
  }
  expectGain(CalResult::TooSmall, {-5.0f, -5.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
  expectGain(CalResult::NotStable, {-50.0f, -50.0f, 0.0f}, {1.0f, 0.1f, 0.1f});
  expectGain(CalResult::Mismatch, {-50.0f, -50.0f, 0.0f}, {0.1f, 0.1f, 0.1f});
}

void test_gain_with_corrupt_old_gain_stays_finite() {
  // NVS:stä luettu gain 0 tai NaN ei saa tuottaa ääretöntä/NaN-kalibrointia.
  Calibration cal;
  cal.zero_valid = true;
  cal.gain[1] = 0.0f;
  cal.gain[2] = std::numeric_limits<float>::quiet_NaN();
  const float mean[kChannels] = {-50.0f, -51.0f, -49.0f};
  const float sd[kChannels] = {0.1f, 0.1f, 0.1f};
  TEST_ASSERT_EQUAL_INT(asInt(CalResult::Ok), asInt(sbx::computeGain(mean, sd, cal)));
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_TRUE(std::isfinite(cal.gain[i]));
    TEST_ASSERT_TRUE(std::isfinite(cal.offset_kpa[i]));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -50.0f, sbx::applyCal(mean[i], i, cal));
  }
}

void test_cal_result_text() {
  const CalResult all[] = {CalResult::Ok,       CalResult::NotAtmosphere, CalResult::NotStable,
                           CalResult::TooSmall, CalResult::Mismatch,      CalResult::GainOutOfRange,
                           CalResult::NoZero,   CalResult::NotSettled};
  const int n_all = static_cast<int>(sizeof(all) / sizeof(all[0]));
  for (int i = 0; i < n_all; ++i) {
    const char* t = sbx::calResultText(all[i]);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_GREATER_THAN(10, std::strlen(t));
    TEST_ASSERT_LESS_THAN(160, std::strlen(t));  // mahtuu snapshotin cal.msg-kenttään
    for (int j = i + 1; j < n_all; ++j) TEST_ASSERT_TRUE(std::strcmp(t, sbx::calResultText(all[j])) != 0);
  }
  // Teksti kertoo mitä tehdä, ei vain mitä tapahtui.
  TEST_ASSERT_NOT_NULL(std::strstr(sbx::calResultText(CalResult::NotAtmosphere), "Sammuta moottori"));
  // NotAtmosphere tulee myös positiivisesta lukemasta (väärä v_zero_mv, ylös
  // leikkaava anturi), joten teksti ei saa väittää pelkkää alipainetta.
  TEST_ASSERT_NOT_NULL(std::strstr(sbx::calResultText(CalResult::NotAtmosphere), "v_zero_mv"));
  TEST_ASSERT_NULL(std::strstr(sbx::calResultText(CalResult::NotAtmosphere), "kanavassa on alipainetta"));
  TEST_ASSERT_NOT_NULL(std::strstr(sbx::calResultText(CalResult::NoZero), "Nollaa"));
  TEST_ASSERT_NOT_NULL(std::strstr(sbx::calResultText(CalResult::NotStable), "vuoda"));
  TEST_ASSERT_NOT_NULL(std::strstr(sbx::calResultText(CalResult::TooSmall), "15 kPa"));
  TEST_ASSERT_NOT_NULL(std::strstr(sbx::calResultText(CalResult::Mismatch), "T-haara"));
  TEST_ASSERT_NOT_NULL(std::strstr(sbx::calResultText(CalResult::GainOutOfRange), "mv_per_kpa"));
  // WP-9: odottaminen riittää (juuri sammutettu moottori), ei vikaa.
  TEST_ASSERT_NOT_NULL(std::strstr(sbx::calResultText(CalResult::NotSettled), "Odota"));
  TEST_ASSERT_EQUAL_INT(7, asInt(CalResult::NotSettled));   // lisätty loppuun (§4.3)
  TEST_ASSERT_NOT_NULL(sbx::calResultText(static_cast<CalResult>(42)));
}

// --- CalCapture ------------------------------------------------------------------

void test_capture_empty_and_single() {
  CalCapture cc;
  float mean[kChannels], sd[kChannels];
  cc.result(mean, sd);
  TEST_ASSERT_EQUAL_UINT32(0, cc.count());
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mean[i]);
    TEST_ASSERT_TRUE(std::isinf(sd[i]));  // vakautta ei voi osoittaa
  }
  const float one[kChannels] = {-50.0f, -51.0f, -52.0f};
  cc.add(one);
  cc.result(mean, sd);
  TEST_ASSERT_EQUAL_UINT32(1, cc.count());
  TEST_ASSERT_EQUAL_FLOAT(-51.0f, mean[1]);
  TEST_ASSERT_TRUE(std::isinf(sd[1]));
}

void test_capture_known_values() {
  CalCapture cc;
  cc.reset();
  const float xs[] = {-50.0f, -52.0f, -48.0f, -51.0f, -49.0f};
  for (float x : xs) {
    const float s[kChannels] = {x, 2.0f * x, -55.0f};
    cc.add(s);
  }
  float mean[kChannels], sd[kChannels];
  cc.result(mean, sd);
  TEST_ASSERT_EQUAL_UINT32(5, cc.count());
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, -50.0f, mean[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, std::sqrt(2.5f), sd[0]);         // otoshajonta (n − 1)
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, -100.0f, mean[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f * std::sqrt(2.5f), sd[1]);  // kanavat erillään
  TEST_ASSERT_EQUAL_FLOAT(-55.0f, mean[2]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sd[2]);                            // vakio -> täsmälleen 0
  cc.reset();
  TEST_ASSERT_EQUAL_UINT32(0, cc.count());
}

void test_capture_float_precision_against_double() {
  // 2 s × 1 kHz −55 kPa:ssa pienellä kohinalla: juuri se tilanne, jossa
  // floatin neliösumma kumoutuisi. Welford floatina vs. kaksivaiheinen double.
  CalCapture cc;
  cc.reset();
  const int n = 2000;
  static float xs[n];
  g_lcg = 777u;
  for (int k = 0; k < n; ++k) {
    xs[k] = -55.0f + 0.025f * noiseMv();  // ±0,05 kPa tasajakauma, sd ≈ 0,0289
    const float s[kChannels] = {xs[k], xs[k], xs[k]};
    cc.add(s);
  }
  double sum = 0.0;
  for (int k = 0; k < n; ++k) sum += static_cast<double>(xs[k]);
  const double dmean = sum / n;
  double ss = 0.0;
  for (int k = 0; k < n; ++k) ss += (static_cast<double>(xs[k]) - dmean) * (static_cast<double>(xs[k]) - dmean);
  const double dsd = std::sqrt(ss / (n - 1));
  float mean[kChannels], sd[kChannels];
  cc.result(mean, sd);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, static_cast<float>(dmean), mean[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.02f * static_cast<float>(dsd), static_cast<float>(dsd), sd[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.003f, 0.0289f, sd[0]);  // 0,1 kPa / √12
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_transfer_function_matches_datasheet_table);
  RUN_TEST(test_transfer_function_other_sensor_variants);
  RUN_TEST(test_rail_compensation_r6);
  RUN_TEST(test_rail_compensation_ignores_broken_monitor);
  RUN_TEST(test_apply_cal);
  RUN_TEST(test_zero_ok_preserves_gain);
  RUN_TEST(test_zero_boundary_and_not_atmosphere);
  RUN_TEST(test_zero_settle_gate_order_and_edges);
  RUN_TEST(test_level_settle_window);
  RUN_TEST(test_two_point_calibration_makes_channels_agree_everywhere);
  RUN_TEST(test_rezero_after_gain_keeps_gain_and_agreement);
  RUN_TEST(test_gain_preserves_zero_point_exactly);
  RUN_TEST(test_gain_positive_pressure_source);
  RUN_TEST(test_gain_too_small);
  RUN_TEST(test_gain_not_stable);
  RUN_TEST(test_gain_mismatch);
  RUN_TEST(test_gain_out_of_range_is_defensive_only);
  RUN_TEST(test_gain_requires_zero);
  RUN_TEST(test_gain_check_precedence);
  RUN_TEST(test_gain_with_corrupt_old_gain_stays_finite);
  RUN_TEST(test_cal_result_text);
  RUN_TEST(test_capture_empty_and_single);
  RUN_TEST(test_capture_known_values);
  RUN_TEST(test_capture_float_precision_against_double);
  return UNITY_END();
}
