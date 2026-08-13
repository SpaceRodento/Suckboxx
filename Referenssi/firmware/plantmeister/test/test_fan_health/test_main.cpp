// Unit tests for the pure fan-health decision logic (fan_evalHealth) in
// fan_hal.h. WHY it exists: because the fan speed is set by PWM, the firmware
// must verify the fan actually spins. This suite pins down the coarse-tacho
// stall verdict — spin-up grace, miss-streak threshold, recovery, and the
// "commanded off ⇒ never stalled" rule — without any hardware.
//
// Serves the fan-health monitoring in fan_hal.h (PCB v2, pcb_v2_layout.md § 2).
// The hardware side (LEDC PWM, MCP23017 tacho poll) is a HAL wrapper and is
// smoke-tested on the rig, not here.

#include <unity.h>

#include "test_feature_flags.h"
#include "fan_hal.h"   // pure fan_evalHealth + FanHealth are outside ENABLE_FAN

// Standard tuning used across the cases.
static const uint32_t GRACE_MS   = 3000;
static const uint16_t MISS_THRESH = 4;

static FanHealth h;

void setUp() {
  h.stalled = false;
  h.commanded = false;
  h.missPolls = 0;
  h.commandedOnSinceMs = 0;
}

void tearDown() {}

// ── Commanded off ─────────────────────────────────────────────────────

