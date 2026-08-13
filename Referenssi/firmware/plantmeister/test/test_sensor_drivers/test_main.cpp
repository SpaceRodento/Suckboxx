#include <unity.h>

// Feature flags for tests
#define ENABLE_SENSORS true
#define ENABLE_HEIGHT_SENSOR true
#define ENABLE_ENV_SENSOR true
#define ENABLE_TDS_SENSOR true
#define ENABLE_WATER_TEMP true
#define ENABLE_BATTERY_MONITOR true

#include "test/stubs/test_feature_flags.h"

#include "sensor_driver.h"
#include "sensor_driver_vl53l0x.h"
#include "sensor_driver_bme280.h"
#include "sensor_driver_ads1115.h"
#include "sensor_driver_ds18b20.h"
#include "sensor_driver_ina219.h"

void setUp(void) {}
void tearDown(void) {}

static void reset_sensor_driver_state() {
  g_wire_endTransmission_result = 0;
  g_ds18b20_device_count = 1;
  g_ds18b20_getTemp_result = 24.0f;
  bme280_resetForTest();
  ads1115_resetForTest();
  ina219_resetForTest();
  vl53l0x_resetForTest();
  ds18b20_resetForTest();
}

// ───── Driver v2 interface contract ─────────────────────────────────
// Every driver must expose readyFlag pointer + reprobeIntervalMs so the
// manager can orchestrate without per-driver special cases.

void test_driver_interface_has_readyFlag_pointers() {
  TEST_ASSERT_NOT_NULL(g_vl53l0xDriver.readyFlag);
  TEST_ASSERT_NOT_NULL(g_bme280Driver.readyFlag);
  TEST_ASSERT_NOT_NULL(g_ads1115Driver.readyFlag);
  TEST_ASSERT_NOT_NULL(g_ds18b20Driver.readyFlag);
  TEST_ASSERT_NOT_NULL(g_ina219Driver.readyFlag);
}

void test_driver_interface_has_reprobe_intervals() {
  // I2C drivers share the same interval
  TEST_ASSERT_EQUAL_UINT32(SENSOR_REPROBE_I2C_MS, g_vl53l0xDriver.reprobeIntervalMs);
  TEST_ASSERT_EQUAL_UINT32(SENSOR_REPROBE_I2C_MS, g_bme280Driver.reprobeIntervalMs);
  TEST_ASSERT_EQUAL_UINT32(SENSOR_REPROBE_I2C_MS, g_ads1115Driver.reprobeIntervalMs);
  TEST_ASSERT_EQUAL_UINT32(SENSOR_REPROBE_I2C_MS, g_ina219Driver.reprobeIntervalMs);
  // OneWire driver uses longer interval
  TEST_ASSERT_EQUAL_UINT32(SENSOR_REPROBE_ONEWIRE_MS, g_ds18b20Driver.reprobeIntervalMs);
}

void test_driver_readyFlag_reflects_state() {
  reset_sensor_driver_state();
  // After reset, all readyFlags should be false
  TEST_ASSERT_FALSE(*g_vl53l0xDriver.readyFlag);
  TEST_ASSERT_FALSE(*g_bme280Driver.readyFlag);
  TEST_ASSERT_FALSE(*g_ads1115Driver.readyFlag);
  TEST_ASSERT_FALSE(*g_ds18b20Driver.readyFlag);
  TEST_ASSERT_FALSE(*g_ina219Driver.readyFlag);

  // After successful init, flags should be true
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(g_vl53l0xDriver.init());
  TEST_ASSERT_TRUE(*g_vl53l0xDriver.readyFlag);
  TEST_ASSERT_TRUE(g_bme280Driver.init());
  TEST_ASSERT_TRUE(*g_bme280Driver.readyFlag);
}

// ───── Happy-path init + probe + read ───────────────────────────────

