/*
  test_config_roundtrip — I3: config save/load round-trip protection.

  Guards three properties:
  1. Round-trip fidelity: every field saved by config_save() is recovered
     correctly by config_load().
  2. Key count sentinel: CONFIG_SAVE_KEY_COUNT_SENTINEL must equal the number
     of top-level keys written by config_save(). Fails if a new field is
     added to save but the sentinel is not bumped (catches drift early).
  3. Load-doc size: StaticJsonDocument<1024> for load must not overflow when
     all 30 keys are present (if it did, fields would silently revert to
     their default values and the round-trip test would catch it).
*/

#include <unity.h>

#include "test/stubs/test_feature_flags.h"
#include "test/stubs/LittleFS.h"

#include "config.h"
#include "structs.h"
#include "schema_migration.h"
#include "config_defaults.h"
#include "config_store.h"
#include "onboarding.h"

// ── Sentinel ─────────────────────────────────────────────────────────────────
// Number of top-level keys written by config_save() (including schema_version).
// Bump this constant every time a field is added or removed from config_save().
// The test_config_key_count_sentinel test will fail at runtime if the count
// drifts — no silent regressions from missing load counterparts.
static const int CONFIG_SAVE_KEY_COUNT_SENTINEL = 40;   // +grow_demo_mode (19.7.2026)

// ── Helpers ───────────────────────────────────────────────────────────────────

static DeviceConfig make_nondefault_config() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);

  // Override every field to a distinct non-default value
  cfg.loraAddress          = 77;
  cfg.loraNetworkId        = 88;
  cfg.loraTargetAddress    = 99;
  strncpy(cfg.wifiSsid,     "TestSSID",   sizeof(cfg.wifiSsid) - 1);
  strncpy(cfg.wifiPassword, "s3cret",     sizeof(cfg.wifiPassword) - 1);
  cfg.wifiAutoConnect      = true;
  strncpy(cfg.adminPin,     "1234",       sizeof(cfg.adminPin) - 1);
  strncpy(cfg.currentPlantId, "parsley",  sizeof(cfg.currentPlantId) - 1);
  cfg.lightOnHour          = 9;
  cfg.motorCurrentMm       = 123;
  cfg.motorTargetMm        = 200;
  cfg.sensorIntervalMs     = 30000UL;
  cfg.loraReportIntervalMs = 60000UL;
  cfg.lightsForceOn        = true;
  cfg.lightsForceOff       = false;
  cfg.growPhase            = 2;
  cfg.growElapsedDays      = 14;
  cfg.growActive           = true;
  cfg.growStartMethod      = 1;
  cfg.growStepIndex        = 2;
  cfg.ebbFlowFaultLatched  = true;
  cfg.ebbFlowFaultCode     = 2;
  cfg.testMode             = true;
  cfg.devMode              = true;
  cfg.onboardingComplete   = true;
  cfg.onboardingSteps      = (uint8_t)(ONBOARD_STEP_PLANT | ONBOARD_STEP_METHOD);
  cfg.buttonAction         = 1;
  cfg.buttonLongAction     = 1;   // stored non-zero value; config_load() clamps it back to
                                   // BUTTON_LONG_ACTION_BACK (0) since D14 removed the only
                                   // other enum value — see the assertion below, not a
                                   // round-trip-fidelity check for this field anymore
  cfg.growDemoMode         = true;
  cfg.ebbFloodIntervalMin  = 180;
  cfg.ebbFloodDurationSec  = 45;
  cfg.ebbSoakDurationSec   = 300;
  cfg.ebbDrainTimeoutSec   = 600;
  cfg.ebbSoakPwmPct        = 35;
  cfg.ebbOverflowAutoClear = true;
  cfg.ebbCirculateEnabled     = true;
  cfg.ebbCirculateIntervalMin = 240;
  cfg.ebbCirculateDurationSec = 90;
  cfg.ebbCirculateDutyPct     = 55;

  return cfg;
}

// ═══════════════════════════════════════════════════════════════════════════════
// TEST: Full round-trip — all fields survive save → load
// ═══════════════════════════════════════════════════════════════════════════════

