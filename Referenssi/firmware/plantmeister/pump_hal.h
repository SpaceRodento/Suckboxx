/*=====================================================================
  pump_hal.h - Peristaltic Pump Control

  Digital on/off control for peristaltic pump.
  Non-blocking: call pump_update() from loop().
  Dosing stops automatically after calculated duration.
=====================================================================*/

#ifndef PUMP_HAL_H
#define PUMP_HAL_H

#include "config.h"
#include "structs.h"
#include "calibration_math.h"

#if ENABLE_PUMP

static bool g_pumpReady = false;
static bool g_pumpRunning = false;
static bool g_pumpContinuous = false;       // true = continuous-flow PWM mode (test/calibration)
static uint8_t g_pumpDutyPct = 0;            // 0-100 % when continuous
static bool g_pumpOverflowLatched = false;   // true = FLOAT_OVF tripped, requires explicit clear
static bool g_pumpMaintenanceLock = false;   // true = operator servicing, all starts refused
static bool g_pumpOverflowHaveClearStamp = false; // true once bed drained below FLOAT_OVF while latched
static uint32_t g_pumpOverflowFloatClearedMs = 0; // millis() when bed drained (settle-timer start)
static unsigned long g_pumpStartMs = 0;
static unsigned long g_pumpDurationMs = 0;
static float g_pumpMlPerSec = PUMP_ML_PER_SEC;

// LEDC PWM channel for continuous mode. Channel 4 = safe (channels 0-3 often
// taken by other libs/Servo). 1 kHz / 8-bit resolution is fine for MOSFET-driven
// peristaltic pumps.
#ifndef PUMP_PWM_CHANNEL
  #define PUMP_PWM_CHANNEL 4
#endif
#ifndef PUMP_PWM_FREQ_HZ
  #define PUMP_PWM_FREQ_HZ 1000
#endif
#ifndef PUMP_PWM_RESOLUTION_BITS
  #define PUMP_PWM_RESOLUTION_BITS 8
#endif

static void pump_writeOutput(bool on) {
#if PUMP_OUTPUT_ACTIVE_HIGH
  digitalWrite(PIN_PUMP, on ? HIGH : LOW);
#else
  digitalWrite(PIN_PUMP, on ? LOW : HIGH);
#endif
}

// Apply LEDC PWM duty (0-255 in 8-bit resolution). Respects PUMP_OUTPUT_ACTIVE_HIGH.
static void pump_writePwm(uint8_t duty8) {
#ifdef ARDUINO
#if PUMP_OUTPUT_ACTIVE_HIGH
  ledcWrite(PUMP_PWM_CHANNEL, duty8);
#else
  ledcWrite(PUMP_PWM_CHANNEL, 255 - duty8);
#endif
#else
  (void)duty8;   // native test: no-op
#endif
}

bool pump_init() {
  pinMode(PIN_PUMP, OUTPUT);
  pump_writeOutput(false);

#ifdef ARDUINO
  // Attach LEDC PWM channel to pin. Digital writes still work because
  // ledcWrite() overrides until ledcDetachPin() — we never detach during
  // dosing, just write duty=0 / duty=255.
  ledcSetup(PUMP_PWM_CHANNEL, PUMP_PWM_FREQ_HZ, PUMP_PWM_RESOLUTION_BITS);
  ledcAttachPin(PIN_PUMP, PUMP_PWM_CHANNEL);
  pump_writePwm(0);
#endif

  g_pumpReady   = true;
  g_pumpRunning = false;
  g_pumpContinuous = false;
  g_pumpDutyPct = 0;
  g_pumpStartMs    = 0;
  g_pumpDurationMs = 0;
  g_pumpOverflowHaveClearStamp = false;
  g_pumpOverflowFloatClearedMs = 0;
  // Boot is never maintenance (the state machine starts at INIT), and the lock
  // is re-derived from state on the first loop tick anyway.
  g_pumpMaintenanceLock = false;
  DEBUG_INFO(F("Pump: initialized (PWM @ 1kHz, 8-bit)"));
  return true;
}

// Start pump for fixed duration (non-blocking, stops in pump_update)
bool pump_startForMs(unsigned long durationMs) {
  if (!g_pumpReady || g_pumpRunning) return false;
  if (durationMs == 0) return false;
  if (g_pumpMaintenanceLock) {
    DEBUG_WARN(F("Pump: refused — huoltotila aktiivinen"));
    return false;
  }
  if (g_pumpOverflowLatched) {
    DEBUG_WARN(F("Pump: refused — FLOAT_OVF latch active"));
    return false;
  }

  g_pumpDurationMs = durationMs;
  g_pumpStartMs = millis();
  g_pumpRunning = true;
  g_pumpContinuous = false;
  g_pumpDutyPct = 100;

  pump_writePwm(255);   // full duty for dose
  DEBUG_PRINTF("[INFO]  Pump: run for %lu ms\n", g_pumpDurationMs);
  return true;
}

