#include <unity.h>

#include "plant_lookup.h"

void setUp() {
  g_stub_millis = 0;
  plants_loadDefaults();
}

void tearDown() {}

void test_load_defaults_has_entries() {
  TEST_ASSERT_TRUE(plants_getCount() > 0);
}

void test_get_by_id_basil_exists() {
  PlantConfig* p = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_STRING("Basilika", p->name);
}

void test_get_by_id_nonexistent_returns_null() {
  TEST_ASSERT_NULL(plants_getById("nonexistent"));
}

void test_get_by_index_zero_returns_valid() {
  PlantConfig* p = plants_getByIndex(0);
  TEST_ASSERT_NOT_NULL(p);
}

void test_get_by_index_negative_returns_null() {
  TEST_ASSERT_NULL(plants_getByIndex(-1));
}

void test_get_by_index_out_of_range_returns_null() {
  TEST_ASSERT_NULL(plants_getByIndex(999));
}

void test_default_plant_params_are_sane() {
  PlantConfig* p = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_TRUE(p->lightHours > 0);
  // PE-tavoitteet (pe-ohjausmalli.md) korvasivat tempMin/Max 18.7.2026.
  // Kasvuvaiheella pitaa olla jarkeva DLI-tavoite ja VPD-alue jarjestyksessa.
  TEST_ASSERT_TRUE(p->phaseCount > 0);
  bool foundVeg = false;
  for (uint8_t i = 0; i < p->phaseCount; i++) {
    if (p->phases[i].type == GROW_PHASE_VEGETATIVE) {
      foundVeg = true;
      TEST_ASSERT_TRUE(p->phases[i].dliTargetMol > 0);
      TEST_ASSERT_TRUE(p->phases[i].vpdMaxKpa > p->phases[i].vpdMinKpa);
    }
  }
  TEST_ASSERT_TRUE(foundVeg);
}

void test_basil_defaults_include_guidance_texts() {
  PlantConfig* p = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_UINT8(4, p->phaseCount);

  for (uint8_t i = 0; i < p->phaseCount; i++) {
    TEST_ASSERT_TRUE(strlen(p->phases[i].guidance) > 0);
  }
}

void test_all_default_plants_have_guided_phases() {
  const char* ids[] = {"basil", "parsley", "dill", "mint", "chives", "cilantro"};

  for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
    PlantConfig* p = plants_getById(ids[i]);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(p->phaseCount > 0);
    TEST_ASSERT_TRUE(strlen(p->phases[0].guidance) > 0);
  }
}

void test_testikasvi_exists_with_custom_phase() {
  PlantConfig* p = plants_getById("testikasvi");
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_STRING("Testikasvi", p->name);
  TEST_ASSERT_EQUAL_UINT8(1, p->phaseCount);
  TEST_ASSERT_EQUAL_INT(GROW_PHASE_CUSTOM, p->phases[0].type);
  TEST_ASSERT_TRUE(p->phases[0].floodIntervalMin > 0);
}

// ── M1 PE-mukavuuskaistat (11.8.2026) ───────────────────────────────────
// Nama olivat aiemmin vain e-inkin status_targets.h:n PE_TARGETS_VEGETATIVE-
// taulukossa; kPePhaseBands[GROW_PHASE_VEGETATIVE] siirsi tismalleen samat
// luvut tanne. Regressio tassa nakyisi vaarana bannerina seinataululla.
void test_vegetative_pe_bands_match_migrated_values() {
  PlantConfig* p = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(p);
  for (uint8_t i = 0; i < p->phaseCount; i++) {
    if (p->phases[i].type != GROW_PHASE_VEGETATIVE) continue;
    const GrowPhaseParams& gp = p->phases[i];
    TEST_ASSERT_EQUAL_FLOAT(12.0f, gp.dliMinMol);
    TEST_ASSERT_EQUAL_FLOAT(17.0f, gp.dliMaxMol);
    TEST_ASSERT_EQUAL_FLOAT(200.0f, gp.ppfdMinUmol);
    TEST_ASSERT_EQUAL_FLOAT(400.0f, gp.ppfdMaxUmol);
    TEST_ASSERT_EQUAL_FLOAT(400.0f, gp.co2MinPpm);
    TEST_ASSERT_EQUAL_FLOAT(800.0f, gp.co2MaxPpm);
    return;
  }
  TEST_FAIL_MESSAGE("basil has no VEGETATIVE phase");
}

// Bands must be a real min < max window on every built-in phase, or the
// published /api/state target would classify every reading as one extreme.
void test_all_built_in_phases_have_ordered_bands() {
  for (int i = 0; i < plants_getCount(); i++) {
    PlantConfig* p = plants_getByIndex(i);
    for (uint8_t j = 0; j < p->phaseCount; j++) {
      const GrowPhaseParams& gp = p->phases[j];
      TEST_ASSERT_TRUE_MESSAGE(gp.dliMinMol < gp.dliMaxMol, p->id);
      TEST_ASSERT_TRUE_MESSAGE(gp.ppfdMinUmol < gp.ppfdMaxUmol, p->id);
      TEST_ASSERT_TRUE_MESSAGE(gp.co2MinPpm < gp.co2MaxPpm, p->id);
    }
  }
}

// CUSTOM (testikasvi) falls back to the VEGETATIVE band set — same fallback
// the e-ink side used for an unrecognized phase name before M1.
void test_custom_phase_uses_vegetative_bands() {
  PlantConfig* p = plants_getById("testikasvi");
  TEST_ASSERT_NOT_NULL(p);
  const GrowPhaseParams& gp = p->phases[0];
  TEST_ASSERT_EQUAL_FLOAT(12.0f, gp.dliMinMol);
  TEST_ASSERT_EQUAL_FLOAT(17.0f, gp.dliMaxMol);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_load_defaults_has_entries);
  RUN_TEST(test_get_by_id_basil_exists);
  RUN_TEST(test_get_by_id_nonexistent_returns_null);
  RUN_TEST(test_get_by_index_zero_returns_valid);
  RUN_TEST(test_get_by_index_negative_returns_null);
  RUN_TEST(test_get_by_index_out_of_range_returns_null);
  RUN_TEST(test_default_plant_params_are_sane);
  RUN_TEST(test_basil_defaults_include_guidance_texts);
  RUN_TEST(test_all_default_plants_have_guided_phases);
  RUN_TEST(test_testikasvi_exists_with_custom_phase);
  RUN_TEST(test_vegetative_pe_bands_match_migrated_values);
  RUN_TEST(test_all_built_in_phases_have_ordered_bands);
  RUN_TEST(test_custom_phase_uses_vegetative_bands);
  return UNITY_END();
}
