/*=====================================================================
  test_sensor_read_budget/test_main.cpp

  Unit tests for the per-driver read() budget tracking in
  sensor_orchestrator.h (sensorOrch_trackBudget).

  Tests the budget logic in isolation: SensorOrchState.budgetOverruns
  grows on overruns, the 3rd consecutive overrun drops readyFlag and
  returns true (caller should raise advisory), and a successful in-budget
  read resets the counter.

  Uses a fake driver whose read() advances g_stub_millis by a configurable
  amount, making elapsed time fully deterministic without real hardware.
=====================================================================*/

#include <unity.h>

// Feature flags before config.h so overrides take effect
#define ENABLE_SENSORS true
#include "test/stubs/test_feature_flags.h"

#include "config.h"
#include "sensor_driver.h"
#include "sensor_orchestrator.h"

// g_stub_millis is declared static in test/stubs/Arduino.h — same TU as this file.

// ───── Fake driver ──────────────────────────────────────────────────

static bool     g_fakeReady       = false;
static uint32_t g_fakeReadDelayMs = 0;

static bool fake_init(void) { g_fakeReady = true; return true; }
static SensorProbeResult fake_probe(void) { return SENSOR_PROBE_OK; }
static void fake_read(SensorData*) { g_stub_millis += g_fakeReadDelayMs; }

static SensorDriver make_fake() {
  SensorDriver d = {
    .name              = "FAKE",
    .readyFlag         = &g_fakeReady,
    .reprobeIntervalMs = 0,
    .init              = fake_init,
    .probe             = fake_probe,
    .read              = fake_read
  };
  return d;
}

// Helper: measure a fake read and call trackBudget. Returns the dropped flag.
static bool do_read_and_track(SensorDriver* drv, SensorOrchState* state,
                               uint32_t budgetMs) {
  uint32_t t0 = millis();
  drv->read(nullptr);
  uint32_t elapsed = (uint32_t)(millis() - t0);
  return sensorOrch_trackBudget(drv, state, elapsed, budgetMs);
}

void setUp(void) {
  g_fakeReady       = true;
  g_fakeReadDelayMs = 0;
  g_stub_millis     = 0;
}
void tearDown(void) {}

// ───── Tests ────────────────────────────────────────────────────────

void test_overrun_counter_increments_on_slow_read() {
  SensorDriver drv = make_fake();
  SensorOrchState st = {};
  g_fakeReadDelayMs = SENSOR_READ_BUDGET_MS + 1;

  bool dropped = do_read_and_track(&drv, &st, SENSOR_READ_BUDGET_MS);

  TEST_ASSERT_EQUAL_UINT8(1, st.budgetOverruns);
  TEST_ASSERT_TRUE(g_fakeReady);    // one overrun does not drop yet
  TEST_ASSERT_FALSE(dropped);
}

void test_second_overrun_does_not_drop() {
  SensorDriver drv = make_fake();
  SensorOrchState st = {};
  g_fakeReadDelayMs = SENSOR_READ_BUDGET_MS + 1;

  do_read_and_track(&drv, &st, SENSOR_READ_BUDGET_MS);
  g_stub_millis = 0;
  bool dropped = do_read_and_track(&drv, &st, SENSOR_READ_BUDGET_MS);

  TEST_ASSERT_EQUAL_UINT8(2, st.budgetOverruns);
  TEST_ASSERT_TRUE(g_fakeReady);
  TEST_ASSERT_FALSE(dropped);
}

void test_third_overrun_drops_ready_and_returns_true() {
  SensorDriver drv = make_fake();
  SensorOrchState st = {};
  g_fakeReadDelayMs = SENSOR_READ_BUDGET_MS + 1;

  for (int i = 0; i < 2; i++) {
    g_stub_millis = 0;
    do_read_and_track(&drv, &st, SENSOR_READ_BUDGET_MS);
  }
  g_stub_millis = 0;
  bool dropped = do_read_and_track(&drv, &st, SENSOR_READ_BUDGET_MS);

  TEST_ASSERT_EQUAL_UINT8(3, st.budgetOverruns);
  TEST_ASSERT_FALSE(g_fakeReady);   // readyFlag dropped
  TEST_ASSERT_TRUE(dropped);        // caller must raise advisory
}

