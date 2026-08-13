#include <unity.h>

#include <string.h>

#include "test_feature_flags.h"
#include "grow_guidance.h"

void setUp() {}
void tearDown() {}

void test_resolve_returns_phase_guidance_when_present() {
  GrowPhaseParams phase = {};
  strncpy(phase.guidance, "Kayta mietoa ravinnetta ja tarkista juuret.", sizeof(phase.guidance) - 1);
  phase.guidance[sizeof(phase.guidance) - 1] = '\0';

  const char* selected = grow_resolvePhaseGuidance(&phase, "fallback");
  TEST_ASSERT_EQUAL_STRING("Kayta mietoa ravinnetta ja tarkista juuret.", selected);
}

void test_resolve_returns_fallback_when_phase_guidance_missing() {
  GrowPhaseParams phase = {};
  phase.guidance[0] = '\0';

  const char* selected = grow_resolvePhaseGuidance(&phase, "fallback instruction");
  TEST_ASSERT_EQUAL_STRING("fallback instruction", selected);
}

void test_resolve_returns_empty_when_both_missing() {
  const char* selected = grow_resolvePhaseGuidance(NULL, NULL);
  TEST_ASSERT_EQUAL_STRING("", selected);
}

// ── grow_shortActionFor / grow_actionWindowText (Fable §3.2/3.4/3.6) ──
// Kasvi- JA paivakohtainen valmennusrivi: vaihepaiva (growElapsedDays)
// valitsee ikkunan, korkein kynnys <= paiva voittaa.

void test_basil_vegetative_day_windows() {
  // Paiva 0-6: varhaissignaali (lehtien vari).
  TEST_ASSERT_EQUAL_STRING("Kasvu käynnissä - lehdet tummanvihreät?",
      grow_shortActionFor("basil", GROW_PHASE_VEGETATIVE, 1, 0));
  TEST_ASSERT_EQUAL_STRING("Kasvu käynnissä - lehdet tummanvihreät?",
      grow_shortActionFor("basil", GROW_PHASE_VEGETATIVE, 1, 6));
  // Paiva 7-15: nipistys (apikaalisen dominanssin katko).
  TEST_ASSERT_EQUAL_STRING("6-8 lehteä? Nipistä kärki, kasvi haarautuu",
      grow_shortActionFor("basil", GROW_PHASE_VEGETATIVE, 1, 7));
  TEST_ASSERT_EQUAL_STRING("6-8 lehteä? Nipistä kärki, kasvi haarautuu",
      grow_shortActionFor("basil", GROW_PHASE_VEGETATIVE, 1, 15));
  // Paiva 16+: kohti korjuuta.
  TEST_ASSERT_EQUAL_STRING("Latvat kasvavat kohti korjuuta",
      grow_shortActionFor("basil", GROW_PHASE_VEGETATIVE, 1, 16));
  TEST_ASSERT_EQUAL_STRING("Latvat kasvavat kohti korjuuta",
      grow_shortActionFor("basil", GROW_PHASE_VEGETATIVE, 1, 99));
}

void test_basil_seedling_and_harvest_windows() {
  // Taimivaihe (SEURANTA idatysaskelten jalkeen). Kolme ikkunaa: orientaatio
  // (paiva 0-1) -> vahvistuminen (2-8) -> lahesty siirtoa (9+).
  TEST_ASSERT_EQUAL_STRING("Taimi valossa, ei vielä vesikiertoa",
      grow_shortActionFor("basil", GROW_PHASE_SEEDLING, 1, 0));
  TEST_ASSERT_EQUAL_STRING("Taimi valossa, ei vielä vesikiertoa",
      grow_shortActionFor("basil", GROW_PHASE_SEEDLING, 1, 1));
  TEST_ASSERT_EQUAL_STRING("Taimi vahvistuu, odota juuria",
      grow_shortActionFor("basil", GROW_PHASE_SEEDLING, 1, 2));
  TEST_ASSERT_EQUAL_STRING("Taimi vahvistuu, odota juuria",
      grow_shortActionFor("basil", GROW_PHASE_SEEDLING, 1, 8));
  TEST_ASSERT_EQUAL_STRING("Tarkista juuret - valmis siirtoon?",
      grow_shortActionFor("basil", GROW_PHASE_SEEDLING, 1, 9));
  // Sadonkorjuu (jatkuva sato, vanhenemisvihje 45 vrk).
  TEST_ASSERT_EQUAL_STRING("Korjaa säännöllisesti - kasvi haarautuu",
      grow_shortActionFor("basil", GROW_PHASE_HARVEST, 2, 0));
  TEST_ASSERT_EQUAL_STRING("Kasvi vanhenee - harkitse uutta siementä",
      grow_shortActionFor("basil", GROW_PHASE_HARVEST, 2, 45));
}

