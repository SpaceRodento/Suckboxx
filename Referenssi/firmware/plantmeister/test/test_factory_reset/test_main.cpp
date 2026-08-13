/*=====================================================================
  test_factory_reset - Tehdasresetin jaettu ydin + bootin eleen matematiikka

  Kattaa kaksi moduulia jotka kuuluvat yhteen:

    factory_reset.h          - MITKA tiedostot nollaus poistaa. Tama on
                               suiten tarkein osa: lista on turvatietoa
                               (calibration.json JAA), ja silla on nyt KAKSI
                               kutsujaa — /api/command FACTORY_RESET ja
                               nappi pohjassa bootissa. Testi lukitsee
                               sopimuksen jotta reitit eivat paase
                               eriytymaan toisistaan.

    factory_reset_gesture.h  - Eleen puhdas osa (vilkun kiihtyvyys). Itse
                               I/O-vuo (I2C, hold-silmukka, LittleFS) on
                               rautatestattava, ei natiivi.

  Ele ja huoltotila jakavat saman fyysisen napin, joten niiden kynnysten
  erillaan pysyminen on regressiovartioitu alla.
=====================================================================*/

#include <unity.h>

// Feature flags BEFORE includes.
#define ENABLE_FACTORY_RESET_GESTURE true
#define ENABLE_BUTTON_INPUT          true
#include "test/stubs/test_feature_flags.h"

#include "config.h"
#include "factory_reset.h"
#include "factory_reset_gesture.h"

void setUp(void) {
  LittleFS.format();
}

void tearDown(void) {}

// Laita LittleFS:aan ne tiedostot jotka oikealla, kalibroidulla laitteella olisi.
static void seedFilesystem(void) {
  LittleFS.open(PATH_DEVICE_CONFIG, "w").close();
  LittleFS.open(PATH_GROW_CLOCK, "w").close();
  LittleFS.open(PATH_PLANTS_DB, "w").close();
  LittleFS.open(PATH_CALIBRATION, "w").close();
}

// ── 1. Jaettu tiedostolista ──────────────────────────────────────────

// Suiten ydin. Kalibrointia EI voi tuottaa uudelleen ilman ihmista ja
// mittalaitetta, ja moottorin rajojen menetys on lisaksi vaarallista koska
// hissilla ei ole homingia. Jos tama testi kaatuu, nollaus on muuttunut
// laitteen rikkovaksi.
void test_wipe_removes_regenerable_files_but_keeps_calibration(void) {
  seedFilesystem();

  const FactoryResetResult r = factory_reset_wipe();

  TEST_ASSERT_TRUE(r.configRemoved);
  TEST_ASSERT_TRUE(r.growClockRemoved);
  TEST_ASSERT_TRUE(r.plantsRemoved);

  TEST_ASSERT_FALSE(LittleFS.littlefs_has(PATH_DEVICE_CONFIG));
  TEST_ASSERT_FALSE(LittleFS.littlefs_has(PATH_GROW_CLOCK));
  TEST_ASSERT_FALSE(LittleFS.littlefs_has(PATH_PLANTS_DB));

  TEST_ASSERT_TRUE(LittleFS.littlefs_has(PATH_CALIBRATION));
}

// plants.json yliajaa firmwaren sisaanrakennetut oletukset. Jos se jaa,
// firmwareen korjatut ohjetekstit eivat koskaan tule laitteelle — todettu
// kantapaan kautta 15.7.2026. Oma testi koska tama on helppo "optimoida" pois
// luulemalla plants.jsonia kayttajan dataksi.
void test_wipe_removes_plants_db_so_firmware_defaults_can_regenerate(void) {
  seedFilesystem();
  factory_reset_wipe();
  TEST_ASSERT_FALSE(LittleFS.littlefs_has(PATH_PLANTS_DB));
}

// Puuttuva tiedosto on normaali tulos (growclock puuttuu jos kasvatusta ei ole
// koskaan aloitettu), ei virhe — nollaus ei saa kaatua tuoreella laitteella.
void test_wipe_on_empty_filesystem_reports_nothing_removed(void) {
  const FactoryResetResult r = factory_reset_wipe();

  TEST_ASSERT_FALSE(r.configRemoved);
  TEST_ASSERT_FALSE(r.growClockRemoved);
  TEST_ASSERT_FALSE(r.plantsRemoved);
}

