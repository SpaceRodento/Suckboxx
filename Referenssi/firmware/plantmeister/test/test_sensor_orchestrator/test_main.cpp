/*=====================================================================
  test_sensor_orchestrator/test_main.cpp

  Unit tests for the generic SensorDriver orchestration logic.
  Exercises sensor_orchestrator.h in isolation using fake drivers — no
  hardware libraries, no Wire, no calibration_math.

  Covers:
    - Reprobe is gated by reprobeIntervalMs
    - SENSOR_PROBE_ABSENT clears readyFlag and logs
    - SENSOR_PROBE_OK re-runs init() when readyFlag was cleared
    - SENSOR_PROBE_ERROR leaves readyFlag alone, increments error counter
    - reprobeIntervalMs == 0 disables probing entirely (skip-cycle)
    - 32-bit millis() wrap-around is handled without spurious probe storms
    - read() is called only when readyFlag is true after tick()
=====================================================================*/

#include <unity.h>

#include "test/stubs/test_feature_flags.h"

#include "sensor_driver.h"
#include "sensor_orchestrator.h"

void setUp(void) {}
void tearDown(void) {}

// ───── Fake driver harness ──────────────────────────────────────────
// One global "current driver" per test. Each test sets it up and points
// the SensorDriver function pointers at the static thunks below.

static bool             g_fakeReadyFlag       = false;
static SensorProbeResult g_fakeProbeResult    = SENSOR_PROBE_OK;
static int              g_fakeProbeCalls      = 0;
static int              g_fakeInitCalls       = 0;
static bool             g_fakeInitResult      = true;
static int              g_fakeReadCalls       = 0;

static bool fake_init(void) {
  g_fakeInitCalls++;
  if (g_fakeInitResult) g_fakeReadyFlag = true;
  return g_fakeReadyFlag;
}
static SensorProbeResult fake_probe(void) {
  g_fakeProbeCalls++;
  return g_fakeProbeResult;
}
static void fake_read(SensorData*) {
  g_fakeReadCalls++;
}

static void reset_fake() {
  g_fakeReadyFlag       = false;
  g_fakeProbeResult     = SENSOR_PROBE_OK;
  g_fakeProbeCalls      = 0;
  g_fakeInitCalls       = 0;
  g_fakeInitResult      = true;
  g_fakeReadCalls       = 0;
}

static SensorDriver make_fake(uint32_t reprobeMs) {
  SensorDriver d = {
    .name               = "FAKE",
    .readyFlag          = &g_fakeReadyFlag,
    .reprobeIntervalMs  = reprobeMs,
    .init               = fake_init,
    .probe              = fake_probe,
    .read               = fake_read
  };
  return d;
}

// ───── Tests ────────────────────────────────────────────────────────

void test_first_tick_skips_probe_within_interval() {
  reset_fake();
  g_fakeReadyFlag = true;
  SensorDriver drv = make_fake(10000);
  SensorOrchState st = {};

  // Hot init: tick at t=0, then again at t<interval — no probe should fire
  bool shouldRead = sensorOrch_tick(&drv, &st, 0);
  TEST_ASSERT_TRUE(shouldRead);
  TEST_ASSERT_EQUAL_INT(0, g_fakeProbeCalls);

  shouldRead = sensorOrch_tick(&drv, &st, 5000);
  TEST_ASSERT_TRUE(shouldRead);
  TEST_ASSERT_EQUAL_INT(0, g_fakeProbeCalls);

  // At t == reprobeInterval the first probe should fire
  shouldRead = sensorOrch_tick(&drv, &st, 10000);
  TEST_ASSERT_TRUE(shouldRead);
  TEST_ASSERT_EQUAL_INT(1, g_fakeProbeCalls);
}

void test_probe_only_fires_after_interval_elapses() {
  reset_fake();
  g_fakeReadyFlag = true;
  SensorDriver drv = make_fake(5000);
  SensorOrchState st = {};

  // First probe at t=5000
  sensorOrch_tick(&drv, &st, 5000);
  TEST_ASSERT_EQUAL_INT(1, g_fakeProbeCalls);

  // Within interval: no new probe
  sensorOrch_tick(&drv, &st, 6000);
  sensorOrch_tick(&drv, &st, 7000);
  sensorOrch_tick(&drv, &st, 9999);
  TEST_ASSERT_EQUAL_INT(1, g_fakeProbeCalls);

  // At interval boundary: next probe
  sensorOrch_tick(&drv, &st, 10000);
  TEST_ASSERT_EQUAL_INT(2, g_fakeProbeCalls);
}

