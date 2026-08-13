#include <unity.h>

#include <string.h>

#define ENABLE_EINK_DISPLAY true
#include "test_feature_flags.h"

#include "eink_display.h"
#include "plant_lookup.h"

static int g_backendInitCalls = 0;
static int g_backendUpdateCalls = 0;
static bool g_backendInitResult = true;
static bool g_backendUpdateResult = true;
static EinkFrameData g_backendLastFrame = {};

static bool fakeEinkBackendInit() {
  g_backendInitCalls++;
  return g_backendInitResult;
}

static bool fakeEinkBackendUpdate(const EinkFrameData* frame) {
  g_backendUpdateCalls++;
  if (!frame) return false;
  g_backendLastFrame = *frame;
  return g_backendUpdateResult;
}

static const EinkBackendOps g_fakeBackend = {
  fakeEinkBackendInit,
  fakeEinkBackendUpdate,
};

void setUp() {
  plants_loadDefaults();
  g_backendInitCalls = 0;
  g_backendUpdateCalls = 0;
  g_backendInitResult = true;
  g_backendUpdateResult = true;
  memset(&g_backendLastFrame, 0, sizeof(g_backendLastFrame));
  eink_setBackend(NULL);
  eink_clearLastFrame();
}

void tearDown() {}

void test_should_update_respects_interval() {
  unsigned long last_update = 0;

  TEST_ASSERT_TRUE(eink_shouldUpdate(1000, &last_update));
  TEST_ASSERT_EQUAL_UINT32(1000, last_update);

  TEST_ASSERT_FALSE(eink_shouldUpdate(1000 + EINK_UPDATE_INTERVAL_MS - 1, &last_update));
  TEST_ASSERT_EQUAL_UINT32(1000, last_update);

  TEST_ASSERT_TRUE(eink_shouldUpdate(1000 + EINK_UPDATE_INTERVAL_MS, &last_update));
  TEST_ASSERT_EQUAL_UINT32(1000 + EINK_UPDATE_INTERVAL_MS, last_update);
}

void test_build_frame_copies_runtime_values() {
  SensorData sensors = {};
  sensors.airTempC = 22.5f;
  sensors.airHumidity = 65.0f;
  sensors.waterTempC = 20.1f;
  sensors.tdsPpm = 640;
  sensors.plantHeightMm = 155;
  sensors.batteryPercent = 84;
  sensors.waterLevelOk = true;

  SystemState state = {};
  state.lightsOn = true;
  state.airPumpOn = false;

  DeviceConfig cfg = {};
  cfg.motorCurrentMm = 188;
  strncpy(cfg.currentPlantId, "basil", sizeof(cfg.currentPlantId) - 1);
  cfg.currentPlantId[sizeof(cfg.currentPlantId) - 1] = '\0';

  PlantConfig* plant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(plant);

  EinkFrameData frame = {};
  eink_buildFrameData(&sensors, &state, &cfg, plant, &frame);

  TEST_ASSERT_EQUAL_FLOAT(22.5f, frame.airTempC);
  TEST_ASSERT_EQUAL_FLOAT(65.0f, frame.airHumidity);
  TEST_ASSERT_EQUAL_FLOAT(20.1f, frame.waterTempC);
  TEST_ASSERT_EQUAL_INT(640, frame.tdsPpm);
  TEST_ASSERT_EQUAL_INT(155, frame.plantHeightMm);
  TEST_ASSERT_EQUAL_INT(188, frame.motorHeightMm);
  TEST_ASSERT_EQUAL_INT(84, frame.batteryPercent);
  TEST_ASSERT_TRUE(frame.lightsOn);
  TEST_ASSERT_FALSE(frame.airPumpOn);
  TEST_ASSERT_TRUE(frame.waterLevelOk);
  TEST_ASSERT_EQUAL_STRING("basil", frame.plantId);
  TEST_ASSERT_EQUAL_STRING("Basilika", frame.plantName);
}

void test_build_frame_sets_grow_phase_details() {
  PlantConfig* plant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(plant);
  TEST_ASSERT_TRUE(plant->phaseCount > 1);

  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  cfg.growActive = true;
  cfg.growPhase = 1;
  cfg.growElapsedDays = 4;

  EinkFrameData frame = {};
  eink_buildFrameData(&sensors, &state, &cfg, plant, &frame);

  TEST_ASSERT_TRUE(frame.growActive);
  TEST_ASSERT_EQUAL_UINT8(1, frame.growPhase);
  TEST_ASSERT_EQUAL_UINT16(4, frame.growElapsedDays);
  TEST_ASSERT_TRUE(strlen(frame.phaseLabel) > 0);
  TEST_ASSERT_TRUE(strlen(frame.phaseGuidance) > 0);
}

