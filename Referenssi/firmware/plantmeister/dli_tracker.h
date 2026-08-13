/*=====================================================================
  dli_tracker.h - Daily Light Integral (DLI) accumulator

  Header-only module that integrates PPFD (µmol/m²/s) over time to
  produce DLI in mol/m²/d.  It is the natural companion to light_calc.h:
  light_calc.h estimates instantaneous PPFD from raw AS7341 counts;
  dli_tracker.h accumulates those samples across a 24-hour window.

  Algorithm:
    DLI = ∫ PPFD dt   (µmol/m²/s × s / 1 000 000 = mol/m²)

  Robustness for long unattended runs:
    - Gap clamp (DLI_MAX_SAMPLE_GAP_MS, default 15 min): a single long
      gap caused by sleep / WiFi outage is treated as at most 15 min of
      the last measured PPFD, so one outage cannot poison a full day.
    - millis() overflow: dt uses unsigned subtraction, which wraps
      correctly on 32-bit overflow at ~49.7 days.
    - First sample only anchors the clock; no integral is produced until
      the second sample arrives (avoids a spurious large dt on boot).

  Caller contract:
    - Call dliTracker_addSample() on every PPFD reading (~5 min interval
      recommended, matches the sensor scheduler cadence).
    - Call dliTracker_checkRollover() on every loop tick.  When it returns
      >= 0.0f the previous day's DLI is ready; the accumulator resets.
    - Pass DLI_DAY_LENGTH_MS (86400000 ms = 24 h) as dayLenMs for real
      deployments; use a shorter value in tests.

  Dependencies: config.h (DLI_* defines), <stdint.h>.
  No dependency on structs.h, hardware, or Arduino runtime.
=====================================================================*/

#ifndef DLI_TRACKER_H
#define DLI_TRACKER_H

#include "config.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// State struct
// ---------------------------------------------------------------------------

struct DliState {
  float    accumulatedMol;   // mol/m² accumulated since dayStartMs
  uint32_t lastSampleMs;     // millis() of last addSample call
  uint32_t dayStartMs;       // millis() at start of current day window
  bool     hasSample;        // false until first sample anchors the clock
};

// ---------------------------------------------------------------------------
// Status enum
// ---------------------------------------------------------------------------

enum DliStatus {
  DLI_STATUS_LOW     = 0,
  DLI_STATUS_OPTIMAL = 1,
  DLI_STATUS_HIGH    = 2
};

// ---------------------------------------------------------------------------
// dliTracker_init — reset accumulator for a new session or day.
//
// s     - pointer to DliState to initialise; no-op if NULL.
// nowMs - current millis() timestamp; becomes dayStartMs and lastSampleMs.
// ---------------------------------------------------------------------------
static inline void dliTracker_init(DliState* s, uint32_t nowMs) {
  if (s == 0) return;
  s->accumulatedMol = 0.0f;
  s->lastSampleMs   = nowMs;
  s->dayStartMs     = nowMs;
  s->hasSample      = false;
}

// ---------------------------------------------------------------------------
// dliTracker_addSample — integrate one PPFD reading into the accumulator.
//
// s     - DliState pointer; no-op if NULL.
// ppfd  - instantaneous PPFD in µmol/m²/s; negative values are clamped to 0.
// nowMs - current millis() timestamp.
//
// The first call after init (or after hasSample==false) only anchors the
// clock; no integral is produced.  Subsequent calls compute:
//   dtMs      = nowMs - lastSampleMs  (unsigned; handles millis() rollover)
//   dtMs      = min(dtMs, DLI_MAX_SAMPLE_GAP_MS)   (gap clamp)
//   deltaMol  = ppfd * (dtMs / 1000.0) / 1 000 000
// ---------------------------------------------------------------------------
static inline void dliTracker_addSample(DliState* s, float ppfd, uint32_t nowMs) {
  if (s == 0) return;

  // Clamp negative sensor noise to zero.
  if (ppfd < 0.0f) ppfd = 0.0f;

  // First sample: only anchors the clock, no integral produced.
  if (!s->hasSample) {
    s->lastSampleMs = nowMs;
    s->hasSample    = true;
    return;
  }

  // Unsigned subtraction handles millis() 32-bit overflow correctly.
  uint32_t dtMs = nowMs - s->lastSampleMs;

  // Gap clamp: one long outage must not poison the day's integral.
  if (dtMs > DLI_MAX_SAMPLE_GAP_MS) {
    dtMs = DLI_MAX_SAMPLE_GAP_MS;
  }

  // µmol/m²/s × s / 1 000 000 = mol/m²
  float deltaMol = ppfd * ((float)dtMs / 1000.0f) / 1000000.0f;

  s->accumulatedMol += deltaMol;
  s->lastSampleMs    = nowMs;
}