void test_probe_absent_clears_readyFlag() {
  reset_fake();
  g_fakeReadyFlag = true;
  g_fakeProbeResult = SENSOR_PROBE_ABSENT;
  SensorDriver drv = make_fake(1000);
  SensorOrchState st = {};

  bool shouldRead = sensorOrch_tick(&drv, &st, 1000);
  TEST_ASSERT_FALSE(shouldRead);
  TEST_ASSERT_FALSE(g_fakeReadyFlag);
  TEST_ASSERT_EQUAL_INT(1, g_fakeProbeCalls);
  TEST_ASSERT_EQUAL_INT(0, g_fakeInitCalls);
}

void test_probe_ok_reinits_when_not_ready() {
  reset_fake();
  g_fakeReadyFlag = false;            // sensor lost previously
  g_fakeProbeResult = SENSOR_PROBE_OK;
  SensorDriver drv = make_fake(1000);
  SensorOrchState st = {};

  bool shouldRead = sensorOrch_tick(&drv, &st, 1000);
  // After tick init() should have been called, and readyFlag set true
  TEST_ASSERT_EQUAL_INT(1, g_fakeProbeCalls);
  TEST_ASSERT_EQUAL_INT(1, g_fakeInitCalls);
  TEST_ASSERT_TRUE(g_fakeReadyFlag);
  TEST_ASSERT_TRUE(shouldRead);
}

void test_probe_ok_does_not_reinit_when_already_ready() {
  reset_fake();
  g_fakeReadyFlag = true;
  g_fakeProbeResult = SENSOR_PROBE_OK;
  SensorDriver drv = make_fake(1000);
  SensorOrchState st = {};

  sensorOrch_tick(&drv, &st, 1000);
  TEST_ASSERT_EQUAL_INT(1, g_fakeProbeCalls);
  TEST_ASSERT_EQUAL_INT(0, g_fakeInitCalls);
  TEST_ASSERT_TRUE(g_fakeReadyFlag);
}

void test_probe_ok_reinit_failure_leaves_flag_false() {
  reset_fake();
  g_fakeReadyFlag = false;
  g_fakeProbeResult = SENSOR_PROBE_OK;
  g_fakeInitResult = false;   // simulate init() failing even after probe OK
  SensorDriver drv = make_fake(1000);
  SensorOrchState st = {};

  bool shouldRead = sensorOrch_tick(&drv, &st, 1000);
  TEST_ASSERT_EQUAL_INT(1, g_fakeInitCalls);
  TEST_ASSERT_FALSE(g_fakeReadyFlag);
  TEST_ASSERT_FALSE(shouldRead);
}

void test_probe_error_does_not_change_state() {
  reset_fake();
  g_fakeReadyFlag = true;
  g_fakeProbeResult = SENSOR_PROBE_ERROR;
  SensorDriver drv = make_fake(1000);
  SensorOrchState st = {};

  sensorOrch_tick(&drv, &st, 1000);
  TEST_ASSERT_TRUE(g_fakeReadyFlag);
  TEST_ASSERT_EQUAL_INT(0, g_fakeInitCalls);
  TEST_ASSERT_EQUAL_UINT8(1, st.consecutiveErrors);

  sensorOrch_tick(&drv, &st, 2000);
  TEST_ASSERT_EQUAL_UINT8(2, st.consecutiveErrors);
}

void test_reprobe_interval_zero_disables_probing() {
  reset_fake();
  g_fakeReadyFlag = true;
  g_fakeProbeResult = SENSOR_PROBE_ABSENT; // would clear flag if probed
  SensorDriver drv = make_fake(0);
  SensorOrchState st = {};

  for (uint32_t now = 0; now < 100000; now += 1000) {
    sensorOrch_tick(&drv, &st, now);
  }
  TEST_ASSERT_EQUAL_INT(0, g_fakeProbeCalls);
  TEST_ASSERT_TRUE(g_fakeReadyFlag);
}

