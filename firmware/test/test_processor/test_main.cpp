// WP-6: processor.h -yksikkötestit (docs/firmware.md §4.8).
//
// Syöte on enimmäkseen vakio-mV:tä. DSP:n EMA-taso ja aaltomuodon
// lohkokeskiarvo ovat silloin tasan kalibroitu arvo, joten muunnosketju
// (nominalKpa + applyCal, R4) ja kalibrointikomennot voidaan tarkistaa
// absoluuttisesti eikä "näyttää järkevältä" -tasolla. Pulssivaa signaalia
// tarvitaan vain masterin vaihdossa (D7: DSP ei saa nollautua); se tulee
// EngineSimistä. Koko ketju simuloidulla moottorilla: test_integration.

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/engine_sim.h"
#include "core/processor.h"
#include "core/raw_ring.h"

using sbx::CalResult;
using sbx::Calibration;
using sbx::Config;
using sbx::CsvWriter;
using sbx::EngineSim;
using sbx::FormPair;
using sbx::Processor;
using sbx::RawFrame;
using sbx::RawSample;
using sbx::SimParams;
using sbx::SyncState;
using sbx::kChannels;

namespace {

constexpr size_t kRingCap = 4096;
RawSample g_mem[kRingCap];

// GN-anturi ADC-pinnissä oletusparametreilla: 2700 mV @ 0 kPa, 25 mV/kPa.
uint16_t mvOf(float kpa) {
  float mv = 2700.0f + 25.0f * kpa;
  if (mv < 0.0f) mv = 0.0f;
  if (mv > 3300.0f) mv = 3300.0f;
  return static_cast<uint16_t>(mv + 0.5f);
}

// Liiman tahti: push 1 kHz, tick 20 Hz (joka 50. näyte), now = näytteet / 1000.
struct Feed {
  Processor& p;
  uint32_t seq = 0;
  bool auto_tick = true;

  explicit Feed(Processor& proc) : p(proc) {}
  float now() const { return static_cast<float>(seq) / 1000.0f; }

  void frame(uint16_t a, uint16_t b, uint16_t c, uint16_t rail = 0) {
    RawFrame f;
    f.seq = seq;
    f.mv[0] = a;
    f.mv[1] = b;
    f.mv[2] = c;
    f.rail_mv = rail;
    p.push(f);
    ++seq;
    if (auto_tick && seq % 50 == 0) p.tick(now());
  }
  void hold(uint16_t a, uint16_t b, uint16_t c, float seconds, uint16_t rail = 0) {
    const int n = static_cast<int>(seconds * 1000.0f + 0.5f);
    for (int i = 0; i < n; ++i) frame(a, b, c, rail);
  }
  void holdKpa(float k0, float k1, float k2, float seconds) {
    hold(mvOf(k0), mvOf(k1), mvOf(k2), seconds);
  }
};

int S(SyncState s) { return static_cast<int>(s); }
int R(CalResult r) { return static_cast<int>(r); }

// Masteria koskevat odotukset on kirjoitettu SYL 1 -masterille, joka oli D7:n
// oletus. D16 (31.10.2026) vaihtoi oletuksen SYL 3:een, joten SYL 1 kiinnitetään
// eksplisiittisesti: testit testaavat samaa kuin ennen. configure on lataus eikä
// käyttäjän muutos, joten takeConfigDirty ei nouse.
void pinSyl1Master(Processor& p) {
  Config c = p.config();
  c.status.master = 0;
  TEST_ASSERT_TRUE(p.configure(c));
  TEST_ASSERT_EQUAL_INT(0, p.master());
}

bool utf8Valid(const char* s) {
  const unsigned char* q = reinterpret_cast<const unsigned char*>(s);
  while (*q) {
    int n = 0;
    if (*q < 0x80u) {
      n = 0;
    } else if ((*q & 0xE0u) == 0xC0u) {
      n = 1;
    } else if ((*q & 0xF0u) == 0xE0u) {
      n = 2;
    } else if ((*q & 0xF8u) == 0xF0u) {
      n = 3;
    } else {
      return false;
    }
    ++q;
    for (int k = 0; k < n; ++k, ++q) {
      if ((*q & 0xC0u) != 0x80u) return false;
    }
  }
  return true;
}

// Nollaus vaatii tason pysyneen paikallaan koko vakausikkunan (WP-9,
// calibration.h kZeroSettleWindowS = 1 s) + yksi tick. Ennen WP-9:ää testit
// pitivät tasoa 0,5 s; arvo oli asetelma, ei väite.
constexpr float kSettledS = sbx::kZeroSettleWindowS + 0.1f;

// Kalibroitu T-haara (R4, docs/rauta.md §7): nollaus ilmanpaineessa, sitten
// sama alipaine kaikissa kanavissa ja 2 s mittaus.
void zeroAtAtmosphere(Feed& f) {
  f.holdKpa(0.0f, 0.0f, 0.0f, kSettledS);
  TEST_ASSERT_EQUAL_INT(R(CalResult::Ok), R(f.p.zeroNow()));
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Muunnos (§4.8: push = nominalKpa + applyCal, R4/R6)
// ---------------------------------------------------------------------------

void test_conversion_nominal_then_calibration() {
  Processor p(g_mem, kRingCap);
  Calibration cal;
  const float gain[kChannels] = {1.02f, 0.98f, 1.10f};
  const float off[kChannels] = {0.5f, -0.3f, 1.0f};
  for (int i = 0; i < kChannels; ++i) {
    cal.gain[i] = gain[i];
    cal.offset_kpa[i] = off[i];
  }
  cal.gain_valid = true;
  p.setCalibration(cal);
  Feed f(p);
  f.hold(2200, 1950, 2700, 0.2f);  // nimellinen -20, -30, 0 kPa
  const float expect[kChannels] = {1.02f * -20.0f + 0.5f, 0.98f * -30.0f - 0.3f, 1.0f};
  for (int i = 0; i < kChannels; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, expect[i], p.channel(i).level_kpa);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, expect[i], p.channel(i).last_kpa);
  }
  // Aaltomuoto on samaa kalibroitua kPa:ta (§5 wave.s): 200 näytettä = 12 lohkoa.
  float rows[Processor::kWaveRows][kChannels];
  const int n = p.takeWave(rows, Processor::kWaveRows);
  TEST_ASSERT_EQUAL_INT(12, n);
  for (int r = 0; r < n; ++r) {
    for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-3f, expect[i], rows[r][i]);
  }
}