// Duty 0: not spinning is expected, so it can never be a stall.
void test_off_never_stalls() {
  FanHealthEval ev = fan_evalHealth(&h, 0, /*sawEdge*/false, 10000, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_FALSE(ev.stalled);
  TEST_ASSERT_FALSE(ev.changed);
}

// ── Spin-up grace ─────────────────────────────────────────────────────

// Commanded on but still inside the spin-up window with no edge yet:
// judgement is held, no stall, miss counter stays at 0.
void test_within_grace_no_edge_holds() {
  // Rising edge at t=0.
  fan_evalHealth(&h, 50, false, 0, GRACE_MS, MISS_THRESH);
  // Still within grace (t=2999 < 3000).
  FanHealthEval ev = fan_evalHealth(&h, 50, false, 2999, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_FALSE(ev.stalled);
  TEST_ASSERT_EQUAL_UINT16(0, h.missPolls);
}

// An edge during grace keeps it healthy and does not accumulate misses.
void test_edge_within_grace_healthy() {
  fan_evalHealth(&h, 50, false, 0, GRACE_MS, MISS_THRESH);
  FanHealthEval ev = fan_evalHealth(&h, 50, true, 1000, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_FALSE(ev.stalled);
  TEST_ASSERT_EQUAL_UINT16(0, h.missPolls);
}

// Rising edge works even at millis()==0 (no 0-sentinel bug).
void test_rising_edge_at_time_zero() {
  fan_evalHealth(&h, 50, false, 0, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_TRUE(h.commanded);
  TEST_ASSERT_EQUAL_UINT32(0, h.commandedOnSinceMs);
  // Immediately after the edge, grace has NOT elapsed → no miss counted.
  TEST_ASSERT_EQUAL_UINT16(0, h.missPolls);
}

// ── Healthy spinning past grace ───────────────────────────────────────

void test_spinning_past_grace_healthy() {
  fan_evalHealth(&h, 50, false, 0, GRACE_MS, MISS_THRESH);
  FanHealthEval ev = fan_evalHealth(&h, 50, true, 5000, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_FALSE(ev.stalled);
  TEST_ASSERT_FALSE(ev.changed);
}

// ── Stall detection ───────────────────────────────────────────────────

// Commanded on, past grace, no edges for MISS_THRESH consecutive polls → stall,
// and 'changed' fires exactly on the poll that crosses the threshold.
void test_no_edge_past_grace_stalls_at_threshold() {
  fan_evalHealth(&h, 50, false, 0, GRACE_MS, MISS_THRESH);  // rising edge

  uint32_t t = GRACE_MS;   // first judged poll is at end of grace
  for (uint16_t i = 1; i < MISS_THRESH; i++) {
    FanHealthEval ev = fan_evalHealth(&h, 50, false, t, GRACE_MS, MISS_THRESH);
    TEST_ASSERT_FALSE(ev.stalled);   // not yet
    t += 500;
  }
  // The MISS_THRESH-th edge-less poll trips the advisory.
  FanHealthEval ev = fan_evalHealth(&h, 50, false, t, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_TRUE(ev.stalled);
  TEST_ASSERT_TRUE(ev.changed);      // rising transition → caller sets advisory
}

// Once stalled, staying edge-less keeps it stalled but does NOT re-fire 'changed'.
void test_stall_persists_without_rechange() {
  fan_evalHealth(&h, 50, false, 0, GRACE_MS, MISS_THRESH);
  uint32_t t = GRACE_MS;
  for (uint16_t i = 0; i < MISS_THRESH; i++) { fan_evalHealth(&h, 50, false, t, GRACE_MS, MISS_THRESH); t += 500; }
  TEST_ASSERT_TRUE(h.stalled);
  FanHealthEval ev = fan_evalHealth(&h, 50, false, t, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_TRUE(ev.stalled);
  TEST_ASSERT_FALSE(ev.changed);
}

// ── Recovery ──────────────────────────────────────────────────────────

// A returning edge clears the stall and fires 'changed' (falling transition).
void test_recovery_edge_clears_stall() {
  fan_evalHealth(&h, 50, false, 0, GRACE_MS, MISS_THRESH);
  uint32_t t = GRACE_MS;
  for (uint16_t i = 0; i < MISS_THRESH; i++) { fan_evalHealth(&h, 50, false, t, GRACE_MS, MISS_THRESH); t += 500; }
  TEST_ASSERT_TRUE(h.stalled);

  FanHealthEval ev = fan_evalHealth(&h, 50, true, t, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_FALSE(ev.stalled);
  TEST_ASSERT_TRUE(ev.changed);
  TEST_ASSERT_EQUAL_UINT16(0, h.missPolls);
}

// Commanding the fan off while stalled clears the advisory.
void test_off_while_stalled_clears() {
  fan_evalHealth(&h, 50, false, 0, GRACE_MS, MISS_THRESH);
  uint32_t t = GRACE_MS;
  for (uint16_t i = 0; i < MISS_THRESH; i++) { fan_evalHealth(&h, 50, false, t, GRACE_MS, MISS_THRESH); t += 500; }
  TEST_ASSERT_TRUE(h.stalled);

  FanHealthEval ev = fan_evalHealth(&h, 0, false, t, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_FALSE(ev.stalled);
  TEST_ASSERT_TRUE(ev.changed);
  TEST_ASSERT_FALSE(h.commanded);
}

// After an off→on restart the grace window is re-armed (no immediate stall).
void test_restart_rearms_grace() {
  // Stall, then turn off.
  fan_evalHealth(&h, 50, false, 0, GRACE_MS, MISS_THRESH);
  uint32_t t = GRACE_MS;
  for (uint16_t i = 0; i < MISS_THRESH; i++) { fan_evalHealth(&h, 50, false, t, GRACE_MS, MISS_THRESH); t += 500; }
  fan_evalHealth(&h, 0, false, t, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_FALSE(h.commanded);

  // Restart at a much later time — the new rising edge re-stamps grace, so a
  // single edge-less poll right after must not stall.
  fan_evalHealth(&h, 50, false, t + 1000, GRACE_MS, MISS_THRESH);
  FanHealthEval ev = fan_evalHealth(&h, 50, false, t + 1500, GRACE_MS, MISS_THRESH);
  TEST_ASSERT_FALSE(ev.stalled);
  TEST_ASSERT_EQUAL_UINT16(0, h.missPolls);
}

// ── main ──────────────────────────────────────────────────────────────

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_off_never_stalls);

  RUN_TEST(test_within_grace_no_edge_holds);
  RUN_TEST(test_edge_within_grace_healthy);
  RUN_TEST(test_rising_edge_at_time_zero);

  RUN_TEST(test_spinning_past_grace_healthy);

  RUN_TEST(test_no_edge_past_grace_stalls_at_threshold);
  RUN_TEST(test_stall_persists_without_rechange);

  RUN_TEST(test_recovery_edge_clears_stall);
  RUN_TEST(test_off_while_stalled_clears);
  RUN_TEST(test_restart_rearms_grace);

  return UNITY_END();
}
