/*=====================================================================
  test_button_input_mcp - Käyttäjänappi MCP23017-laajentimen takana (PCB v2)

  Kattaa sen mitä 15.7.2026 johdotettiin: BTN_USER = GPA5, vihreä
  merkkivalo = GPA3. Suite vartioi kolmea asiaa:

    1. Reititys — PIN_BUTTON/PIN_BTN_LED_GREEN osoittavat laajentimelle
       eikä natiivi-GPIO:hon, eikä natiivi-GPIO voi mennä sekaisin
       laajenninpinnin kanssa (MCP_PIN_BASE-koodaus, config.h).
    2. Näytteenotto — nappi lukee mcp23017_tick():n välimuistia, ei
       väylää, ja debounce toimii sen päällä.
    3. Turva (§ 8) — kadonnut laajennin ei saa syöttää käyttäjäkomentoja.
       Tämä on suiten tärkein osa: lyhyt paine aloittaa kasvatuksen tai
       kuittaa vian, joten haamupainallus rautaviasta olisi oikea bugi.
=====================================================================*/

#include <unity.h>

// Feature flags BEFORE includes.
#define ENABLE_MCP23017     true
#define ENABLE_BUTTON_INPUT true
#include "test/stubs/test_feature_flags.h"

#include "config.h"
#include "mcp23017_hal.h"
#include "button_input.h"

#define REG_GPIOA 0x12

// Viimeisin nappitapahtuma callbackista.
static ButtonEvent g_lastEvent  = BUTTON_EVENT_NONE;
static int         g_eventCount = 0;

static void testButtonCallback(ButtonEvent e) {
  g_lastEvent = e;
  g_eventCount++;
}

// Aseta GPA5 (nappi) tasolle ja päivitä välimuisti tick():llä.
// ACTIVE_LOW: painettu = LOW = bitti 0.
static void setButtonLevel(bool pressed, uint32_t atMs) {
  uint8_t porta = pressed ? (uint8_t)~(1 << MCP_PIN_BTN_USER) : 0xFF;
  Wire.wire_setReg(I2C_ADDR_MCP23017, REG_GPIOA, porta);
  mcp23017_tick(atMs);
}

// Aja nappipollia annetun ajan yli ilman tason muutosta.
static void advanceTo(uint32_t targetMs) {
  g_stub_millis = targetMs;
  button_tick();
}

void setUp(void) {
  mcp23017_resetForTest();
  Wire.wire_reset();
  g_wire_endTransmission_result = 0;
  g_stub_millis = 0;
  g_lastEvent  = BUTTON_EVENT_NONE;
  g_eventCount = 0;
  button_init();
  button_setCallback(testButtonCallback);
}

void tearDown(void) {}

// Nosta laajennin pystyyn ja vakauta nappi vapautetuksi.
static void bringExpanderUp(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  TEST_ASSERT_TRUE(mcp23017_init());
  setButtonLevel(false, 0);
}

// ── 1. Reititys ──────────────────────────────────────────────────────

void test_button_pin_is_routed_to_expander(void) {
  TEST_ASSERT_EQUAL_INT(MCP_GPIO(MCP_PIN_BTN_USER), PIN_BUTTON);
  TEST_ASSERT_TRUE(PIN_IS_ON_MCP(PIN_BUTTON));
}

void test_green_led_is_routed_to_expander(void) {
  TEST_ASSERT_EQUAL_INT(MCP_GPIO(MCP_PIN_LED_G), PIN_BTN_LED_GREEN);
  TEST_ASSERT_TRUE(PIN_IS_ON_MCP(PIN_BTN_LED_GREEN));
}

// Kolmen valon nappi ei ole vielä tullut — punainen pysyy johdottamattomana,
// jolloin ux_indicator ajaa green-only-fallbackia.
void test_red_led_still_unwired(void) {
  TEST_ASSERT_EQUAL_INT(-1, PIN_BTN_LED_RED);
}

// Suojaa koodauksen ydinriskiltä: natiivi-GPIO ei saa näyttää laajenninpinniltä.
// ESP32-S3:n GPIO-avaruus on 0..48, MCP_PIN_BASE on 100.
void test_native_gpio_is_never_mistaken_for_expander_pin(void) {
  TEST_ASSERT_FALSE(PIN_IS_ON_MCP(0));
  TEST_ASSERT_FALSE(PIN_IS_ON_MCP(9));    // V1:n nappi D10
  TEST_ASSERT_FALSE(PIN_IS_ON_MCP(48));   // ESP32-S3:n suurin GPIO
}

// "-1 = ei johdotettu" -sopimus säilyy koodauksen rinnalla.
void test_unwired_pin_is_neither_native_nor_expander(void) {
  TEST_ASSERT_FALSE(PIN_IS_ON_MCP(-1));
}

// ── 2. Näytteenotto välimuistista ────────────────────────────────────

// Välimuistin päivitysväli on napin todellinen näytteenottotaajuus, joten
// sen on oltava debouncea tiheämpi tai nopeat näpäytykset katoavat.
void test_refresh_is_faster_than_debounce(void) {
  TEST_ASSERT_LESS_THAN_UINT32(BUTTON_DEBOUNCE_MS, MCP23017_REFRESH_MS);
}

