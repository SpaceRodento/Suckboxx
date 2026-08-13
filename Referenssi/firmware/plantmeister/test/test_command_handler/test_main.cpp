#include <unity.h>

// Narrow feature surface for command handler unit tests.
#define ENABLE_LORA false
#define ENABLE_WIFI_PORTAL false
#define ENABLE_SENSORS false
#define ENABLE_TDS_SENSOR false
#define ENABLE_WATER_TEMP false
#define ENABLE_FLOAT_SWITCH false
#define ENABLE_HEIGHT_SENSOR false
#define ENABLE_ENV_SENSOR false
#define ENABLE_BATTERY_MONITOR false
#define ENABLE_MOTOR false
#define ENABLE_PUMP false
#define ENABLE_AIR_PUMP false
#define ENABLE_SENSOR_HISTORY false
#define ENABLE_GUIDED_GROWING true
#include "test_feature_flags.h"

#include "config_defaults.h"
#include "command_handler.h"
#include "device_state.h"

// Minimal stubs required by command_handler linkage.
bool config_save(const DeviceConfig*) { return true; }

void setUp() {
  g_stub_millis = 0;
  plants_loadDefaults();
  device_init();
  device_setState(DEVICE_IDLE);
}

void tearDown() {}

void test_unknown_command_does_not_change_flags() {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  PlantConfig* activePlant = plants_getById("basil");

  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("NONEXISTENT", "123", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_FALSE(cfg.lightsForceOn);
  TEST_ASSERT_FALSE(cfg.lightsForceOff);
}

void test_light_on_sets_force_flags() {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  PlantConfig* activePlant = plants_getById("basil");

  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("LIGHT", "1", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_TRUE(cfg.lightsForceOn);
  TEST_ASSERT_FALSE(cfg.lightsForceOff);
}

void test_light_toggle_uses_state_when_value_empty() {
  SensorData sensors = {};
  SystemState state = {};
  state.lightsOn = true;
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  PlantConfig* activePlant = plants_getById("basil");

  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("LIGHT", "", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_FALSE(cfg.lightsForceOn);
  TEST_ASSERT_TRUE(cfg.lightsForceOff);
}

void test_plant_switch_updates_active_plant_and_id() {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);

  PlantConfig* activePlant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(activePlant);

  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("PLANT", "mint", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_NOT_NULL(activePlant);
  TEST_ASSERT_EQUAL_STRING("mint", activePlant->id);
  TEST_ASSERT_EQUAL_STRING("mint", cfg.currentPlantId);
}

void test_plant_unknown_keeps_current_plant() {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);

  PlantConfig* activePlant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(activePlant);

  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("PLANT", "nonexistent", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_EQUAL_STRING("basil", activePlant->id);
  TEST_ASSERT_EQUAL_STRING("basil", cfg.currentPlantId);
}

void test_grow_start_activates_cycle() {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  PlantConfig* activePlant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(activePlant);

  g_stub_millis = 12345;
  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("GROW_START", "0", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_TRUE(cfg.growActive);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.growStartMethod);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.growPhase);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.growElapsedDays);
  TEST_ASSERT_EQUAL_UINT32(12345, state.growDayStartMs);
  TEST_ASSERT_FALSE(state.growPhasePendingAdvance);
}

void test_grow_start_seed_method_picks_seedling_phase() {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  PlantConfig* activePlant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(activePlant);

  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("GROW_START", "1", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_TRUE(cfg.growActive);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.growStartMethod);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.growPhase);
}

void test_grow_next_advances_phase_and_resets_days() {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  PlantConfig* activePlant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(activePlant);

  cfg.growActive = true;
  cfg.growPhase = 0;
  cfg.growElapsedDays = 7;
  state.growPhasePendingAdvance = true;
  device_setState(DEVICE_GROWING);

  g_stub_millis = 999;
  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("GROW_NEXT", "", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_EQUAL_UINT8(1, cfg.growPhase);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.growElapsedDays);
  TEST_ASSERT_FALSE(state.growPhasePendingAdvance);
  TEST_ASSERT_EQUAL_UINT32(999, state.growDayStartMs);
}

void test_grow_stop_disables_cycle() {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  PlantConfig* activePlant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(activePlant);

  cfg.growActive = true;
  state.growPhasePendingAdvance = true;
  device_setState(DEVICE_GROWING);
  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("GROW_STOP", "", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_FALSE(cfg.growActive);
  TEST_ASSERT_FALSE(state.growPhasePendingAdvance);
}

void test_clear_fault_returns_to_idle() {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  PlantConfig* activePlant = plants_getById("basil");
  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };

  // Aja laite FAULT-tilaan blocking-faultilla (SENSOR on nyt advisory, ei lukitse)
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  TEST_ASSERT_EQUAL_INT(DEVICE_FAULT, g_device.state);
  TEST_ASSERT_TRUE(device_hasFault());

  command_handle("CLEAR_FAULT", "", &sensors, &state, &cfg, &activePlant, &ctx);

  TEST_ASSERT_FALSE(device_hasFault());
  TEST_ASSERT_EQUAL_INT(DEVICE_IDLE, g_device.state);
}

// ── FACTORY_RESET ────────────────────────────────────────────────────
//
// Palauttaa laitteen kayttoonottotilaan poistamalla ne tiedostot jotka
// syntyvat itsestaan uudelleen (config/plants/growclock -> defaults), mutta
// SAILYTTAA calibration.json:n jota kone ei voi tuottaa ilman ihmista ja
// mittalaitetta. Config.json:n poisto riittaa onboarding-tilaan, koska
// config_load putoaa oletuksiin joissa onboardingComplete=false.

