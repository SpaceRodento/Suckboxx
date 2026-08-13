#include <unity.h>

// Feature flags BEFORE includes — use test_feature_flags defaults.
#include "test/stubs/test_feature_flags.h"

// LittleFS stub (must come before plant_database.h which includes <LittleFS.h>)
#include "test/stubs/LittleFS.h"

// Project headers
#include "config.h"
#include "structs.h"
#include "schema_migration.h"
#include "plant_lookup.h"
#include "plant_database.h"

// ═══════════════════════════════════════════════════════════════════
// TEST: Load missing file uses defaults
// ═══════════════════════════════════════════════════════════════════

void test_load_missing_file_uses_defaults(void) {
  // LittleFS is empty, no injection
  bool result = plants_load();

  // Should return true (fell back to defaults)
  TEST_ASSERT_TRUE(result);

  // Should have loaded defaults
  int count = plants_getCount();
  TEST_ASSERT_GREATER_THAN(0, count);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Load empty file uses defaults
// ═══════════════════════════════════════════════════════════════════

void test_load_empty_file_uses_defaults(void) {
  // Inject empty file
  LittleFS.littlefs_inject("/plants.json", "");

  bool result = plants_load();

  // Empty file should return false but fall back to defaults
  TEST_ASSERT_FALSE(result);

  // Should have defaults loaded
  int count = plants_getCount();
  TEST_ASSERT_GREATER_THAN(0, count);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Load corrupt JSON uses defaults
// ═══════════════════════════════════════════════════════════════════

void test_load_corrupt_json_uses_defaults(void) {
  // Inject invalid JSON
  LittleFS.littlefs_inject("/plants.json", "this is not json {{{");

  bool result = plants_load();

  // Corrupt JSON should return false but fall back to defaults
  TEST_ASSERT_FALSE(result);

  // Should have defaults loaded
  int count = plants_getCount();
  TEST_ASSERT_GREATER_THAN(0, count);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Load valid JSON overrides defaults
// ═══════════════════════════════════════════════════════════════════

void test_load_valid_json_overrides_defaults(void) {
  // Inject valid JSON with one custom plant
  const char* json = R"({
    "schema_version":1,
    "testplant":{
      "name":"Test",
      "max_height_mm":200,
      "light_hours":12,
      "water_ml_per_dose":20,
      "water_interval_hours":8,
      "tds_target_ppm":500,
      "temp_min_c":18.0,
      "temp_max_c":28.0
    }
  })";
  LittleFS.littlefs_inject("/plants.json", json);

  bool result = plants_load();

  // Valid JSON should load successfully
  TEST_ASSERT_TRUE(result);

  // The custom file entry loads AND missing built-ins are merged in (plants_load
  // merges built-in species absent from plants.json so new firmware plants
  // surface). Verify by id presence rather than an exact count.
  int count = plants_getCount();
  TEST_ASSERT_TRUE(count > 1);
  TEST_ASSERT_NOT_NULL(plants_getById("basil"));  // a built-in was merged

  // Plant should be findable by ID
  PlantConfig* plant = plants_getById("testplant");
  TEST_ASSERT_NOT_NULL(plant);

  // Check name
  TEST_ASSERT_EQUAL_STRING("Test", plant->name);

  // Check maxHeightMm
  TEST_ASSERT_EQUAL(200, plant->maxHeightMm);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Load missing required field skips entry
// ═══════════════════════════════════════════════════════════════════

void test_load_missing_required_field_skips_entry(void) {
  // Inject JSON with two plants: one complete, one missing light_hours
  const char* json = R"({
    "schema_version":1,
    "good":{
      "name":"Good Plant",
      "max_height_mm":300,
      "light_hours":14,
      "water_ml_per_dose":25,
      "water_interval_hours":8,
      "tds_target_ppm":600,
      "temp_min_c":18.0,
      "temp_max_c":28.0
    },
    "bad":{
      "name":"Bad Plant",
      "max_height_mm":300,
      "water_ml_per_dose":25,
      "water_interval_hours":8,
      "tds_target_ppm":600,
      "temp_min_c":18.0,
      "temp_max_c":28.0
    }
  })";
  LittleFS.littlefs_inject("/plants.json", json);

  bool result = plants_load();

  // Load should succeed (at least one valid entry)
  TEST_ASSERT_TRUE(result);

  // 'good' loads, 'bad' (missing required field) is skipped, built-ins merged.
  // Verified by id presence (merge adds built-ins, so exact count is not 1).
  PlantConfig* good = plants_getById("good");
  TEST_ASSERT_NOT_NULL(good);

  // Bad plant should not be found (rejected, and not a built-in id)
  PlantConfig* bad = plants_getById("bad");
  TEST_ASSERT_NULL(bad);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Load out-of-range value rejects entry
// ═══════════════════════════════════════════════════════════════════

void test_load_out_of_range_value_rejects_entry(void) {
  // Inject JSON with out-of-range max_height_mm (limit is 3000)
  const char* json = R"({
    "schema_version":1,
    "toobig":{
      "name":"Too Big",
      "max_height_mm":99999,
      "light_hours":14,
      "water_ml_per_dose":25,
      "water_interval_hours":8,
      "tds_target_ppm":600,
      "temp_min_c":18.0,
      "temp_max_c":28.0
    }
  })";
  LittleFS.littlefs_inject("/plants.json", json);

  bool result = plants_load();

  // All entries rejected → falls back to defaults, returns false
  TEST_ASSERT_FALSE(result);

  // Should have defaults loaded (count > 0)
  int count = plants_getCount();
  TEST_ASSERT_GREATER_THAN(0, count);

  // "toobig" should not be in the list
  PlantConfig* toobig = plants_getById("toobig");
  TEST_ASSERT_NULL(toobig);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Save writes schema_version
