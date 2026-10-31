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

// Testien odotukset (neuvotekstien "kuin SYL 1", vac_delta, rivi 12 kanavalla 0)
// on kirjoitettu SYL 1 -masterille, joka oli D7:n oletus. D16 (31.10.2026)
// vaihtoi oletuksen SYL 3:een, joten SYL 1 kiinnitetään tässä eksplisiittisesti:
// testit testaavat samaa kuin ennen. Oletusta itseään testaavat test_types ja
// test_processor (test_default_master_is_syl3_D16).
StatusParams params() {
  StatusParams p;
  p.master = 0;
  return p;
}

struct Rig {
  StatusMachine sm;
  float t = 0.0f;
  int zero_requests = 0;
  StatusResult last;

  void begin(const StatusParams& p = params()) {
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

// act (WP-10, firmware.md §5) on advice-tekstin ruuvineuvo rakenteisena: sama
// kanava ja suunta, kun teksti on "SYL n: avaa|sulje läppää – …", muuten -1.
void assertActMatchesAdvice(const StatusResult& r) {
  int want_ch = -1;
  bool want_open = false;
  for (int ch = 0; ch < kChannels; ++ch) {
    for (int open = 0; open < 2; ++open) {
      char p[48];
      std::snprintf(p, sizeof(p), "SYL %d: %s läppää – ", ch + 1, open == 1 ? "avaa" : "sulje");
      if (std::strncmp(r.advice, p, std::strlen(p)) == 0) {
        want_ch = ch;
        want_open = open == 1;
      }
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(want_ch, r.act_ch, r.advice);
  if (want_ch >= 0) TEST_ASSERT_EQUAL_INT_MESSAGE(want_open ? 1 : 0, r.act_open ? 1 : 0, r.advice);
}

// Teksti mahtuu puskuriin katkeamatta: NUL löytyy ja tilaa jää vähintään yksi
// tavu (snprintf olisi katkaissut täsmälleen täyteen). Lisäksi kelvollista UTF-8:aa,
// ja act vastaa tekstiä.
void assertTextSane(const StatusResult& r) {
  const size_t hl = strnlen(r.headline, sizeof(r.headline));
  const size_t al = strnlen(r.advice, sizeof(r.advice));
  TEST_ASSERT_LESS_THAN(static_cast<long long>(kHeadlineLen) - 1, static_cast<long long>(hl));
  TEST_ASSERT_LESS_THAN(static_cast<long long>(kAdviceLen) - 1, static_cast<long long>(al));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(hl));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(al));
  TEST_ASSERT_TRUE_MESSAGE(utf8Valid(r.headline), r.headline);
  TEST_ASSERT_TRUE_MESSAGE(utf8Valid(r.advice), r.advice);
  assertActMatchesAdvice(r);
}

void assertNoVerdict(const StatusResult& r) {
  TEST_ASSERT_FALSE(r.show_bars);
  TEST_ASSERT_FALSE(r.show_verdict);
}

// Ajaa vakauden yli (stable_hold_s + marginaali).
StatusResult settle(const StatusInput& in) { return g.run(in, 4.0f); }

// Rivi 6: letkuvika vasta kHoseHoldS:n jälkeen (ks. test_row6_hose_off_needs_hold).
StatusResult hoseHold(const StatusInput& in) {
  return g.run(in, sbx::StatusMachine::kHoseHoldS + kDt);
}

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
  // Taso asettui -0,5:een (viimeinen rampin näyte hetkellä t_last). NoSignal
  // on jatkunut jo 14 s, joten rivin 1 off_hold_s täyttyi aikoja sitten, ja
  // pyynnön ratkaisee vakausportti (WP-9: 1 s:n vaihteluväli <
  // zero_settle_kpa 0,1). Rampin viimeinen 0,1 kPa kesti 0,1 s, joten pyyntö
  // tulee aikaisintaan 1,0 - 0,1 s ja viimeistään 1,0 s + yksi askel
  // t_lastin jälkeen. (WP-4: off_hold_s laskettiin vasta asettumisesta, 2,7-3,06 s;
  // muutettu WP-9:ssä, docs/paketit.md: "lyhennetään toteutuksen osuus".)
  const float t_last = g.t - kDt;
  float t_fire = -1.0f;
  for (int k = 0; k < 80 && t_fire < 0.0f; ++k) {
    StatusResult rr = g.step(engineOff(-0.5f, -0.5f, -0.5f));
    if (rr.request_auto_zero) t_fire = g.t - kDt;
  }
  TEST_ASSERT_TRUE(t_fire > 0.0f);
  TEST_ASSERT_GREATER_THAN_FLOAT(sbx::kZeroSettleWindowS - 0.1f - 0.001f, t_fire - t_last);
  TEST_ASSERT_LESS_THAN_FLOAT(sbx::kZeroSettleWindowS + kDt + 0.001f, t_fire - t_last);
}

// §8:n suunnittelusääntö + D10 + rivi 7: nollaus vain NoSignal-tilassa.
// Anturivikainen kanava ei näe luotettavasti ilmanpainetta, vaikka sen taso
// sattuisi olemaan off_kpa:n sisällä (esim. fault_hi_mv 2750 ja anturi
// 2760 mV:ssa = +2,4 kPa; config.h sallii fault_hi_mv:n 2500 mV:iin asti).
// Tappaa mutaatiot, joissa nollausportin tila-ehto puuttuu tai on väärä
// (WP-4:n selviäjä "auto-zero-portti FaultSensor-tilassa").
void test_row1_no_auto_zero_in_fault_state_even_near_zero() {
  StatusInput in = engineOff();
  sensorFault(in, 1, 2760);
  TEST_ASSERT_LESS_THAN_FLOAT(5.0f, std::fabs(in.ch[1].level_kpa));
  StatusResult r = g.run(in, 10.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultSensor), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(0, g.zero_requests);
  // Vertailu: sama taso ilman vikalippua nollautuu (portti on tila, ei taso).
  in.ch[1].sensor_fault = false;
  g.run(in, 4.0f);
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
}

// WP-9 (d, e): automaattinen ja käsin tehty nollaus käyttävät samaa
// vakausporttia: tason vaihteluväli 1 s:n ikkunassa < zero_settle_kpa. Raja
// on tiukka (<) ja koskee jokaista kanavaa. Ikkuna nollautuu configure():ssa.
void test_row1_settle_gate_edges() {
  const StatusParams def = params();
  // Yksi kanava värähtelee juuri rajan yli: ei nollausta koskaan.
  const float swing = def.zero_settle_kpa + 0.01f;
  for (int k = 0; k < 200; ++k) {
    const float l = (k & 1) ? 0.0f : swing;
    g.step(engineOff(0.1f, l, 0.15f));
  }
  TEST_ASSERT_EQUAL_INT(0, g.zero_requests);
  // Juuri rajan alla: nollaus heti, kun off_hold_s on täynnä.
  g.begin();
  const float small = def.zero_settle_kpa - 0.01f;
  float t_fire = -1.0f;
  for (int k = 0; k < 80 && t_fire < 0.0f; ++k) {
    const float now = g.t;
    const float l = (k & 1) ? 0.0f : small;
    if (g.step(engineOff(0.1f, l, 0.15f)).request_auto_zero) t_fire = now;
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, def.off_hold_s, t_fire);
  // Ikkuna vaatii koko sekunnin historiaa: off_hold_s < 1 s ei nollaa ennen sitä.
  StatusParams quick = params();
  quick.off_hold_s = 0.5f;
  g.begin(quick);
  t_fire = -1.0f;
  for (int k = 0; k < 40 && t_fire < 0.0f; ++k) {
    const float now = g.t;
    if (g.step(engineOff()).request_auto_zero) t_fire = now;
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, sbx::kZeroSettleWindowS, t_fire);
  // NaN-taso ei ole vakaa, kunnes se on poistunut ikkunasta.
  g.begin();
  g.run(engineOff(), 1.5f);
  TEST_ASSERT_TRUE(g.sm.settle().range(2) < def.zero_settle_kpa);
  StatusInput nan_in = engineOff();
  nan_in.ch[2].level_kpa = NAN;
  g.step(nan_in);
  g.run(engineOff(), 0.5f);
  TEST_ASSERT_TRUE(std::isinf(g.sm.settle().range(2)));
  g.run(engineOff(), 0.6f);
  TEST_ASSERT_TRUE(g.sm.settle().range(2) < def.zero_settle_kpa);
}

void test_row1_level_just_outside_off_kpa_blocks_zero() {
  // off_kpa = 5: yksikin kanava 5,1 kPa:ssa estää nollauksen.
  g.run(engineOff(0.0f, -5.1f, 0.0f), 10.0f);
  TEST_ASSERT_EQUAL_INT(0, g.zero_requests);
  g.run(engineOff(0.0f, -4.9f, 0.0f), 3.5f);
  TEST_ASSERT_EQUAL_INT(1, g.zero_requests);
}

void test_row1_auto_zero_disabled() {
  StatusParams p = params();
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
  StatusParams p = params();
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
  // Rivi 3: valmis, ei säätöä -> ei ruuvineuvoa, vaikka worst_ch on määritelty.
  TEST_ASSERT_EQUAL_INT(-1, r.act_ch);
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
  // Rivi 4a: suurin poikkeama + säätösuunta (§4.7: d > 0 -> avaa), myös rakenteisena.
  TEST_ASSERT_EQUAL_INT(2, r.act_ch);
  TEST_ASSERT_TRUE(r.act_open);
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
  TEST_ASSERT_EQUAL_INT(1, r.act_ch);   // §4.7: d < 0 -> sulje
  TEST_ASSERT_FALSE(r.act_open);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.8f, r.vac_delta_kpa[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.3f, r.spread_kpa);   // 0,5 - (-1,8)
}

void test_row4b_out_of_sync_and_yellow_edge() {
  StatusResult r = settle(running(-55.0f, -51.0f, -56.0f));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::OutOfSync), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Ei synkronissa", r.headline);
  TEST_ASSERT_EQUAL_STRING("SYL 2: sulje läppää – imee 4,0 kPa vähemmän kuin SYL 1", r.advice);
  TEST_ASSERT_EQUAL_INT(1, r.act_ch);   // rivi 4b: nimetty sylinteri, §4.7: d < 0 -> sulje
  TEST_ASSERT_FALSE(r.act_open);
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
  StatusParams p = params();
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
    StatusParams p = params();
    p.master = static_cast<uint8_t>(m);
    g.begin(p);
    StatusResult r = settle(in);
    TEST_ASSERT_EQUAL_INT(m, g.sm.master());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(r.state));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, r.spread_kpa);
    for (int i = 0; i < kChannels; ++i) TEST_ASSERT_EQUAL_FLOAT(d[m][i], r.vac_delta_kpa[i]);
    TEST_ASSERT_EQUAL_INT(worst[m], r.worst_ch);
    TEST_ASSERT_EQUAL_STRING(advice[m], r.advice);
    // D7: neuvo riippuu viitteestä (suunta d:n merkistä, §4.7).
    TEST_ASSERT_EQUAL_INT(worst[m], r.act_ch);
    TEST_ASSERT_EQUAL_INT(d[m][worst[m]] > 0.0f ? 1 : 0, r.act_open ? 1 : 0);
  }
}

