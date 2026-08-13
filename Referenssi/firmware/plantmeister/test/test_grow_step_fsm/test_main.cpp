/*=====================================================================
  test_grow_step_fsm — askelmoottorin puhdas logiikka (grow_step_fsm.h
  + grow_steps.h -haku). WIZARD: grow-steps.

  Tarkein yksittainen testi on tyyppi-vs-indeksi-regressio:
  cfg->growPhase on indeksi phases[]-taulukkoon, askellistat avaimen-
  netaan vaiheen TYYPILLA. Basilikalla ne sattuvat samoiksi (phaset
  enum-jarjestyksessa), joten bugi lapaisisi kaikki basilika-testit —
  siksi tassa kaytetaan kasvia jonka phases[] EI ole enum-jarjestyksessa.
=====================================================================*/

#include <unity.h>

#include "test_feature_flags.h"

#include "structs.h"
#include "grow_steps.h"
#include "grow_step_fsm.h"

static const uint32_t MS_PER_MIN = 60000UL;

void setUp()    {}
void tearDown() {}

// ── Testidata: omat listat grow_stepsForIn-hakuun ────────────────────

static const GrowStep TEST_STEPS_EXACT[] = {
  { "exact-1", NULL, 10UL, STEP_ADVANCE_TIMER },
  { "exact-2", NULL, 0UL,  STEP_ADVANCE_BUTTON },
};
static const GrowStep TEST_STEPS_GENERIC[] = {
  { "generic-1", NULL, 0UL, STEP_ADVANCE_BUTTON },
};
static const GrowStepList TEST_LISTS[] = {
  { "",      GROW_PHASE_SEEDLING, 1, TEST_STEPS_GENERIC, 1 },
  { "basil", GROW_PHASE_SEEDLING, 1, TEST_STEPS_EXACT,   2 },
};

// Aloitustapa-jokeri (STEP_METHOD_ANY): tarkka aloitustapa voittaa jokerin,
// tarkka kasvi voittaa yleisen (nelja precedenssitasoa).
static const GrowStep TEST_STEPS_ANY[]    = { { "any", NULL, 0UL, STEP_ADVANCE_BUTTON } };
static const GrowStep TEST_STEPS_EXACTM[] = { { "em1", NULL, 0UL, STEP_ADVANCE_BUTTON },
                                              { "em2", NULL, 0UL, STEP_ADVANCE_BUTTON } };
static const GrowStepList TEST_LISTS_WILDCARD[] = {
  { "",      GROW_PHASE_VEGETATIVE, STEP_METHOD_ANY, TEST_STEPS_ANY,    1 },
  { "basil", GROW_PHASE_VEGETATIVE, 1,               TEST_STEPS_EXACTM, 2 },
};

// Kasvi jonka phases[] on TAHALLAAN eri jarjestyksessa kuin enum:
// index 0 = VEGETATIVE (tyyppi 2), index 1 = SEEDLING (tyyppi 1).
// Nain indeksi ja tyyppi eroavat molemmissa suunnissa.
static PlantConfig makeShuffledBasil() {
  PlantConfig p = {};
  strncpy(p.id, "basil", sizeof(p.id) - 1);
  p.phaseCount = 2;
  p.phases[0] = {};
  p.phases[0].type = GROW_PHASE_VEGETATIVE;
  p.phases[1] = {};
  p.phases[1].type = GROW_PHASE_SEEDLING;
  return p;
}

static DeviceConfig makeGrowCfg(uint8_t phaseIdx, uint8_t startMethod) {
  DeviceConfig c = {};
  c.growActive      = true;
  c.growPhase       = phaseIdx;
  c.growStartMethod = startMethod;
  c.growStepIndex   = 0;
  return c;
}

// ════════════════════════════════════════════════════════════════════
// grow_stepsForIn — haku ja fallback-precedenssi
// ════════════════════════════════════════════════════════════════════

