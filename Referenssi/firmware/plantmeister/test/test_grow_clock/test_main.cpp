/*=====================================================================
  test_grow_clock — grow_clock_math.h + reboot continuity through the
  scheduler (docs/arkisto/kehitys/valo-ja-keskeytymattomyys-suunnitelma.md,
  vika 3: millis()-anchored grow timers reset on every power cycle).

  Covers the pure math (anchor back-dating, clamp/wrap, save gating)
  and the end-to-end property that matters: after a simulated reboot
  restore, the day tick and the photoperiod continue from where the
  previous power cycle left them.
=====================================================================*/

#include <unity.h>

#include "test_feature_flags.h"

// Hardware stubs required by scheduler.h (same set as test_scheduler)
inline void power_setLight(bool) {}
inline bool pump_dose(int)       { return true; }
inline bool pump_startForMs(unsigned long) { return true; }
inline void pump_stop() {}
inline bool pump_isRunning()     { return false; }
inline void motor_moveTo(int)    {}

#include "structs.h"
#include "grow_clock_math.h"
#include "scheduler.h"

static const unsigned long MS_PER_HOUR = 3600000UL;

void setUp()    { g_stub_millis = 0; }
void tearDown() {}

// ════════════════════════════════════════════════════════════════════
// growclock_restoredAnchor
// ════════════════════════════════════════════════════════════════════

