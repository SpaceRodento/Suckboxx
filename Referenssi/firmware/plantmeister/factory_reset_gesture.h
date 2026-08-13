/*=====================================================================
  factory_reset_gesture.h - Tehdasreset nappi pohjassa kaynnistyksessa

  ── Miksi ele on olemassa vaikka /api/command FACTORY_RESET on jo ─────
  Muna–kana: resetin kaksi tyypillisinta syyta ovat unohtunut salasana ja
  vaara WiFi-verkko. Molemmissa API-reitti on juuri se joka ei toimi —
  se vaatii verkon ja autentikoinnin. API-only-reset ei siis palvele omaa
  paakayttotapaustaan. Fyysinen paasy laitteeseen on oikea valtuutus: se
  toimii ilman verkkoa, ilman salasanaa, ja ilman tietokonetta.

  ── Miksi ele EI rebootaa ────────────────────────────────────────────
  Tama ajetaan setup():ssa ENNEN config_load():ia. Kun tiedostot on
  poistettu, config_load() putoaa oletuksiin aivan itsestaan ja boot
  jatkaa suoraan kayttoonottotilaan. Reboottia ei tarvita — ja se on
  nimenomaan hyva, koska reboot < 60 s OTA:sta rullaisi vahvistamattoman
  imagen takaisin (ks. ota_manager.h). Komentoreitti joutuu vartioimaan
  sita erikseen (command_handler.h ota_isPendingVerify); ele ei joudu,
  koska se ei rebootaa lainkaan.

  ── Miksi tama ei riko boot-robustisuutta (architecture.md § 8, Aukko A) ─
  Sanonta "ei rajattomia blokkaavia kutsuja setup():ssa" koskee odotusta
  jolla ei ole ylarajaa. Tassa on kaksi rajattua kustannusta:
    - Nappi EI pohjassa (normaali boot): ~25 ms, yksi MCP-luku. Sitten pois.
    - Nappi pohjassa: enintaan FACTORY_RESET_HOLD_MS, watchdog ruokitaan
      joka kierroksella, ja vapautus keskeyttaa heti.
  Kumpikaan ei voi jaada jumiin. Jos laajennin puuttuu tai katoaa kesken,
  luku palauttaa "ei painettu" -> ele peruuntuu (turvallinen suunta).

  ── Ele vs. huoltotila ────────────────────────────────────────────────
  Nappi on sama, joten elesta pitaa olla mahdoton vahingossa. Siksi:
    - Pitaa olla pohjassa jo kaynnistyshetkella (ei kesken ajon).
    - FACTORY_RESET_HOLD_MS (10 s) on 2x BUTTON_LONG_PRESS_MS (5 s).
    - Kiihtyva vilkku kertoo koko ajan mita on tapahtumassa, ja irrottaminen
      peruu. Kayttaja ei voi ajautua nollaukseen tietamattaan.
=====================================================================*/

#ifndef FACTORY_RESET_GESTURE_H
#define FACTORY_RESET_GESTURE_H

#include "config.h"

// ── Puhdas osa: aina saatavilla (myos natiivitesteissa) ──────────────

// Vilkun jaksonaika sen mukaan kauanko nappia on pidetty. Lyhenee lineaarisesti
// FACTORY_GESTURE_BLINK_SLOW_MS -> FACTORY_GESTURE_BLINK_FAST_MS kun hold etenee.
//
// Miksi kiihtyva eika tasainen: tasainen vilkku kertoo "jotain tapahtuu",
// kiihtyva kertoo "olet lahella". Kayttaja nakee etenemisen ilman naytto ja
// tietaa milloin irrottaa jos ei halunnutkaan nollata.
inline uint16_t factory_gesture_blinkPeriodMs(uint32_t heldMs, uint32_t requiredMs) {
  if (requiredMs == 0 || heldMs >= requiredMs) return FACTORY_GESTURE_BLINK_FAST_MS;
  const uint32_t span = FACTORY_GESTURE_BLINK_SLOW_MS - FACTORY_GESTURE_BLINK_FAST_MS;
  return (uint16_t)(FACTORY_GESTURE_BLINK_SLOW_MS - (span * heldMs) / requiredMs);
}

#if ENABLE_FACTORY_RESET_GESTURE && ENABLE_BUTTON_INPUT && !defined(PLANTMEISTER_NATIVE_TEST)

#include "factory_reset.h"
#include "button_input.h"     // button_readRaw(), button_suppressUntilRelease()
#include "mcp23017_hal.h"
#include "watchdog.h"
#include <Wire.h>

// Napin oma merkkivalo suoraan, ilman ux_indicator:ia: tama ajetaan ennen
// ux_init():ia eika tilakone ole viela pystyssa. Ainoa poikkeus § 4:n
// "LED-palaute on ux_indicator:n vastuulla" -saantoon, ja se on rajattu
// bootin ensimmaisiin sekunteihin ennen kuin ux_indicator on olemassa.
static inline void factory_gesture_led(bool on) {
  if (PIN_BTN_LED_GREEN < 0) return;
  const bool level = BTN_LED_ACTIVE_LOW ? !on : on;
#if ENABLE_MCP23017
  if (PIN_IS_ON_MCP(PIN_BTN_LED_GREEN)) {
    mcp23017_writePin((uint8_t)(PIN_BTN_LED_GREEN - MCP_PIN_BASE), level);
    return;
  }
#endif
  pinMode(PIN_BTN_LED_GREEN, OUTPUT);
  digitalWrite(PIN_BTN_LED_GREEN, level ? HIGH : LOW);
}

