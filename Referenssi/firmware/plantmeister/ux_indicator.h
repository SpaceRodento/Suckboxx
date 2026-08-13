/*=====================================================================
  ux_indicator.h - Unified UX driver (LED + e-ink)

  Reads g_device.state and produces visual output via two channels:
    - 3-color LED (green/yellow/red, with blink patterns)
    - E-ink (when ENABLE_EINK_DISPLAY)

  Single source of truth for what the device looks like at any moment.
=====================================================================*/

#ifndef UX_INDICATOR_H
#define UX_INDICATOR_H

#include "config.h"
#include "structs.h"
#include "device_state.h"
#include "eink_display.h"
#include "grow_step_fsm.h"  // askelkonteksti: vilkku kun askel odottaa kuittausta
#include "mcp23017_hal.h"   // LED voi olla laajentimen pinnissä (PIN_IS_ON_MCP)
#include "button_input.h"   // napin reuna -> vahvistusvälähdys (ux_flashAck)

#include <stdio.h>
#include <string.h>

enum LedColor : uint8_t {
  LED_OFF      = 0,
  LED_GREEN    = 1,
  LED_YELLOW   = 2,
  LED_RED      = 3,
};

enum LedPattern : uint8_t {
  LED_PATTERN_OFF        = 0,
  LED_PATTERN_SOLID      = 1,
  LED_PATTERN_BLINK_FAST = 2,
  LED_PATTERN_BLINK_SLOW = 3,
};

#define UX_MESSAGE_LEN 64
#define UX_ACTION_LEN  48

struct UxFrame {
  LedColor   ledColor;
  LedPattern ledPattern;
  char       message[UX_MESSAGE_LEN];
  char       action[UX_ACTION_LEN];
};

#if ENABLE_UX_INDICATOR

// Internal state
static unsigned long g_uxLastTickMs = 0;
static UxFrame g_uxLastFrame;
static bool g_uxFrameValid = false;
static bool g_uxForceRefresh = false;

// Napinpainalluksen vahvistusvälähdys: reunan (ei painettu -> painettu)
// tunnistus ux_tick:ssa asettaa g_uxAckFlashUntilMs:n, ja LED-ajo antaa
// nopeita pulsseja siihen asti. ux_flashAck() sallii saman laukaisun muualta
// (esim. etäkomento) — pura tila -> testattavuus.
static unsigned long g_uxAckFlashUntilMs = 0;
static bool g_uxPrevButtonPressed = false;

// Laukaise vahvistusvälähdys nyt (kesto UX_ACK_FLASH_MS).
inline void ux_flashAck() { g_uxAckFlashUntilMs = millis() + UX_ACK_FLASH_MS; }

// Initialize LED pins. Call once in setup().
void ux_init();

// Compute UxFrame from current device state + optional runtime context.
// Pure function - no side effects. Context pointers may be NULL (tests,
// callers without grow context) -> frame falls back to state-only output.
//
// HUOM: /api/state (wifi_portal_api_state.h) kutsuu TATA suoraan — sama
// funktio tuottaa seka fyysisen LEDin etta API:n ux-lohkon. Jos vilkku-
// logiikka eläisi vain ux_tick:ssa, LED ja seinataulun nakema led_pattern
// eriytyisivat ja e-ink valehtelisi laitteen tilasta.
void ux_computeFrame(UxFrame* out,
                     const SystemState* state,
                     const DeviceConfig* config,
                     const PlantConfig* plant);

// Periodic update: reads g_device, drives LED, refreshes e-ink on change.
// Call every loop(). Internal rate limiting via UX_TICK_INTERVAL_MS.
void ux_tick(const SensorData* sensors,
             const SystemState* state,
             const DeviceConfig* config,
             const PlantConfig* plant);

// Force immediate refresh of all outputs.
void ux_forceRefresh();

// Yksi kirjoituspiste kaikille merkkivaloille. Pin voi olla natiivi-GPIO tai
// laajentimen pinni (MCP_GPIO(n)); kutsujan ei tarvitse tietää kumpi, joten
// väri-/vilkkulogiikka pysyy rautariippumattomana (config.h MCP_PIN_BASE).
//
// Jos laajennin ei vastaa, mcp23017_writePin on no-op ready-lipun takana (§ 8):
// merkkivalo katoaa mutta mikään ei kaadu eikä blokkaa.
static inline void ux_writeLedLevel(int pin, bool level) {
  if (pin < 0) return;
#if ENABLE_MCP23017
  if (PIN_IS_ON_MCP(pin)) {
    mcp23017_writePin((uint8_t)(pin - MCP_PIN_BASE), level);
    return;
  }
#endif
  digitalWrite(pin, level ? HIGH : LOW);
}