void test_drivers_init_and_read() {
  reset_sensor_driver_state();
  SensorData d{};
  // Initialize drivers
  TEST_ASSERT_TRUE(g_vl53l0xDriver.init());
  TEST_ASSERT_TRUE(g_bme280Driver.init());
  TEST_ASSERT_TRUE(g_ads1115Driver.init());
  TEST_ASSERT_TRUE(g_ds18b20Driver.init());
  TEST_ASSERT_TRUE(g_ina219Driver.init());

  // Probe and read
  TEST_ASSERT_EQUAL(SENSOR_PROBE_OK, g_vl53l0xDriver.probe());
  g_vl53l0xDriver.read(&d);
  TEST_ASSERT_TRUE(d.heightValid);

  TEST_ASSERT_EQUAL(SENSOR_PROBE_OK, g_bme280Driver.probe());
  g_bme280Driver.read(&d);
  TEST_ASSERT_TRUE(d.envValid);

  TEST_ASSERT_EQUAL(SENSOR_PROBE_OK, g_ads1115Driver.probe());
  g_ads1115Driver.read(&d);
  TEST_ASSERT_TRUE(d.tdsValid);

  TEST_ASSERT_EQUAL(SENSOR_PROBE_OK, g_ds18b20Driver.probe());
  g_ds18b20Driver.read(&d);
  TEST_ASSERT_TRUE(d.waterTempValid);

  // INA219 sets batteryVoltage
  TEST_ASSERT_EQUAL(SENSOR_PROBE_OK, g_ina219Driver.probe());
  g_ina219Driver.read(&d);
  TEST_ASSERT_TRUE(d.batteryVoltage > 0.0f);
}

// ───── Presence-failure per I2C driver ──────────────────────────────

void test_bme280_init_fails_when_absent() {
  reset_sensor_driver_state();
  g_wire_endTransmission_result = 2;  // NACK
  TEST_ASSERT_FALSE(g_bme280Driver.init());
  TEST_ASSERT_FALSE(*g_bme280Driver.readyFlag);
}

void test_bme280_init_ok_when_present() {
  reset_sensor_driver_state();
  g_wire_endTransmission_result = 0;  // ACK
  TEST_ASSERT_TRUE(g_bme280Driver.init());
  TEST_ASSERT_TRUE(*g_bme280Driver.readyFlag);
}

void test_ads1115_init_fails_when_absent() {
  reset_sensor_driver_state();
  g_wire_endTransmission_result = 2;
  TEST_ASSERT_FALSE(g_ads1115Driver.init());
  TEST_ASSERT_FALSE(*g_ads1115Driver.readyFlag);
}

void test_ads1115_init_ok_when_present() {
  reset_sensor_driver_state();
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(g_ads1115Driver.init());
  TEST_ASSERT_TRUE(*g_ads1115Driver.readyFlag);
}

void test_ina219_init_fails_when_absent() {
  reset_sensor_driver_state();
  g_wire_endTransmission_result = 2;
  TEST_ASSERT_FALSE(g_ina219Driver.init());
  TEST_ASSERT_FALSE(*g_ina219Driver.readyFlag);
}

void test_ina219_init_ok_when_present() {
  reset_sensor_driver_state();
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(g_ina219Driver.init());
  TEST_ASSERT_TRUE(*g_ina219Driver.readyFlag);
}

void test_vl53l0x_init_fails_when_absent() {
  reset_sensor_driver_state();
  g_wire_endTransmission_result = 2;
  TEST_ASSERT_FALSE(g_vl53l0xDriver.init());
  TEST_ASSERT_FALSE(*g_vl53l0xDriver.readyFlag);
}

void test_vl53l0x_init_ok_when_present() {
  reset_sensor_driver_state();
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(g_vl53l0xDriver.init());
  TEST_ASSERT_TRUE(*g_vl53l0xDriver.readyFlag);
}

// ───── I2C probe returns ABSENT when bus NACKs ──────────────────────

void test_i2c_probe_returns_absent_when_no_ack() {
  reset_sensor_driver_state();
  g_wire_endTransmission_result = 2;  // NACK
  TEST_ASSERT_EQUAL(SENSOR_PROBE_ABSENT, g_bme280Driver.probe());
  TEST_ASSERT_EQUAL(SENSOR_PROBE_ABSENT, g_ads1115Driver.probe());
  TEST_ASSERT_EQUAL(SENSOR_PROBE_ABSENT, g_ina219Driver.probe());
  TEST_ASSERT_EQUAL(SENSOR_PROBE_ABSENT, g_vl53l0xDriver.probe());
}

