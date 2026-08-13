// Tests the capture math behind the PUMP-THROUGHPUT calibration wizard
// (WIZARD: pump-throughput). The wizard itself is portal-driven and lives
// in wifi_portal_routes_post.h (/api/calib/pump/*); this suite covers its
// ml/s computation. Wizard registry + naming convention:
// docs/ohjeet/wizardit.md.
#include <unity.h>

#include "test_feature_flags.h"
#include "calibration_math.h"

void setUp() {}
void tearDown() {}

void test_30s_15ml_yields_half_ml_per_sec() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f,
    calibration_computePumpMlPerSec(15.0f, 30000));
}

void test_zero_duration_returns_zero() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f,
    calibration_computePumpMlPerSec(15.0f, 0));
}

void test_zero_ml_returns_zero() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f,
    calibration_computePumpMlPerSec(0.0f, 30000));
}

void test_negative_ml_returns_zero() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f,
    calibration_computePumpMlPerSec(-1.0f, 30000));
}

void test_1s_1ml_yields_one_ml_per_sec() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f,
    calibration_computePumpMlPerSec(1.0f, 1000));
}

void test_round_trip_matches_calcPumpDurationMs() {
  // If we say a pump does 2 ml/s, then 10 ml should take 5000 ms,
  // and feeding (10 ml, 5000 ms) back into compute should give us 2 ml/s.
  CalibrationData calib = {};
  calib.pumpMlPerSec = 2.0f;
  unsigned long durationMs = calibration_calcPumpDurationMs(10, &calib);
  TEST_ASSERT_EQUAL_UINT32(5000UL, (uint32_t)durationMs);

  float recovered = calibration_computePumpMlPerSec(10.0f, (uint32_t)durationMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, recovered);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_30s_15ml_yields_half_ml_per_sec);
  RUN_TEST(test_zero_duration_returns_zero);
  RUN_TEST(test_zero_ml_returns_zero);
  RUN_TEST(test_negative_ml_returns_zero);
  RUN_TEST(test_1s_1ml_yields_one_ml_per_sec);
  RUN_TEST(test_round_trip_matches_calcPumpDurationMs);
  return UNITY_END();
}
