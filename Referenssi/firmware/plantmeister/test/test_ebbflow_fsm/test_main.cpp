#include <unity.h>
// Pakota ebb&flow päälle ENNEN feature-flageja: FSM on puhdasta logiikkaa ja se
// pitää testata lipun tilasta riippumatta. (Aiemmin lippu jäi false -> koko suite
// meni TEST_IGNORE-haaraan eikä ajanut lainkaan.)
#define ENABLE_EBB_FLOW true
#define ENABLE_PUMP true
#include "test_feature_flags.h"

void setUp() {}
void tearDown() {}

#if ENABLE_EBB_FLOW
#include "ebbflow_fsm.h"

using namespace ebbflow;

// {flood_interval, flood_duration, soak_duration, drain_timeout, max_overflow_retries}
static Config cfg = { 1000, 300, 200, 400, 3 };

// ── Perustilakone ───────────────────────────────────────────────

void test_init_state_is_idle() {
  Runtime rt;
  init_runtime(&rt, 0);
  TEST_ASSERT_EQUAL_UINT8(IDLE, rt.state);
  TEST_ASSERT_EQUAL_UINT8(FAULT_NONE, rt.fault);
  TEST_ASSERT_FALSE(rt.fault_latched);
  TEST_ASSERT_EQUAL_UINT8(0, rt.overflow_count);
}

void test_idle_to_flood_when_interval_elapsed() {
  Runtime rt;
  init_runtime(&rt, 0);
  Inputs in = {1001, true, false, false, false};
  Result r = step(&cfg, &in, &rt);
  TEST_ASSERT_TRUE(r.start_pump);
  TEST_ASSERT_EQUAL_UINT8(FLOOD, rt.state);
}

void test_full_cycle_returns_to_idle() {
  Runtime rt;
  init_runtime(&rt, 0);

  Inputs start = {1001, true, false, false, false};
  Result r0 = step(&cfg, &start, &rt);
  TEST_ASSERT_TRUE(r0.start_pump);
  TEST_ASSERT_EQUAL_UINT8(FLOOD, rt.state);

  Inputs flood_done = {1100, true, false, false, false};  // pump stopped
  Result r1 = step(&cfg, &flood_done, &rt);
  TEST_ASSERT_TRUE(r1.state_changed);
  TEST_ASSERT_EQUAL_UINT8(SOAK, rt.state);

  Inputs soak_done = {1400, true, false, false, false};
  Result r2 = step(&cfg, &soak_done, &rt);
  TEST_ASSERT_EQUAL_UINT8(DRAIN, rt.state);

  Inputs drained = {1500, true, false, false, false};
  Result r3 = step(&cfg, &drained, &rt);
  TEST_ASSERT_TRUE(r3.state_changed);
  TEST_ASSERT_EQUAL_UINT8(IDLE, rt.state);
}

// ── FORCE_FLOOD: nappi/portaali pakottaa tulvan heti ────────────

void test_force_flood_starts_before_interval() {
  Runtime rt;
  init_runtime(&rt, 0);
  // now=500 < flood_interval(1000): normaalisti EI tulvi, mutta force_flood pakottaa.
  Inputs in = {500, true, false, false, false, true};
  Result r = step(&cfg, &in, &rt);
  TEST_ASSERT_TRUE(r.start_pump);
  TEST_ASSERT_EQUAL_UINT8(FLOOD, rt.state);
}

void test_force_flood_blocked_when_reservoir_dry() {
  Runtime rt;
  init_runtime(&rt, 0);
  // Pakotus EI saa ohittaa kuiva-ajosuojaa: säiliö tyhjä -> ei pumppausta.
  Inputs in = {500, false, false, false, false, true};
  Result r = step(&cfg, &in, &rt);
  TEST_ASSERT_FALSE(r.start_pump);
  TEST_ASSERT_EQUAL_UINT8(IDLE, rt.state);
  TEST_ASSERT_EQUAL_UINT8(FAULT_DRY_RUN, rt.fault);
  TEST_ASSERT_FALSE(rt.fault_latched);
}

// ── DRY_RUN: advisory, ei latchia, ei pumppausta, toipuu ─────────

void test_dry_run_is_advisory_not_latched() {
  Runtime rt;
  init_runtime(&rt, 0);
  Inputs in = {1001, false, false, false, false};  // interval elapsed but reservoir low
  Result r = step(&cfg, &in, &rt);
  TEST_ASSERT_FALSE(r.start_pump);          // never pump dry
  TEST_ASSERT_EQUAL_UINT8(IDLE, rt.state);  // stays IDLE, cycle not halted
  TEST_ASSERT_EQUAL_UINT8(FAULT_DRY_RUN, rt.fault);
  TEST_ASSERT_FALSE(rt.fault_latched);
}

