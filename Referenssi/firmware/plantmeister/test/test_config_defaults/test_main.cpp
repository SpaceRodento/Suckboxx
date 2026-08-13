#include <unity.h>

// Shared feature flags for native tests (test/stubs/test_feature_flags.h)
#include "test_feature_flags.h"

#include "config_defaults.h"
#include "calibration_math.h"

void setUp()    {}
void tearDown() {}

void test_defaults_lora_address() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_EQUAL_UINT8(LORA_DEVICE_ADDRESS, cfg.loraAddress);
}

void test_defaults_lora_network_id() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_EQUAL_UINT8(LORA_NETWORK_ID, cfg.loraNetworkId);
}

void test_defaults_lora_target() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_EQUAL_UINT8(LORA_TARGET_ADDRESS, cfg.loraTargetAddress);
}

void test_defaults_plant_id_is_basil() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_EQUAL_STRING("basil", cfg.currentPlantId);
}

void test_defaults_light_on_hour() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_EQUAL_INT(6, cfg.lightOnHour);
}

void test_defaults_motor_target_mm() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_EQUAL_INT(50, cfg.motorTargetMm);
}

void test_defaults_motor_current_mm() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_EQUAL_INT(0, cfg.motorCurrentMm);
}

void test_defaults_grow_inactive() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_FALSE(cfg.growActive);
}

void test_defaults_wifi_auto_connect_off() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_FALSE(cfg.wifiAutoConnect);
}

void test_defaults_admin_pin_empty() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_EQUAL_STRING("", cfg.adminPin);
}

void test_calibration_defaults_and_clamp() {
  CalibrationData calib = {};
  calib.tdsOffset = CALIB_TDS_OFFSET_DEFAULT;
  calib.tdsGain = CALIB_TDS_GAIN_DEFAULT;
  calib.pumpMlPerSec = CALIB_PUMP_ML_PER_SEC_DEFAULT;
  calib.motorStepsDown = calibration_defaultMotorDownSteps();
  calib.motorStepsUp = calibration_defaultMotorUpSteps();
  TEST_ASSERT_EQUAL_FLOAT(CALIB_TDS_OFFSET_DEFAULT, calib.tdsOffset);
  TEST_ASSERT_EQUAL_FLOAT(CALIB_TDS_GAIN_DEFAULT, calib.tdsGain);
  TEST_ASSERT_EQUAL_FLOAT(CALIB_PUMP_ML_PER_SEC_DEFAULT, calib.pumpMlPerSec);

  calib.tdsGain = 100.0f;
  calib.pumpMlPerSec = 0.0f;
  calib.motorStepsDown = 200;
  calib.motorStepsUp = 100;
  TEST_ASSERT_EQUAL_INT32(calibration_defaultMotorDownSteps(), calibration_clampMotorStepTarget(-10, &calib));
  TEST_ASSERT_EQUAL_INT32(calibration_defaultMotorUpSteps(), calibration_clampMotorStepTarget(999999, &calib));
}

void test_defaults_lights_force_off() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_FALSE(cfg.lightsForceOn);
  TEST_ASSERT_FALSE(cfg.lightsForceOff);
}

void test_defaults_strings_null_terminated() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  // Verify that fixed-length char arrays are null-terminated at the last byte
  TEST_ASSERT_EQUAL_INT('\0', cfg.currentPlantId[sizeof(cfg.currentPlantId) - 1]);
  TEST_ASSERT_EQUAL_INT('\0', cfg.wifiSsid[sizeof(cfg.wifiSsid) - 1]);
  TEST_ASSERT_EQUAL_INT('\0', cfg.wifiPassword[sizeof(cfg.wifiPassword) - 1]);
  TEST_ASSERT_EQUAL_INT('\0', cfg.adminPin[sizeof(cfg.adminPin) - 1]);
}

void test_clamp_light_on_hour_out_of_range() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.lightOnHour = 99;

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_INT(6, cfg.lightOnHour);
}

void test_clamp_motor_target_negative_to_zero() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.motorTargetMm = -123;

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_INT(0, cfg.motorTargetMm);
}

void test_clamp_motor_target_above_max() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.motorTargetMm = MOTOR_MAX_HEIGHT_MM + 500;

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_INT(MOTOR_MAX_HEIGHT_MM, cfg.motorTargetMm);
}

void test_clamp_grow_start_method_out_of_range() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.growStartMethod = 255;

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.growStartMethod);
}

void test_clamp_grow_phase_out_of_range() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.growPhase = (uint8_t)(GROW_PHASE_MAX + 2);

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.growPhase);
}

void test_clamp_sensor_interval_too_small_resets_to_zero() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.sensorIntervalMs = 250UL;  // Sub-second interval would starve loop()

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_UINT32(0UL, cfg.sensorIntervalMs);
}

void test_clamp_sensor_interval_zero_stays_zero() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.sensorIntervalMs = 0UL;  // 0 means "use plant defaults"

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_UINT32(0UL, cfg.sensorIntervalMs);
}

void test_clamp_sensor_interval_valid_kept() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.sensorIntervalMs = 30000UL;  // 30s — typical

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_UINT32(30000UL, cfg.sensorIntervalMs);
}

void test_clamp_lora_report_interval_too_small_resets_to_zero() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.loraReportIntervalMs = 999UL;

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_UINT32(0UL, cfg.loraReportIntervalMs);
}

void test_clamp_lora_report_interval_valid_kept() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.loraReportIntervalMs = 120000UL;  // 2 min

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_UINT32(120000UL, cfg.loraReportIntervalMs);
}