// ═══════════════════════════════════════════════════════════════════

void test_save_writes_schema_version(void) {
  // Load defaults
  plants_loadDefaults();

  // Save to LittleFS
  bool result = plants_save();

  // Save should succeed
  TEST_ASSERT_TRUE(result);

  // Read back the content
  std::string content = LittleFS.littlefs_read("/plants.json");

  // Should contain schema_version key
  TEST_ASSERT_TRUE(content.find("\"schema_version\"") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Save then load roundtrip
// ═══════════════════════════════════════════════════════════════════

void test_save_then_load_roundtrip(void) {
  // Load defaults
  plants_loadDefaults();

  // Record original count and basil data
  int originalCount = plants_getCount();
  TEST_ASSERT_GREATER_THAN(0, originalCount);

  PlantConfig* basil = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(basil);
  int originalBasilHeight = basil->maxHeightMm;

  // Save to LittleFS
  bool saveResult = plants_save();
  TEST_ASSERT_TRUE(saveResult);

  // Clear in-memory state
  g_plantCount = 0;

  // Load from LittleFS
  bool loadResult = plants_load();
  TEST_ASSERT_TRUE(loadResult);

  // Count should match
  int newCount = plants_getCount();
  TEST_ASSERT_EQUAL(originalCount, newCount);

  // Basil should be present with same height
  PlantConfig* basilAgain = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(basilAgain);
  TEST_ASSERT_EQUAL(originalBasilHeight, basilAgain->maxHeightMm);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Built-in ID without phases in JSON inherits built-in phases (I1-F1)
// ═══════════════════════════════════════════════════════════════════

void test_builtin_id_without_phases_inherits_builtin_phases(void) {
  // "basil" exists as a built-in with 4 phases. JSON entry has flat fields
  // only — no "phases" key. After load, basil must have phases (F1).
  const char* json = R"({
    "schema_version":1,
    "basil":{
      "name":"Basil Custom",
      "max_height_mm":300,
      "light_hours":16,
      "water_ml_per_dose":100,
      "water_interval_hours":4,
      "tds_target_ppm":800,
      "temp_min_c":18.0,
      "temp_max_c":30.0
    }
  })";
  LittleFS.littlefs_inject("/plants.json", json);

  bool result = plants_load();
  TEST_ASSERT_TRUE(result);

  PlantConfig* p = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(p);

  // Flat field override must be preserved
  TEST_ASSERT_EQUAL_STRING("Basil Custom", p->name);
  TEST_ASSERT_EQUAL(16, p->lightHours);

  // Phases must have been inherited from built-in
  TEST_ASSERT_GREATER_THAN(0, p->phaseCount);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Custom (unknown) ID without phases keeps phaseCount==0 (I1-F1)
// ═══════════════════════════════════════════════════════════════════

void test_custom_id_without_phases_stays_empty(void) {
  // "myfern" has no built-in counterpart — phaseCount stays 0.
  const char* json = R"({
    "schema_version":1,
    "myfern":{
      "name":"My Fern",
      "max_height_mm":200,
      "light_hours":10,
      "water_ml_per_dose":50,
      "water_interval_hours":6,
      "tds_target_ppm":400,
      "temp_min_c":15.0,
      "temp_max_c":25.0
    }
  })";
  LittleFS.littlefs_inject("/plants.json", json);

  bool result = plants_load();
  TEST_ASSERT_TRUE(result);

  PlantConfig* p = plants_getById("myfern");
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL(0, p->phaseCount);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: Phases survive save → load round-trip (I1-F1 regression guard)
// ═══════════════════════════════════════════════════════════════════

void test_phases_preserved_through_save_load(void) {
  // Inject basil without phases → load inherits built-in phases → save → load again.
  const char* json = R"({
    "schema_version":1,
    "basil":{
      "name":"Basil",
      "max_height_mm":300,
      "light_hours":16,
      "water_ml_per_dose":100,
      "water_interval_hours":4,
      "tds_target_ppm":800,
      "temp_min_c":18.0,
      "temp_max_c":30.0
    }
  })";
  LittleFS.littlefs_inject("/plants.json", json);

  bool r1 = plants_load();
  TEST_ASSERT_TRUE(r1);

  PlantConfig* p1 = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(p1);
  int phaseCountAfterLoad = p1->phaseCount;
  TEST_ASSERT_GREATER_THAN(0, phaseCountAfterLoad);

  // Save (now with phases written)
  bool saveOk = plants_save();
  TEST_ASSERT_TRUE(saveOk);

  g_plantCount = 0;

  // Second load — phases now come from JSON, not inheritance
  bool r2 = plants_load();
  TEST_ASSERT_TRUE(r2);

  PlantConfig* p2 = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(p2);
  TEST_ASSERT_EQUAL(phaseCountAfterLoad, p2->phaseCount);
}

