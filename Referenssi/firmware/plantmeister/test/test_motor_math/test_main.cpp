#include <unity.h>
// Self-contained — does not include motor_hal.h (which pulls in AccelStepper).
// The conversion formulas are duplicated here intentionally; if motor_hal.h
// changes MOTOR_STEPS_PER_REV or MOTOR_LEAD_MM the values below must match.

// ── Motor constants (mirrors config.h values) ────────────────────
static const int  STEPS_PER_REV = 200;
static const int  LEAD_MM       = 8;
// L298N HALF4WIRE: half-step mode doubles the resolution
// steps = mm * STEPS_PER_REV * 2 / LEAD_MM = mm * 50
static const int  HALF_STEP_FACTOR = 2;

// Conversion functions under test — inline copies of motor_hal.h logic
static inline long mm_to_steps_l298n(int mm) {
  return (long)mm * STEPS_PER_REV * HALF_STEP_FACTOR / LEAD_MM;
}

static inline int steps_to_mm_l298n(long steps) {
  return (int)(steps * LEAD_MM / (STEPS_PER_REV * HALF_STEP_FACTOR));
}

// TMC2208 with 16 microsteps
static const int TMC_MICROSTEPS = 16;
// steps = mm * STEPS_PER_REV * MICROSTEPS / LEAD_MM = mm * 400

static inline long mm_to_steps_tmc(int mm) {
  return (long)mm * STEPS_PER_REV * TMC_MICROSTEPS / LEAD_MM;
}

static inline int steps_to_mm_tmc(long steps) {
  return (int)(steps * LEAD_MM / (STEPS_PER_REV * TMC_MICROSTEPS));
}

// TMC2209 with 16 microsteps (BTT default jumper config — sama kaava kuin TMC2208,
// mutta MOTOR_TMC2209_MICROSTEPS on oma vakio joka voi olla 32/64/256.)
static const int TMC2209_MICROSTEPS = 16;

static inline long mm_to_steps_tmc2209(int mm) {
  return (long)mm * STEPS_PER_REV * TMC2209_MICROSTEPS / LEAD_MM;
}

static inline int steps_to_mm_tmc2209(long steps) {
  return (int)(steps * LEAD_MM / (STEPS_PER_REV * TMC2209_MICROSTEPS));
}

// TMC2209 with 32 microsteps (yleisempi käyttötapaus kun halutaan
// hiljaisempaa liikettä) — varmistus että vakio voi vaihdella.
static inline long mm_to_steps_tmc2209_32us(int mm) {
  return (long)mm * STEPS_PER_REV * 32 / LEAD_MM;
}

void setUp()    {}
void tearDown() {}

// ── L298N HALF4WIRE conversion tests ────────────────────────────

void test_l298n_mm_to_steps_zero() {
  TEST_ASSERT_EQUAL_INT32(0, mm_to_steps_l298n(0));
}

void test_l298n_mm_to_steps_100mm() {
  // 100 * 200 * 2 / 8 = 5000
  TEST_ASSERT_EQUAL_INT32(5000, mm_to_steps_l298n(100));
}

void test_l298n_mm_to_steps_1mm() {
  // 1 * 200 * 2 / 8 = 50
  TEST_ASSERT_EQUAL_INT32(50, mm_to_steps_l298n(1));
}

void test_l298n_steps_to_mm_zero() {
  TEST_ASSERT_EQUAL_INT(0, steps_to_mm_l298n(0));
}

void test_l298n_steps_to_mm_5000() {
  // 5000 * 8 / (200 * 2) = 100
  TEST_ASSERT_EQUAL_INT(100, steps_to_mm_l298n(5000));
}

void test_l298n_round_trip() {
  // Convert mm → steps → mm and verify we get back the same value.
  // Uses a value that divides evenly (avoids integer truncation).
  int original = 40;
  long steps = mm_to_steps_l298n(original);
  int result = steps_to_mm_l298n(steps);
  TEST_ASSERT_EQUAL_INT(original, result);
}

// ── TMC2208 conversion tests ─────────────────────────────────────

void test_tmc_mm_to_steps_100mm() {
  // 100 * 200 * 16 / 8 = 40000
  TEST_ASSERT_EQUAL_INT32(40000, mm_to_steps_tmc(100));
}

void test_tmc_mm_to_steps_1mm() {
  // 1 * 200 * 16 / 8 = 400
  TEST_ASSERT_EQUAL_INT32(400, mm_to_steps_tmc(1));
}

void test_tmc_steps_to_mm_40000() {
  TEST_ASSERT_EQUAL_INT(100, steps_to_mm_tmc(40000));
}

void test_tmc_round_trip() {
  int original = 75;
  long steps = mm_to_steps_tmc(original);
  int result = steps_to_mm_tmc(steps);
  TEST_ASSERT_EQUAL_INT(original, result);
}

// ── TMC2209 conversion tests ─────────────────────────────────────

void test_tmc2209_mm_to_steps_100mm_16us() {
  // 100 * 200 * 16 / 8 = 40000 (sama kuin TMC2208 oletusasetuksilla)
  TEST_ASSERT_EQUAL_INT32(40000, mm_to_steps_tmc2209(100));
}

void test_tmc2209_mm_to_steps_1mm_16us() {
  // 1 * 200 * 16 / 8 = 400
  TEST_ASSERT_EQUAL_INT32(400, mm_to_steps_tmc2209(1));
}

void test_tmc2209_steps_to_mm_40000_16us() {
  TEST_ASSERT_EQUAL_INT(100, steps_to_mm_tmc2209(40000));
}

void test_tmc2209_round_trip_16us() {
  int original = 50;
  long steps = mm_to_steps_tmc2209(original);
  int result = steps_to_mm_tmc2209(steps);
  TEST_ASSERT_EQUAL_INT(original, result);
}

void test_tmc2209_mm_to_steps_100mm_32us() {
  // 100 * 200 * 32 / 8 = 80000 (TMC2209 yleinen 32 microstep)
  TEST_ASSERT_EQUAL_INT32(80000, mm_to_steps_tmc2209_32us(100));
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_l298n_mm_to_steps_zero);
  RUN_TEST(test_l298n_mm_to_steps_100mm);
  RUN_TEST(test_l298n_mm_to_steps_1mm);
  RUN_TEST(test_l298n_steps_to_mm_zero);
  RUN_TEST(test_l298n_steps_to_mm_5000);
  RUN_TEST(test_l298n_round_trip);

  RUN_TEST(test_tmc_mm_to_steps_100mm);
  RUN_TEST(test_tmc_mm_to_steps_1mm);
  RUN_TEST(test_tmc_steps_to_mm_40000);
  RUN_TEST(test_tmc_round_trip);

  RUN_TEST(test_tmc2209_mm_to_steps_100mm_16us);
  RUN_TEST(test_tmc2209_mm_to_steps_1mm_16us);
  RUN_TEST(test_tmc2209_steps_to_mm_40000_16us);
  RUN_TEST(test_tmc2209_round_trip_16us);
  RUN_TEST(test_tmc2209_mm_to_steps_100mm_32us);

  return UNITY_END();
}