// ---------------------------------------------------------------------------
// dliTracker_restore — resume a persisted window after a reboot.
//
// s         - DliState pointer; no-op if NULL.
// nowMs     - current millis() (post-boot, so ~0).
// mol       - integral to resume from (mol/m²), already sanitised by caller.
// elapsedMs - how far into the 24 h window that integral was accumulated.
//
// hasSample is deliberately left FALSE: the downtime carries no PPFD samples,
// so the next reading must only anchor the clock. Integrating across the gap
// would credit the whole outage with the first post-boot reading — boot at
// sunrise after a night-long outage would then invent a full day of light.
// This is the same reasoning as the gap clamp, taken to its limit.
// ---------------------------------------------------------------------------
static inline void dliTracker_restore(DliState* s, uint32_t nowMs,
                                      float mol, uint32_t elapsedMs) {
  if (s == 0) return;
  if (!(mol >= 0.0f)) mol = 0.0f;                       // catches NaN
  if (elapsedMs >= DLI_DAY_LENGTH_MS) elapsedMs = DLI_DAY_LENGTH_MS - 1UL;
  s->accumulatedMol = mol;
  s->dayStartMs     = nowMs - elapsedMs;   // unsigned wrap is intentional
  s->lastSampleMs   = nowMs;
  s->hasSample      = false;
}

// ---------------------------------------------------------------------------
// dliTracker_elapsedMs — how far into the current window we are.
// Companion to dliTracker_restore for the persistence layer.
// ---------------------------------------------------------------------------
static inline uint32_t dliTracker_elapsedMs(const DliState* s, uint32_t nowMs) {
  if (s == 0) return 0;
  return nowMs - s->dayStartMs;
}

// ---------------------------------------------------------------------------
// dliTracker_checkRollover — test whether a full day window has elapsed.
//
// s        - DliState pointer; returns -1.0f if NULL.
// nowMs    - current millis() timestamp.
// dayLenMs - length of one accumulation window in milliseconds
//            (use DLI_DAY_LENGTH_MS = 86400000 for 24 h).
//
// Returns:
//   >= 0.0f  — the completed day's DLI in mol/m²/d; accumulator reset.
//   -1.0f    — day window not yet complete; accumulator unchanged.
// ---------------------------------------------------------------------------
static inline float dliTracker_checkRollover(DliState* s, uint32_t nowMs, uint32_t dayLenMs) {
  if (s == 0) return -1.0f;

  if ((uint32_t)(nowMs - s->dayStartMs) >= dayLenMs) {
    float prev        = s->accumulatedMol;
    s->accumulatedMol = 0.0f;
    s->dayStartMs     = nowMs;
    return prev;
  }
  return -1.0f;
}

// ---------------------------------------------------------------------------
// dliTracker_status — classify a DLI value relative to a crop optimum range.
//
// dli     - DLI in mol/m²/d (typically from the previous day's rollover).
// optMin  - lower bound of the optimal range (inclusive).
// optMax  - upper bound of the optimal range (inclusive).
//
// Returns DLI_STATUS_LOW / DLI_STATUS_OPTIMAL / DLI_STATUS_HIGH.
// ---------------------------------------------------------------------------
static inline DliStatus dliTracker_status(float dli, float optMin, float optMax) {
  if (dli < optMin)  return DLI_STATUS_LOW;
  if (dli > optMax)  return DLI_STATUS_HIGH;
  return DLI_STATUS_OPTIMAL;
}

#endif // DLI_TRACKER_H
