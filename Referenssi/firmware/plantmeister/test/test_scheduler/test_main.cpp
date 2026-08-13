#include <unity.h>

// Shared feature flags for native tests (test/stubs/test_feature_flags.h).
// EBB_FLOW disabled to avoid pump-dependency in scheduler_updateEbbFlow.
#include "test_feature_flags.h"

// ── Hardware function stubs — must come before scheduler.h ──────
// scheduler.h defines inline functions that call these; the compiler
// needs declarations in scope when it parses the header.
static int  g_lightSetCalls = 0;
static bool g_lightSetLast  = false;
inline void power_setLight(bool on) { g_lightSetCalls++; g_lightSetLast = on; }
inline bool pump_dose(int)       { return true; }
inline bool pump_startForMs(unsigned long) { return true; }
inline void pump_stop() {}
inline bool pump_isRunning()     { return false; }
inline void motor_moveTo(int)    {}

// ── Project includes ─────────────────────────────────────────────
#include "structs.h"
#include "scheduler.h"

// ── Helpers ──────────────────────────────────────────────────────
static const unsigned long MS_PER_HOUR = 3600000UL;
static const unsigned long MS_PER_DAY  = 86400000UL;

// Build a minimal PlantConfig with phaseCount=0 (flat-params mode)
static PlantConfig make_plant(int lightHours = 16,
                               int waterIntervalHours = 24,
                               int waterMl = 100,
                               int tdsPpm = 800,
                               float tempMin = 15.0f,
                               float tempMax = 30.0f,
                               int maxHeightMm = 200) {
  PlantConfig p = {};
  p.lightHours          = lightHours;
  p.waterIntervalHours  = waterIntervalHours;
  p.waterMlPerDose      = waterMl;
  // tdsTargetPpm/tempMinC/tempMaxC poistettu 18.7.2026 (pe-ohjausmalli.md).
  // Parametrit sailyvat positional-yhteensopivuuden vuoksi, arvot jatetaan
  // huomiotta — PE-tavoitteet elavat per vaihe, ei litteassa PlantConfigissa.
  (void)tdsPpm; (void)tempMin; (void)tempMax;
  p.maxHeightMm         = maxHeightMm;
  p.phaseCount          = 0;
  return p;
}

static DeviceConfig make_cfg(int lightOnHour = 0, bool growActive = false) {
  DeviceConfig cfg = {};
  cfg.lightOnHour = lightOnHour;
  cfg.growActive  = growActive;
  return cfg;
}

static SystemState make_state() {
  SystemState s = {};
  s.lightCycleStartMs = 0;
  return s;
}

static SensorData make_sensor(float airTemp = 22.0f, bool envValid = true,
                               bool waterLevelOk = true, bool heightValid = true,
                               int plantHeightMm = 100, int tdsPpm = 700,
                               bool tdsValid = true) {
  SensorData d = {};
  d.airTempC      = airTemp;
  d.envValid      = envValid;
  d.waterLevelOk  = waterLevelOk;
  d.heightValid   = heightValid;
  d.plantHeightMm = plantHeightMm;
  d.tdsPpm        = tdsPpm;
  d.tdsValid      = tdsValid;
  return d;
}

void setUp() {
  g_stub_millis   = 0;
  g_lightSetCalls = 0;
  g_lightSetLast  = false;
}
void tearDown() {}

// ════════════════════════════════════════════════════════════════════
// scheduler_shouldLightBeOn
// ════════════════════════════════════════════════════════════════════

