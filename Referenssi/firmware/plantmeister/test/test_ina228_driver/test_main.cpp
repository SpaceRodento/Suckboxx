#include <unity.h>

// Feature flags for INA228-only test surface
#define ENABLE_SENSORS true
#define HW_INA228      true
#define HW_BME280      false
#define HW_SCD41       false
#define HW_VL53L0X     false
#define HW_DS18B20     false
#define HW_ADS1115     false
#define HW_INA219      false
#define HW_AS7341      false
#define HW_MLX90614    false

#include "test/stubs/test_feature_flags.h"
#include "sensor_driver.h"
#include "sensor_driver_ina228.h"

void setUp(void) {
  ina228_resetForTest();
  g_wire_endTransmission_result = 0;
}

void tearDown(void) {}

// Build a 24-bit register value from a signed 20-bit ADC code sitting in
// bits [23:4] (matches how the INA228 lays out VSHUNT/CURRENT).
static uint32_t raw24_from_code20(int32_t code) {
  return ((uint32_t)(code & 0xFFFFF)) << 4;
}

// ── Pure conversion math ─────────────────────────────────────────────

void test_current_lsb(void) {
  // CURRENT_LSB = maxCurrent / 2^19
  TEST_ASSERT_FLOAT_WITHIN(1e-9f, 1e-6f, ina228_currentLsb(0.524288f));   // 1 uA/LSB
  TEST_ASSERT_FLOAT_WITHIN(1e-8f, 1e-5f, ina228_currentLsb(5.24288f));    // 10 uA/LSB
}

void test_shunt_cal_register(void) {
  // cal = 13107.2e6 * CURRENT_LSB * R_shunt   (round to nearest)
  // 1e-6 * 0.015 * 13107.2e6 = 196.608 -> 197
  TEST_ASSERT_EQUAL_UINT16(197, ina228_shuntCalReg(1e-6f, 0.015f, 0));
  // ADCRANGE=1 multiplies by 4: 196.608 * 4 = 786.432 -> 786
  TEST_ASSERT_EQUAL_UINT16(786, ina228_shuntCalReg(1e-6f, 0.015f, 1));
  // Overflow clamps to 16-bit max
  TEST_ASSERT_EQUAL_UINT16(65535, ina228_shuntCalReg(1e-3f, 1.0f, 0));
}

void test_bus_voltage_conversion(void) {
  // 195.3125 uV/LSB, 20-bit in bits [23:4]. 12 V -> code 61440.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 12.0f, ina228_rawToBusV((uint32_t)61440 << 4));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.3f,  ina228_rawToBusV((uint32_t)16896 << 4));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f,  ina228_rawToBusV(0));
}

void test_shunt_voltage_conversion(void) {
  // ADCRANGE=0: 0.3125 uV/LSB
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 312.5f, ina228_rawToShuntuV(raw24_from_code20(1000), 0));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -312.5f, ina228_rawToShuntuV(raw24_from_code20(-1000), 0));
  // ADCRANGE=1: 0.078125 uV/LSB
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 78.125f, ina228_rawToShuntuV(raw24_from_code20(1000), 1));
}

void test_current_conversion_signed(void) {
  // CURRENT_LSB = 1 uA -> code 50000 = 50 mA
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.05f,  ina228_rawToCurrentA(raw24_from_code20(50000), 1e-6f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.05f, ina228_rawToCurrentA(raw24_from_code20(-50000), 1e-6f));
}

void test_die_temp_conversion(void) {
  // 7.8125 m degC/LSB. +25 C -> 3200, -10 C -> -1280.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 25.0f, ina228_rawToDieTempC(3200));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -10.0f, ina228_rawToDieTempC((uint16_t)(int16_t)-1280));
}

void test_power_conversion(void) {
  // Power = 3.2 * CURRENT_LSB * raw. 3.2 * 1e-6 * 100000 = 0.32 W
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.32f, ina228_rawToPowerW(100000, 1e-6f));
}

