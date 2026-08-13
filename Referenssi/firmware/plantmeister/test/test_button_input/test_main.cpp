#include <unity.h>

#include "test_feature_flags.h"
#include "Arduino.h"  // defines HIGH/LOW before we use them below

// Override digitalRead before button_input.h is included.
// button_input.h is header-only and calls digitalRead() directly.
// We redirect it to a test-controlled variable so we can simulate
// press / release without touching stubs/Arduino.h.
static int g_button_pin_level = HIGH;  // HIGH = not pressed (BUTTON_ACTIVE_LOW=1)
#undef digitalRead
#define digitalRead(pin) (g_button_pin_level)

#include "button_input.h"

// ── Callback counter ──────────────────────────────────────────────────

static int g_short_press_count = 0;
static int g_long_press_count  = 0;

static void test_callback(ButtonEvent ev) {
  if (ev == BUTTON_EVENT_SHORT_PRESS) g_short_press_count++;
  if (ev == BUTTON_EVENT_LONG_PRESS)  g_long_press_count++;
}

// ── Helpers ───────────────────────────────────────────────────────────

// Advance millis and run one button_tick() poll cycle.
// button_tick() rate-limits to BUTTON_POLL_INTERVAL_MS (10 ms), so we
// advance by that amount each call to guarantee a poll is executed.
static void advance_and_tick(unsigned long stepMs = BUTTON_POLL_INTERVAL_MS) {
  g_stub_millis += stepMs;
  button_tick();
}

// Advance millis by `totalMs` in BUTTON_POLL_INTERVAL_MS steps.
static void advance_and_tick_for(unsigned long totalMs) {
  unsigned long steps = totalMs / BUTTON_POLL_INTERVAL_MS;
  if (steps == 0) steps = 1;
  for (unsigned long i = 0; i < steps; i++) {
    advance_and_tick();
  }
}

// ── Setup / teardown ──────────────────────────────────────────────────

void setUp() {
  g_stub_millis = 0;
  g_button_pin_level = HIGH;  // not pressed
  g_short_press_count = 0;
  g_long_press_count  = 0;
  button_init();
  button_setCallback(test_callback);
}

void tearDown() {}

// ── Tests ─────────────────────────────────────────────────────────────

void test_init_not_pressed() {
  TEST_ASSERT_FALSE(button_isPressed());
}

void test_single_tick_low_no_trigger_before_debounce() {
  // Press pin LOW, but don't advance past debounce window (30 ms).
  g_button_pin_level = LOW;
  advance_and_tick();  // 10 ms elapsed — still in debounce window
  // isPressed should remain false (raw not yet promoted to stable)
  TEST_ASSERT_FALSE(button_isPressed());
  // No callbacks fired
  TEST_ASSERT_EQUAL_INT(0, g_short_press_count);
  TEST_ASSERT_EQUAL_INT(0, g_long_press_count);
}

void test_short_press_fires_after_debounce_and_release() {
  // Press button and hold past debounce (30 ms)
  g_button_pin_level = LOW;
  advance_and_tick_for(BUTTON_DEBOUNCE_MS + BUTTON_POLL_INTERVAL_MS);

  // Stable press should be detected
  TEST_ASSERT_TRUE(button_isPressed());

  // Release
  g_button_pin_level = HIGH;
  advance_and_tick_for(BUTTON_DEBOUNCE_MS + BUTTON_POLL_INTERVAL_MS);

  // Short press callback should have fired exactly once
  TEST_ASSERT_EQUAL_INT(1, g_short_press_count);
  TEST_ASSERT_EQUAL_INT(0, g_long_press_count);
}

void test_long_press_fires_at_threshold() {
  // Press and hold past debounce first (g_buttonPressStartMs is set after debounce),
  // then hold for BUTTON_LONG_PRESS_MS + margin
  g_button_pin_level = LOW;
  advance_and_tick_for(BUTTON_DEBOUNCE_MS + BUTTON_LONG_PRESS_MS + BUTTON_POLL_INTERVAL_MS * 2);

  TEST_ASSERT_EQUAL_INT(1, g_long_press_count);
}

void test_long_press_does_not_fire_short_on_release() {
  // Hold for debounce + long press, then release
  g_button_pin_level = LOW;
  advance_and_tick_for(BUTTON_DEBOUNCE_MS + BUTTON_LONG_PRESS_MS + BUTTON_POLL_INTERVAL_MS * 2);

  g_button_pin_level = HIGH;
  advance_and_tick_for(BUTTON_DEBOUNCE_MS + BUTTON_POLL_INTERVAL_MS);

  // Long press fired, but short press must NOT fire after release
  TEST_ASSERT_EQUAL_INT(1, g_long_press_count);
  TEST_ASSERT_EQUAL_INT(0, g_short_press_count);
}

void test_long_press_fires_only_once_while_held() {
  // Hold well past the threshold — long press must not repeat
  g_button_pin_level = LOW;
  advance_and_tick_for(BUTTON_DEBOUNCE_MS + BUTTON_LONG_PRESS_MS * 2);

  TEST_ASSERT_EQUAL_INT(1, g_long_press_count);
}

void test_multiple_short_presses_count_correctly() {
  for (int i = 0; i < 3; i++) {
    // Press
    g_button_pin_level = LOW;
    advance_and_tick_for(BUTTON_DEBOUNCE_MS + BUTTON_POLL_INTERVAL_MS);
    // Release
    g_button_pin_level = HIGH;
    advance_and_tick_for(BUTTON_DEBOUNCE_MS + BUTTON_POLL_INTERVAL_MS);
  }
  TEST_ASSERT_EQUAL_INT(3, g_short_press_count);
  TEST_ASSERT_EQUAL_INT(0, g_long_press_count);
}

void test_contact_bounce_under_debounce_does_not_trigger() {
  // Simulate contact bounce: several rapid LOW-HIGH transitions well
  // below the debounce threshold (10 ms each, total 20 ms < 30 ms).
  for (int i = 0; i < 2; i++) {
    g_button_pin_level = LOW;
    advance_and_tick();
    g_button_pin_level = HIGH;
    advance_and_tick();
  }
  // Nothing should have triggered — bounces resolved before debounce settled
  TEST_ASSERT_FALSE(button_isPressed());
  TEST_ASSERT_EQUAL_INT(0, g_short_press_count);
  TEST_ASSERT_EQUAL_INT(0, g_long_press_count);
}

// ── main ──────────────────────────────────────────────────────────────

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_not_pressed);
  RUN_TEST(test_single_tick_low_no_trigger_before_debounce);
  RUN_TEST(test_short_press_fires_after_debounce_and_release);
  RUN_TEST(test_long_press_fires_at_threshold);
  RUN_TEST(test_long_press_does_not_fire_short_on_release);
  RUN_TEST(test_long_press_fires_only_once_while_held);
  RUN_TEST(test_multiple_short_presses_count_correctly);
  RUN_TEST(test_contact_bounce_under_debounce_does_not_trigger);
  return UNITY_END();
}