void test_restored_anchor_reproduces_elapsed() {
  g_stub_millis = 5000;
  unsigned long anchor = growclock_restoredAnchor(g_stub_millis, 23 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL_UINT32(23 * MS_PER_HOUR,
                           (unsigned long)(g_stub_millis - anchor));
}

void test_restored_anchor_wraps_when_elapsed_exceeds_uptime() {
  // Boot: now (1 s) < persisted elapsed (23 h) → anchor underflows and wraps.
  // The scheduler's unsigned `now - anchor` must still see 23 h.
  g_stub_millis = 1000;
  unsigned long anchor = growclock_restoredAnchor(g_stub_millis, 23 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL_UINT32(23 * MS_PER_HOUR,
                           (unsigned long)(g_stub_millis - anchor));
}

// ════════════════════════════════════════════════════════════════════
// clamp / wrap
// ════════════════════════════════════════════════════════════════════

void test_clamp_day_elapsed_passes_normal_value() {
  TEST_ASSERT_EQUAL_UINT32(5 * MS_PER_HOUR,
                           growclock_clampDayElapsed(5 * MS_PER_HOUR));
}

void test_clamp_day_elapsed_caps_at_one_day() {
  // A stale/corrupt value must not fire several day ticks back-to-back.
  TEST_ASSERT_EQUAL_UINT32(GROWCLOCK_DAY_MS - 1UL,
                           growclock_clampDayElapsed(30 * MS_PER_HOUR));
}

void test_wrap_light_elapsed_is_cyclic() {
  TEST_ASSERT_EQUAL_UINT32(1 * MS_PER_HOUR,
                           growclock_wrapLightElapsed(25 * MS_PER_HOUR));
}

// ════════════════════════════════════════════════════════════════════
// growclock_shouldSave
// ════════════════════════════════════════════════════════════════════

void test_should_save_after_interval() {
  TEST_ASSERT_FALSE(growclock_shouldSave(/*now=*/1000, /*lastSave=*/0,
                                         /*interval=*/2000,
                                         /*day=*/500, /*lastSavedDay=*/0));
  TEST_ASSERT_TRUE(growclock_shouldSave(/*now=*/2000, /*lastSave=*/0,
                                        /*interval=*/2000,
                                        /*day=*/500, /*lastSavedDay=*/0));
}

void test_should_save_immediately_on_day_rollover() {
  // Day accumulator rolled backwards (day tick or grow restart) → save now,
  // even mid-interval: a power cut right after a day tick must not restore
  // yesterday's nearly-full accumulator.
  TEST_ASSERT_TRUE(growclock_shouldSave(/*now=*/1000, /*lastSave=*/900,
                                        /*interval=*/2000,
                                        /*day=*/100,
                                        /*lastSavedDay=*/23 * MS_PER_HOUR));
}

// ════════════════════════════════════════════════════════════════════
// Grow-step clock helpers (grow_steps.h step engine persistence)
// ════════════════════════════════════════════════════════════════════

void test_step_elapsed_honoured_only_with_matching_tag() {
  // Tag matches the current step index -> the elapsed is ours.
  TEST_ASSERT_EQUAL_UINT32(25 * 60000UL,
                           growclock_stepElapsedFor(25 * 60000UL, /*tag=*/2, /*cur=*/2));
  // Mismatch (config.json advanced, growclock.json lagged) -> start over.
  // Silently timing the WRONG step would be worse than losing the clock.
  TEST_ASSERT_EQUAL_UINT32(0,
                           growclock_stepElapsedFor(25 * 60000UL, /*tag=*/1, /*cur=*/2));
}

void test_step_save_interval_tightens_for_timer_step() {
  const unsigned long coarse = 30UL * 60UL * 1000UL;
  // Day-scale BUTTON step: coarse interval (flash wear).
  TEST_ASSERT_EQUAL_UINT32(coarse, growclock_saveIntervalMs(coarse, false));
  // Countdown (TIMER) step running: 60 s — a power cut must not swallow
  // a whole 30 min soak that fits inside one coarse save gap.
  TEST_ASSERT_EQUAL_UINT32(60000UL, growclock_saveIntervalMs(coarse, true));
  // Never loosen: if the default is already tighter, keep it.
  TEST_ASSERT_EQUAL_UINT32(1000UL, growclock_saveIntervalMs(1000UL, true));
}

void test_step_forces_save_on_index_change_or_rollback() {
  // Index changed (advance/reset) -> the new epoch tag must hit flash now.
  TEST_ASSERT_TRUE(growclock_stepForcesSave(/*idx=*/1, /*savedIdx=*/0,
                                            /*elapsed=*/0, /*savedElapsed=*/0));
  // Elapsed rolled back within the same index -> anchor was reset, save.
  TEST_ASSERT_TRUE(growclock_stepForcesSave(1, 1, /*elapsed=*/100,
                                            /*savedElapsed=*/500000));
  // Steady forward progress -> no forced save (interval gating decides).
  TEST_ASSERT_FALSE(growclock_stepForcesSave(1, 1, /*elapsed=*/500000,
                                             /*savedElapsed=*/100));
}

// ════════════════════════════════════════════════════════════════════
// Reboot continuity through the scheduler (end-to-end property)
// ════════════════════════════════════════════════════════════════════

void test_day_tick_continues_across_simulated_reboot() {
  PlantConfig plant = {};
  plant.phaseCount = 1;
  plant.phases[0] = {};
  plant.phases[0].durationDays = 14;

  DeviceConfig cfg = {};
  cfg.growActive = true;
  cfg.growPhase = 0;
  cfg.growElapsedDays = 3;

  // "Reboot" at 23 h into the grow day: fresh state, restored anchor.
  SystemState state = {};
  g_stub_millis = 60000;  // 60 s uptime after boot
  state.growDayStartMs = growclock_restoredAnchor(g_stub_millis, 23 * MS_PER_HOUR);

  // 30 min later: not yet 24 h — no tick.
  g_stub_millis += 30 * 60000UL;
  TEST_ASSERT_FALSE(scheduler_updateGrowPhase(&plant, &state, &cfg));
  TEST_ASSERT_EQUAL_INT(3, cfg.growElapsedDays);

  // 31 more minutes: crosses 24 h → the day ticks. Without the restore this
  // would have required 24 h of continuous uptime.
  g_stub_millis += 31 * 60000UL;
  TEST_ASSERT_TRUE(scheduler_updateGrowPhase(&plant, &state, &cfg));
  TEST_ASSERT_EQUAL_INT(4, cfg.growElapsedDays);
}

void test_photoperiod_continues_across_simulated_reboot() {
  PlantConfig plant = {};
  plant.lightHours = 16;
  plant.phaseCount = 0;
  DeviceConfig cfg = {};

  // "Reboot" at 15 h into the light cycle → still ON (15 < 16)...
  SystemState state = {};
  g_stub_millis = 60000;
  state.lightCycleStartMs = growclock_restoredAnchor(g_stub_millis, 15 * MS_PER_HOUR);
  TEST_ASSERT_TRUE(scheduler_shouldLightBeOn(&plant, &state, &cfg));

  // ...and 2 h later the 16 h window has closed → OFF. Without the restore
  // every reboot restarted the photoperiod from hour 0 (light stuck ON).
  g_stub_millis += 2 * MS_PER_HOUR;
  TEST_ASSERT_FALSE(scheduler_shouldLightBeOn(&plant, &state, &cfg));
}

void test_restore_at_boot_with_wrapped_anchor_still_ticks() {
  // Same as the day-tick case but with uptime (1 s) < elapsed (23.5 h), so
  // the restored anchor wraps below zero — the realistic boot situation.
  PlantConfig plant = {};
  plant.phaseCount = 1;
  plant.phases[0] = {};
  plant.phases[0].durationDays = 14;

  DeviceConfig cfg = {};
  cfg.growActive = true;
  cfg.growElapsedDays = 0;

  SystemState state = {};
  g_stub_millis = 1000;
  state.growDayStartMs =
      growclock_restoredAnchor(g_stub_millis,
                               growclock_clampDayElapsed(23 * MS_PER_HOUR + 30 * 60000UL));

  g_stub_millis += 31 * 60000UL;  // 31 min later → crosses 24 h
  TEST_ASSERT_TRUE(scheduler_updateGrowPhase(&plant, &state, &cfg));
  TEST_ASSERT_EQUAL_INT(1, cfg.growElapsedDays);
}

// ════════════════════════════════════════════════════════════════════
// main
// ════════════════════════════════════════════════════════════════════


// ════════════════════════════════════════════════════════════════════
// DLI-kertyman sanitointi (growclock_sanitizeDli)
// ════════════════════════════════════════════════════════════════════

void test_sanitize_dli_passes_plausible_value() {
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 7.82f, growclock_sanitizeDli(7.82f));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, growclock_sanitizeDli(0.0f));
}

