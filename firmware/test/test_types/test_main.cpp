#include <unity.h>

#include "core/types.h"

void setUp() {}
void tearDown() {}

void test_defaults_match_design_doc() {
  sbx::StatusParams sp;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, sp.tol_green_kpa);   // D2
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, sp.tol_yellow_kpa);  // D2
  TEST_ASSERT_EQUAL_UINT8(2, sp.master);                     // D7/D16: SYL 3
  sbx::SensorParams sens;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2700.0f, sens.v_zero_mv);
  TEST_ASSERT_GREATER_THAN(sens.fault_lo_mv, sens.fault_hi_mv);
}

void test_status_result_starts_safe() {
  sbx::StatusResult r;
  TEST_ASSERT_FALSE(r.show_verdict);  // ei koskaan vihreää oletuksena
  TEST_ASSERT_EQUAL_INT(-1, r.fault_ch);
  TEST_ASSERT_EQUAL_STRING("", r.headline);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_match_design_doc);
  RUN_TEST(test_status_result_starts_safe);
  return UNITY_END();
}
