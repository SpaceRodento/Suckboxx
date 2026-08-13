// Priority-order tests for resolveAdvisory() (resolve_advisory.h), the
// COACH profile's one-line "what should I do" synthesis. Each test builds
// a DisplayData with exactly the fields needed to trigger one advisory
// level and asserts both the message and the level, so a future reordering
// of the priority chain in resolve_advisory.h shows up here first.
#include <string.h>
#include <unity.h>

#include "resolve_advisory.h"

void setUp() {}
void tearDown() {}

static DisplayData makeBaseData() {
  DisplayData d = {};
  d.dataValid = true;
  d.waterLevelOk = true;
  strncpy(d.deviceStateName, "IDLE", sizeof(d.deviceStateName) - 1);
  return d;
}

// ── Priority 0: no data yet ─────────────────────────────────────────

void test_no_data_shows_waiting() {
  DisplayData d = makeBaseData();
  d.dataValid = false;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_INFO, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_WAITING, r.message);
}

// ── Priority 1: device FAULT beats everything else ──────────────────

void test_device_fault_with_message() {
  DisplayData d = makeBaseData();
  strncpy(d.deviceStateName, "FAULT", sizeof(d.deviceStateName) - 1);
  strncpy(d.deviceFaultMsg, "Anturi ei vastaa", sizeof(d.deviceFaultMsg) - 1);
  // Also set a low water level to prove FAULT still wins.
  d.waterLevelOk = false;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_FAULT, r.level);
  char expected[64];
  snprintf(expected, sizeof(expected), TXT_ADV_FAULT, "Anturi ei vastaa");
  TEST_ASSERT_EQUAL_STRING(expected, r.message);
}

void test_device_fault_without_message_uses_generic() {
  DisplayData d = makeBaseData();
  strncpy(d.deviceStateName, "FAULT", sizeof(d.deviceStateName) - 1);
  d.deviceFaultMsg[0] = '\0';
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_FAULT, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_FAULT_GENERIC, r.message);
}

// ── Priority 2: ebb&flow water fault beats water-level and VPD ──────

void test_ebb_fault_beats_vpd() {
  DisplayData d = makeBaseData();
  d.ebbFault = true;
  d.vpdValid = true;
  d.vpdKpa = 5.0f;  // wildly out of comfort band too
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_FAULT, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_WATER_LOW, r.message);
}

// ── Priority 3: water level low beats VPD/PPFD ───────────────────────

void test_water_low_beats_vpd() {
  DisplayData d = makeBaseData();
  d.waterLevelOk = false;
  d.vpdValid = true;
  d.vpdKpa = 5.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_WATER_LOW, r.message);
}

// ── Priority 4: VPD comfort band ─────────────────────────────────────

void test_vpd_below_comfort_band() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = COACH_VPD_COMFORT_MIN_KPA - 0.1f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_VPD_LOW, r.message);
}

void test_vpd_above_comfort_band() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = COACH_VPD_COMFORT_MAX_KPA + 0.1f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_VPD_HIGH, r.message);
}

void test_vpd_inside_comfort_band_is_ok() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = (COACH_VPD_COMFORT_MIN_KPA + COACH_VPD_COMFORT_MAX_KPA) / 2.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_OK, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_OK, r.message);
}

void test_vpd_invalid_is_skipped_not_flagged() {
  DisplayData d = makeBaseData();
  d.vpdValid = false;  // sensor absent -> must not trigger VPD advisory
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_OK, r.level);
}

// ── Priority 5: PPFD too low while lights are on ─────────────────────

void test_ppfd_low_while_lights_on() {
  DisplayData d = makeBaseData();
  d.lightsOn = true;
  d.ppfdValid = true;
  d.ppfd = COACH_PPFD_LIGHTS_ON_MIN_UMOL - 1.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_LIGHT_LOW, r.message);
}

void test_ppfd_low_while_lights_off_is_ok() {
  DisplayData d = makeBaseData();
  d.lightsOn = false;
  d.ppfdValid = true;
  d.ppfd = 0.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_OK, r.level);
}

// ── Priority 6: everything nominal -> "all good" ─────────────────────

