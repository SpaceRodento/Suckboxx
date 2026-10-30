// WP-4: status.h -testit (käytösmatriisi SUUNNITTELU.md §8, D10-D12).
//
// Totuusarvot johdetaan fysiikasta: suljetumpi kaasuläppä -> sylinteri imee
// syvemmän alipaineen (negatiivisempi syklin keskiarvo, D1) -> d =
// mean_master - mean_ch > 0 -> "avaa". Letku irti = ilmanpaine (0 kPa) ilman
// pulssia; DSP:n taso on EMA (tau 0,5 s), joten irrotetun letkun taso laskee
// eksponentiaalisesti eikä hyppää nollaan.
//
// Ajastimet ajetaan 16 Hz:n askelilla (dt = 1/16 s): binäärinen murtoluku
// tekee ajastinrajoista tarkkoja floatilla, joten rajatestit (>= 3,0 s) eivät
// riipu pyöristyksestä. Epätasainen dt ja 10/50 Hz testataan erikseen.

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/status.h"

using sbx::ChannelState;
using sbx::RpmEstimate;
using sbx::StatusInput;
using sbx::StatusMachine;
using sbx::StatusParams;
using sbx::StatusResult;
using sbx::SyncState;
using sbx::kAdviceLen;
using sbx::kChannels;
using sbx::kHeadlineLen;

namespace {

constexpr float kDt = 0.0625f;

uint16_t mvOf(float kpa) {
  // GN-anturi ADC-pinnissä: 2700 mV @ 0 kPa, 25 mV/kPa (types.h SensorParams).
  float mv = 2700.0f + 25.0f * kpa;
  if (mv < 0.0f) mv = 0.0f;
  if (mv > 3300.0f) mv = 3300.0f;
  return static_cast<uint16_t>(mv + 0.5f);
}

// Käyvä moottori: kaikki kanavat pulssivat annetuilla syklin keskiarvoilla.
StatusInput running(float m0, float m1, float m2, float rpm = 850.0f, float cv = 0.01f) {
  StatusInput in;
  const float means[kChannels] = {m0, m1, m2};
  for (int i = 0; i < kChannels; ++i) {
    ChannelState& c = in.ch[i];
    c.has_pulse = true;
    c.cycles = 1000;
    c.mean_kpa = means[i];
    c.level_kpa = means[i];
    c.min_kpa = means[i] - 12.0f;
    c.p2p_kpa = 20.0f;
    c.rpm = rpm;
    c.last_kpa = means[i];
    c.last_mv = mvOf(means[i]);
  }
  in.rpm.valid = true;
  in.rpm.rpm = rpm;
  in.rpm.cv = cv;
  in.rpm.sd_rpm = cv * rpm;
  in.zero_valid = true;
  in.gain_valid = true;
  return in;
}

StatusInput idleSynced() { return running(-55.0f, -55.4f, -54.8f); }

// Moottori seis (tai kaikki letkut irti, D10): ei pulssia, tasot ~0.
StatusInput engineOff(float l0 = 0.1f, float l1 = -0.2f, float l2 = 0.15f) {
  StatusInput in;
  const float lv[kChannels] = {l0, l1, l2};
  for (int i = 0; i < kChannels; ++i) {
    in.ch[i].has_pulse = false;
    in.ch[i].level_kpa = lv[i];
    in.ch[i].last_kpa = lv[i];
    in.ch[i].last_mv = mvOf(lv[i]);
    in.ch[i].mean_kpa = -55.0f;   // vanha arvo jää DSP:ssä talteen (ei voimassa)
  }
  in.zero_valid = true;
  in.gain_valid = true;
  return in;
}

// Kanava ilman pulssia annetulla tasolla (letku irti / tukossa).
void dropPulse(StatusInput& in, int ch, float level) {
  in.ch[ch].has_pulse = false;
  in.ch[ch].level_kpa = level;
  in.ch[ch].rpm = 0.0f;
  in.ch[ch].last_mv = mvOf(level);
}

void sensorFault(StatusInput& in, int ch, uint16_t mv) {
  in.ch[ch].has_pulse = false;
  in.ch[ch].sensor_fault = true;
  in.ch[ch].last_mv = mv;
  in.ch[ch].level_kpa = (static_cast<float>(mv) - 2700.0f) / 25.0f;
}

struct Rig {
  StatusMachine sm;
  float t = 0.0f;
  int zero_requests = 0;
  StatusResult last;

  void begin(const StatusParams& p = StatusParams()) {
    sm.configure(p);
    t = 0.0f;
    zero_requests = 0;
  }
  StatusResult step(StatusInput in, float dt = kDt) {
    in.now_s = t;
    last = sm.update(in);
    if (last.request_auto_zero) ++zero_requests;
    t += dt;
    return last;
  }
  StatusResult run(const StatusInput& in, float seconds, float dt = kDt) {
    const int n = static_cast<int>(seconds / dt + 0.5f);
    for (int i = 0; i < n; ++i) step(in, dt);
    return last;
  }
};

Rig g;

bool utf8Valid(const char* s) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  while (*p) {
    int n = 0;
    if (*p < 0x80u) n = 0;
    else if ((*p & 0xE0u) == 0xC0u) n = 1;
    else if ((*p & 0xF0u) == 0xE0u) n = 2;
    else if ((*p & 0xF8u) == 0xF0u) n = 3;
    else return false;
    ++p;
    for (int k = 0; k < n; ++k, ++p) {
      if ((*p & 0xC0u) != 0x80u) return false;
    }
  }
  return true;
}

// Teksti mahtuu puskuriin katkeamatta: NUL löytyy ja tilaa jää vähintään yksi
// tavu (snprintf olisi katkaissut täsmälleen täyteen). Lisäksi kelvollista UTF-8:aa.
void assertTextSane(const StatusResult& r) {
  const size_t hl = strnlen(r.headline, sizeof(r.headline));
  const size_t al = strnlen(r.advice, sizeof(r.advice));
  TEST_ASSERT_LESS_THAN(static_cast<long long>(kHeadlineLen) - 1, static_cast<long long>(hl));
  TEST_ASSERT_LESS_THAN(static_cast<long long>(kAdviceLen) - 1, static_cast<long long>(al));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(hl));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(al));
  TEST_ASSERT_TRUE_MESSAGE(utf8Valid(r.headline), r.headline);
  TEST_ASSERT_TRUE_MESSAGE(utf8Valid(r.advice), r.advice);
}

void assertNoVerdict(const StatusResult& r) {
  TEST_ASSERT_FALSE(r.show_bars);
  TEST_ASSERT_FALSE(r.show_verdict);
}

// Ajaa vakauden yli (stable_hold_s + marginaali).
StatusResult settle(const StatusInput& in) { return g.run(in, 4.0f); }

}  // namespace

void setUp() { g.begin(); }
void tearDown() {}

// ---------------------------------------------------------------------------
// Rivit 1 + 8 (D10): NoSignal ja automaattinen nollaus
// ---------------------------------------------------------------------------

void test_row1_8_no_pulses_is_no_signal() {
  StatusResult r = g.step(engineOff());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::NoSignal), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Ei alipainesignaalia", r.headline);
  TEST_ASSERT_EQUAL_STRING("Moottori sammuksissa tai letkut irti", r.advice);
  assertNoVerdict(r);
  TEST_ASSERT_EQUAL_INT(-1, r.fault_ch);
  TEST_ASSERT_EQUAL_INT(-1, r.worst_ch);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.spread_kpa);
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_FLOAT(0.0f, r.vac_delta_kpa[i]);
  TEST_ASSERT_FALSE(r.flag_drift);
  TEST_ASSERT_EQUAL_STRING("no_signal", sbx::syncStateKey(r.state));
  assertTextSane(r);
}

