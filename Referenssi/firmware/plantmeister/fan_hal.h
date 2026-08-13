/*=====================================================================
  fan_hal.h - Ventilation fan control + health monitoring (PCB v2)

  One native pin drives the fan speed: PIN_FAN_PWM (D6/GPIO43), a 25 kHz
  LEDC PWM that feeds both Q3's gate (3-wire power PWM) and J12 pin 4
  (4-wire dedicated PWM). The SB1 jumper (FAN_4WIRE) selects the wiring;
  the firmware drives the same LEDC channel for both (pcb_v2_layout.md § 2).

  WHY health monitoring matters: because we set a *target speed* with PWM,
  it is essential to verify the fan actually responds. A coarse tacho
  (spins / does not spin) read from the MCP23017 (GPA0, interrupt-on-change
  polled over I2C) is enough — precise RPM is not required. A stall is
  ADVISORY only (DEVICE_FAULT_FAN): a stuck fan degrades VPD control but is
  not a safety fault, so it must not lock the device (architecture § 8 B).

  The health decision (fan_evalHealth) is a pure function with no hardware
  dependencies so it is unit-tested natively (test/test_fan_health).
=====================================================================*/

#ifndef FAN_HAL_H
#define FAN_HAL_H

#include "config.h"

// ═══════════════════════════════════════════════════════════════════
// Pure fan-health decision logic (always compiled; no Arduino/I2C deps)
// ═══════════════════════════════════════════════════════════════════

struct FanHealth {
  bool     stalled;             // advisory currently active
  bool     commanded;           // was the fan commanded on last tick (rising-edge tracking)
  uint16_t missPolls;           // consecutive judged polls with no tacho edge
  uint32_t commandedOnSinceMs;  // millis() at the rising edge (duty 0 → >0)
};

struct FanHealthEval {
  bool stalled;   // new stalled verdict
  bool changed;   // stalled flipped this call → caller sets/clears the advisory
};

// Advance fan health by one tacho poll.
//   dutyPct       commanded duty (0 = fan off)
//   sawEdge       did the coarse tacho show motion since the last poll
//   nowMs         current millis()
//   spinUpGraceMs allow the fan to start before judging (no stall during grace)
//   missThreshold consecutive edge-less polls (past grace) before flagging stalled
inline FanHealthEval fan_evalHealth(FanHealth* h, uint8_t dutyPct, bool sawEdge,
                                    uint32_t nowMs, uint32_t spinUpGraceMs,
                                    uint16_t missThreshold) {
  bool prev = h->stalled;

  if (dutyPct == 0) {
    // Commanded off: not spinning is expected. Reset and clear any stall.
    h->missPolls = 0;
    h->commanded = false;
    h->stalled = false;
    FanHealthEval off = { h->stalled, h->stalled != prev };
    return off;
  }

  // Commanded on. Stamp the spin-up grace window on the rising edge (clean bool
  // edge, no 0-sentinel — works even when nowMs == 0 at boot).
  if (!h->commanded) {
    h->commanded = true;
    h->commandedOnSinceMs = nowMs;
  }

  bool graceElapsed = (uint32_t)(nowMs - h->commandedOnSinceMs) >= spinUpGraceMs;

  if (sawEdge) {
    h->missPolls = 0;
    h->stalled = false;                                   // spinning → healthy
  } else if (graceElapsed) {
    if (h->missPolls < 0xFFFF) h->missPolls++;
    if (h->missPolls >= missThreshold) h->stalled = true; // edge-less past grace → stalled
  }
  // Within grace with no edge yet: hold judgement (missPolls stays 0).

  FanHealthEval ev = { h->stalled, h->stalled != prev };
  return ev;
}

// ═══════════════════════════════════════════════════════════════════
// Hardware driver (only built when the fan is enabled — PCB v2)
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_FAN

#include "structs.h"
#include "device_state.h"
#if ENABLE_MCP23017
  #include "mcp23017_hal.h"
#endif

static bool      g_fanReady    = false;
static uint8_t   g_fanDutyPct  = 0;
static bool      g_fanSpinning = false;
static FanHealth g_fanHealth   = { false, false, 0, 0 };

