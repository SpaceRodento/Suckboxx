/*=====================================================================
  test_leaf_ir_fallback/test_main.cpp

  Unit tests for the MLX90614 aim-loss fallback in VPD calculation.
  Exercises vpd_calc.h pure functions + verifies advisory is raised
  when leaf temperature is implausible (simulating sensor_manager.h flow).

  Covers:
    vpd_isLeafTempPlausible() — all three aim-loss conditions
    vpd_calculateLeafVpd()    — plant-empowerment formula vs air VPD
    vpd_selectAndCalculate()  — returns leaf VPD when plausible,
                                air VPD + aimLost=true when not
    Advisory path simulation  — aimLost=true leads to DEVICE_FAULT_SENSOR
                                being set in g_device.advisories
=====================================================================*/

#include <unity.h>
#include "test_feature_flags.h"

// Enable MLX90614 and device state so aim-loss advisory path compiles.
// These must be defined before config.h is pulled in.
#define HW_MLX90614    true
#define HW_BME280      false
#define HW_SCD41       false
#define HW_AS7341      false
#define HW_VL53L0X     false
#define HW_DS18B20     false
#define HW_ADS1115     false
#define HW_INA219      false
#define ENABLE_SENSORS true
#define ENABLE_DEVICE_STATE true

#include "config.h"
#include "structs.h"
#include "device_state.h"
#include "vpd_calc.h"

void setUp() {
  g_stub_millis = 1000;
  device_init();
}

void tearDown() {}

// Floating-point compare with absolute tolerance.
static void assert_near(float expected, float actual, float tol, const char* msg) {
  char buf[80];
  snprintf(buf, sizeof(buf), "%s: expected %.4f got %.4f (tol %.4f)", msg, expected, actual, tol);
  TEST_ASSERT_TRUE_MESSAGE(fabsf(expected - actual) <= tol, buf);
}

// ── vpd_dewPointC ─────────────────────────────────────────────────

void test_dewpoint_25c_60pct() {
  float dp = vpd_dewPointC(25.0f, 60.0f);
  // Magnus formula: γ = ln(0.6) + 17.27*25/(25+237.3) = -0.5108 + 1.6441 = 1.1333
  // dp = 237.3 * 1.1333 / (17.27 - 1.1333) = 268.9 / 16.137 ≈ 16.67°C
  assert_near(16.67f, dp, 0.5f, "dew point 25C/60%");
}

void test_dewpoint_20c_80pct() {
  float dp = vpd_dewPointC(20.0f, 80.0f);
  // expected ~16.4°C
  assert_near(16.4f, dp, 0.5f, "dew point 20C/80%");
}

// ── vpd_isLeafTempPlausible ───────────────────────────────────────

void test_plausible_normal_leaf_cool() {
  // Leaf 3°C below air — typical transpiring leaf in grow tent
  TEST_ASSERT_TRUE(vpd_isLeafTempPlausible(22.0f, 25.0f, 60.0f, 20.0f, false));
}

void test_implausible_below_dewpoint() {
  // Leaf at 10°C when dew point is ~16.7°C (25°C air, 60% RH) → below dewpoint
  TEST_ASSERT_FALSE(vpd_isLeafTempPlausible(10.0f, 25.0f, 60.0f, 0.0f, false));
}

void test_implausible_delta_too_large() {
  // Leaf 20°C above air — sensor aimed at a hot lamp or ceiling
  TEST_ASSERT_FALSE(vpd_isLeafTempPlausible(45.0f, 25.0f, 60.0f, 0.0f, false));
}

void test_implausible_delta_too_large_cold() {
  // Leaf 20°C below air — sensor aimed at cold reservoir wall
  TEST_ASSERT_FALSE(vpd_isLeafTempPlausible(5.0f, 25.0f, 60.0f, 0.0f, false));
}

void test_implausible_water_proximity() {
  // Leaf reads 20.5°C, water is 20.0°C — within LEAF_IR_WATER_PROXIMITY_C (2°C)
  TEST_ASSERT_FALSE(vpd_isLeafTempPlausible(20.5f, 25.0f, 60.0f, 20.0f, true));
}

void test_plausible_not_too_close_to_water() {
  // Leaf reads 22.0°C, water is 18.0°C — delta 4°C > 2°C threshold
  TEST_ASSERT_TRUE(vpd_isLeafTempPlausible(22.0f, 25.0f, 60.0f, 18.0f, true));
}

void test_plausible_water_valid_false_ignored() {
  // waterTempValid=false: water proximity check skipped even if temps are close
  TEST_ASSERT_TRUE(vpd_isLeafTempPlausible(20.5f, 25.0f, 60.0f, 20.0f, false));
}

// ── vpd_calculateLeafVpd ──────────────────────────────────────────

void test_leaf_vpd_differs_from_air_vpd() {
  // Leaf 22°C, air 25°C, 60% RH
  // Air VPD = SVP(25) * 0.4 ≈ 3.169 * 0.4 = 1.267 kPa
  // Leaf VPD = SVP(22) - SVP(25)*0.6 ≈ 2.645 - 1.901 = 0.744 kPa
  float airVpd  = vpd_calculate(25.0f, 60.0f);
  float leafVpd = vpd_calculateLeafVpd(25.0f, 60.0f, 22.0f);
  // Leaf VPD must be lower (cooler leaf → lower SVP at leaf surface)
  TEST_ASSERT_TRUE_MESSAGE(leafVpd < airVpd, "leaf VPD should be less than air VPD");
  TEST_ASSERT_TRUE_MESSAGE(leafVpd > 0.0f, "leaf VPD must be positive");
}