void test_conversion_rail_compensation_R6() {
  // R6: kiskon notkahdus skaalataan pois ennen siirtofunktiota (calibration.h).
  Processor p(g_mem, kRingCap);
  Config c;
  c.sensor.rail_comp = true;
  TEST_ASSERT_TRUE(p.configure(c));
  Feed f(p);
  f.hold(2200, 2200, 2200, 0.1f, 3250);
  const float expect = (2200.0f * 3300.0f / 3250.0f - 2700.0f) / 25.0f;
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-3f, expect, p.channel(i).level_kpa);
}

void test_channel_and_status_are_tick_time_values() {
  // §4.8: snapshotin luvut ja tila samalta tickiltä (d = mean_master - mean_ch).
  Processor p(g_mem, kRingCap);
  Feed f(p);
  f.auto_tick = false;
  f.hold(2200, 2200, 2200, 0.1f);
  TEST_ASSERT_EQUAL_INT(0, p.channel(0).last_mv);  // ei tickiä vielä
  p.tick(f.now());
  TEST_ASSERT_EQUAL_INT(2200, p.channel(0).last_mv);
  TEST_ASSERT_EQUAL_INT(S(SyncState::NoSignal), S(p.status().state));
  TEST_ASSERT_EQUAL_STRING("Ei alipainesignaalia", p.status().headline);
}

// ---------------------------------------------------------------------------
// setCalibration (D11) ja NVS:n rikkinäiset arvot
// ---------------------------------------------------------------------------

void test_set_calibration_forces_zero_invalid_D11() {
  Processor p(g_mem, kRingCap);
  Calibration cal;
  cal.offset_kpa[0] = 1.5f;
  cal.gain[1] = 1.04f;
  cal.zero_valid = true;  // edellisen käynnistyksen nollaus ei kelpaa
  cal.gain_valid = true;
  p.setCalibration(cal);
  TEST_ASSERT_FALSE(p.calibration().zero_valid);
  TEST_ASSERT_TRUE(p.calibration().gain_valid);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.04f, p.calibration().gain[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, p.calibration().offset_kpa[0]);
  TEST_ASSERT_FALSE(p.takeCalDirty());  // ei korjattavaa
  Feed f(p);
  f.holdKpa(-55.0f, -55.0f, -55.0f, 0.1f);
  TEST_ASSERT_TRUE(p.status().flag_uncal_zero);    // D11
  TEST_ASSERT_FALSE(p.status().flag_uncal_gain);   // rivi 11
}

void test_set_calibration_sanitizes_broken_gain() {
  // Rikkinäinen gain tekisi lukemista NaN:ia tai vääriä; rivi 11 näyttää
  // silloin vahvistuksen puuttuvan.
  const float bad[] = {std::nanf(""), 1.3f, 0.5f, 0.0f, -1.0f};
  for (float g : bad) {
    Processor p(g_mem, kRingCap);
    Calibration cal;
    cal.gain[2] = g;
    cal.gain[0] = 1.05f;
    cal.gain_valid = true;
    p.setCalibration(cal);
    for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_FLOAT(1.0f, p.calibration().gain[i]);
    TEST_ASSERT_FALSE(p.calibration().gain_valid);
    TEST_ASSERT_TRUE(p.takeCalDirty());  // NVS siivotaan
  }
  // gain ilman gain_valid-lippua ei ole kalibrointi.
  Processor p(g_mem, kRingCap);
  Calibration cal;
  cal.gain[1] = 1.05f;
  cal.offset_kpa[1] = std::nanf("");
  p.setCalibration(cal);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, p.calibration().gain[1]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.calibration().offset_kpa[1]);
  TEST_ASSERT_TRUE(p.takeCalDirty());
}

// ---------------------------------------------------------------------------
// Nollaus (rivi 1, D10, D11)
// ---------------------------------------------------------------------------

void test_zero_now_at_atmosphere() {
  Processor p(g_mem, kRingCap);
  Feed f(p);
  f.hold(2650, 2675, 2725, kSettledS);  // anturien offsetit -2, -1, +1 kPa (R4)
  TEST_ASSERT_EQUAL_INT(R(CalResult::Ok), R(p.zeroNow()));
  TEST_ASSERT_TRUE(p.calibration().zero_valid);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2.0f, p.calibration().offset_kpa[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, p.calibration().offset_kpa[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -1.0f, p.calibration().offset_kpa[2]);
  TEST_ASSERT_EQUAL_STRING(sbx::calResultText(CalResult::Ok), p.calStatus().msg);
  TEST_ASSERT_FALSE(p.takeCalDirty());  // D11: nollaus ei ole pysyvä
  // DSP nollattiin uuteen kalibrointiin: taso on heti nolla, ei EMA-liukumaa.
  f.hold(2650, 2675, 2725, 0.05f);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, p.channel(i).level_kpa);
  TEST_ASSERT_FALSE(p.status().flag_uncal_zero);
}

void test_zero_now_keeps_gain() {
  // computeZero säilyttää gainin: offset = -gain * nimellinen, jolloin
  // ilmanpaine kuvautuu nollaan myös vahvistuskalibroidulla kanavalla.
  Processor p(g_mem, kRingCap);
  Calibration cal;
  cal.gain[0] = 1.05f;
  cal.gain[2] = 0.95f;
  cal.gain_valid = true;
  p.setCalibration(cal);
  Feed f(p);
  f.hold(2650, 2650, 2650, kSettledS);  // nimellinen -2 kPa
  TEST_ASSERT_EQUAL_INT(R(CalResult::Ok), R(p.zeroNow()));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2.1f, p.calibration().offset_kpa[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.9f, p.calibration().offset_kpa[2]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.05f, p.calibration().gain[0]);
  f.hold(2650, 2650, 2650, 0.05f);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, p.channel(i).level_kpa);
}

void test_zero_now_rejected_without_atmosphere() {
  Processor p(g_mem, kRingCap);
  // Ei yhtään näytettä: ilmanpainetta ei ole todettu.
  TEST_ASSERT_EQUAL_INT(R(CalResult::NotAtmosphere), R(p.zeroNow()));
  TEST_ASSERT_FALSE(p.calibration().zero_valid);
  TEST_ASSERT_EQUAL_STRING(sbx::calResultText(CalResult::NotAtmosphere), p.calStatus().msg);
  Feed f(p);
  // Yksi kanava off_kpa:n (5 kPa, D10) ulkopuolella: moottori käy tai alipaine loukussa.
  f.holdKpa(0.0f, -6.0f, 0.0f, 0.5f);
  TEST_ASSERT_EQUAL_INT(R(CalResult::NotAtmosphere), R(p.zeroNow()));
  TEST_ASSERT_FALSE(p.calibration().zero_valid);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_FLOAT(0.0f, p.calibration().offset_kpa[i]);
  // Raja on off_kpa (S5a): 8 kPa:lla sama tilanne kelpaa.
  Config c;
  c.status.off_kpa = 8.0f;
  TEST_ASSERT_TRUE(p.configure(c));
  f.holdKpa(0.0f, -6.0f, 0.0f, kSettledS);
  TEST_ASSERT_EQUAL_INT(R(CalResult::Ok), R(p.zeroNow()));
}