static inline void ux_writePin(int pin, bool on) {
  ux_writeLedLevel(pin, LED_ACTIVE_LOW ? !on : on);
}

static inline void ux_writeBtnPin(int pin, bool on) {
  ux_writeLedLevel(pin, BTN_LED_ACTIVE_LOW ? !on : on);
}

static inline bool ux_blinkActive(LedPattern pattern, unsigned long nowMs) {
  switch (pattern) {
    case LED_PATTERN_SOLID:
      return true;
    case LED_PATTERN_BLINK_FAST:
      return ((nowMs / LED_BLINK_FAST_MS) & 1) == 0;
    case LED_PATTERN_BLINK_SLOW:
      return ((nowMs / LED_BLINK_SLOW_MS) & 1) == 0;
    case LED_PATTERN_OFF:
    default:
      return false;
  }
}

// V1 single-LED fallback: when only PIN_LED_GREEN exists (PIN_LED_RED < 0 and
// PIN_LED_YELLOW < 0), map every active color to the green LED so fault/warning
// states still produce a visible blink pattern. Color is conveyed by pattern
// alone (BLINK_FAST = fault, BLINK_SLOW = warning, SOLID = ok).
static inline void ux_driveLeds(LedColor color, LedPattern pattern, unsigned long nowMs) {
  bool on = ux_blinkActive(pattern, nowMs);
  bool singleLedFallback = (PIN_LED_GREEN >= 0) &&
                           (PIN_LED_RED < 0) &&
                           (PIN_LED_YELLOW < 0);
  if (singleLedFallback) {
    ux_writePin(PIN_LED_GREEN, on && color != LED_OFF);
    return;
  }
  ux_writePin(PIN_LED_GREEN,  on && color == LED_GREEN);
  ux_writePin(PIN_LED_YELLOW, on && color == LED_YELLOW);
  ux_writePin(PIN_LED_RED,    on && color == LED_RED);
}

// Button's own integrated LED. Reuses the same state->color/pattern
// machinery as the status LED so we get one source of truth.
//
// Flexible by design (no rewrite when hardware grows):
//   - Green wired, red = -1  → green-only fallback: every active color
//     shows on green, urgency conveyed by pattern (BLINK_FAST = fault).
//     This is the V1 starting point: wire green, it just works.
//   - Green + red wired       → red faults light red, ok states light
//     green, warnings (YELLOW) light both. Adding red is a one-line pin
//     change in config.h, no logic change here.
static inline void ux_driveButtonLed(LedColor color, LedPattern pattern, unsigned long nowMs) {
  if (PIN_BTN_LED_GREEN < 0 && PIN_BTN_LED_RED < 0) return;  // not wired

  bool on = ux_blinkActive(pattern, nowMs);
  bool greenOnly = (PIN_BTN_LED_GREEN >= 0) && (PIN_BTN_LED_RED < 0);

  if (greenOnly) {
    ux_writeBtnPin(PIN_BTN_LED_GREEN, on && color != LED_OFF);
    return;
  }

  // Both colors available (YELLOW = green + red simultaneously).
  bool wantGreen = (color == LED_GREEN) || (color == LED_YELLOW);
  bool wantRed   = (color == LED_RED)   || (color == LED_YELLOW);
  ux_writeBtnPin(PIN_BTN_LED_GREEN, on && wantGreen);
  ux_writeBtnPin(PIN_BTN_LED_RED,   on && wantRed);
}

// Laajentimen pinnien suunta asetetaan kerralla IODIRA:sta (mcp23017_init),
// joten pinMode koskee vain natiivi-GPIO:ita — sitä ei saa kutsua arvolla
// >= MCP_PIN_BASE, joka ei ole olemassa oleva GPIO.
static inline void ux_pinModeOutput(int pin) {
  if (pin < 0) return;
#if ENABLE_MCP23017
  if (PIN_IS_ON_MCP(pin)) return;
#endif
  pinMode(pin, OUTPUT);
}

void ux_init() {
  ux_pinModeOutput(PIN_LED_GREEN);
  ux_pinModeOutput(PIN_LED_YELLOW);
  ux_pinModeOutput(PIN_LED_RED);
  ux_pinModeOutput(PIN_BTN_LED_GREEN);
  ux_pinModeOutput(PIN_BTN_LED_RED);
  ux_writePin(PIN_LED_GREEN, false);
  ux_writePin(PIN_LED_YELLOW, false);
  ux_writePin(PIN_LED_RED, false);
  ux_writeBtnPin(PIN_BTN_LED_GREEN, false);
  ux_writeBtnPin(PIN_BTN_LED_RED, false);
  g_uxLastTickMs = 0;
  g_uxFrameValid = false;
  g_uxForceRefresh = true;
  memset(&g_uxLastFrame, 0, sizeof(g_uxLastFrame));
}

