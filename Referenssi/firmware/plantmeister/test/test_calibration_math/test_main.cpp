#include <unity.h>

#include "calibration_math.h"

void setUp() {}
void tearDown() {}

void test_tds_apply_uses_gain_and_offset() {
  CalibrationData calib = {};
  calib.tdsOffset = 12.5f;
  calib.tdsGain = 1.2f;

  float result = calibration_applyTds(100.0f, &calib);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 132.5f, result);
}

void test_tds_apply_clamps_negative_to_zero() {
  CalibrationData calib = {};
  calib.tdsOffset = -500.0f;
  calib.tdsGain = 1.0f;

  float result = calibration_applyTds(100.0f, &calib);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, result);
}

void test_pump_duration_uses_calibrated_throughput() {
  CalibrationData calib = {};
  calib.pumpMlPerSec = 2.0f;
  TEST_ASSERT_EQUAL_UINT32(5000UL, calibration_calcPumpDurationMs(10, &calib));
}

void test_motor_clamp_uses_default_limits_when_invalid() {
  CalibrationData calib = {};
  calib.motorStepsDown = 200;
  calib.motorStepsUp = 100;

  TEST_ASSERT_EQUAL_INT32(calibration_defaultMotorDownSteps(), calibration_clampMotorStepTarget(-10, &calib));
  TEST_ASSERT_EQUAL_INT32(calibration_defaultMotorUpSteps(), calibration_clampMotorStepTarget(999999, &calib));
}

// ── Soak-hold duty clamp (calibration_clampSoakDutyPct) ──────────
// 0 stays 0 ("soak with pump off"). Non-zero values are forced into
// [PUMP_SOAK_MIN_DUTY_PCT..100] so the pump never gets a stalling duty.
void test_soak_duty_zero_stays_off() {
  TEST_ASSERT_EQUAL_UINT8(0, calibration_clampSoakDutyPct(0));
}

void test_soak_duty_negative_clamps_to_off() {
  TEST_ASSERT_EQUAL_UINT8(0, calibration_clampSoakDutyPct(-5));
}

void test_soak_duty_below_floor_clamps_up_to_min() {
  // 1..(min-1) all snap UP to the stall floor, never to 0.
  TEST_ASSERT_EQUAL_UINT8(PUMP_SOAK_MIN_DUTY_PCT, calibration_clampSoakDutyPct(1));
  TEST_ASSERT_EQUAL_UINT8(PUMP_SOAK_MIN_DUTY_PCT,
                          calibration_clampSoakDutyPct(PUMP_SOAK_MIN_DUTY_PCT - 1));
}

void test_soak_duty_at_floor_unchanged() {
  TEST_ASSERT_EQUAL_UINT8(PUMP_SOAK_MIN_DUTY_PCT,
                          calibration_clampSoakDutyPct(PUMP_SOAK_MIN_DUTY_PCT));
}

void test_soak_duty_in_range_unchanged() {
  TEST_ASSERT_EQUAL_UINT8(50, calibration_clampSoakDutyPct(50));
  TEST_ASSERT_EQUAL_UINT8(100, calibration_clampSoakDutyPct(100));
}

void test_soak_duty_above_max_clamps_to_100() {
  TEST_ASSERT_EQUAL_UINT8(100, calibration_clampSoakDutyPct(150));
}

// ── PPFD calibration factor (calibration_computePpfdFactor) ──────
// Wizard: /api/calib/ppfd/save. factor = reference / raw, so a later
// multiply by that factor turns the raw relative count back into the
// reference's physical unit.
void test_ppfd_factor_computed_from_reference_and_raw() {
  float factor = calibration_computePpfdFactor(0.955f, 5685.75f);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.955f / 5685.75f, factor);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.955f, factor * 5685.75f);
}

void test_ppfd_factor_zero_reference_rejected() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computePpfdFactor(0.0f, 5685.75f));
}

void test_ppfd_factor_zero_raw_rejected() {
  // No light on the sensor (raw=0) must not divide-by-zero into inf/nan.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computePpfdFactor(500.0f, 0.0f));
}

void test_ppfd_factor_negative_inputs_rejected() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computePpfdFactor(-1.0f, 5685.75f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computePpfdFactor(500.0f, -1.0f));
}

// ── PPFD geometry factor (calibration_computeGeometryFactor) ─────
// Wizard: /api/calib/ppfd/geometry. Both distances are measured from the
// lamp; the factor scales the sensor's reading to the canopy position via
// the inverse-square law.
void test_geometry_factor_inverse_square_from_distances() {
  // Sensor 40 cm from lamp, canopy 15 cm: canopy gets (40/15)^2 = 7.11x more.
  float f = calibration_computeGeometryFactor(400.0f, 150.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.1111f, f);
}