void test_light_on_within_cycle() {
  PlantConfig plant = make_plant(/*lightHours=*/16);
  DeviceConfig cfg  = make_cfg();
  SystemState  state = make_state();

  g_stub_millis = 1 * MS_PER_HOUR;  // 1h into cycle — well within 16h window
  TEST_ASSERT_TRUE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

void test_light_off_after_cycle() {
  PlantConfig plant = make_plant(/*lightHours=*/16);
  DeviceConfig cfg  = make_cfg();
  SystemState  state = make_state();

  g_stub_millis = 17 * MS_PER_HOUR;  // 17h — past the 16h ON window
  TEST_ASSERT_FALSE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

void test_light_off_zero_hours() {
  PlantConfig plant = make_plant(/*lightHours=*/0);
  DeviceConfig cfg  = make_cfg();
  SystemState  state = make_state();

  g_stub_millis = 1 * MS_PER_HOUR;
  TEST_ASSERT_FALSE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

void test_light_force_on_overrides() {
  PlantConfig plant = make_plant(/*lightHours=*/0);  // would be OFF by schedule
  DeviceConfig cfg  = make_cfg();
  cfg.lightsForceOn = true;
  SystemState state = make_state();

  g_stub_millis = 20 * MS_PER_HOUR;
  TEST_ASSERT_TRUE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

void test_light_force_off_overrides() {
  PlantConfig plant = make_plant(/*lightHours=*/16);  // would be ON by schedule
  DeviceConfig cfg  = make_cfg();
  cfg.lightsForceOff = true;
  SystemState state  = make_state();

  g_stub_millis = 1 * MS_PER_HOUR;
  TEST_ASSERT_FALSE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

// Basilika-taimivaihe idatysgatea varten: oikea "basil" id + SEEDLING-vaihe,
// jotta growstep_sequenceActive loytaa oikean GROW_STEP_LISTS-listan.
static PlantConfig make_seedling_plant() {
  PlantConfig p = make_plant(/*lightHours=*/16);
  strncpy(p.id, "basil", sizeof(p.id) - 1);
  p.phaseCount           = 1;
  p.phases[0]            = {};
  p.phases[0].type       = GROW_PHASE_SEEDLING;
  p.phases[0].lightHours = 16;
  return p;
}

// Etiolaation esto (Fable §3.1): idatysaskelsarjan aikana valo pysyy OFF vaikka
// valojakso olisi paalla.
void test_light_off_during_seedling_steps() {
  PlantConfig  plant = make_seedling_plant();
  DeviceConfig cfg   = make_cfg();
  cfg.growActive      = true;
  cfg.growStartMethod = 1;   // siemen
  cfg.growPhase       = 0;   // SEEDLING
  cfg.growStepIndex   = 0;   // sarja kesken
  SystemState state = make_state();

  g_stub_millis = 1 * MS_PER_HOUR;  // olisi ON valojakson mukaan
  TEST_ASSERT_FALSE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

// Kun kayttaja kuittaa sirkkalehdet (index == count), sarja paattyy ja valo
// seuraa taas valojaksoa.
void test_light_on_after_seedling_sequence_done() {
  PlantConfig  plant = make_seedling_plant();
  DeviceConfig cfg   = make_cfg();
  cfg.growActive      = true;
  cfg.growStartMethod = 1;
  cfg.growPhase       = 0;
  cfg.growStepIndex   = 4;   // 4 askelta kuitattu -> sarja ohi
  SystemState state = make_state();

  g_stub_millis = 1 * MS_PER_HOUR;
  TEST_ASSERT_TRUE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

// Gate koskee VAIN SEEDLING-idatysta: kasvuvaiheen askelsarja ei pakota OFF.
void test_light_not_forced_off_during_vegetative_steps() {
  PlantConfig  plant   = make_seedling_plant();
  plant.phases[0].type = GROW_PHASE_VEGETATIVE;   // sama slotti, eri tyyppi
  DeviceConfig cfg     = make_cfg();
  cfg.growActive      = true;
  cfg.growStartMethod = 1;
  cfg.growPhase       = 0;
  cfg.growStepIndex   = 0;   // basil VEGETATIVE -sarja aktiivinen
  SystemState state = make_state();

  g_stub_millis = 1 * MS_PER_HOUR;
  TEST_ASSERT_TRUE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

void test_light_grow_phase_overrides_plant_hours() {
  // Flat plant: 8h light. Active phase: 18h light.
  // At 10h elapsed, flat config says OFF, phase says ON.
  PlantConfig plant = make_plant(/*lightHours=*/8);
  plant.phaseCount = 1;
  plant.phases[0] = {};
  plant.phases[0].lightHours = 18;
  plant.phases[0].durationDays = 14;

  DeviceConfig cfg = make_cfg(/*lightOnHour=*/0, /*growActive=*/true);
  cfg.growPhase = 0;
  SystemState state = make_state();

  g_stub_millis = 10 * MS_PER_HOUR;  // 10h: 8h flat OFF, 18h phase ON
  TEST_ASSERT_TRUE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

void test_light_cycle_wraparound_elapsed_is_stable() {
  PlantConfig plant = make_plant(/*lightHours=*/16);
  DeviceConfig cfg  = make_cfg();
  SystemState state = make_state();

  // Simulate 32-bit millis wrap using a wrapped low 32-bit window.
  const unsigned long wrapStart = (unsigned long)0x1FFFFF000ULL;
  state.lightCycleStartMs = wrapStart;
  g_stub_millis = wrapStart + 0x2000UL;  // 8192 ms after cycle start

  TEST_ASSERT_TRUE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

// ════════════════════════════════════════════════════════════════════
// scheduler_applyLightOverride
// Regression: light_toggle in IDLE set lightsForceOn/Off but nothing
// consumed the flags — scheduler_update() only runs in DEVICE_GROWING.
// ════════════════════════════════════════════════════════════════════

void test_light_override_force_on_drives_light_in_idle() {
  DeviceConfig cfg  = make_cfg();
  cfg.lightsForceOn = true;
  SystemState state = make_state();
  state.lightsOn    = false;

  TEST_ASSERT_TRUE(scheduler_applyLightOverride(&state, &cfg));
  TEST_ASSERT_TRUE(state.lightsOn);
  TEST_ASSERT_EQUAL_INT(1, g_lightSetCalls);
  TEST_ASSERT_TRUE(g_lightSetLast);
}

void test_light_override_force_off_drives_light_off() {
  DeviceConfig cfg   = make_cfg();
  cfg.lightsForceOff = true;
  SystemState state  = make_state();
  state.lightsOn     = true;

  TEST_ASSERT_TRUE(scheduler_applyLightOverride(&state, &cfg));
  TEST_ASSERT_FALSE(state.lightsOn);
  TEST_ASSERT_EQUAL_INT(1, g_lightSetCalls);
  TEST_ASSERT_FALSE(g_lightSetLast);
}

void test_light_override_no_flags_is_noop() {
  // Without an explicit override the schedule (GROWING) owns the light —
  // IDLE must not touch it.
  DeviceConfig cfg  = make_cfg();
  SystemState state = make_state();
  state.lightsOn    = true;

  TEST_ASSERT_FALSE(scheduler_applyLightOverride(&state, &cfg));
  TEST_ASSERT_TRUE(state.lightsOn);
  TEST_ASSERT_EQUAL_INT(0, g_lightSetCalls);
}

void test_light_override_is_idempotent() {
  // Runs every loop iteration — must not re-drive the pin once applied
  // (also keeps it harmless alongside scheduler_update() in GROWING).
  DeviceConfig cfg  = make_cfg();
  cfg.lightsForceOn = true;
  SystemState state = make_state();

  TEST_ASSERT_TRUE(scheduler_applyLightOverride(&state, &cfg));
  TEST_ASSERT_FALSE(scheduler_applyLightOverride(&state, &cfg));
  TEST_ASSERT_EQUAL_INT(1, g_lightSetCalls);
}

// ════════════════════════════════════════════════════════════════════
// scheduler_shouldWater
// ════════════════════════════════════════════════════════════════════

void test_water_pump_already_running() {
  PlantConfig plant = make_plant();
  SystemState state = make_state();
  state.pumpRunning = true;
  SensorData data   = make_sensor();

  TEST_ASSERT_FALSE(scheduler_shouldWater(&plant, &state, &data));
}

void test_water_level_low() {
  PlantConfig plant = make_plant();
  SystemState state = make_state();
  SensorData data   = make_sensor();
  data.waterLevelOk = false;

  // Interval has elapsed
  state.lastWaterDose = 0;
  g_stub_millis = 25 * MS_PER_HOUR;
  TEST_ASSERT_FALSE(scheduler_shouldWater(&plant, &state, &data));
}

void test_water_interval_not_elapsed() {
  PlantConfig plant = make_plant(/*lightHours=*/16, /*waterIntervalHours=*/24);
  SystemState state = make_state();
  state.lastWaterDose = 0;
  SensorData data = make_sensor();

  g_stub_millis = 12 * MS_PER_HOUR;  // Only 12h since last dose, need 24h
  TEST_ASSERT_FALSE(scheduler_shouldWater(&plant, &state, &data));
}

void test_water_should_water() {
  PlantConfig plant = make_plant(/*lightHours=*/16, /*waterIntervalHours=*/24,
                                  /*waterMl=*/100, /*tdsPpm=*/800);
  SystemState state = make_state();
  state.lastWaterDose = 0;
  state.pumpRunning = false;

  SensorData data = make_sensor();
  data.waterLevelOk = true;
  data.tdsPpm       = 600;   // Below target (800) → water needed
  data.tdsValid     = true;

  g_stub_millis = 25 * MS_PER_HOUR;  // 25h since last dose
  TEST_ASSERT_TRUE(scheduler_shouldWater(&plant, &state, &data));
}

// test_water_tds_ok_skips_dose poistettu 18.7.2026: TDS-pohjainen
// annostelun ohitus (tdsPpm > target → skip) poistettiin schedulerista
// kun tdsTargetPpm-kentta poistui (TDS-anturi pois, kuollut logiikka).
// Kastelu maaraytyy nyt vain intervallista. pe-ohjausmalli.md.

void test_water_interval_wraparound_elapsed_is_stable() {
  PlantConfig plant = make_plant(/*lightHours=*/16, /*waterIntervalHours=*/24,
                                 /*waterMl=*/100, /*tdsPpm=*/800);
  SystemState state = make_state();
  state.pumpRunning = false;

  SensorData data = make_sensor();
  data.waterLevelOk = true;
  data.tdsPpm = 600;
  data.tdsValid = true;

  const unsigned long wrapDose = (unsigned long)0x1FFFF0000ULL;
  state.lastWaterDose = wrapDose;
  g_stub_millis = wrapDose + (25UL * MS_PER_HOUR);

  TEST_ASSERT_TRUE(scheduler_shouldWater(&plant, &state, &data));
}

// scheduler_checkTemp-testit poistettu 18.7.2026: funktio poistettiin
// (luki plant->tempMinC/tempMaxC, tulosti vain loki-varoituksen, ei
// aktuoinut). Kasvin lampoa valvotaan nyt lehtilammon kautta per vaihe
// (leafTempMaxC) — valmennus tulee PE-mallin V2:ssa. pe-ohjausmalli.md.

// ════════════════════════════════════════════════════════════════════
// scheduler_calcNewHeight
// ════════════════════════════════════════════════════════════════════

void test_height_calc_normal() {
  TEST_ASSERT_EQUAL_INT(110, scheduler_calcNewHeight(100, 200));
}

void test_height_calc_near_max() {
  // 195 + 10 = 205 > 200 → clamped to 200
  TEST_ASSERT_EQUAL_INT(200, scheduler_calcNewHeight(195, 200));
}

void test_height_calc_at_max() {
  // Already at max — stay
  TEST_ASSERT_EQUAL_INT(200, scheduler_calcNewHeight(200, 200));
}

// ════════════════════════════════════════════════════════════════════
// scheduler_shouldRaiseHeight
// ════════════════════════════════════════════════════════════════════

void test_raise_height_sensor_invalid() {
  PlantConfig plant = make_plant();
  SystemState state = make_state();
  SensorData  data  = make_sensor();
  data.heightValid = false;

  TEST_ASSERT_FALSE(scheduler_shouldRaiseHeight(&plant, &state, &data, 100));
}

void test_raise_height_interval_not_elapsed() {
  PlantConfig plant = make_plant();
  SystemState state = make_state();
  // lastHeightCheck just happened
  state.lastHeightCheck = 0;
  g_stub_millis = HEIGHT_CHECK_INTERVAL_MS / 2;

  SensorData data = make_sensor();
  data.heightValid   = true;
  data.plantHeightMm = MOTOR_HEIGHT_MARGIN_MM - 5;  // plant close to lamp

  TEST_ASSERT_FALSE(scheduler_shouldRaiseHeight(&plant, &state, &data, 100));
}

void test_raise_height_plant_far_from_lamp() {
  PlantConfig plant = make_plant();
  SystemState state = make_state();
  state.lastHeightCheck = 0;
  g_stub_millis = HEIGHT_CHECK_INTERVAL_MS + 1;

  SensorData data = make_sensor();
  data.heightValid   = true;
  data.plantHeightMm = MOTOR_HEIGHT_MARGIN_MM + 50;  // plant well below lamp

  TEST_ASSERT_FALSE(scheduler_shouldRaiseHeight(&plant, &state, &data, 100));
}

void test_raise_height_should_raise() {
  PlantConfig plant = make_plant(/*lightHours=*/16, 24, 100, 800, 15, 30, /*maxHeightMm=*/200);
  SystemState state = make_state();
  state.lastHeightCheck = 0;
  g_stub_millis = HEIGHT_CHECK_INTERVAL_MS + 1;

  SensorData data = make_sensor();
  data.heightValid   = true;
  data.plantHeightMm = MOTOR_HEIGHT_MARGIN_MM - 5;  // within margin — raise
  int currentHeightMm = 100;

  TEST_ASSERT_TRUE(scheduler_shouldRaiseHeight(&plant, &state, &data, currentHeightMm));
}

void test_raise_height_wraparound_interval_is_stable() {
  PlantConfig plant = make_plant(/*lightHours=*/16, 24, 100, 800, 15, 30, /*maxHeightMm=*/200);
  SystemState state = make_state();

  const unsigned long wrapCheck = (unsigned long)0x1FFFFFF00ULL;
  state.lastHeightCheck = wrapCheck;
  g_stub_millis = wrapCheck + HEIGHT_CHECK_INTERVAL_MS + 1UL;

  SensorData data = make_sensor();
  data.heightValid = true;
  data.plantHeightMm = MOTOR_HEIGHT_MARGIN_MM - 2;

  TEST_ASSERT_TRUE(scheduler_shouldRaiseHeight(&plant, &state, &data, 100));
}

// ════════════════════════════════════════════════════════════════════
// scheduler_getActivePhase
// ════════════════════════════════════════════════════════════════════

void test_get_phase_grow_inactive() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 2;

  DeviceConfig cfg = make_cfg(0, /*growActive=*/false);
  cfg.growPhase = 0;

  TEST_ASSERT_NULL(scheduler_getActivePhase(&plant, &cfg));
}

void test_get_phase_no_phases() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 0;  // flat-params mode

  DeviceConfig cfg = make_cfg(0, /*growActive=*/true);
  TEST_ASSERT_NULL(scheduler_getActivePhase(&plant, &cfg));
}

void test_get_phase_active_returns_correct_phase() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 2;
  plant.phases[0]   = {};
  plant.phases[0].lightHours = 12;
  plant.phases[1]   = {};
  plant.phases[1].lightHours = 18;

  DeviceConfig cfg = make_cfg(0, /*growActive=*/true);
  cfg.growPhase = 1;

  const GrowPhaseParams* p = scheduler_getActivePhase(&plant, &cfg);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_INT(18, p->lightHours);
}

// ════════════════════════════════════════════════════════════════════
// scheduler_updateGrowPhase
// ════════════════════════════════════════════════════════════════════

void test_grow_phase_grow_inactive() {
  PlantConfig  plant = make_plant();
  plant.phaseCount   = 1;
  plant.phases[0]    = {};
  plant.phases[0].durationDays = 7;

  SystemState  state = make_state();
  DeviceConfig cfg   = make_cfg(0, /*growActive=*/false);

  bool changed = scheduler_updateGrowPhase(&plant, &state, &cfg);
  TEST_ASSERT_FALSE(changed);
  TEST_ASSERT_EQUAL_INT(0, cfg.growElapsedDays);
}

void test_grow_phase_day_tick() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 1;
  plant.phases[0]   = {};
  plant.phases[0].durationDays = 14;  // Long phase, no advance yet

  SystemState state = make_state();
  state.growDayStartMs = 0;

  DeviceConfig cfg = make_cfg(0, /*growActive=*/true);
  cfg.growPhase       = 0;
  cfg.growElapsedDays = 0;

  // Advance 24h — should tick the day counter
  g_stub_millis = MS_PER_DAY + 1;
  bool changed = scheduler_updateGrowPhase(&plant, &state, &cfg);

  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_EQUAL_INT(1, cfg.growElapsedDays);
}

void test_grow_phase_no_day_tick_before_24h() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 1;
  plant.phases[0]   = {};
  plant.phases[0].durationDays = 14;

  SystemState state = make_state();
  state.growDayStartMs = 0;

  DeviceConfig cfg = make_cfg(0, /*growActive=*/true);
  cfg.growPhase       = 0;
  cfg.growElapsedDays = 0;

  g_stub_millis = MS_PER_DAY - 1;
  bool changed = scheduler_updateGrowPhase(&plant, &state, &cfg);

  TEST_ASSERT_FALSE(changed);
  TEST_ASSERT_EQUAL_INT(0, cfg.growElapsedDays);
}

void test_grow_phase_advance_proposed_when_duration_elapsed() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 2;
  plant.phases[0]   = {};
  plant.phases[0].durationDays = 7;
  plant.phases[1]   = {};
  plant.phases[1].durationDays = 7;

  SystemState state = make_state();
  state.growDayStartMs = 0;
  state.growPhasePendingAdvance = false;

  DeviceConfig cfg = make_cfg(0, /*growActive=*/true);
  cfg.growPhase       = 0;
  cfg.growElapsedDays = 7;  // Exactly at duration

  g_stub_millis = 1;
  scheduler_updateGrowPhase(&plant, &state, &cfg);

  TEST_ASSERT_TRUE(state.growPhasePendingAdvance);
  // Phase itself has not changed yet
  TEST_ASSERT_EQUAL_INT(0, cfg.growPhase);
}

void test_grow_phase_indefinite_duration_no_pending_advance() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 2;
  plant.phases[0]   = {};
  plant.phases[0].durationDays = 0;  // Indefinite
  plant.phases[1]   = {};
  plant.phases[1].durationDays = 7;

  SystemState state = make_state();
  state.growDayStartMs = 0;
  state.growPhasePendingAdvance = false;

  DeviceConfig cfg = make_cfg(0, /*growActive=*/true);
  cfg.growPhase       = 0;
  cfg.growElapsedDays = 99;

  g_stub_millis = 10 * MS_PER_DAY;
  bool changed = scheduler_updateGrowPhase(&plant, &state, &cfg);

  TEST_ASSERT_TRUE(changed);  // day tick still updates elapsed days
  TEST_ASSERT_FALSE(state.growPhasePendingAdvance);
  TEST_ASSERT_EQUAL_INT(0, cfg.growPhase);
}

void test_grow_phase_pending_does_not_auto_advance_before_3_days() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 2;
  plant.phases[0]   = {};
  plant.phases[0].durationDays = 7;
  plant.phases[1]   = {};
  plant.phases[1].durationDays = 7;

  SystemState state = make_state();
  state.growDayStartMs          = 2 * MS_PER_DAY;
  state.growPhasePendingAdvance = true;
  state.growAdvanceProposedMs   = 0;

  DeviceConfig cfg = make_cfg(0, /*growActive=*/true);
  cfg.growPhase       = 0;
  cfg.growElapsedDays = 7;

  g_stub_millis = 2 * MS_PER_DAY;
  bool changed = scheduler_updateGrowPhase(&plant, &state, &cfg);

  TEST_ASSERT_FALSE(changed);
  TEST_ASSERT_TRUE(state.growPhasePendingAdvance);
  TEST_ASSERT_EQUAL_INT(0, cfg.growPhase);
}

void test_grow_phase_auto_advances_after_3_days() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 2;
  plant.phases[0]   = {};
  plant.phases[0].durationDays = 7;
  plant.phases[1]   = {};
  plant.phases[1].durationDays = 7;

  SystemState state = make_state();
  state.growDayStartMs          = 0;
  state.growPhasePendingAdvance = true;
  state.growAdvanceProposedMs   = 0;  // Proposed at t=0

  DeviceConfig cfg = make_cfg(0, /*growActive=*/true);
  cfg.growPhase       = 0;
  cfg.growElapsedDays = 7;

  // 3 days later — user never responded, auto-advance
  g_stub_millis = 3 * MS_PER_DAY + 1;
  bool changed = scheduler_updateGrowPhase(&plant, &state, &cfg);

  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_EQUAL_INT(1, cfg.growPhase);
  TEST_ASSERT_EQUAL_INT(0, cfg.growElapsedDays);
  TEST_ASSERT_FALSE(state.growPhasePendingAdvance);
}

void test_grow_phase_last_phase_no_auto_advance() {
  PlantConfig plant = make_plant();
  plant.phaseCount  = 1;  // Only one phase
  plant.phases[0]   = {};
  plant.phases[0].durationDays = 7;

  SystemState state = make_state();
  state.growDayStartMs          = 0;
  state.growPhasePendingAdvance = true;
  state.growAdvanceProposedMs   = 0;

  DeviceConfig cfg = make_cfg(0, /*growActive=*/true);
  cfg.growPhase       = 0;
  cfg.growElapsedDays = 7;

  // 3+ days later — but there's no next phase
  g_stub_millis = 3 * MS_PER_DAY + 1;
  scheduler_updateGrowPhase(&plant, &state, &cfg);

  TEST_ASSERT_EQUAL_INT(0, cfg.growPhase);   // Stays at phase 0
  TEST_ASSERT_FALSE(state.growPhasePendingAdvance);
}

// ── Ebb&Flow tick gate (scheduler_shouldTickEbbFlow) ────────────────
// Regression: manual force-flood silently did nothing in a flood-disabled
// phase (rooting, floodIntervalMin=0) because the whole ebb update was gated
// out. A manual override must always tick the FSM; periodic must not.

void test_ebb_tick_calibration_blocks_everything() {
  // calibIdle == false → calibration owns the pump, never tick (even forced)
  TEST_ASSERT_FALSE(scheduler_shouldTickEbbFlow(false, true,  true,  true));
  TEST_ASSERT_FALSE(scheduler_shouldTickEbbFlow(false, true,  false, false));
}

void test_ebb_tick_periodic_when_flood_enabled() {
  // Normal phase (floodEnabled), no force, idle → periodic tick runs
  TEST_ASSERT_TRUE(scheduler_shouldTickEbbFlow(true, true, false, false));
}

void test_ebb_tick_no_periodic_in_flood_disabled_phase() {
  // Rooting phase (floodEnabled=false), no force, idle → no periodic flood
  TEST_ASSERT_FALSE(scheduler_shouldTickEbbFlow(true, false, false, false));
}

void test_ebb_tick_force_flood_overrides_disabled_phase() {
  // THE FIX: force-flood requested in a flood-disabled phase → tick anyway
  TEST_ASSERT_TRUE(scheduler_shouldTickEbbFlow(true, false, true, false));
}

void test_ebb_tick_in_progress_cycle_finishes_in_disabled_phase() {
  // Cycle already running (midCycle) in a flood-disabled phase → keep ticking
  TEST_ASSERT_TRUE(scheduler_shouldTickEbbFlow(true, false, false, true));
}

// ── floodEnabled computation: phase default vs explicit portal override ──

void test_flood_enabled_phase_governs_without_override() {
  GrowPhaseParams rooting = {}; rooting.floodIntervalMin = 0;    // no-flood phase
  GrowPhaseParams veg     = {}; veg.floodIntervalMin     = 240;  // flooding phase
  TEST_ASSERT_FALSE(scheduler_floodEnabled(&rooting, 0));  // rooting disables
  TEST_ASSERT_TRUE(scheduler_floodEnabled(&veg, 0));       // veg enables
  TEST_ASSERT_TRUE(scheduler_floodEnabled(nullptr, 0));    // no phase → enabled
}

void test_flood_enabled_explicit_override_enables_disabled_phase() {
  // THE FIX: operator set 15 min in the portal while in a no-flood phase →
  // flooding is enabled (explicit override wins over the phase default).
  GrowPhaseParams rooting = {}; rooting.floodIntervalMin = 0;
  TEST_ASSERT_TRUE(scheduler_floodEnabled(&rooting, 15));
}

// ── Continuous-flow soak pump command (scheduler_soakPumpCommand) ───
// SOAK drives the pump at the configured soak duty: RUN on entry (duty>0),
// STOP on exit, NONE while staying / for non-soak transitions / duty 0.

void test_soak_cmd_run_on_entry_with_duty() {
  // FLOOD → SOAK with a non-zero soak duty → start continuous pump
  TEST_ASSERT_EQUAL_INT(SOAK_CMD_RUN,
    scheduler_soakPumpCommand(EBB_STATE_FLOOD, EBB_STATE_SOAK, 30));
}

void test_soak_cmd_none_on_entry_when_duty_zero() {
  // Legacy soak (duty 0): pump stays off on entry
  TEST_ASSERT_EQUAL_INT(SOAK_CMD_NONE,
    scheduler_soakPumpCommand(EBB_STATE_FLOOD, EBB_STATE_SOAK, 0));
}

void test_soak_cmd_none_while_staying_in_soak() {
  // No restart mid-soak (so a 5 min cap trip is NOT auto-restarted)
  TEST_ASSERT_EQUAL_INT(SOAK_CMD_NONE,
    scheduler_soakPumpCommand(EBB_STATE_SOAK, EBB_STATE_SOAK, 30));
}

void test_soak_cmd_stop_on_exit_to_drain() {
  TEST_ASSERT_EQUAL_INT(SOAK_CMD_STOP,
    scheduler_soakPumpCommand(EBB_STATE_SOAK, EBB_STATE_DRAIN, 30));
}

void test_soak_cmd_stop_on_exit_is_unconditional() {
  // Leaving SOAK always stops, even if duty was 0 (harmless if already off)
  TEST_ASSERT_EQUAL_INT(SOAK_CMD_STOP,
    scheduler_soakPumpCommand(EBB_STATE_SOAK, EBB_STATE_DRAIN, 0));
}

void test_soak_cmd_none_for_non_soak_transition() {
  TEST_ASSERT_EQUAL_INT(SOAK_CMD_NONE,
    scheduler_soakPumpCommand(EBB_STATE_IDLE, EBB_STATE_FLOOD, 30));
}

// ════════════════════════════════════════════════════════════════════
// main
// ════════════════════════════════════════════════════════════════════

int main() {
  UNITY_BEGIN();

  // scheduler_shouldLightBeOn
  RUN_TEST(test_light_on_within_cycle);
  RUN_TEST(test_light_off_after_cycle);
  RUN_TEST(test_light_off_zero_hours);
  RUN_TEST(test_light_force_on_overrides);
  RUN_TEST(test_light_force_off_overrides);
  RUN_TEST(test_light_off_during_seedling_steps);
  RUN_TEST(test_light_on_after_seedling_sequence_done);
  RUN_TEST(test_light_not_forced_off_during_vegetative_steps);
  RUN_TEST(test_light_grow_phase_overrides_plant_hours);
  RUN_TEST(test_light_cycle_wraparound_elapsed_is_stable);

  // scheduler_applyLightOverride
  RUN_TEST(test_light_override_force_on_drives_light_in_idle);
  RUN_TEST(test_light_override_force_off_drives_light_off);
  RUN_TEST(test_light_override_no_flags_is_noop);
  RUN_TEST(test_light_override_is_idempotent);

  // scheduler_shouldWater
  RUN_TEST(test_water_pump_already_running);
  RUN_TEST(test_water_level_low);
  RUN_TEST(test_water_interval_not_elapsed);
  RUN_TEST(test_water_should_water);
  RUN_TEST(test_water_interval_wraparound_elapsed_is_stable);
  // TDS- ja checkTemp-testit poistettu 18.7.2026 (pe-ohjausmalli.md):
  // TDS-annostelu ja ilman lampovaroitus poistettiin schedulerista.

  // scheduler_calcNewHeight
  RUN_TEST(test_height_calc_normal);
  RUN_TEST(test_height_calc_near_max);
  RUN_TEST(test_height_calc_at_max);

  // scheduler_shouldRaiseHeight
  RUN_TEST(test_raise_height_sensor_invalid);
  RUN_TEST(test_raise_height_interval_not_elapsed);
  RUN_TEST(test_raise_height_plant_far_from_lamp);
  RUN_TEST(test_raise_height_should_raise);
  RUN_TEST(test_raise_height_wraparound_interval_is_stable);

  // scheduler_getActivePhase
  RUN_TEST(test_get_phase_grow_inactive);
  RUN_TEST(test_get_phase_no_phases);
  RUN_TEST(test_get_phase_active_returns_correct_phase);

  // scheduler_updateGrowPhase
  RUN_TEST(test_grow_phase_grow_inactive);
  RUN_TEST(test_grow_phase_day_tick);
  RUN_TEST(test_grow_phase_no_day_tick_before_24h);
  RUN_TEST(test_grow_phase_advance_proposed_when_duration_elapsed);
  RUN_TEST(test_grow_phase_indefinite_duration_no_pending_advance);
  RUN_TEST(test_grow_phase_pending_does_not_auto_advance_before_3_days);
  RUN_TEST(test_grow_phase_auto_advances_after_3_days);
  RUN_TEST(test_grow_phase_last_phase_no_auto_advance);

  RUN_TEST(test_ebb_tick_calibration_blocks_everything);
  RUN_TEST(test_ebb_tick_periodic_when_flood_enabled);
  RUN_TEST(test_ebb_tick_no_periodic_in_flood_disabled_phase);
  RUN_TEST(test_ebb_tick_force_flood_overrides_disabled_phase);
  RUN_TEST(test_ebb_tick_in_progress_cycle_finishes_in_disabled_phase);
  RUN_TEST(test_flood_enabled_phase_governs_without_override);
  RUN_TEST(test_flood_enabled_explicit_override_enables_disabled_phase);

  // scheduler_soakPumpCommand (continuous-flow soak)
  RUN_TEST(test_soak_cmd_run_on_entry_with_duty);
  RUN_TEST(test_soak_cmd_none_on_entry_when_duty_zero);
  RUN_TEST(test_soak_cmd_none_while_staying_in_soak);
  RUN_TEST(test_soak_cmd_stop_on_exit_to_drain);
  RUN_TEST(test_soak_cmd_stop_on_exit_is_unconditional);
  RUN_TEST(test_soak_cmd_none_for_non_soak_transition);

  return UNITY_END();
}
