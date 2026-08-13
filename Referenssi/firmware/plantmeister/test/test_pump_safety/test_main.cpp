#include <unity.h>

#include "test_feature_flags.h"
#include "pump_hal.h"

// ── Setup / teardown ──────────────────────────────────────────────────

void setUp() {
  g_stub_millis = 0;
  pump_init();
}

void tearDown() {}

// ── Initialization ─────────────────────────────────────────────────────

void test_init_pump_is_ready_and_not_running() {
  TEST_ASSERT_TRUE(pump_isReady());
  TEST_ASSERT_FALSE(pump_isRunning());
}

// ── pump_startForMs ───────────────────────────────────────────────────

void test_start_zero_duration_fails() {
  bool ok = pump_startForMs(0);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_FALSE(pump_isRunning());
}

void test_start_valid_duration_succeeds() {
  bool ok = pump_startForMs(1000);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(pump_isRunning());
}

void test_start_while_running_fails() {
  pump_startForMs(1000);
  bool ok = pump_startForMs(500);
  TEST_ASSERT_FALSE(ok);
  // Still running from the first call
  TEST_ASSERT_TRUE(pump_isRunning());
}

void test_remaining_ms_at_start() {
  g_stub_millis = 0;
  pump_startForMs(1000);
  // Immediately after start, elapsed=0 so remaining ≈ duration
  TEST_ASSERT_EQUAL_UINT32(1000, pump_remainingMs());
}

// ── pump_update ───────────────────────────────────────────────────────

void test_update_when_not_running_returns_false() {
  TEST_ASSERT_FALSE(pump_isRunning());
  TEST_ASSERT_FALSE(pump_update());
}

void test_update_before_duration_returns_true() {
  pump_startForMs(1000);
  g_stub_millis = 500;   // elapsed 500 ms < 1000 ms
  TEST_ASSERT_TRUE(pump_update());
  TEST_ASSERT_TRUE(pump_isRunning());
}

void test_update_at_duration_stops_pump() {
  pump_startForMs(1000);
  g_stub_millis = 1000;  // elapsed == duration
  bool still_running = pump_update();
  TEST_ASSERT_FALSE(still_running);
  TEST_ASSERT_FALSE(pump_isRunning());
}

void test_update_past_duration_stops_pump() {
  pump_startForMs(1000);
  g_stub_millis = 2000;  // elapsed > duration
  bool still_running = pump_update();
  TEST_ASSERT_FALSE(still_running);
  TEST_ASSERT_FALSE(pump_isRunning());
}

// ── Safety — absolute max on-time ────────────────────────────────────

void test_emergency_stop_at_absolute_max() {
  // Request a duration beyond the absolute safety ceiling (5 min).
  // The pump should be force-stopped as soon as elapsed reaches the limit.
  unsigned long over_limit = PUMP_ABSOLUTE_MAX_ON_MS + 60000UL;
  pump_startForMs(over_limit);
  TEST_ASSERT_TRUE(pump_isRunning());

  // Advance time just past the hard limit
  g_stub_millis = PUMP_ABSOLUTE_MAX_ON_MS + 1;
  bool still_running = pump_update();

  TEST_ASSERT_FALSE(still_running);
  TEST_ASSERT_FALSE(pump_isRunning());
}

void test_pump_stops_exactly_at_absolute_max() {
  // Start with a duration equal to the absolute max — it should still stop.
  pump_startForMs(PUMP_ABSOLUTE_MAX_ON_MS);
  g_stub_millis = PUMP_ABSOLUTE_MAX_ON_MS;
  // Elapsed == limit → emergency stop path OR normal stop — either way: stopped
  pump_update();
  TEST_ASSERT_FALSE(pump_isRunning());
}

// ── pump_dose ─────────────────────────────────────────────────────────

void test_dose_zero_fails() {
  TEST_ASSERT_FALSE(pump_dose(0));
  TEST_ASSERT_FALSE(pump_isRunning());
}

void test_dose_negative_fails() {
  TEST_ASSERT_FALSE(pump_dose(-5));
  TEST_ASSERT_FALSE(pump_isRunning());
}

void test_dose_100ml_at_default_rate_gives_correct_duration() {
  // Default PUMP_ML_PER_SEC = 1.5f → 100 ml / 1.5 ml/s * 1000 ms/s ≈ 66666 ms
  // Use a 1% tolerance band: 66000 – 67000 ms
  bool ok = pump_dose(100);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(pump_isRunning());

  unsigned long remaining = pump_remainingMs();
  TEST_ASSERT_GREATER_OR_EQUAL(66000UL, remaining);
  TEST_ASSERT_LESS_OR_EQUAL(67000UL, remaining);
}

// ── pump_setMlPerSec / pump_getMlPerSec ───────────────────────────────

