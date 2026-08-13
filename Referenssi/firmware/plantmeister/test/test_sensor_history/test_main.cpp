#include <unity.h>

// Feature flags BEFORE includes.
#define ENABLE_SENSOR_HISTORY true
#include "test/stubs/test_feature_flags.h"

// LittleFS stub (must come before sensor_history.h which includes <LittleFS.h>)
#include "test/stubs/LittleFS.h"

#include "config.h"
#include "structs.h"
#include "sensor_history.h"

void setUp(void) {
  LittleFS.littlefs_clear();
  history_resetForTest();
}

void tearDown(void) {}

static SensorData makeSample(unsigned long ts, float temp) {
  SensorData s = {};
  s.timestamp = ts;
  s.airTempC = temp;
  s.airHumidity = 55.0f;
  s.plantHeightMm = 120;
  s.batteryVoltage = 4.1f;
  s.batteryPercent = 87;
  s.loraRssi = -62;
  return s;
}

// ── Init ─────────────────────────────────────────────────────────────

void test_init_with_no_file_starts_empty(void) {
  history_init();
  TEST_ASSERT_EQUAL_INT(0, history_getCount());
}

void test_init_is_idempotent(void) {
  history_init();
  SensorData s = makeSample(1000, 20.0f);
  history_addReading(&s);
  history_init();  // second call must not wipe state
  TEST_ASSERT_EQUAL_INT(1, history_getCount());
}

// ── Adding readings / ring buffer ────────────────────────────────────

void test_add_reading_before_init_is_noop(void) {
  SensorData s = makeSample(1000, 20.0f);
  history_addReading(&s);
  TEST_ASSERT_EQUAL_INT(0, history_getCount());
}

void test_add_reading_null_pointer_is_noop(void) {
  history_init();
  history_addReading(nullptr);
  TEST_ASSERT_EQUAL_INT(0, history_getCount());
}

void test_add_reading_increments_count(void) {
  history_init();
  SensorData s = makeSample(1000, 20.0f);
  history_addReading(&s);
  history_addReading(&s);
  TEST_ASSERT_EQUAL_INT(2, history_getCount());
}

void test_count_caps_at_max_entries_on_wraparound(void) {
  history_init();
  for (int i = 0; i < HISTORY_MAX_ENTRIES + 10; i++) {
    SensorData s = makeSample(1000 + i, 20.0f + i);
    history_addReading(&s);
  }
  TEST_ASSERT_EQUAL_INT(HISTORY_MAX_ENTRIES, history_getCount());
}

void test_wraparound_keeps_chronological_order(void) {
  history_init();
  // Fill buffer completely, then overwrite the first 5 slots.
  for (int i = 0; i < HISTORY_MAX_ENTRIES + 5; i++) {
    SensorData s = makeSample(1000 + i, (float)i);
    history_addReading(&s);
  }

  // 240 full entries serialize to ~23 KB - a small buffer here would just
  // reproduce the same silent-truncation failure mode this test is guarding
  // against, but as a test bug instead of a production one.
  static char buf[65536];
  TEST_ASSERT_TRUE(history_getAsJson(buf, sizeof(buf), HISTORY_MAX_ENTRIES));

  DynamicJsonDocument doc(65536);
  DeserializationError err = deserializeJson(doc, buf);
  TEST_ASSERT_FALSE(err);

  JsonArray arr = doc.as<JsonArray>();
  TEST_ASSERT_EQUAL_INT(HISTORY_MAX_ENTRIES, (int)arr.size());
  // Oldest surviving reading is index 5 (0..4 were overwritten by wraparound).
  TEST_ASSERT_EQUAL_UINT32(1005, arr[0]["timestamp"].as<unsigned long>());
  // Newest reading is the very last one added.
  TEST_ASSERT_EQUAL_UINT32(1000 + HISTORY_MAX_ENTRIES + 4,
                            arr[arr.size() - 1]["timestamp"].as<unsigned long>());
}

// ── Save / load round-trip ───────────────────────────────────────────

void test_save_writes_valid_json(void) {
  history_init();
  SensorData s = makeSample(2000, 21.5f);
  history_addReading(&s);
  TEST_ASSERT_TRUE(history_save());

  std::string raw = LittleFS.littlefs_read(HISTORY_FILE);
  TEST_ASSERT_FALSE(raw.empty());

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, raw.c_str());
  TEST_ASSERT_FALSE(err);
  TEST_ASSERT_EQUAL_INT(1, doc["count"].as<int>());
}