void test_energy_conversion(void) {
  // Energy = 16 * 3.2 * CURRENT_LSB * raw = 51.2 * 1e-6 * 1e6 = 51.2 J
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 51.2f, (float)ina228_rawToEnergyJ(1000000ULL, 1e-6f));
}

void test_charge_conversion_signed(void) {
  // Charge = CURRENT_LSB * raw. 1e-6 * 3.6e6 = 3.6 C (= 1 mAh)
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.6f,
      (float)ina228_rawToChargeC(3600000ULL, 1e-6f));
  // Negative accumulator (40-bit two's complement)
  uint64_t neg = ((uint64_t)(int64_t)(-3600000)) & 0xFFFFFFFFFFULL;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -3.6f,
      (float)ina228_rawToChargeC(neg, 1e-6f));
}

// ── Driver interface contract ────────────────────────────────────────

void test_ina228_driver_interface(void) {
  TEST_ASSERT_NOT_NULL(g_ina228Driver.readyFlag);
  TEST_ASSERT_NOT_NULL(g_ina228Driver.init);
  TEST_ASSERT_NOT_NULL(g_ina228Driver.probe);
  TEST_ASSERT_NOT_NULL(g_ina228Driver.read);
  TEST_ASSERT_EQUAL_STRING("INA228", g_ina228Driver.name);
  TEST_ASSERT_EQUAL_UINT32(SENSOR_REPROBE_I2C_MS, g_ina228Driver.reprobeIntervalMs);
}

void test_ina228_init_succeeds_when_present(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(ina228_init());
  TEST_ASSERT_TRUE(ina228_isReady());
}

void test_ina228_init_fails_when_absent(void) {
  g_wire_endTransmission_result = 2;
  TEST_ASSERT_FALSE(ina228_init());
  TEST_ASSERT_FALSE(ina228_isReady());
}

void test_ina228_read_does_nothing_when_not_ready(void) {
  SensorData data = {};
  data.powerBusV = 42.0f;          // sentinel
  ina228_read(&data);
  TEST_ASSERT_EQUAL_FLOAT(42.0f, data.powerBusV);
  TEST_ASSERT_FALSE(data.powerValid);
}

void test_ina228_read_sets_valid_when_ready(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(ina228_init());
  SensorData data = {};
  ina228_read(&data);
  TEST_ASSERT_TRUE(data.powerValid);
}

// ── Runtime reconfiguration (Level 1) ────────────────────────────────

void test_ina228_apply_runtime_config_keeps_ready(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(ina228_init());
  // Change shunt / max / range / cal-factor live — device stays ready.
  ina228_applyRuntimeConfig(1.0f, 0.041f, 1, 1.5f);
  TEST_ASSERT_TRUE(ina228_isReady());
}

void test_ina228_read_current_ma_zero_when_not_ready(void) {
  // After reset (setUp) the driver is not ready → 0 mA, no crash.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, ina228_readCurrentMa());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_current_lsb);
  RUN_TEST(test_shunt_cal_register);
  RUN_TEST(test_bus_voltage_conversion);
  RUN_TEST(test_shunt_voltage_conversion);
  RUN_TEST(test_current_conversion_signed);
  RUN_TEST(test_die_temp_conversion);
  RUN_TEST(test_power_conversion);
  RUN_TEST(test_energy_conversion);
  RUN_TEST(test_charge_conversion_signed);
  RUN_TEST(test_ina228_driver_interface);
  RUN_TEST(test_ina228_init_succeeds_when_present);
  RUN_TEST(test_ina228_init_fails_when_absent);
  RUN_TEST(test_ina228_read_does_nothing_when_not_ready);
  RUN_TEST(test_ina228_read_sets_valid_when_ready);
  RUN_TEST(test_ina228_apply_runtime_config_keeps_ready);
  RUN_TEST(test_ina228_read_current_ma_zero_when_not_ready);
  return UNITY_END();
}
