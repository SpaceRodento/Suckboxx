/*
  test_config_reset — per-nakyman "Palauta oletukset" -kenttapalautus.

  Lukitsee config_reset.h:n whitelistin molempiin suuntiin: saannolliset
  saatoasetukset palautuvat config_setDefaults()-arvoihin, ja salaisuudet/
  kasvatustila/kayttoonotto EIVAT ole palautettavissa tasta polusta —
  wifi-avaimen hyvaksyminen olisi hiljainen verkosta-pudotus (sama
  vikaluokka jonka PR #212 korjasi salaisuuksien maski-roundtripista).

  Palvelee: POST /api/config/reset (wifi_portal_routes_post.h) + portaalin
  resetView() (wifi_portal_html_script.h). Vaatimus:
  docs/kehitys/web-ui-uudelleensuunnittelu.md §8.1.
*/

#include <unity.h>

// Shared feature flags for native tests (test/stubs/test_feature_flags.h)
#include "test_feature_flags.h"

#include "config_defaults.h"
#include "config_reset.h"

void setUp()    {}
void tearDown() {}

// Apuri: cfg jossa kenttia on sotkettu + koskematon defaults-referenssi.
static void makePair(DeviceConfig* cfg, DeviceConfig* defs) {
  *cfg = DeviceConfig{};
  *defs = DeviceConfig{};
  config_setDefaults(cfg);
  config_setDefaults(defs);
}

// ═══════════════════════════════════════════════════════════════════════════
// Palautettavat kentat
// ═══════════════════════════════════════════════════════════════════════════

void test_light_on_hour_restores(void) {
  DeviceConfig cfg, defs;
  makePair(&cfg, &defs);
  cfg.lightOnHour = 23;
  TEST_ASSERT_TRUE(config_resetField(&cfg, &defs, "light_on_hour"));
  TEST_ASSERT_EQUAL(defs.lightOnHour, cfg.lightOnHour);
}

void test_lora_fields_restore(void) {
  DeviceConfig cfg, defs;
  makePair(&cfg, &defs);
  cfg.loraAddress = 99;
  cfg.loraNetworkId = 7;
  cfg.loraTargetAddress = 42;
  TEST_ASSERT_TRUE(config_resetField(&cfg, &defs, "lora_address"));
  TEST_ASSERT_TRUE(config_resetField(&cfg, &defs, "lora_network_id"));
  TEST_ASSERT_TRUE(config_resetField(&cfg, &defs, "lora_target"));
  TEST_ASSERT_EQUAL(defs.loraAddress, cfg.loraAddress);
  TEST_ASSERT_EQUAL(defs.loraNetworkId, cfg.loraNetworkId);
  TEST_ASSERT_EQUAL(defs.loraTargetAddress, cfg.loraTargetAddress);
}

// Ebb-ajat: palautus = 0 ("kayta kasvuvaiheen / config.h:n oletusta").
// Tama on juuri se live-saatojen peruutuspolku jonka §8.1 vaatii.
void test_ebb_overrides_restore(void) {
  DeviceConfig cfg, defs;
  makePair(&cfg, &defs);
  cfg.ebbFloodIntervalMin = 3;
  cfg.ebbSoakPwmPct = 55;
  cfg.ebbOverflowAutoClear = !defs.ebbOverflowAutoClear;
  TEST_ASSERT_TRUE(config_resetField(&cfg, &defs, "ebb_flood_interval_min"));
  TEST_ASSERT_TRUE(config_resetField(&cfg, &defs, "ebb_soak_pwm_pct"));
  TEST_ASSERT_TRUE(config_resetField(&cfg, &defs, "ebb_overflow_auto_clear"));
  TEST_ASSERT_EQUAL(defs.ebbFloodIntervalMin, cfg.ebbFloodIntervalMin);
  TEST_ASSERT_EQUAL(defs.ebbSoakPwmPct, cfg.ebbSoakPwmPct);
  TEST_ASSERT_EQUAL(defs.ebbOverflowAutoClear, cfg.ebbOverflowAutoClear);
}

void test_button_action_restores(void) {
  DeviceConfig cfg, defs;
  makePair(&cfg, &defs);
  cfg.buttonAction = 3;
  TEST_ASSERT_TRUE(config_resetField(&cfg, &defs, "button_action"));
  TEST_ASSERT_EQUAL(defs.buttonAction, cfg.buttonAction);
}

// Palautus koskee VAIN pyydettya kenttaa — muut sailyvat.
void test_reset_touches_only_named_field(void) {
  DeviceConfig cfg, defs;
  makePair(&cfg, &defs);
  cfg.lightOnHour = 23;
  cfg.loraAddress = 99;
  TEST_ASSERT_TRUE(config_resetField(&cfg, &defs, "light_on_hour"));
  TEST_ASSERT_EQUAL(defs.lightOnHour, cfg.lightOnHour);
  TEST_ASSERT_EQUAL(99, cfg.loraAddress);
}

// ═══════════════════════════════════════════════════════════════════════════
// Whitelistin ulkopuoliset — EIVAT palaudu tasta polusta
// ═══════════════════════════════════════════════════════════════════════════

void test_secrets_are_rejected(void) {
  DeviceConfig cfg, defs;
  makePair(&cfg, &defs);
  TEST_ASSERT_FALSE(config_resetField(&cfg, &defs, "wifi_ssid"));
  TEST_ASSERT_FALSE(config_resetField(&cfg, &defs, "wifi_password"));
  TEST_ASSERT_FALSE(config_resetField(&cfg, &defs, "admin_pin"));
}

void test_grow_state_is_rejected(void) {
  DeviceConfig cfg, defs;
  makePair(&cfg, &defs);
  TEST_ASSERT_FALSE(config_resetField(&cfg, &defs, "plant_id"));
  TEST_ASSERT_FALSE(config_resetField(&cfg, &defs, "grow_start_method"));
}

void test_unknown_key_is_rejected(void) {
  DeviceConfig cfg, defs;
  makePair(&cfg, &defs);
  TEST_ASSERT_FALSE(config_resetField(&cfg, &defs, "no_such_key"));
  TEST_ASSERT_FALSE(config_resetField(&cfg, &defs, ""));
}

void test_null_args_are_rejected(void) {
  DeviceConfig cfg, defs;
  makePair(&cfg, &defs);
  TEST_ASSERT_FALSE(config_resetField(NULL, &defs, "light_on_hour"));
  TEST_ASSERT_FALSE(config_resetField(&cfg, NULL, "light_on_hour"));
  TEST_ASSERT_FALSE(config_resetField(&cfg, &defs, NULL));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_light_on_hour_restores);
  RUN_TEST(test_lora_fields_restore);
  RUN_TEST(test_ebb_overrides_restore);
  RUN_TEST(test_button_action_restores);
  RUN_TEST(test_reset_touches_only_named_field);
  RUN_TEST(test_secrets_are_rejected);
  RUN_TEST(test_grow_state_is_rejected);
  RUN_TEST(test_unknown_key_is_rejected);
  RUN_TEST(test_null_args_are_rejected);
  return UNITY_END();
}
