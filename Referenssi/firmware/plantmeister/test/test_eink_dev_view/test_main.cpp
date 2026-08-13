#include <unity.h>
#include <string.h>

#include "eink_dev_view.h"

// ── State label mapping ──────────────────────────────────────────────

void test_state_label_known_values() {
  TEST_ASSERT_EQUAL_STRING("BOOT",  eink_dev_stateLabel(0));
  TEST_ASSERT_EQUAL_STRING("SELF",  eink_dev_stateLabel(1));
  TEST_ASSERT_EQUAL_STRING("IDLE",  eink_dev_stateLabel(2));
  TEST_ASSERT_EQUAL_STRING("WAIT",  eink_dev_stateLabel(3));
  TEST_ASSERT_EQUAL_STRING("GROW",  eink_dev_stateLabel(4));
  TEST_ASSERT_EQUAL_STRING("PAUS",  eink_dev_stateLabel(5));
  TEST_ASSERT_EQUAL_STRING("FAULT", eink_dev_stateLabel(6));
  TEST_ASSERT_EQUAL_STRING("SHDN",  eink_dev_stateLabel(7));
  TEST_ASSERT_EQUAL_STRING("TEST",  eink_dev_stateLabel(8));
}

void test_state_label_unknown_returns_question() {
  TEST_ASSERT_EQUAL_STRING("?", eink_dev_stateLabel(-1));
  TEST_ASSERT_EQUAL_STRING("?", eink_dev_stateLabel(99));
}

// ── Uptime formatting ────────────────────────────────────────────────

void test_uptime_seconds_only() {
  char buf[32];
  eink_dev_formatUptime(45, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0m 45s", buf);
}

void test_uptime_minutes_seconds() {
  char buf[32];
  eink_dev_formatUptime(125, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("2m 05s", buf);
}

void test_uptime_just_under_hour() {
  char buf[32];
  eink_dev_formatUptime(3599, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("59m 59s", buf);
}

void test_uptime_exactly_one_hour() {
  char buf[32];
  eink_dev_formatUptime(3600, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1h 00m", buf);
}

void test_uptime_hours_minutes() {
  char buf[32];
  eink_dev_formatUptime(13UL * 3600UL + 4UL * 60UL + 17UL, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("13h 04m", buf);
}

void test_uptime_zero() {
  char buf[32];
  eink_dev_formatUptime(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0m 00s", buf);
}

// ── Heap formatting ──────────────────────────────────────────────────

void test_heap_zero() {
  char buf[16];
  eink_dev_formatHeap(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0 kB", buf);
}

void test_heap_under_one_kb_rounds_down() {
  char buf[16];
  eink_dev_formatHeap(1023, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0 kB", buf);
}

void test_heap_typical_value() {
  char buf[16];
  eink_dev_formatHeap(220 * 1024, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("220 kB", buf);
}

// ── Fault hex formatting ─────────────────────────────────────────────

void test_faults_zero() {
  char buf[8];
  eink_dev_formatFaults(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0x00", buf);
}

void test_faults_some_bits() {
  char buf[8];
  eink_dev_formatFaults(0xA5, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0xA5", buf);
}

void test_faults_all_bits() {
  char buf[8];
  eink_dev_formatFaults(0xFF, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0xFF", buf);
}

// ── Last-intent formatting ───────────────────────────────────────────

void test_intent_present_with_source() {
  char buf[64];
  eink_dev_formatLastIntent("START_GROWING", "BUTTON", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Last intent: START_GROWING  src=BUTTON", buf);
}

void test_intent_present_no_source() {
  char buf[64];
  eink_dev_formatLastIntent("STOP_GROWING", "", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Last intent: STOP_GROWING  src=?", buf);
}

void test_intent_missing() {
  char buf[64];
  eink_dev_formatLastIntent("", "", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Last intent: (none reported)", buf);
}

void test_intent_null_treated_as_missing() {
  char buf[64];
  eink_dev_formatLastIntent(NULL, NULL, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Last intent: (none reported)", buf);
}

// ── Buffer safety ────────────────────────────────────────────────────

void test_formatters_handle_null_buffer() {
  size_t n;
  n = eink_dev_formatUptime(60, NULL, 0);
  TEST_ASSERT_EQUAL_UINT(0, n);
  n = eink_dev_formatHeap(1024, NULL, 0);
  TEST_ASSERT_EQUAL_UINT(0, n);
  n = eink_dev_formatFaults(0xFF, NULL, 0);
  TEST_ASSERT_EQUAL_UINT(0, n);
  n = eink_dev_formatLastIntent("X", "Y", NULL, 0);
  TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_formatter_truncation_keeps_null_terminator() {
  // 4-byte buffer cannot hold "13h 04m\0" (8 bytes); ensure no crash and
  // returned string is still null-terminated within bounds.
  char buf[4];
  buf[0] = 'x';
  eink_dev_formatUptime(13UL * 3600UL + 4UL * 60UL, buf, sizeof(buf));
  TEST_ASSERT_LESS_THAN(sizeof(buf), strlen(buf));
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_state_label_known_values);
  RUN_TEST(test_state_label_unknown_returns_question);

  RUN_TEST(test_uptime_seconds_only);
  RUN_TEST(test_uptime_minutes_seconds);
  RUN_TEST(test_uptime_just_under_hour);
  RUN_TEST(test_uptime_exactly_one_hour);
  RUN_TEST(test_uptime_hours_minutes);
  RUN_TEST(test_uptime_zero);

  RUN_TEST(test_heap_zero);
  RUN_TEST(test_heap_under_one_kb_rounds_down);
  RUN_TEST(test_heap_typical_value);

  RUN_TEST(test_faults_zero);
  RUN_TEST(test_faults_some_bits);
  RUN_TEST(test_faults_all_bits);

  RUN_TEST(test_intent_present_with_source);
  RUN_TEST(test_intent_present_no_source);
  RUN_TEST(test_intent_missing);
  RUN_TEST(test_intent_null_treated_as_missing);

  RUN_TEST(test_formatters_handle_null_buffer);
  RUN_TEST(test_formatter_truncation_keeps_null_terminator);
  return UNITY_END();
}
