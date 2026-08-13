#include <unity.h>

#define ENABLE_INPUT_ROUTER true
#include "test_feature_flags.h"

#include "input_router.h"

// config_save stub — input_router.h declares but does not define it
bool config_save(const DeviceConfig*) { return true; }

void setUp() {}
void tearDown() {}

void test_intent_none_maps_to_none() {
  TEST_ASSERT_EQUAL_STRING("none", intent_name(INTENT_NONE));
}

void test_intent_start_growing() {
  TEST_ASSERT_EQUAL_STRING("start_growing", intent_name(INTENT_START_GROWING));
}

void test_intent_stop_growing() {
  TEST_ASSERT_EQUAL_STRING("stop_growing", intent_name(INTENT_STOP_GROWING));
}

void test_intent_next_phase() {
  TEST_ASSERT_EQUAL_STRING("next_phase", intent_name(INTENT_NEXT_PHASE));
}

void test_intent_delay_phase() {
  TEST_ASSERT_EQUAL_STRING("delay_phase", intent_name(INTENT_DELAY_PHASE));
}

void test_intent_ack_fault() {
  TEST_ASSERT_EQUAL_STRING("ack_fault", intent_name(INTENT_ACK_FAULT));
}

void test_intent_shutdown() {
  TEST_ASSERT_EQUAL_STRING("shutdown", intent_name(INTENT_SHUTDOWN));
}

void test_intent_reboot() {
  TEST_ASSERT_EQUAL_STRING("reboot", intent_name(INTENT_REBOOT));
}

void test_intent_reactivate_ap() {
  TEST_ASSERT_EQUAL_STRING("reactivate_ap", intent_name(INTENT_REACTIVATE_AP));
}

void test_intent_ebb_flood_now() {
  TEST_ASSERT_EQUAL_STRING("ebb_flood_now", intent_name(INTENT_EBB_FLOOD_NOW));
}

void test_intent_maintenance_enter() {
  TEST_ASSERT_EQUAL_STRING("maintenance_enter", intent_name(INTENT_MAINTENANCE_ENTER));
}

void test_intent_maintenance_exit() {
  TEST_ASSERT_EQUAL_STRING("maintenance_exit", intent_name(INTENT_MAINTENANCE_EXIT));
}

void test_intent_ack_step() {
  TEST_ASSERT_EQUAL_STRING("ack_step", intent_name(INTENT_ACK_STEP));
}

void test_intent_skip_step() {
  TEST_ASSERT_EQUAL_STRING("skip_step", intent_name(INTENT_SKIP_STEP));
}

void test_intent_set_step() {
  TEST_ASSERT_EQUAL_STRING("set_step", intent_name(INTENT_SET_STEP));
}

void test_intent_set_phase() {
  TEST_ASSERT_EQUAL_STRING("set_phase", intent_name(INTENT_SET_PHASE));
}

void test_intent_set_day() {
  TEST_ASSERT_EQUAL_STRING("set_day", intent_name(INTENT_SET_DAY));
}

void test_intent_prev_step() {
  TEST_ASSERT_EQUAL_STRING("prev_step", intent_name(INTENT_PREV_STEP));
}

void test_intent_count_boundary_returns_unknown() {
  TEST_ASSERT_EQUAL_STRING("unknown", intent_name(INTENT_COUNT));
}

void test_all_intents_covered_by_count() {
  // Fails deliberately when an Intent is added without a name — an unnamed
  // intent shows up as "unknown" in logs, which is where these are read.
  TEST_ASSERT_EQUAL_INT(18, (int)INTENT_COUNT);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_intent_none_maps_to_none);
  RUN_TEST(test_intent_start_growing);
  RUN_TEST(test_intent_stop_growing);
  RUN_TEST(test_intent_next_phase);
  RUN_TEST(test_intent_delay_phase);
  RUN_TEST(test_intent_ack_fault);
  RUN_TEST(test_intent_shutdown);
  RUN_TEST(test_intent_reboot);
  RUN_TEST(test_intent_reactivate_ap);
  RUN_TEST(test_intent_ebb_flood_now);
  RUN_TEST(test_intent_maintenance_enter);
  RUN_TEST(test_intent_maintenance_exit);
  RUN_TEST(test_intent_ack_step);
  RUN_TEST(test_intent_skip_step);
  RUN_TEST(test_intent_set_step);
  RUN_TEST(test_intent_set_phase);
  RUN_TEST(test_intent_set_day);
  RUN_TEST(test_intent_prev_step);
  RUN_TEST(test_intent_count_boundary_returns_unknown);
  RUN_TEST(test_all_intents_covered_by_count);
  return UNITY_END();
}
