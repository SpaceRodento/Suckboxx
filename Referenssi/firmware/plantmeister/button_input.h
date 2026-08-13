/*=====================================================================
  button_input.h - Physical button with debounce + long-press detection

  v1 semantics: long press (>= BUTTON_LONG_PRESS_MS) emits a callback.
  Short press is detected but no action yet (reserved for user testing).

  Polling-based, no interrupts - keeps timing predictable.
=====================================================================*/

#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include "config.h"
#include "mcp23017_hal.h"   // nappi voi olla laajentimen pinnissä (PIN_IS_ON_MCP)

#if ENABLE_BUTTON_INPUT

enum ButtonEvent : uint8_t {
  BUTTON_EVENT_NONE        = 0,
  BUTTON_EVENT_SHORT_PRESS = 1,
  BUTTON_EVENT_LONG_PRESS  = 2,
};

typedef void (*ButtonEventCallback)(ButtonEvent event);

// Internal state
static bool g_buttonRawLast = false;
static bool g_buttonStable = false;
static unsigned long g_buttonRawChangeMs = 0;
static unsigned long g_buttonPressStartMs = 0;
static unsigned long g_buttonLastPollMs = 0;
static bool g_buttonLongFired = false;
static bool g_buttonSuppressUntilRelease = false;
static ButtonEventCallback g_buttonCallback = NULL;

inline void button_setCallback(ButtonEventCallback cb) {
  g_buttonCallback = cb;
}

inline bool button_isPressed() {
  return g_buttonStable;
}

// Ohita nykyinen painallus kokonaan: ala tuota siita tapahtumia, ala edes
// aloita long-press-kelloa. Normaali toiminta palaa heti kun nappi on kerran
// nahty vapautettuna.
//
// Miksi: nappi voi olla jo pohjassa kun tama moduuli havahtuu — tyypillisesti
// kaynnistyksen tehdasreset-eleen jaljilta (factory_reset_gesture.h), jossa
// sita on pidetty 10 s. FSM alempana tulkitsisi sen UUDEN painalluksen alkuna
// ja laukaisisi BUTTON_LONG_PRESS_MS:n (5 s) kuluttua huoltotilan. Kayttaja
// saisi juuri nollatun laitteen huoltotilaan pyytamatta, valittomasti sen
// jalkeen kun han onnistui nollaamaan sen. Sama koskee ketaa tahansa joka
// vain sattuu pitamaan sormea napilla kun virrat kytketaan.
inline void button_suppressUntilRelease() {
  g_buttonSuppressUntilRelease = true;
}

// Onko napin lukukanava käytettävissä juuri nyt. Natiivi-GPIO on aina; laajentimen
// takana oleva nappi ei ole, jos laajennin on kadonnut väylältä (§ 8).
static inline bool button_inputAvailable() {
#if ENABLE_MCP23017
  if (PIN_IS_ON_MCP(PIN_BUTTON)) return mcp23017_isReady();
#endif
  return true;
}

static inline bool button_readRaw() {
  int level;
#if ENABLE_MCP23017
  if (PIN_IS_ON_MCP(PIN_BUTTON)) {
    // Laajentimen pinni luetaan välimuistista, ei väylältä: mcp23017_tick()
    // näytteistää PORTA:n MCP23017_REFRESH_MS välein (20 ms < debounce 30 ms).
    // Näin nappi ei aja I2C-liikennettä omalla 10 ms pollillaan.
    //
    // Jos laajennin ei vastaa, välimuisti jäisi viimeiseen lukemaan — palautetaan
    // sen sijaan "ei painettu". Kadonnut laajennin ei siis voi jättää nappia
    // jumiin painetuksi ja laukaista huoltotilaa tai kasvatuksen aloitusta (§ 8).
    if (!mcp23017_isReady()) return false;
    level = mcp23017_cachedPin((uint8_t)(PIN_BUTTON - MCP_PIN_BASE));
  } else
#endif
  {
    level = digitalRead(PIN_BUTTON);
  }
  bool pressed = BUTTON_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
  return pressed;
}