// Negatiivinen tai jarjeton kertyma hylataan nollaan. Nollasta aloittaminen on
// turvallisempi vika kuin haamuylijaama: alivalotus nakyy kasvissa, mutta
// keksitty "tavoite jo taynna" ei nay missaan.
void test_sanitize_dli_rejects_garbage() {
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, growclock_sanitizeDli(-1.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, growclock_sanitizeDli(1.0e9f));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f,
                           growclock_sanitizeDli(GROWCLOCK_DLI_MAX_MOL + 1.0f));
}

// Kesken vuorokauden tapahtunut reboot jatkaa samasta kertymasta ja samasta
// ikkunan kohdasta — tama on koko ominaisuuden syy.
void test_dli_survives_simulated_reboot() {
  GrowClockData saved = {};
  saved.dliMol       = growclock_sanitizeDli(5.25f);
  saved.dliElapsedMs = growclock_clampDayElapsed(9UL * 3600000UL);

  // "Reboot": vain tiedoston sisalto sailyy.
  GrowClockData loaded = saved;
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.25f, loaded.dliMol);
  TEST_ASSERT_EQUAL_UINT32(9UL * 3600000UL, loaded.dliElapsedMs);

  unsigned long now = 1500;
  unsigned long anchor = growclock_restoredAnchor(now, loaded.dliElapsedMs);
  TEST_ASSERT_EQUAL_UINT32(loaded.dliElapsedMs, (uint32_t)(now - anchor));
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_restored_anchor_reproduces_elapsed);
  RUN_TEST(test_restored_anchor_wraps_when_elapsed_exceeds_uptime);

  RUN_TEST(test_clamp_day_elapsed_passes_normal_value);
  RUN_TEST(test_clamp_day_elapsed_caps_at_one_day);
  RUN_TEST(test_wrap_light_elapsed_is_cyclic);

  RUN_TEST(test_should_save_after_interval);
  RUN_TEST(test_should_save_immediately_on_day_rollover);

  RUN_TEST(test_step_elapsed_honoured_only_with_matching_tag);
  RUN_TEST(test_step_save_interval_tightens_for_timer_step);
  RUN_TEST(test_step_forces_save_on_index_change_or_rollback);

  RUN_TEST(test_day_tick_continues_across_simulated_reboot);
  RUN_TEST(test_photoperiod_continues_across_simulated_reboot);
  RUN_TEST(test_restore_at_boot_with_wrapped_anchor_still_ticks);

  RUN_TEST(test_sanitize_dli_passes_plausible_value);
  RUN_TEST(test_sanitize_dli_rejects_garbage);
  RUN_TEST(test_dli_survives_simulated_reboot);

  return UNITY_END();
}