void test_dry_run_recovers_when_water_returns() {
  Runtime rt;
  init_runtime(&rt, 0);
  Inputs dry = {1001, false, false, false, false};
  (void)step(&cfg, &dry, &rt);
  TEST_ASSERT_EQUAL_UINT8(IDLE, rt.state);

  Inputs refilled = {1002, true, false, false, false};  // water back, interval still elapsed
  Result r = step(&cfg, &refilled, &rt);
  TEST_ASSERT_TRUE(r.start_pump);            // resumes automatically, no ack needed
  TEST_ASSERT_EQUAL_UINT8(FLOOD, rt.state);
  TEST_ASSERT_EQUAL_UINT8(FAULT_NONE, rt.fault);
}

// ── OVERFLOW: yksi tapaus tyhjentää ja yrittää uudelleen ─────────

void test_overflow_single_drains_and_retries() {
  Runtime rt;
  init_runtime(&rt, 0);
  Inputs start = {1001, true, false, false, false};
  (void)step(&cfg, &start, &rt);  // -> FLOOD

  Inputs overflow = {1100, true, true, true, false};
  Result r = step(&cfg, &overflow, &rt);
  TEST_ASSERT_TRUE(r.stop_pump);             // pump stopped immediately (safety)
  TEST_ASSERT_EQUAL_UINT8(DRAIN, rt.state);  // relieve, not halt
  TEST_ASSERT_FALSE(rt.fault_latched);
  TEST_ASSERT_EQUAL_UINT8(FAULT_OVERFLOW, rt.fault);
  TEST_ASSERT_EQUAL_UINT8(1, rt.overflow_count);
}

void test_overflow_repeated_latches() {
  Runtime rt;
  init_runtime(&rt, 0);
  uint32_t t = 1001;
  // Drive flood->overflow cycles; the 3rd overflow (retries=3) must hard-latch.
  for (int i = 0; i < 3; i++) {
    Inputs flood = {t, true, false, false, false};
    (void)step(&cfg, &flood, &rt);            // IDLE -> FLOOD
    TEST_ASSERT_EQUAL_UINT8(FLOOD, rt.state);
    t += 50;
    Inputs ovf = {t, true, true, true, false};
    (void)step(&cfg, &ovf, &rt);
    if (i < 2) {
      TEST_ASSERT_EQUAL_UINT8(DRAIN, rt.state);
      // drain completes -> IDLE so next interval can flood again
      t += 10;
      Inputs drained = {t, true, false, false, false};
      (void)step(&cfg, &drained, &rt);
      TEST_ASSERT_EQUAL_UINT8(IDLE, rt.state);
      t += cfg.flood_interval_ms;  // let interval elapse for next flood
    }
  }
  TEST_ASSERT_EQUAL_UINT8(FAULT, rt.state);
  TEST_ASSERT_EQUAL_UINT8(FAULT_OVERFLOW, rt.fault);
  TEST_ASSERT_TRUE(rt.fault_latched);
}

void test_overflow_count_resets_on_clean_flood() {
  Runtime rt;
  init_runtime(&rt, 0);
  Inputs start = {1001, true, false, false, false};
  (void)step(&cfg, &start, &rt);             // FLOOD
  Inputs ovf = {1050, true, true, true, false};
  (void)step(&cfg, &ovf, &rt);               // -> DRAIN, count=1
  TEST_ASSERT_EQUAL_UINT8(1, rt.overflow_count);
  Inputs drained = {1060, true, false, false, false};
  (void)step(&cfg, &drained, &rt);           // -> IDLE
  uint32_t t = 1060 + cfg.flood_interval_ms;
  Inputs reflood = {t, true, false, false, false};
  (void)step(&cfg, &reflood, &rt);           // -> FLOOD
  Inputs clean = {t + 50, true, false, false, false};  // pump done, no overflow
  (void)step(&cfg, &clean, &rt);             // FLOOD -> SOAK, resets count
  TEST_ASSERT_EQUAL_UINT8(SOAK, rt.state);
  TEST_ASSERT_EQUAL_UINT8(0, rt.overflow_count);
}

// ── FLOOD_TIMEOUT: kova latch (pumppu jumissa) ───────────────────

void test_flood_timeout_latches() {
  Runtime rt;
  init_runtime(&rt, 0);
  Inputs start = {1001, true, false, false, false};
  (void)step(&cfg, &start, &rt);
  TEST_ASSERT_EQUAL_UINT8(FLOOD, rt.state);

  Inputs stuck = {2502, true, false, true, false};  // pump still running past window
  Result r = step(&cfg, &stuck, &rt);
  TEST_ASSERT_TRUE(r.stop_pump);
  TEST_ASSERT_EQUAL_UINT8(FAULT, rt.state);
  TEST_ASSERT_EQUAL_UINT8(FAULT_FLOOD_TIMEOUT, rt.fault);
  TEST_ASSERT_TRUE(rt.fault_latched);
}

// ── DRAIN_TIMEOUT: advisory, ei latchia, toipuu ──────────────────