// WP-9 (jatko.md 8): käsin nollaus vaatii tason vakauden. Juuri sammutetun
// moottorin alipaine purkautuu: taso laskee -4 -> 0 kPa 1 kPa/s, kaikki
// |taso| < off_kpa koko ajan. Ennen WP-9:ää zeroNow hyväksyi tämän ja kirjasi
// laskevan tason offsetiksi. Raja: vaihteluväli 1 s:n aikana < zero_settle_kpa.
void test_zero_now_rejects_bleeding_vacuum() {
  Processor p(g_mem, kRingCap);
  Config c;
  c.status.auto_zero = false;   // vain käsin nollaus tässä
  TEST_ASSERT_TRUE(p.configure(c));
  Feed f(p);
  for (int ms = 0; ms < 3000; ++ms) {   // -4,0 -> -1,0 kPa
    const float kpa = -4.0f + static_cast<float>(ms) / 1000.0f;
    f.holdKpa(kpa, kpa, kpa, 0.001f);
  }
  TEST_ASSERT_EQUAL_INT(R(CalResult::NotSettled), R(p.zeroNow()));
  TEST_ASSERT_FALSE(p.calibration().zero_valid);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_FLOAT(0.0f, p.calibration().offset_kpa[i]);
  TEST_ASSERT_EQUAL_STRING(sbx::calResultText(CalResult::NotSettled), p.calStatus().msg);
  // Yksikin kanava liikkeessä (0,3 kPa/s) riittää hylkäämään. Taso-EMA:n
  // jälkiviive (tau 0,5 s) asettuu ensin 2 s.
  f.holdKpa(-1.0f, -1.0f, -1.0f, 2.0f);
  for (int ms = 0; ms < 1100; ++ms) {
    f.holdKpa(-1.0f, -1.0f + 0.32f * static_cast<float>(ms) / 1100.0f, -1.0f, 0.001f);
  }
  TEST_ASSERT_EQUAL_INT(R(CalResult::NotSettled), R(p.zeroNow()));
  // Asettunut taso: nollaus onnistuu ja offset on lopullinen taso
  // (-0,68 kPa = 2683 mV tasan).
  f.holdKpa(-1.0f, -0.68f, -1.0f, 2.0f);
  TEST_ASSERT_EQUAL_INT(R(CalResult::Ok), R(p.zeroNow()));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, p.calibration().offset_kpa[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.68f, p.calibration().offset_kpa[1]);
}

void test_zero_now_needs_full_settle_window() {
  // Vakautta ei voi osoittaa ilman ikkunan verran historiaa: käynnistyksen,
  // asetusten muutoksen (DSP ja ikkuna nollautuvat) ja kalibroinnin jälkeen.
  Processor p(g_mem, kRingCap);
  Feed f(p);
  f.holdKpa(0.0f, 0.0f, 0.0f, sbx::kZeroSettleWindowS - 0.1f);
  TEST_ASSERT_EQUAL_INT(R(CalResult::NotSettled), R(p.zeroNow()));
  f.holdKpa(0.0f, 0.0f, 0.0f, 0.2f);
  TEST_ASSERT_EQUAL_INT(R(CalResult::Ok), R(p.zeroNow()));
  // Nollaus nollasi DSP:n ja ikkunan: heti perään uusi nollaus odottaa.
  f.holdKpa(0.0f, 0.0f, 0.0f, 0.5f);
  TEST_ASSERT_EQUAL_INT(R(CalResult::NotSettled), R(p.zeroNow()));
  TEST_ASSERT_TRUE(p.calibration().zero_valid);   // hylkäys ei mitätöi edellistä
  const FormPair form[] = {{"tol_green_kpa", "1,2"}};
  char err[sbx::kFormErrLen];
  f.holdKpa(0.0f, 0.0f, 0.0f, kSettledS);
  TEST_ASSERT_TRUE(p.applyConfigForm(form, 1, err, sizeof(err)));
  f.holdKpa(0.0f, 0.0f, 0.0f, 0.5f);
  TEST_ASSERT_EQUAL_INT(R(CalResult::NotSettled), R(p.zeroNow()));
  f.holdKpa(0.0f, 0.0f, 0.0f, 0.6f);
  TEST_ASSERT_EQUAL_INT(R(CalResult::Ok), R(p.zeroNow()));
}

