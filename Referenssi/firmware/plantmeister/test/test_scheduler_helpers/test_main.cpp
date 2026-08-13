/*=====================================================================
  test_scheduler_helpers — Unit tests for scheduler_ebb_helpers.h

  Tests: scheduler_baseFloodIntervalMin, scheduler_effectiveFloodIntervalMin,
  scheduler_floodReasonStr — all pure functions, no hardware stubs needed.
=====================================================================*/
#include <unity.h>
#include <string.h>

#include "test_feature_flags.h"
#include "config.h"
#include "structs.h"
#include "scheduler_ebb_helpers.h"

static GrowPhaseParams g_phase;

void setUp() {
  memset(&g_phase, 0, sizeof(g_phase));
  g_phase.floodIntervalMin = 180;
  g_phase.floodDurationSec = 45;
}

void tearDown() {}

// ── scheduler_baseFloodIntervalMin ──────────────────────────────

void test_base_override_wins_over_phase() {
  TEST_ASSERT_EQUAL(60, scheduler_baseFloodIntervalMin(&g_phase, 60));
}

void test_base_uses_phase_when_no_override() {
  TEST_ASSERT_EQUAL(180, scheduler_baseFloodIntervalMin(&g_phase, 0));
}

void test_base_zero_when_phase_interval_zero() {
  GrowPhaseParams noFlood;
  memset(&noFlood, 0, sizeof(noFlood));
  TEST_ASSERT_EQUAL(0, scheduler_baseFloodIntervalMin(&noFlood, 0));
}

void test_base_zero_when_phase_null_and_no_override() {
  TEST_ASSERT_EQUAL(0, scheduler_baseFloodIntervalMin(NULL, 0));
}

void test_base_override_with_null_phase() {
  TEST_ASSERT_EQUAL(30, scheduler_baseFloodIntervalMin(NULL, 30));
}

// ── scheduler_effectiveFloodIntervalMin ─────────────────────────

void test_effective_day_no_multiplier() {
  TEST_ASSERT_EQUAL(180, scheduler_effectiveFloodIntervalMin(&g_phase, 0, true));
}

void test_effective_night_applies_multiplier() {
  uint16_t expected = (uint16_t)(180u * EBB_NIGHT_FLOOD_MULTIPLIER);
  TEST_ASSERT_EQUAL(expected, scheduler_effectiveFloodIntervalMin(&g_phase, 0, false));
}

void test_effective_zero_phase_stays_zero_at_night() {
  GrowPhaseParams noFlood;
  memset(&noFlood, 0, sizeof(noFlood));
  TEST_ASSERT_EQUAL(0, scheduler_effectiveFloodIntervalMin(&noFlood, 0, false));
}

void test_effective_override_night_multiplied() {
  uint16_t expected = (uint16_t)(60u * EBB_NIGHT_FLOOD_MULTIPLIER);
  TEST_ASSERT_EQUAL(expected, scheduler_effectiveFloodIntervalMin(&g_phase, 60, false));
}

void test_effective_override_day_no_multiplier() {
  TEST_ASSERT_EQUAL(60, scheduler_effectiveFloodIntervalMin(&g_phase, 60, true));
}

// ── scheduler_floodReasonStr ────────────────────────────────────

void test_reason_no_flood_phase_contains_pois() {
  GrowPhaseParams noFlood;
  memset(&noFlood, 0, sizeof(noFlood));
  char out[80];
  scheduler_floodReasonStr(out, sizeof(out), &noFlood, 0, true);
  TEST_ASSERT_NOT_NULL(strstr(out, "pois"));
}

void test_reason_null_phase_contains_pois() {
  char out[80];
  scheduler_floodReasonStr(out, sizeof(out), NULL, 0, true);
  TEST_ASSERT_NOT_NULL(strstr(out, "pois"));
}

void test_reason_phase_day_contains_vaihe() {
  char out[80];
  scheduler_floodReasonStr(out, sizeof(out), &g_phase, 0, true);
  TEST_ASSERT_NOT_NULL(strstr(out, "vaihe"));
}

void test_reason_phase_day_no_night_marker() {
  char out[80];
  scheduler_floodReasonStr(out, sizeof(out), &g_phase, 0, true);
  TEST_ASSERT_NULL(strstr(out, "yo"));
}

void test_reason_override_contains_override() {
  char out[80];
  scheduler_floodReasonStr(out, sizeof(out), &g_phase, 60, true);
  TEST_ASSERT_NOT_NULL(strstr(out, "override"));
}

void test_reason_phase_night_contains_yo() {
  char out[80];
  scheduler_floodReasonStr(out, sizeof(out), &g_phase, 0, false);
#if EBB_NIGHT_FLOOD_MULTIPLIER > 1
  TEST_ASSERT_NOT_NULL(strstr(out, "yo"));
#else
  TEST_ASSERT_NULL(strstr(out, "yo"));
#endif
}

void test_reason_override_night_contains_yo() {
  char out[80];
  scheduler_floodReasonStr(out, sizeof(out), &g_phase, 60, false);
#if EBB_NIGHT_FLOOD_MULTIPLIER > 1
  TEST_ASSERT_NOT_NULL(strstr(out, "yo"));
#else
  TEST_ASSERT_NULL(strstr(out, "yo"));
#endif
}

// ── scheduler_secondsUntilNextFlood ─────────────────────────────