void test_init_reloads_saved_history_from_disk(void) {
  history_init();
  SensorData s1 = makeSample(3000, 18.0f);
  SensorData s2 = makeSample(3030, 18.5f);
  history_addReading(&s1);
  history_addReading(&s2);
  TEST_ASSERT_TRUE(history_save());

  // Simulate reboot: reset RAM state but keep the LittleFS file, then re-init.
  history_resetForTest();
  history_init();

  TEST_ASSERT_EQUAL_INT(2, history_getCount());

  char buf[4096];
  TEST_ASSERT_TRUE(history_getAsJson(buf, sizeof(buf)));
  DynamicJsonDocument doc(4096);
  TEST_ASSERT_FALSE(deserializeJson(doc, buf));
  JsonArray arr = doc.as<JsonArray>();
  TEST_ASSERT_EQUAL_INT(2, (int)arr.size());
  TEST_ASSERT_EQUAL_UINT32(3000, arr[0]["timestamp"].as<unsigned long>());
  TEST_ASSERT_EQUAL_UINT32(3030, arr[1]["timestamp"].as<unsigned long>());
}

void test_init_with_corrupt_file_starts_empty_not_crash(void) {
  LittleFS.littlefs_inject(HISTORY_FILE, "not json {{{");
  history_init();
  TEST_ASSERT_EQUAL_INT(0, history_getCount());
}

// ── getAsJson / getField ──────────────────────────────────────────────

void test_get_as_json_before_init_returns_empty_array(void) {
  char buf[64];
  bool ok = history_getAsJson(buf, sizeof(buf));
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_STRING("[]", buf);
}

void test_get_as_json_respects_max_entries(void) {
  history_init();
  for (int i = 0; i < 10; i++) {
    SensorData s = makeSample(1000 + i, (float)i);
    history_addReading(&s);
  }
  char buf[4096];
  TEST_ASSERT_TRUE(history_getAsJson(buf, sizeof(buf), 3));

  DynamicJsonDocument doc(4096);
  TEST_ASSERT_FALSE(deserializeJson(doc, buf));
  TEST_ASSERT_EQUAL_INT(3, (int)doc.as<JsonArray>().size());
}

void test_get_field_returns_requested_values(void) {
  history_init();
  SensorData s = makeSample(1000, 23.4f);
  history_addReading(&s);

  char buf[1024];
  TEST_ASSERT_TRUE(history_getField("plant_height_mm", buf, sizeof(buf)));

  DynamicJsonDocument doc(1024);
  TEST_ASSERT_FALSE(deserializeJson(doc, buf));
  JsonArray arr = doc.as<JsonArray>();
  TEST_ASSERT_EQUAL_INT(1, (int)arr.size());
  TEST_ASSERT_EQUAL_INT(120, arr[0]["value"].as<int>());
}

void test_get_field_unknown_name_leaves_value_absent(void) {
  history_init();
  SensorData s = makeSample(1000, 23.4f);
  history_addReading(&s);

  char buf[1024];
  TEST_ASSERT_TRUE(history_getField("no_such_field", buf, sizeof(buf)));

  DynamicJsonDocument doc(1024);
  TEST_ASSERT_FALSE(deserializeJson(doc, buf));
  JsonArray arr = doc.as<JsonArray>();
  // The object is created (and "timestamp" set) before the field-name switch,
  // so an unknown field still yields one entry - just without "value".
  TEST_ASSERT_EQUAL_INT(1, (int)arr.size());
  TEST_ASSERT_EQUAL_UINT32(1000, arr[0]["timestamp"].as<unsigned long>());
  TEST_ASSERT_FALSE(arr[0].containsKey("value"));
}

void test_get_field_before_init_returns_empty_array(void) {
  char buf[64];
  bool ok = history_getField("air_temp", buf, sizeof(buf));
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_STRING("[]", buf);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_with_no_file_starts_empty);
  RUN_TEST(test_init_is_idempotent);
  RUN_TEST(test_add_reading_before_init_is_noop);
  RUN_TEST(test_add_reading_null_pointer_is_noop);
  RUN_TEST(test_add_reading_increments_count);
  RUN_TEST(test_count_caps_at_max_entries_on_wraparound);
  RUN_TEST(test_wraparound_keeps_chronological_order);
  RUN_TEST(test_save_writes_valid_json);
  RUN_TEST(test_init_reloads_saved_history_from_disk);
  RUN_TEST(test_init_with_corrupt_file_starts_empty_not_crash);
  RUN_TEST(test_get_as_json_before_init_returns_empty_array);
  RUN_TEST(test_get_as_json_respects_max_entries);
  RUN_TEST(test_get_field_returns_requested_values);
  RUN_TEST(test_get_field_unknown_name_leaves_value_absent);
  RUN_TEST(test_get_field_before_init_returns_empty_array);
  return UNITY_END();
}
