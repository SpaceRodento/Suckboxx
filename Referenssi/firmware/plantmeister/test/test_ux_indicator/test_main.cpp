#include <unity.h>

// Force flags before any project header is included.
// EINK must be off so no third-party hardware headers are pulled in.
#define ENABLE_EINK_DISPLAY false
#define ENABLE_UX_INDICATOR true

#include "test_feature_flags.h"
#include "device_state.h"
#include "ux_indicator.h"

// ── Setup / teardown ──────────────────────────────────────────────────

void setUp() {
  g_stub_millis = 0;
  device_init();
}

void tearDown() {}

// ── ux_computeFrame — per-state assertions ───────────────────────────

void test_compute_frame_init() {
  // DEVICE_INIT → LED off, non-empty message
  UxFrame f;
  ux_computeFrame(&f, nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(LED_OFF, f.ledColor);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_OFF, f.ledPattern);
  TEST_ASSERT_TRUE(f.message[0] != '\0');
}

void test_compute_frame_self_test() {
  device_setState(DEVICE_SELF_TEST);
  UxFrame f;
  ux_computeFrame(&f, nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(LED_YELLOW, f.ledColor);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_BLINK_FAST, f.ledPattern);
  TEST_ASSERT_TRUE(f.message[0] != '\0');
}

void test_compute_frame_idle() {
  device_setState(DEVICE_IDLE);
  UxFrame f;
  ux_computeFrame(&f, nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(LED_GREEN, f.ledColor);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_SOLID, f.ledPattern);
}

void test_compute_frame_growing() {
  device_setState(DEVICE_GROWING);
  UxFrame f;
  ux_computeFrame(&f, nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(LED_GREEN, f.ledColor);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_SOLID, f.ledPattern);
}

void test_compute_frame_idle_with_advisory_is_yellow_degraded() {
  // Operatiivinen tila + advisory: KELTAINEN (degraded), ei punainen FAULT.
  device_setState(DEVICE_IDLE);
  device_setAdvisory(DEVICE_FAULT_SENSOR, "Anturi ei vastaa");
  UxFrame f;
  ux_computeFrame(&f, nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(LED_YELLOW, f.ledColor);          // ei GREEN, ei RED
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_SOLID, f.ledPattern); // operatiivinen pattern säilyy
  TEST_ASSERT_TRUE(f.action[0] != '\0');                    // advisory pinnalla
}

void test_compute_frame_fault_no_message() {
  // Transition directly: device_setState won't work from INIT to FAULT
  // unless we use device_setFault which also sets faultMessage.
  // Use setFault with empty message then clear it to test default.
  device_setFault(DEVICE_FAULT_WATER, NULL);
  // Clear the message so the fallback "Vika havaittu" is used.
  g_device.faultMessage[0] = '\0';
  UxFrame f;
  ux_computeFrame(&f, nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(LED_RED, f.ledColor);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_BLINK_FAST, f.ledPattern);
  TEST_ASSERT_EQUAL_STRING("Vika havaittu", f.message);
}