void test_invalid_master_index_falls_back_to_syl1() {
  StatusParams p = params();
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

// Suodatus pois (drift_tau_s = 0): ikkunavertailu sellaisenaan, WP-4:n käytös.
// Ikkunan tarkkuus ja historiavaatimus testataan tällä; suodatettu oletus
// (WP-9) omissa testeissään (test_row5_drift_filter_*).
StatusParams unfiltered() {
  StatusParams p = params();
  p.drift_tau_s = 0.0f;
  return p;
}

// Ajaa rpm-rampin annetulla dt-generaattorilla ja palauttaa (liputetut, arvioidut)
// päivitykset, kun ikkuna + marginaali (+ suodattimen asettuminen) on kulunut.
void driftRun(float slope, float dt_min, float dt_max, float noise, int& flagged, int& counted,
              const StatusParams& p = unfiltered(), float settle_s = 0.0f) {
  g.begin(p);
  flagged = 0;
  counted = 0;
  float t = 0.0f;
  while (t < 12.0f) {
    const float dt = dt_min + (dt_max - dt_min) * lcgUnit();
    const float rpm = 900.0f + slope * t + noise * (2.0f * lcgUnit() - 1.0f);
    StatusInput in = running(-55.0f, -55.4f, -54.8f, rpm);
    in.now_s = t;
    StatusResult r = g.sm.update(in);
    if (t > p.drift_window_s + settle_s + dt_max) {
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
  // Suodatus pois: askeleen vaste testataan ikkunan reunoja vasten.
  g.begin(unfiltered());
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

// Rivi 5 + WP-9: suodatettu oletus (drift_tau_s 0,5 s) säilyttää rajan.
// Tasaisella rampilla suodatettu rpm on rpm - kulmakerroin * tau, joten
// ikkunan erotus on asettumisen jälkeen tarkalleen sama kuin ilman suodatusta:
// raja 50 rpm / 2 s ei siirry. Asettuminen ~4 tau (vaje kulmakerroin * tau *
// e^-4 = 0,24 rpm 26 rpm/s:lla). 60 rpm / 2 s liputtaa, 20 rpm / 2 s ei
// (WP-9-tehtävä, docs/paketit.md), ja raja on tarkka kuten ilman suodatusta.
void test_row5_drift_filter_keeps_threshold_and_rates() {
  const StatusParams def = params();
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, def.drift_tau_s);   // oletus suodattaa
  const float settle = 4.0f * def.drift_tau_s;
  int flagged = 0, counted = 0;
  g_lcg = 4242u;
  driftRun(30.0f, 0.02f, 0.1f, 3.0f, flagged, counted, def, settle);
  TEST_ASSERT_GREATER_THAN(80, counted);
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  driftRun(-30.0f, 0.02f, 0.1f, 3.0f, flagged, counted, def, settle);
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  driftRun(10.0f, 0.02f, 0.1f, 3.0f, flagged, counted, def, 0.0f);   // 20 / 2 s
  TEST_ASSERT_EQUAL_INT(0, flagged);
  driftRun(24.0f, 0.02f, 0.1f, 0.0f, flagged, counted, def, 0.0f);   // 48 / 2 s
  TEST_ASSERT_EQUAL_INT(0, flagged);
  driftRun(26.0f, 0.02f, 0.1f, 0.0f, flagged, counted, def, settle);  // 52 / 2 s
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  driftRun(26.0f, 0.1f, 0.1f, 0.0f, flagged, counted, def, settle);   // 10 Hz
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  driftRun(26.0f, 0.02f, 0.02f, 0.0f, flagged, counted, def, settle);  // 50 Hz
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  // Raja tarkka ±0,4 rpm / 2 s epätasaisella dt:llä. Tavallinen EMA (nollannen
  // kertaluvun pito) viivästäisi rampin s * (tau - dt/2):n verran, jolloin
  // viive vaihtelisi dt:n mukana (0,02-0,1 s: ~1 rpm 25 rpm/s:lla) ja raja
  // heiluisi; first-order hold suodattaa rampin tarkasti. Asettuminen 5 tau.
  driftRun(25.2f, 0.02f, 0.1f, 0.0f, flagged, counted, def, 5.0f * def.drift_tau_s);   // 50,4
  TEST_ASSERT_GREATER_THAN(60, counted);
  TEST_ASSERT_EQUAL_INT(counted, flagged);
  driftRun(24.8f, 0.02f, 0.1f, 0.0f, flagged, counted, def, 0.0f);   // 49,6
  TEST_ASSERT_EQUAL_INT(0, flagged);
}

// Rivi 5 + WP-9: päivitystaajuus vaihtuu kesken rampin (status.h: "toimii
// 10-50 Hz:llä ja epätasaisella dt:llä"), 10 <-> 50 Hz 2 s:n jaksoissa.
// Tavallisen EMA:n viive s * (tau - dt/2) olisi 0,45 s / 0,49 s jakson mukaan,
// jolloin 50,4 rpm / 2 s näkyisi 49,4:nä ja 49,6 voisi näkyä yli 50:n.
// First-order hold suodattaa rampin tarkasti, joten raja pysyy.
void test_row5_drift_filter_exact_when_update_rate_changes() {
  const StatusParams def = params();
  const float slopes[] = {25.2f, 24.8f};   // 50,4 ja 49,6 rpm / 2 s
  for (float slope : slopes) {
    g.begin(def);
    int flagged = 0, counted = 0;
    float t = 0.0f;
    while (t < 14.0f) {
      const float dt = (static_cast<int>(t / 2.0f) % 2 == 0) ? 0.1f : 0.02f;
      StatusInput in = running(-55.0f, -55.4f, -54.8f, 900.0f + slope * t);
      in.now_s = t;
      const StatusResult r = g.sm.update(in);
      if (t > def.drift_window_s + 6.0f * def.drift_tau_s) {
        ++counted;
        if (r.flag_drift) ++flagged;
      }
      t += dt;
    }
    TEST_ASSERT_GREATER_THAN(100, counted);
    TEST_ASSERT_EQUAL_INT(slope > 25.0f ? counted : 0, flagged);
  }
}

// Rivi 5 + WP-9: päivityskohtainen rpm-kohina ±30 rpm (tasajakauma) ylittää
// suodattamattomana rajan (|x(t) - x(t-2)| jopa 60 > 50), suodatettuna ei.
// Ramppi 60 rpm / 2 s saman kohinan päällä liputtaa yhä valtaosan ajasta
// (marginaali rajaan on 10 rpm, suodatetun kohinan erotuksen hajonta ~6 rpm;
// kohinaton ramppi aina: test_row5_drift_filter_keeps_threshold_and_rates).
// EngineSimin realistinen kohina (DSP:n 8 syklin rpm, jitter): test_integration
// test_wp9_drift_ignores_rpm_noise.
void test_row5_drift_filter_rejects_update_noise() {
  int flagged = 0, counted = 0;
  g_lcg = 99u;
  driftRun(0.0f, 0.0625f, 0.0625f, 30.0f, flagged, counted, unfiltered());
  TEST_ASSERT_GREATER_THAN(0, flagged);   // ongelma: suodattamaton reagoi kohinaan
  const StatusParams def = params();
  g_lcg = 99u;
  driftRun(0.0f, 0.0625f, 0.0625f, 30.0f, flagged, counted, def);
  TEST_ASSERT_GREATER_THAN(100, counted);
  TEST_ASSERT_EQUAL_INT(0, flagged);
  g_lcg = 99u;
  driftRun(30.0f, 0.0625f, 0.0625f, 30.0f, flagged, counted, def, 4.0f * def.drift_tau_s);
  TEST_ASSERT_GREATER_THAN(counted * 3 / 4, flagged);
}

void test_row5_drift_history_cleared_when_rpm_lost() {
  settle(idleSynced());
  g.run(engineOff(), 2.0f);   // rpm ei voimassa -> historia pois
  // Moottori käynnistyy eri kierroksille: ei vertailukohtaa -> ei ajautumista.
  StatusResult r = g.run(running(-55.0f, -55.4f, -54.8f, 1500.0f), 1.5f);
  TEST_ASSERT_FALSE(r.flag_drift);
  // Rivi 5: kierrosluku pysyy 1500:ssa, joten lippu ei nouse myöhemminkään.
  // WP-9: myös suodattimen tila alkaa alusta (vanha 850 rpm ei saa vetää
  // suodatettua arvoa ja näkyä 2 s myöhemmin 650 rpm:n "ajautumana").
  for (int k = 0; k < 64; ++k) {
    r = g.step(running(-55.0f, -55.4f, -54.8f, 1500.0f));
    TEST_ASSERT_FALSE(r.flag_drift);
  }
}

// ---------------------------------------------------------------------------
// Rivi 6: letkuvika
// ---------------------------------------------------------------------------

void test_row6_hose_off() {
  StatusInput in = idleSynced();
  dropPulse(in, 1, -0.5f);
  StatusResult r = hoseHold(in);
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

void test_row6_hose_off_needs_hold() {
  // Käynnistyksessä ja sammutuksessa kanavat saavat ja menettävät pulssin
  // eri sykleillä, ja matalalla alipaineella taso on jo nollan tuntumassa.
  // Ensin "odota", letkuvika vasta kHoseHoldS:n jälkeen.
  StatusInput in = idleSynced();
  settle(in);
  dropPulse(in, 1, -0.5f);
  StatusResult r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  assertNoVerdict(r);
  r = g.run(in, sbx::StatusMachine::kHoseHoldS - 2.0f * kDt);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  r = g.run(in, 2.0f * kDt);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  // Pulssi palaa ennen pitoaikaa: ei vikaa koskaan.
  g.begin();
  in = idleSynced();
  settle(in);
  StatusInput off = in;
  dropPulse(off, 0, 0.5f);
  for (int i = 0; i < 6; ++i) {
    r = g.step(off);
    TEST_ASSERT_TRUE(r.state != SyncState::FaultHose && r.state != SyncState::FaultMaster);
  }
}

void test_row5_common_move_hides_verdict() {
  // Kaasu: taso-EMA karkaa syklikeskiarvoista kaikissa kanavissa -> odota,
  // ei tuomiota (keskiarvot päivittyvät porrastetusti). Vakausajastin säilyy,
  // joten tuomio palaa heti liikkeen loputtua.
  StatusInput in = idleSynced();
  StatusResult r = settle(in);
  TEST_ASSERT_TRUE(r.show_verdict);
  StatusInput moving = in;
  for (int i = 0; i < kChannels; ++i) moving.ch[i].level_kpa = in.ch[i].mean_kpa + 3.0f;
  r = g.step(moving);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("Alipaine muuttuu kaikissa sylintereissä", r.advice);
  assertNoVerdict(r);
  assertTextSane(r);
  r = g.step(in);
  TEST_ASSERT_TRUE(r.show_verdict);
  // Yhden kanavan ruuvin säätö liikuttaa vain sitä: tuomio pysyy.
  StatusInput one = in;
  one.ch[2].level_kpa = in.ch[2].mean_kpa + 3.0f;
  r = g.step(one);
  TEST_ASSERT_TRUE(r.show_verdict);
}

void test_row6_threshold_and_blocked_hose_extension() {
  StatusInput in = idleSynced();
  dropPulse(in, 1, -1.9f);   // |taso| < hose_off_kpa (2,0)
  StatusResult r = hoseHold(in);
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

// Rivi 6 + jatko.md 7 (WP-9): poistumishystereesi. Taso tasan rajalla
// (esim. irti oleva letku, jonka anturin nolla on ryöminyt -2 kPa:iin) ja
// kohina ±0,02 kPa (taso-EMA:n vaihteluväli 0,024-0,047 kPa, mitattu).
// Sisääntulosääntö ennallaan (|taso| < hose_off_kpa yhtäjaksoisesti kHoseHoldS).
StatusInput hoseAtEdge(int k) {
  StatusInput in = idleSynced();
  dropPulse(in, 1, (k & 1) ? -1.98f : -2.02f);
  return in;
}

void test_row6_hose_fault_does_not_flicker_at_threshold() {
  // Ennen (hose_hyst_kpa = 0, WP-4:n käytös): VIKA <-> "Odota" off_hold_s:n
  // sisällä ja otsikko "ei alipainetta" <-> "ei pulssia" sen jälkeen.
  StatusParams p = params();
  p.hose_hyst_kpa = 0.0f;
  g.begin(p);
  settle(idleSynced());
  int state_changes = 0, head_changes = 0;
  StatusResult prev = g.step(hoseAtEdge(0));
  for (int k = 1; k < 160; ++k) {   // 10 s
    const StatusResult r = g.step(hoseAtEdge(k));
    if (r.state != prev.state) ++state_changes;
    if (std::strcmp(r.headline, prev.headline) != 0) ++head_changes;
    prev = r;
  }
  TEST_ASSERT_GREATER_THAN(10, state_changes);
  TEST_ASSERT_GREATER_THAN(50, head_changes);
  // Oletus (hystereesi): kerran sisään, pysyy - sama vika, sama teksti.
  g.begin();
  settle(idleSynced());
  int entries = 0;
  bool in_fault = false;
  for (int k = 0; k < 160; ++k) {
    const StatusResult r = g.step(hoseAtEdge(k));
    if (r.state == SyncState::FaultHose && !in_fault) ++entries;
    if (in_fault) {
      TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
      TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei alipainetta, tarkista letku", r.headline);
    }
    in_fault = r.state == SyncState::FaultHose;
  }
  TEST_ASSERT_EQUAL_INT(1, entries);
}

void test_row6_hysteresis_exit_and_reentry() {
  const StatusParams def = params();
  const float exit_kpa = def.hose_off_kpa + def.hose_hyst_kpa;   // 2,2
  StatusInput in = idleSynced();
  settle(in);
  dropPulse(in, 1, -1.5f);
  StatusResult r = hoseHold(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  // Raja ja raja + hystereesi välissä: pysyy letkuvikana ("ei alipainetta").
  dropPulse(in, 1, -(exit_kpa - 0.02f));
  r = g.run(in, 5.0f);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei alipainetta, tarkista letku", r.headline);
  // Raja + hystereesi ylittyy: ei enää "irti" vaan rivin 6 laajennus (tukossa).
  dropPulse(in, 1, -(exit_kpa + 0.02f));
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei pulssia, tarkista letku", r.headline);
  // Takaisin hystereesialueelle: sisääntulo vaatii taas |taso| < hose_off_kpa.
  dropPulse(in, 1, -(exit_kpa - 0.02f));
  r = g.step(in);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei pulssia, tarkista letku", r.headline);
  dropPulse(in, 1, -(def.hose_off_kpa - 0.02f));
  r = g.step(in);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei alipainetta, tarkista letku", r.headline);
  // Pulssi palaa -> muisti nollautuu: uusi katko hystereesialueella ei ole
  // "irti" (sisääntulosääntö), vaan ensin "odota" ja off_hold_s:n jälkeen tukos.
  r = settle(idleSynced());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
  in = idleSynced();
  dropPulse(in, 1, -(exit_kpa - 0.02f));
  r = g.run(in, 1.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
}

// Rivi 6 + D7: masterin vaihto (configure) aloittaa uuden jakson, jossa myös
// hystereesin muisti on tyhjä (§4.7: "Masterin vaihto nollaa ajastimet").
// Hystereesialueella (2,0-2,2 kPa) oleva kanava ei siksi palaa "irti"-tilaan,
// vaan sisääntulosääntö pätee alusta: ensin "odota", off_hold_s:n jälkeen tukos.
void test_row6_hysteresis_memory_cleared_by_configure() {
  const StatusParams def = params();
  StatusInput in = idleSynced();
  settle(in);
  dropPulse(in, 1, -1.5f);
  StatusResult r = hoseHold(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  dropPulse(in, 1, -(def.hose_off_kpa + 0.1f));   // hystereesialueella: pysyy "irti"
  r = g.step(in);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei alipainetta, tarkista letku", r.headline);
  StatusParams p = params();
  p.master = 2;
  g.sm.configure(p);
  r = g.run(in, 1.0f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  r = g.run(in, def.off_hold_s);
  TEST_ASSERT_EQUAL_STRING("VIKA: SYL 2 – ei pulssia, tarkista letku", r.headline);
}

// Rivi 6 vs. 7: anturivian aika ei ole "pulssittomuutta". Kun anturi toipuu
// (esim. löysä liitin, jatko.md: SYL 3 0-300 mV), DSP tarvitsee hetken
// lukitukseen kuten käynnistyksessä, eikä VIKA saa välähtää (§4.7: HoseOff
// vasta kHoseHoldS:n, tukos vasta off_hold_s:n pulssittomuuden jälkeen).
// Tappaa mutaation, jossa lost-ehdosta puuttuu !sensor_fault (WP-4:n selviäjä).
void test_row6_hold_times_restart_after_sensor_fault() {
  settle(idleSynced());
  StatusInput in = idleSynced();
  sensorFault(in, 2, 12);
  StatusResult r = g.run(in, 5.0f);   // > off_hold_s
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultSensor), static_cast<int>(r.state));
  // Anturi toipui, pulssia ei vielä tunnistettu. Taso-EMA joko vielä
  // alipaineessa (tukosta muistuttava) tai nollan tuntumassa (irti-tyyppinen).
  const float levels[] = {-30.0f, -0.5f};
  for (float lv : levels) {
    g.begin();
    settle(idleSynced());
    g.run(in, 5.0f);
    StatusInput rec = idleSynced();
    dropPulse(rec, 2, lv);
    r = g.step(rec);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
    r = g.run(rec, sbx::StatusMachine::kHoseHoldS - 2.0f * kDt);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  }
}

void test_row6_two_hoses_off() {
  StatusInput in = idleSynced();
  dropPulse(in, 1, 0.2f);
  dropPulse(in, 2, -0.3f);
  StatusResult r = hoseHold(in);
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

void test_row10_saturation_needs_hold() {
  // Käynnistyksessä ja sammutuksessa DSP:n saturated (1 s muisti
  // ilmanpaineesta) ja has_pulse ovat päällekkäin alle 1 s: ei varoitusta.
  // Aito leikkautuminen jatkuu, ja lippu nousee kHoseHoldS:n tapaan viiveellä.
  StatusInput in = idleSynced();
  settle(in);
  in.ch[1].saturated = true;
  StatusResult r = g.run(in, 1.0f);
  TEST_ASSERT_FALSE(r.flag_saturation);
  r = g.run(in, sbx::StatusMachine::kSatHoldS);
  TEST_ASSERT_TRUE(r.flag_saturation);
  in.ch[1].saturated = false;
  r = g.step(in);
  TEST_ASSERT_FALSE(r.flag_saturation);
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
  // D11: ero voi olla pelkkää offsetia -> nollausohje, ei ruuvineuvoa.
  TEST_ASSERT_EQUAL_INT(-1, r.act_ch);
  assertTextSane(r);
  // Kun nollaus on tehty, sama tilanne on vihreä.
  in.zero_valid = true;
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Synced), static_cast<int>(r.state));
}

void test_d11_manual_zero_advice_and_red_stays_red() {
  StatusParams p = params();
  p.auto_zero = false;
  g.begin(p);
  StatusInput in = running(-55.0f, -55.2f, -54.8f);
  in.zero_valid = false;
  StatusResult r = settle(in);
  // Ei "irrota letkut": tuomio näkyy vain käyvällä moottorilla, ja irrotettu
  // letku päästää imusarjaan vuotoilmaa (docs/kayttoohje.md §1, moottoriajo.md §2).
  TEST_ASSERT_EQUAL_STRING("Nollaus puuttuu – sammuta moottori ja paina Nollaa", r.advice);
  assertTextSane(r);
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
  StatusParams p = params();
  p.master = 1;
  g.begin(p);
  StatusResult r = hoseHold(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultMaster), static_cast<int>(r.state));
  TEST_ASSERT_EQUAL_INT(1, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("Master (SYL 2) ei kelpaa viitteeksi – valitse toinen", r.headline);
  TEST_ASSERT_EQUAL_STRING("SYL 2: ei alipainetta – tarkista letku", r.advice);
  // Käyttäjä vaihtaa masterin (rivi 12): sama kanava on nyt tavallinen letkuvika.
  // Vaihto nollaa ajastimet, joten pitoaika (kHoseHoldS) alkaa alusta.
  p.master = 0;
  g.sm.configure(p);
  r = hoseHold(in);
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
// Kanavakohtainen luokitus (ch_cond, snapshotin ch[i].cond, WP-10)
// ---------------------------------------------------------------------------

using sbx::ChannelCond;

void assertConds(const StatusResult& r, ChannelCond c0, ChannelCond c1, ChannelCond c2) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(c0), static_cast<int>(r.ch_cond[0]));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(c1), static_cast<int>(r.ch_cond[1]));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(c2), static_cast<int>(r.ch_cond[2]));
}

void test_channel_cond_follows_matrix_rows() {
  // Arvo kertoo kanavan oman tilan samoilla pitoajoilla kuin tila (firmware.md §5).
  const ChannelCond ok = ChannelCond::Ok;
  StatusResult r = g.step(engineOff());
  assertConds(r, ChannelCond::Idle, ChannelCond::Idle, ChannelCond::Idle);   // rivit 1+8, D10
  r = settle(idleSynced());
  assertConds(r, ok, ok, ok);
  // Rivi 6: pitoajan (kHoseHoldS) ajan ohimenevä, sitten letkuvika.
  StatusInput in = idleSynced();
  dropPulse(in, 1, -0.5f);
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Unstable), static_cast<int>(r.state));
  assertConds(r, ok, ChannelCond::NoPulse, ok);
  r = hoseHold(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultHose), static_cast<int>(r.state));
  assertConds(r, ok, ChannelCond::Hose, ok);
  // Rivin 6 laajennus: taso ei nollassa, off_hold_s jälkeen tukos.
  g.begin();
  in = idleSynced();
  dropPulse(in, 2, -30.0f);
  r = g.run(in, 3.5f);
  assertConds(r, ok, ok, ChannelCond::Blocked);
  // Rivi 7.
  in = idleSynced();
  sensorFault(in, 0, 3300);
  r = g.step(in);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::FaultMaster), static_cast<int>(r.state));
  assertConds(r, ChannelCond::Sensor, ok, ok);   // rivi 12: masterin oma vika näkyy
}

