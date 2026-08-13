// test_dli_expectation - daily light sum judged against the photoperiod
//
// Regression origin (29.7.2026): the status view compared a RUNNING daily
// light sum against the FULL-day target window, so "Valosumma matala" was
// true every morning on every plant — the accumulator starts at zero and is
// below target by definition until the day has delivered it. These tests pin
// the projection that replaced it, and the guards that keep the projection
// from being mistaken for a weather forecast.
//
// Elapsed hours come from the DEVICE (growing.light_elapsed_h), not from a
// wall clock: the light cycle is anchored to the device's own
// lightCycleStartMs. -1 means the cycle is in its dark part.

#include <unity.h>

#include "dli_expectation.h"

void setUp() {}
void tearDown() {}

// Basil seedling: 6-10 mol over an 18 h photoperiod.
static const float LO = 6.0f;
static const float HI = 10.0f;
static const int   HOURS = 18;

// ── The bug this module exists for ──────────────────────────────────

// 7.65 h into an 18 h photoperiod, 4.6 mol banked, 166 umol arriving. The old
// code called this LOW because 4.6 < 6. The day had 10.35 h left and was on
// course for ~10.8 mol — the one verdict that must not appear is LOW.
void test_midday_shortfall_is_not_flagged_when_on_track() {
  const PeStatus st = dli_evaluate(4.6f, 166.0f, true, 7.65f, HOURS, LO, HI);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(PE_LOW, st,
      "kesken paivaa oleva kertyma tuomittiin vajaaksi vaikka tahti riittaa");
}

// Same point in the day, genuinely too dark: 15 umol adds 0.6 mol over the
// remaining 11 h and lands far short.
void test_genuine_shortfall_is_flagged() {
  TEST_ASSERT_EQUAL_INT(PE_LOW,
      dli_evaluate(1.0f, 15.0f, true, 7.0f, HOURS, LO, HI));
}

// The balcony case: strong sun 4 h in. A reaction to present light ("shade is
// coming due"), not a prediction about clouds.
void test_strong_sun_projects_an_overshoot() {
  TEST_ASSERT_EQUAL_INT(PE_HIGH,
      dli_evaluate(3.0f, 800.0f, true, 4.0f, HOURS, LO, HI));
}

// ── Guards ──────────────────────────────────────────────────────────

// One hour into eighteen, the remaining-hours multiplier is 17: a passing
// shadow would swing the projection by more than the whole target window.
void test_silent_early_in_the_photoperiod() {
  TEST_ASSERT_EQUAL_INT(PE_NODATA,
      dli_evaluate(0.1f, 20.0f, true, 1.0f, HOURS, LO, HI));
}

// Dark part of the cycle. Judging here would announce a shortfall nobody can
// act on until the lights come back, and the accumulator may have just rolled.
void test_no_verdict_during_the_dark_hours() {
  TEST_ASSERT_EQUAL_INT(PE_NODATA,
      dli_evaluate(0.2f, 0.0f, false, -1.0f, HOURS, LO, HI));
}

// An overshoot already banked needs neither projection nor clock: light the
// plant has taken cannot be un-taken by later cloud.
void test_banked_overshoot_reported_even_early() {
  TEST_ASSERT_EQUAL_INT(PE_HIGH,
      dli_evaluate(12.0f, 0.0f, false, 1.0f, HOURS, LO, HI));
}

// ...and it outranks the dark-hours guard too, for the same reason.
void test_banked_overshoot_reported_in_the_dark() {
  TEST_ASSERT_EQUAL_INT(PE_HIGH,
      dli_evaluate(12.0f, 0.0f, false, -1.0f, HOURS, LO, HI));
}

// No PPFD -> nothing to extrapolate from. Defer until the day is essentially
// over, which is the one moment a raw accumulated-vs-target read is fair.
void test_without_ppfd_defers_until_day_is_nearly_over() {
  TEST_ASSERT_EQUAL_INT(PE_NODATA,
      dli_evaluate(2.0f, 0.0f, false, 8.0f, HOURS, LO, HI));
  TEST_ASSERT_EQUAL_INT(PE_LOW,
      dli_evaluate(2.0f, 0.0f, false, 16.0f, HOURS, LO, HI));
}

// A device that never reported a photoperiod must not be second-guessed:
// assuming a default produces confident advice from invented input.
void test_unknown_photoperiod_stays_silent() {
  TEST_ASSERT_EQUAL_INT(PE_NODATA,
      dli_evaluate(2.0f, 100.0f, true, 5.0f, 0, LO, HI));
}

// Elapsed longer than the photoperiod (clock skew, a phase change shortening
// lightHours mid-cycle) must not produce negative remaining hours — that
// would subtract light the plant never lost.
void test_elapsed_beyond_photoperiod_is_clamped() {
  const PeStatus st = dli_evaluate(2.0f, 500.0f, true, 25.0f, HOURS, LO, HI);
  TEST_ASSERT_EQUAL_INT_MESSAGE(PE_LOW, st,
      "ylipitka kulunut aika tuotti haamuvaloa projektioon");
}

// ── Projection arithmetic ───────────────────────────────────────────

// 200 umol for 5 h = 200 * 5 * 0.0036 = 3.6 mol on top of what is banked.
void test_projection_uses_umol_hour_conversion() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.6f, dli_project(2.0f, 200.0f, 5.0f));
}

// At the end of the photoperiod the projection equals the sum itself — no
// phantom light after lights-out.
void test_projection_adds_nothing_when_day_is_over() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, dli_project(7.0f, 900.0f, 0.0f));
}

void test_projection_never_subtracts_on_negative_remaining() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, dli_project(7.0f, 900.0f, -3.0f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_midday_shortfall_is_not_flagged_when_on_track);
  RUN_TEST(test_genuine_shortfall_is_flagged);
  RUN_TEST(test_strong_sun_projects_an_overshoot);
  RUN_TEST(test_silent_early_in_the_photoperiod);
  RUN_TEST(test_no_verdict_during_the_dark_hours);
  RUN_TEST(test_banked_overshoot_reported_even_early);
  RUN_TEST(test_banked_overshoot_reported_in_the_dark);
  RUN_TEST(test_without_ppfd_defers_until_day_is_nearly_over);
  RUN_TEST(test_unknown_photoperiod_stays_silent);
  RUN_TEST(test_elapsed_beyond_photoperiod_is_clamped);
  RUN_TEST(test_projection_uses_umol_hour_conversion);
  RUN_TEST(test_projection_adds_nothing_when_day_is_over);
  RUN_TEST(test_projection_never_subtracts_on_negative_remaining);
  return UNITY_END();
}