void test_config_roundtrip_all_fields(void) {
  DeviceConfig orig = make_nondefault_config();
  TEST_ASSERT_TRUE(config_save(&orig));

  DeviceConfig loaded = {};
  TEST_ASSERT_TRUE(config_load(&loaded));

  // Scalar fields
  TEST_ASSERT_EQUAL_UINT8(77,  loaded.loraAddress);
  TEST_ASSERT_EQUAL_UINT8(88,  loaded.loraNetworkId);
  TEST_ASSERT_EQUAL_UINT8(99,  loaded.loraTargetAddress);
  TEST_ASSERT_EQUAL_INT(9,     loaded.lightOnHour);
  TEST_ASSERT_EQUAL_INT(123,   loaded.motorCurrentMm);
  TEST_ASSERT_EQUAL_INT(200,   loaded.motorTargetMm);
  TEST_ASSERT_EQUAL_UINT32(30000UL, loaded.sensorIntervalMs);
  TEST_ASSERT_EQUAL_UINT32(60000UL, loaded.loraReportIntervalMs);
  TEST_ASSERT_TRUE(loaded.lightsForceOn);
  TEST_ASSERT_FALSE(loaded.lightsForceOff);
  TEST_ASSERT_EQUAL_UINT8(2,   loaded.growPhase);
  TEST_ASSERT_EQUAL_UINT32(14, loaded.growElapsedDays);
  TEST_ASSERT_TRUE(loaded.growActive);
  TEST_ASSERT_EQUAL_UINT8(1,   loaded.growStartMethod);
  TEST_ASSERT_EQUAL_UINT8(2,   loaded.growStepIndex);
  TEST_ASSERT_TRUE(loaded.ebbFlowFaultLatched);
  TEST_ASSERT_EQUAL_UINT8(2,   loaded.ebbFlowFaultCode);
  TEST_ASSERT_TRUE(loaded.testMode);
  TEST_ASSERT_TRUE(loaded.devMode);
  TEST_ASSERT_TRUE(loaded.onboardingComplete);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(ONBOARD_STEP_PLANT | ONBOARD_STEP_METHOD),
                           loaded.onboardingSteps);
  TEST_ASSERT_EQUAL_UINT8(1,   loaded.buttonAction);
  // Not raw fidelity: config_clampBounds() (called from config_load()) folds
  // any stored value above BUTTON_LONG_ACTION_MAX back to BACK (0) — D14
  // removed the only other enum value, so 1 is now an "old config" value.
  TEST_ASSERT_EQUAL_UINT8(0,   loaded.buttonLongAction);
  TEST_ASSERT_TRUE(loaded.growDemoMode);
  TEST_ASSERT_EQUAL_UINT16(180, loaded.ebbFloodIntervalMin);
  TEST_ASSERT_EQUAL_UINT16(45,  loaded.ebbFloodDurationSec);
  TEST_ASSERT_EQUAL_UINT16(300, loaded.ebbSoakDurationSec);
  TEST_ASSERT_EQUAL_UINT16(600, loaded.ebbDrainTimeoutSec);
  TEST_ASSERT_EQUAL_UINT8(35,   loaded.ebbSoakPwmPct);
  TEST_ASSERT_TRUE(loaded.ebbOverflowAutoClear);
  TEST_ASSERT_TRUE(loaded.ebbCirculateEnabled);
  TEST_ASSERT_EQUAL_UINT16(240, loaded.ebbCirculateIntervalMin);
  TEST_ASSERT_EQUAL_UINT16(90,  loaded.ebbCirculateDurationSec);
  TEST_ASSERT_EQUAL_UINT8(55,   loaded.ebbCirculateDutyPct);
  TEST_ASSERT_TRUE(loaded.wifiAutoConnect);

  // String fields
  TEST_ASSERT_EQUAL_STRING("TestSSID",  loaded.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("s3cret",    loaded.wifiPassword);
  TEST_ASSERT_EQUAL_STRING("1234",      loaded.adminPin);
  TEST_ASSERT_EQUAL_STRING("parsley",   loaded.currentPlantId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// TEST: Key count sentinel — config_save() writes exactly the expected
//       number of top-level keys. Add a new field → bump the sentinel.
// ═══════════════════════════════════════════════════════════════════════════════

void test_config_key_count_sentinel(void) {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_TRUE(config_save(&cfg));

  std::string raw = LittleFS.littlefs_read("/config.json");
  TEST_ASSERT_GREATER_THAN(0u, raw.size());

  // Parse and count top-level keys
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, raw.c_str());
  TEST_ASSERT_TRUE_MESSAGE(err == DeserializationError::Ok, err.c_str());

  int keyCount = 0;
  for (auto kv : doc.as<JsonObject>()) {
    (void)kv;
    keyCount++;
  }

  TEST_ASSERT_EQUAL_MESSAGE(CONFIG_SAVE_KEY_COUNT_SENTINEL, keyCount,
    "config_save() key count changed — update CONFIG_SAVE_KEY_COUNT_SENTINEL "
    "and add the matching config_load() read for any new field");
}

// ═══════════════════════════════════════════════════════════════════════════════
// TEST: Default config round-trip — defaults survive save → load
// ═══════════════════════════════════════════════════════════════════════════════

void test_config_defaults_roundtrip(void) {
  DeviceConfig orig = {};
  config_setDefaults(&orig);
  TEST_ASSERT_TRUE(config_save(&orig));

  DeviceConfig loaded = {};
  TEST_ASSERT_TRUE(config_load(&loaded));

  TEST_ASSERT_EQUAL_UINT8(LORA_DEVICE_ADDRESS, loaded.loraAddress);
  TEST_ASSERT_EQUAL_STRING("basil",            loaded.currentPlantId);
  TEST_ASSERT_EQUAL_INT(6,                     loaded.lightOnHour);
  TEST_ASSERT_FALSE(loaded.growActive);
  TEST_ASSERT_FALSE(loaded.ebbFlowFaultLatched);
  TEST_ASSERT_FALSE(loaded.ebbOverflowAutoClear);
  // Explicit false written by save must survive load (fresh device that saved
  // config e.g. via portal WiFi step BEFORE completing onboarding).
  TEST_ASSERT_FALSE(loaded.onboardingComplete);
}

// ═══════════════════════════════════════════════════════════════════════════════
// TEST: Onboarding migration — a config.json that PREDATES the
//       onboarding_complete field belongs to an already-set-up device and
//       must load as onboardingComplete=true. Regression guard: an OTA
//       update must never throw an operating device back into onboarding.
// ═══════════════════════════════════════════════════════════════════════════════

void test_config_onboarding_migration_old_file_is_complete(void) {
  // Minimal pre-onboarding config.json (schema v3, no onboarding_complete key)
  LittleFS.littlefs_inject("/config.json",
    "{\"schema_version\":3,\"plant_id\":\"parsley\",\"wifi_ssid\":\"Koti\","
    "\"grow_active\":true}");

  DeviceConfig cfg = {};
  TEST_ASSERT_TRUE(config_load(&cfg));
  TEST_ASSERT_TRUE_MESSAGE(cfg.onboardingComplete,
    "old config without onboarding_complete must load as TRUE (already set up)");
  TEST_ASSERT_EQUAL_STRING("parsley", cfg.currentPlantId);
}

// Fresh device: no config.json at all → defaults → onboarding NOT complete.
void test_config_onboarding_fresh_device_not_complete(void) {
  DeviceConfig cfg = {};
  TEST_ASSERT_TRUE(config_load(&cfg));
  TEST_ASSERT_FALSE_MESSAGE(cfg.onboardingComplete,
    "fresh device (no config.json) must start with onboarding incomplete");
}

// ═══════════════════════════════════════════════════════════════════════════════
// TEST: onboarding_steps migration — a config.json that PREDATES the
//       onboarding_steps field (same vintage as the onboarding_complete
//       migration above) must load as onboardingSteps=0. This default is
//       safe: a file this old has no onboarding_complete key either, so it
//       loads as onboardingComplete=true (see migration test above) — the
//       banner never shows and the step bitmask is never read. onboardingSteps
//       only matters while onboarding is incomplete, which this file already
//       is not.
// ═══════════════════════════════════════════════════════════════════════════════

void test_config_onboarding_steps_missing_key_defaults_to_zero(void) {
  // Same minimal pre-onboarding config.json as the migration test above —
  // no "onboarding_steps" key present.
  LittleFS.littlefs_inject("/config.json",
    "{\"schema_version\":3,\"plant_id\":\"parsley\",\"wifi_ssid\":\"Koti\","
    "\"grow_active\":true}");

  DeviceConfig cfg = {};
  TEST_ASSERT_TRUE(config_load(&cfg));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, cfg.onboardingSteps,
    "old config without onboarding_steps must default to 0 (harmless: such "
    "a file also predates onboarding_complete, which loads true, so the "
    "banner and step bitmask are never consulted)");
}

// ═══════════════════════════════════════════════════════════════════════════════
// TEST: Missing file uses defaults (no regression)
// ═══════════════════════════════════════════════════════════════════════════════

void test_config_missing_file_uses_defaults(void) {
  DeviceConfig cfg = {};
  bool ok = config_load(&cfg);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("basil", cfg.currentPlantId);
  TEST_ASSERT_EQUAL_INT(6,          cfg.lightOnHour);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SETUP / TEARDOWN
// ═══════════════════════════════════════════════════════════════════════════════

void setUp(void) {
  LittleFS.littlefs_clear();
}

void tearDown(void) {}

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_config_roundtrip_all_fields);
  RUN_TEST(test_config_key_count_sentinel);
  RUN_TEST(test_config_defaults_roundtrip);
  RUN_TEST(test_config_missing_file_uses_defaults);
  RUN_TEST(test_config_onboarding_migration_old_file_is_complete);
  RUN_TEST(test_config_onboarding_fresh_device_not_complete);
  RUN_TEST(test_config_onboarding_steps_missing_key_defaults_to_zero);

  return UNITY_END();
}