void test_auto_zero_row1_once_per_no_signal_period() {
  // Rivi 1: NoSignal off_hold_s (3 s) tasaisilla tasoilla -> StatusMachine
  // pyytää kerran -> Processor nollaa. Ensimmäinen tick on t = 0,05 s, joten
  // pyyntö tulee noin hetkellä 3,05 s (tarkka raja: test_status).
  Processor p(g_mem, kRingCap);
  Feed f(p);
  f.hold(2662, 2688, 2720, 2.9f);  // -1,52 / -0,48 / +0,8 kPa kalibroimatta
  TEST_ASSERT_FALSE_MESSAGE(p.calibration().zero_valid, "nollaus ennen off_hold_s:ää");
  f.hold(2662, 2688, 2720, 0.3f);
  TEST_ASSERT_TRUE(p.calibration().zero_valid);
  TEST_ASSERT_EQUAL_STRING(Processor::kMsgAutoZero, p.calStatus().msg);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.52f, p.calibration().offset_kpa[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.48f, p.calibration().offset_kpa[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.8f, p.calibration().offset_kpa[2]);
  // Reunaliipattu: sama jakso ei nollaa uudelleen, vaikka offset muuttuisi.
  f.hold(2650, 2650, 2650, 10.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.52f, p.calibration().offset_kpa[0]);
}

void test_auto_zero_disabled() {
  Processor p(g_mem, kRingCap);
  Config c;
  c.status.auto_zero = false;  // S5a
  TEST_ASSERT_TRUE(p.configure(c));
  Feed f(p);
  f.hold(2650, 2650, 2650, 10.0f);
  TEST_ASSERT_FALSE(p.calibration().zero_valid);
  TEST_ASSERT_EQUAL_STRING("", p.calStatus().msg);
}

// ---------------------------------------------------------------------------
// Vahvistusmittaus (R4, §4.8: 2 s nimellisillä näytteillä, päättyy tickissä)
// ---------------------------------------------------------------------------

void test_gain_capture_two_seconds_then_tick() {
  Processor p(g_mem, kRingCap);
  Feed f(p);
  zeroAtAtmosphere(f);
  f.hold(1425, 1475, 1450, 0.3f);  // T-haara -50 kPa, anturien gain +2 %, -2 %, 0
  TEST_ASSERT_TRUE(p.startGainCapture());
  TEST_ASSERT_TRUE(p.calStatus().busy);
  TEST_ASSERT_EQUAL_STRING(Processor::kMsgGainStart, p.calStatus().msg);
  f.auto_tick = false;
  for (uint32_t i = 0; i + 1 < Processor::kGainCaptureSamples; ++i) f.frame(1425, 1475, 1450);
  p.tick(f.now());
  TEST_ASSERT_TRUE_MESSAGE(p.calStatus().busy, "1999 näytettä ei riitä");
  f.frame(1425, 1475, 1450);
  TEST_ASSERT_TRUE_MESSAGE(p.calStatus().busy, "tulos vasta tickissä (push O(1))");
  p.tick(f.now());
  TEST_ASSERT_FALSE(p.calStatus().busy);
  TEST_ASSERT_EQUAL_STRING(sbx::calResultText(CalResult::Ok), p.calStatus().msg);
  TEST_ASSERT_TRUE(p.calibration().gain_valid);
  TEST_ASSERT_TRUE(p.calibration().zero_valid);
  TEST_ASSERT_TRUE(p.takeCalDirty());
  // Referenssi = kanavien keskiarvo (-50): gain = 50/51, 50/49, 1.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.0f / 51.0f, p.calibration().gain[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.0f / 49.0f, p.calibration().gain[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, p.calibration().gain[2]);
  // Kanavat lukevat nyt samaa sekä alipaineessa että ilmanpaineessa.
  f.auto_tick = true;
  f.hold(1425, 1475, 1450, 0.1f);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(0.01f, -50.0f, p.channel(i).last_kpa);
  f.holdKpa(0.0f, 0.0f, 0.0f, 0.1f);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, p.channel(i).last_kpa);
  TEST_ASSERT_FALSE(p.status().flag_uncal_gain);  // rivi 11
}

void test_gain_capture_uses_nominal_samples() {
  // Vanha vahvistus ei saa vaikuttaa uuteen mittaukseen: keruu on nimellistä
  // kPa:ta. Täydelliset anturit -> uudet gainit 1, vaikka vanhat olivat 1,1/0,9.
  Processor p(g_mem, kRingCap);
  Calibration cal;
  cal.gain[0] = 1.1f;
  cal.gain[1] = 0.9f;
  cal.gain_valid = true;
  p.setCalibration(cal);
  Feed f(p);
  zeroAtAtmosphere(f);
  f.holdKpa(-50.0f, -50.0f, -50.0f, 0.2f);
  TEST_ASSERT_TRUE(p.startGainCapture());
  f.holdKpa(-50.0f, -50.0f, -50.0f, 2.1f);
  TEST_ASSERT_EQUAL_STRING(sbx::calResultText(CalResult::Ok), p.calStatus().msg);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, p.calibration().gain[i]);
}

void test_gain_requires_zero_D11() {
  // computeGain: ilman tämän käynnistyksen nollausta offsetit menisivät gainiin.
  Processor p(g_mem, kRingCap);
  Feed f(p);
  f.holdKpa(-50.0f, -50.0f, -50.0f, 0.2f);
  TEST_ASSERT_TRUE(p.startGainCapture());
  f.holdKpa(-50.0f, -50.0f, -50.0f, 2.1f);
  TEST_ASSERT_FALSE(p.calStatus().busy);
  TEST_ASSERT_EQUAL_STRING(sbx::calResultText(CalResult::NoZero), p.calStatus().msg);
  TEST_ASSERT_FALSE(p.calibration().gain_valid);
  TEST_ASSERT_FALSE(p.takeCalDirty());
}

void test_gain_capture_rejects_unstable_pressure() {
  Processor p(g_mem, kRingCap);
  Feed f(p);
  zeroAtAtmosphere(f);
  TEST_ASSERT_TRUE(p.startGainCapture());
  for (int i = 0; i < 2100; ++i) {
    const uint16_t mv = (i & 1) ? 1400 : 1500;  // sd 2 kPa: pumppu vuotaa
    f.frame(mv, mv, mv);
  }
  TEST_ASSERT_EQUAL_STRING(sbx::calResultText(CalResult::NotStable), p.calStatus().msg);
  TEST_ASSERT_FALSE(p.calibration().gain_valid);
}

void test_gain_second_start_refused() {
  Processor p(g_mem, kRingCap);
  Feed f(p);
  zeroAtAtmosphere(f);
  TEST_ASSERT_TRUE(p.startGainCapture());
  f.holdKpa(-50.0f, -50.0f, -50.0f, 1.0f);
  TEST_ASSERT_FALSE(p.startGainCapture());
  TEST_ASSERT_TRUE(p.calStatus().busy);
  TEST_ASSERT_EQUAL_STRING(Processor::kMsgGainBusy, p.calStatus().msg);
  // Ensimmäinen mittaus jatkuu alkuperäisestä hetkestä (ei aloitettu alusta).
  f.holdKpa(-50.0f, -50.0f, -50.0f, 1.05f);
  TEST_ASSERT_FALSE(p.calStatus().busy);
}

void test_gain_capture_aborted_by_configure() {
  // §4.8: configure keskeyttää käynnissä olevan vahvistusmittauksen.
  Processor p(g_mem, kRingCap);
  Feed f(p);
  zeroAtAtmosphere(f);
  f.hold(1425, 1475, 1450, 0.2f);
  TEST_ASSERT_TRUE(p.startGainCapture());
  f.hold(1425, 1475, 1450, 1.0f);
  const FormPair form[] = {{"tol_green_kpa", "1,2"}};
  char err[sbx::kFormErrLen];
  TEST_ASSERT_TRUE(p.applyConfigForm(form, 1, err, sizeof(err)));
  TEST_ASSERT_FALSE(p.calStatus().busy);
  TEST_ASSERT_EQUAL_STRING(Processor::kMsgGainAborted, p.calStatus().msg);
  f.hold(1425, 1475, 1450, 2.5f);
  TEST_ASSERT_FALSE(p.calibration().gain_valid);
  TEST_ASSERT_EQUAL_STRING(Processor::kMsgGainAborted, p.calStatus().msg);
}