void test_all_nominal_is_ok() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = 1.0f;
  d.lightsOn = true;
  d.ppfdValid = true;
  d.ppfd = 300.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_OK, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_OK, r.message);
}

// ── Priority 3: maintenance ────────────────────────────────────────

void test_maintenance_shows_service_banner() {
  DisplayData d = makeBaseData();
  strncpy(d.deviceStateName, "MAINTENANCE", sizeof(d.deviceStateName) - 1);
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL(ADVISORY_INFO, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_MAINTENANCE, r.message);
}

void test_maintenance_beats_water_low() {
  // An open, low reservoir is expected during service — telling the operator to
  // refill while they are refilling is noise; the pump lock is the useful fact.
  DisplayData d = makeBaseData();
  strncpy(d.deviceStateName, "MAINTENANCE", sizeof(d.deviceStateName) - 1);
  d.waterLevelOk = false;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_MAINTENANCE, r.message);
}

void test_fault_still_beats_maintenance() {
  DisplayData d = makeBaseData();
  strncpy(d.deviceStateName, "FAULT", sizeof(d.deviceStateName) - 1);
  strncpy(d.deviceFaultMsg, "Ylivuoto", sizeof(d.deviceFaultMsg) - 1);
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL(ADVISORY_FAULT, r.level);
}

// ── Vaiheohje: nakyy "kaikki hyvin" -rivin TILALLA, ei halytysten yli ──
//
// Nama testit vartioivat suunnittelupaatosta: ohje on saman vaiheen neuvo
// joka patee viikkoja, kun taas vesi/VPD vaativat toimia nyt. Jos ohje
// nousisi niiden ylitse, seinataulu neuvoisi "seuraa lehtien varia" samalla
// kun sailio on tyhja.

void test_phase_action_replaces_all_good_when_nothing_is_wrong() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  strcpy(d.growAction, "Pida kannen alla lammossa, ala kaivele");
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_OK, r.level);
  TEST_ASSERT_EQUAL_STRING("Pida kannen alla lammossa, ala kaivele", r.message);
}

void test_water_low_beats_phase_action() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  strcpy(d.growAction, "Seuraa lehtien varia ja vedenkulutusta");
  d.waterLevelOk = false;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_WATER_LOW, r.message);
}

void test_vpd_out_of_band_beats_phase_action() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  strcpy(d.growAction, "Seuraa lehtien varia ja vedenkulutusta");
  d.vpdValid = true;
  d.vpdKpa = COACH_VPD_COMFORT_MAX_KPA + 0.5f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_VPD_HIGH, r.message);
}

void test_fault_beats_phase_action() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  strcpy(d.growAction, "Seuraa lehtien varia ja vedenkulutusta");
  strcpy(d.deviceStateName, "FAULT");
  strcpy(d.deviceFaultMsg, "Ylivuoto");
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_FAULT, r.level);
}

// Vahvistuspyynto on ainoa kohta jossa laite odottaa kayttajalta paatosta —
// se voittaa tavallisen vaiheohjeen.
void test_pending_advance_beats_phase_action() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.growPendingAdvance = true;
  strcpy(d.growAction, "Seuraa lehtien varia ja vedenkulutusta");
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_INFO, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_PHASE_DONE, r.message);
}

// Ei kasvatusta -> ei ohjetta. Tyhja growAction ei saa tulostua tyhjana rivina.
void test_no_grow_falls_back_to_all_good() {
  DisplayData d = makeBaseData();
  d.growActive = false;
  strcpy(d.growAction, "Seuraa lehtien varia ja vedenkulutusta");
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_OK, r.message);
}

void test_grow_active_but_empty_action_falls_back_to_all_good() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.growAction[0] = '\0';
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_OK, r.message);
}