void test_compute_frame_fault_with_message() {
  device_setFault(DEVICE_FAULT_WATER, "Pumppu rikki");
  UxFrame f;
  ux_computeFrame(&f, nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(LED_RED, f.ledColor);
  TEST_ASSERT_EQUAL_STRING("Pumppu rikki", f.message);
}

void test_compute_frame_fault_message_is_copied() {
  // Verify that a longer fault message is stored (up to UX_MESSAGE_LEN-1 chars)
  const char* msg = "Vedenkorkeusanturi ei vastaa";
  device_setFault(DEVICE_FAULT_WATER, msg);
  UxFrame f;
  ux_computeFrame(&f, nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL_STRING(msg, f.message);
}

void test_compute_frame_shutdown() {
  // FAULT → SHUTDOWN is allowed
  device_setFault(DEVICE_FAULT_WATER, "X");
  device_setState(DEVICE_SHUTDOWN);
  UxFrame f;
  ux_computeFrame(&f, nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(LED_OFF, f.ledColor);
}

// ── Askelkonteksti (grow_steps.h): vilkku vain kun on jotain tehtavaa ──

// Basilika + siemen + taimivaihe -> oikea askellista (grow_steps.h, Fable §3.1):
// [0] valmistele BUTTON, [1] liotus TIMER 30 min, [2] kylvo BUTTON,
// [3] idatys BUTTON + 5 vrk kynnys.
static void makeStepFixture(SystemState* st, DeviceConfig* cfg, PlantConfig* pl) {
  memset(st, 0, sizeof(*st));
  memset(cfg, 0, sizeof(*cfg));
  memset(pl, 0, sizeof(*pl));
  strncpy(pl->id, "basil", sizeof(pl->id) - 1);
  pl->phaseCount = 1;
  pl->phases[0].type = GROW_PHASE_SEEDLING;
  cfg->growActive      = true;
  cfg->growPhase       = 0;
  cfg->growStartMethod = 1;
  cfg->growStepIndex   = 0;
  st->growStepStartMs  = 0;
}

void test_compute_frame_timer_step_does_not_blink() {
  // Liotus (index 1) kaynnissa: ei mitaan tehtavaa -> tasainen valo, ei vilkkua.
  device_setState(DEVICE_GROWING);
  SystemState st; DeviceConfig cfg; PlantConfig pl;
  makeStepFixture(&st, &cfg, &pl);
  cfg.growStepIndex = 1;         // liotus (TIMER) — valmistele on nyt index 0
  g_stub_millis = 5 * 60000UL;   // 5 min liotusta kulunut
  UxFrame f;
  ux_computeFrame(&f, &st, &cfg, &pl);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_SOLID, f.ledPattern);
}

void test_compute_frame_button_step_blinks_and_prompts() {
  // Kylvoaskel odottaa nappia -> verkkainen vilkku + kehote.
  device_setState(DEVICE_GROWING);
  SystemState st; DeviceConfig cfg; PlantConfig pl;
  makeStepFixture(&st, &cfg, &pl);
  cfg.growStepIndex = 2;   // kylvo (BUTTON, ei ajastinta) — index +1 valmistele-askeleesta
  UxFrame f;
  ux_computeFrame(&f, &st, &cfg, &pl);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_BLINK_SLOW, f.ledPattern);
  TEST_ASSERT_TRUE(f.action[0] != '\0');
}

void test_compute_frame_germination_blinks_only_after_threshold() {
  // Idatys: paivina 0-5 EI vilkkua (kayttajan valinta 16.7.2026), kynnyksen
  // jalkeen vilkku — "naina paivina sirkkalehdet ilmestyvat".
  device_setState(DEVICE_GROWING);
  SystemState st; DeviceConfig cfg; PlantConfig pl;
  makeStepFixture(&st, &cfg, &pl);
  cfg.growStepIndex = 3;   // idatys (BUTTON + 5 vrk kynnys) — index +1 valmistele-askeleesta

  g_stub_millis = 3UL * 24UL * 3600UL * 1000UL;   // paiva 3
  UxFrame f;
  ux_computeFrame(&f, &st, &cfg, &pl);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_SOLID, f.ledPattern);

  g_stub_millis = 5UL * 24UL * 3600UL * 1000UL;   // paiva 5
  ux_computeFrame(&f, &st, &cfg, &pl);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_BLINK_SLOW, f.ledPattern);
}

void test_compute_frame_finished_sequence_back_to_normal() {
  // Sarja valmis (index == count) -> normaali kasvatusnakyma, ei vilkkua.
  device_setState(DEVICE_GROWING);
  SystemState st; DeviceConfig cfg; PlantConfig pl;
  makeStepFixture(&st, &cfg, &pl);
  cfg.growStepIndex = 4;   // 4 askelta (valmistele+liota+kylva+idata) -> sarja ohi
  UxFrame f;
  ux_computeFrame(&f, &st, &cfg, &pl);
  TEST_ASSERT_EQUAL_UINT8(LED_PATTERN_SOLID, f.ledPattern);
}

// ── ux_frameDiffers ───────────────────────────────────────────────────

void test_frame_differs_false_for_identical() {
  UxFrame a, b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  a.ledColor = LED_GREEN;
  a.ledPattern = LED_PATTERN_SOLID;
  strncpy(a.message, "hello", UX_MESSAGE_LEN - 1);
  b = a;
  TEST_ASSERT_FALSE(ux_frameDiffers(&a, &b));
}

void test_frame_differs_true_when_color_differs() {
  UxFrame a, b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  a.ledColor = LED_GREEN;
  b.ledColor = LED_RED;
  TEST_ASSERT_TRUE(ux_frameDiffers(&a, &b));
}

void test_frame_differs_true_when_message_differs() {
  UxFrame a, b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  a.ledColor = b.ledColor = LED_GREEN;
  a.ledPattern = b.ledPattern = LED_PATTERN_SOLID;
  strncpy(a.message, "teksti A", UX_MESSAGE_LEN - 1);
  strncpy(b.message, "teksti B", UX_MESSAGE_LEN - 1);
  TEST_ASSERT_TRUE(ux_frameDiffers(&a, &b));
}

void test_frame_differs_true_when_a_null() {
  UxFrame b;
  memset(&b, 0, sizeof(b));
  TEST_ASSERT_TRUE(ux_frameDiffers(NULL, &b));
}

