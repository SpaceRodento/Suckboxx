/*=====================================================================
  test_dli_tracker — Unity tests for dli_tracker.h DLI accumulation.

  Tests cover: init state, first-sample anchor, known accumulation,
  cumulative integration, negative PPFD clamping, gap clamping,
  day rollover, millis() overflow safety, and status classification.
=====================================================================*/

#include <unity.h>

// Feature flag overrides — must come before test_feature_flags.h.
// dli_tracker.h depends only on config.h and stdint.h; no hardware needed.
#define ENABLE_SENSORS       false
#define ENABLE_DEVICE_STATE  false
#define HW_AS7341            false
// Disable all sensor sub-features so config.h guard does not fire.
#define ENABLE_TDS_SENSOR    false
#define ENABLE_WATER_TEMP    false
#define ENABLE_FLOAT_SWITCH  false
#define ENABLE_HEIGHT_SENSOR false
#define ENABLE_ENV_SENSOR    false
#define ENABLE_BATTERY_MONITOR false

#include "test_feature_flags.h"

#include "dli_tracker.h"

void setUp() {}
void tearDown() {}

// init() must zero the accumulator, clear hasSample, and set dayStartMs.
void test_dli_init_zero() {
  DliState s;
  dliTracker_init(&s, 5000);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.accumulatedMol);
  TEST_ASSERT_FALSE(s.hasSample);
  TEST_ASSERT_EQUAL_UINT32(5000, s.dayStartMs);
}

// The first addSample after init must only anchor the clock; no integral.
void test_dli_first_sample_anchors_only() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_addSample(&s, 500.0f, 0);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.accumulatedMol);
  TEST_ASSERT_TRUE(s.hasSample);
}

// 500 µmol/m²/s sampled at a realistic 5-min cadence (300 000 ms) for 1 h.
// 5 min is below DLI_MAX_SAMPLE_GAP_MS, so no clamping applies — this is the
// pure-accumulation case (the gap-clamp behaviour is covered separately by
// test_dli_gap_clamped).  12 intervals × (500 × 300 / 1e6) = 12 × 0.15 = 1.8
// mol/m² — the realistic hourly DLI contribution at 500 µmol/m²/s.
void test_dli_known_accumulation() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_addSample(&s, 500.0f, 0);              // anchor at t=0
  for (uint32_t t = 300000; t <= 3600000; t += 300000) {
    dliTracker_addSample(&s, 500.0f, t);            // 5-min cadence, no clamp
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.8f, s.accumulatedMol);
}

// Two consecutive 1-s intervals at 1000 µmol/m²/s => 2 × 0.001 = 0.002 mol/m²
void test_dli_cumulative() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_addSample(&s, 0.0f,    0);      // anchor
  dliTracker_addSample(&s, 1000.0f, 1000);   // +1 s => 0.001 mol
  dliTracker_addSample(&s, 1000.0f, 2000);   // +1 s => 0.001 mol
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.002f, s.accumulatedMol);
}

// Negative PPFD is clamped to 0 — sensor noise must not subtract from DLI.
void test_dli_negative_ppfd_clamped() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_addSample(&s, 0.0f,    0);         // anchor
  dliTracker_addSample(&s, -500.0f, 3600000);   // should contribute 0 mol
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.accumulatedMol);
}

// A 2-hour gap (7 200 000 ms) must be clamped to DLI_MAX_SAMPLE_GAP_MS (900 000 ms = 900 s).
// 500 µmol/m²/s × 900 s / 1e6 = 0.45 mol/m²
void test_dli_gap_clamped() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_addSample(&s, 0.0f,    0);         // anchor
  dliTracker_addSample(&s, 500.0f,  7200000);   // 2 h gap, clamped to 15 min
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.45f, s.accumulatedMol);
}

// After exactly one day, checkRollover must return the accumulated DLI and reset.
// The single 1-hour sample is gap-clamped to 900 s => 500*900/1e6 = 0.45 mol.
void test_dli_rollover_resets() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_addSample(&s, 0.0f,   0);          // anchor at t=0
  dliTracker_addSample(&s, 500.0f, 3600000);    // 0.45 mol accumulated (gap-clamped)

  // nowMs == DLI_DAY_LENGTH_MS, dayStartMs == 0 => elapsed == DLI_DAY_LENGTH_MS => rollover
  float prev = dliTracker_checkRollover(&s, DLI_DAY_LENGTH_MS, DLI_DAY_LENGTH_MS);

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.45f, prev);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.accumulatedMol);
}

// Before a full day elapses, checkRollover must return -1.0f and leave state intact.
void test_dli_no_rollover_before_day() {
  DliState s;
  dliTracker_init(&s, 0);
  float result = dliTracker_checkRollover(&s, 1000, DLI_DAY_LENGTH_MS);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, result);
}