void test_set_ml_per_sec_below_minimum_fails() {
  float before = pump_getMlPerSec();
  bool ok = pump_setMlPerSec(0.01f);  // min is 0.05 f
  TEST_ASSERT_FALSE(ok);
  // Value must not have changed
  TEST_ASSERT_EQUAL_FLOAT(before, pump_getMlPerSec());
}

void test_set_ml_per_sec_above_maximum_fails() {
  float before = pump_getMlPerSec();
  bool ok = pump_setMlPerSec(1000.0f);  // max is 200.0 f
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_FLOAT(before, pump_getMlPerSec());
}

void test_set_ml_per_sec_valid_succeeds() {
  bool ok = pump_setMlPerSec(2.5f);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, pump_getMlPerSec());
}

void test_set_ml_per_sec_boundary_min_succeeds() {
  // Exactly at the lower boundary (0.05 f)
  bool ok = pump_setMlPerSec(0.05f);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_FLOAT(0.05f, pump_getMlPerSec());
}

void test_set_ml_per_sec_boundary_max_succeeds() {
  // Exactly at the upper boundary (200.0 f)
  bool ok = pump_setMlPerSec(200.0f);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_FLOAT(200.0f, pump_getMlPerSec());
}

// ── pump_stop ────────────────────────────────────────────────────────

void test_stop_halts_running_pump() {
  pump_startForMs(5000);
  TEST_ASSERT_TRUE(pump_isRunning());
  pump_stop();
  TEST_ASSERT_FALSE(pump_isRunning());
}

void test_remaining_ms_zero_when_not_running() {
  TEST_ASSERT_EQUAL_UINT32(0, pump_remainingMs());
}

// ── Edge case: millis() rollover near 32-bit boundary ────────────────
// Modulo-32 arithmetic must keep "elapsed = now - start" sane across
// the rollover at ~49.7 days. Worth verifying explicitly because the
// pump can run continuously in field deployment.
void test_pump_stops_correctly_across_millis_rollover() {
  // Start the pump 1000 ms before uint32_t wraps.
  g_stub_millis = 0xFFFFFFFFul - 1000UL;
  pump_startForMs(2000);
  TEST_ASSERT_TRUE(pump_isRunning());

  // Advance time past the wrap point — overall elapsed becomes 1500 ms.
  g_stub_millis = 500UL;  // 1000 ms before wrap + 500 ms after = 1500 ms elapsed
  TEST_ASSERT_TRUE(pump_update());
  TEST_ASSERT_TRUE(pump_isRunning());

  // Advance further so total elapsed exceeds the 2000 ms duration.
  g_stub_millis = 1500UL;  // 1000 + 1500 = 2500 ms elapsed > 2000 ms duration
  pump_update();
  TEST_ASSERT_FALSE(pump_isRunning());
}

// ── Opt-in overflow latch auto-clear (pure decision) ──────────────────

// Disabled feature never auto-clears, even when fully drained and settled.
void test_autoclear_disabled_never_clears() {
  TEST_ASSERT_FALSE(pump_overflowAutoClearDue(
      /*latched*/true, /*floatActive*/false, /*haveClearStamp*/true,
      /*floatClearedMs*/0, /*nowMs*/999999, /*enabled*/false, /*settleMs*/30000));
}

// Enabled but latch not set → nothing to clear.
void test_autoclear_not_latched_returns_false() {
  TEST_ASSERT_FALSE(pump_overflowAutoClearDue(
      false, false, true, 0, 999999, true, 30000));
}

// Float still active (bed still overflowing) → never clear regardless of time.
void test_autoclear_float_active_blocks() {
  TEST_ASSERT_FALSE(pump_overflowAutoClearDue(
      true, /*floatActive*/true, true, 0, 999999, true, 30000));
}

// Bed drained but the settle timer hasn't been stamped yet → wait.
void test_autoclear_no_clear_stamp_waits() {
  TEST_ASSERT_FALSE(pump_overflowAutoClearDue(
      true, false, /*haveClearStamp*/false, 0, 999999, true, 30000));
}

// Drained but settle window not yet elapsed → wait.
void test_autoclear_before_settle_waits() {
  TEST_ASSERT_FALSE(pump_overflowAutoClearDue(
      true, false, true, /*cleared*/1000, /*now*/1000 + 29999, true, 30000));
}

// Drained and settle window elapsed → clear.
void test_autoclear_at_settle_clears() {
  TEST_ASSERT_TRUE(pump_overflowAutoClearDue(
      true, false, true, /*cleared*/1000, /*now*/1000 + 30000, true, 30000));
}