void ux_computeFrame(UxFrame* out,
                     const SystemState* state,
                     const DeviceConfig* config,
                     const PlantConfig* plant) {
  if (!out) return;
  memset(out, 0, sizeof(*out));

  switch (g_device.state) {
    case DEVICE_INIT:
      out->ledColor = LED_OFF;
      out->ledPattern = LED_PATTERN_OFF;
      strncpy(out->message, "K\u00e4ynnistyy...", UX_MESSAGE_LEN - 1);
      break;

    case DEVICE_SELF_TEST:
      out->ledColor = LED_YELLOW;
      out->ledPattern = LED_PATTERN_BLINK_FAST;
      strncpy(out->message, "Tarkistan toiminnot...", UX_MESSAGE_LEN - 1);
      break;

    case DEVICE_IDLE:
      out->ledColor = LED_GREEN;
      out->ledPattern = LED_PATTERN_SOLID;
      strncpy(out->message, "Valmis. L\u00e4het\u00e4 kasvatusk\u00e4sky.", UX_MESSAGE_LEN - 1);
      break;

    case DEVICE_GROWING:
      out->ledColor = LED_GREEN;
      out->ledPattern = LED_PATTERN_SOLID;
      strncpy(out->message, "Kasvatus k\u00e4ynniss\u00e4", UX_MESSAGE_LEN - 1);
      break;

    case DEVICE_FAULT:
      out->ledColor = LED_RED;
      out->ledPattern = LED_PATTERN_BLINK_FAST;
      if (g_device.faultMessage[0] != '\0') {
        strncpy(out->message, g_device.faultMessage, UX_MESSAGE_LEN - 1);
      } else {
        strncpy(out->message, "Vika havaittu", UX_MESSAGE_LEN - 1);
      }
      break;

    case DEVICE_MAINTENANCE:
      // Yellow slow blink: a unique combination (yellow+fast is SELF_TEST).
      // Yellow because the device is neither operating normally (green) nor
      // faulted (red) — the same "attention, but nothing is broken" meaning
      // the advisory color carries elsewhere.
      // The blink is what tells the operator across the room that the lock is
      // still on and the device is waiting for them to finish.
      out->ledColor = LED_YELLOW;
      out->ledPattern = LED_PATTERN_BLINK_SLOW;
      strncpy(out->message, "Huoltotila - pumppu ja moottori lukittu", UX_MESSAGE_LEN - 1);
      strncpy(out->action, "Pidä nappi pohjassa kun olet valmis", UX_ACTION_LEN - 1);
      break;

    case DEVICE_SHUTDOWN:
      out->ledColor = LED_OFF;
      out->ledPattern = LED_PATTERN_OFF;
      strncpy(out->message, "Sammutan turvallisesti...", UX_MESSAGE_LEN - 1);
      break;

    default:
      out->ledColor = LED_RED;
      out->ledPattern = LED_PATTERN_BLINK_SLOW;
      strncpy(out->message, "Tuntematon tila", UX_MESSAGE_LEN - 1);
      break;
  }

  // GROWING-koristelu: kasvin nimi viestiin kun se tunnetaan. Asuu tassa
  // (ei ux_tick:ssa) jotta /api/state:n ux.message on identtinen LEDin
  // rinnalla kulkevan viestin kanssa.
  if (g_device.state == DEVICE_GROWING && plant && plant->name[0] != '\0') {
    snprintf(out->message, UX_MESSAGE_LEN, "%s - kasvatus käynnissä", plant->name);
  }

#if ENABLE_GUIDED_GROWING
  // Askel-overlay (WIZARD: grow-steps): kun aktiivinen askel odottaa
  // kuittausta, napin valo vilkkuu verkkaisesti — "on jotain mihin pitaisi
  // reagoida" (kayttajan muotoilu 16.7.2026). Vilkku EI ole paalla kun askel
  // vain odottaa aikaa (liotus kaynnissa, idatys paivina 0-5): silloin ei
  // ole mitaan tehtavaa, ja turha vilkku opettaisi ohittamaan valon.
  if (g_device.state == DEVICE_GROWING && state && config) {
    GrowStepView sv;
    uint32_t stepElapsedMs = (uint32_t)(millis() - state->growStepStartMs);
    if (growstep_buildView(plant, config, stepElapsedMs, &sv)) {
      if (sv.awaitingUser) {
        out->ledPattern = LED_PATTERN_BLINK_SLOW;
        snprintf(out->message, UX_MESSAGE_LEN, "Askel %u/%u odottaa kuittausta",
                 (unsigned)(sv.index + 1), (unsigned)sv.count);
        strncpy(out->action, "Paina nappia kun valmis", UX_ACTION_LEN - 1);
      } else {
        snprintf(out->message, UX_MESSAGE_LEN, "Askel %u/%u käynnissä",
                 (unsigned)(sv.index + 1), (unsigned)sv.count);
      }
    }
  }
#endif

  // Advisory / safe-mode overlay: in an operational state but a non-blocking
  // condition is active (architecture.md § 8) OR the device booted in safe mode.
  // Distinguish it from BOTH clean-OK (green) and hard FAULT (red blink_fast):
  // show YELLOW, keep the operational pattern, and surface the reason on the
  // action line. A degraded device must never look like a broken one.
  if ((device_hasAdvisory() || device_inSafeMode()) &&
      (g_device.state == DEVICE_IDLE ||
       g_device.state == DEVICE_GROWING)) {
    out->ledColor = LED_YELLOW;
    if (device_inSafeMode() && out->action[0] == '\0') {
      snprintf(out->action, UX_ACTION_LEN, "SAFE MODE: flashaa korjattu firmware");
    } else if (g_device.advisoryMessage[0] != '\0' && out->action[0] == '\0') {
      snprintf(out->action, UX_ACTION_LEN, "Huom: %s", g_device.advisoryMessage);
    }
  }

  out->message[UX_MESSAGE_LEN - 1] = '\0';
  out->action[UX_ACTION_LEN - 1] = '\0';
}