// Start a dose of given ml (non-blocking, stops in pump_update)
bool pump_dose(int ml) {
  if (!g_pumpReady || g_pumpRunning) return false;
  if (ml <= 0) return false;

  CalibrationData calib;
  calib.pumpMlPerSec = g_pumpMlPerSec;
  unsigned long durationMs = calibration_calcPumpDurationMs(ml, &calib);
  if (!pump_startForMs(durationMs)) return false;

  DEBUG_PRINTF("[INFO]  Pump: dosing %d ml (%lu ms)\n", ml, durationMs);
  return true;
}

// Stop pump immediately — clears both dose and continuous modes.
void pump_stop() {
  pump_writePwm(0);
  pump_writeOutput(false);
  g_pumpRunning = false;
  g_pumpContinuous = false;
  g_pumpDutyPct = 0;
  DEBUG_VERBOSE(F("Pump: stopped"));
}

// Continuous-flow mode for testing/calibration: pump runs at requested duty
// indefinitely (subject to PUMP_ABSOLUTE_MAX_ON_MS hard cap and FLOAT_OVF
// safety latch). Caller is responsible for stopping via pump_stop().
bool pump_setContinuous(uint8_t dutyPct) {
  if (!g_pumpReady) return false;
  if (g_pumpMaintenanceLock) {
    DEBUG_WARN(F("Pump: refused — huoltotila aktiivinen"));
    return false;
  }
  if (g_pumpOverflowLatched) {
    DEBUG_WARN(F("Pump: refused — FLOAT_OVF latch active, clear first"));
    return false;
  }
  if (dutyPct > 100) dutyPct = 100;

  // If switching from dose to continuous, reset timing.
  if (!g_pumpContinuous) {
    g_pumpStartMs = millis();
    g_pumpDurationMs = PUMP_ABSOLUTE_MAX_ON_MS;
  }

  g_pumpContinuous = true;
  g_pumpRunning = (dutyPct > 0);
  g_pumpDutyPct = dutyPct;
  uint8_t duty8 = (uint8_t)((uint16_t)dutyPct * 255U / 100U);
  pump_writePwm(duty8);
  DEBUG_PRINTF("[INFO]  Pump: continuous duty=%u%%\n", (unsigned)dutyPct);
  return true;
}

// Maintenance lock — refuses every pump start while the operator services the
// device, and stops a running pump the moment the lock engages.
//
// Separate from the FLOAT_OVF latch on purpose: that one is a fault latch the
// operator must explicitly clear, this one is a mirror of DEVICE_MAINTENANCE.
// The caller (loop) derives it from g_device.state every tick rather than
// setting it once on the transition, so the lock cannot outlive the state it
// represents — a missed "unlock" on some exit path would otherwise disable the
// pump permanently, and a missed "lock" would arm it under the operator's hand.
// State stays the single source of truth (architecture.md § 2).
//
// This layer exists because loop_dispatch alone is not enough: /api/test/pump
// and the calibration wizards call pump_startForMs()/pump_setContinuous()
// directly, bypassing the scheduler entirely.
void pump_setMaintenanceLock(bool locked) {
  if (locked && !g_pumpMaintenanceLock) {
    DEBUG_INFO(F("Pump: huoltolukko päälle — pumppu pysäytetty"));
  } else if (!locked && g_pumpMaintenanceLock) {
    DEBUG_INFO(F("Pump: huoltolukko pois"));
  }
  g_pumpMaintenanceLock = locked;
  if (locked && g_pumpRunning) pump_stop();
}

bool pump_isMaintenanceLocked() { return g_pumpMaintenanceLock; }

// Latch FLOAT_OVF — caller (loop) must invoke when sensors->waterOverflowActive
// goes true. Pumps off and refuses pump_setContinuous() until cleared.
void pump_latchOverflow() {
  if (!g_pumpOverflowLatched) {
    g_pumpOverflowLatched = true;
    pump_stop();
    DEBUG_ERROR(F("Pump: FLOAT_OVF LATCHED — pump forced off"));
  }
}

// Clear overflow latch. UI should expose this after operator confirms physical
// state. Refuses if FLOAT_OVF still active (caller must verify).
bool pump_clearOverflowLatch() {
  g_pumpOverflowLatched = false;
  DEBUG_INFO(F("Pump: FLOAT_OVF latch cleared"));
  return true;
}

bool pump_isOverflowLatched() { return g_pumpOverflowLatched; }