void test_channel_cond_sat_needs_hold_and_own_clipping() {
  // Rivi 10: Sat vasta kun flag_saturation on noussut (kSatHoldS), ja vain
  // kanavalle, joka itse leikkautuu.
  StatusInput in = idleSynced();
  settle(in);
  in.ch[1].saturated = true;
  StatusResult r = g.run(in, 1.0f);
  TEST_ASSERT_FALSE(r.flag_saturation);
  assertConds(r, ChannelCond::Ok, ChannelCond::Ok, ChannelCond::Ok);
  r = g.run(in, sbx::StatusMachine::kSatHoldS);
  TEST_ASSERT_TRUE(r.flag_saturation);
  assertConds(r, ChannelCond::Ok, ChannelCond::Sat, ChannelCond::Ok);
  // Ilmanpaine (GN-anturi leikkausrajalla) ilman pulssia ei ole rivin 10 ehto.
  StatusInput off = engineOff();
  for (int i = 0; i < kChannels; ++i) off.ch[i].saturated = true;
  r = g.step(off);
  assertConds(r, ChannelCond::Idle, ChannelCond::Idle, ChannelCond::Idle);
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
  StatusResult r = hoseHold(in);
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
  StatusResult r = hoseHold(in);
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
  StatusParams p = params();
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
  p = params();
  p.stable_hold_s = 30.0f;
  g.begin(p);
  r = g.run(idleSynced(), 29.9375f);
  TEST_ASSERT_EQUAL_STRING("Vakautta odotetaan: 29,9 / 30,0 s", r.advice);
  assertTextSane(r);

  // Matala rpm: alarajan konfiguraation ylärajalla.
  p = params();
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
  StatusParams p = params();
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
      a.request_auto_zero != b.request_auto_zero || a.act_ch != b.act_ch ||
      a.act_open != b.act_open) {
    return false;
  }
  if (std::memcmp(a.ch_cond, b.ch_cond, sizeof(a.ch_cond)) != 0) return false;
  if (std::memcmp(&a.spread_kpa, &b.spread_kpa, sizeof(float)) != 0) return false;
  if (std::memcmp(a.vac_delta_kpa, b.vac_delta_kpa, sizeof(a.vac_delta_kpa)) != 0) return false;
  return std::strcmp(a.headline, b.headline) == 0 && std::strcmp(a.advice, b.advice) == 0;
}