void test_lookup_exact_match_beats_generic() {
  uint8_t count = 0;
  const GrowStep* s = grow_stepsForIn(TEST_LISTS, 2, "basil",
                                      GROW_PHASE_SEEDLING, 1, &count);
  TEST_ASSERT_EQUAL_PTR(TEST_STEPS_EXACT, s);
  TEST_ASSERT_EQUAL_UINT8(2, count);
}

void test_lookup_falls_back_to_generic() {
  uint8_t count = 0;
  const GrowStep* s = grow_stepsForIn(TEST_LISTS, 2, "parsley",
                                      GROW_PHASE_SEEDLING, 1, &count);
  TEST_ASSERT_EQUAL_PTR(TEST_STEPS_GENERIC, s);
  TEST_ASSERT_EQUAL_UINT8(1, count);
}

void test_lookup_no_match_returns_null() {
  uint8_t count = 99;
  // Sama kasvi ja vaihe, eri aloitustapa (pistokas) -> ei listaa.
  TEST_ASSERT_NULL(grow_stepsForIn(TEST_LISTS, 2, "basil",
                                   GROW_PHASE_SEEDLING, 0, &count));
  TEST_ASSERT_EQUAL_UINT8(0, count);
  // Eri vaihe -> ei listaa.
  TEST_ASSERT_NULL(grow_stepsForIn(TEST_LISTS, 2, "basil",
                                   GROW_PHASE_HARVEST, 1, &count));
}

void test_lookup_exact_method_beats_wildcard() {
  uint8_t count = 0;
  // basil + method 1: tarkka kasvi+tapa (score 3) voittaa jokerin (score 0).
  const GrowStep* s = grow_stepsForIn(TEST_LISTS_WILDCARD, 2, "basil",
                                      GROW_PHASE_VEGETATIVE, 1, &count);
  TEST_ASSERT_EQUAL_PTR(TEST_STEPS_EXACTM, s);
  TEST_ASSERT_EQUAL_UINT8(2, count);
}

void test_lookup_wildcard_matches_any_method() {
  uint8_t count = 0;
  // basil + method 0: tarkkaa method-0-listaa ei ole -> jokeri (yleinen) osuu.
  const GrowStep* s = grow_stepsForIn(TEST_LISTS_WILDCARD, 2, "basil",
                                      GROW_PHASE_VEGETATIVE, 0, &count);
  TEST_ASSERT_EQUAL_PTR(TEST_STEPS_ANY, s);
  TEST_ASSERT_EQUAL_UINT8(1, count);

  // parsley + method 2: eri kasvi, jokeri (yleinen + any) osuu yha.
  const GrowStep* s2 = grow_stepsForIn(TEST_LISTS_WILDCARD, 2, "parsley",
                                       GROW_PHASE_VEGETATIVE, 2, &count);
  TEST_ASSERT_EQUAL_PTR(TEST_STEPS_ANY, s2);
}

// ════════════════════════════════════════════════════════════════════
// growstep_stepsForConfig — TYYPPI vs INDEKSI (regressiotesti)
// ════════════════════════════════════════════════════════════════════

void test_steps_resolved_by_phase_TYPE_not_index() {
  PlantConfig plant = makeShuffledBasil();

  // growPhase INDEKSI 1 -> phases[1].type == SEEDLING -> lista loytyy.
  // Indeksia tyyppina kayttava toteutus hakisi tyypilla 1 == SEEDLING
  // "vahingossa oikein" vain jos jarjestys sattuu tasmaamaan — talla
  // kasvilla se hakisi... indeksilla 1 se osuisi oikeaan, siksi
  // varsinainen ansa testataan alla toisin pain.
  DeviceConfig cfg = makeGrowCfg(/*phaseIdx=*/1, /*startMethod=*/1);
  uint8_t count = 0;
  const GrowStep* s = growstep_stepsForConfig(&plant, &cfg, &count);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_UINT8(4, count);   // oikea basil-lista (grow_steps.h, Fable §3.1: 4 askelta)
}