// Nollaus on idempotentti: toinen ajo ei saa kaatua eika koskea kalibrointiin.
void test_wipe_is_idempotent(void) {
  seedFilesystem();
  factory_reset_wipe();
  const FactoryResetResult second = factory_reset_wipe();

  TEST_ASSERT_FALSE(second.configRemoved);
  TEST_ASSERT_TRUE(LittleFS.littlefs_has(PATH_CALIBRATION));
}

// ── 2. Eleen vilkku: kiihtyy kun hold etenee ─────────────────────────

void test_blink_starts_slow_and_ends_fast(void) {
  TEST_ASSERT_EQUAL_UINT16(
    FACTORY_GESTURE_BLINK_SLOW_MS,
    factory_gesture_blinkPeriodMs(0, FACTORY_RESET_HOLD_MS));
  TEST_ASSERT_EQUAL_UINT16(
    FACTORY_GESTURE_BLINK_FAST_MS,
    factory_gesture_blinkPeriodMs(FACTORY_RESET_HOLD_MS, FACTORY_RESET_HOLD_MS));
}

// Kiihtyvyys on koko palautteen idea: kayttaja nakee etenemisen ilman naytto ja
// tietaa milloin irrottaa jos ei sittenkaan halunnut nollata. Jakson pitaa siis
// lyhentya monotonisesti, ei heilua.
void test_blink_accelerates_monotonically(void) {
  uint16_t prev = factory_gesture_blinkPeriodMs(0, FACTORY_RESET_HOLD_MS);
  for (uint32_t held = 250; held <= FACTORY_RESET_HOLD_MS; held += 250) {
    const uint16_t p = factory_gesture_blinkPeriodMs(held, FACTORY_RESET_HOLD_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(prev, p);
    prev = p;
  }
  TEST_ASSERT_EQUAL_UINT16(FACTORY_GESTURE_BLINK_FAST_MS, prev);
}

// Kayttaja voi pitaa nappia viela senkin jalkeen kun ele on lauennut.
void test_blink_clamps_to_fast_floor_when_held_past_threshold(void) {
  TEST_ASSERT_EQUAL_UINT16(
    FACTORY_GESTURE_BLINK_FAST_MS,
    factory_gesture_blinkPeriodMs(FACTORY_RESET_HOLD_MS * 3, FACTORY_RESET_HOLD_MS));
}

void test_blink_handles_zero_required_without_dividing_by_zero(void) {
  TEST_ASSERT_EQUAL_UINT16(FACTORY_GESTURE_BLINK_FAST_MS,
                           factory_gesture_blinkPeriodMs(0, 0));
}

// ── 3. Ele vs. huoltotila: sama nappi, eri kynnys ────────────────────

// Molemmat elettä ovat "pida nappia pohjassa". Ainoa asia joka erottaa ne on
// kesto, joten kynnysten pitaa pysya selvasti erillaan. Jos joku laskee
// FACTORY_RESET_HOLD_MS:aa lahelle BUTTON_LONG_PRESS_MS:aa, huoltotilaan
// menija voi nollata laitteensa vahingossa.
void test_gesture_hold_is_clearly_longer_than_maintenance_long_press(void) {
  TEST_ASSERT_GREATER_THAN_UINT32(BUTTON_LONG_PRESS_MS, FACTORY_RESET_HOLD_MS);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(BUTTON_LONG_PRESS_MS * 2, FACTORY_RESET_HOLD_MS);
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_wipe_removes_regenerable_files_but_keeps_calibration);
  RUN_TEST(test_wipe_removes_plants_db_so_firmware_defaults_can_regenerate);
  RUN_TEST(test_wipe_on_empty_filesystem_reports_nothing_removed);
  RUN_TEST(test_wipe_is_idempotent);

  RUN_TEST(test_blink_starts_slow_and_ends_fast);
  RUN_TEST(test_blink_accelerates_monotonically);
  RUN_TEST(test_blink_clamps_to_fast_floor_when_held_past_threshold);
  RUN_TEST(test_blink_handles_zero_required_without_dividing_by_zero);

  RUN_TEST(test_gesture_hold_is_clearly_longer_than_maintenance_long_press);

  return UNITY_END();
}