// Sijaitsee button_readRaw():n JALKEEN, koska se lukee napin kerran alustuksessa
// (alla) — C++ vaatii maarittelyn ennen kayttoa header-only-moduulissa.
inline void button_init() {
  // Laajentimella suunta ja pull-up tulevat IODIRA/GPPUA:sta (mcp23017_init),
  // eikä PIN_BUTTON ole natiivi-GPIO — pinMode kutsutaan vain oikealle GPIO:lle.
#if ENABLE_MCP23017
  if (!PIN_IS_ON_MCP(PIN_BUTTON))
#endif
    pinMode(PIN_BUTTON, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  g_buttonRawLast = false;
  g_buttonStable = false;
  g_buttonRawChangeMs = 0;
  g_buttonPressStartMs = 0;
  g_buttonLastPollMs = 0;
  g_buttonLongFired = false;

  // HUOM: g_buttonSuppressUntilRelease EI nollaudu tassa, toisin kuin muu tila.
  // Bootin tehdasreset-ele ajetaan ENNEN tata ja se tietaa varmuudella etta
  // nappi oli pohjassa; sen tietoa ei saa pyyhkia pois. Alla oleva luku vain
  // lisaa saman suojan niille poluille joilla ele ei ollut kaynnissa (esim.
  // ENABLE_FACTORY_RESET_GESTURE=false, mutta sormi silti napilla bootissa).
#if ENABLE_MCP23017
  if (PIN_IS_ON_MCP(PIN_BUTTON)) {
    // Tuore lukema ennen paatosta: valimuisti on init-tilassa 0xFF ("ei
    // painettu"), mika olisi vale juuri silloin kun sormi on napilla.
    mcp23017_tick(millis());
  }
#endif
  if (button_readRaw()) g_buttonSuppressUntilRelease = true;
}

inline void button_tick() {
  unsigned long now = millis();
  if ((now - g_buttonLastPollMs) < BUTTON_POLL_INTERVAL_MS) return;
  g_buttonLastPollMs = now;

  // Lukukanava kadonnut (laajennin irti väylältä) — unohda kesken oleva painallus
  // hiljaisesti sen sijaan että raportoisimme vapautuksen.
  //
  // Miksi: FSM alempana tulkitsee "ei enää painettuna" vapautukseksi ja lähettää
  // SHORT_PRESS:in. Jos laajennin katoaa juuri kun nappia pidetään pohjassa, se
  // tapahtuma olisi erottamaton oikeasta näpäytyksestä — ja lyhyt paine aloittaa
  // kasvatuksen (IDLE) tai kuittaa vian (FAULT). Rautavika ei saa syöttää
  // käyttäjäkomentoja, joita kukaan ei antanut.
  if (!button_inputAvailable()) {
    g_buttonRawLast   = false;
    g_buttonStable    = false;
    g_buttonLongFired = false;
    return;
  }

  bool raw = button_readRaw();

  // Nappi oli jo pohjassa kun tama moduuli havahtui (ks. button_suppressUntilRelease).
  // Odota vapautus ennen kuin FSM saa nahda mitaan: muuten jo kaynnissa oleva
  // painallus naytettaisiin uutena ja tuottaisi tapahtuman jota kukaan ei antanut.
  if (g_buttonSuppressUntilRelease) {
    if (!raw) g_buttonSuppressUntilRelease = false;   // vapautettu -> nappi elava taas
    g_buttonRawLast   = raw;
    g_buttonStable    = false;
    g_buttonLongFired = false;
    return;
  }

  if (raw != g_buttonRawLast) {
    g_buttonRawLast = raw;
    g_buttonRawChangeMs = now;
  }

  // Promote raw -> stable after debounce period
  if (raw != g_buttonStable && (now - g_buttonRawChangeMs) >= BUTTON_DEBOUNCE_MS) {
    bool wasPressed = g_buttonStable;
    g_buttonStable = raw;

    if (g_buttonStable && !wasPressed) {
      // Press start
      g_buttonPressStartMs = now;
      g_buttonLongFired = false;
    } else if (!g_buttonStable && wasPressed) {
      // Release: emit short-press if long-press not fired
      if (!g_buttonLongFired && g_buttonCallback) {
        g_buttonCallback(BUTTON_EVENT_SHORT_PRESS);
      }
    }
  }

  // While held, check for long-press threshold
  if (g_buttonStable && !g_buttonLongFired &&
      (now - g_buttonPressStartMs) >= BUTTON_LONG_PRESS_MS) {
    g_buttonLongFired = true;
    if (g_buttonCallback) {
      g_buttonCallback(BUTTON_EVENT_LONG_PRESS);
    }
  }
}

#else  // ENABLE_BUTTON_INPUT

enum ButtonEvent : uint8_t {
  BUTTON_EVENT_NONE = 0,
  BUTTON_EVENT_SHORT_PRESS = 1,
  BUTTON_EVENT_LONG_PRESS = 2,
};
typedef void (*ButtonEventCallback)(ButtonEvent);

inline void button_setCallback(ButtonEventCallback) {}
inline void button_init() {}
inline void button_tick() {}
inline bool button_isPressed() { return false; }
inline void button_suppressUntilRelease() {}

#endif // ENABLE_BUTTON_INPUT
#endif // BUTTON_INPUT_H