// Aja napin lukukanava pystyyn niin etta button_readRaw() kertoo totuuden.
// Palauttaa false jos kanavaa ei saada -> ele ohitetaan hiljaisesti.
static inline bool factory_gesture_prepareInput() {
#if ENABLE_MCP23017
  if (PIN_IS_ON_MCP(PIN_BUTTON)) {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    if (!mcp23017_init()) {
      DEBUG_INFO(F("Factory gesture: laajenninta ei loydy — ele ohitettu"));
      return false;
    }
    // mcp23017_tick() on rate-limitoitu MCP23017_REFRESH_MS:aan, joten yksi
    // kutsu ei valttamatta lue mitaan. Kaksi kutsua valin yli takaa yhden
    // oikean GPIOA-luvun ennen kuin valimuistiin luotetaan.
    mcp23017_tick(millis());
    delay(MCP23017_REFRESH_MS + 5);
    mcp23017_tick(millis());
    return true;
  }
#endif
  pinMode(PIN_BUTTON, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  return true;
}

// Kutsu setup():ssa LittleFS-mountin jalkeen mutta ENNEN config_load():ia.
// Palauttaa true jos nollaus tehtiin (kutsujan ei tarvitse tehda mitaan —
// config_load() hoitaa loput putoamalla oletuksiin).
inline bool factory_gesture_check() {
  if (!factory_gesture_prepareInput()) return false;

  // Yleisin tapaus: nappi ei ole pohjassa -> ulos heti, ~0 boot-kustannusta.
  if (!button_readRaw()) return false;

  DEBUG_PRINTF("[INFO]  Factory gesture: nappi pohjassa bootissa — pida %u s "
               "nollataksesi, irrota peruaksesi\n",
               (unsigned)(FACTORY_RESET_HOLD_MS / 1000U));

  const uint32_t start = millis();
  uint32_t lastToggleMs = start;
  bool ledOn = false;

  for (;;) {
    watchdog_feed();
#if ENABLE_MCP23017
    if (PIN_IS_ON_MCP(PIN_BUTTON)) mcp23017_tick(millis());
#endif
    const uint32_t now  = millis();
    const uint32_t held = now - start;

    // Vapautus peruu. Kattaa myos laajentimen katoamisen kesken eleen:
    // button_readRaw() palauttaa silloin false -> peruuntuu. Turvallinen
    // suunta — rautavika ei saa nollata laitetta.
    if (!button_readRaw()) {
      factory_gesture_led(false);
      DEBUG_PRINTF("[INFO]  Factory gesture: vapautettu %lu ms kohdalla — normaali boot\n",
                   (unsigned long)held);
      return false;
    }

    if (held >= FACTORY_RESET_HOLD_MS) break;

    const uint16_t period = factory_gesture_blinkPeriodMs(held, FACTORY_RESET_HOLD_MS);
    if ((now - lastToggleMs) >= (uint32_t)(period / 2)) {
      lastToggleMs = now;
      ledOn = !ledOn;
      factory_gesture_led(ledOn);
    }
    delay(5);
  }

  // Tasainen valo: "meni lapi". Nakyy ennen kuin kayttaja irrottaa sormensa,
  // joten han tietaa onnistuiko ilman etta joutuu arvaamaan.
  factory_gesture_led(true);
  delay(FACTORY_GESTURE_CONFIRM_MS);

  const FactoryResetResult r = factory_reset_wipe();
  DEBUG_PRINTF("[INFO]  Factory gesture: NOLLATTU — config %s, growclock %s, "
               "plants %s, kalibrointi SAILYTETTY\n",
               r.configRemoved    ? "poistettu" : "ei ollut",
               r.growClockRemoved ? "poistettu" : "ei ollut",
               r.plantsRemoved    ? "poistettu" : "ei ollut");

  // Valo jaa PALAMAAN tarkoituksella. ux_indicator ottaa sen haltuun vasta
  // ux_init():ssa muutamaa sekuntia myohemmin, ja laite paatyy IDLE:en jossa
  // vihrea palaa muutenkin tasaisesti — nain "tasainen valo" jatkuu yhtenaisena
  // eleen lapimenosta valmiiseen laitteeseen.
  //
  // Tassa sammutettiin valo aiemmin heti wipe:n jalkeen. Rautatestissa
  // 15.7.2026 se osoittautui huonoksi: kayttaja naki vilkun, tasaisen valon, ja
  // sitten pimeyden loppubootin ajan — ja raportoi "ilmeisesti laite boottasi,
  // en tieda". Vahvistusvalo on olemassa juuri siksi ettei tarvitsisi arvata,
  // joten sen sammuttaminen kesken kumosi sen oman tarkoituksen.

  // Nappi on yha pohjassa. Ilman tata button_init() nakisi sen uuden
  // painalluksen alkuna ja BUTTON_LONG_PRESS_MS (5 s) kuluttua laukaisisi
  // huoltotilan — kayttaja saisi juuri nollatun laitteen huoltotilaan
  // pyytamatta, valittomasti nollauksen jalkeen.
  button_suppressUntilRelease();
  return true;
}

#else  // ele pois paalta / natiivitesti

inline bool factory_gesture_check() { return false; }

#endif
#endif // FACTORY_RESET_GESTURE_H