void test_build_frame_clamps_out_of_range_phase() {
  PlantConfig* plant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(plant);

  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  cfg.growActive = true;
  cfg.growPhase = 250;

  EinkFrameData frame = {};
  eink_buildFrameData(&sensors, &state, &cfg, plant, &frame);

  TEST_ASSERT_EQUAL_UINT8((uint8_t)(plant->phaseCount - 1), frame.growPhase);
}

void test_build_frame_falls_back_to_label_when_guidance_missing() {
  PlantConfig plant = {};
  strncpy(plant.id, "test", sizeof(plant.id) - 1);
  strncpy(plant.name, "Testi", sizeof(plant.name) - 1);
  plant.phaseCount = 1;
  strncpy(plant.phases[0].label, "Vaihe A", sizeof(plant.phases[0].label) - 1);
  plant.phases[0].guidance[0] = '\0';

  SensorData sensors = {};
  SystemState state = {};
  DeviceConfig cfg = {};
  cfg.growActive = true;
  cfg.growPhase = 0;

  EinkFrameData frame = {};
  eink_buildFrameData(&sensors, &state, &cfg, &plant, &frame);

  TEST_ASSERT_EQUAL_STRING("Vaihe A", frame.phaseGuidance);
}

void test_init_delegates_to_registered_backend() {
  eink_setBackend(&g_fakeBackend);

  TEST_ASSERT_TRUE(eink_init());
  TEST_ASSERT_EQUAL_INT(1, g_backendInitCalls);
}

void test_init_returns_backend_failure() {
  g_backendInitResult = false;
  eink_setBackend(&g_fakeBackend);

  TEST_ASSERT_FALSE(eink_init());
  TEST_ASSERT_EQUAL_INT(1, g_backendInitCalls);
}

void test_update_from_runtime_calls_backend_and_saves_frame() {
  PlantConfig* plant = plants_getById("basil");
  TEST_ASSERT_NOT_NULL(plant);

  SensorData sensors = {};
  sensors.airTempC = 21.2f;
  sensors.airHumidity = 58.0f;
  sensors.plantHeightMm = 99;

  SystemState state = {};
  state.lightsOn = true;

  DeviceConfig cfg = {};
  cfg.growActive = true;
  cfg.growPhase = 0;
  strncpy(cfg.currentPlantId, "basil", sizeof(cfg.currentPlantId) - 1);
  cfg.currentPlantId[sizeof(cfg.currentPlantId) - 1] = '\0';

  eink_setBackend(&g_fakeBackend);
  TEST_ASSERT_TRUE(eink_updateFromRuntime(&sensors, &state, &cfg, plant));
  TEST_ASSERT_EQUAL_INT(1, g_backendUpdateCalls);

  TEST_ASSERT_EQUAL_FLOAT(21.2f, g_backendLastFrame.airTempC);
  TEST_ASSERT_EQUAL_FLOAT(58.0f, g_backendLastFrame.airHumidity);
  TEST_ASSERT_EQUAL_INT(99, g_backendLastFrame.plantHeightMm);
  TEST_ASSERT_TRUE(g_backendLastFrame.lightsOn);
  TEST_ASSERT_EQUAL_STRING("Basilika", g_backendLastFrame.plantName);

  EinkFrameData cached = {};
  TEST_ASSERT_TRUE(eink_getLastFrame(&cached));
  TEST_ASSERT_EQUAL_FLOAT(21.2f, cached.airTempC);
  TEST_ASSERT_EQUAL_STRING("basil", cached.plantId);
}

void test_update_frame_returns_backend_failure() {
  g_backendUpdateResult = false;
  eink_setBackend(&g_fakeBackend);

  EinkFrameData frame = {};
  TEST_ASSERT_FALSE(eink_updateFrame(&frame));
  TEST_ASSERT_EQUAL_INT(1, g_backendUpdateCalls);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_should_update_respects_interval);
  RUN_TEST(test_build_frame_copies_runtime_values);
  RUN_TEST(test_build_frame_sets_grow_phase_details);
  RUN_TEST(test_build_frame_clamps_out_of_range_phase);
  RUN_TEST(test_build_frame_falls_back_to_label_when_guidance_missing);
  RUN_TEST(test_init_delegates_to_registered_backend);
  RUN_TEST(test_init_returns_backend_failure);
  RUN_TEST(test_update_from_runtime_calls_backend_and_saves_frame);
  RUN_TEST(test_update_frame_returns_backend_failure);
  return UNITY_END();
}