// Banneri on yksi rivi 12pt-fontilla (~45 merkkia) — pidempi teksti leikkautuisi
// paneelilla. Fonttikoon pienennys hylattiin rautatestissa 10.7.2026, joten
// raja on rautatestattu tosiasia eika mielipide: jos joku kirjoittaa pidemman
// ohjeen grow_shortAction():iin, tama testi kaatuu ennen kuin se paatyy seinalle.
void test_all_shipped_banner_texts_fit_one_line() {
  const char* texts[] = {
    TXT_ADV_OK, TXT_ADV_PHASE_DONE, TXT_ADV_MAINTENANCE,
    TXT_ADV_WATER_LOW, TXT_ADV_VPD_LOW, TXT_ADV_VPD_HIGH, TXT_ADV_LIGHT_LOW,
    TXT_ADV_HEAT_HIGH, TXT_ADV_LIGHT_HIGH,
  };
  for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
    TEST_ASSERT_TRUE_MESSAGE(strlen(texts[i]) <= 45, texts[i]);
  }
}

// ── resolveCriticalAlert(): STATUS-profiilin jaettu turva-/tilahalytys ──────
//
// Sama prioriteetti kuin resolveAdvisory FAULT/vesi/huolto/vaihe-valmis-riveille,
// MUTTA sensori-mukavuus (VPD/PPFD) ja vaiheohje (growAction) EIVAT laukaise sita
// — STATUS:n PE-paneeli hoitaa VPD/PPFD:n per-vaihe-tarkasti ja growAction on jo
// tilalause. Nama testit vartioivat juuri sita eroa.

void test_critical_fault_with_message() {
  DisplayData d = makeBaseData();
  strncpy(d.deviceStateName, "FAULT", sizeof(d.deviceStateName) - 1);
  strncpy(d.deviceFaultMsg, "Ylivuoto", sizeof(d.deviceFaultMsg) - 1);
  AdvisoryResult r = resolveCriticalAlert(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_FAULT, r.level);
  char expected[64];
  snprintf(expected, sizeof(expected), TXT_ADV_FAULT, "Ylivuoto");
  TEST_ASSERT_EQUAL_STRING(expected, r.message);
}

void test_critical_maintenance() {
  DisplayData d = makeBaseData();
  strncpy(d.deviceStateName, "MAINTENANCE", sizeof(d.deviceStateName) - 1);
  d.waterLevelOk = false;   // matala sailio odotettua huollon aikana
  AdvisoryResult r = resolveCriticalAlert(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_INFO, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_MAINTENANCE, r.message);  // voittaa vesi-rivin
}

void test_critical_ebb_fault_and_water_low() {
  DisplayData d = makeBaseData();
  d.ebbFault = true;
  AdvisoryResult r = resolveCriticalAlert(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_FAULT, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_WATER_LOW, r.message);

  DisplayData d2 = makeBaseData();
  d2.waterLevelOk = false;
  AdvisoryResult r2 = resolveCriticalAlert(&d2);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r2.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_WATER_LOW, r2.message);
}

void test_critical_pending_advance() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.growPendingAdvance = true;
  AdvisoryResult r = resolveCriticalAlert(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_INFO, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_PHASE_DONE, r.message);
}

// KRIITTINEN ERO resolveAdvisoryyn: VPD/PPFD-poikkeama EI ole kriittinen
// halytys — STATUS-paneelin PE-rivit nayttavat sen per-vaihe-tarkasti.
void test_critical_ignores_vpd_and_ppfd() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = COACH_VPD_COMFORT_MAX_KPA + 1.0f;   // rajusti yli
  d.lightsOn = true;
  d.ppfdValid = true;
  d.ppfd = 1.0f;                                  // valo paalla, PPFD lattiassa
  AdvisoryResult r = resolveCriticalAlert(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_OK, r.level);    // ei kriittista -> normaali tilalause
  TEST_ASSERT_EQUAL_STRING("", r.message);
}

// growAction EI nay kriittisena — se on paneelin normaali tilalause.
void test_critical_ok_when_only_grow_action() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  strcpy(d.growAction, "Taimi valossa, ei viela vesikiertoa");
  AdvisoryResult r = resolveCriticalAlert(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_OK, r.level);
  TEST_ASSERT_EQUAL_STRING("", r.message);
}

void test_critical_ok_when_no_data() {
  DisplayData d = makeBaseData();
  d.dataValid = false;
  AdvisoryResult r = resolveCriticalAlert(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_OK, r.level);    // status_view nayttaa oman oletuksen
}