void test_steps_index_equal_to_seedling_type_is_not_enough() {
  // ANSA: growPhase INDEKSI 1 mutta phases[1].type == VEGETATIVE.
  // Indeksia tyyppina kayttava toteutus lukisi "1 == GROW_PHASE_SEEDLING"
  // ja palauttaisi SIEMENlistan (4 askelta); oikea tyyppi-resoluutio antaa
  // basilikan KASVUVAIHElistan (3 askelta, Fable §3.3). Erisuuri askelmaara
  // paljastaa bugin (buggy=4 siemen, oikea=3 kasvuvaihe).
  PlantConfig plant = makeShuffledBasil();
  plant.phases[1].type = GROW_PHASE_VEGETATIVE;
  plant.phases[0].type = GROW_PHASE_SEEDLING;

  DeviceConfig cfg = makeGrowCfg(/*phaseIdx=*/1, /*startMethod=*/1);
  uint8_t count = 0;
  const GrowStep* s = growstep_stepsForConfig(&plant, &cfg, &count);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_UINT8(3, count);   // kasvuvaihe (tyyppi), EI siemen (indeksi->4)

  // Ja indeksi 0 (== ROOTING enumissa) loytaa SIEMENlistan (4), koska
  // phases[0].type on SEEDLING.
  cfg.growPhase = 0;
  const GrowStep* s0 = growstep_stepsForConfig(&plant, &cfg, &count);
  TEST_ASSERT_NOT_NULL(s0);
  TEST_ASSERT_EQUAL_UINT8(4, count);   // siemen (tyyppi)
}

void test_steps_null_when_not_growing_or_out_of_bounds() {
  PlantConfig plant = makeShuffledBasil();
  uint8_t count = 0;

  DeviceConfig cfg = makeGrowCfg(1, 1);
  cfg.growActive = false;
  TEST_ASSERT_NULL(growstep_stepsForConfig(&plant, &cfg, &count));

  cfg = makeGrowCfg(/*phaseIdx=*/7, 1);   // indeksi yli phaseCountin
  TEST_ASSERT_NULL(growstep_stepsForConfig(&plant, &cfg, &count));

  cfg = makeGrowCfg(1, 1);
  TEST_ASSERT_NULL(growstep_stepsForConfig(NULL, &cfg, &count));
}

// ════════════════════════════════════════════════════════════════════
// sequenceActive — indeksin clamp (puolustus syvyydessa)
// ════════════════════════════════════════════════════════════════════

// sequenceComplete erottaa kaksi tilannetta jotka sequenceActive niputtaa
// yhteen "ei aktiivista askelta" -kieltoon. Nappi tarvitsee eron: vain
// loppuun kuitattu opastus tarkoittaa "valmis, seuraavaan vaiheeseen"
// (button_intent_map.h, 3.8.2026).
void test_sequence_complete_only_after_a_real_list_was_walked() {
  PlantConfig plant = makeShuffledBasil();
  DeviceConfig cfg = makeGrowCfg(1, 1);

  // Kesken sarjan: aktiivinen, ei valmis.
  TEST_ASSERT_TRUE(growstep_sequenceActive(&plant, &cfg));
  TEST_ASSERT_FALSE(growstep_sequenceComplete(&plant, &cfg));

  cfg.growStepIndex = 3;   // viimeinen askel viela nakyvissa
  TEST_ASSERT_TRUE(growstep_sequenceActive(&plant, &cfg));
  TEST_ASSERT_FALSE(growstep_sequenceComplete(&plant, &cfg));

  cfg.growStepIndex = 4;   // == count -> kuitattu loppuun
  TEST_ASSERT_FALSE(growstep_sequenceActive(&plant, &cfg));
  TEST_ASSERT_TRUE(growstep_sequenceComplete(&plant, &cfg));

  cfg.growStepIndex = 200; // roikkuva indeksi: yha "valmis", ei hiljaisuutta
  TEST_ASSERT_TRUE(growstep_sequenceComplete(&plant, &cfg));

  // Kasvatus pois paalta -> ei listaa -> ei valmis (ei myoskaan aktiivinen).
  cfg.growActive = false;
  TEST_ASSERT_FALSE(growstep_sequenceComplete(&plant, &cfg));
}