void test_row8_all_hoses_off_engine_running_is_no_signal_not_hose_fault() {
  // D10: kaikki letkut irti = ilmanpaine kaikissa, ei pulssia missään. Rivin 6
  // ehto "muut pulssivat" ei täyty, joten tämä ei ole letkuvika. Vanhentunut
  // rpm-arvio (valid) ei saa muuttaa tätä.
  StatusInput in = engineOff(0.0f, 0.0f, 0.0f);
  in.rpm.valid = true;
  in.rpm.rpm = 850.0f;
  in.rpm.cv = 0.01f;
  StatusResult r = g.run(in, 10.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::NoSignal), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(-1, r.fault_ch);
}

void test_row1_auto_zero_fires_once_after_off_hold() {
  const StatusInput off = engineOff();
  // off_hold_s = 3,0 s = 48 askelta. Askeleet 0..47 (t < 3,0) eivät saa laukaista.
  for (int k = 0; k < 48; ++k) {
    StatusResult r = g.step(off);
    TEST_ASSERT_FALSE_MESSAGE(r.request_auto_zero, "nollaus ennen off_hold_s:ää");
  }
  StatusResult r = g.step(off);   // t = 3,0 s
  TEST_ASSERT_TRUE(r.request_auto_zero);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::NoSignal), static_cast<int>(r.state));
  // Reunaliipattu: sama jakso ei pyydä uudelleen, vaikka se jatkuu pitkään.
  g.run(off, 30.0f);
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
}

void test_row1_auto_zero_also_without_previous_zero() {
  // Käynnistys moottori sammuksissa ilman nollausta: juuri tämä tilanne
  // nollauksen kuuluu hoitaa (D11).
  StatusInput off = engineOff(1.8f, -1.2f, 0.6f);   // R4: ±2 kPa offset ennen nollausta
  off.zero_valid = false;
  g.run(off, 3.5f);
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
}

void test_row1_auto_zero_rearms_after_pulses_return() {
  g.run(engineOff(), 5.0f);
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
  g.run(idleSynced(), 2.0f);   // moottori käynnistettiin
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
  // Toinen sammutus: uusi pyyntö tasan off_hold_s:n kuluttua.
  const float t0 = g.t;
  float t_fire = -1.0f;
  while (g.t < t0 + 5.0f && t_fire < 0.0f) {
    const float now = g.t;
    if (g.step(engineOff()).request_auto_zero) t_fire = now;
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, t0 + 3.0f, t_fire);
  TEST_ASSERT_EQUAL_INT(2, g.zero_requests);
}

void test_row1_auto_zero_not_rearmed_by_fault_without_pulses() {
  g.run(engineOff(), 5.0f);
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
  StatusInput f = engineOff();
  sensorFault(f, 2, 12);
  StatusResult r = g.run(f, 1.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultSensor), static_cast<int>(r.state));
  g.run(engineOff(), 10.0f);
  // Pulssit eivät palanneet välissä -> ei uutta pyyntöä.
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
}

void test_row1_no_auto_zero_while_vacuum_bleeds_down() {
  // Moottori juuri sammui, alipaine loukussa: tasot -20 kPa. NoSignal kyllä,
  // nollaus ei - se kirjaisi 20 kPa offsetiksi.
  StatusResult r = g.run(engineOff(-20.0f, -19.0f, -21.0f), 10.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::NoSignal), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(0, g.zero_requests);
  // Taso laskee lineaarisesti -4,5 -> -0,5 kPa 4 sekunnissa: kaikki alle
  // off_kpa:n, mutta liikkuu yhä -> ei nollausta liikkeen aikana.
  for (int k = 0; k <= 64; ++k) {
    const float l = -4.5f + 4.0f * static_cast<float>(k) / 64.0f;
    g.step(engineOff(l, l, l));
  }
  TEST_ASSERT_EQUAL_INT(0, g.zero_requests);
  // Taso asettui -0,5:een (viimeinen rampin näyte hetkellä t_last). Kaistan
  // (0,3 kPa) viimeinen uudelleenaloitus oli enintään 0,3 kPa / (1 kPa/s) =
  // 0,3 s ennen sitä, joten pyyntö tulee aikaisintaan 2,7 s ja viimeistään
  // 3,0 s + yksi askel t_lastin jälkeen.
  const float t_last = g.t - kDt;
  float t_fire = -1.0f;
  for (int k = 0; k < 80 && t_fire < 0.0f; ++k) {
    StatusResult rr = g.step(engineOff(-0.5f, -0.5f, -0.5f));
    if (rr.request_auto_zero) t_fire = g.t - kDt;
  }
  TEST_ASSERT_TRUE(t_fire > 0.0f);
  TEST_ASSERT_GREATER_THAN_FLOAT(2.7f - 0.001f, t_fire - t_last);
  TEST_ASSERT_LESS_THAN_FLOAT(3.0f + kDt + 0.001f, t_fire - t_last);
}

void test_row1_level_just_outside_off_kpa_blocks_zero() {
  // off_kpa = 5: yksikin kanava 5,1 kPa:ssa estää nollauksen.
  g.run(engineOff(0.0f, -5.1f, 0.0f), 10.0f);
  TEST_ASSERT_EQUAL_INT(0, g.zero_requests);
  g.run(engineOff(0.0f, -4.9f, 0.0f), 3.5f);
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
}

void test_row1_auto_zero_disabled() {
  StatusParams p;
  p.auto_zero = false;
  g.begin(p);
  StatusResult r = g.run(engineOff(), 20.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::NoSignal), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(0, g.zero_requests);
}

// ---------------------------------------------------------------------------
// Rivi 2 ja vakausajastin (D12)
// ---------------------------------------------------------------------------