// ═══════════════════════════════════════════════════════════════════
// TEST: M1 PE band migration (11.8.2026) — plants.json written before this
// change has phase entries with the old target/vpd keys but none of the new
// dli_min_mol/dli_max_mol/ppfd_min_umol/ppfd_max_umol/co2_min_ppm/
// co2_max_ppm keys. plants_load() must backfill them from the matching
// built-in (plant_lookup.h plants_fillMissingBands()) instead of leaving
// them at 0 — a 0/0 band would classify every /api/state reading as HIGH.
// ═══════════════════════════════════════════════════════════════════

void test_load_backfills_missing_pe_bands_from_builtin(void) {
  const char* json = R"({
    "schema_version":1,
    "basil":{
      "name":"Basilika",
      "max_height_mm":400,
      "light_hours":14,
      "water_ml_per_dose":25,
      "water_interval_hours":8,
      "phases":[
        {
          "type":2,
          "label":"Kasvuvaihe",
          "guidance":"g",
          "duration_days":21,
          "light_hours":16,
          "flood_interval_min":180,
          "flood_duration_sec":45,
          "dli_target_mol":14,
          "ppfd_target_umol":300,
          "vpd_min_kpa":0.8,
          "vpd_max_kpa":1.2,
          "co2_target_ppm":600,
          "leaf_temp_max_c":28.0
        }
      ]
    }
  })";
  LittleFS.littlefs_inject("/plants.json", json);

  bool result = plants_load();
  TEST_ASSERT_TRUE(result);

  PlantConfig* p = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL(1, p->phaseCount);
  const GrowPhaseParams& gp = p->phases[0];
  // Old fields parsed as usual...
  TEST_ASSERT_EQUAL(14, gp.dliTargetMol);
  // ...new band fields backfilled from the built-in VEGETATIVE band, not 0.
  TEST_ASSERT_EQUAL_FLOAT(12.0f, gp.dliMinMol);
  TEST_ASSERT_EQUAL_FLOAT(17.0f, gp.dliMaxMol);
  TEST_ASSERT_EQUAL_FLOAT(200.0f, gp.ppfdMinUmol);
  TEST_ASSERT_EQUAL_FLOAT(400.0f, gp.ppfdMaxUmol);
  TEST_ASSERT_EQUAL_FLOAT(400.0f, gp.co2MinPpm);
  TEST_ASSERT_EQUAL_FLOAT(800.0f, gp.co2MaxPpm);
}