void test_critical_fault_beats_pending_advance() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.growPendingAdvance = true;
  strncpy(d.deviceStateName, "FAULT", sizeof(d.deviceStateName) - 1);
  AdvisoryResult r = resolveCriticalAlert(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_FAULT, r.level);
}

// Saturoitunut PPFD EI saa laukaista "valoteho heikko" -neuvoa. Kynnys on
// 100 umol ja katossa oleva lukema jaa ~97:aan, joten ilman tata vartijaa
// naytto kehottaisi lisaamaan valoa suorassa auringossa (29.7.2026).
void test_no_light_low_advisory_when_saturated() {
  DisplayData d = makeBaseData();
  d.lightsOn = true; d.ppfdValid = true; d.ppfd = 96.8f; d.ppfdSaturated = true;
  AdvisoryResult r = resolveAdvisory(&d);
  // Vaite on nimenomaan "ei TATA neuvoa" — ketju saa pudota mihin tahansa
  // muuhun (esim. "kaikki hyvin"). Tarkka merkkijono sitoisi testin
  // fallback-tekstiin joka ei ole taman testin asia.
  TEST_ASSERT_NOT_EQUAL(0, strcmp(r.message, TXT_ADV_LIGHT_LOW));
}

// Sama tilanne ilman saturaatiota: neuvo kuuluu antaa.
void test_light_low_advisory_when_genuinely_dim() {
  DisplayData d = makeBaseData();
  d.lightsOn = true; d.ppfdValid = true; d.ppfd = 40.0f; d.ppfdSaturated = false;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_LIGHT_LOW, r.message);
}

// ── Kuumuus: uusi sääntö 29.7.2026 ──────────────────────────────────

// 43,7 C / 23,7 % RH / VPD 6,9 kPa mitattiin yhta aikaa parvekkeella. Molemmat
// saannot laukeavat, ja kuumuuden PITAA voittaa: "lisaa kosteutta" ei tee
// mitaan paahteessa, varjostus tekee.
void test_heat_beats_vpd() {
  DisplayData d = makeBaseData();
  d.envValid = true; d.airTemp = 43.7f;
  d.vpdValid = true; d.vpdKpa = 6.9f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_HEAT_HIGH, r.message);
}

// Vesi on yha kuumuutta kiireellisempi: kuiva pumppu rikkoo laitteen.
void test_water_low_beats_heat() {
  DisplayData d = makeBaseData();
  d.waterLevelOk = false;
  d.envValid = true; d.airTemp = 43.7f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_WATER_LOW, r.message);
}

void test_heat_below_threshold_is_not_flagged() {
  DisplayData d = makeBaseData();
  d.envValid = true; d.airTemp = COACH_AIR_TEMP_MAX_C - 0.1f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_NOT_EQUAL(0, strcmp(r.message, TXT_ADV_HEAT_HIGH));
}

// Ilman envValid-vartijaa nollattu airTemp (0.0) ei laukaise mitaan, mutta
// yhta lailla EI saa laukaista roskalukema ilman validiteettia.
void test_heat_ignored_when_env_invalid() {
  DisplayData d = makeBaseData();
  d.envValid = false; d.airTemp = 99.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_NOT_EQUAL(0, strcmp(r.message, TXT_ADV_HEAT_HIGH));
}

// ── Liika valo: uusi sääntö 29.7.2026 ───────────────────────────────

// Ydintapaus: 795 umol taimelle jonka tavoite on 100-200.
void test_light_high_warns() {
  DisplayData d = makeBaseData();
  d.lightsOn = true; d.ppfdValid = true; d.ppfd = 795.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_LIGHT_HIGH, r.message);
}

// Tama on koko saannon pointti: aurinko paistaa vaikka valorele on OFF.
// Jos ehto olisi sarjattu lightsOn:iin, varoitus katoaisi juuri silloin kun
// lamppu sammutetaan ja kasvit ovat ulkona.
void test_light_high_warns_even_when_lights_off() {
  DisplayData d = makeBaseData();
  d.lightsOn = false; d.ppfdValid = true; d.ppfd = 795.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_LIGHT_HIGH, r.message);
}

