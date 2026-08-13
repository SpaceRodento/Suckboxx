#include <unity.h>

#include "test_feature_flags.h"
#include "watchdog.h"

void setUp() {}
void tearDown() {}

void test_watchdog_init_does_not_crash_native() {
  watchdog_init();
  TEST_PASS();
}

void test_watchdog_feed_does_not_crash_native() {
  watchdog_feed();
  TEST_PASS();
}

void test_watchdog_log_reset_reason_does_not_crash_native() {
  watchdog_logResetReason();
  TEST_PASS();
}

// ROM-tason boot-cause-tunniste — tarkista keskeiset arvot
void test_rom_reset_reason_name_known_codes() {
  TEST_ASSERT_EQUAL_STRING("POWERON_RESET",       watchdog_romResetReasonName(1));
  TEST_ASSERT_EQUAL_STRING("RTC_SW_SYS_RESET",    watchdog_romResetReasonName(3));
  TEST_ASSERT_EQUAL_STRING("DEEPSLEEP_RESET",     watchdog_romResetReasonName(5));
  TEST_ASSERT_EQUAL_STRING("RTCWDT_BROWN_OUT_RESET", watchdog_romResetReasonName(15));
  TEST_ASSERT_EQUAL_STRING("USB_UART_CHIP_RESET", watchdog_romResetReasonName(21));
  TEST_ASSERT_EQUAL_STRING("USB_JTAG_CHIP_RESET", watchdog_romResetReasonName(22));
  TEST_ASSERT_EQUAL_STRING("POWER_GLITCH_RESET",  watchdog_romResetReasonName(23));
}

void test_rom_reset_reason_name_unknown_returns_fallback() {
  TEST_ASSERT_EQUAL_STRING("UNKNOWN_RAW", watchdog_romResetReasonName(99));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN_RAW", watchdog_romResetReasonName(-1));
  TEST_ASSERT_EQUAL_STRING("NO_MEAN",     watchdog_romResetReasonName(0));
}

// ── Crash-reason classifier (safe-mode input) ──────────────────────
// Values mirror esp_reset_reason_t. These count toward safe mode.
void test_is_crash_reason_true_for_crashes() {
  TEST_ASSERT_TRUE(watchdog_isCrashReason(4));   // PANIC
  TEST_ASSERT_TRUE(watchdog_isCrashReason(5));   // INT_WDT
  TEST_ASSERT_TRUE(watchdog_isCrashReason(6));   // TASK_WDT
  TEST_ASSERT_TRUE(watchdog_isCrashReason(7));   // WDT
  TEST_ASSERT_TRUE(watchdog_isCrashReason(9));   // BROWNOUT
  TEST_ASSERT_TRUE(watchdog_isCrashReason(15));  // CPU_LOCKUP
}

// Clean reboots (power-on, RST button, SW restart, deep-sleep) must NOT count.
void test_is_crash_reason_false_for_clean_resets() {
  TEST_ASSERT_FALSE(watchdog_isCrashReason(0));   // UNKNOWN
  TEST_ASSERT_FALSE(watchdog_isCrashReason(1));   // POWERON
  TEST_ASSERT_FALSE(watchdog_isCrashReason(2));   // EXT (RST button)
  TEST_ASSERT_FALSE(watchdog_isCrashReason(3));   // SW restart
  TEST_ASSERT_FALSE(watchdog_isCrashReason(8));   // DEEPSLEEP
}

void test_is_crash_reason_false_for_out_of_range() {
  TEST_ASSERT_FALSE(watchdog_isCrashReason(99));
  TEST_ASSERT_FALSE(watchdog_isCrashReason(-1));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_watchdog_init_does_not_crash_native);
  RUN_TEST(test_watchdog_feed_does_not_crash_native);
  RUN_TEST(test_watchdog_log_reset_reason_does_not_crash_native);
  RUN_TEST(test_rom_reset_reason_name_known_codes);
  RUN_TEST(test_rom_reset_reason_name_unknown_returns_fallback);
  RUN_TEST(test_is_crash_reason_true_for_crashes);
  RUN_TEST(test_is_crash_reason_false_for_clean_resets);
  RUN_TEST(test_is_crash_reason_false_for_out_of_range);
  return UNITY_END();
}