void test_clamp_empty_plant_id_resets_to_basil() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.currentPlantId[0] = '\0';

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_STRING("basil", cfg.currentPlantId);
}

void test_clamp_non_empty_plant_id_kept() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  strncpy(cfg.currentPlantId, "parsley", sizeof(cfg.currentPlantId) - 1);

  config_clampBounds(&cfg);
  TEST_ASSERT_EQUAL_STRING("parsley", cfg.currentPlantId);
}

void test_boot_clear_clears_lights_force_on() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.lightsForceOn = true;

  config_clearBootOnlyOverrides(&cfg);
  TEST_ASSERT_FALSE(cfg.lightsForceOn);
  TEST_ASSERT_FALSE(cfg.lightsForceOff);
}

void test_boot_clear_clears_lights_force_off() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.lightsForceOff = true;

  config_clearBootOnlyOverrides(&cfg);
  TEST_ASSERT_FALSE(cfg.lightsForceOn);
  TEST_ASSERT_FALSE(cfg.lightsForceOff);
}

void test_boot_clear_preserves_other_fields() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.lightsForceOn = true;
  cfg.lightOnHour = 8;
  cfg.motorCurrentMm = 123;
  strncpy(cfg.currentPlantId, "parsley", sizeof(cfg.currentPlantId) - 1);

  config_clearBootOnlyOverrides(&cfg);
  TEST_ASSERT_FALSE(cfg.lightsForceOn);
  TEST_ASSERT_EQUAL_INT(8, cfg.lightOnHour);
  TEST_ASSERT_EQUAL_INT(123, cfg.motorCurrentMm);
  TEST_ASSERT_EQUAL_STRING("parsley", cfg.currentPlantId);
}

void test_defaults_ebbflow_fault_latched_false() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_FALSE(cfg.ebbFlowFaultLatched);
}

void test_defaults_ebbflow_fault_code_none() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.ebbFlowFaultCode);
}

void test_boot_clear_clears_ebbflow_fault_latch() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);
  cfg.ebbFlowFaultLatched = true;
  cfg.ebbFlowFaultCode    = 2;

  config_clearBootOnlyOverrides(&cfg);
  TEST_ASSERT_FALSE(cfg.ebbFlowFaultLatched);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.ebbFlowFaultCode);
}

void test_clamp_valid_config_no_changes() {
  DeviceConfig cfg = {};
  config_setDefaults(&cfg);

  int expectedLightOnHour = cfg.lightOnHour;
  int expectedMotorCurrent = cfg.motorCurrentMm;
  int expectedMotorTarget = cfg.motorTargetMm;
  uint8_t expectedGrowStartMethod = cfg.growStartMethod;
  uint8_t expectedGrowPhase = cfg.growPhase;

  config_clampBounds(&cfg);

  TEST_ASSERT_EQUAL_INT(expectedLightOnHour, cfg.lightOnHour);
  TEST_ASSERT_EQUAL_INT(expectedMotorCurrent, cfg.motorCurrentMm);
  TEST_ASSERT_EQUAL_INT(expectedMotorTarget, cfg.motorTargetMm);
  TEST_ASSERT_EQUAL_UINT8(expectedGrowStartMethod, cfg.growStartMethod);
  TEST_ASSERT_EQUAL_UINT8(expectedGrowPhase, cfg.growPhase);
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_defaults_lora_address);
  RUN_TEST(test_defaults_lora_network_id);
  RUN_TEST(test_defaults_lora_target);
  RUN_TEST(test_defaults_plant_id_is_basil);
  RUN_TEST(test_defaults_light_on_hour);
  RUN_TEST(test_defaults_motor_target_mm);
  RUN_TEST(test_defaults_motor_current_mm);
  RUN_TEST(test_defaults_grow_inactive);
  RUN_TEST(test_defaults_wifi_auto_connect_off);
  RUN_TEST(test_defaults_admin_pin_empty);
  RUN_TEST(test_calibration_defaults_and_clamp);
  RUN_TEST(test_defaults_lights_force_off);
  RUN_TEST(test_defaults_strings_null_terminated);
  RUN_TEST(test_clamp_light_on_hour_out_of_range);
  RUN_TEST(test_clamp_motor_target_negative_to_zero);
  RUN_TEST(test_clamp_motor_target_above_max);
  RUN_TEST(test_clamp_grow_start_method_out_of_range);
  RUN_TEST(test_clamp_grow_phase_out_of_range);
  RUN_TEST(test_clamp_sensor_interval_too_small_resets_to_zero);
  RUN_TEST(test_clamp_sensor_interval_zero_stays_zero);
  RUN_TEST(test_clamp_sensor_interval_valid_kept);
  RUN_TEST(test_clamp_lora_report_interval_too_small_resets_to_zero);
  RUN_TEST(test_clamp_lora_report_interval_valid_kept);
  RUN_TEST(test_clamp_empty_plant_id_resets_to_basil);
  RUN_TEST(test_clamp_non_empty_plant_id_kept);
  RUN_TEST(test_boot_clear_clears_lights_force_on);
  RUN_TEST(test_boot_clear_clears_lights_force_off);
  RUN_TEST(test_boot_clear_preserves_other_fields);
  RUN_TEST(test_defaults_ebbflow_fault_latched_false);
  RUN_TEST(test_defaults_ebbflow_fault_code_none);
  RUN_TEST(test_boot_clear_clears_ebbflow_fault_latch);
  RUN_TEST(test_clamp_valid_config_no_changes);

  return UNITY_END();
}