// ───── DS18B20 OneWire probe semantics ──────────────────────────────

void test_ds18b20_init_fails_when_no_device() {
  reset_sensor_driver_state();
  g_ds18b20_device_count = 0;
  TEST_ASSERT_FALSE(g_ds18b20Driver.init());
  TEST_ASSERT_FALSE(*g_ds18b20Driver.readyFlag);
}

void test_ds18b20_init_ok_when_device_present() {
  reset_sensor_driver_state();
  g_ds18b20_device_count = 1;
  TEST_ASSERT_TRUE(g_ds18b20Driver.init());
  TEST_ASSERT_TRUE(*g_ds18b20Driver.readyFlag);
}

void test_ds18b20_probe_reports_presence() {
  reset_sensor_driver_state();
  g_ds18b20_device_count = 1;
  TEST_ASSERT_EQUAL(SENSOR_PROBE_OK, g_ds18b20Driver.probe());
  g_ds18b20_device_count = 0;
  TEST_ASSERT_EQUAL(SENSOR_PROBE_ABSENT, g_ds18b20Driver.probe());
}

void test_ds18b20_read_does_not_mutate_readyFlag() {
  // Behavioural shift in v2: driver no longer clears its own readyFlag in
  // read(); the orchestrator owns runtime-degradation via probe().
  reset_sensor_driver_state();
  g_ds18b20_device_count = 1;
  g_ds18b20_getTemp_result = 24.0f;
  TEST_ASSERT_TRUE(g_ds18b20Driver.init());

  // Even when device "disappears", read() must not flip the flag.
  g_ds18b20_device_count = 0;
  g_ds18b20_getTemp_result = DEVICE_DISCONNECTED_C;
  SensorData d{};
  g_ds18b20Driver.read(&d);
  TEST_ASSERT_TRUE(*g_ds18b20Driver.readyFlag);
  TEST_ASSERT_FALSE(d.waterTempValid);
}

// ───── BME280 read should clamp invalid sensor responses ────────────

void test_bme280_read_invalid_marks_envInvalid() {
  // We can only stub one return value from the Adafruit library stub;
  // smoke-checks that the driver respects readyFlag and writes nothing
  // when flag is clear.
  reset_sensor_driver_state();
  // readyFlag is false (no init)
  SensorData d{};
  d.envValid = false;
  g_bme280Driver.read(&d);
  TEST_ASSERT_FALSE(d.envValid);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  // Interface contract
  RUN_TEST(test_driver_interface_has_readyFlag_pointers);
  RUN_TEST(test_driver_interface_has_reprobe_intervals);
  RUN_TEST(test_driver_readyFlag_reflects_state);

  // Happy path
  RUN_TEST(test_drivers_init_and_read);

  // Per-driver presence / absence
  RUN_TEST(test_bme280_init_fails_when_absent);
  RUN_TEST(test_bme280_init_ok_when_present);
  RUN_TEST(test_ads1115_init_fails_when_absent);
  RUN_TEST(test_ads1115_init_ok_when_present);
  RUN_TEST(test_ina219_init_fails_when_absent);
  RUN_TEST(test_ina219_init_ok_when_present);
  RUN_TEST(test_vl53l0x_init_fails_when_absent);
  RUN_TEST(test_vl53l0x_init_ok_when_present);
  RUN_TEST(test_i2c_probe_returns_absent_when_no_ack);

  // DS18B20 OneWire
  RUN_TEST(test_ds18b20_init_fails_when_no_device);
  RUN_TEST(test_ds18b20_init_ok_when_device_present);
  RUN_TEST(test_ds18b20_probe_reports_presence);
  RUN_TEST(test_ds18b20_read_does_not_mutate_readyFlag);

  // Read-guard contracts
  RUN_TEST(test_bme280_read_invalid_marks_envInvalid);

  return UNITY_END();
}