// Alaraja voi todistaa ylityksen vaikkei alitusta: saturoitunut 700 umol
// tarkoittaa "vahintaan 700", joten ylitys on varma.
void test_light_high_warns_when_saturated() {
  DisplayData d = makeBaseData();
  d.ppfdValid = true; d.ppfd = 700.0f; d.ppfdSaturated = true;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_LIGHT_HIGH, r.message);
}

// Kuumuus voittaa liian valon: sama juurisyy (paahde), mutta lampo on se joka
// tappaa taimen tunneissa.
void test_heat_beats_light_high() {
  DisplayData d = makeBaseData();
  d.envValid = true; d.airTemp = 43.7f;
  d.ppfdValid = true; d.ppfd = 795.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_HEAT_HIGH, r.message);
}

// ── M1 (11.8.2026): the device-published PE band WIDENS the alert bounds and
// never narrows them ──────────────────────────────────────────────────────
//
// The banner is an alert ("this is actually wrong"), the published band is a
// comfort range ("this is optimum") — two different concepts about the same
// quantity, so the alert takes the union of the two (advisory_peBounds()).
// Narrowing the alert to the comfort band would make the banner warn on
// perfectly normal readings; whether it SHOULD narrow, and with what margin,
// is KYSYMYKSET.md Q9 and not something these tests may decide for it.

static void setVpdTarget(DisplayData* d, float lo, float hi) {
  d->peTargetCount = 1;
  strncpy(d->peTargets[0].field, "vpd", sizeof(d->peTargets[0].field));
  d->peTargets[0].lo = lo;
  d->peTargets[0].hi = hi;
}

// ── Regression guards: a NARROWER published band must not tighten the alert ──
// These are the two cases that decide the banner's whole character. A reading
// inside the generic alert band but outside a tighter comfort band is a normal
// reading, and the banner must stay quiet about it.

void test_narrower_vpd_band_does_not_tighten_alert_low() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = 0.5f;               // inside COACH_[0.4,1.6], below a 0.6-0.9 band
  setVpdTarget(&d, 0.6f, 0.9f);
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_NOT_EQUAL(ADVISORY_WARN, r.level);
}

void test_narrower_vpd_band_does_not_tighten_alert_high() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = 1.0f;               // inside COACH_[0.4,1.6], above a 0.4-0.8 band
  setVpdTarget(&d, 0.4f, 0.8f);
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_NOT_EQUAL(ADVISORY_WARN, r.level);
}

void test_narrower_ppfd_band_does_not_tighten_alert() {
  DisplayData d = makeBaseData();
  d.lightsOn = true;
  d.ppfdValid = true;
  d.ppfd = 120.0f;               // above COACH floor 100, below a 150-200 band
  d.peTargetCount = 1;
  strncpy(d.peTargets[0].field, "ppfd", sizeof(d.peTargets[0].field));
  d.peTargets[0].lo = 150.0f; d.peTargets[0].hi = 200.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_NOT_EQUAL(ADVISORY_WARN, r.level);
}

// ── The band IS consulted: a WIDER published band widens the alert ──
// This is what stops the union from being the same thing as ignoring the
// device entirely — a phase whose comfort range is broader than the generic
// literature band must be allowed to silence a warning the constants alone
// would have raised.

void test_wider_vpd_band_widens_alert_high() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = 1.8f;               // above COACH ceiling 1.6, inside a 0.4-2.0 band
  setVpdTarget(&d, 0.4f, 2.0f);
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_NOT_EQUAL(ADVISORY_WARN, r.level);
}

void test_wider_vpd_band_still_warns_outside_it() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = 2.2f;               // outside both COACH 1.6 and the 0.4-2.0 band
  setVpdTarget(&d, 0.4f, 2.0f);
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_VPD_HIGH, r.message);
}

void test_wider_ppfd_band_widens_ceiling() {
  DisplayData d = makeBaseData();
  d.ppfdValid = true;
  d.ppfd = 700.0f;               // above COACH ceiling 600, inside a 200-900 band
  d.peTargetCount = 1;
  strncpy(d.peTargets[0].field, "ppfd", sizeof(d.peTargets[0].field));
  d.peTargets[0].lo = 200.0f; d.peTargets[0].hi = 900.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_NOT_EQUAL(ADVISORY_WARN, r.level);
}