void test_in_budget_read_resets_counter() {
  SensorDriver drv = make_fake();
  SensorOrchState st = {};
  st.budgetOverruns = 2;             // pre-load streak

  g_fakeReadDelayMs = 0;             // instant read — well within budget
  bool dropped = do_read_and_track(&drv, &st, SENSOR_READ_BUDGET_MS);

  TEST_ASSERT_EQUAL_UINT8(0, st.budgetOverruns);
  TEST_ASSERT_TRUE(g_fakeReady);
  TEST_ASSERT_FALSE(dropped);
}

void test_already_dropped_no_second_drop_signal() {
  SensorDriver drv = make_fake();
  SensorOrchState st = {};
  st.budgetOverruns = 3;
  g_fakeReady = false;               // already dropped by a previous cycle

  g_fakeReadDelayMs = SENSOR_READ_BUDGET_MS + 1;
  // NOTE: sensorOrch_tick would gate read() when readyFlag=false; here we
  // call trackBudget directly to verify no spurious second-drop signal.
  bool dropped = sensorOrch_trackBudget(&drv, &st,
                                         SENSOR_READ_BUDGET_MS + 1,
                                         SENSOR_READ_BUDGET_MS);

  TEST_ASSERT_FALSE(dropped);        // was already false — not "newly" dropped
  TEST_ASSERT_FALSE(g_fakeReady);
}

void test_counter_increments_but_no_drop_when_already_false() {
  SensorDriver drv = make_fake();
  SensorOrchState st = {};
  st.budgetOverruns = 5;
  g_fakeReady = false;

  sensorOrch_trackBudget(&drv, &st, SENSOR_READ_BUDGET_MS + 1, SENSOR_READ_BUDGET_MS);

  TEST_ASSERT_EQUAL_UINT8(6, st.budgetOverruns);  // counter still grows
  TEST_ASSERT_FALSE(g_fakeReady);
}

void test_exact_budget_is_not_an_overrun() {
  SensorDriver drv = make_fake();
  SensorOrchState st = {};
  st.budgetOverruns = 1;

  // elapsed == budget: NOT an overrun (>), so counter resets
  bool dropped = sensorOrch_trackBudget(&drv, &st,
                                         SENSOR_READ_BUDGET_MS,
                                         SENSOR_READ_BUDGET_MS);

  TEST_ASSERT_EQUAL_UINT8(0, st.budgetOverruns);
  TEST_ASSERT_FALSE(dropped);
}

void test_reset_state_clears_budget_overruns() {
  SensorOrchState st = { .lastProbeMs = 999, .consecutiveErrors = 2, .budgetOverruns = 3 };
  sensorOrch_resetState(&st);
  TEST_ASSERT_EQUAL_UINT32(0, st.lastProbeMs);
  TEST_ASSERT_EQUAL_UINT8(0, st.consecutiveErrors);
  TEST_ASSERT_EQUAL_UINT8(0, st.budgetOverruns);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_overrun_counter_increments_on_slow_read);
  RUN_TEST(test_second_overrun_does_not_drop);
  RUN_TEST(test_third_overrun_drops_ready_and_returns_true);
  RUN_TEST(test_in_budget_read_resets_counter);
  RUN_TEST(test_already_dropped_no_second_drop_signal);
  RUN_TEST(test_counter_increments_but_no_drop_when_already_false);
  RUN_TEST(test_exact_budget_is_not_an_overrun);
  RUN_TEST(test_reset_state_clears_budget_overruns);
  return UNITY_END();
}