// Kasvi jolla ei ole taulukkoriviä putoaa geneeriseen switchiin (grow_shortAction).
void test_unknown_plant_falls_back_to_generic_switch() {
  const char* vegGen = grow_shortAction(GROW_PHASE_VEGETATIVE, 1);
  TEST_ASSERT_EQUAL_STRING(vegGen,
      grow_shortActionFor("parsley", GROW_PHASE_VEGETATIVE, 1, 5));
  // Basilika ROOTING-vaiheelle ei ole ikkunaa -> myos fallback.
  const char* rootGen = grow_shortAction(GROW_PHASE_ROOTING, 1);
  TEST_ASSERT_EQUAL_STRING(rootGen,
      grow_shortActionFor("basil", GROW_PHASE_ROOTING, 1, 3));
}

// grow_actionWindowText palauttaa NULL kun riviä ei ole (myos tyhja/NULL
// plantId), jotta kutsuja tietaa pudota fallbackiin.
void test_action_window_null_cases() {
  TEST_ASSERT_NULL(grow_actionWindowText("parsley", GROW_PHASE_VEGETATIVE, 5));
  TEST_ASSERT_NULL(grow_actionWindowText("basil", GROW_PHASE_ROOTING, 5));
  TEST_ASSERT_NULL(grow_actionWindowText("", GROW_PHASE_VEGETATIVE, 5));
  TEST_ASSERT_NULL(grow_actionWindowText(NULL, GROW_PHASE_VEGETATIVE, 5));
}

// UTF-8-koodipisteiden maara (nayttoleveys), EI tavujen: a/o vievat 2 tavua
// mutta yhden nayttomerkin, ja e-ink muuntaa ne yhdeksi Latin-1-tavuksi ennen
// piirtoa. Jatkotavut ovat muotoa 10xxxxxx -> ei lasketa.
static size_t utf8_codepoints(const char* s) {
  size_t n = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++)
    if ((*p & 0xC0) != 0x80) n++;
  return n;
}

// Jokainen valmennusrivi mahtuu e-inkin yhdelle 12pt-riville (<= 44 merkkia,
// grow_guidance.h). Suojaa tulevat kasvilisaykset banneri-ylivuodolta.
void test_all_action_windows_fit_banner() {
  for (int i = 0; i < GROW_ACTION_WINDOW_COUNT; i++) {
    size_t len = utf8_codepoints(GROW_ACTION_WINDOWS[i].text);
    TEST_ASSERT_TRUE_MESSAGE(len <= 44, GROW_ACTION_WINDOWS[i].text);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_resolve_returns_phase_guidance_when_present);
  RUN_TEST(test_resolve_returns_fallback_when_phase_guidance_missing);
  RUN_TEST(test_resolve_returns_empty_when_both_missing);
  RUN_TEST(test_basil_vegetative_day_windows);
  RUN_TEST(test_basil_seedling_and_harvest_windows);
  RUN_TEST(test_unknown_plant_falls_back_to_generic_switch);
  RUN_TEST(test_action_window_null_cases);
  RUN_TEST(test_all_action_windows_fit_banner);
  return UNITY_END();
}
