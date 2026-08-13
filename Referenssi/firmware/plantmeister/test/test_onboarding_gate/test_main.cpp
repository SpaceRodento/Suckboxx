/*
  test_onboarding_gate — onboarding.h completion-gate logic.

  Pure header, no Arduino dependencies (only <stdint.h>), so it is included
  directly without any stubs.

  Guards the reason onboarding.h exists at all: the old portal banner's
  "Valmis" button marked onboarding complete unconditionally, regardless of
  which steps the user had actually completed. Test 1 below is the direct
  regression guard for that bug (empty mask must never pass the gate).
*/

#include <unity.h>

#include "onboarding.h"

void setUp() {}
void tearDown() {}

// ═══════════════════════════════════════════════════════════════════════════
// onboarding_canComplete
// ═══════════════════════════════════════════════════════════════════════════

// Regression guard for the bug this module exists to fix: a fresh device
// (no steps done at all) must never be allowed to mark onboarding complete.
// The old "Valmis" button did exactly this — one click, zero validation.
void test_empty_mask_is_rejected(void) {
  TEST_ASSERT_FALSE(onboarding_canComplete(0x00));
}

// PLANT alone is not enough — the start method must be chosen separately.
// A user who sowed a seed but left growStartMethod at its default got
// basil's cutting-propagation instructions instead of seed instructions
// (docs/kehitys/kayttoonotto-onboarding-kartoitus.md §7 kohta 2).
void test_plant_alone_is_rejected(void) {
  TEST_ASSERT_FALSE(onboarding_canComplete(ONBOARD_STEP_PLANT));
}

void test_method_alone_is_rejected(void) {
  TEST_ASSERT_FALSE(onboarding_canComplete(ONBOARD_STEP_METHOD));
}

void test_plant_and_method_is_accepted(void) {
  TEST_ASSERT_TRUE(onboarding_canComplete(ONBOARD_STEP_PLANT | ONBOARD_STEP_METHOD));
}

// Optional steps alone (no required steps) must not satisfy the gate.
void test_optional_steps_alone_are_rejected(void) {
  TEST_ASSERT_FALSE(onboarding_canComplete(ONBOARD_STEP_WIFI | ONBOARD_STEP_PIN));
}

// Optional steps must not interfere with an otherwise-satisfied gate.
void test_all_steps_set_is_accepted(void) {
  TEST_ASSERT_TRUE(onboarding_canComplete(0x0F));
}

// ═══════════════════════════════════════════════════════════════════════════
// onboarding_missingRequired
// ═══════════════════════════════════════════════════════════════════════════

void test_missing_required_reports_both_on_empty_mask(void) {
  TEST_ASSERT_EQUAL_UINT8(ONBOARD_STEP_PLANT | ONBOARD_STEP_METHOD,
                           onboarding_missingRequired(0x00));
}

void test_missing_required_reports_method_when_only_plant_set(void) {
  TEST_ASSERT_EQUAL_UINT8(ONBOARD_STEP_METHOD,
                           onboarding_missingRequired(ONBOARD_STEP_PLANT));
}

void test_missing_required_is_zero_when_both_required_steps_set(void) {
  TEST_ASSERT_EQUAL_UINT8(0x00,
                           onboarding_missingRequired(ONBOARD_STEP_PLANT | ONBOARD_STEP_METHOD));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_empty_mask_is_rejected);
  RUN_TEST(test_plant_alone_is_rejected);
  RUN_TEST(test_method_alone_is_rejected);
  RUN_TEST(test_plant_and_method_is_accepted);
  RUN_TEST(test_optional_steps_alone_are_rejected);
  RUN_TEST(test_all_steps_set_is_accepted);
  RUN_TEST(test_missing_required_reports_both_on_empty_mask);
  RUN_TEST(test_missing_required_reports_method_when_only_plant_set);
  RUN_TEST(test_missing_required_is_zero_when_both_required_steps_set);

  return UNITY_END();
}
