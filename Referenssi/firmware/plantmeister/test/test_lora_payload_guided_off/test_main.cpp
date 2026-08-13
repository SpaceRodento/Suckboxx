#include <unity.h>

#include <string.h>

#define ENABLE_GUIDED_GROWING false
#define ENABLE_FLOAT_SWITCH true
#include "test_feature_flags.h"

#include "lora_payload.h"

void setUp() {}
void tearDown() {}

void test_payload_has_no_gp_or_gd_when_guided_growing_disabled() {
  SensorData data = {};
  data.envValid = true;
  data.airTempC = 21.0f;
  data.airHumidity = 60.0f;
  data.airPressureHpa = 1010.0f;
  data.waterLevelOk = true;
  data.batteryVoltage = 4.0f;
  data.batteryPercent = 80;

  SystemState state = {};
  state.lightsOn = true;
  state.airPumpOn = false;

  DeviceConfig cfg = {};
  cfg.growActive = true;
  cfg.growPhase = 3;
  cfg.growElapsedDays = 99;
  cfg.motorCurrentMm = 100;
  strncpy(cfg.currentPlantId, "mint", sizeof(cfg.currentPlantId) - 1);
  cfg.currentPlantId[sizeof(cfg.currentPlantId) - 1] = '\0';

  char payload[LORA_MAX_PAYLOAD] = {0};
  lora_buildReportPayload(&data, &state, &cfg, payload, sizeof(payload));

  TEST_ASSERT_NULL(strstr(payload, "GP="));
  TEST_ASSERT_NULL(strstr(payload, "GD="));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_payload_has_no_gp_or_gd_when_guided_growing_disabled);
  return UNITY_END();
}
