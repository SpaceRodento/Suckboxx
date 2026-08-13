#include <math.h>
#include <unity.h>

#include "vpd_calc.h"

void setUp() {}
void tearDown() {}

// Floating-point compare with absolute tolerance.
static void assert_near(float expected, float actual, float tol) {
  TEST_ASSERT_TRUE_MESSAGE(fabsf(expected - actual) <= tol, "value outside tolerance");
}

void test_vpd_0c_0pct() {
  assert_near(0.6108f, vpd_calculate(0.0f, 0.0f), 0.002f);
}

void test_vpd_20c_50pct() {
  assert_near(1.1691406f, vpd_calculate(20.0f, 50.0f), 0.002f);
}

void test_vpd_25c_40pct() {
  assert_near(1.9006666f, vpd_calculate(25.0f, 40.0f), 0.002f);
}

void test_vpd_30c_70pct() {
  assert_near(1.2729195f, vpd_calculate(30.0f, 70.0f), 0.002f);
}

void test_vpd_10c_100pct() {
  assert_near(0.0f, vpd_calculate(10.0f, 100.0f), 0.002f);
}

void test_vpd_clamps_over_100pct() {
  assert_near(0.0f, vpd_calculate(22.0f, 110.0f), 0.002f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_vpd_0c_0pct);
  RUN_TEST(test_vpd_20c_50pct);
  RUN_TEST(test_vpd_25c_40pct);
  RUN_TEST(test_vpd_30c_70pct);
  RUN_TEST(test_vpd_10c_100pct);
  RUN_TEST(test_vpd_clamps_over_100pct);
  return UNITY_END();
}