void test_geometry_factor_identity_when_sensor_at_canopy() {
  // Sensor at canopy level is the default case — must be exactly 1.0 so an
  // uncalibrated device behaves as if the feature did not exist.
  TEST_ASSERT_EQUAL_FLOAT(1.0f, calibration_computeGeometryFactor(250.0f, 250.0f));
}

void test_geometry_factor_below_one_when_sensor_nearer_lamp() {
  // Sensor on the lamp bracket, canopy further down -> canopy sees LESS.
  float f = calibration_computeGeometryFactor(100.0f, 200.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, f);
}

// ── Geometria sivuoffsetilla (calibration_computeGeometryFactorOffset) ──
// Todellinen asennus: anturi on TARKOITUKSELLA sivussa latvasta
// (anturien_sijoittelu.md), joten lamppu on seka ylapuolella etta sivussa.

void test_geometry_offset_matches_measured_setup() {
  // Mitattu 28.7.2026: pystykateetti 26 cm, vaakakateetti 15 cm, latva 16 cm.
  // Suora etaisyys 30.02 cm, tulokulma 30 deg -> kerroin 4.063.
  float f = calibration_computeGeometryFactorOffset(260.0f, 150.0f, 160.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.063f, f);
}

// Kosinikorjaus on se osa joka erottaa taman pelkasta etaisyyssuhteesta:
// ilman sita sama geometria antaisi 3.52 eli 15 % liian vahan.
void test_geometry_offset_exceeds_slant_only_estimate() {
  float withCosine = calibration_computeGeometryFactorOffset(260.0f, 150.0f, 160.0f);
  float slantOnly  = calibration_computeGeometryFactor(300.2f, 160.0f);  // hypot(260,150)
  TEST_ASSERT_TRUE(withCosine > slantOnly);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 3.52f, slantOnly);
}

// Offset 0 = anturi suoraan lampun alla -> palautuu kaanteiseen neliolakiin.
void test_geometry_offset_zero_equals_plain_inverse_square() {
  float a = calibration_computeGeometryFactorOffset(400.0f, 0.0f, 150.0f);
  float b = calibration_computeGeometryFactor(400.0f, 150.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, b, a);
}

void test_geometry_offset_rejects_invalid_input() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computeGeometryFactorOffset(0.0f, 150.0f, 160.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computeGeometryFactorOffset(260.0f, 150.0f, 0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computeGeometryFactorOffset(-260.0f, 150.0f, 160.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computeGeometryFactorOffset(260.0f, -150.0f, 160.0f));
}

void test_geometry_factor_rejects_zero_and_negative() {
  // Zero canopy distance would divide by zero; negatives are nonsense input.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computeGeometryFactor(400.0f, 0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computeGeometryFactor(0.0f, 150.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computeGeometryFactor(-400.0f, 150.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, calibration_computeGeometryFactor(400.0f, -150.0f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tds_apply_uses_gain_and_offset);
  RUN_TEST(test_tds_apply_clamps_negative_to_zero);
  RUN_TEST(test_pump_duration_uses_calibrated_throughput);
  RUN_TEST(test_motor_clamp_uses_default_limits_when_invalid);
  RUN_TEST(test_soak_duty_zero_stays_off);
  RUN_TEST(test_soak_duty_negative_clamps_to_off);
  RUN_TEST(test_soak_duty_below_floor_clamps_up_to_min);
  RUN_TEST(test_soak_duty_at_floor_unchanged);
  RUN_TEST(test_soak_duty_in_range_unchanged);
  RUN_TEST(test_soak_duty_above_max_clamps_to_100);
  RUN_TEST(test_ppfd_factor_computed_from_reference_and_raw);
  RUN_TEST(test_ppfd_factor_zero_reference_rejected);
  RUN_TEST(test_ppfd_factor_zero_raw_rejected);
  RUN_TEST(test_ppfd_factor_negative_inputs_rejected);
  RUN_TEST(test_geometry_factor_inverse_square_from_distances);
  RUN_TEST(test_geometry_factor_identity_when_sensor_at_canopy);
  RUN_TEST(test_geometry_factor_below_one_when_sensor_nearer_lamp);
  RUN_TEST(test_geometry_factor_rejects_zero_and_negative);
  RUN_TEST(test_geometry_offset_matches_measured_setup);
  RUN_TEST(test_geometry_offset_exceeds_slant_only_estimate);
  RUN_TEST(test_geometry_offset_zero_equals_plain_inverse_square);
  RUN_TEST(test_geometry_offset_rejects_invalid_input);
  return UNITY_END();
}