void test_drain_timeout_is_advisory_not_latched() {
  Runtime rt;
  init_runtime(&rt, 0);
  Inputs start = {1001, true, false, false, false};
  (void)step(&cfg, &start, &rt);                 // FLOOD
  Inputs flood_done = {1100, true, false, false, false};
  (void)step(&cfg, &flood_done, &rt);            // SOAK
  Inputs soak_done = {1400, true, false, false, false};
  (void)step(&cfg, &soak_done, &rt);             // DRAIN
  TEST_ASSERT_EQUAL_UINT8(DRAIN, rt.state);

  Inputs slow = {1900, false, false, false, false};  // not drained past timeout
  Result r = step(&cfg, &slow, &rt);
  TEST_ASSERT_FALSE(r.stop_pump);
  TEST_ASSERT_EQUAL_UINT8(DRAIN, rt.state);      // keeps waiting, no halt
  TEST_ASSERT_FALSE(rt.fault_latched);
  TEST_ASSERT_EQUAL_UINT8(FAULT_DRAIN_TIMEOUT, rt.fault);

  // eventually drains -> recovers to IDLE on its own
  Inputs drained = {2500, true, false, false, false};
  Result r2 = step(&cfg, &drained, &rt);
  TEST_ASSERT_TRUE(r2.state_changed);
  TEST_ASSERT_EQUAL_UINT8(IDLE, rt.state);
  TEST_ASSERT_EQUAL_UINT8(FAULT_NONE, rt.fault);
}

// ── Manuaalinen ACK kovasta faultista ───────────────────────────

void test_ack_clears_latched_fault() {
  Runtime rt;
  init_runtime(&rt, 0);
  Inputs start = {1001, true, false, false, false};
  (void)step(&cfg, &start, &rt);
  Inputs stuck = {2502, true, false, true, false};
  (void)step(&cfg, &stuck, &rt);                 // latched FLOOD_TIMEOUT
  TEST_ASSERT_TRUE(rt.fault_latched);

  Inputs ack = {2600, true, false, false, true};
  Result r = step(&cfg, &ack, &rt);
  TEST_ASSERT_TRUE(r.fault_changed);
  TEST_ASSERT_EQUAL_UINT8(IDLE, rt.state);       // level ok -> IDLE
  TEST_ASSERT_FALSE(rt.fault_latched);
  TEST_ASSERT_EQUAL_UINT8(0, rt.overflow_count);
}

void test_ack_without_level_ok_goes_to_drain() {
  Runtime rt;
  init_runtime(&rt, 0);
  Inputs start = {1001, true, false, false, false};
  (void)step(&cfg, &start, &rt);
  Inputs stuck = {2502, true, false, true, false};
  (void)step(&cfg, &stuck, &rt);                 // latched
  Inputs ack = {2600, false, false, false, true};
  Result r = step(&cfg, &ack, &rt);
  TEST_ASSERT_TRUE(r.state_changed);
  TEST_ASSERT_EQUAL_UINT8(DRAIN, rt.state);
  TEST_ASSERT_EQUAL_UINT8(FAULT_NONE, rt.fault);
}

// ── millis() wraparound ──────────────────────────────────────────

void test_idle_to_flood_wraparound_elapsed() {
  Runtime rt;
  init_runtime(&rt, 0xFFFFF000U);
  Inputs in = {0x00000500U, true, false, false, false};
  Result r = step(&cfg, &in, &rt);
  TEST_ASSERT_TRUE(r.start_pump);
  TEST_ASSERT_EQUAL_UINT8(FLOOD, rt.state);
}

void test_flood_timeout_wraparound_elapsed() {
  Runtime rt;
  init_runtime(&rt, 0x00000000U);
  rt.state = FLOOD;
  rt.state_since_ms = 0xFFFFFF00U;
  Inputs in = {0x00000600U, true, false, true, false};
  Result r = step(&cfg, &in, &rt);
  TEST_ASSERT_TRUE(r.stop_pump);
  TEST_ASSERT_EQUAL_UINT8(FAULT, rt.state);
  TEST_ASSERT_EQUAL_UINT8(FAULT_FLOOD_TIMEOUT, rt.fault);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_state_is_idle);
  RUN_TEST(test_idle_to_flood_when_interval_elapsed);
  RUN_TEST(test_full_cycle_returns_to_idle);
  RUN_TEST(test_force_flood_starts_before_interval);
  RUN_TEST(test_force_flood_blocked_when_reservoir_dry);
  RUN_TEST(test_dry_run_is_advisory_not_latched);
  RUN_TEST(test_dry_run_recovers_when_water_returns);
  RUN_TEST(test_overflow_single_drains_and_retries);
  RUN_TEST(test_overflow_repeated_latches);
  RUN_TEST(test_overflow_count_resets_on_clean_flood);
  RUN_TEST(test_flood_timeout_latches);
  RUN_TEST(test_drain_timeout_is_advisory_not_latched);
  RUN_TEST(test_ack_clears_latched_fault);
  RUN_TEST(test_ack_without_level_ok_goes_to_drain);
  RUN_TEST(test_idle_to_flood_wraparound_elapsed);
  RUN_TEST(test_flood_timeout_wraparound_elapsed);
  return UNITY_END();
}
#else
void test_ebbflow_fsm_disabled() {
  TEST_IGNORE_MESSAGE("ENABLE_EBB_FLOW=false");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ebbflow_fsm_disabled);
  return UNITY_END();
}
#endif