// millis() rollover: anchor at 0xFFFFFC18, second sample at (0xFFFFFC18 + 1000)
// which wraps to 0.  unsigned dtMs = 0 - 0xFFFFFC18 = 1000.
// deltaMol = 1000 * 1 s / 1e6 = 0.001 mol/m²
void test_dli_millis_overflow() {
  DliState s;
  uint32_t anchorMs = 0xFFFFFC18UL;
  uint32_t nextMs   = (uint32_t)(anchorMs + 1000UL);  // wraps to 0x00000000

  dliTracker_init(&s, anchorMs);
  dliTracker_addSample(&s, 0.0f,    anchorMs);  // anchor
  dliTracker_addSample(&s, 1000.0f, nextMs);    // dtMs unsigned = 1000 ms

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.001f, s.accumulatedMol);
}

// --- Status classification ---

void test_dli_status_low() {
  TEST_ASSERT_EQUAL_INT(DLI_STATUS_LOW, dliTracker_status(11.9f, 12.0f, 17.0f));
}

void test_dli_status_optimal() {
  TEST_ASSERT_EQUAL_INT(DLI_STATUS_OPTIMAL, dliTracker_status(14.0f, 12.0f, 17.0f));
}

void test_dli_status_high() {
  TEST_ASSERT_EQUAL_INT(DLI_STATUS_HIGH, dliTracker_status(17.1f, 12.0f, 17.0f));
}

// Boundary values at optMin and optMax must both be OPTIMAL (inclusive).
void test_dli_status_boundaries() {
  TEST_ASSERT_EQUAL_INT(DLI_STATUS_OPTIMAL, dliTracker_status(12.0f, 12.0f, 17.0f));
  TEST_ASSERT_EQUAL_INT(DLI_STATUS_OPTIMAL, dliTracker_status(17.0f, 12.0f, 17.0f));
}


// ════════════════════════════════════════════════════════════════════
// Persistointi: ikkunan palautus reboottin yli (dliTracker_restore)
// ════════════════════════════════════════════════════════════════════

// Palautettu integraali ja ikkunan sijainti sailyvat sellaisenaan.
void test_dli_restore_resumes_integral_and_window() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_restore(&s, 1000, 4.5f, 3600000UL);   // 4.5 mol, 1 h ikkunaa kulunut
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.5f, s.accumulatedMol);
  TEST_ASSERT_EQUAL_UINT32(3600000UL, dliTracker_elapsedMs(&s, 1000));
}

// Palautus EI saa integroida katkon yli: ensimmainen naytte vain ankkuroi
// kellon. Muuten yon yli ollut katko saisi aamun ensimmaisen lukeman
// kerrottuna koko katkon pituudella.
void test_dli_restore_does_not_integrate_across_outage() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_restore(&s, 1000, 4.5f, 3600000UL);
  dliTracker_addSample(&s, 500.0f, 1000);          // heti bootin jalkeen
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.5f, s.accumulatedMol);   // ei muutosta
  // Seuraava naytte integroi normaalisti vain sen jalkeisen ajan.
  dliTracker_addSample(&s, 500.0f, 1000 + 10000UL);            // 10 s @ 500
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.5f + 0.005f, s.accumulatedMol);
}

// Rollover laskee palautetusta ikkunasta, ei bootista: 23 h palautettuna +
// 1 h ajoa = paiva taynna.
void test_dli_restore_rollover_counts_from_restored_window() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_restore(&s, 1000, 7.0f, 23UL * 3600000UL);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f,
      dliTracker_checkRollover(&s, 1000 + 3600000UL - 2000UL, DLI_DAY_LENGTH_MS));
  float done = dliTracker_checkRollover(&s, 1000 + 3600000UL, DLI_DAY_LENGTH_MS);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, done);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.accumulatedMol);
}

// Roskadata ei saa paasta lapi: negatiivinen -> 0, liian pitka ikkuna
// leikataan vuorokauteen.
void test_dli_restore_sanitizes_bad_input() {
  DliState s;
  dliTracker_init(&s, 0);
  dliTracker_restore(&s, 0, -3.0f, DLI_DAY_LENGTH_MS * 2UL);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.accumulatedMol);
  TEST_ASSERT_TRUE(dliTracker_elapsedMs(&s, 0) < DLI_DAY_LENGTH_MS);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_dli_init_zero);
  RUN_TEST(test_dli_first_sample_anchors_only);
  RUN_TEST(test_dli_known_accumulation);
  RUN_TEST(test_dli_cumulative);
  RUN_TEST(test_dli_negative_ppfd_clamped);
  RUN_TEST(test_dli_gap_clamped);
  RUN_TEST(test_dli_rollover_resets);
  RUN_TEST(test_dli_no_rollover_before_day);
  RUN_TEST(test_dli_millis_overflow);
  RUN_TEST(test_dli_status_low);
  RUN_TEST(test_dli_status_optimal);
  RUN_TEST(test_dli_status_high);
  RUN_TEST(test_dli_status_boundaries);
  RUN_TEST(test_dli_restore_resumes_integral_and_window);
  RUN_TEST(test_dli_restore_does_not_integrate_across_outage);
  RUN_TEST(test_dli_restore_rollover_counts_from_restored_window);
  RUN_TEST(test_dli_restore_sanitizes_bad_input);
  return UNITY_END();
}