// Aseta LittleFS:aan ne tiedostot jotka oikealla laitteella olisi.
static void seedPersistedFiles() {
  LittleFS.format();
  LittleFS.open(PATH_DEVICE_CONFIG, "w").close();
  LittleFS.open(PATH_GROW_CLOCK, "w").close();
  LittleFS.open(PATH_CALIBRATION, "w").close();
  LittleFS.open(PATH_PLANTS_DB, "w").close();
}

static void runFactoryReset(const char* confirmValue) {
  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  PlantConfig* activePlant = plants_getById("basil");
  RouterContext ctx = { &sensors, &state, &cfg, &activePlant };
  command_handle("FACTORY_RESET", confirmValue, &sensors, &state, &cfg,
                 &activePlant, &ctx);
}

// Ydinvaite. Jakoperuste: syntyyko tiedosto itsestaan uudelleen?
//   config/plants/growclock -> kylla (defaults) -> poistetaan
//   calibration             -> EI (vaatii ihmisen mittalaitteineen) -> jaa
//
// calibration.json:n sailyminen on taman komennon tarkein yksittainen
// ominaisuus: sen pyyhkiminen ei palauta laitetta tehdastilaan vaan tekee
// siita toimintakelvottoman, ja moottorin rajojen menetys on vaarallista
// koska homingia ei ole.
void test_factory_reset_clears_regenerable_files_but_keeps_calibration() {
  seedPersistedFiles();
  reboot_request_clearForTest();

  runFactoryReset("CONFIRM");

  TEST_ASSERT_FALSE(LittleFS.littlefs_has(PATH_DEVICE_CONFIG));
  TEST_ASSERT_FALSE(LittleFS.littlefs_has(PATH_GROW_CLOCK));
  // plants.json regeneroituu plants_loadDefaults():sta, joten se nollataan —
  // muuten edellisen omistajan profiilimuokkaukset (ja vanhentuneet
  // ohjetekstit) jaisivat elamaan "tehdasresetin" yli.
  TEST_ASSERT_FALSE(LittleFS.littlefs_has(PATH_PLANTS_DB));
  TEST_ASSERT_TRUE(LittleFS.littlefs_has(PATH_CALIBRATION));
}

// Reboot on osa sopimusta: puolittain nollattu RAM olisi arvaamaton.
void test_factory_reset_schedules_reboot() {
  seedPersistedFiles();
  reboot_request_clearForTest();

  runFactoryReset("CONFIRM");

  TEST_ASSERT_TRUE(reboot_request_pending());
}

// Ilman vahvistusta ei saa tapahtua mitaan — lipsahtanut komento ei saa
// pyyhkia kayttajan asetuksia.
void test_factory_reset_without_confirm_does_nothing() {
  seedPersistedFiles();
  reboot_request_clearForTest();

  runFactoryReset("");

  TEST_ASSERT_TRUE(LittleFS.littlefs_has(PATH_DEVICE_CONFIG));
  TEST_ASSERT_TRUE(LittleFS.littlefs_has(PATH_GROW_CLOCK));
  TEST_ASSERT_FALSE(reboot_request_pending());
}

void test_factory_reset_with_wrong_confirm_does_nothing() {
  seedPersistedFiles();
  reboot_request_clearForTest();

  runFactoryReset("yes");   // ei tasmalleen "CONFIRM"

  TEST_ASSERT_TRUE(LittleFS.littlefs_has(PATH_DEVICE_CONFIG));
  TEST_ASSERT_FALSE(reboot_request_pending());
}

// Tuore laite: tiedostoja ei ole. Resetin pitaa onnistua silti (ei virhetta,
// reboot silti) — muuten "nollaa varmuuden vuoksi" jaisi puolitiehen.
void test_factory_reset_on_already_clean_device_still_reboots() {
  LittleFS.format();
  reboot_request_clearForTest();

  runFactoryReset("CONFIRM");

  TEST_ASSERT_FALSE(LittleFS.littlefs_has(PATH_DEVICE_CONFIG));
  TEST_ASSERT_TRUE(reboot_request_pending());
}

// Oletukset joihin config_load putoaa poiston jalkeen = kayttoonottotila.
// Jos tama muuttuu, reset lakkaa palauttamasta laitetta onboardingiin ilman
// etta mikaan muu testi huomaa.
void test_defaults_after_reset_are_onboarding_state() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);

  TEST_ASSERT_FALSE(cfg.onboardingComplete);
  TEST_ASSERT_FALSE(cfg.growActive);
  TEST_ASSERT_EQUAL_STRING("", cfg.adminPin);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_unknown_command_does_not_change_flags);
  RUN_TEST(test_light_on_sets_force_flags);
  RUN_TEST(test_light_toggle_uses_state_when_value_empty);
  RUN_TEST(test_plant_switch_updates_active_plant_and_id);
  RUN_TEST(test_plant_unknown_keeps_current_plant);
  RUN_TEST(test_grow_start_activates_cycle);
  RUN_TEST(test_grow_start_seed_method_picks_seedling_phase);
  RUN_TEST(test_grow_next_advances_phase_and_resets_days);
  RUN_TEST(test_grow_stop_disables_cycle);
  RUN_TEST(test_clear_fault_returns_to_idle);

  RUN_TEST(test_factory_reset_clears_regenerable_files_but_keeps_calibration);
  RUN_TEST(test_factory_reset_schedules_reboot);
  RUN_TEST(test_factory_reset_without_confirm_does_nothing);
  RUN_TEST(test_factory_reset_with_wrong_confirm_does_nothing);
  RUN_TEST(test_factory_reset_on_already_clean_device_still_reboots);
  RUN_TEST(test_defaults_after_reset_are_onboarding_state);
  return UNITY_END();
}
