/*
  test_portal_primary_action — portaalin toimintopalkin resolveri.

  Pure header (vain <stdint.h>), ei stubeja. Lukitsee prioriteettijarjestyksen
  (FAULT > askelkuittaus > vaihesiirtyma > aloitus > advisory > ei mitaan)
  ja sen etta toimintoja on aina TASAN yksi — koko moduulin olemassaolon syy
  on rautatestin 16.7.2026 havainto kahdesta kilpailevasta kaynnistysnapista.

  Palvelee: wifi_portal_routes_get.h /api/status "primary_action" +
  wifi_portal_html_script.h renderActionBar(). Suunnitelma:
  docs/kehitys/web-ui-uudelleensuunnittelu.md §3.2.
*/

#include <unity.h>

#include "portal_primary_action.h"

void setUp() {}
void tearDown() {}

// Apuri: kaikki liput false — "tuore IDLE, kayttoonotto tehty".
static PrimaryActionInput baseInput() {
  PrimaryActionInput in;
  in.fault = false;
  in.stepAckAllowed = false;
  in.phasePendingAdvance = false;
  in.advisoryActive = false;
  in.growActive = false;
  in.onboarding = false;
  return in;
}

// ═══════════════════════════════════════════════════════════════════════════
// Perustilat
// ═══════════════════════════════════════════════════════════════════════════

// IDLE + kayttoonotto tehty + ei kasvatusta -> tarjoa aloitus. Tama on
// kartoituksen §6 eksplisiittinen ele — resolveri tarjoaa, ei kaynnista.
void test_idle_onboarded_offers_start(void) {
  PrimaryActionInput in = baseInput();
  TEST_ASSERT_EQUAL(PRIMARY_ACTION_START_GROW, portal_resolvePrimaryAction(&in));
}

// GROWING ja kaikki hyvin -> ei nappia. Palkki ei keksi tekemista tyhjasta.
void test_growing_all_ok_is_none(void) {
  PrimaryActionInput in = baseInput();
  in.growActive = true;
  TEST_ASSERT_EQUAL(PRIMARY_ACTION_NONE, portal_resolvePrimaryAction(&in));
}

// Onboarding kesken -> aina NONE, myos kun kasvatus ei ole kaynnissa.
// /setup-sivu omistaa kayttoonoton UX:n; paasivu ei saa tarjota aloitusta
// ennen kuin pakolliset valinnat (kasvi + aloitustapa) on tehty.
void test_onboarding_suppresses_everything(void) {
  PrimaryActionInput in = baseInput();
  // Esiehto: ilman onboarding-lippua sama syote tarjoaisi aloituksen.
  TEST_ASSERT_EQUAL(PRIMARY_ACTION_START_GROW, portal_resolvePrimaryAction(&in));
  in.onboarding = true;
  in.fault = true;  // jopa FAULT: paasivun palkki vaikenee, setup-sivu ohjaa
  TEST_ASSERT_EQUAL(PRIMARY_ACTION_NONE, portal_resolvePrimaryAction(&in));
}

// NULL-syote ei kaada — palauttaa NONE (sama kaava kuin muissa resolvereissa).
void test_null_input_is_none(void) {
  TEST_ASSERT_EQUAL(PRIMARY_ACTION_NONE, portal_resolvePrimaryAction(NULL));
}

// ═══════════════════════════════════════════════════════════════════════════
// Prioriteettijarjestys — ylin voittaa
// ═══════════════════════════════════════════════════════════════════════════

// FAULT voittaa kaiken muun: lukitusta ei voi ohittaa kuittaamalla askelta.
void test_fault_wins_all(void) {
  PrimaryActionInput in = baseInput();
  in.fault = true;
  in.stepAckAllowed = true;
  in.phasePendingAdvance = true;
  in.advisoryActive = true;
  in.growActive = true;
  TEST_ASSERT_EQUAL(PRIMARY_ACTION_CLEAR_FAULT, portal_resolvePrimaryAction(&in));
}

// Askelkuittaus voittaa vaihesiirtyman: askel on kesken oleva fyysinen tyo
// (esim. "valuta ja kylva"), siirtyma voi odottaa sen yli.
void test_step_ack_beats_phase_pending(void) {
  PrimaryActionInput in = baseInput();
  in.growActive = true;
  in.stepAckAllowed = true;
  in.phasePendingAdvance = true;
  in.advisoryActive = true;
  TEST_ASSERT_EQUAL(PRIMARY_ACTION_STEP_ACK, portal_resolvePrimaryAction(&in));
}