// A plants.json already written by this firmware (new keys present) must
// win over the built-in fallback — a user's saved band edit is not silently
// overridden just because plants_fillMissingBands() runs unconditionally.
void test_load_keeps_explicit_pe_bands_when_present(void) {
  const char* json = R"({
    "schema_version":1,
    "basil":{
      "name":"Basilika",
      "max_height_mm":400,
      "light_hours":14,
      "water_ml_per_dose":25,
      "water_interval_hours":8,
      "phases":[
        {
          "type":2,
          "label":"Kasvuvaihe",
          "guidance":"g",
          "duration_days":21,
          "light_hours":16,
          "flood_interval_min":180,
          "flood_duration_sec":45,
          "dli_target_mol":14,
          "ppfd_target_umol":300,
          "vpd_min_kpa":0.8,
          "vpd_max_kpa":1.2,
          "co2_target_ppm":600,
          "leaf_temp_max_c":28.0,
          "dli_min_mol":10.0,
          "dli_max_mol":20.0,
          "ppfd_min_umol":150.0,
          "ppfd_max_umol":450.0,
          "co2_min_ppm":500.0,
          "co2_max_ppm":900.0
        }
      ]
    }
  })";
  LittleFS.littlefs_inject("/plants.json", json);

  TEST_ASSERT_TRUE(plants_load());

  PlantConfig* p = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(p);
  const GrowPhaseParams& gp = p->phases[0];
  TEST_ASSERT_EQUAL_FLOAT(10.0f, gp.dliMinMol);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, gp.dliMaxMol);
}

// ═══════════════════════════════════════════════════════════════════
// TEST SETUP / TEARDOWN
// ═══════════════════════════════════════════════════════════════════

void setUp(void) {
  // Clear LittleFS stub and reset plant count before each test
  LittleFS.littlefs_clear();
  g_plantCount = 0;
}

void tearDown(void) {
  // Cleanup after each test
}

// ═══════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_load_missing_file_uses_defaults);
  RUN_TEST(test_load_empty_file_uses_defaults);
  RUN_TEST(test_load_corrupt_json_uses_defaults);
  RUN_TEST(test_load_valid_json_overrides_defaults);
  RUN_TEST(test_load_missing_required_field_skips_entry);
  RUN_TEST(test_load_out_of_range_value_rejects_entry);
  RUN_TEST(test_save_writes_schema_version);
  RUN_TEST(test_save_then_load_roundtrip);

  // I1: phase inheritance tests
  RUN_TEST(test_builtin_id_without_phases_inherits_builtin_phases);
  RUN_TEST(test_custom_id_without_phases_stays_empty);
  RUN_TEST(test_phases_preserved_through_save_load);
  RUN_TEST(test_load_backfills_missing_pe_bands_from_builtin);
  RUN_TEST(test_load_keeps_explicit_pe_bands_when_present);

  return UNITY_END();
}