void test_leaf_vpd_equals_air_vpd_when_temps_equal() {
  // When T_leaf == T_air, leaf VPD formula reduces to air VPD
  float airVpd  = vpd_calculate(25.0f, 60.0f);
  float leafVpd = vpd_calculateLeafVpd(25.0f, 60.0f, 25.0f);
  assert_near(airVpd, leafVpd, 0.002f, "VPDs equal when temps equal");
}

// ── vpd_selectAndCalculate ────────────────────────────────────────

void test_select_uses_leaf_when_plausible() {
  bool aimLost = false;
  float vpd = vpd_selectAndCalculate(
      25.0f, 60.0f,
      22.0f, true,    // plausible leaf temp
      18.0f, true,
      &aimLost);
  TEST_ASSERT_FALSE_MESSAGE(aimLost, "aim not lost for plausible leaf");
  // Should match leaf VPD, not air VPD
  float expected = vpd_calculateLeafVpd(25.0f, 60.0f, 22.0f);
  assert_near(expected, vpd, 0.001f, "select returns leaf VPD");
}

void test_select_falls_back_when_implausible_delta() {
  bool aimLost = false;
  // Leaf 45°C — way above air 25°C: |45-25| = 20 > LEAF_IR_MAX_DELTA_FROM_AIR_C(15)
  float vpd = vpd_selectAndCalculate(
      25.0f, 60.0f,
      45.0f, true,    // implausible leaf temp
      18.0f, false,
      &aimLost);
  TEST_ASSERT_TRUE_MESSAGE(aimLost, "aim lost for implausible leaf");
  float expected = vpd_calculate(25.0f, 60.0f);
  assert_near(expected, vpd, 0.001f, "select falls back to air VPD");
}

void test_select_falls_back_when_leaf_invalid() {
  bool aimLost = false;
  float vpd = vpd_selectAndCalculate(
      25.0f, 60.0f,
      22.0f, false,   // leafTempValid=false
      18.0f, false,
      &aimLost);
  TEST_ASSERT_FALSE_MESSAGE(aimLost, "no aim-lost when leaf not valid");
  float expected = vpd_calculate(25.0f, 60.0f);
  assert_near(expected, vpd, 0.001f, "select falls back to air VPD");
}

void test_select_null_aimLostOut_safe() {
  // Passing NULL for aimLostOut should not crash
  float vpd = vpd_selectAndCalculate(
      25.0f, 60.0f, 45.0f, true, 0.0f, false, NULL);
  // Should still fall back to air VPD
  float expected = vpd_calculate(25.0f, 60.0f);
  assert_near(expected, vpd, 0.001f, "NULL aimLostOut safe");
}

// ── Advisory path simulation ──────────────────────────────────────
// Mirrors what sensor_manager.h does: call vpd_selectAndCalculate,
// then raise device_setAdvisory when aimLost=true.

void test_advisory_raised_on_aim_loss() {
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_NONE, g_device.advisories);

  bool aimLost = false;
  (void)vpd_selectAndCalculate(
      25.0f, 60.0f,
      45.0f, true,    // implausible
      18.0f, false,
      &aimLost);

  if (aimLost) {
    device_setAdvisory(DEVICE_FAULT_SENSOR, "MLX90614 aim lost - VPD laskettu T_air");
  }

  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_SENSOR, g_device.advisories & DEVICE_FAULT_SENSOR);
  TEST_ASSERT_NOT_EQUAL(0, strlen(g_device.advisoryMessage));
}

void test_no_advisory_when_leaf_plausible() {
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_NONE, g_device.advisories);

  bool aimLost = false;
  (void)vpd_selectAndCalculate(
      25.0f, 60.0f,
      22.0f, true,    // plausible
      18.0f, true,
      &aimLost);

  if (aimLost) {
    device_setAdvisory(DEVICE_FAULT_SENSOR, "MLX90614 aim lost");
  }

  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_NONE, g_device.advisories & DEVICE_FAULT_SENSOR);
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_dewpoint_25c_60pct);
  RUN_TEST(test_dewpoint_20c_80pct);

  RUN_TEST(test_plausible_normal_leaf_cool);
  RUN_TEST(test_implausible_below_dewpoint);
  RUN_TEST(test_implausible_delta_too_large);
  RUN_TEST(test_implausible_delta_too_large_cold);
  RUN_TEST(test_implausible_water_proximity);
  RUN_TEST(test_plausible_not_too_close_to_water);
  RUN_TEST(test_plausible_water_valid_false_ignored);

  RUN_TEST(test_leaf_vpd_differs_from_air_vpd);
  RUN_TEST(test_leaf_vpd_equals_air_vpd_when_temps_equal);

  RUN_TEST(test_select_uses_leaf_when_plausible);
  RUN_TEST(test_select_falls_back_when_implausible_delta);
  RUN_TEST(test_select_falls_back_when_leaf_invalid);
  RUN_TEST(test_select_null_aimLostOut_safe);

  RUN_TEST(test_advisory_raised_on_aim_loss);
  RUN_TEST(test_no_advisory_when_leaf_plausible);

  return UNITY_END();
}