void test_sequence_complete_false_when_phase_has_no_list() {
  // Vaihe jolle ei ole rekisteroitya askellistaa: count == 0. Talloin nappi
  // sailyttaa vanhan buttonAction-fallbackin eika hyppaa vaihetta.
  PlantConfig plant = makeShuffledBasil();
  DeviceConfig cfg = makeGrowCfg(1, 1);
  for (uint8_t i = 0; i < plant.phaseCount; i++) {
    plant.phases[i].type = GROW_PHASE_CUSTOM;   // ei listaa millekaan vaiheelle
  }
  cfg.growStepIndex = 4;
  TEST_ASSERT_FALSE(growstep_sequenceActive(&plant, &cfg));
  TEST_ASSERT_FALSE(growstep_sequenceComplete(&plant, &cfg));
}

void test_sequence_inert_when_index_at_or_past_count() {
  PlantConfig plant = makeShuffledBasil();
  DeviceConfig cfg = makeGrowCfg(1, 1);

  TEST_ASSERT_TRUE(growstep_sequenceActive(&plant, &cfg));

  cfg.growStepIndex = 4;   // == count (4 askelta) -> sarja valmis
  TEST_ASSERT_FALSE(growstep_sequenceActive(&plant, &cfg));

  cfg.growStepIndex = 200; // roikkuva indeksi lyhentyneen listan jaljilta
  TEST_ASSERT_FALSE(growstep_sequenceActive(&plant, &cfg));

  GrowStepView v;
  TEST_ASSERT_FALSE(growstep_buildView(&plant, &cfg, 0, &v));
  TEST_ASSERT_FALSE(v.active);
}

// ════════════════════════════════════════════════════════════════════
// Ajastinmatematiikka
// ════════════════════════════════════════════════════════════════════

void test_timer_elapsed_boundary() {
  TEST_ASSERT_FALSE(growstep_timerElapsed(30 * MS_PER_MIN - 1, 30));
  TEST_ASSERT_TRUE(growstep_timerElapsed(30 * MS_PER_MIN, 30));
  TEST_ASSERT_FALSE(growstep_timerElapsed(0xFFFFFFFFUL, 0));  // 0 = ei ajastinta
}

void test_auto_advance_only_for_timer_steps() {
  GrowStep timerStep  = { "t", NULL, 30UL, STEP_ADVANCE_TIMER };
  GrowStep buttonStep = { "b", NULL, 30UL, STEP_ADVANCE_BUTTON };

  TEST_ASSERT_FALSE(growstep_autoAdvanceDue(&timerStep, 29 * MS_PER_MIN));
  TEST_ASSERT_TRUE(growstep_autoAdvanceDue(&timerStep, 30 * MS_PER_MIN));
  // BUTTON-askeleella timerMin on vain tekstikynnys — EI etene itsestaan.
  TEST_ASSERT_FALSE(growstep_autoAdvanceDue(&buttonStep, 300 * MS_PER_MIN));
}

void test_ack_allowed_only_for_button_steps() {
  GrowStep timerStep  = { "t", NULL, 30UL, STEP_ADVANCE_TIMER };
  GrowStep buttonStep = { "b", NULL, 0UL,  STEP_ADVANCE_BUTTON };
  TEST_ASSERT_FALSE(growstep_ackAllowed(&timerStep));
  TEST_ASSERT_TRUE(growstep_ackAllowed(&buttonStep));
}