void test_millis_wraparound_does_not_spam_probes() {
  reset_fake();
  g_fakeReadyFlag = true;
  SensorDriver drv = make_fake(10000);
  SensorOrchState st = {};
  st.lastProbeMs = 0xFFFFE000UL;       // just before 32-bit wrap

  // Just after wrap: now is very small. (uint32_t)(now - last) should be
  // a small positive number, NOT 0xFFFFFFFF-ish.
  uint32_t justAfterWrap = 0x00000500UL;
  // delta = now - last (unsigned) = 0x00000500 - 0xFFFFE000 = 0x00002500 = 9472
  // Below interval (10000) so no probe expected.
  sensorOrch_tick(&drv, &st, justAfterWrap);
  TEST_ASSERT_EQUAL_INT(0, g_fakeProbeCalls);

  // A bit further: delta crosses interval, probe should fire.
  uint32_t laterAfterWrap = 0x00001000UL;
  // delta = 0x00001000 - 0xFFFFE000 = 0x00003000 = 12288, exceeds 10000.
  sensorOrch_tick(&drv, &st, laterAfterWrap);
  TEST_ASSERT_EQUAL_INT(1, g_fakeProbeCalls);
}

void test_runtime_degradation_full_cycle() {
  // End-to-end: sensor present → probe OK; sensor disappears → flag clears
  // and read() stops; sensor returns → re-detected and read() resumes.
  reset_fake();
  g_fakeReadyFlag = true;
  g_fakeProbeResult = SENSOR_PROBE_OK;
  SensorDriver drv = make_fake(1000);
  SensorOrchState st = {};

  // Cycle 1: present, read should happen
  TEST_ASSERT_TRUE(sensorOrch_tick(&drv, &st, 1000));

  // Cycle 2: sensor disappears, probe returns ABSENT
  g_fakeProbeResult = SENSOR_PROBE_ABSENT;
  TEST_ASSERT_FALSE(sensorOrch_tick(&drv, &st, 2000));
  TEST_ASSERT_FALSE(g_fakeReadyFlag);

  // Cycle 3: sensor still absent, no read
  TEST_ASSERT_FALSE(sensorOrch_tick(&drv, &st, 3000));

  // Cycle 4: sensor re-appears, probe OK, init should fire and read resumes
  g_fakeProbeResult = SENSOR_PROBE_OK;
  bool shouldRead = sensorOrch_tick(&drv, &st, 4000);
  TEST_ASSERT_TRUE(g_fakeReadyFlag);
  TEST_ASSERT_TRUE(shouldRead);
  TEST_ASSERT_EQUAL_INT(1, g_fakeInitCalls);
}

void test_state_reset_helper() {
  SensorOrchState st = { .lastProbeMs = 99999, .consecutiveErrors = 5 };
  sensorOrch_resetState(&st);
  TEST_ASSERT_EQUAL_UINT32(0, st.lastProbeMs);
  TEST_ASSERT_EQUAL_UINT8(0, st.consecutiveErrors);
}

void test_reprobeDue_pure_helper() {
  // Boundary checks for the public reprobeDue helper used by callers
  // that may want to peek at the orchestrator state.
  SensorDriver drv = make_fake(1000);
  SensorOrchState st = { .lastProbeMs = 1000, .consecutiveErrors = 0 };

  TEST_ASSERT_FALSE(sensorOrch_reprobeDue(&drv, &st, 1500));
  TEST_ASSERT_FALSE(sensorOrch_reprobeDue(&drv, &st, 1999));
  TEST_ASSERT_TRUE(sensorOrch_reprobeDue(&drv, &st, 2000));
  TEST_ASSERT_TRUE(sensorOrch_reprobeDue(&drv, &st, 3000));

  // Disabled: always false
  SensorDriver disabled = make_fake(0);
  TEST_ASSERT_FALSE(sensorOrch_reprobeDue(&disabled, &st, 999999));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_first_tick_skips_probe_within_interval);
  RUN_TEST(test_probe_only_fires_after_interval_elapses);
  RUN_TEST(test_probe_absent_clears_readyFlag);
  RUN_TEST(test_probe_ok_reinits_when_not_ready);
  RUN_TEST(test_probe_ok_does_not_reinit_when_already_ready);
  RUN_TEST(test_probe_ok_reinit_failure_leaves_flag_false);
  RUN_TEST(test_probe_error_does_not_change_state);
  RUN_TEST(test_reprobe_interval_zero_disables_probing);
  RUN_TEST(test_millis_wraparound_does_not_spam_probes);
  RUN_TEST(test_runtime_degradation_full_cycle);
  RUN_TEST(test_state_reset_helper);
  RUN_TEST(test_reprobeDue_pure_helper);
  return UNITY_END();
}