void test_frame_differs_true_when_b_null() {
  UxFrame a;
  memset(&a, 0, sizeof(a));
  TEST_ASSERT_TRUE(ux_frameDiffers(&a, NULL));
}

// ── ux_blinkActive ────────────────────────────────────────────────────

void test_blink_active_solid_always_true() {
  TEST_ASSERT_TRUE(ux_blinkActive(LED_PATTERN_SOLID, 0));
  TEST_ASSERT_TRUE(ux_blinkActive(LED_PATTERN_SOLID, 99999));
}

void test_blink_active_off_always_false() {
  TEST_ASSERT_FALSE(ux_blinkActive(LED_PATTERN_OFF, 0));
  TEST_ASSERT_FALSE(ux_blinkActive(LED_PATTERN_OFF, 99999));
}

void test_blink_active_fast_toggles_per_period() {
  // LED_BLINK_FAST_MS = 250 — even multiples are ON, odd multiples are OFF
  unsigned long period = LED_BLINK_FAST_MS;   // 250 ms
  // t=0 → bucket 0 (even) → true
  TEST_ASSERT_TRUE(ux_blinkActive(LED_PATTERN_BLINK_FAST, 0));
  // t=period → bucket 1 (odd) → false
  TEST_ASSERT_FALSE(ux_blinkActive(LED_PATTERN_BLINK_FAST, period));
  // t=2*period → bucket 2 (even) → true
  TEST_ASSERT_TRUE(ux_blinkActive(LED_PATTERN_BLINK_FAST, 2 * period));
}

void test_blink_active_slow_toggles_per_period() {
  // LED_BLINK_SLOW_MS = 1500
  unsigned long period = LED_BLINK_SLOW_MS;
  TEST_ASSERT_TRUE(ux_blinkActive(LED_PATTERN_BLINK_SLOW, 0));
  TEST_ASSERT_FALSE(ux_blinkActive(LED_PATTERN_BLINK_SLOW, period));
  TEST_ASSERT_TRUE(ux_blinkActive(LED_PATTERN_BLINK_SLOW, 2 * period));
}

// ── ux_driveLeds — single-LED fallback (V1 config) ───────────────────
// PIN_LED_GREEN >= 0, PIN_LED_RED < 0, PIN_LED_YELLOW < 0 on XIAO V1.
// We can only verify it doesn't crash; actual GPIO calls are no-ops in stubs.

void test_drive_leds_does_not_crash_in_v1_config() {
  // Exercise all color × pattern × time combos without asserting hardware state.
  ux_driveLeds(LED_OFF,    LED_PATTERN_OFF,        0);
  ux_driveLeds(LED_GREEN,  LED_PATTERN_SOLID,      0);
  ux_driveLeds(LED_GREEN,  LED_PATTERN_BLINK_FAST, 0);
  ux_driveLeds(LED_GREEN,  LED_PATTERN_BLINK_SLOW, 0);
  ux_driveLeds(LED_YELLOW, LED_PATTERN_BLINK_FAST, 0);
  ux_driveLeds(LED_RED,    LED_PATTERN_BLINK_FAST, 0);
  // All no-op, test passes if no crash.
  TEST_ASSERT_TRUE(true);
}

// ── main ──────────────────────────────────────────────────────────────

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_compute_frame_init);
  RUN_TEST(test_compute_frame_self_test);
  RUN_TEST(test_compute_frame_idle);
  RUN_TEST(test_compute_frame_growing);
  RUN_TEST(test_compute_frame_idle_with_advisory_is_yellow_degraded);
  RUN_TEST(test_compute_frame_fault_no_message);
  RUN_TEST(test_compute_frame_fault_with_message);
  RUN_TEST(test_compute_frame_fault_message_is_copied);
  RUN_TEST(test_compute_frame_shutdown);

  RUN_TEST(test_compute_frame_timer_step_does_not_blink);
  RUN_TEST(test_compute_frame_button_step_blinks_and_prompts);
  RUN_TEST(test_compute_frame_germination_blinks_only_after_threshold);
  RUN_TEST(test_compute_frame_finished_sequence_back_to_normal);

  RUN_TEST(test_frame_differs_false_for_identical);
  RUN_TEST(test_frame_differs_true_when_color_differs);
  RUN_TEST(test_frame_differs_true_when_message_differs);
  RUN_TEST(test_frame_differs_true_when_a_null);
  RUN_TEST(test_frame_differs_true_when_b_null);

  RUN_TEST(test_blink_active_solid_always_true);
  RUN_TEST(test_blink_active_off_always_false);
  RUN_TEST(test_blink_active_fast_toggles_per_period);
  RUN_TEST(test_blink_active_slow_toggles_per_period);

  RUN_TEST(test_drive_leds_does_not_crash_in_v1_config);

  return UNITY_END();
}
