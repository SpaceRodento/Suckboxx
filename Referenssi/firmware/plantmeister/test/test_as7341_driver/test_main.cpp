#include <unity.h>

// Feature flags for AS7341-only test surface
#define ENABLE_SENSORS true
#define HW_AS7341      true
#define HW_BME280      false
#define HW_SCD41       false
#define HW_VL53L0X     false
#define HW_DS18B20     false
#define HW_ADS1115     false
#define HW_INA219      false
#define HW_MLX90614    false

#define CAP_LIGHT_PAR      true
#define CAP_LIGHT_SPECTRUM true
#define ENABLE_AS7341_FLICKER true   // test the flicker path (default off in prod)

#include "test/stubs/test_feature_flags.h"
#include "sensor_driver.h"
#include "sensor_driver_as7341.h"

void setUp(void) {
  as7341_resetForTest();
  g_wire_endTransmission_result = 0;
  g_stub_as7341_begin_result   = true;
  g_stub_as7341_readAll_result = true;
  g_stub_as7341_flicker_hz     = 100;
  // Reset channel readings to a known set.
  g_stub_as7341_channels[0] = 100;
  g_stub_as7341_channels[1] = 200;
  g_stub_as7341_channels[2] = 300;
  g_stub_as7341_channels[3] = 400;
  g_stub_as7341_channels[4] = 500;
  g_stub_as7341_channels[5] = 600;
  g_stub_as7341_channels[6] = 700;
  g_stub_as7341_channels[7] = 800;
  g_stub_as7341_channels[8] = 1500;
  g_stub_as7341_channels[9] = 250;
}

void tearDown(void) {}

// ── Driver interface contract ────────────────────────────────────────

void test_as7341_driver_interface(void) {
  TEST_ASSERT_NOT_NULL(g_as7341Driver.readyFlag);
  TEST_ASSERT_NOT_NULL(g_as7341Driver.init);
  TEST_ASSERT_NOT_NULL(g_as7341Driver.probe);
  TEST_ASSERT_NOT_NULL(g_as7341Driver.read);
  TEST_ASSERT_EQUAL_STRING("AS7341", g_as7341Driver.name);
  TEST_ASSERT_EQUAL_UINT32(SENSOR_REPROBE_I2C_MS, g_as7341Driver.reprobeIntervalMs);
}

// ── Initialization ───────────────────────────────────────────────────

void test_as7341_init_succeeds_when_present(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(as7341_init());
  TEST_ASSERT_TRUE(as7341_isReady());
}

void test_as7341_init_fails_when_absent(void) {
  g_wire_endTransmission_result = 2;
  TEST_ASSERT_FALSE(as7341_init());
  TEST_ASSERT_FALSE(as7341_isReady());
}

void test_as7341_init_fails_when_begin_returns_false(void) {
  g_wire_endTransmission_result = 0;
  g_stub_as7341_begin_result = false;
  TEST_ASSERT_FALSE(as7341_init());
  TEST_ASSERT_FALSE(as7341_isReady());
}

// ── Reading ──────────────────────────────────────────────────────────

void test_as7341_read_fills_spectrum_when_ready(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(as7341_init());

  SensorData data = {};
  as7341_read(&data);

  TEST_ASSERT_TRUE(data.spectrumValid);
  TEST_ASSERT_EQUAL_UINT16(100,  data.lightSpectrum.f1_415nm);
  TEST_ASSERT_EQUAL_UINT16(200,  data.lightSpectrum.f2_445nm);
  TEST_ASSERT_EQUAL_UINT16(300,  data.lightSpectrum.f3_480nm);
  TEST_ASSERT_EQUAL_UINT16(400,  data.lightSpectrum.f4_515nm);
  TEST_ASSERT_EQUAL_UINT16(500,  data.lightSpectrum.f5_555nm);
  TEST_ASSERT_EQUAL_UINT16(600,  data.lightSpectrum.f6_590nm);
  TEST_ASSERT_EQUAL_UINT16(700,  data.lightSpectrum.f7_630nm);
  TEST_ASSERT_EQUAL_UINT16(800,  data.lightSpectrum.f8_680nm);
  TEST_ASSERT_EQUAL_UINT16(1500, data.lightSpectrum.clear);
  TEST_ASSERT_EQUAL_UINT16(250,  data.lightSpectrum.nir);
  TEST_ASSERT_EQUAL_UINT16(100,  data.lightSpectrum.flickerHz);
  // Default AS7341_GAIN is AS7341_GAIN_256X (index 9) -> 0.5*2^9 = 256.
  // Regression guard: this used to be hardcoded regardless of AS7341_GAIN.
  TEST_ASSERT_EQUAL_UINT16(256,  data.lightSpectrum.gain);
}

void test_as7341_read_does_nothing_when_not_ready(void) {
  SensorData data = {};
  data.lightSpectrum.f1_415nm = 9999;     // sentinel
  as7341_read(&data);
  TEST_ASSERT_EQUAL_UINT16(9999, data.lightSpectrum.f1_415nm);
  TEST_ASSERT_FALSE(data.spectrumValid);
}

void test_as7341_read_uses_cache_when_data_not_ready(void) {
  g_wire_endTransmission_result = 0;
  TEST_ASSERT_TRUE(as7341_init());

  // First read: stub returns fresh data, cache fills.
  g_stub_as7341_readAll_result = true;
  g_stub_as7341_channels[0] = 111;
  SensorData first = {};
  as7341_read(&first);
  TEST_ASSERT_TRUE(first.spectrumValid);
  TEST_ASSERT_EQUAL_UINT16(111, first.lightSpectrum.f1_415nm);

  // Second read: stub returns "not ready" → driver should serve cached values.
  g_stub_as7341_readAll_result = false;
  SensorData second = {};
  as7341_read(&second);
  TEST_ASSERT_TRUE(second.spectrumValid);
  TEST_ASSERT_EQUAL_UINT16(111, second.lightSpectrum.f1_415nm);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_as7341_driver_interface);
  RUN_TEST(test_as7341_init_succeeds_when_present);
  RUN_TEST(test_as7341_init_fails_when_absent);
  RUN_TEST(test_as7341_init_fails_when_begin_returns_false);
  RUN_TEST(test_as7341_read_fills_spectrum_when_ready);
  RUN_TEST(test_as7341_read_does_nothing_when_not_ready);
  RUN_TEST(test_as7341_read_uses_cache_when_data_not_ready);
  return UNITY_END();
}