void test_button_not_pressed_when_expander_reports_high(void) {
  bringExpanderUp();
  TEST_ASSERT_FALSE(button_readRaw());
}

void test_button_pressed_when_expander_reports_low(void) {
  bringExpanderUp();
  setButtonLevel(true, 100);
  TEST_ASSERT_TRUE(button_readRaw());
}

void test_short_press_emits_short_event(void) {
  bringExpanderUp();
  advanceTo(100);

  setButtonLevel(true, 200);
  advanceTo(200);
  advanceTo(240);            // > debounce (30 ms) -> vakaa painallus
  TEST_ASSERT_TRUE(button_isPressed());

  setButtonLevel(false, 400);
  advanceTo(400);
  advanceTo(440);            // > debounce -> vapautus

  TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_SHORT_PRESS, g_lastEvent);
  TEST_ASSERT_EQUAL_INT(1, g_eventCount);
}

// Pitkä paine = huoltotilan ele (button_intent_map.h).
void test_long_press_emits_long_event(void) {
  bringExpanderUp();
  advanceTo(100);

  setButtonLevel(true, 200);
  advanceTo(200);
  advanceTo(240);
  advanceTo(240 + BUTTON_LONG_PRESS_MS);

  TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_LONG_PRESS, g_lastEvent);
}

// ── 3. Turva: kadonnut laajennin ei syötä komentoja (§ 8) ────────────

void test_button_reads_not_pressed_when_expander_absent(void) {
  // Ei init:iä -> ready = false. Välimuisti on 0xFF, mutta lukua ei
  // saa tehdä lainkaan kun laite ei vastaa.
  TEST_ASSERT_FALSE(mcp23017_isReady());
  TEST_ASSERT_FALSE(button_readRaw());
  TEST_ASSERT_FALSE(button_inputAvailable());
}

// Suiten ydin: laajennin katoaa kesken painalluksen -> EI haamu-SHORT_PRESSiä.
// Ilman button_tick():n saatavuustarkistusta FSM tulkitsisi tämän vapautukseksi
// ja aloittaisi kasvatuksen tai kuittaisi vian ilman että kukaan painoi mitään.
void test_expander_lost_mid_press_does_not_emit_phantom_short_press(void) {
  bringExpanderUp();
  advanceTo(100);

  setButtonLevel(true, 200);
  advanceTo(200);
  advanceTo(240);
  TEST_ASSERT_TRUE(button_isPressed());
  TEST_ASSERT_EQUAL_INT(0, g_eventCount);   // pito ei vielä tuota tapahtumaa

  // Laajennin irtoaa väylältä kesken pidon: luku epäonnistuu -> "lost".
  g_wire_endTransmission_result = 2;        // NACK kaikille osoitteille
  Wire.wire_clearAckAddress();
  mcp23017_tick(300);
  TEST_ASSERT_FALSE(mcp23017_isReady());

  advanceTo(340);
  advanceTo(380);

  TEST_ASSERT_EQUAL_INT(0, g_eventCount);
  TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, g_lastEvent);
  TEST_ASSERT_FALSE(button_isPressed());
}

// Kadonnut laajennin ei myöskään saa kypsyttää pitkää painallusta
// (= huoltotilan päälle/pois -ele) taustalla.
void test_expander_lost_mid_press_does_not_emit_phantom_long_press(void) {
  bringExpanderUp();
  advanceTo(100);

  setButtonLevel(true, 200);
  advanceTo(200);
  advanceTo(240);

  g_wire_endTransmission_result = 2;
  Wire.wire_clearAckAddress();
  mcp23017_tick(300);

  advanceTo(300 + BUTTON_LONG_PRESS_MS + 100);

  TEST_ASSERT_EQUAL_INT(0, g_eventCount);
  TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, g_lastEvent);
}

// Laajentimen palattua nappi toimii taas normaalisti (re-detect, § 8).
void test_button_works_again_after_expander_returns(void) {
  bringExpanderUp();
  g_wire_endTransmission_result = 2;
  Wire.wire_clearAckAddress();
  mcp23017_tick(100);
  TEST_ASSERT_FALSE(mcp23017_isReady());

  // Laite palaa väylälle; tick re-probaa (>= 2 s tauko) ja alustaa uudelleen.
  g_wire_endTransmission_result = 0;
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  mcp23017_tick(3000);
  TEST_ASSERT_TRUE(mcp23017_isReady());

  setButtonLevel(false, 3100);
  advanceTo(3100);
  setButtonLevel(true, 3200);
  advanceTo(3200);
  advanceTo(3240);
  TEST_ASSERT_TRUE(button_isPressed());

  setButtonLevel(false, 3400);
  advanceTo(3400);
  advanceTo(3440);
  TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_SHORT_PRESS, g_lastEvent);
}