void test_reset_gain_keeps_zero_point() {
  Processor p(g_mem, kRingCap);
  Feed f(p);
  f.hold(2650, 2650, 2650, kSettledS);
  TEST_ASSERT_EQUAL_INT(R(CalResult::Ok), R(p.zeroNow()));
  f.hold(1400, 1475, 1450, 0.2f);
  TEST_ASSERT_TRUE(p.startGainCapture());
  f.hold(1400, 1475, 1450, 2.1f);
  TEST_ASSERT_TRUE(p.calibration().gain_valid);
  TEST_ASSERT_TRUE(p.takeCalDirty());
  p.resetGain();
  TEST_ASSERT_FALSE(p.calibration().gain_valid);
  TEST_ASSERT_TRUE(p.calibration().zero_valid);
  TEST_ASSERT_TRUE(p.takeCalDirty());
  TEST_ASSERT_EQUAL_STRING(Processor::kMsgGainReset, p.calStatus().msg);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_FLOAT(1.0f, p.calibration().gain[i]);
  // Ilmanpaine näkyy yhä nollana (nollapiste säilyi).
  f.hold(2650, 2650, 2650, 0.1f);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, p.channel(i).last_kpa);
  // Toinen poisto ei muuta mitään eikä likaa NVS:ää.
  p.resetGain();
  TEST_ASSERT_FALSE(p.takeCalDirty());
}

// ---------------------------------------------------------------------------
// Master (D7) ja sen lomake
// ---------------------------------------------------------------------------

// D7/D16 (31.10.2026): oletusmaster on SYL 3 = BF50A:n alin kaasutin, joka on
// Hondan synkronoinnin perus (docs/bf50a-huoltotiedot.md: Carburetion Manual
// s. 7-13, luokka A). Processor ei kutsu configurea luodessaan, joten ketju
// types.h -> StatusMachinen konstruktori -> neuvoteksti lukitaan päästä päähän.
// Lopuksi Palauta oletukset (POST /api/config/reset) vie SYL 1:stä SYL 3:een:
// sama tilanne kuin laitteella, jonka NVS:ssä on vanha SYL 1.
void test_default_master_is_syl3_D16() {
  Processor p(g_mem, kRingCap);
  TEST_ASSERT_EQUAL_INT(2, p.config().status.master);
  TEST_ASSERT_EQUAL_INT(2, p.master());
  // SYL 1 imee 4 kPa syvemmin kuin muut: d = mean_SYL3 - mean_SYL1 = +4 -> avaa (D3).
  EngineSim sim;
  SimParams sp;
  sp.sync_err_kpa[0] = -4.0f;
  sim.configure(sp, sbx::SensorParams());
  for (uint32_t n = 1; n <= 6000u; ++n) {
    p.push(sim.next());
    if (n % 50 == 0) p.tick(static_cast<float>(n) / 1000.0f);
  }
  const sbx::StatusResult& r = p.status();
  TEST_ASSERT_TRUE_MESSAGE(r.show_verdict, r.advice);
  TEST_ASSERT_EQUAL_INT(0, r.worst_ch);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.vac_delta_kpa[2]);   // master = nollataso
  TEST_ASSERT_GREATER_THAN_FLOAT(3.5f, r.vac_delta_kpa[0]);
  const char* head = "SYL 1: avaa läppää – imee ";
  const char* tail = " kPa enemmän kuin SYL 3";
  const size_t al = std::strlen(r.advice);
  const size_t tl = std::strlen(tail);
  TEST_ASSERT_TRUE_MESSAGE(std::strncmp(r.advice, head, std::strlen(head)) == 0, r.advice);
  TEST_ASSERT_TRUE_MESSAGE(al > tl && std::strcmp(r.advice + al - tl, tail) == 0, r.advice);
  // Palauta oletukset: tallennettu SYL 1 -> SYL 3.
  TEST_ASSERT_TRUE(p.setMaster(0));
  p.resetConfig();
  TEST_ASSERT_EQUAL_INT(2, p.master());
}

void test_set_master_keeps_dsp_and_restarts_stability_D7() {
  Processor p(g_mem, kRingCap);
  pinSyl1Master(p);
  EngineSim sim;
  sim.configure(SimParams(), sbx::SensorParams());
  uint32_t n = 0;
  auto run = [&](float s) {
    const int k = static_cast<int>(s * 1000.0f + 0.5f);
    for (int i = 0; i < k; ++i) {
      p.push(sim.next());
      if (++n % 50 == 0) p.tick(static_cast<float>(n) / 1000.0f);
    }
  };
  run(5.0f);  // lukitus + stable_hold_s: tuomio näkyy (kalibroimaton -> Near, D11)
  TEST_ASSERT_TRUE(p.channel(0).has_pulse);
  TEST_ASSERT_TRUE(p.status().show_verdict);
  const uint32_t cycles = p.channel(1).cycles;
  TEST_ASSERT_TRUE(p.setMaster(1));
  TEST_ASSERT_EQUAL_INT(1, p.master());
  TEST_ASSERT_EQUAL_INT(1, p.config().status.master);
  TEST_ASSERT_TRUE(p.takeConfigDirty());
  run(0.05f);
  // DSP ei nollautunut: syklilaskuri jatkaa ja pulssi pysyy.
  TEST_ASSERT_TRUE(p.channel(1).has_pulse);
  TEST_ASSERT_GREATER_OR_EQUAL(static_cast<long long>(cycles), static_cast<long long>(p.channel(1).cycles));
  // Uusi nollataso = uusi vakausjakso (rivi 3: vakaa >= 3 s ennen tuomiota).
  TEST_ASSERT_EQUAL_INT(S(SyncState::Unstable), S(p.status().state));
  TEST_ASSERT_FALSE(p.status().show_verdict);
  // Sama master uudelleen ei ole muutos; virheellinen hylätään.
  TEST_ASSERT_TRUE(p.setMaster(1));
  TEST_ASSERT_FALSE(p.takeConfigDirty());
  TEST_ASSERT_FALSE(p.setMaster(3));
  TEST_ASSERT_FALSE(p.setMaster(-1));
  TEST_ASSERT_EQUAL_INT(1, p.master());
  // Vertailu: configure nollaa DSP:n (§4.8), joten syklit alkavat alusta.
  TEST_ASSERT_TRUE(p.configure(p.config()));
  run(0.05f);
  TEST_ASSERT_FALSE(p.channel(1).has_pulse);
  TEST_ASSERT_LESS_THAN(static_cast<long long>(cycles), static_cast<long long>(p.channel(1).cycles));
}

