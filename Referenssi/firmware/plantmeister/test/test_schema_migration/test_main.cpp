#include <unity.h>

#include "test_feature_flags.h"
#include "schema_migration.h"

void setUp()    {}
void tearDown() {}

// ── Current == CURRENT ──────────────────────────────────────
void test_decide_current_version_ok() {
  TEST_ASSERT_EQUAL_INT(SCHEMA_OK_AS_IS,
                        schema_decide(1, 1, 1));
}

void test_decide_current_with_future_floor() {
  // file v5, min v2, current v5 — exact match
  TEST_ASSERT_EQUAL_INT(SCHEMA_OK_AS_IS,
                        schema_decide(5, 2, 5));
}

// ── Missing key (== 0) ────────────────────────────────────
void test_decide_v0_treated_as_migrate_when_min_is_1() {
  // Pre-versioning file: v0 -> migrate to v1 (no structural change).
  TEST_ASSERT_EQUAL_INT(SCHEMA_NEEDS_MIGRATE,
                        schema_decide(0, 1, 1));
}

void test_decide_v0_resets_when_min_above_1() {
  // If we ever bump MIN above 1, v0 files become unsupported.
  TEST_ASSERT_EQUAL_INT(SCHEMA_RESET_DEFAULTS,
                        schema_decide(0, 2, 3));
}

// ── Older than current but >= min ─────────────────────────────
void test_decide_older_within_min_migrates() {
  // file v1, min v1, current v3 — migrate
  TEST_ASSERT_EQUAL_INT(SCHEMA_NEEDS_MIGRATE,
                        schema_decide(1, 1, 3));
}

void test_decide_just_below_current_migrates() {
  TEST_ASSERT_EQUAL_INT(SCHEMA_NEEDS_MIGRATE,
                        schema_decide(2, 1, 3));
}

// ── Below min ───────────────────────────────────────────
void test_decide_below_min_resets() {
  // file v1, min v3, current v5 — too old, drop file
  TEST_ASSERT_EQUAL_INT(SCHEMA_RESET_DEFAULTS,
                        schema_decide(1, 3, 5));
}

// ── Above current (forward incompatibility) ─────────────────────
void test_decide_future_version_resets() {
  // File written by newer firmware — refuse to interpret unknown semantics.
  TEST_ASSERT_EQUAL_INT(SCHEMA_RESET_DEFAULTS,
                        schema_decide(5, 1, 1));
}

void test_decide_one_above_current_resets() {
  TEST_ASSERT_EQUAL_INT(SCHEMA_RESET_DEFAULTS,
                        schema_decide(3, 1, 2));
}

// ── PlantMeister-specific constants sanity check ─────────────────
void test_config_constants_present() {
  // v5 (18.7.2026): DeviceConfig.devMode (Huolto-valilehden nakyvyys).
  // v4 (18.7.2026): PE-tavoitteet korvasivat arvaus-parametrit
  // (docs/kehitys/pe-ohjausmalli.md).
  TEST_ASSERT_EQUAL_UINT8(5, SCHEMA_VERSION_CONFIG_CURRENT);
  TEST_ASSERT_EQUAL_UINT8(1, SCHEMA_VERSION_CONFIG_MIN);
}

// v5 additive: a v4 config file migrates forward (devMode defaults false);
// a v5 file read by v4 firmware is forward-incompatible -> reset to defaults.
void test_config_v5_additive_migration() {
  TEST_ASSERT_EQUAL_INT(
    SCHEMA_NEEDS_MIGRATE,
    schema_decide(4, SCHEMA_VERSION_CONFIG_MIN, SCHEMA_VERSION_CONFIG_CURRENT));
  TEST_ASSERT_EQUAL_INT(
    SCHEMA_RESET_DEFAULTS,
    schema_decide(5, SCHEMA_VERSION_CONFIG_MIN, /*v4 firmware*/4));
}

void test_calibration_constants_present() {
  TEST_ASSERT_EQUAL_UINT8(4, SCHEMA_VERSION_CALIBRATION_CURRENT);  // v4: ppfd_calibration_factor (geometry lisatty ilman bumppia)
  TEST_ASSERT_EQUAL_UINT8(1, SCHEMA_VERSION_CALIBRATION_MIN);
}

void test_plants_constants_present() {
  TEST_ASSERT_EQUAL_UINT8(1, SCHEMA_VERSION_PLANTS_CURRENT);
  TEST_ASSERT_EQUAL_UINT8(1, SCHEMA_VERSION_PLANTS_MIN);
}

// Real PlantMeister scenario: existing device boots after this PR ships.
// File on disk has no schema_version (v0), firmware writes v1.
void test_real_scenario_existing_device_upgrades() {
  // Config
  TEST_ASSERT_EQUAL_INT(
    SCHEMA_NEEDS_MIGRATE,
    schema_decide(0, SCHEMA_VERSION_CONFIG_MIN, SCHEMA_VERSION_CONFIG_CURRENT));
  // Calibration (v0 file, MIN=1, CURRENT=2 -> migrate)
  TEST_ASSERT_EQUAL_INT(
    SCHEMA_NEEDS_MIGRATE,
    schema_decide(0, SCHEMA_VERSION_CALIBRATION_MIN, SCHEMA_VERSION_CALIBRATION_CURRENT));
  // Plants
  TEST_ASSERT_EQUAL_INT(
    SCHEMA_NEEDS_MIGRATE,
    schema_decide(0, SCHEMA_VERSION_PLANTS_MIN, SCHEMA_VERSION_PLANTS_CURRENT));
}

// Real PlantMeister scenario: calibration file at v1 with current=2 -> migrate.
void test_real_scenario_v1_calib_file_migrates() {
  TEST_ASSERT_EQUAL_INT(SCHEMA_NEEDS_MIGRATE,
                        schema_decide(1, SCHEMA_VERSION_CALIBRATION_MIN,
                                         SCHEMA_VERSION_CALIBRATION_CURRENT));
}

// Real PlantMeister scenario: file has matching version — no rewrite needed.
void test_real_scenario_v1_file_loads_clean() {
  TEST_ASSERT_EQUAL_INT(SCHEMA_OK_AS_IS,
                        schema_decide(1, 1, 1));
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_decide_current_version_ok);
  RUN_TEST(test_decide_current_with_future_floor);
  RUN_TEST(test_decide_v0_treated_as_migrate_when_min_is_1);
  RUN_TEST(test_decide_v0_resets_when_min_above_1);
  RUN_TEST(test_decide_older_within_min_migrates);
  RUN_TEST(test_decide_just_below_current_migrates);
  RUN_TEST(test_decide_below_min_resets);
  RUN_TEST(test_decide_future_version_resets);
  RUN_TEST(test_decide_one_above_current_resets);
  RUN_TEST(test_config_constants_present);
  RUN_TEST(test_config_v5_additive_migration);
  RUN_TEST(test_calibration_constants_present);
  RUN_TEST(test_plants_constants_present);
  RUN_TEST(test_real_scenario_existing_device_upgrades);
  RUN_TEST(test_real_scenario_v1_calib_file_migrates);
  RUN_TEST(test_real_scenario_v1_file_loads_clean);

  return UNITY_END();
}
