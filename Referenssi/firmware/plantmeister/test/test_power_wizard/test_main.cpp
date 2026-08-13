#include <unity.h>

// Tests the INA228 power-current calibration wizard math.
// WIZARD: power-current-cal — see docs/ohjeet/wizardit.md.
// The wizard's core logic is calibration_refinePowerCalFactor(): given the
// stored factor + a reference vs. measured current, it returns the refined
// factor so the device reads the reference.

#include "test/stubs/test_feature_flags.h"
#include "calibration_math.h"

void setUp(void) {}
void tearDown(void) {}

// Device reads LOW vs reference -> factor scales UP to compensate.
void test_refine_scales_up_when_reading_low(void) {
  // measured 90, ref 100 -> 1.0 * 100/90 = 1.111
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.11111f,
      calibration_refinePowerCalFactor(1.0f, 100.0f, 90.0f));
}

// Device reads HIGH vs reference -> factor scales DOWN.
void test_refine_scales_down_when_reading_high(void) {
  // measured 110, ref 100 -> 100/110 = 0.909
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.90909f,
      calibration_refinePowerCalFactor(1.0f, 100.0f, 110.0f));
}

void test_refine_exact_match_keeps_factor(void) {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f,
      calibration_refinePowerCalFactor(1.0f, 100.0f, 100.0f));
}

// Re-runnable: applies multiplicatively on the existing factor (convergence).
void test_refine_is_multiplicative(void) {
  // old 1.2, measured 96, ref 100 -> 1.2 * 100/96 = 1.25
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.25f,
      calibration_refinePowerCalFactor(1.2f, 100.0f, 96.0f));
}

void test_refine_clamps_high(void) {
  // measured 10, ref 100 -> 10.0 -> clamp 2.0
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f,
      calibration_refinePowerCalFactor(1.0f, 100.0f, 10.0f));
}

void test_refine_clamps_low(void) {
  // measured 100, ref 10 -> 0.1 -> clamp 0.5
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f,
      calibration_refinePowerCalFactor(1.0f, 10.0f, 100.0f));
}

void test_refine_guards_zero_measured(void) {
  // No usable reading -> keep old factor unchanged.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.3f,
      calibration_refinePowerCalFactor(1.3f, 100.0f, 0.0f));
}

void test_refine_guards_nonpositive_reference(void) {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.3f,
      calibration_refinePowerCalFactor(1.3f, -5.0f, 100.0f));
}

// Measured current is signed; calibration uses magnitude (polarity is wiring).
void test_refine_uses_magnitude_of_measured(void) {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.11111f,
      calibration_refinePowerCalFactor(1.0f, 100.0f, -90.0f));
}

// A zero/negative stored factor is treated as 1.0 (uncalibrated) baseline.
void test_refine_normalizes_bad_old_factor(void) {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f,
      calibration_refinePowerCalFactor(0.0f, 100.0f, 100.0f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_refine_scales_up_when_reading_low);
  RUN_TEST(test_refine_scales_down_when_reading_high);
  RUN_TEST(test_refine_exact_match_keeps_factor);
  RUN_TEST(test_refine_is_multiplicative);
  RUN_TEST(test_refine_clamps_high);
  RUN_TEST(test_refine_clamps_low);
  RUN_TEST(test_refine_guards_zero_measured);
  RUN_TEST(test_refine_guards_nonpositive_reference);
  RUN_TEST(test_refine_uses_magnitude_of_measured);
  RUN_TEST(test_refine_normalizes_bad_old_factor);
  return UNITY_END();
}