static inline bool ux_frameDiffers(const UxFrame* a, const UxFrame* b) {
  if (!a || !b) return true;
  if (a->ledColor != b->ledColor) return true;
  if (a->ledPattern != b->ledPattern) return true;
  if (strncmp(a->message, b->message, UX_MESSAGE_LEN) != 0) return true;
  if (strncmp(a->action, b->action, UX_ACTION_LEN) != 0) return true;
  return false;
}

void ux_tick(const SensorData* sensors,
             const SystemState* state,
             const DeviceConfig* config,
             const PlantConfig* plant) {
  unsigned long now = millis();

  // Napin reuna (ei painettu -> painettu) joka loopilla, EI tickrajoituksen
  // takana -> vahvistusvälähdys laukeaa heti painalluksen hetkellä.
  bool pressed = button_isPressed();
  if (pressed && !g_uxPrevButtonPressed) {
    g_uxAckFlashUntilMs = now + UX_ACK_FLASH_MS;
  }
  g_uxPrevButtonPressed = pressed;
  bool flashing = (long)(g_uxAckFlashUntilMs - now) > 0;

  // Väläyksen aikana ohitetaan tickrajoitus, jotta pulssi on terävä eikä 100 ms
  // tickiin sidottu.
  if (!flashing && !g_uxForceRefresh && (now - g_uxLastTickMs) < UX_TICK_INTERVAL_MS) return;
  g_uxLastTickMs = now;

  UxFrame frame;
  ux_computeFrame(&frame, state, config, plant);

  if (flashing) {
    // Vahvistuspulssi: nopea on/off nykyvärillä (OFF-tilassa vihreä), ohittaa
    // tilakuvion. E-ink EI päivity väläyksen aikana (sisältö ei muutu).
    bool on = ((now / UX_ACK_FLASH_TOGGLE_MS) & 1UL) == 0;
    LedColor c = (frame.ledColor == LED_OFF) ? LED_GREEN : frame.ledColor;
    LedPattern p = on ? LED_PATTERN_SOLID : LED_PATTERN_OFF;
    ux_driveLeds(c, p, now);
    ux_driveButtonLed(c, p, now);
    (void)sensors;
    return;
  }

  // LED is driven every tick (blink pattern needs steady updates).
  ux_driveLeds(frame.ledColor, frame.ledPattern, now);
  ux_driveButtonLed(frame.ledColor, frame.ledPattern, now);

  // E-ink updates only when the frame changes.
  bool changed = g_uxForceRefresh || !g_uxFrameValid || ux_frameDiffers(&frame, &g_uxLastFrame);
  if (changed) {
#if ENABLE_EINK_DISPLAY
    if (state && state->einkReady) {
      eink_updateFromRuntime(sensors, state, config, plant);
    }
#endif
    g_uxLastFrame = frame;
    g_uxFrameValid = true;
    g_uxForceRefresh = false;
  }

  (void)sensors;
}

void ux_forceRefresh() {
  g_uxForceRefresh = true;
}

#else  // ENABLE_UX_INDICATOR

inline void ux_init() {}
inline void ux_computeFrame(UxFrame*, const SystemState*, const DeviceConfig*,
                            const PlantConfig*) {}
inline void ux_tick(const SensorData*, const SystemState*, const DeviceConfig*, const PlantConfig*) {}
inline void ux_forceRefresh() {}
inline void ux_flashAck() {}

#endif // ENABLE_UX_INDICATOR
#endif // UX_INDICATOR_H