void test_next_flood_not_scheduled_returns_minus_one() {
  // effectiveIntervalMin == 0 → flooding not scheduled → -1 sentinel.
  TEST_ASSERT_EQUAL_INT32(-1, scheduler_secondsUntilNextFlood(0, 1000, 5000));
}

void test_next_flood_overdue_returns_zero() {
  // 1 min interval, 2 min elapsed → due now (0).
  TEST_ASSERT_EQUAL_INT32(0, scheduler_secondsUntilNextFlood(1, 0, 120000));
}

void test_next_flood_midway_returns_remaining() {
  // 10 min interval (600 s), 1 min elapsed → 540 s remaining.
  TEST_ASSERT_EQUAL_INT32(540, scheduler_secondsUntilNextFlood(10, 0, 60000));
}

void test_next_flood_wrap_safe() {
  // millis() wrapped: last just before 2^32, now just after → 512 ms elapsed.
  // 1 min interval → (60000-512)/1000 = 59 s.
  TEST_ASSERT_EQUAL_INT32(59, scheduler_secondsUntilNextFlood(1, 0xFFFFFF00u, 0x00000100u));
}

// ── scheduler_shouldStartCirculate / secondsUntilNextCirculate ──

void test_circulate_starts_when_due_and_idle() {
  // enabled, ebb IDLE, calib idle, not active, 20 min interval, 20 min elapsed.
  TEST_ASSERT_TRUE(scheduler_shouldStartCirculate(true, true, true, false, 20, 0, 1200000));
}

void test_circulate_blocked_when_disabled() {
  TEST_ASSERT_FALSE(scheduler_shouldStartCirculate(false, true, true, false, 20, 0, 1200000));
}

void test_circulate_blocked_when_ebb_not_idle() {
  // Flood in progress (ebbIdle=false) → circulation waits.
  TEST_ASSERT_FALSE(scheduler_shouldStartCirculate(true, false, true, false, 20, 0, 1200000));
}

void test_circulate_blocked_when_calibrating_or_active() {
  TEST_ASSERT_FALSE(scheduler_shouldStartCirculate(true, true, false, false, 20, 0, 1200000));
  TEST_ASSERT_FALSE(scheduler_shouldStartCirculate(true, true, true, true, 20, 0, 1200000));
}

void test_circulate_not_due_before_interval() {
  // 20 min interval, only 10 min elapsed.
  TEST_ASSERT_FALSE(scheduler_shouldStartCirculate(true, true, true, false, 20, 0, 600000));
}

void test_circulate_seconds_until_next() {
  TEST_ASSERT_EQUAL_INT32(-1, scheduler_secondsUntilNextCirculate(false, 20, 0, 0));
  TEST_ASSERT_EQUAL_INT32(0,  scheduler_secondsUntilNextCirculate(true, 20, 0, 1200000));
  // 20 min interval, 10 min elapsed → 600 s remaining.
  TEST_ASSERT_EQUAL_INT32(600, scheduler_secondsUntilNextCirculate(true, 20, 0, 600000));
}

// Priority invariant (#6): in scheduler_update the flood is ticked before a new
// circulation may start, and circulation is gated on the ebb FSM being IDLE.
// Encodes "flood always wins" — while a flood is mid-cycle (ebb != IDLE)
// circulation never starts, so the two never fight over the pump.
void test_circulate_priority_flood_wins() {
  // Flood in progress (ebb not IDLE) → circulation blocked even though due.
  TEST_ASSERT_FALSE(scheduler_shouldStartCirculate(true, false, true, false, 20, 0, 9999999));
  // Only once the flood returns the FSM to IDLE may circulation start.
  TEST_ASSERT_TRUE(scheduler_shouldStartCirculate(true, true, true, false, 20, 0, 9999999));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_base_override_wins_over_phase);
  RUN_TEST(test_base_uses_phase_when_no_override);
  RUN_TEST(test_base_zero_when_phase_interval_zero);
  RUN_TEST(test_base_zero_when_phase_null_and_no_override);
  RUN_TEST(test_base_override_with_null_phase);

  RUN_TEST(test_effective_day_no_multiplier);
  RUN_TEST(test_effective_night_applies_multiplier);
  RUN_TEST(test_effective_zero_phase_stays_zero_at_night);
  RUN_TEST(test_effective_override_night_multiplied);
  RUN_TEST(test_effective_override_day_no_multiplier);

  RUN_TEST(test_reason_no_flood_phase_contains_pois);
  RUN_TEST(test_reason_null_phase_contains_pois);
  RUN_TEST(test_reason_phase_day_contains_vaihe);
  RUN_TEST(test_reason_phase_day_no_night_marker);
  RUN_TEST(test_reason_override_contains_override);
  RUN_TEST(test_reason_phase_night_contains_yo);
  RUN_TEST(test_reason_override_night_contains_yo);

  RUN_TEST(test_next_flood_not_scheduled_returns_minus_one);
  RUN_TEST(test_next_flood_overdue_returns_zero);
  RUN_TEST(test_next_flood_midway_returns_remaining);
  RUN_TEST(test_next_flood_wrap_safe);

  RUN_TEST(test_circulate_starts_when_due_and_idle);
  RUN_TEST(test_circulate_blocked_when_disabled);
  RUN_TEST(test_circulate_blocked_when_ebb_not_idle);
  RUN_TEST(test_circulate_blocked_when_calibrating_or_active);
  RUN_TEST(test_circulate_not_due_before_interval);
  RUN_TEST(test_circulate_seconds_until_next);
  RUN_TEST(test_circulate_priority_flood_wins);

  return UNITY_END();
}
