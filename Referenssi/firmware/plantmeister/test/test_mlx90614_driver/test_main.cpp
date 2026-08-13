#include <unity.h>
#include <math.h>

// Feature flags for MLX90614-only test surface
#define ENABLE_SENSORS true
#define HW_MLX90614    true
#define HW_BME280      false
#define HW_SCD41       false
#define HW_VL53L0X     false
#define HW_DS18B20     false
#define HW_ADS1115     false
#define HW_INA219      false
#define HW_AS7341      false

#define CAP_LEAF_TEMP true

#include "test/stubs/test_feature_flags.h"
#include "sensor_driver.h"
#include "sensor_driver_mlx90614.h"

void setUp(void) {
  mlx90614_resetForTest();
  g_wire_endTransmission_result = 0;
  g_stub_mlx_begin_result   = true;
  g_stub_mlx_object_temp_c  = 21.5f;
  g_stub_mlx_ambient_temp_c = 22.5f;
}

void tearDown(void) {}

// ── Driver interface contract ────────────────────────────────────────

void test_mlx90614_driver_interface(void) {
  TEST_ASSERT_NOT_NULL(g_mlx90614Driver.readyFlag);
  TEST_ASSERT_NOT_NULL(g_mlx90614Driver.init);
  TEST_ASSERT_NOT_NULL(g_mlx90614Driver.probe);
  TEST_ASSERT_NOT_NULL(g_mlx90614Driver.read);
  TEST_ASSERT_EQUAL_STRING("MLX90614", g_mlx90614Driver.name);
  TEST_ASSERT_EQUAL_UINT32(SENSOR_REPROBE_I2C_MS, g_mlx90614Driver.reprobeIntervalMs);
}

// ── Initialization ───────────────────────────────────────────────────

void test_mlx90614_init_succeeds_when_present(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(mlx90614_init());
  TEST_ASSERT_TRUE(mlx90614_isReady());
}

void test_mlx90614_init_fails_when_absent(void) {
  g_wire_endTransmission_result = 2;
  TEST_ASSERT_FALSE(mlx90614_init());
  TEST_ASSERT_FALSE(mlx90614_isReady());
}

void test_mlx90614_init_fails_when_begin_returns_false(void) {
  g_wire_endTransmission_result = 0;
  g_stub_mlx_begin_result = false;
  TEST_ASSERT_FALSE(mlx90614_init());
  TEST_ASSERT_FALSE(mlx90614_isReady());
}

// ── Reading ──────────────────────────────────────────────────────────

void test_mlx90614_read_fills_temp_when_ready(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(mlx90614_init());

  g_stub_mlx_object_temp_c  = 19.5f;
  g_stub_mlx_ambient_temp_c = 22.5f;

  SensorData data = {};
  mlx90614_read(&data);

  TEST_ASSERT_TRUE(data.leafTempValid);
  TEST_ASSERT_EQUAL_FLOAT(19.5f, data.leafTempC);
  TEST_ASSERT_EQUAL_FLOAT(22.5f, data.leafAmbientC);
}

void test_mlx90614_read_does_nothing_when_not_ready(void) {
  SensorData data = {};
  data.leafTempC = 99.0f;
  mlx90614_read(&data);
  TEST_ASSERT_EQUAL_FLOAT(99.0f, data.leafTempC);
  TEST_ASSERT_FALSE(data.leafTempValid);
}

void test_mlx90614_read_rejects_nan(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(mlx90614_init());

  g_stub_mlx_object_temp_c = NAN;
  g_stub_mlx_ambient_temp_c = 22.0f;

  SensorData data = {};
  mlx90614_read(&data);

  TEST_ASSERT_FALSE(data.leafTempValid);
}

void test_mlx90614_read_rejects_out_of_range(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(mlx90614_init());

  g_stub_mlx_object_temp_c = 200.0f;   // > 125 C cap
  g_stub_mlx_ambient_temp_c = 22.0f;

  SensorData data = {};
  mlx90614_read(&data);

  TEST_ASSERT_FALSE(data.leafTempValid);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_mlx90614_driver_interface);
  RUN_TEST(test_mlx90614_init_succeeds_when_present);
  RUN_TEST(test_mlx90614_init_fails_when_absent);
  RUN_TEST(test_mlx90614_init_fails_when_begin_returns_false);
  RUN_TEST(test_mlx90614_read_fills_temp_when_ready);
  RUN_TEST(test_mlx90614_read_does_nothing_when_not_ready);
  RUN_TEST(test_mlx90614_read_rejects_nan);
  RUN_TEST(test_mlx90614_read_rejects_out_of_range);
  return UNITY_END();
}