void test_awaiting_user_blink_policy() {
  // BUTTON ilman ajastinta: heti tehtavaa -> vilkku.
  GrowStep sow = { "b", NULL, 0UL, STEP_ADVANCE_BUTTON };
  TEST_ASSERT_TRUE(growstep_awaitingUser(&sow, 0));

  // BUTTON + 5 vrk kynnys: EI vilkkua paivina 0-5, vilkku kynnyksen jalkeen.
  GrowStep germinate = { "g", "g-late", 5UL * 24UL * 60UL, STEP_ADVANCE_BUTTON };
  TEST_ASSERT_FALSE(growstep_awaitingUser(&germinate, 3UL * 24UL * 60UL * MS_PER_MIN));
  TEST_ASSERT_TRUE(growstep_awaitingUser(&germinate, 5UL * 24UL * 60UL * MS_PER_MIN));

  // TIMER-askel ei koskaan odota nappia.
  GrowStep soak = { "t", NULL, 30UL, STEP_ADVANCE_TIMER };
  TEST_ASSERT_FALSE(growstep_awaitingUser(&soak, 60 * MS_PER_MIN));
}

void test_text_switches_to_late_variant_at_threshold() {
  GrowStep germinate = { "early", "late", 5UL * 24UL * 60UL, STEP_ADVANCE_BUTTON };
  TEST_ASSERT_EQUAL_STRING("early", growstep_text(&germinate, 0));
  TEST_ASSERT_EQUAL_STRING("early",
      growstep_text(&germinate, 5UL * 24UL * 60UL * MS_PER_MIN - 1));
  TEST_ASSERT_EQUAL_STRING("late",
      growstep_text(&germinate, 5UL * 24UL * 60UL * MS_PER_MIN));

  GrowStep noLate = { "only", NULL, 30UL, STEP_ADVANCE_TIMER };
  TEST_ASSERT_EQUAL_STRING("only", growstep_text(&noLate, 60 * MS_PER_MIN));
}

void test_remaining_sec_counts_down_and_clamps() {
  GrowStep soak = { "t", NULL, 30UL, STEP_ADVANCE_TIMER };
  TEST_ASSERT_EQUAL_INT32(30 * 60, growstep_remainingSec(&soak, 0));
  TEST_ASSERT_EQUAL_INT32(5 * 60,  growstep_remainingSec(&soak, 25 * MS_PER_MIN));
  TEST_ASSERT_EQUAL_INT32(0,       growstep_remainingSec(&soak, 45 * MS_PER_MIN));

  GrowStep sow = { "b", NULL, 0UL, STEP_ADVANCE_BUTTON };
  TEST_ASSERT_EQUAL_INT32(-1, growstep_remainingSec(&sow, 0));
  GrowStep germinate = { "g", NULL, 5UL * 24UL * 60UL, STEP_ADVANCE_BUTTON };
  TEST_ASSERT_EQUAL_INT32(-1, growstep_remainingSec(&germinate, 0));
}

// ════════════════════════════════════════════════════════════════════
// Oikea basil-lista (grow_steps.h) — sisaltoregressio
// ════════════════════════════════════════════════════════════════════

void test_real_basil_seed_list_shape() {
  uint8_t count = 0;
  const GrowStep* s = grow_stepsFor("basil", GROW_PHASE_SEEDLING, 1, &count);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_UINT8(4, count);   // Fable §3.1: valmistele+liota+kylva+idata

  // Askel 1: valmistele — nappi, ei ajastinta (tarvikkeet + pH-vesi kasaan).
  TEST_ASSERT_EQUAL_UINT8(STEP_ADVANCE_BUTTON, s[0].advanceOn);
  TEST_ASSERT_EQUAL_UINT32(0UL, s[0].timerMin);
  // Askel 2: liotus — kello on totuus (30 min TIMER).
  TEST_ASSERT_EQUAL_UINT8(STEP_ADVANCE_TIMER, s[1].advanceOn);
  TEST_ASSERT_EQUAL_UINT32(30UL, s[1].timerMin);
  // Askel 3: valutus+kylvo — nappi, ei ajastinta.
  TEST_ASSERT_EQUAL_UINT8(STEP_ADVANCE_BUTTON, s[2].advanceOn);
  TEST_ASSERT_EQUAL_UINT32(0UL, s[2].timerMin);
  // Askel 4: idatys — nappi + 5 vrk tekstikynnys + myohaisteksti.
  TEST_ASSERT_EQUAL_UINT8(STEP_ADVANCE_BUTTON, s[3].advanceOn);
  TEST_ASSERT_EQUAL_UINT32(5UL * 24UL * 60UL, s[3].timerMin);
  TEST_ASSERT_NOT_NULL(s[3].textLate);
}