// millis() rollover during the settle window must not delay the auto-clear.
void test_autoclear_survives_millis_rollover() {
  uint32_t cleared = 0xFFFFFFF0UL;          // 16 ms before wrap
  uint32_t now = cleared + 30000UL;         // wraps past 0
  TEST_ASSERT_TRUE(pump_overflowAutoClearDue(
      true, false, true, cleared, now, true, 30000));
}

// ── main ──────────────────────────────────────────────────────────────

// ── Maintenance lock ──────────────────────────────────────────────────
// The hazard: an operator with a hand in the reservoir. loop_dispatch already
// stops the scheduler from flooding, but /api/test/pump and the calibration
// wizards call these functions directly — so the refusal has to live here too.

void test_maintenance_lock_refuses_dose() {
  pump_setMaintenanceLock(true);
  TEST_ASSERT_FALSE(pump_startForMs(1000));
  TEST_ASSERT_FALSE(pump_isRunning());
}

void test_maintenance_lock_refuses_continuous() {
  pump_setMaintenanceLock(true);
  TEST_ASSERT_FALSE(pump_setContinuous(50));
  TEST_ASSERT_FALSE(pump_isRunning());
}

void test_maintenance_lock_stops_running_pump_immediately() {
  // Entering maintenance while a dose is in flight must cut it, not wait it out.
  TEST_ASSERT_TRUE(pump_startForMs(60000));
  TEST_ASSERT_TRUE(pump_isRunning());
  pump_setMaintenanceLock(true);
  TEST_ASSERT_FALSE(pump_isRunning());
}

void test_maintenance_lock_release_restores_pumping() {
  pump_setMaintenanceLock(true);
  TEST_ASSERT_FALSE(pump_startForMs(1000));
  pump_setMaintenanceLock(false);
  TEST_ASSERT_FALSE(pump_isMaintenanceLocked());
  TEST_ASSERT_TRUE(pump_startForMs(1000));
}

void test_maintenance_lock_is_independent_of_overflow_latch() {
  // Two different reasons to refuse: clearing the overflow latch must not
  // unlock a device that is still being serviced.
  pump_latchOverflow();
  pump_setMaintenanceLock(true);
  pump_clearOverflowLatch();
  TEST_ASSERT_FALSE(pump_startForMs(1000));
  TEST_ASSERT_TRUE(pump_isMaintenanceLocked());
}

void test_init_clears_maintenance_lock() {
  pump_setMaintenanceLock(true);
  pump_init();
  TEST_ASSERT_FALSE(pump_isMaintenanceLocked());
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_init_pump_is_ready_and_not_running);

  RUN_TEST(test_start_zero_duration_fails);
  RUN_TEST(test_start_valid_duration_succeeds);
  RUN_TEST(test_start_while_running_fails);
  RUN_TEST(test_remaining_ms_at_start);

  RUN_TEST(test_update_when_not_running_returns_false);
  RUN_TEST(test_update_before_duration_returns_true);
  RUN_TEST(test_update_at_duration_stops_pump);
  RUN_TEST(test_update_past_duration_stops_pump);

  RUN_TEST(test_emergency_stop_at_absolute_max);
  RUN_TEST(test_pump_stops_exactly_at_absolute_max);

  RUN_TEST(test_dose_zero_fails);
  RUN_TEST(test_dose_negative_fails);
  RUN_TEST(test_dose_100ml_at_default_rate_gives_correct_duration);

  RUN_TEST(test_set_ml_per_sec_below_minimum_fails);
  RUN_TEST(test_set_ml_per_sec_above_maximum_fails);
  RUN_TEST(test_set_ml_per_sec_valid_succeeds);
  RUN_TEST(test_set_ml_per_sec_boundary_min_succeeds);
  RUN_TEST(test_set_ml_per_sec_boundary_max_succeeds);

  RUN_TEST(test_stop_halts_running_pump);
  RUN_TEST(test_remaining_ms_zero_when_not_running);
  RUN_TEST(test_pump_stops_correctly_across_millis_rollover);

  RUN_TEST(test_autoclear_disabled_never_clears);
  RUN_TEST(test_autoclear_not_latched_returns_false);
  RUN_TEST(test_autoclear_float_active_blocks);
  RUN_TEST(test_autoclear_no_clear_stamp_waits);
  RUN_TEST(test_autoclear_before_settle_waits);
  RUN_TEST(test_autoclear_at_settle_clears);
  RUN_TEST(test_autoclear_survives_millis_rollover);

  RUN_TEST(test_maintenance_lock_refuses_dose);
  RUN_TEST(test_maintenance_lock_refuses_continuous);
  RUN_TEST(test_maintenance_lock_stops_running_pump_immediately);
  RUN_TEST(test_maintenance_lock_release_restores_pumping);
  RUN_TEST(test_maintenance_lock_is_independent_of_overflow_latch);
  RUN_TEST(test_init_clears_maintenance_lock);

  return UNITY_END();
}