// No VPD entry in the published targets (only a different field present) ->
// falls back to COACH_* for VPD specifically, same as an empty payload.
void test_missing_vpd_target_falls_back_to_coach_constant() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = COACH_VPD_COMFORT_MIN_KPA - 0.1f;
  d.peTargetCount = 1;
  strncpy(d.peTargets[0].field, "dli", sizeof(d.peTargets[0].field));
  d.peTargets[0].lo = 6.0f; d.peTargets[0].hi = 10.0f;
  AdvisoryResult r = resolveAdvisory(&d);
  TEST_ASSERT_EQUAL_INT(ADVISORY_WARN, r.level);
  TEST_ASSERT_EQUAL_STRING(TXT_ADV_VPD_LOW, r.message);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_data_shows_waiting);
  RUN_TEST(test_device_fault_with_message);
  RUN_TEST(test_device_fault_without_message_uses_generic);
  RUN_TEST(test_maintenance_shows_service_banner);
  RUN_TEST(test_maintenance_beats_water_low);
  RUN_TEST(test_fault_still_beats_maintenance);
  RUN_TEST(test_ebb_fault_beats_vpd);
  RUN_TEST(test_water_low_beats_vpd);
  RUN_TEST(test_vpd_below_comfort_band);
  RUN_TEST(test_vpd_above_comfort_band);
  RUN_TEST(test_vpd_inside_comfort_band_is_ok);
  RUN_TEST(test_vpd_invalid_is_skipped_not_flagged);
  RUN_TEST(test_ppfd_low_while_lights_on);
  RUN_TEST(test_ppfd_low_while_lights_off_is_ok);
  RUN_TEST(test_all_nominal_is_ok);

  RUN_TEST(test_phase_action_replaces_all_good_when_nothing_is_wrong);
  RUN_TEST(test_water_low_beats_phase_action);
  RUN_TEST(test_vpd_out_of_band_beats_phase_action);
  RUN_TEST(test_fault_beats_phase_action);
  RUN_TEST(test_pending_advance_beats_phase_action);
  RUN_TEST(test_no_grow_falls_back_to_all_good);
  RUN_TEST(test_grow_active_but_empty_action_falls_back_to_all_good);
  RUN_TEST(test_all_shipped_banner_texts_fit_one_line);

  RUN_TEST(test_critical_fault_with_message);
  RUN_TEST(test_critical_maintenance);
  RUN_TEST(test_critical_ebb_fault_and_water_low);
  RUN_TEST(test_critical_pending_advance);
  RUN_TEST(test_critical_ignores_vpd_and_ppfd);
  RUN_TEST(test_critical_ok_when_only_grow_action);
  RUN_TEST(test_critical_ok_when_no_data);
  RUN_TEST(test_critical_fault_beats_pending_advance);
  RUN_TEST(test_no_light_low_advisory_when_saturated);
  RUN_TEST(test_light_low_advisory_when_genuinely_dim);

  RUN_TEST(test_heat_beats_vpd);
  RUN_TEST(test_water_low_beats_heat);
  RUN_TEST(test_heat_below_threshold_is_not_flagged);
  RUN_TEST(test_heat_ignored_when_env_invalid);
  RUN_TEST(test_light_high_warns);
  RUN_TEST(test_light_high_warns_even_when_lights_off);
  RUN_TEST(test_light_high_warns_when_saturated);
  RUN_TEST(test_heat_beats_light_high);
  RUN_TEST(test_narrower_vpd_band_does_not_tighten_alert_low);
  RUN_TEST(test_narrower_vpd_band_does_not_tighten_alert_high);
  RUN_TEST(test_narrower_ppfd_band_does_not_tighten_alert);
  RUN_TEST(test_wider_vpd_band_widens_alert_high);
  RUN_TEST(test_wider_vpd_band_still_warns_outside_it);
  RUN_TEST(test_wider_ppfd_band_widens_ceiling);
  RUN_TEST(test_missing_vpd_target_falls_back_to_coach_constant);
  return UNITY_END();
}