// Vaihesiirtyma voittaa advisoryn: siirtymassa on tekeminen, advisoryssa ei.
void test_phase_pending_beats_advisory(void) {
  PrimaryActionInput in = baseInput();
  in.growActive = true;
  in.phasePendingAdvance = true;
  in.advisoryActive = true;
  TEST_ASSERT_EQUAL(PRIMARY_ACTION_APPROVE_PHASE, portal_resolvePrimaryAction(&in));
}

// Advisory nakyy kasvatuksen aikana kun mitaan kuitattavaa ei ole.
void test_advisory_shown_during_grow(void) {
  PrimaryActionInput in = baseInput();
  in.growActive = true;
  in.advisoryActive = true;
  TEST_ASSERT_EQUAL(PRIMARY_ACTION_ADVISORY, portal_resolvePrimaryAction(&in));
}

// KAANNETTY 17.7.2026 rautatestin jalkeen. Tama testi lukitsi ennen
// pain vastaisen vaitteen ("advisory voittaa aloitustarjouksen") ja
// perusteli sen silla etta "aloitus loytyy silti Kasvatus-nakymasta" —
// mika EI PIDA PAIKKAANSA: suunnitelman §3.5 poisti kaynnistysnapin
// Kasvatus-nakymasta juuri siksi etta aloitus tapahtuu vain palkista.
// Kun advisory soi palkin, aloitusta ei ollut missaan. Kayttajan laite
// oli tasmalleen tassa tilassa (MLX90614 irti): Koti neuvoi "Aloita
// kasvatus ylapalkista" ja ylapalkissa luki anturivaroitus ilman nappia.
//
// Periaate: advisorylla EI OLE nappia, joten se ei saa syrjayttaa
// toimintoa jolla on. Puuttuva anturi on advisory nimenomaan siksi ettei
// se estaisi toimintaa (architecture.md §8 Aukko B) — olisi nurinkurista
// jos se sitten estaisi sen UI:ssa.
void test_start_offer_beats_advisory(void) {
  PrimaryActionInput in = baseInput();
  in.advisoryActive = true;   // esim. MLX90614 ei vastaa
  in.growActive = false;      // IDLE, kayttoonotto tehty
  TEST_ASSERT_EQUAL_MESSAGE(PRIMARY_ACTION_START_GROW, portal_resolvePrimaryAction(&in),
      "advisory ei saa syrjayttaa aloitusnappia — se on ainoa paikka aloittaa");
}

// ═══════════════════════════════════════════════════════════════════════════
// Nimimappaus — JS-sopimus
// ═══════════════════════════════════════════════════════════════════════════

// /api/status "primary_action" -arvot ovat JS:n renderActionBar():n avaimia.
// Lukitaan merkkijonot: muutos rikkoisi selainpaan hiljaa.
void test_action_names_are_stable(void) {
  TEST_ASSERT_EQUAL_STRING("none",          portal_primaryActionName(PRIMARY_ACTION_NONE));
  TEST_ASSERT_EQUAL_STRING("clear_fault",   portal_primaryActionName(PRIMARY_ACTION_CLEAR_FAULT));
  TEST_ASSERT_EQUAL_STRING("step_ack",      portal_primaryActionName(PRIMARY_ACTION_STEP_ACK));
  TEST_ASSERT_EQUAL_STRING("approve_phase", portal_primaryActionName(PRIMARY_ACTION_APPROVE_PHASE));
  TEST_ASSERT_EQUAL_STRING("advisory",      portal_primaryActionName(PRIMARY_ACTION_ADVISORY));
  TEST_ASSERT_EQUAL_STRING("start_grow",    portal_primaryActionName(PRIMARY_ACTION_START_GROW));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_onboarded_offers_start);
  RUN_TEST(test_growing_all_ok_is_none);
  RUN_TEST(test_onboarding_suppresses_everything);
  RUN_TEST(test_null_input_is_none);
  RUN_TEST(test_fault_wins_all);
  RUN_TEST(test_step_ack_beats_phase_pending);
  RUN_TEST(test_phase_pending_beats_advisory);
  RUN_TEST(test_advisory_shown_during_grow);
  RUN_TEST(test_start_offer_beats_advisory);
  RUN_TEST(test_action_names_are_stable);
  return UNITY_END();
}