// ── 4. Nappi pohjassa kun moduuli havahtuu (bootin tehdasreset-ele) ──
//
// Ele (factory_reset_gesture.h) vaatii napin pohjassa 10 s bootissa. Kun se on
// ohi, sormi on yhä napilla ja button_init() ajetaan vasta sen jälkeen. Ilman
// suppressiä FSM näkisi sen UUTENA painalluksena ja laukaisisi
// BUTTON_LONG_PRESS_MS:n (5 s) kuluttua huoltotilan — käyttäjä saisi juuri
// nollatun laitteen huoltotilaan pyytämättä. Sama koskee ketä tahansa joka vain
// pitää sormea napilla kun virrat kytketään.

// Alusta moduuli tilanteessa jossa nappi on jo pohjassa.
static void initWithButtonHeld(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  TEST_ASSERT_TRUE(mcp23017_init());
  setButtonLevel(true, 50);            // >= MCP23017_REFRESH_MS -> oikea luku
  TEST_ASSERT_TRUE(button_readRaw());  // stub tosiaan sanoo "painettu"

  g_stub_millis = 100;
  button_init();                       // lukee napin -> suppress päälle
  button_setCallback(testButtonCallback);
}

void test_button_held_at_init_does_not_emit_long_press(void) {
  initWithButtonHeld();

  setButtonLevel(true, 200);
  advanceTo(200);
  advanceTo(240);                                  // > debounce
  advanceTo(300 + BUTTON_LONG_PRESS_MS);           // > huoltotilan kynnys
  advanceTo(400 + BUTTON_LONG_PRESS_MS);

  TEST_ASSERT_EQUAL_INT(0, g_eventCount);
  TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, g_lastEvent);
  TEST_ASSERT_FALSE(button_isPressed());
}

// Vapautus ei myöskään saa tuottaa SHORT_PRESSiä (= aloittaisi kasvatuksen
// IDLE-tilassa) — koko painallus on ohitettava, ei vain sen pito.
void test_button_held_at_init_does_not_emit_short_press_on_release(void) {
  initWithButtonHeld();

  setButtonLevel(true, 200);
  advanceTo(200);
  advanceTo(240);

  setButtonLevel(false, 400);
  advanceTo(400);
  advanceTo(440);

  TEST_ASSERT_EQUAL_INT(0, g_eventCount);
  TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, g_lastEvent);
}

// Suppress purkautuu vapautuksesta: seuraava aito painallus toimii normaalisti.
// Ilman tätä nappi jäisi kuolleeksi eleen jälkeen.
void test_button_is_live_again_after_release(void) {
  initWithButtonHeld();

  setButtonLevel(false, 400);
  advanceTo(400);                      // vapautus nähty -> suppress pois

  setButtonLevel(true, 500);
  advanceTo(500);
  advanceTo(540);
  TEST_ASSERT_TRUE(button_isPressed());

  setButtonLevel(false, 700);
  advanceTo(700);
  advanceTo(740);

  TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_SHORT_PRESS, g_lastEvent);
  TEST_ASSERT_EQUAL_INT(1, g_eventCount);
}

// Ele kutsuu button_suppressUntilRelease():a suoraan, koska se TIETÄÄ napin
// olleen pohjassa 10 s — sitä tietoa ei saa menettää vaikka button_init():n
// oma luku sattuisi epäonnistumaan (laajennin hikkaa juuri sillä hetkellä).
void test_explicit_suppress_blocks_events_until_release(void) {
  bringExpanderUp();
  advanceTo(100);

  button_suppressUntilRelease();

  setButtonLevel(true, 200);
  advanceTo(200);
  advanceTo(240);
  advanceTo(300 + BUTTON_LONG_PRESS_MS);
  TEST_ASSERT_EQUAL_INT(0, g_eventCount);

  setButtonLevel(false, 6000);
  advanceTo(6000);                     // vapautus -> elävä taas
  setButtonLevel(true, 6100);
  advanceTo(6100);
  advanceTo(6140);
  TEST_ASSERT_TRUE(button_isPressed());
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_button_pin_is_routed_to_expander);
  RUN_TEST(test_green_led_is_routed_to_expander);
  RUN_TEST(test_red_led_still_unwired);
  RUN_TEST(test_native_gpio_is_never_mistaken_for_expander_pin);
  RUN_TEST(test_unwired_pin_is_neither_native_nor_expander);

  RUN_TEST(test_refresh_is_faster_than_debounce);
  RUN_TEST(test_button_not_pressed_when_expander_reports_high);
  RUN_TEST(test_button_pressed_when_expander_reports_low);
  RUN_TEST(test_short_press_emits_short_event);
  RUN_TEST(test_long_press_emits_long_event);

  RUN_TEST(test_button_reads_not_pressed_when_expander_absent);
  RUN_TEST(test_expander_lost_mid_press_does_not_emit_phantom_short_press);
  RUN_TEST(test_expander_lost_mid_press_does_not_emit_phantom_long_press);
  RUN_TEST(test_button_works_again_after_expander_returns);

  RUN_TEST(test_button_held_at_init_does_not_emit_long_press);
  RUN_TEST(test_button_held_at_init_does_not_emit_short_press_on_release);
  RUN_TEST(test_button_is_live_again_after_release);
  RUN_TEST(test_explicit_suppress_blocks_events_until_release);

  return UNITY_END();
}
