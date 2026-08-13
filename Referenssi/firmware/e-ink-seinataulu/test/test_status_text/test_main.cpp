/*=====================================================================
  test_status_text - display_status_text.h (formatActivity + composeSysLine)

  Kattaa NFT-oletuksen (ENABLE_EINK_FLOOD_STATUS=false, config.h) ja
  ohjatun askeleen vaihe-kehystyksen. Nama ovat ne kolme regressiota jotka
  kayttaja havaitsi seinataululla 19.7.2026:
    1. "Seuraava tulva ~N min" ei saa nakya NFT-laitteessa
    2. Idatys/juurtuminen-kehystys puuttui vaihe-rivilta (oli vain webissa)
    3. "N pv" -paivalaskuri on harhaanjohtava kesken idatyksen
=====================================================================*/

#include <string.h>
#include <unity.h>

#include "display_status_text.h"

void setUp() {}
void tearDown() {}

static DisplayData makeBaseData() {
  DisplayData d = {};
  d.dataValid = true;
  return d;
}

// ── formatActivity ────────────────────────────────────────────────────

void test_activity_no_data() {
  DisplayData d = makeBaseData();
  d.dataValid = false;
  char out[40];
  formatActivity(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("-", out);
}

void test_activity_fault_wins() {
  DisplayData d = makeBaseData();
  d.ebbFault = true;
  d.circulateActive = true;   // fault beats everything
  char out[40];
  formatActivity(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(TXT_ACT_FAULT, out);
}

void test_activity_circulate_shows_in_nft() {
  DisplayData d = makeBaseData();
  d.circulateActive = true;   // NFT-relevantti — nakyy vaikka tulvitus piilossa
  char out[40];
  formatActivity(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(TXT_ACT_CIRCULATE, out);
}

void test_activity_idle_when_not_growing() {
  DisplayData d = makeBaseData();
  d.growActive = false;
  char out[40];
  formatActivity(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(TXT_ACT_IDLE, out);
}

// Regressio 1: NFT-oletuksessa tulvalaskuri EI saa nakya vaikka PM raportoi
// next_cycle_sec:n (ENABLE_EBB_FLOW paalla PM-puolella).
void test_activity_hides_next_flood_countdown_in_nft() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.ebbNextCycleSec = 329 * 60;   // sama arvo jonka kayttaja naki ("~329 min")
  char out[40];
  formatActivity(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(TXT_ACT_GROWING, out);
  TEST_ASSERT_NULL(strstr(out, "tulva"));
  TEST_ASSERT_NULL(strstr(out, "Tulva"));
}

// NFT: myoskaan raaka FLOOD-tila ei tuota tulvatekstia (branch kaannetaan pois).
void test_activity_hides_flood_state_in_nft() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  strncpy(d.ebbState, "FLOOD", sizeof(d.ebbState) - 1);
  char out[40];
  formatActivity(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(TXT_ACT_GROWING, out);
}

// ── eink_composeSysLine ───────────────────────────────────────────────

void test_sysline_idle() {
  DisplayData d = makeBaseData();
  d.growActive = false;
  d.lightsOn = false;
  d.waterLevelOk = true;
  char out[80];
  eink_composeSysLine(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Valo: OFF   Vesi: OK", out);
}

void test_sysline_growing_no_step_shows_day_count() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.stepCount = 0;             // ei askelta -> normaali vaiheen nimi + paivat
  d.growPhase = 2;            // VEGETATIVE
  d.growElapsedDays = 5;
  d.lightsOn = true;
  d.waterLevelOk = true;
  strncpy(d.phaseName, "Kasvuvaihe", sizeof(d.phaseName) - 1);
  char out[80];
  eink_composeSysLine(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Vaihe: Kasvuvaihe (5 pv)   Valo: ON   Vesi: OK", out);
}

// Regressiot 2+3: siemenaloitus + askel kaynnissa -> "Idatys kaynnissa",
// EI "Taimivaihe (0 pv)".
void test_sysline_seedling_step_frames_as_germinating() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.stepCount = 3;
  strncpy(d.stepText, "Pida kuutiot kannen alla...", sizeof(d.stepText) - 1);
  d.growPhase = EINK_PHASE_SEEDLING;
  d.growElapsedDays = 0;
  d.lightsOn = true;
  d.waterLevelOk = true;
  strncpy(d.phaseName, "Taimivaihe", sizeof(d.phaseName) - 1);
  char out[80];
  eink_composeSysLine(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Vaihe: Id\xE4tys k\xE4ynniss\xE4   Valo: ON   Vesi: OK", out);
  TEST_ASSERT_NULL(strstr(out, "pv"));              // ei paivalaskuria
  TEST_ASSERT_NULL(strstr(out, "Taimivaihe"));      // ei raakaa vaiheen nimea
}

void test_sysline_rooting_step_frames_as_rooting() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.stepCount = 3;
  strncpy(d.stepText, "Leikkaa terve pistokas...", sizeof(d.stepText) - 1);
  d.growPhase = EINK_PHASE_ROOTING;
  d.lightsOn = false;
  d.waterLevelOk = true;
  strncpy(d.phaseName, "Juurtuminen", sizeof(d.phaseName) - 1);
  char out[80];
  eink_composeSysLine(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Vaihe: Juurtuminen k\xE4ynniss\xE4   Valo: OFF   Vesi: OK", out);
  TEST_ASSERT_NULL(strstr(out, "pv"));
}

// Askel-lippu ilman tekstia (stepText tyhja) -> ei kehystysta, normaali rivi.
// Suojaa silta etta stepCount>0 mutta teksti puuttuu (vanha/valilainen data).
void test_sysline_step_count_without_text_falls_back() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.stepCount = 3;
  d.stepText[0] = '\0';       // teksti puuttuu -> ei aktiivinen askel
  d.growPhase = EINK_PHASE_SEEDLING;
  d.growElapsedDays = 2;
  d.lightsOn = true;
  d.waterLevelOk = true;
  strncpy(d.phaseName, "Taimivaihe", sizeof(d.phaseName) - 1);
  char out[80];
  eink_composeSysLine(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Vaihe: Taimivaihe (2 pv)   Valo: ON   Vesi: OK", out);
}

// ── status_packContextLines (tilannenayton alarivit) ──────────────────
// Mittaus stubataan kiinteaan merkkileveyteen, jotta testi ei riipu GFX-
// fontista: 10 px/merkki. STATUS_DATA_W on 450 px -> 45 merkkia per rivi.

static int16_t measureFixed(const char* s, int len, void* ctx) {
  (void)s; (void)ctx;
  return (int16_t)(len * 10);
}

void test_context_single_line_when_it_fits() {
  const char* parts[] = { "22.7 C", "Kosteus 66 %" };   // 6 + 2 + 12 = 20 merkkia
  char l1[140], l2[140];
  int n = status_packContextLines(parts, 2, 450, measureFixed, nullptr,
                                  l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("22.7 C  Kosteus 66 %", l1);
  TEST_ASSERT_EQUAL_STRING("", l2);
}

void test_context_wraps_to_second_line() {
  // 5 palaa taysin yksikoin ylittaa 45 merkkia -> loput valuvat riville 2.
  const char* parts[] = { "22.7 C", "Kosteus 66 %", "Valo 120 umol",
                          "Kork 400 mm", "0.7 W" };
  char l1[140], l2[140];
  int n = status_packContextLines(parts, 5, 450, measureFixed, nullptr,
                                  l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_INT(2, n);
  // Rivi 1 saa mahtuvat palat, rivi 2 loput — pala ei katkea kesken.
  TEST_ASSERT_TRUE(strlen(l1) <= 45);
  TEST_ASSERT_TRUE(strlen(l2) > 0);
  TEST_ASSERT_EQUAL_STRING("22.7 C  Kosteus 66 %  Valo 120 umol", l1);
  TEST_ASSERT_EQUAL_STRING("Kork 400 mm  0.7 W", l2);
}

void test_context_skips_empty_parts() {
  // resolveField-epaonnistuminen jattaa palan pois (esim. height_valid=false).
  const char* parts[] = { "22.7 C", "", "0.7 W" };
  char l1[140], l2[140];
  int n = status_packContextLines(parts, 3, 450, measureFixed, nullptr,
                                  l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("22.7 C  0.7 W", l1);
}

void test_context_drops_overflow_instead_of_breaking_layout() {
  // Kuusi pitkaa palaa ei mahdu kahdellekaan riville: ylimaarainen PUDOTETAAN,
  // ei valuteta ruudun reunaan. Tama on se vahti joka estaa uuden kentan
  // lisaysta rikkomasta layoutia hiljaa.
  const char* parts[] = { "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",   // 30
                          "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",   // 30
                          "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCC" }; // 30
  char l1[140], l2[140];
  int n = status_packContextLines(parts, 3, 450, measureFixed, nullptr,
                                  l1, sizeof(l1), l2, sizeof(l2));
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_TRUE(strlen(l1) <= 45);
  TEST_ASSERT_TRUE(strlen(l2) <= 45);
  // Kolmas pala ei mahtunut mihinkaan -> ei saa esiintya kummallakaan rivilla.
  TEST_ASSERT_NULL(strstr(l1, "CCC"));
  TEST_ASSERT_NULL(strstr(l2, "CCC"));
}

void test_context_empty_input_yields_no_lines() {
  char l1[140], l2[140];
  TEST_ASSERT_EQUAL_INT(0, status_packContextLines(nullptr, 0, 450, measureFixed,
                                                   nullptr, l1, sizeof(l1), l2, sizeof(l2)));
  TEST_ASSERT_EQUAL_STRING("", l1);
}

// ── status_selectPanelSentenceKind (STATUS-paneelin tilalauseen valinta) ──
// Puhdas paatoslogiikka status_view.h:n status_drawPanel():lle: kriittinen
// halytys > nappivihje (button_next_phase) > vaiheohje (growAction) > oletus.
// Lisatty 10.8.2026, JONO.md kohta 1 — raudalla 3.8.2026 havaitun bugin
// korjaus ("nappi toimii mutta paneeli ei kerro sita").

static DisplayData makeGrowingData() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  return d;
}

void test_sentence_critical_wins_over_button_hint() {
  DisplayData d = makeGrowingData();
  d.buttonNextPhase = true;
  TEST_ASSERT_EQUAL_INT(STATUS_SENTENCE_CRITICAL,
                        status_selectPanelSentenceKind(&d, /*criticalActive=*/true));
}

void test_sentence_button_hint_when_no_critical_alert() {
  DisplayData d = makeGrowingData();
  d.buttonNextPhase = true;
  strncpy(d.growAction, "Kastele joka 2. paiva", sizeof(d.growAction) - 1);
  TEST_ASSERT_EQUAL_INT(STATUS_SENTENCE_BUTTON_HINT,
                        status_selectPanelSentenceKind(&d, /*criticalActive=*/false));
}

void test_sentence_button_hint_requires_data_valid() {
  DisplayData d = makeGrowingData();
  d.dataValid = false;
  d.buttonNextPhase = true;
  TEST_ASSERT_EQUAL_INT(STATUS_SENTENCE_WAITING,
                        status_selectPanelSentenceKind(&d, /*criticalActive=*/false));
}

void test_sentence_grow_action_when_no_button_hint() {
  DisplayData d = makeGrowingData();
  d.buttonNextPhase = false;
  strncpy(d.growAction, "Kastele joka 2. paiva", sizeof(d.growAction) - 1);
  TEST_ASSERT_EQUAL_INT(STATUS_SENTENCE_GROW_ACTION,
                        status_selectPanelSentenceKind(&d, /*criticalActive=*/false));
}

void test_sentence_default_when_grow_action_empty() {
  DisplayData d = makeGrowingData();
  d.buttonNextPhase = false;
  d.growAction[0] = '\0';
  TEST_ASSERT_EQUAL_INT(STATUS_SENTENCE_DEFAULT,
                        status_selectPanelSentenceKind(&d, /*criticalActive=*/false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_activity_no_data);
  RUN_TEST(test_activity_fault_wins);
  RUN_TEST(test_activity_circulate_shows_in_nft);
  RUN_TEST(test_activity_idle_when_not_growing);
  RUN_TEST(test_activity_hides_next_flood_countdown_in_nft);
  RUN_TEST(test_activity_hides_flood_state_in_nft);
  RUN_TEST(test_sysline_idle);
  RUN_TEST(test_sysline_growing_no_step_shows_day_count);
  RUN_TEST(test_sysline_seedling_step_frames_as_germinating);
  RUN_TEST(test_sysline_rooting_step_frames_as_rooting);
  RUN_TEST(test_sysline_step_count_without_text_falls_back);
  RUN_TEST(test_context_single_line_when_it_fits);
  RUN_TEST(test_context_wraps_to_second_line);
  RUN_TEST(test_context_skips_empty_parts);
  RUN_TEST(test_context_drops_overflow_instead_of_breaking_layout);
  RUN_TEST(test_context_empty_input_yields_no_lines);
  RUN_TEST(test_sentence_critical_wins_over_button_hint);
  RUN_TEST(test_sentence_button_hint_when_no_critical_alert);
  RUN_TEST(test_sentence_button_hint_requires_data_valid);
  RUN_TEST(test_sentence_grow_action_when_no_button_hint);
  RUN_TEST(test_sentence_default_when_grow_action_empty);
  return UNITY_END();
}