void test_master_form_strict_channel() {
  Processor p(g_mem, kRingCap);
  pinSyl1Master(p);
  char err[sbx::kFormErrLen];
  const FormPair ok2[] = {{"ch", "2"}};
  TEST_ASSERT_TRUE(p.applyMasterForm(ok2, 1, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("", err);
  TEST_ASSERT_EQUAL_INT(2, p.master());
  TEST_ASSERT_TRUE(p.takeConfigDirty());
  // Mock api_master: vain "0", "1" tai "2"; toInt("x") = 0 ei saa valita SYL 1:tä.
  const char* bad[] = {"x", "3", "", "-1", "1.0", " 1", "01"};
  for (const char* v : bad) {
    const FormPair f[] = {{"ch", v}};
    TEST_ASSERT_FALSE_MESSAGE(p.applyMasterForm(f, 1, err, sizeof(err)), v);
    TEST_ASSERT_EQUAL_STRING(Processor::kErrMaster, err);
    TEST_ASSERT_EQUAL_INT(2, p.master());
  }
  TEST_ASSERT_FALSE(p.applyMasterForm(nullptr, 0, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING(Processor::kErrMaster, err);
  const FormPair other[] = {{"master", "1"}};
  TEST_ASSERT_FALSE(p.applyMasterForm(other, 1, err, sizeof(err)));
  TEST_ASSERT_FALSE(p.takeConfigDirty());  // hylätty pyyntö ei ole pysyvä muutos
  // Viimeinen ch ratkaisee (mock: dict(pairs)).
  const FormPair twice[] = {{"ch", "0"}, {"ch", "1"}};
  TEST_ASSERT_TRUE(p.applyMasterForm(twice, 2, err, sizeof(err)));
  TEST_ASSERT_EQUAL_INT(1, p.master());
  TEST_ASSERT_TRUE(p.takeConfigDirty());
}

// ---------------------------------------------------------------------------
// Atominen konfiguraatio (§4.4: POST /api/config, mock api_config_set)
// ---------------------------------------------------------------------------

void test_config_form_applies_all_pairs() {
  Processor p(g_mem, kRingCap);
  char err[sbx::kFormErrLen];
  const FormPair form[] = {{"tol_green_kpa", "1,2"}, {"tol_yellow_kpa", "2.5"}, {"auto_zero", "off"}};
  TEST_ASSERT_TRUE(p.applyConfigForm(form, 3, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("", err);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.2f, p.config().status.tol_green_kpa);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.5f, p.config().status.tol_yellow_kpa);
  TEST_ASSERT_FALSE(p.config().status.auto_zero);
  TEST_ASSERT_TRUE(p.takeConfigDirty());
  TEST_ASSERT_FALSE(p.takeConfigDirty());  // kuittaus
  // Ristiintarkistus vasta kaikkien parien jälkeen: yksittäin tol_green 5
  // olisi > tol_yellow 2,5, mutta samassa pyynnössä nostettu keltainen kelpaa.
  const FormPair both[] = {{"tol_green_kpa", "5"}, {"tol_yellow_kpa", "8"}};
  TEST_ASSERT_TRUE(p.applyConfigForm(both, 2, err, sizeof(err)));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, p.config().status.tol_green_kpa);
}

void test_config_form_is_atomic() {
  Processor p(g_mem, kRingCap);
  char err[sbx::kFormErrLen];
  // Ensimmäinen pari kelpaisi, toinen ei: mitään ei sovelleta.
  const FormPair form[] = {{"tol_green_kpa", "1.0"}, {"avg_cycles", "15,5"}};
  TEST_ASSERT_FALSE(p.applyConfigForm(form, 2, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING(
      "Keskiarvoistettavat syklit (avg_cycles): Virheellinen arvo – anna luku (desimaalierotin "
      "pilkku tai piste)",
      err);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, p.config().status.tol_green_kpa);
  TEST_ASSERT_EQUAL_INT(8, p.config().dsp.avg_cycles);
  TEST_ASSERT_FALSE(p.takeConfigDirty());

  const FormPair unknown[] = {{"tol_green_kpa", "1.0"}, {"foo", "1"}};
  TEST_ASSERT_FALSE(p.applyConfigForm(unknown, 2, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("Tuntematon asetus: foo", err);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, p.config().status.tol_green_kpa);

  const FormPair range[] = {{"off_kpa", "50"}};
  TEST_ASSERT_FALSE(p.applyConfigForm(range, 1, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING(
      "Ei signaalia: kaikki kanavat alle (off_kpa): Arvo sallitun alueen ulkopuolella", err);

  const FormPair cross[] = {{"tol_green_kpa", "5"}};  // >= tol_yellow 3
  TEST_ASSERT_FALSE(p.applyConfigForm(cross, 1, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING(
      "Vihreän rajan (tol_green_kpa) on oltava pienempi kuin keltaisen rajan (tol_yellow_kpa)", err);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, p.config().status.tol_green_kpa);

  TEST_ASSERT_FALSE(p.applyConfigForm(form, 0, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING(Processor::kErrNoPairs, err);
  const FormPair null_value[] = {{"tol_green_kpa", nullptr}};
  TEST_ASSERT_FALSE(p.applyConfigForm(null_value, 1, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING(
      "Synkronoitu, kun ero enintään (tol_green_kpa): Virheellinen arvo – anna luku "
      "(desimaalierotin pilkku tai piste)",
      err);
  const FormPair null_key[] = {{nullptr, "1"}};
  TEST_ASSERT_FALSE(p.applyConfigForm(null_key, 1, err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("Tuntematon asetus: ", err);
  TEST_ASSERT_FALSE(p.takeConfigDirty());
}

void test_config_form_error_texts_fit_and_stay_utf8() {
  // Jokainen hylkäyssyy mahtuu kFormErrLen-puskuriin katkeamatta, ja pieni
  // puskuri katkaisee merkkirajalla (UI näyttää tekstin sellaisenaan).
  const FormPair cases[][2] = {
      {{"rpm_min", "2000"}, {"rpm_max", "1000"}},
      {{"sync_rpm_min", "2000"}, {"sync_rpm_max", "1000"}},
      {{"sync_rpm_max", "7000"}, {"tol_green_kpa", "1"}},
      {{"pulse_timeout_s", "0,3"}, {"tol_green_kpa", "1"}},
      {{"sat_mv", "3200"}, {"tol_green_kpa", "1"}},
      {{"v_zero_mv", "3200"}, {"tol_green_kpa", "1"}},
      {{"hose_off_kpa", "6"}, {"tol_green_kpa", "1"}},
      {{"auto_zero", "kyllä"}, {"tol_green_kpa", "1"}},
      {{"rail_nominal_mv", "99999"}, {"tol_green_kpa", "1"}},
  };
  size_t longest = 0;
  for (const auto& form : cases) {
    Processor p(g_mem, kRingCap);
    char err[sbx::kFormErrLen];
    TEST_ASSERT_FALSE(p.applyConfigForm(form, 2, err, sizeof(err)));
    const size_t len = std::strlen(err);
    TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(len));
    TEST_ASSERT_LESS_THAN(static_cast<long long>(sbx::kFormErrLen) - 1, static_cast<long long>(len));
    TEST_ASSERT_TRUE_MESSAGE(utf8Valid(err), err);
    if (len > longest) longest = len;
    char small[12];
    TEST_ASSERT_FALSE(p.applyConfigForm(form, 2, small, sizeof(small)));
    TEST_ASSERT_TRUE_MESSAGE(utf8Valid(small), small);
    TEST_ASSERT_LESS_THAN(12, static_cast<long long>(std::strlen(small)));
  }
  char msg[64];
  std::snprintf(msg, sizeof(msg), "pisin virheteksti %u tavua", static_cast<unsigned>(longest));
  TEST_MESSAGE(msg);
}

void test_configure_rejects_invalid_config() {
  Processor p(g_mem, kRingCap);
  pinSyl1Master(p);
  Config c;
  c.status.tol_green_kpa = 4.0f;  // > tol_yellow_kpa 3 (D2)
  TEST_ASSERT_FALSE(p.configure(c));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, p.config().status.tol_green_kpa);
  c = Config();
  c.status.master = 7;  // NVS:stä vanhan skeeman arvo
  TEST_ASSERT_FALSE(p.configure(c));
  TEST_ASSERT_EQUAL_INT(0, p.master());
  TEST_ASSERT_FALSE(p.takeConfigDirty());  // configure on lataus, ei käyttäjän muutos
}

void test_conversion_change_invalidates_zero_D11() {
  // Nollaus kuuluu muunnokseen, jolla se mitattiin: v_zero_mv:n tai lähteen
  // vaihdon jälkeen vanha offset ei enää nollaa ilmanpainetta.
  const FormPair keep[] = {{"tol_green_kpa", "1.2"}, {"hyst_frac", "0.25"}};
  const FormPair changes[][1] = {
      {{"v_zero_mv", "2650"}}, {{"mv_per_kpa", "24"}}, {{"rail_comp", "1"}},
      {{"rail_nominal_mv", "3250"}}, {{"source", "1"}}};
  {
    Processor p(g_mem, kRingCap);
    Feed f(p);
    zeroAtAtmosphere(f);
    char err[sbx::kFormErrLen];
    TEST_ASSERT_TRUE(p.applyConfigForm(keep, 2, err, sizeof(err)));
    TEST_ASSERT_TRUE_MESSAGE(p.calibration().zero_valid, "kynnysten muutos ei koske nollaukseen");
  }
  for (const auto& form : changes) {
    Processor p(g_mem, kRingCap);
    Feed f(p);
    zeroAtAtmosphere(f);
    char err[sbx::kFormErrLen];
    TEST_ASSERT_TRUE_MESSAGE(p.applyConfigForm(form, 1, err, sizeof(err)), err);
    TEST_ASSERT_FALSE_MESSAGE(p.calibration().zero_valid, form[0].key);
  }
}

void test_reset_config_and_config_json() {
  Processor p(g_mem, kRingCap);
  char err[sbx::kFormErrLen];
  // master "0": eri kuin oletus SYL 3 (D16), jotta palautus näkyy alla.
  const FormPair form[] = {{"tol_green_kpa", "1.1"}, {"master", "0"}, {"push_rate_hz", "10"}};
  TEST_ASSERT_TRUE(p.applyConfigForm(form, 3, err, sizeof(err)));
  TEST_ASSERT_EQUAL_INT(0, p.master());
  TEST_ASSERT_TRUE(p.takeConfigDirty());
  // GET /api/config = configToJson(config()) (§4.4).
  static char a[sbx::kConfigJsonSize];
  static char b[sbx::kConfigJsonSize];
  const size_t na = p.configJson(a, sizeof(a));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(na));
  TEST_ASSERT_EQUAL_size_t(sbx::configToJson(p.config(), b, sizeof(b)), na);
  // strcmp eikä EQUAL_STRING: 6 kt:n puskuri laukaisisi shimin -Wformat-truncationin.
  TEST_ASSERT_TRUE(std::strcmp(a, b) == 0);
  TEST_ASSERT_NOT_NULL(std::strstr(a, "\"key\":\"tol_green_kpa\""));
  TEST_ASSERT_NOT_NULL(std::strstr(a, "\"value\":1.1}"));
  // POST /api/config/reset: oletukset, master SYL 3 (D7/D16), pysyvä muutos.
  p.resetConfig();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, p.config().status.tol_green_kpa);
  TEST_ASSERT_EQUAL_INT(2, p.master());
  TEST_ASSERT_EQUAL_INT(15, p.config().push_rate_hz);
  TEST_ASSERT_TRUE(p.takeConfigDirty());
}

// ---------------------------------------------------------------------------
// Aaltojono (§5 wave.s: 16 näytteen lohkokeskiarvot, 62,5 Hz, enintään 16 riviä)
// ---------------------------------------------------------------------------

void test_wave_block_means_oldest_first() {
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 62.5f, Processor::kWaveFsHz);
  Processor p(g_mem, kRingCap);
  Feed f(p);
  // Lohko k: 15 näytettä -k kPa ja yksi -k-16: keskiarvo -k-1 (oikea lohkoraja).
  for (int k = 0; k < 5; ++k) {
    for (int i = 0; i < 15; ++i) f.holdKpa(-static_cast<float>(k), 0.0f, 1.0f, 0.001f);
    f.holdKpa(-static_cast<float>(k) - 16.0f, 0.0f, 1.0f, 0.001f);
  }
  f.holdKpa(-40.0f, -40.0f, -40.0f, 0.008f);  // puolikas lohko ei vielä näy
  float rows[Processor::kWaveRows][kChannels];
  const int n = p.takeWave(rows, Processor::kWaveRows);
  TEST_ASSERT_EQUAL_INT(5, n);
  for (int k = 0; k < n; ++k) {
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -static_cast<float>(k) - 1.0f, rows[k][0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, rows[k][1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, rows[k][2]);
  }
  TEST_ASSERT_EQUAL_INT(0, p.takeWave(rows, Processor::kWaveRows));  // tyhjennetty
  f.holdKpa(-40.0f, -40.0f, -40.0f, 0.008f);  // lohko täyttyy
  TEST_ASSERT_EQUAL_INT(1, p.takeWave(rows, Processor::kWaveRows));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -40.0f, rows[0][0]);
}

void test_wave_overflow_keeps_newest_and_partial_take() {
  Processor p(g_mem, kRingCap);
  Feed f(p);
  for (int k = 0; k < 40; ++k) f.holdKpa(-static_cast<float>(k), 0.0f, 0.0f, 0.016f);
  float rows[Processor::kWaveRows][kChannels];
  // Haku 3 riviä: vanhimmat säilyneet (24, 25, 26), loput jäävät jonoon.
  TEST_ASSERT_EQUAL_INT(3, p.takeWave(rows, 3));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -24.0f, rows[0][0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -26.0f, rows[2][0]);
  TEST_ASSERT_EQUAL_INT(13, p.takeWave(rows, Processor::kWaveRows));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -27.0f, rows[0][0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -39.0f, rows[12][0]);
  TEST_ASSERT_EQUAL_INT(0, p.takeWave(nullptr, 4));
  TEST_ASSERT_EQUAL_INT(0, p.takeWave(rows, 0));
}

// ---------------------------------------------------------------------------
// Rengas ja CSV (S5b): käsittely jatkuu latauksen aikana
// ---------------------------------------------------------------------------

void test_ring_and_csv_during_processing() {
  Processor p(g_mem, kRingCap);
  Feed f(p);
  // 1,1 s (WP-9: nollaus vaatii 1 s vakaata tasoa; ennen 0,5 s -> 600 näytettä).
  f.hold(2650, 2675, 2700, 1.1f);
  TEST_ASSERT_EQUAL_INT(R(CalResult::Ok), R(p.zeroNow()));
  f.hold(1450, 1475, 1500, 0.1f);
  sbx::RawRing& ring = p.ring();
  TEST_ASSERT_EQUAL_size_t(1200, ring.size());
  TEST_ASSERT_EQUAL_UINT32(1199, ring.newestSeq());
  // GET /api/raw.csv: jäädytys, mutta DSP ja tila jatkavat (push ei pysähdy).
  ring.freeze();
  CsvWriter csv(ring, p.config(), p.calibration());
  f.hold(1400, 1400, 1400, 0.2f);
  TEST_ASSERT_EQUAL_size_t(1200, ring.size());
  TEST_ASSERT_EQUAL_INT(1400, p.channel(0).last_mv);
  static char out[64 * 1024];
  size_t len = 0;
  for (;;) {
    const size_t m = csv.fill(out + len, 700);  // palakoko kuin chunked-vastaus
    if (m == 0) break;
    len += m;
    TEST_ASSERT_LESS_THAN(static_cast<long long>(sizeof(out)), static_cast<long long>(len));
  }
  out[len] = '\0';
  ring.unfreeze();
  TEST_ASSERT_FALSE(csv.aborted());
  TEST_ASSERT_NOT_NULL(std::strstr(out, "# samples=1200\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "# zero_valid=1\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "# offset_kpa=2,1,0\n"));
  // Ensimmäinen rivi ennen nollausta mitattu, kalibroitu nykyisellä nollauksella.
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\n0,0,2650,2675,2700,0.000,0.000,0.000\n"));
  // Viimeinen jäädytystä edeltävä näyte: -50 kPa nimellinen -> -48 / -48 / -48.
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\n1199,1199,1450,1475,1500,-48.000,-48.000,-48.000\n"));
  // Vapautuksen jälkeen rengas jatkaa; jäädytyksen pudotukset eivät ole aukkoja.
  f.hold(1400, 1400, 1400, 0.01f);
  TEST_ASSERT_EQUAL_size_t(1210, ring.size());
  TEST_ASSERT_EQUAL_UINT32(0, ring.gaps());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_conversion_nominal_then_calibration);
  RUN_TEST(test_conversion_rail_compensation_R6);
  RUN_TEST(test_channel_and_status_are_tick_time_values);
  RUN_TEST(test_set_calibration_forces_zero_invalid_D11);
  RUN_TEST(test_set_calibration_sanitizes_broken_gain);
  RUN_TEST(test_zero_now_at_atmosphere);
  RUN_TEST(test_zero_now_keeps_gain);
  RUN_TEST(test_zero_now_rejected_without_atmosphere);
  RUN_TEST(test_zero_now_rejects_bleeding_vacuum);
  RUN_TEST(test_zero_now_needs_full_settle_window);
  RUN_TEST(test_auto_zero_row1_once_per_no_signal_period);
  RUN_TEST(test_auto_zero_disabled);
  RUN_TEST(test_gain_capture_two_seconds_then_tick);
  RUN_TEST(test_gain_capture_uses_nominal_samples);
  RUN_TEST(test_gain_requires_zero_D11);
  RUN_TEST(test_gain_capture_rejects_unstable_pressure);
  RUN_TEST(test_gain_second_start_refused);
  RUN_TEST(test_gain_capture_aborted_by_configure);
  RUN_TEST(test_reset_gain_keeps_zero_point);
  RUN_TEST(test_default_master_is_syl3_D16);
  RUN_TEST(test_set_master_keeps_dsp_and_restarts_stability_D7);
  RUN_TEST(test_master_form_strict_channel);
  RUN_TEST(test_config_form_applies_all_pairs);
  RUN_TEST(test_config_form_is_atomic);
  RUN_TEST(test_config_form_error_texts_fit_and_stay_utf8);
  RUN_TEST(test_configure_rejects_invalid_config);
  RUN_TEST(test_conversion_change_invalidates_zero_D11);
  RUN_TEST(test_reset_config_and_config_json);
  RUN_TEST(test_wave_block_means_oldest_first);
  RUN_TEST(test_wave_overflow_keeps_newest_and_partial_take);
  RUN_TEST(test_ring_and_csv_during_processing);
  return UNITY_END();
}