// Basilikan omat kasvuvaihe- ja sadonkorjuulistat (Fable §3.3/3.5) voittavat
// geneerisen fallbackin: eri pituus + eri pointteri kuin yleisella.
void test_real_basil_veg_harvest_specific() {
  uint8_t count = 0;

  // Basil VEGETATIVE -> oma 3-askelinen lista (siirto/ravinne/valo), kaikki
  // BUTTON. Yleinen VEGETATIVE on 4 askelta -> pointterit eroavat.
  const GrowStep* bveg = grow_stepsFor("basil", GROW_PHASE_VEGETATIVE, 1, &count);
  TEST_ASSERT_NOT_NULL(bveg);
  TEST_ASSERT_EQUAL_UINT8(3, count);
  for (uint8_t i = 0; i < 3; i++)
    TEST_ASSERT_EQUAL_UINT8(STEP_ADVANCE_BUTTON, bveg[i].advanceOn);

  uint8_t gcount = 0;
  const GrowStep* gveg = grow_stepsFor("parsley", GROW_PHASE_VEGETATIVE, 1, &gcount);
  TEST_ASSERT_EQUAL_UINT8(4, gcount);
  TEST_ASSERT_TRUE(bveg != gveg);

  // Basil HARVEST -> oma 2-askelinen, toisessa ratooning-textLate.
  const GrowStep* bharv = grow_stepsFor("basil", GROW_PHASE_HARVEST, 2, &count);
  TEST_ASSERT_NOT_NULL(bharv);
  TEST_ASSERT_EQUAL_UINT8(2, count);
  TEST_ASSERT_NOT_NULL(bharv[1].textLate);

  const GrowStep* gharv = grow_stepsFor("parsley", GROW_PHASE_HARVEST, 2, &gcount);
  TEST_ASSERT_TRUE(bharv != gharv);
}

