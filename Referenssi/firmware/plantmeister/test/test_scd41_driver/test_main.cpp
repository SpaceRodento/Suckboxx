#include <unity.h>

// Feature flags for SCD41-only test surface
#define ENABLE_SENSORS true
#define HW_SCD41       true
#define HW_BME280      false
#define HW_VL53L0X     false
#define HW_DS18B20     false
#define HW_ADS1115     false
#define HW_INA219      false

// Capability flags (resolver requires at least one of these consistent with HW_*)
#define CAP_AIR_TEMP_HUMIDITY true
#define CAP_AIR_CO2           true

#include "test/stubs/test_feature_flags.h"

// Stub globals — must be defined before SCD41 driver is included so its
// extern declarations resolve.
bool     g_scd41_stub_dataReady   = false;
uint16_t g_scd41_stub_co2         = 800;
float    g_scd41_stub_tempC       = 22.0f;
float    g_scd41_stub_humidity    = 50.0f;
uint16_t g_scd41_stub_readErr     = 0;
uint16_t g_scd41_stub_dataReadyErr = 0;
uint16_t g_scd41_stub_startErr    = 0;

#include "sensor_driver.h"
#include "sensor_driver_scd41.h"

void setUp(void) {
  scd41_resetForTest();
  g_wire_endTransmission_result = 0;
  g_scd41_stub_dataReady = false;
  g_scd41_stub_co2 = 800;
  g_scd41_stub_tempC = 22.0f;
  g_scd41_stub_humidity = 50.0f;
  g_scd41_stub_readErr = 0;
  g_scd41_stub_dataReadyErr = 0;
  g_scd41_stub_startErr = 0;
}

void tearDown(void) {}

// ───── Driver interface contract ─────────────────────────────────────

void test_scd41_driver_interface(void) {
  TEST_ASSERT_NOT_NULL(g_scd41Driver.readyFlag);
  TEST_ASSERT_NOT_NULL(g_scd41Driver.init);
  TEST_ASSERT_NOT_NULL(g_scd41Driver.probe);
  TEST_ASSERT_NOT_NULL(g_scd41Driver.read);
  TEST_ASSERT_EQUAL_STRING("SCD41", g_scd41Driver.name);
  TEST_ASSERT_EQUAL_UINT32(SENSOR_REPROBE_I2C_MS, g_scd41Driver.reprobeIntervalMs);
}

// ───── Initialization ────────────────────────────────────────────────

void test_scd41_init_succeeds_when_present(void) {
  g_wire_endTransmission_result = 0;   // ACK
  bool ok = scd41_init();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(scd41_isReady());
}

void test_scd41_init_fails_when_absent(void) {
  g_wire_endTransmission_result = 2;   // NACK
  bool ok = scd41_init();
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_FALSE(scd41_isReady());
}

void test_scd41_init_fails_when_startMeasurement_errors(void) {
  g_wire_endTransmission_result = 0;
  g_scd41_stub_startErr = 1;   // Sensirion API non-zero = error
  bool ok = scd41_init();
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_FALSE(scd41_isReady());
}

// ───── Reading ───────────────────────────────────────────────────────

void test_scd41_read_fills_data_when_ready(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(scd41_init());

  g_scd41_stub_dataReady = true;
  g_scd41_stub_co2 = 800;
  g_scd41_stub_tempC = 22.5f;
  g_scd41_stub_humidity = 55.0f;

  SensorData data = {};
  scd41_read(&data);

  TEST_ASSERT_EQUAL_INT(800, data.airCO2Ppm);
  TEST_ASSERT_TRUE(data.airCO2Valid);
  TEST_ASSERT_EQUAL_FLOAT(22.5f, data.airTempC);
  TEST_ASSERT_EQUAL_FLOAT(55.0f, data.airHumidity);
  TEST_ASSERT_TRUE(data.envValid);
}

void test_scd41_read_skips_when_data_not_ready(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(scd41_init());

  g_scd41_stub_dataReady = false;   // not ready

  SensorData data = {};
  data.airCO2Ppm = 1234;       // sentinel
  data.airTempC = -99.0f;
  data.airHumidity = -99.0f;
  data.airCO2Valid = false;
  data.envValid = false;

  scd41_read(&data);

  TEST_ASSERT_EQUAL_INT(1234, data.airCO2Ppm);   // unchanged
  TEST_ASSERT_FALSE(data.airCO2Valid);
  TEST_ASSERT_FALSE(data.envValid);
}

void test_scd41_read_rejects_zero_co2(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(scd41_init());

  g_scd41_stub_dataReady = true;
  g_scd41_stub_co2 = 0;   // bogus
  g_scd41_stub_tempC = 22.0f;
  g_scd41_stub_humidity = 50.0f;

  SensorData data = {};
  scd41_read(&data);

  TEST_ASSERT_FALSE(data.airCO2Valid);
  TEST_ASSERT_FALSE(data.envValid);
}

void test_scd41_read_does_nothing_when_not_ready(void) {
  // No init called — driver should never touch SensorData
  SensorData data = {};
  data.airCO2Ppm = 7777;
  scd41_read(&data);
  TEST_ASSERT_EQUAL_INT(7777, data.airCO2Ppm);
  TEST_ASSERT_FALSE(data.airCO2Valid);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_scd41_driver_interface);
  RUN_TEST(test_scd41_init_succeeds_when_present);
  RUN_TEST(test_scd41_init_fails_when_absent);
  RUN_TEST(test_scd41_init_fails_when_startMeasurement_errors);
  RUN_TEST(test_scd41_read_fills_data_when_ready);
  RUN_TEST(test_scd41_read_skips_when_data_not_ready);
  RUN_TEST(test_scd41_read_rejects_zero_co2);
  RUN_TEST(test_scd41_read_does_nothing_when_not_ready);
  return UNITY_END();
}