void test_deterministic() {
  // Kaksi konetta, sama syötesarja (sammutus, käynnistys, ajautuminen, vika)
  // -> bitilleen samat tulokset.
  StatusMachine a;
  StatusMachine b;
  a.configure(params());
  b.configure(params());
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
  RUN_TEST(test_row1_no_auto_zero_in_fault_state_even_near_zero);
  RUN_TEST(test_row1_settle_gate_edges);
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
  RUN_TEST(test_row5_drift_filter_keeps_threshold_and_rates);
  RUN_TEST(test_row5_drift_filter_exact_when_update_rate_changes);
  RUN_TEST(test_row5_drift_filter_rejects_update_noise);
  RUN_TEST(test_row5_drift_history_cleared_when_rpm_lost);
  RUN_TEST(test_row6_hose_off);
  RUN_TEST(test_row6_hose_off_needs_hold);
  RUN_TEST(test_row5_common_move_hides_verdict);
  RUN_TEST(test_row6_threshold_and_blocked_hose_extension);
  RUN_TEST(test_row6_realistic_disconnect_sequence);
  RUN_TEST(test_row6_hose_fault_does_not_flicker_at_threshold);
  RUN_TEST(test_row6_hysteresis_exit_and_reentry);
  RUN_TEST(test_row6_hysteresis_memory_cleared_by_configure);
  RUN_TEST(test_row6_hold_times_restart_after_sensor_fault);
  RUN_TEST(test_row6_two_hoses_off);
  RUN_TEST(test_row6_mixed_hose_faults_generic_text);
  RUN_TEST(test_startup_and_shutdown_stagger_never_flash_fault);
  RUN_TEST(test_row7_sensor_fault);
  RUN_TEST(test_row7_multiple_sensor_faults);
  RUN_TEST(test_row7_sensor_fault_with_engine_off_beats_no_signal);
  RUN_TEST(test_row10_saturation_flag_is_independent_of_state);
  RUN_TEST(test_row10_atmosphere_is_not_pulse_saturation);
  RUN_TEST(test_row10_saturation_needs_hold);
  RUN_TEST(test_row11_uncal_gain_is_warning_only);
  RUN_TEST(test_d11_never_green_without_zero);
  RUN_TEST(test_d11_manual_zero_advice_and_red_stays_red);
  RUN_TEST(test_row12_master_sensor_fault);
  RUN_TEST(test_row12_master_hose_off_depends_on_master_choice);
  RUN_TEST(test_row12_master_blocked_hose);
  RUN_TEST(test_channel_cond_follows_matrix_rows);
  RUN_TEST(test_channel_cond_sat_needs_hold_and_own_clipping);
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