// Tuntematon kasvi osuu YLEISEEN fallback-listaan (grow_steps.h): kaikki
// kasvit saavat idatys- (siemen) ja juurtumis- (pistokas) alitehtavat, ei
// vain basilika. Tarkka basil-osuma voittaa yha yleisen.
void test_real_generic_fallback_covers_all_plants() {
  uint8_t count = 0;

  // Tuntematon kasvi + SEEDLING + siemen -> yleinen siemenlista (3 askelta).
  const GrowStep* gen = grow_stepsFor("parsley", GROW_PHASE_SEEDLING, 1, &count);
  TEST_ASSERT_NOT_NULL(gen);
  TEST_ASSERT_EQUAL_UINT8(3, count);
  TEST_ASSERT_EQUAL_UINT8(STEP_ADVANCE_TIMER, gen[0].advanceOn);   // liotus

  // Basilika voittaa yha yleisen tarkalla osumalla (eri pointteri).
  uint8_t bcount = 0;
  const GrowStep* basil = grow_stepsFor("basil", GROW_PHASE_SEEDLING, 1, &bcount);
  TEST_ASSERT_NOT_NULL(basil);
  TEST_ASSERT_TRUE(basil != gen);

  // Tuntematon kasvi + ROOTING + pistokas -> yleinen juurtumislista (3 askelta).
  const GrowStep* root = grow_stepsFor("mint", GROW_PHASE_ROOTING, 0, &count);
  TEST_ASSERT_NOT_NULL(root);
  TEST_ASSERT_EQUAL_UINT8(3, count);
  TEST_ASSERT_EQUAL_UINT8(STEP_ADVANCE_BUTTON, root[0].advanceOn);  // leikkaa pistokas
  TEST_ASSERT_NOT_NULL(root[2].textLate);                          // "odota juuria" -> late

  // Taso B: kasvuvaihe + sadonkorjuu loytyvat KAIKILLE kasveille ja KAIKILLE
  // aloitustavoille (STEP_METHOD_ANY). parsleylla ei ole omaa listaa naille
  // vaiheille, joten se osuu yleiseen jokeriin (basilika saa omansa erikseen).
  for (uint8_t m = 0; m <= 2; m++) {
    const GrowStep* veg = grow_stepsFor("parsley", GROW_PHASE_VEGETATIVE, m, &count);
    TEST_ASSERT_NOT_NULL(veg);
    TEST_ASSERT_EQUAL_UINT8(4, count);                         // siirto/ravinne/valo/kasva
    TEST_ASSERT_NOT_NULL(veg[3].textLate);                     // "kasva" -> "korjaa valmis"

    const GrowStep* harv = grow_stepsFor("parsley", GROW_PHASE_HARVEST, m, &count);
    TEST_ASSERT_NOT_NULL(harv);
    TEST_ASSERT_EQUAL_UINT8(2, count);                         // ensisato + toistuva
    TEST_ASSERT_NOT_NULL(harv[1].textLate);                    // ratooning-late
  }

  // Vaihe jolle ei ole listaa lainkaan -> NULL (askeleet ovat lisa, ei pakko).
  TEST_ASSERT_NULL(grow_stepsFor("parsley", GROW_PHASE_CUSTOM, 1, &count));
  TEST_ASSERT_EQUAL_UINT8(0, count);
}

// ════════════════════════════════════════════════════════════════════
// Mutaatiot + koko sarjan simulointi
// ════════════════════════════════════════════════════════════════════

void test_reset_and_advance_move_index_and_anchor() {
  SystemState state = {};
  DeviceConfig cfg = makeGrowCfg(1, 1);
  cfg.growStepIndex = 2;

  grow_resetStepProgress(&state, &cfg, /*nowMs=*/5000);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.growStepIndex);
  TEST_ASSERT_EQUAL_UINT32(5000, state.growStepStartMs);

  growstep_advance(&state, &cfg, /*nowMs=*/9000);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.growStepIndex);
  TEST_ASSERT_EQUAL_UINT32(9000, state.growStepStartMs);
}