void test_row2_rpm_not_yet_valid_is_unstable() {
  StatusInput in = idleSynced();
  in.rpm.valid = false;
  StatusResult r = g.run(in, 10.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Odota, käynti tasaantuu", r.headline);
  TEST_ASSERT_EQUAL_STRING("Kierroslukua ei vielä tunnistettu", r.advice);
  assertNoVerdict(r);
  TEST_ASSERT_EQUAL_INT(-1, r.fault_ch);
  assertTextSane(r);
}

void test_row2_high_cv_is_unstable_and_limit_is_inclusive() {
  StatusInput in = running(-55.0f, -55.4f, -54.8f, 850.0f, 0.16f);
  StatusResult r = g.run(in, 10.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Käynti epätasainen: hajonta 16,0 % (raja 15,0 %)", r.advice);
  assertNoVerdict(r);
  // Rajalla (cv == unstable_cv) ei ole epävakaa (rivi 2: "yli 15 %").
  g.begin();
  r = settle(running(-55.0f, -55.4f, -54.8f, 850.0f, 0.15f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
}

void test_row2_nan_cv_or_rpm_is_unstable() {
  StatusInput in = idleSynced();
  in.rpm.cv = NAN;
  StatusResult r = g.run(in, 10.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  in = idleSynced();
  in.rpm.rpm = NAN;
  r = g.run(in, 10.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  assertTextSane(r);
}

void test_stability_timer_gates_verdict() {
  const StatusInput in = idleSynced();
  // stable_hold_s = 3,0 s = 48 askelta: askeleet 0..47 odottavat.
  for (int k = 0; k < 48; ++k) {
    StatusResult r = g.step(in);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
    assertNoVerdict(r);
    if (k == 16) {
      TEST_ASSERT_EQUAL_STRING("Vakautta odotetaan: 1,0 / 3,0 s", r.advice);
    }
    // Erot lasketaan jo odotuksen aikana (UI voi näyttää ne harmaana).
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.4f, r.vac_delta_kpa[1]);
  }
  StatusResult r = g.step(in);   // t = 3,0 s
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  TEST_ASSERT_TRUE(r.show_bars);
  TEST_ASSERT_TRUE(r.show_verdict);

  // Yksikin epävakaa päivitys aloittaa vakausjakson alusta.
  g.step(running(-55.0f, -55.4f, -54.8f, 850.0f, 0.3f));
  for (int k = 0; k < 48; ++k) {
    r = g.step(in);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  }
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
}

void test_stable_hold_zero_gives_immediate_verdict() {
  StatusParams p;
  p.stable_hold_s = 0.0f;
  g.begin(p);
  StatusResult r = g.step(idleSynced());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
}

// ---------------------------------------------------------------------------
// D12-ikkuna ja rivi 9
// ---------------------------------------------------------------------------

void test_d12_low_edge() {
  StatusResult r = g.run(running(-55.0f, -55.4f, -54.8f, 499.9f), 10.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Kierrosluku liian matala: 499 rpm (raja 500)", r.advice);
  assertNoVerdict(r);
  g.begin();
  r = settle(running(-55.0f, -55.4f, -54.8f, 500.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
}

void test_d12_high_edge_and_row9() {
  StatusResult r = settle(running(-55.0f, -55.4f, -54.8f, 3000.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  // Heti yli rajan: RpmTooHigh ilman vakausodotusta, arvio piiloon.
  r = g.step(running(-55.0f, -55.4f, -54.8f, 3000.2f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::RpmTooHigh), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Säädä joutokäynnillä", r.headline);
  TEST_ASSERT_EQUAL_STRING("Kierrosluku 3001 rpm (raja 3000) – laske kaasua", r.advice);
  assertNoVerdict(r);
  TEST_ASSERT_EQUAL_INT(-1, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("rpm_too_high", sbx::syncStateKey(r.state));
  assertTextSane(r);
  // Takaisin ikkunaan: vakausjakso alkaa alusta.
  r = g.step(running(-55.0f, -55.4f, -54.8f, 2900.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  r = settle(running(-55.0f, -55.4f, -54.8f, 2900.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
}

void test_row9_from_cold_start() {
  // Kaasu auki heti: RpmTooHigh ensimmäisestä vakaasta päivityksestä.
  StatusResult r = g.step(running(-30.0f, -31.0f, -29.0f, 4500.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::RpmTooHigh), static_cast<int>(r.state));
}

// ---------------------------------------------------------------------------
// Rivit 3 / 4a / 4b (D2)
// ---------------------------------------------------------------------------

void test_row3_synced() {
  StatusResult r = settle(running(-55.0f, -55.8f, -54.6f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Synkronoitu", r.headline);
  TEST_ASSERT_EQUAL_STRING("Valmis: ero 1,2 kPa (raja 1,5 kPa)", r.advice);
  TEST_ASSERT_TRUE(r.show_bars);
  TEST_ASSERT_TRUE(r.show_verdict);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.vac_delta_kpa[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8f, r.vac_delta_kpa[1]);    // SYL 2 imee enemmän
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.4f, r.vac_delta_kpa[2]);   // SYL 3 imee vähemmän
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.2f, r.spread_kpa);
  TEST_ASSERT_EQUAL_INT(1, r.worst_ch);
  TEST_ASSERT_FALSE(r.flag_uncal_zero);
  TEST_ASSERT_FALSE(r.flag_uncal_gain);
  TEST_ASSERT_FALSE(r.request_auto_zero);
  assertTextSane(r);
}

void test_row3_4a_green_edge_is_inclusive() {
  // spread täsmälleen 1,5 (binäärisesti tarkka) -> vihreä.
  StatusResult r = settle(running(-55.0f, -56.5f, -55.0f));
  TEST_ASSERT_EQUAL_FLOAT(1.5f, r.spread_kpa);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  r = g.step(running(-55.0f, -56.5625f, -55.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(r.state));
}

void test_row4a_near_with_open_advice() {
  // SYL 3:n läppä suljetumpi -> imee 2,1 kPa syvemmän alipaineen.
  StatusResult r = settle(running(-55.0f, -55.3f, -57.1f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Lähellä", r.headline);
  TEST_ASSERT_EQUAL_STRING("SYL 3: avaa läppää – imee 2,1 kPa enemmän kuin SYL 1", r.advice);
  TEST_ASSERT_EQUAL_INT(2, r.worst_ch);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.1f, r.spread_kpa);
  TEST_ASSERT_TRUE(r.show_bars);
  TEST_ASSERT_TRUE(r.show_verdict);
  TEST_ASSERT_EQUAL_STRING("near", sbx::syncStateKey(r.state));
  assertTextSane(r);
}

void test_row4a_near_with_close_advice() {
  // SYL 2:n läppä avoimempi -> imee 1,8 kPa vähemmän -> sulje.
  StatusResult r = settle(running(-55.0f, -53.2f, -55.5f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("SYL 2: sulje läppää – imee 1,8 kPa vähemmän kuin SYL 1", r.advice);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.8f, r.vac_delta_kpa[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.3f, r.spread_kpa);   // 0,5 - (-1,8)
}

void test_row4b_out_of_sync_and_yellow_edge() {
  StatusResult r = settle(running(-55.0f, -51.0f, -56.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::OutOfSync), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Ei synkronissa", r.headline);
  TEST_ASSERT_EQUAL_STRING("SYL 2: sulje läppää – imee 4,0 kPa vähemmän kuin SYL 1", r.advice);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, r.spread_kpa);
  TEST_ASSERT_TRUE(r.show_bars);
  TEST_ASSERT_TRUE(r.show_verdict);
  TEST_ASSERT_EQUAL_STRING("out_of_sync", sbx::syncStateKey(r.state));
  // spread täsmälleen 3,0 -> keltainen; 3,0625 -> punainen.
  r = g.step(running(-55.0f, -58.0f, -55.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(r.state));
  r = g.step(running(-55.0f, -58.0625f, -55.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::OutOfSync), static_cast<int>(r.state));
}

void test_spread_uses_both_sides_of_master() {
  // Kumpikaan kanava ei yksin ylitä 1,5:tä, mutta ne ovat eri puolilla
  // masteria: spread = 1,2 - (-1,2) = 2,4 -> ei vihreä (D2: max(Δ) - min(Δ)).
  StatusResult r = settle(running(-55.0f, -56.2f, -53.8f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.4f, r.spread_kpa);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(r.state));
}

void test_advice_direction_follows_sign() {
  // Sama poikkeama eri suuntiin: suljetumpi läppä (syvempi alipaine) -> avaa.
  StatusResult r = settle(running(-55.0f, -55.0f, -59.0f));
  TEST_ASSERT_TRUE(r.vac_delta_kpa[2] > 0.0f);
  TEST_ASSERT_EQUAL_STRING("SYL 3: avaa läppää – imee 4,0 kPa enemmän kuin SYL 1", r.advice);
  r = g.step(running(-55.0f, -55.0f, -51.0f));
  TEST_ASSERT_TRUE(r.vac_delta_kpa[2] < 0.0f);
  TEST_ASSERT_EQUAL_STRING("SYL 3: sulje läppää – imee 4,0 kPa vähemmän kuin SYL 1", r.advice);
}

void test_worst_channel_tie_and_master_excluded() {
  StatusParams p;
  p.master = 2;
  g.begin(p);
  // d0 = -55 - (-57) = +2, d1 = -55 - (-53) = -2: tasapeli -> pienempi indeksi.
  StatusResult r = settle(running(-57.0f, -53.0f, -55.0f));
  TEST_ASSERT_EQUAL_INT(0, r.worst_ch);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.vac_delta_kpa[2]);
  TEST_ASSERT_EQUAL_STRING("SYL 1: avaa läppää – imee 2,0 kPa enemmän kuin SYL 3", r.advice);
  // Täydellinen synkronointi: worst on silti määritelty ei-master-kanava.
  r = g.step(running(-55.0f, -55.0f, -55.0f));
  TEST_ASSERT_EQUAL_INT(0, r.worst_ch);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.spread_kpa);
}

void test_master_selection_changes_deltas_and_advice() {
  // Sama moottori, kolme masteria. spread on moottorin ominaisuus eikä saa
  // riippua viitteestä; d:t ja ohje riippuvat.
  const StatusInput in = running(-55.0f, -56.0f, -53.0f);
  const char* advice[3] = {
      "SYL 3: sulje läppää – imee 2,0 kPa vähemmän kuin SYL 1",
      "SYL 3: sulje läppää – imee 3,0 kPa vähemmän kuin SYL 2",
      "SYL 2: avaa läppää – imee 3,0 kPa enemmän kuin SYL 3",
  };
  const float d[3][3] = {{0.0f, 1.0f, -2.0f}, {-1.0f, 0.0f, -3.0f}, {2.0f, 3.0f, 0.0f}};
  const int worst[3] = {2, 2, 1};
  for (int m = 0; m < 3; ++m) {
    StatusParams p;
    p.master = static_cast<uint8_t>(m);
    g.begin(p);
    StatusResult r = settle(in);
    TEST_ASSERT_EQUAL_INT(m, g.sm.master());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(r.state));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, r.spread_kpa);
    for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_FLOAT(d[m][i], r.vac_delta_kpa[i]);
    TEST_ASSERT_EQUAL_INT(worst[m], r.worst_ch);
    TEST_ASSERT_EQUAL_STRING(advice[m], r.advice);
  }
}

void test_invalid_master_index_falls_back_to_syl1() {
  StatusParams p;
  p.master = 7;
  g.begin(p);
  TEST_ASSERT_EQUAL_INT(0, g.sm.master());
  StatusResult r = settle(running(-55.0f, -55.3f, -57.1f));
  TEST_ASSERT_EQUAL_STRING("SYL 3: avaa läppää – imee 2,1 kPa enemmän kuin SYL 1", r.advice);
}

// ---------------------------------------------------------------------------
// Rivi 5: kierrosluvun ajautuminen
// ---------------------------------------------------------------------------

void test_row5_drift_keeps_bars_and_verdict() {
  StatusResult r = settle(idleSynced());
  TEST_ASSERT_FALSE(r.flag_drift);
  // Ruuvia käännetään: rpm nousee 40 rpm/s -> 80 rpm / 2 s > 50.
  const float t0 = g.t;
  int flagged = 0, steps = 0;
  while (g.t < t0 + 5.0f) {
    const float rpm = 850.0f + 40.0f * (g.t - t0);
    r = g.step(running(-55.0f, -55.4f, -54.8f, rpm));
    if (g.t - kDt >= t0 + 2.0f + 0.1f) {
      ++steps;
      if (r.flag_drift) ++flagged;
      // Ajautuminen ei yksin tee epävakaaksi: Δ-luvut näkyvät (D3).
      TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
      TEST_ASSERT_TRUE(r.show_bars);
      TEST_ASSERT_TRUE(r.show_verdict);
    }
  }
  TEST_ASSERT_GREATER_THAN(10, steps);
  TEST_ASSERT_EQUAL_INT(steps, flagged);
  // rpm asettuu: 2 s + harvennusväli myöhemmin lippu laskee.
  const float rpm_end = 850.0f + 40.0f * 5.0f;
  r = g.run(running(-55.0f, -55.4f, -54.8f, rpm_end), 2.25f);
  TEST_ASSERT_FALSE(r.flag_drift);
}

void test_row5_slow_change_is_not_drift() {
  settle(idleSynced());
  const float t0 = g.t;
  while (g.t < t0 + 8.0f) {
    const float rpm = 850.0f + 20.0f * (g.t - t0);   // 40 rpm / 2 s < 50
    StatusResult r = g.step(running(-55.0f, -55.4f, -54.8f, rpm));
    TEST_ASSERT_FALSE(r.flag_drift);
  }
}

// Deterministinen LCG epätasaiselle dt:lle.
uint32_t g_lcg = 1u;
float lcgUnit() {
  g_lcg = g_lcg * 1664525u + 1013904223u;
  return static_cast<float>(g_lcg >> 8) / 16777216.0f;
}

// Ajaa rpm-rampin annetulla dt-generaattorilla ja palauttaa (liputetut, arvioidut)
// päivitykset, kun ikkuna + marginaali on kulunut.
void driftRun(float slope, float dt_min, float dt_max, float noise, int& flagged, int& counted) {
  g.begin();
  flagged = 0;
  counted = 0;
  float t = 0.0f;
  while (t < 12.0f) {
    const float dt = dt_min + (dt_max - dt_min) * lcgUnit();
    const float rpm = 900.0f + slope * t + noise * (2.0f * lcgUnit() - 1.0f);
    StatusInput in = running(-55.0f, -55.4f, -54.8f, rpm);
    in.now_s = t;
    StatusResult r = g.sm.update(in);
    if (t > 2.0f + dt_max) {
      ++counted;
      if (r.flag_drift) ++flagged;
    }
    t += dt;
  }
}

void test_row5_drift_irregular_dt_and_rates() {
  int flagged = 0, counted = 0;
  g_lcg = 12345u;
  // Epätasainen 10...50 Hz, ±3 rpm kohina. 30 rpm/s -> 60 / 2 s > 50.
  driftRun(30.0f, 0.02f, 0.1f, 3.0f, flagged, counted);
  TEST_ASSERT_GREATER_THAN(100, counted);
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  // 20 rpm/s -> 40 / 2 s < 50: ei koskaan.
  driftRun(20.0f, 0.02f, 0.1f, 3.0f, flagged, counted);
  TEST_ASSERT_EQUAL_INT(0, flagged);
  // Laskeva rpm on yhtä lailla ajautumista.
  driftRun(-30.0f, 0.02f, 0.1f, 3.0f, flagged, counted);
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  // Tasainen 10 Hz ja 50 Hz.
  driftRun(30.0f, 0.1f, 0.1f, 0.0f, flagged, counted);
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  driftRun(20.0f, 0.1f, 0.1f, 0.0f, flagged, counted);
  TEST_ASSERT_EQUAL_INT(0, flagged);
  driftRun(30.0f, 0.02f, 0.02f, 0.0f, flagged, counted);
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  driftRun(20.0f, 0.02f, 0.02f, 0.0f, flagged, counted);
  TEST_ASSERT_EQUAL_INT(0, flagged);
}

void test_row5_drift_window_is_exact_with_irregular_dt() {
  // Raja ±4 %: 24 rpm/s -> 48 / 2 s ei saa liputtaa, 26 rpm/s -> 52 / 2 s
  // pitää aina liputtaa. Päivitykset eivät osu ikkunan reunaan, joten
  // lähimmän vanhemman näytteen käyttö ilman interpolointia venyttäisi
  // ikkunaa jopa dt:n verran (24 x 2,1 = 50,4 > 50).
  int flagged = 0, counted = 0;
  g_lcg = 777u;
  driftRun(24.0f, 0.02f, 0.1f, 0.0f, flagged, counted);
  TEST_ASSERT_GREATER_THAN(100, counted);
  TEST_ASSERT_EQUAL_INT(0, flagged);
  driftRun(26.0f, 0.02f, 0.1f, 0.0f, flagged, counted);
  TEST_ASSERT_EQUAL_INT(counted, flagged);
}

void test_row5_drift_step_needs_full_window_of_history() {
  // Askel 850 -> 1000 rpm hetkellä 0,5 s. Lippu nousee vasta kun historiaa on
  // koko ikkunan verran (t >= 2,0 s; vertailukohta t = 0 -> 850) ja laskee kun
  // vertailukohta on askeleen jälkeen (t > 2,5 s + harvennusväli).
  bool seen_before = false;
  bool seen_mid = false;
  bool seen_after = false;
  while (g.t < 4.0f) {
    const float now = g.t;
    StatusResult r = g.step(running(-55.0f, -55.4f, -54.8f, now < 0.5f ? 850.0f : 1000.0f));
    if (now < 2.0f && r.flag_drift) seen_before = true;
    if (now >= 2.0f && now <= 2.375f && r.flag_drift) seen_mid = true;
    if (now >= 2.75f && r.flag_drift) seen_after = true;
  }
  TEST_ASSERT_FALSE(seen_before);
  TEST_ASSERT_TRUE(seen_mid);
  TEST_ASSERT_FALSE(seen_after);
}

void test_row5_drift_history_cleared_when_rpm_lost() {
  settle(idleSynced());
  g.run(engineOff(), 2.0f);   // rpm ei voimassa -> historia pois
  // Moottori käynnistyy eri kierroksille: ei vertailukohtaa -> ei ajautumista.
  StatusResult r = g.run(running(-55.0f, -55.4f, -54.8f, 1500.0f), 1.5f);
  TEST_ASSERT_FALSE(r.flag_drift);
}

// ---------------------------------------------------------------------------
// Rivi 6: letkuvika
// ---------------------------------------------------------------------------

void test_row6_hose_off() {
  StatusInput in = idleSynced();
  dropPulse(in, 1, -0.5f);
  StatusResult r = g.step(in);   // ei pitoaikaa: has_pulse + EMA viivästävät jo
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(1, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei alipainetta, tarkista letku", r.headline);
  TEST_ASSERT_EQUAL_STRING("Letku irti tai vuotaa – kytke ja tarkista liitokset", r.advice);
  assertNoVerdict(r);
  TEST_ASSERT_EQUAL_STRING("fault_hose", sbx::syncStateKey(r.state));
  // Ehjät kanavat lasketaan yhä masteria vasten, vikakanava d = 0.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.vac_delta_kpa[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.2f, r.vac_delta_kpa[2]);
  TEST_ASSERT_EQUAL_INT(2, r.worst_ch);
  assertTextSane(r);
  // Ei automaattista nollausta letkuvian aikana, vaikka kanava on nollassa.
  g.run(in, 10.0f);
  TEST_ASSERT_EQUAL_INT(0, g.zero_requests);
}

void test_row6_threshold_and_blocked_hose_extension() {
  StatusInput in = idleSynced();
  dropPulse(in, 1, -1.9f);   // |taso| < hose_off_kpa (2,0)
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));

  // -2,1 kPa ilman pulssia: ei vielä rivin 6 ehto -> ohimenevä (Unstable).
  g.begin();
  settle(idleSynced());
  in = idleSynced();
  dropPulse(in, 1, -2.1f);
  for (int k = 0; k < 48; ++k) {   // off_hold_s = 3,0 s
    r = g.step(in);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
    TEST_ASSERT_EQUAL_STRING("SYL 2: ei pulssia (taso -2,1 kPa)", r.advice);
    assertNoVerdict(r);
  }
  // Jatkui off_hold_s: vika (tukos, taittuma tai kalibroimaton irti oleva letku).
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(1, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei pulssia, tarkista letku", r.headline);
  TEST_ASSERT_EQUAL_STRING("Taso -2,1 kPa ilman pulssia – letku irti, tukossa tai taittunut?",
                           r.advice);
  assertTextSane(r);
}

void test_row6_realistic_disconnect_sequence() {
  // Letku irrotetaan hetkellä t0. DSP: has_pulse pysyy pulse_timeout_s = 1 s,
  // taso on EMA (tau 0,5 s) -55 kPa:sta kohti nollaa. Rivi 6 täyttyy kun
  // 55 e^(-dt/0,5) < 2 eli dt > 0,5 ln(27,5) = 1,66 s.
  settle(idleSynced());
  const float t0 = g.t;
  bool fault_seen = false;
  while (g.t < t0 + 3.0f) {
    const float dt = g.t - t0;
    StatusInput in = idleSynced();
    in.ch[2].level_kpa = -54.8f * std::exp(-dt / 0.5f);
    if (dt >= 1.0f) dropPulse(in, 2, in.ch[2].level_kpa);
    StatusResult r = g.step(in);
    if (dt < 1.0f) {
      // Vanhentunut keskiarvo on yhä DSP:n mukaan voimassa.
      TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
    } else if (dt < 1.6f) {
      TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
    } else if (dt > 1.7f) {
      TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
      TEST_ASSERT_EQUAL_INT(2, r.fault_ch);
      fault_seen = true;
    }
    TEST_ASSERT_TRUE(r.state != SyncState::FaultMaster);
  }
  TEST_ASSERT_TRUE(fault_seen);
}

void test_row6_two_hoses_off() {
  StatusInput in = idleSynced();
  dropPulse(in, 1, 0.2f);
  dropPulse(in, 2, -0.3f);
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(1, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 ja SYL 3 – ei alipainetta, tarkista letkut", r.headline);
  assertTextSane(r);
}

void test_row6_mixed_hose_faults_generic_text() {
  StatusInput in = idleSynced();
  dropPulse(in, 1, -0.2f);    // irti
  dropPulse(in, 2, -30.0f);   // tukossa
  StatusResult r = g.run(in, 3.5f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 ja SYL 3 – tarkista letkut", r.headline);
  assertTextSane(r);
}

void test_startup_and_shutdown_stagger_never_flash_fault() {
  // Käynnistys: SYL 3:n ensimmäinen sykli tunnistetaan 0,3 s muita myöhemmin,
  // alipaine on jo olemassa. Ei saa välähtää VIKA.
  StatusInput in = idleSynced();
  dropPulse(in, 2, -40.0f);
  in.rpm.valid = false;
  for (int k = 0; k < 5; ++k) {
    StatusResult r = g.step(in);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  }
  settle(idleSynced());
  // Sammutus: kanavat aikakatkaistaan eri hetkinä (1/3 sykliä välein), taso
  // vielä ~-7 kPa (EMA 1 s sammutuksen jälkeen).
  in = idleSynced();
  dropPulse(in, 0, -7.4f);   // master ensin
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  dropPulse(in, 1, -7.2f);
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  r = g.step(engineOff(-7.0f, -6.9f, -6.8f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::NoSignal), static_cast<int>(r.state));
}

// ---------------------------------------------------------------------------
// Rivi 7: anturivika
// ---------------------------------------------------------------------------

void test_row7_sensor_fault() {
  StatusInput in = idleSynced();
  sensorFault(in, 2, 12);
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultSensor), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(2, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 3 – anturivika", r.headline);
  TEST_ASSERT_EQUAL_STRING("Jännite 12 mV – tarkista anturin johdot tai vaihda", r.advice);
  assertNoVerdict(r);
  TEST_ASSERT_EQUAL_STRING("fault_sensor", sbx::syncStateKey(r.state));
  assertTextSane(r);
}

void test_row7_multiple_sensor_faults() {
  StatusInput in = idleSynced();
  sensorFault(in, 1, 3300);
  sensorFault(in, 2, 3300);
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultSensor), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(1, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 ja SYL 3 – anturivika", r.headline);
  TEST_ASSERT_EQUAL_STRING("Useita anturivikoja – tarkista anturien käyttöjännite", r.advice);
  assertTextSane(r);
}

void test_row7_sensor_fault_with_engine_off_beats_no_signal() {
  // Anturivika näkyy myös moottori sammuksissa (mV jumissa ääripäässä), eikä
  // nollausta pyydetä, koska yksi kanava on epäluotettava.
  StatusInput in = engineOff();
  sensorFault(in, 1, 3);
  StatusResult r = g.run(in, 10.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultSensor), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(1, r.fault_ch);
  TEST_ASSERT_EQUAL_INT(0, g.zero_requests);
}

// ---------------------------------------------------------------------------
// Rivit 10 ja 11, D11: liput
// ---------------------------------------------------------------------------

void test_row10_saturation_flag_is_independent_of_state() {
  StatusInput in = idleSynced();
  in.ch[0].saturated = true;
  StatusResult r = settle(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  TEST_ASSERT_TRUE(r.flag_saturation);
  // Sama lippu myös odotustilassa.
  in.rpm.valid = false;
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  TEST_ASSERT_TRUE(r.flag_saturation);
}

void test_row10_atmosphere_is_not_pulse_saturation() {
  // GN-anturi ilmanpaineessa = 2700 mV >= sat_mv (2680). Sammutetulla
  // moottorilla tämä ei ole rivin 10 "pulssin huippu leikkautuu".
  StatusInput in = engineOff();
  for (int i = 0; i < kChannels; ++i) in.ch[i].saturated = true;
  StatusResult r = g.step(in);
  TEST_ASSERT_FALSE(r.flag_saturation);
}

void test_row11_uncal_gain_is_warning_only() {
  StatusInput in = idleSynced();
  in.gain_valid = false;
  StatusResult r = settle(in);
  // D11: puuttuva vahvistuskalibrointi ei estä vihreää.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  TEST_ASSERT_TRUE(r.flag_uncal_gain);
  TEST_ASSERT_FALSE(r.flag_uncal_zero);
  // Lippu on tilasta riippumaton.
  StatusInput off = engineOff();
  off.gain_valid = false;
  r = g.step(off);
  TEST_ASSERT_TRUE(r.flag_uncal_gain);
}

void test_d11_never_green_without_zero() {
  StatusInput in = running(-55.0f, -55.2f, -54.8f);   // spread 0,4: olisi vihreä
  in.zero_valid = false;
  for (int k = 0; k < 200; ++k) {
    StatusResult r = g.step(in);
    TEST_ASSERT_TRUE(r.state != SyncState::Synced);
    TEST_ASSERT_TRUE(r.flag_uncal_zero);
  }
  StatusResult r = g.last;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Lähellä – kalibroimaton", r.headline);
  TEST_ASSERT_EQUAL_STRING("Nollaus puuttuu – sammuta moottori hetkeksi, laite nollaa itse",
                           r.advice);
  TEST_ASSERT_TRUE(r.show_verdict);
  TEST_ASSERT_TRUE(r.show_bars);
  assertTextSane(r);
  // Kun nollaus on tehty, sama tilanne on vihreä.
  in.zero_valid = true;
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
}

void test_d11_manual_zero_advice_and_red_stays_red() {
  StatusParams p;
  p.auto_zero = false;
  g.begin(p);
  StatusInput in = running(-55.0f, -55.2f, -54.8f);
  in.zero_valid = false;
  StatusResult r = settle(in);
  TEST_ASSERT_EQUAL_STRING("Nollaus puuttuu – irrota letkut ja tee nollaus", r.advice);
  // Keltainen ja punainen pysyvät, otsikko kertoo kalibroimattomuuden.
  in = running(-55.0f, -55.3f, -57.1f);
  in.zero_valid = false;
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Lähellä – kalibroimaton", r.headline);
  TEST_ASSERT_EQUAL_STRING("SYL 3: avaa läppää – imee 2,1 kPa enemmän kuin SYL 1", r.advice);
  in = running(-55.0f, -51.0f, -56.0f);
  in.zero_valid = false;
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::OutOfSync), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Ei synkronissa – kalibroimaton", r.headline);
}

// ---------------------------------------------------------------------------
// Rivi 12: master vialla
// ---------------------------------------------------------------------------

void test_row12_master_sensor_fault() {
  StatusInput in = idleSynced();
  sensorFault(in, 0, 3300);
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultMaster), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(0, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("Master (SYL 1) ei kelpaa viitteeksi – valitse toinen", r.headline);
  TEST_ASSERT_EQUAL_STRING("SYL 1: anturivika (jännite 3300 mV)", r.advice);
  assertNoVerdict(r);
  // Erot masteriin ovat merkityksettömiä -> nollattu.
  for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_FLOAT(0.0f, r.vac_delta_kpa[i]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.spread_kpa);
  TEST_ASSERT_EQUAL_INT(-1, r.worst_ch);
  TEST_ASSERT_EQUAL_STRING("fault_master", sbx::syncStateKey(r.state));
  assertTextSane(r);
}

void test_row12_master_hose_off_depends_on_master_choice() {
  StatusInput in = idleSynced();
  dropPulse(in, 1, -0.3f);
  StatusParams p;
  p.master = 1;
  g.begin(p);
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultMaster), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(1, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("Master (SYL 2) ei kelpaa viitteeksi – valitse toinen", r.headline);
  TEST_ASSERT_EQUAL_STRING("SYL 2: ei alipainetta – tarkista letku", r.advice);
  // Käyttäjä vaihtaa masterin (rivi 12): sama kanava on nyt tavallinen letkuvika.
  p.master = 0;
  g.sm.configure(p);
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(1, r.fault_ch);
}

void test_row12_master_blocked_hose() {
  StatusInput in = idleSynced();
  dropPulse(in, 0, -30.0f);
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  r = g.run(in, 3.5f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultMaster), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("SYL 1: ei pulssia (taso -30,0 kPa) – tarkista letku", r.advice);
}

// ---------------------------------------------------------------------------
// Prioriteettiristiriidat (§8: vikatilat voittavat aina)
// ---------------------------------------------------------------------------

void test_priority_sensor_fault_beats_out_of_sync_and_green() {
  StatusInput in = running(-55.0f, -51.0f, -60.0f);
  sensorFault(in, 1, 5);
  StatusResult r = settle(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultSensor), static_cast<int>(r.state));
  assertNoVerdict(r);
  // Ajoittainen vika: kanava pulssii JA on vikalipulla -> silti vika, ei vihreää.
  in = idleSynced();
  in.ch[2].sensor_fault = true;
  r = settle(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultSensor), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(2, r.fault_ch);
}

void test_priority_master_fault_beats_other_faults() {
  StatusInput in = idleSynced();
  dropPulse(in, 0, 0.0f);       // master letku irti
  sensorFault(in, 2, 3300);     // ja toisen kanavan anturi rikki
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultMaster), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(0, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("SYL 1: ei alipainetta – tarkista letku", r.advice);
}

void test_priority_sensor_fault_beats_hose_fault() {
  StatusInput in = idleSynced();
  dropPulse(in, 1, 0.0f);
  sensorFault(in, 2, 3300);
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultSensor), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(2, r.fault_ch);
}

void test_priority_fault_beats_rpm_too_high_and_unstable() {
  StatusInput in = running(-30.0f, -30.0f, -30.0f, 4000.0f);
  dropPulse(in, 2, 0.0f);
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  in = running(-55.0f, -55.0f, -55.0f, 850.0f, 0.5f);
  dropPulse(in, 2, 0.0f);
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
}

void test_nan_mean_never_green() {
  StatusInput in = idleSynced();
  in.ch[1].mean_kpa = NAN;   // ei pitäisi tulla DSP:stä, mutta ei saa olla vihreä
  StatusResult r = settle(in);
  TEST_ASSERT_TRUE(r.state != SyncState::Synced);
  TEST_ASSERT_FALSE(r.show_verdict);
  assertTextSane(r);
}

// ---------------------------------------------------------------------------
// Tekstit: pituus, UTF-8, desimaalipilkku
// ---------------------------------------------------------------------------

void test_text_extreme_values_fit() {
  // -99,9 kPa:n erot (anturin koko alue) ja 3-numeroiset (väärä kalibrointi),
  // jotka rajataan 999,9:ään.
  StatusResult r = settle(running(-0.1f, -100.0f, -0.1f));
  TEST_ASSERT_EQUAL_STRING("SYL 2: avaa läppää – imee 99,9 kPa enemmän kuin SYL 1", r.advice);
  assertTextSane(r);
  r = g.step(running(-150.0f, -0.5f, -150.0f));
  TEST_ASSERT_EQUAL_STRING("SYL 2: sulje läppää – imee 149,5 kPa vähemmän kuin SYL 1", r.advice);
  assertTextSane(r);
  r = g.step(running(-500.0f, 900.0f, -500.0f));
  TEST_ASSERT_EQUAL_STRING("SYL 2: sulje läppää – imee 999,9 kPa vähemmän kuin SYL 1", r.advice);
  assertTextSane(r);

  // Rivi 9 suurimmalla konfiguroitavalla rajalla ja järjettömällä rpm:llä.
  StatusParams p;
  p.sync_rpm_max = 12000.0f;
  g.begin(p);
  r = g.step(running(-30.0f, -30.0f, -30.0f, 1.0e9f));
  TEST_ASSERT_EQUAL_STRING("Kierrosluku 99999 rpm (raja 12000) – laske kaasua", r.advice);
  assertTextSane(r);

  g.begin();
  r = g.step(running(-55.0f, -55.0f, -55.0f, 850.0f, 1.0e6f));
  TEST_ASSERT_EQUAL_STRING("Käynti epätasainen: hajonta 999,9 % (raja 15,0 %)", r.advice);
  assertTextSane(r);

  StatusInput in = idleSynced();
  sensorFault(in, 0, 65535);
  r = g.step(in);
  TEST_ASSERT_EQUAL_STRING("SYL 1: anturivika (jännite 65535 mV)", r.advice);
  assertTextSane(r);
  in = idleSynced();
  sensorFault(in, 1, 65535);
  r = g.step(in);
  TEST_ASSERT_EQUAL_STRING("Jännite 65535 mV – tarkista anturin johdot tai vaihda", r.advice);
  assertTextSane(r);

  in = idleSynced();
  dropPulse(in, 2, -1.0e6f);
  r = g.run(in, 3.5f);
  TEST_ASSERT_EQUAL_STRING("Taso -999,9 kPa ilman pulssia – letku irti, tukossa tai taittunut?",
                           r.advice);
  assertTextSane(r);
  in = idleSynced();
  dropPulse(in, 0, -1.0e6f);
  r = g.run(in, 3.5f);
  TEST_ASSERT_EQUAL_STRING("SYL 1: ei pulssia (taso -999,9 kPa) – tarkista letku", r.advice);
  assertTextSane(r);

  // Pisin odotusteksti: stable_hold_s konfiguraation ylärajalla.
  p = StatusParams();
  p.stable_hold_s = 30.0f;
  g.begin(p);
  r = g.run(idleSynced(), 29.9375f);
  TEST_ASSERT_EQUAL_STRING("Vakautta odotetaan: 29,9 / 30,0 s", r.advice);
  assertTextSane(r);

  // Matala rpm: alarajan konfiguraation ylärajalla.
  p = StatusParams();
  p.sync_rpm_min = 3000.0f;
  p.sync_rpm_max = 12000.0f;
  g.begin(p);
  r = g.step(running(-55.0f, -55.0f, -55.0f, 2999.5f));
  TEST_ASSERT_EQUAL_STRING("Kierrosluku liian matala: 2999 rpm (raja 3000)", r.advice);
  assertTextSane(r);
}

void test_every_state_produces_sane_text() {
  // Kaikki tilat yhdellä kertaa: otsikko ja ohje aina ei-tyhjiä ja ehjiä.
  StatusInput in = idleSynced();
  assertTextSane(g.step(in));                         // Unstable (odotus)
  assertTextSane(settle(in));                         // Synced
  assertTextSane(g.step(running(-55.0f, -57.0f, -55.0f)));   // Near
  assertTextSane(g.step(running(-55.0f, -60.0f, -55.0f)));   // OutOfSync
  assertTextSane(g.step(running(-55.0f, -55.0f, -55.0f, 5000.0f)));  // RpmTooHigh
  assertTextSane(g.step(engineOff()));                // NoSignal
  in = idleSynced();
  sensorFault(in, 1, 0);
  assertTextSane(g.step(in));                         // FaultSensor
  in = idleSynced();
  dropPulse(in, 1, 0.0f);
  assertTextSane(g.step(in));                         // FaultHose
  in = idleSynced();
  dropPulse(in, 0, 0.0f);
  assertTextSane(g.step(in));                         // FaultMaster
}

void test_utf8_safe_truncation() {
  using sbx::status_detail::writeText;
  char buf[16];
  // "läppää" = l(1) ä(2) p p ä(2) ä(2) = 9 tavua.
  writeText(buf, 8, "%s", "läppää");
  TEST_ASSERT_EQUAL_STRING("läppä", buf);   // 7 tavua mahtuu kokonaan
  writeText(buf, 7, "%s", "läppää");
  TEST_ASSERT_EQUAL_STRING("läpp", buf);    // puolikas ä pois
  TEST_ASSERT_TRUE(utf8Valid(buf));
  // "–" = 3 tavua: kumpikaan vajaa katkaisu ei saa jättää siitä osaa.
  writeText(buf, 3, "%s", "a–b");
  TEST_ASSERT_EQUAL_STRING("a", buf);
  writeText(buf, 4, "%s", "a–b");
  TEST_ASSERT_EQUAL_STRING("a", buf);
  writeText(buf, 5, "%s", "a–b");
  TEST_ASSERT_EQUAL_STRING("a–", buf);
  writeText(buf, 6, "%s", "a–b");
  TEST_ASSERT_EQUAL_STRING("a–b", buf);
  writeText(buf, 1, "%s", "ä");
  TEST_ASSERT_EQUAL_STRING("", buf);
  writeText(buf, 2, "%s", "ä");
  TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_decimal_comma_formatting() {
  using sbx::status_detail::fmt1;
  char b[8];
  fmt1(b, 2.1f);
  TEST_ASSERT_EQUAL_STRING("2,1", b);
  fmt1(b, -55.0f);
  TEST_ASSERT_EQUAL_STRING("-55,0", b);
  fmt1(b, -0.04f);
  TEST_ASSERT_EQUAL_STRING("0,0", b);       // ei "-0,0"
  fmt1(b, 1.0e9f);
  TEST_ASSERT_EQUAL_STRING("999,9", b);
  fmt1(b, -1.0e9f);
  TEST_ASSERT_EQUAL_STRING("-999,9", b);
  fmt1(b, NAN);
  TEST_ASSERT_EQUAL_STRING("?", b);
  fmt1(b, INFINITY);
  TEST_ASSERT_EQUAL_STRING("?", b);
}

// ---------------------------------------------------------------------------
// Ajastimet, configure(), determinismi
// ---------------------------------------------------------------------------

void test_configure_resets_timers() {
  StatusResult r = settle(idleSynced());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  StatusParams p;
  p.master = 1;   // esim. master vaihdettiin UI:sta (D7)
  g.sm.configure(p);
  r = g.step(idleSynced());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  r = settle(idleSynced());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  // configure() virittää myös nollauksen uudelleen.
  g.run(engineOff(), 5.0f);
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
  g.sm.configure(p);
  g.run(engineOff(), 5.0f);
  TEST_ASSERT_EQUAL_INT(2, g.zero_requests);
}

void test_reset_restarts_stability() {
  settle(idleSynced());
  g.sm.reset();
  StatusResult r = g.step(idleSynced());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
}

void test_time_going_backwards_restarts_timers() {
  g.run(idleSynced(), 10.0f);
  // Kello hyppää taaksepäin (esim. nollaus). Ilman uutta alkua vakausjakso
  // (alkanut t = 0) näyttäisi jo 5 s:n pituiselta ja tuomio jatkuisi.
  g.t = 5.0f;
  StatusResult r = g.step(idleSynced());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  r = settle(idleSynced());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  // NaN-aika: aika ei etene, mutta tila pysyy järkevänä.
  StatusInput in = idleSynced();
  in.now_s = NAN;
  r = g.sm.update(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
}

bool sameResult(const StatusResult& a, const StatusResult& b) {
  if (a.state != b.state || a.fault_ch != b.fault_ch || a.show_bars != b.show_bars ||
      a.show_verdict != b.show_verdict || a.worst_ch != b.worst_ch ||
      a.flag_saturation != b.flag_saturation || a.flag_uncal_zero != b.flag_uncal_zero ||
      a.flag_uncal_gain != b.flag_uncal_gain || a.flag_drift != b.flag_drift ||
      a.request_auto_zero != b.request_auto_zero) {
    return false;
  }
  if (std::memcmp(&a.spread_kpa, &b.spread_kpa, sizeof(float)) != 0) return false;
  if (std::memcmp(a.vac_delta_kpa, b.vac_delta_kpa, sizeof(a.vac_delta_kpa)) != 0) return false;
  return std::strcmp(a.headline, b.headline) == 0 && std::strcmp(a.advice, b.advice) == 0;
}

void test_deterministic() {
  // Kaksi konetta, sama syötesarja (sammutus, käynnistys, ajautuminen, vika)
  // -> bitilleen samat tulokset.
  StatusMachine a;
  StatusMachine b;
  g_lcg = 99u;
  float t = 0.0f;
  int requests = 0;
  for (int k = 0; k < 2000; ++k) {
    StatusInput in;
    const int phase = (k / 150) % 5;
    if (phase == 0) {
      in = engineOff();
    } else if (phase == 4) {
      in = idleSynced();
      dropPulse(in, 2, -0.5f);
    } else {
      in = running(-55.0f, -55.0f - 3.0f * lcgUnit(), -54.0f - 2.0f * lcgUnit(),
                   800.0f + 300.0f * lcgUnit(), 0.2f * lcgUnit());
    }
    in.now_s = t;
    const StatusResult ra = a.update(in);
    const StatusResult rb = b.update(in);
    TEST_ASSERT_TRUE(sameResult(ra, rb));
    assertTextSane(ra);
    if (ra.request_auto_zero) ++requests;
    t += 0.02f + 0.06f * lcgUnit();
  }
  // Kolme sammutusjaksoa (k = 0, 750, 1500; ~150 x 0,05 s = 7,5 s > off_hold_s),
  // välissä pulssit palaavat -> jokainen nollaa tasan kerran.
  TEST_ASSERT_EQUAL_INT(3, requests);
}

void test_state_keys_match_snapshot_contract() {
  TEST_ASSERT_EQUAL_STRING("no_signal", sbx::syncStateKey(SyncState::NoSignal));
  TEST_ASSERT_EQUAL_STRING("unstable", sbx::syncStateKey(SyncState::Unstable));
  TEST_ASSERT_EQUAL_STRING("rpm_too_high", sbx::syncStateKey(SyncState::RpmTooHigh));
  TEST_ASSERT_EQUAL_STRING("synced", sbx::syncStateKey(SyncState::Synced));
  TEST_ASSERT_EQUAL_STRING("near", sbx::syncStateKey(SyncState::Near));
  TEST_ASSERT_EQUAL_STRING("out_of_sync", sbx::syncStateKey(SyncState::OutOfSync));
  TEST_ASSERT_EQUAL_STRING("fault_sensor", sbx::syncStateKey(SyncState::FaultSensor));
  TEST_ASSERT_EQUAL_STRING("fault_hose", sbx::syncStateKey(SyncState::FaultHose));
  TEST_ASSERT_EQUAL_STRING("fault_master", sbx::syncStateKey(SyncState::FaultMaster));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_row1_8_no_pulses_is_no_signal);
  RUN_TEST(test_row8_all_hoses_off_engine_running_is_no_signal_not_hose_fault);
  RUN_TEST(test_row1_auto_zero_fires_once_after_off_hold);
  RUN_TEST(test_row1_auto_zero_also_without_previous_zero);
  RUN_TEST(test_row1_auto_zero_rearms_after_pulses_return);
  RUN_TEST(test_row1_auto_zero_not_rearmed_by_fault_without_pulses);
  RUN_TEST(test_row1_no_auto_zero_while_vacuum_bleeds_down);
  RUN_TEST(test_row1_level_just_outside_off_kpa_blocks_zero);
  RUN_TEST(test_row1_auto_zero_disabled);
  RUN_TEST(test_row2_rpm_not_yet_valid_is_unstable);
  RUN_TEST(test_row2_high_cv_is_unstable_and_limit_is_inclusive);
  RUN_TEST(test_row2_nan_cv_or_rpm_is_unstable);
  RUN_TEST(test_stability_timer_gates_verdict);
  RUN_TEST(test_stable_hold_zero_gives_immediate_verdict);
  RUN_TEST(test_d12_low_edge);
  RUN_TEST(test_d12_high_edge_and_row9);
  RUN_TEST(test_row9_from_cold_start);
  RUN_TEST(test_row3_synced);
  RUN_TEST(test_row3_4a_green_edge_is_inclusive);
  RUN_TEST(test_row4a_near_with_open_advice);
  RUN_TEST(test_row4a_near_with_close_advice);
  RUN_TEST(test_row4b_out_of_sync_and_yellow_edge);
  RUN_TEST(test_spread_uses_both_sides_of_master);
  RUN_TEST(test_advice_direction_follows_sign);
  RUN_TEST(test_worst_channel_tie_and_master_excluded);
  RUN_TEST(test_master_selection_changes_deltas_and_advice);
  RUN_TEST(test_invalid_master_index_falls_back_to_syl1);
  RUN_TEST(test_row5_drift_keeps_bars_and_verdict);
  RUN_TEST(test_row5_slow_change_is_not_drift);
  RUN_TEST(test_row5_drift_irregular_dt_and_rates);
  RUN_TEST(test_row5_drift_window_is_exact_with_irregular_dt);
  RUN_TEST(test_row5_drift_step_needs_full_window_of_history);
  RUN_TEST(test_row5_drift_history_cleared_when_rpm_lost);
  RUN_TEST(test_row6_hose_off);
  RUN_TEST(test_row6_threshold_and_blocked_hose_extension);
  RUN_TEST(test_row6_realistic_disconnect_sequence);
  RUN_TEST(test_row6_two_hoses_off);
  RUN_TEST(test_row6_mixed_hose_faults_generic_text);
  RUN_TEST(test_startup_and_shutdown_stagger_never_flash_fault);
  RUN_TEST(test_row7_sensor_fault);
  RUN_TEST(test_row7_multiple_sensor_faults);
  RUN_TEST(test_row7_sensor_fault_with_engine_off_beats_no_signal);
  RUN_TEST(test_row10_saturation_flag_is_independent_of_state);
  RUN_TEST(test_row10_atmosphere_is_not_pulse_saturation);
  RUN_TEST(test_row11_uncal_gain_is_warning_only);
  RUN_TEST(test_d11_never_green_without_zero);
  RUN_TEST(test_d11_manual_zero_advice_and_red_stays_red);
  RUN_TEST(test_row12_master_sensor_fault);
  RUN_TEST(test_row12_master_hose_off_depends_on_master_choice);
  RUN_TEST(test_row12_master_blocked_hose);
  RUN_TEST(test_priority_sensor_fault_beats_out_of_sync_and_green);
  RUN_TEST(test_priority_master_fault_beats_other_faults);
  RUN_TEST(test_priority_sensor_fault_beats_hose_fault);
  RUN_TEST(test_priority_fault_beats_rpm_too_high_and_unstable);
  RUN_TEST(test_nan_mean_never_green);
  RUN_TEST(test_text_extreme_values_fit);
  RUN_TEST(test_every_state_produces_sane_text);
  RUN_TEST(test_utf8_safe_truncation);
  RUN_TEST(test_decimal_comma_formatting);
  RUN_TEST(test_configure_resets_timers);
  RUN_TEST(test_reset_restarts_stability);
  RUN_TEST(test_time_going_backwards_restarts_timers);
  RUN_TEST(test_deterministic);
  RUN_TEST(test_state_keys_match_snapshot_contract);
  return UNITY_END();
}
