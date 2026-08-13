#include <unity.h>

#include <string.h>

#define ENABLE_FLOAT_SWITCH true
#include "test_feature_flags.h"

#include "lora_payload.h"

void setUp() {}
void tearDown() {}

static SensorData make_sensor_data() {
  SensorData data = {};
  data.envValid = true;
  data.airTempC = 22.5f;
  data.airHumidity = 65.0f;
  data.airPressureHpa = 1013.0f;
  data.waterTempValid = true;
  data.waterTempC = 21.0f;
  data.tdsValid = true;
  data.tdsPpm = 650;
  data.heightValid = true;
  data.plantHeightMm = 150;
  data.waterLevelOk = true;
  data.batteryVoltage = 4.1f;
  data.batteryPercent = 85;
  return data;
}

static SystemState make_state() {
  SystemState state = {};
  state.lightsOn = true;
  state.airPumpOn = true;
  return state;
}

static DeviceConfig make_config(bool growActive) {
  DeviceConfig cfg = {};
  cfg.motorCurrentMm = 200;
  cfg.growActive = growActive;
  cfg.growPhase = 2;
  cfg.growElapsedDays = 14;
  strncpy(cfg.currentPlantId, "basil", sizeof(cfg.currentPlantId) - 1);
  cfg.currentPlantId[sizeof(cfg.currentPlantId) - 1] = '\0';
  return cfg;
}

void test_payload_includes_gp_gd_when_grow_active() {
  SensorData data = make_sensor_data();
  SystemState state = make_state();
  DeviceConfig cfg = make_config(true);
  char payload[LORA_MAX_PAYLOAD] = {0};

  lora_buildReportPayload(&data, &state, &cfg, payload, sizeof(payload));

  TEST_ASSERT_NOT_NULL(strstr(payload, "GP=2"));
  TEST_ASSERT_NOT_NULL(strstr(payload, "GD=14"));
}

void test_payload_omits_gp_gd_when_grow_inactive() {
  SensorData data = make_sensor_data();
  SystemState state = make_state();
  DeviceConfig cfg = make_config(false);
  char payload[LORA_MAX_PAYLOAD] = {0};

  lora_buildReportPayload(&data, &state, &cfg, payload, sizeof(payload));

  TEST_ASSERT_NULL(strstr(payload, "GP="));
  TEST_ASSERT_NULL(strstr(payload, "GD="));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_payload_includes_gp_gd_when_grow_active);
  RUN_TEST(test_payload_omits_gp_gd_when_grow_inactive);
  return UNITY_END();
}