void test_full_basil_sequence_walkthrough() {
  PlantConfig plant = makeShuffledBasil();      // SEEDLING indeksissa 1
  DeviceConfig cfg = makeGrowCfg(1, 1);
  SystemState state = {};
  unsigned long now = 100000;
  grow_resetStepProgress(&state, &cfg, now);

  GrowStepView v;

  // Askel 1 (valmistele): BUTTON heti, ei ajastinta -> odottaa nappia.
  TEST_ASSERT_TRUE(growstep_buildView(&plant, &cfg, now - state.growStepStartMs, &v));
  TEST_ASSERT_EQUAL_UINT8(0, v.index);
  TEST_ASSERT_TRUE(v.ackAllowed);
  TEST_ASSERT_TRUE(v.awaitingUser);
  growstep_advance(&state, &cfg, now);   // kayttaja kuittaa tarvikkeet kasaan

  // Askel 2 (liotus): TIMER laskee alas, ei vilkkua, kuittaus ei kelpaa.
  TEST_ASSERT_TRUE(growstep_buildView(&plant, &cfg, now - state.growStepStartMs, &v));
  TEST_ASSERT_EQUAL_UINT8(1, v.index);
  TEST_ASSERT_FALSE(v.ackAllowed);
  TEST_ASSERT_FALSE(v.awaitingUser);
  TEST_ASSERT_EQUAL_INT32(30 * 60, v.remainingSec);

  // 30 min kuluu -> auto-advance due.
  now += 30 * MS_PER_MIN;
  uint8_t count = 0;
  const GrowStep* steps = growstep_stepsForConfig(&plant, &cfg, &count);
  TEST_ASSERT_EQUAL_UINT8(4, count);
  TEST_ASSERT_TRUE(growstep_autoAdvanceDue(&steps[cfg.growStepIndex],
                                           now - state.growStepStartMs));
  growstep_advance(&state, &cfg, now);

  // Askel 3 (valutus+kylvo): odottaa nappia heti.
  TEST_ASSERT_TRUE(growstep_buildView(&plant, &cfg, now - state.growStepStartMs, &v));
  TEST_ASSERT_EQUAL_UINT8(2, v.index);
  TEST_ASSERT_TRUE(v.ackAllowed);
  TEST_ASSERT_TRUE(v.awaitingUser);
  growstep_advance(&state, &cfg, now);   // kayttaja kuittaa

  // Askel 4 (idatys): paiva 3 — ei vilkkua, alkuteksti; paiva 5 — vilkku + late.
  now += 3UL * 24UL * 60UL * MS_PER_MIN;
  TEST_ASSERT_TRUE(growstep_buildView(&plant, &cfg, now - state.growStepStartMs, &v));
  TEST_ASSERT_EQUAL_UINT8(3, v.index);
  TEST_ASSERT_FALSE(v.awaitingUser);
  TEST_ASSERT_FALSE(v.late);

  now += 2UL * 24UL * 60UL * MS_PER_MIN;
  TEST_ASSERT_TRUE(growstep_buildView(&plant, &cfg, now - state.growStepStartMs, &v));
  TEST_ASSERT_TRUE(v.awaitingUser);
  TEST_ASSERT_TRUE(v.late);
  TEST_ASSERT_TRUE(v.ackAllowed);        // kuittaus sallittu jo ENNEN kynnysta —
                                         // sirkkalehdet voivat tulla paivana 3

  // Kuittaus paattaa sarjan: indeksi == count -> inertti.
  growstep_advance(&state, &cfg, now);
  TEST_ASSERT_FALSE(growstep_sequenceActive(&plant, &cfg));
  TEST_ASSERT_FALSE(growstep_buildView(&plant, &cfg, 0, &v));
}

// ════════════════════════════════════════════════════════════════════
// main
// ════════════════════════════════════════════════════════════════════

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_lookup_exact_match_beats_generic);
  RUN_TEST(test_lookup_falls_back_to_generic);
  RUN_TEST(test_lookup_no_match_returns_null);
  RUN_TEST(test_lookup_exact_method_beats_wildcard);
  RUN_TEST(test_lookup_wildcard_matches_any_method);

  RUN_TEST(test_steps_resolved_by_phase_TYPE_not_index);
  RUN_TEST(test_steps_index_equal_to_seedling_type_is_not_enough);
  RUN_TEST(test_steps_null_when_not_growing_or_out_of_bounds);

  RUN_TEST(test_sequence_complete_only_after_a_real_list_was_walked);
  RUN_TEST(test_sequence_complete_false_when_phase_has_no_list);
  RUN_TEST(test_sequence_inert_when_index_at_or_past_count);

  RUN_TEST(test_timer_elapsed_boundary);
  RUN_TEST(test_auto_advance_only_for_timer_steps);
  RUN_TEST(test_ack_allowed_only_for_button_steps);
  RUN_TEST(test_awaiting_user_blink_policy);
  RUN_TEST(test_text_switches_to_late_variant_at_threshold);
  RUN_TEST(test_remaining_sec_counts_down_and_clamps);

  RUN_TEST(test_real_basil_seed_list_shape);
  RUN_TEST(test_real_basil_veg_harvest_specific);
  RUN_TEST(test_real_generic_fallback_covers_all_plants);

  RUN_TEST(test_reset_and_advance_move_index_and_anchor);
  RUN_TEST(test_full_basil_sequence_walkthrough);

  return UNITY_END();
}