// ── Opt-in overflow latch auto-clear ────────────────────────────────────
// Pure decision: may the FLOAT_OVF latch be auto-cleared this tick? Requires the
// feature enabled, the latch set, the float no longer active (bed drained), and
// the bed to have STAYED drained for at least settleMs (floatClearedMs marks when
// it drained). enabled=false (default) always returns false → manual clear only.
inline bool pump_overflowAutoClearDue(bool latched, bool floatActive,
                                      bool haveClearStamp, uint32_t floatClearedMs,
                                      uint32_t nowMs, bool enabled, uint32_t settleMs) {
  if (!enabled || !latched || floatActive || !haveClearStamp) return false;
  return (uint32_t)(nowMs - floatClearedMs) >= settleMs;
}

// Loop tick (call with config): tracks how long the bed has stayed drained while
// latched and auto-clears the latch once the settle period elapses. Reads
// FLOAT_OVF directly (same source pump_update() latches on). No-op on native and
// when the switch is disabled. The ebb&flow FSM's overflow_count still hard-latches
// a chronically overflowing bed, so auto-clear cannot cycle forever into a flood.
inline void pump_overflowAutoClearTick(bool enabled, uint32_t settleMs) {
#if ENABLE_OVERFLOW_SWITCH && (PIN_FLOAT_SWITCH_OVERFLOW >= 0)
  if (!g_pumpOverflowLatched) { g_pumpOverflowHaveClearStamp = false; return; }
  bool floatActive = (digitalRead(PIN_FLOAT_SWITCH_OVERFLOW) == LOW);
  uint32_t now = (uint32_t)millis();
  if (floatActive) {
    g_pumpOverflowHaveClearStamp = false;        // still overflowing — reset settle timer
  } else if (!g_pumpOverflowHaveClearStamp) {
    g_pumpOverflowHaveClearStamp = true;         // bed just drained — start settle timer
    g_pumpOverflowFloatClearedMs = now;
  }
  if (pump_overflowAutoClearDue(g_pumpOverflowLatched, floatActive,
                                g_pumpOverflowHaveClearStamp, g_pumpOverflowFloatClearedMs,
                                now, enabled, settleMs)) {
    g_pumpOverflowLatched = false;
    g_pumpOverflowHaveClearStamp = false;
    DEBUG_PRINTF("[INFO]  Pump: FLOAT_OVF latch auto-cleared after %lus drained\n",
                 (unsigned long)(settleMs / 1000UL));
  }
#else
  (void)enabled; (void)settleMs;
#endif
}
bool pump_isContinuous() { return g_pumpContinuous; }
uint8_t pump_getDutyPct() { return g_pumpDutyPct; }

// Call from loop() — auto-stops after dose duration or absolute safety limit.
// Also reads FLOAT_OVF directly each cycle (faster than 30 s sensor poll) and
// latches the pump off if active.
bool pump_update() {
#if ENABLE_OVERFLOW_SWITCH && (PIN_FLOAT_SWITCH_OVERFLOW >= 0)
  // Direct GPIO read: FLOAT_OVF is INPUT_PULLUP active-LOW. Latch on edge so
  // operator must explicitly clear after the float retracts.
  if (digitalRead(PIN_FLOAT_SWITCH_OVERFLOW) == LOW) {
    pump_latchOverflow();
  }
#endif

  if (!g_pumpRunning) return false;

  // uint32_t cast pakottaa modulo-32 -aritmetiikan natiivissa testauksessa
  // (Linux unsigned long = 64-bit). ESP32:lla cast on no-op.
  uint32_t elapsed = (uint32_t)millis() - (uint32_t)g_pumpStartMs;

  // Hardware safety: absolute max on-time regardless of mode (dose or continuous)
  if (elapsed >= PUMP_ABSOLUTE_MAX_ON_MS) {
    pump_stop();
    DEBUG_ERROR(F("Pump: EMERGENCY STOP — absolute max on-time exceeded"));
    return false;
  }

  // Continuous mode: no dose-duration end, just hard cap above.
  if (g_pumpContinuous) {
    return true;
  }

  if (elapsed >= g_pumpDurationMs) {
    pump_stop();
    DEBUG_INFO(F("Pump: dose complete"));
    return false;
  }
  return true;
}

bool pump_isRunning() { return g_pumpRunning; }
bool pump_isReady()   { return g_pumpReady; }

bool pump_setMlPerSec(float mlPerSec) {
  if (mlPerSec < 0.05f || mlPerSec > 200.0f) return false;
  g_pumpMlPerSec = mlPerSec;
  return true;
}

float pump_getMlPerSec() {
  return g_pumpMlPerSec;
}

// Remaining time for current dose
unsigned long pump_remainingMs() {
  if (!g_pumpRunning) return 0;
  // Sama uint32_t-cast kuin pump_update():ssa — rollover-safe sekä
  // ESP32:lla etta natiivihostissa.
  uint32_t elapsed = (uint32_t)millis() - (uint32_t)g_pumpStartMs;
  if (elapsed >= g_pumpDurationMs) return 0;
  return g_pumpDurationMs - elapsed;
}

#endif // ENABLE_PUMP
#endif // PUMP_HAL_H