static inline uint8_t fan_dutyToRaw(uint8_t pct) {
  if (pct > 100) pct = 100;
  return (uint8_t)((uint16_t)pct * 255U / 100U);
}

bool fan_isReady()    { return g_fanReady; }
uint8_t fan_getDutyPct() { return g_fanDutyPct; }
bool fan_isSpinning() { return g_fanSpinning; }
bool fan_isStalled()  { return g_fanHealth.stalled; }

bool fan_init() {
  g_fanReady = false;
  g_fanDutyPct = 0;
  g_fanSpinning = false;
  g_fanHealth.stalled = false;
  g_fanHealth.commanded = false;
  g_fanHealth.missPolls = 0;
  g_fanHealth.commandedOnSinceMs = 0;

#if PIN_FAN_PWM >= 0
  ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ_HZ, FAN_PWM_RESOLUTION_BITS);
  ledcAttachPin(PIN_FAN_PWM, FAN_PWM_CHANNEL);
  ledcWrite(FAN_PWM_CHANNEL, 0);
  g_fanReady = true;
  DEBUG_PRINTF("[INFO]  Fan: initialized (PWM D6 @ %d Hz, %s)\n",
               FAN_PWM_FREQ_HZ, FAN_4WIRE ? "4-wire" : "3-wire");
#else
  DEBUG_WARN(F("Fan: PIN_FAN_PWM not configured"));
#endif
  return g_fanReady;
}

// Set fan speed 0-100 %. 25 kHz PWM gives stepless control for both fan types.
void fan_setDutyPct(uint8_t pct) {
  if (!g_fanReady) return;
  if (pct > 100) pct = 100;
  g_fanDutyPct = pct;
  ledcWrite(FAN_PWM_CHANNEL, fan_dutyToRaw(pct));
  DEBUG_PRINTF("[INFO]  Fan: duty=%u%%\n", (unsigned)pct);
}

// Loop tick — call every loop(); internally rate-limited to FAN_TACHO_POLL_MS.
// Reads the coarse tacho (MCP23017 GPA0 edges), evaluates health, and raises or
// clears the fan advisory. Non-blocking: a stall never changes device state.
void fan_update() {
  if (!g_fanReady) return;
  uint32_t now = millis();
  static uint32_t s_lastPollMs = 0;
  if ((uint32_t)(now - s_lastPollMs) < FAN_TACHO_POLL_MS) return;
  s_lastPollMs = now;

  bool sawEdge;
#if ENABLE_FAN_TACHO && ENABLE_MCP23017
  if (mcp23017_isReady()) {
    uint8_t edges = mcp23017_consumeEdgesA();
    sawEdge = (edges & (1 << MCP_PIN_FAN_TACHO)) != 0;
  } else {
    // No expander → no tacho. Don't fabricate a stall from a missing sensor;
    // treat as "can't tell" = healthy so we never raise a false advisory.
    sawEdge = true;
  }
#else
  // Tacho monitoring disabled (ENABLE_FAN_TACHO=false, default) or no expander.
  // Open-loop: we can't observe rotation → treat as "can't tell" = healthy so we
  // never raise a false "fan not spinning" advisory. See config.h ENABLE_FAN_TACHO.
  sawEdge = true;
#endif
  g_fanSpinning = (g_fanDutyPct > 0) && sawEdge;

  FanHealthEval ev = fan_evalHealth(&g_fanHealth, g_fanDutyPct, sawEdge, now,
                                    FAN_SPINUP_GRACE_MS, FAN_STALL_MISS_POLLS);
  if (ev.changed) {
    if (ev.stalled) {
      device_setAdvisory(DEVICE_FAULT_FAN, "Tuuletin ei pyöri (tarkista tuuletin)");
      DEBUG_WARN(F("Fan: STALL — commanded on, tacho shows no spin (advisory)"));
    } else {
      device_clearAdvisory(DEVICE_FAULT_FAN);
      DEBUG_INFO(F("Fan: spin OK (advisory cleared)"));
    }
  }
}

#endif // ENABLE_FAN
#endif // FAN_HAL_H
