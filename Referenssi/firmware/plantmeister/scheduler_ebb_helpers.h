/*=====================================================================
  scheduler_ebb_helpers.h - Flood schedule status helpers

  Pure, hardware-free helper functions for computing the effective
  flood interval and a human-readable explanation. Included by
  both scheduler.h (runtime scheduling) and wifi_portal_routes_get.h
  (status display) so the logic is defined once.

  Depends only on config.h (EBB_NIGHT_FLOOD_MULTIPLIER) and structs.h
  (GrowPhaseParams). No Arduino, no hardware HAL, no LittleFS.
=====================================================================*/

#ifndef SCHEDULER_EBB_HELPERS_H
#define SCHEDULER_EBB_HELPERS_H

#include "config.h"
#include "structs.h"
#include <stdio.h>

// Base flood interval in minutes before the night multiplier is applied.
//   cfgOverrideMin > 0  → portal override wins (always enables flooding)
//   phase with floodIntervalMin > 0 → phase setting
//   phase with floodIntervalMin == 0 or NULL phase → 0 (flooding disabled)
static inline uint16_t scheduler_baseFloodIntervalMin(const GrowPhaseParams* phase,
                                                       uint16_t cfgOverrideMin) {
  if (cfgOverrideMin > 0) return cfgOverrideMin;
  if (!phase || phase->floodIntervalMin == 0) return 0;
  return phase->floodIntervalMin;
}

// Effective flood interval in minutes with night multiplier applied.
// Returns 0 if flooding is disabled.
static inline uint16_t scheduler_effectiveFloodIntervalMin(const GrowPhaseParams* phase,
                                                            uint16_t cfgOverrideMin,
                                                            bool lightsOn) {
  uint16_t base = scheduler_baseFloodIntervalMin(phase, cfgOverrideMin);
  if (base == 0) return 0;
  if (!lightsOn) return (uint16_t)((uint32_t)base * EBB_NIGHT_FLOOD_MULTIPLIER);
  return base;
}

// Human-readable Finnish explanation of the current flood schedule.
// Fills out with a null-terminated string (at most outLen chars incl. NUL).
// Examples:
//   "tulvitus pois tassa vaiheessa"
//   "vaihe 180 min"
//   "vaihe 180 min x2 yo = 360 min"
//   "portaali-override 60 min"
//   "portaali-override 60 min x2 yo = 120 min"
static inline void scheduler_floodReasonStr(char* out, size_t outLen,
                                            const GrowPhaseParams* phase,
                                            uint16_t cfgOverrideMin,
                                            bool lightsOn) {
  uint16_t base = scheduler_baseFloodIntervalMin(phase, cfgOverrideMin);
  if (base == 0) {
    snprintf(out, outLen, "tulvitus pois tassa vaiheessa");
    return;
  }
  bool nightApplied = (!lightsOn && EBB_NIGHT_FLOOD_MULTIPLIER > 1);
  if (cfgOverrideMin > 0) {
    if (nightApplied) {
      snprintf(out, outLen, "portaali-override %u min x%u yo = %u min",
               (unsigned)cfgOverrideMin, (unsigned)EBB_NIGHT_FLOOD_MULTIPLIER,
               (unsigned)((uint32_t)cfgOverrideMin * EBB_NIGHT_FLOOD_MULTIPLIER));
    } else {
      snprintf(out, outLen, "portaali-override %u min", (unsigned)cfgOverrideMin);
    }
  } else {
    if (nightApplied) {
      snprintf(out, outLen, "vaihe %u min x%u yo = %u min",
               (unsigned)phase->floodIntervalMin,
               (unsigned)EBB_NIGHT_FLOOD_MULTIPLIER,
               (unsigned)((uint32_t)phase->floodIntervalMin * EBB_NIGHT_FLOOD_MULTIPLIER));
    } else {
      snprintf(out, outLen, "vaihe %u min", (unsigned)phase->floodIntervalMin);
    }
  }
}

// Seconds until the next periodic flood, given the effective interval (minutes,
// night multiplier already applied) and the millis() timestamp when the last
// cycle started. Returns 0 when already due/overdue, and -1 ("not scheduled")
// when effectiveIntervalMin == 0. The caller must pass effectiveIntervalMin == 0
// whenever flooding is not currently scheduled (device not GROWING, mid-cycle, or
// flood disabled) so the UI can show "—" rather than a misleading countdown.
static inline int32_t scheduler_secondsUntilNextFlood(uint16_t effectiveIntervalMin,
                                                      uint32_t lastCycleStartMs,
                                                      uint32_t nowMs) {
  if (effectiveIntervalMin == 0) return -1;
  uint32_t intervalMs = (uint32_t)effectiveIntervalMin * 60000UL;
  uint32_t elapsed = nowMs - lastCycleStartMs;  // unsigned millis() wrap-safe
  if (elapsed >= intervalMs) return 0;          // due now
  return (int32_t)((intervalMs - elapsed) / 1000UL);
}

// ── Circulation ("kierto") — independent anti-stagnation cycle ──────

// True when an independent circulation cycle should start now: enabled, the ebb
// FSM is IDLE (no flood in progress — the flood always wins the pump), no
// calibration owns the pump, none already running, and the interval has elapsed
// since the last circulation start. Pure → unit-testable.
static inline bool scheduler_shouldStartCirculate(bool enabled, bool ebbIdle,
                                                  bool calibIdle, bool circulateActive,
                                                  uint16_t intervalMin,
                                                  uint32_t lastStartMs, uint32_t nowMs) {
  if (!enabled || !ebbIdle || !calibIdle || circulateActive || intervalMin == 0)
    return false;
  uint32_t intervalMs = (uint32_t)intervalMin * 60000UL;
  return (uint32_t)(nowMs - lastStartMs) >= intervalMs;  // unsigned wrap-safe
}

// Seconds until the next circulation cycle. 0 = due now, -1 = disabled / not
// scheduled. UI shows "—" for -1.
static inline int32_t scheduler_secondsUntilNextCirculate(bool enabled, uint16_t intervalMin,
                                                          uint32_t lastStartMs, uint32_t nowMs) {
  if (!enabled || intervalMin == 0) return -1;
  uint32_t intervalMs = (uint32_t)intervalMin * 60000UL;
  uint32_t elapsed = nowMs - lastStartMs;
  if (elapsed >= intervalMs) return 0;
  return (int32_t)((intervalMs - elapsed) / 1000UL);
}

#endif // SCHEDULER_EBB_HELPERS_H
