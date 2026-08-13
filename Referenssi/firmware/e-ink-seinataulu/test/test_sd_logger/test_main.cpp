/*=====================================================================
  test_sd_logger - sd_logger_format.h (CSV row/filename formatting, M2)

  Covers only the pure formatting logic (no SD/SPI/Arduino) - the hardware
  side (sdlog_init/sdlog_appendRow in sd_logger.h) needs real SD/SPI and is
  proven on the bench instead (smoke/sd_test.cpp, testaus.md rauta-smoke).
=====================================================================*/

#include <string.h>
#include <unity.h>

#include "sd_logger_format.h"

void setUp() {}
void tearDown() {}

static DisplayData makeBaseData() {
  DisplayData d = {};
  d.dataValid = true;
  strncpy(d.deviceStateName, "GROWING", sizeof(d.deviceStateName) - 1);
  return d;
}

// Count commas -> number of columns is commaCount + 1. Cheap way to catch a
// field/header count mismatch without hardcoding the exact string twice.
static int countCommas(const char* s) {
  int n = 0;
  for (const char* p = s; *p; p++) if (*p == ',') n++;
  return n;
}

// ── Filename ────────────────────────────────────────────────────────

void test_filename_iso_format() {
  char out[32];
  sdlog_buildFilename(out, sizeof(out), 2026, 8, 3);
  TEST_ASSERT_EQUAL_STRING("/sensors_2026-08-03.csv", out);
}

void test_filename_pads_single_digit_month_and_day() {
  char out[32];
  sdlog_buildFilename(out, sizeof(out), 2026, 1, 5);
  TEST_ASSERT_EQUAL_STRING("/sensors_2026-01-05.csv", out);
}

// ── Header / meta ───────────────────────────────────────────────────

void test_header_line_column_count_matches_row() {
  char header[256];
  sdlog_headerLine(header, sizeof(header));
  DisplayData d = makeBaseData();
  char row[256];
  sdlog_buildRow(&d, true, "2026-08-03T12:00:00", 12345, row, sizeof(row));
  TEST_ASSERT_EQUAL_INT(countCommas(header), countCommas(row));
}

void test_header_line_starts_with_timestamp_iso() {
  char header[256];
  sdlog_headerLine(header, sizeof(header));
  TEST_ASSERT_EQUAL_STRING_LEN("timestamp_iso,time_source,uptime_ms,device_state,", header, 49);
}

void test_meta_line_contains_schema_version() {
  char meta[48];
  sdlog_metaLine(meta, sizeof(meta));
  TEST_ASSERT_TRUE(meta[0] == '#');
  char expectSuffix[16];
  snprintf(expectSuffix, sizeof(expectSuffix), "version=%d", SD_LOG_SCHEMA_VERSION);
  TEST_ASSERT_NOT_NULL(strstr(meta, expectSuffix));
}

// ── Row: gated fields ───────────────────────────────────────────────

void test_row_all_invalid_leaves_gated_fields_empty() {
  DisplayData d = makeBaseData();
  d.envValid = false;
  d.vpdValid = false;
  d.co2Valid = false;
  d.leafTempValid = false;
  d.heightValid = false;
  d.ppfdValid = false;
  d.powerValid = false;
  char row[256];
  sdlog_buildRow(&d, false, "", 500, row, sizeof(row));
  // "uptime,500,GROWING,,,,,," -> the six gated fields right after
  // device_state are empty (double commas back to back).
  TEST_ASSERT_NOT_NULL(strstr(row, ",uptime,500,GROWING,,,,,,"));
}

void test_row_all_valid_fills_gated_fields() {
  DisplayData d = makeBaseData();
  d.envValid = true;       d.airTemp = 22.5f;      d.airHumidity = 61.2f;
  d.vpdValid = true;       d.vpdKpa = 0.86f;
  d.co2Valid = true;       d.co2Ppm = 620;
  d.leafTempValid = true;  d.leafTempC = 21.9f;
  d.waterTemp = 20.1f;
  d.heightValid = true;    d.plantHeightMm = 145;
  d.ppfdValid = true;      d.ppfd = 210.4f;
  d.dli = 8.123f;
  d.tdsPpm = 480;
  d.batteryVoltage = 12.34f;
  d.batteryPercent = 87;
  d.powerValid = true;     d.powerCurrentMa = 340.2f; d.powerChargeMah = 12.55f;
  d.lightsOn = true;
  d.pumpRunning = false;

  char row[256];
  sdlog_buildRow(&d, true, "2026-08-03T09:15:00", 999000, row, sizeof(row));

  TEST_ASSERT_NOT_NULL(strstr(row, "2026-08-03T09:15:00,ntp,999000,GROWING,"));
  TEST_ASSERT_NOT_NULL(strstr(row, "22.5,61.2,0.86,620,21.9,20.10,145,210.4,8.123,480,12.34,87,340.2,12.55,1,0"));
}

// ── Row: time source ────────────────────────────────────────────────

void test_row_uptime_source_when_ntp_unavailable() {
  DisplayData d = makeBaseData();
  char row[256];
  sdlog_buildRow(&d, false, "", 42, row, sizeof(row));
  TEST_ASSERT_NOT_NULL(strstr(row, ",uptime,42,"));
  // No fake calendar timestamp when NTP is not available.
  TEST_ASSERT_EQUAL_STRING_LEN(",uptime", row, 7);
}

void test_row_ntp_source_carries_iso_timestamp() {
  DisplayData d = makeBaseData();
  char row[256];
  sdlog_buildRow(&d, true, "2026-08-03T10:00:00", 100, row, sizeof(row));
  TEST_ASSERT_EQUAL_STRING_LEN("2026-08-03T10:00:00,ntp,100,", row, 28);
}

// ── Booleans as 0/1 ─────────────────────────────────────────────────

void test_row_booleans_render_as_zero_one() {
  DisplayData d = makeBaseData();
  d.lightsOn = true;
  d.pumpRunning = true;
  char row[256];
  sdlog_buildRow(&d, false, "", 0, row, sizeof(row));
  size_t len = strlen(row);
  TEST_ASSERT_TRUE(len >= 3);
  TEST_ASSERT_EQUAL_STRING("1,1", row + len - 3);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_filename_iso_format);
  RUN_TEST(test_filename_pads_single_digit_month_and_day);
  RUN_TEST(test_header_line_column_count_matches_row);
  RUN_TEST(test_header_line_starts_with_timestamp_iso);
  RUN_TEST(test_meta_line_contains_schema_version);
  RUN_TEST(test_row_all_invalid_leaves_gated_fields_empty);
  RUN_TEST(test_row_all_valid_fills_gated_fields);
  RUN_TEST(test_row_uptime_source_when_ntp_unavailable);
  RUN_TEST(test_row_ntp_source_carries_iso_timestamp);
  RUN_TEST(test_row_booleans_render_as_zero_one);
  return UNITY_END();
}
