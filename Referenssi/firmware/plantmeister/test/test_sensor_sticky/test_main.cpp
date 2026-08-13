// test_sensor_sticky - last-good-sample gate
//
// Regression origin (29.7.2026): VPD vanished from the wall panel at random.
// Root cause was not a broken sensor but sensors_read() treating "no new
// sample this tick" identically to "sensor is gone" — one skipped SCD41
// reading dropped envValid, and with it vpdValid, air temp and humidity.
// These tests pin the gate that decides when a cached sample may stand in.

#include <unity.h>
#include <stdint.h>

#include "sensor_sticky.h"

static StickyState s;

void setUp() { sticky_reset(&s); }
void tearDown() {}

// A gate that has never seen a good sample must not authorise anything.
// Booting with the sensor unplugged has to show "--", never a fabricated 0.
void test_never_reuses_before_first_good_sample() {
  TEST_ASSERT_FALSE(sticky_shouldReuse(&s, 1000, 5000));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, sticky_ageMs(&s, 1000));
}

void test_reuses_within_grace_window() {
  sticky_markGood(&s, 10000);
  TEST_ASSERT_TRUE(sticky_shouldReuse(&s, 12000, 5000));
  TEST_ASSERT_EQUAL_UINT32(2000, sticky_ageMs(&s, 12000));
}

// The window must close on its own, otherwise a dead sensor reads as fresh
// forever — the failure mode that makes stickiness dangerous.
void test_stops_reusing_after_grace_window() {
  sticky_markGood(&s, 10000);
  TEST_ASSERT_FALSE(sticky_shouldReuse(&s, 15000, 5000));   // exactly at limit
  TEST_ASSERT_FALSE(sticky_shouldReuse(&s, 90000, 5000));
}

void test_fresh_sample_restarts_the_window() {
  sticky_markGood(&s, 10000);
  TEST_ASSERT_FALSE(sticky_shouldReuse(&s, 16000, 5000));
  sticky_markGood(&s, 16000);
  TEST_ASSERT_TRUE(sticky_shouldReuse(&s, 20000, 5000));
  TEST_ASSERT_EQUAL_UINT32(4000, sticky_ageMs(&s, 20000));
}

// millis() wraps at ~49.7 days. An unattended grow runs past that, and a
// signed comparison here would make the gate slam shut for a whole window.
void test_survives_millis_rollover() {
  const uint32_t beforeWrap = UINT32_MAX - 1000;
  sticky_markGood(&s, beforeWrap);
  const uint32_t afterWrap = 1000;
  // 2001, not 2000: 1000 ticks up to UINT32_MAX, then 0 is a tick of its own,
  // then 1000 more. The off-by-one is in the counting, not the code — what
  // matters is that the result is a small positive number rather than the
  // ~49-day value a signed comparison would produce here.
  TEST_ASSERT_EQUAL_UINT32(2001, sticky_ageMs(&s, afterWrap));
  TEST_ASSERT_TRUE(sticky_shouldReuse(&s, afterWrap, 5000));
}

// Age zero must not be mistaken for "no sample": a reading taken this very
// millisecond is the freshest possible, not an absent one.
void test_zero_age_is_a_valid_fresh_sample() {
  sticky_markGood(&s, 7000);
  TEST_ASSERT_EQUAL_UINT32(0, sticky_ageMs(&s, 7000));
  TEST_ASSERT_TRUE(sticky_shouldReuse(&s, 7000, 5000));
}

void test_reset_forgets_the_cached_sample() {
  sticky_markGood(&s, 10000);
  sticky_reset(&s);
  TEST_ASSERT_FALSE(sticky_shouldReuse(&s, 10500, 5000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_never_reuses_before_first_good_sample);
  RUN_TEST(test_reuses_within_grace_window);
  RUN_TEST(test_stops_reusing_after_grace_window);
  RUN_TEST(test_fresh_sample_restarts_the_window);
  RUN_TEST(test_survives_millis_rollover);
  RUN_TEST(test_zero_age_is_a_valid_fresh_sample);
  RUN_TEST(test_reset_forgets_the_cached_sample);
  return UNITY_END();
}
