/*=====================================================================
  grow_clock_math.h - Pure grow-clock math (natively testable)

  Restores millis()-anchored grow timers across power cycles. The
  scheduler measures progress as `now - anchor` (growDayStartMs,
  lightCycleStartMs); both anchors reset to ~0 on boot, so with
  repeated power cuts the grow-day counter never reaches 24 h and the
  photoperiod restarts from zero (docs/kehitys/
  valo-ja-keskeytymattomyys-suunnitelma.md, vika 3).

  The fix persists the *elapsed* accumulators and back-dates the
  anchors at boot: anchor = now - elapsed. Unsigned arithmetic makes a
  "negative" anchor wrap so the scheduler's comparisons keep working
  unchanged. This header holds the pure helpers; LittleFS I/O and the
  boot/loop integration live in grow_clock.h.
=====================================================================*/

#ifndef GROW_CLOCK_MATH_H
#define GROW_CLOCK_MATH_H

#include <stdint.h>

#define GROWCLOCK_DAY_MS 86400000UL

// Ceiling for a restored DLI integral. Full summer sun outdoors reaches
// ~60 mol/m2/d at temperate latitudes; 100 leaves headroom for a sensor
// reading high while still rejecting garbage from a corrupt file.
#define GROWCLOCK_DLI_MAX_MOL 100.0f

// Persisted accumulators. Power-cut duration itself is unknowable without an
// RTC and intentionally does not accumulate: the plant received no light
// during the outage, so counting grow time in powered light-cycles is the
// biologically consistent choice.
//
// stepIndex is an EPOCH TAG, not a truth: the step decision itself lives in
// config.json (DeviceConfig.growStepIndex, like growPhase). This file's
// contract is "the file may be lost" — a lost growclock.json must never be
// able to command re-soaking already-sown rockwool. The tag only answers
// "does this stepElapsedMs still belong to the current step".
// dliMol/dliElapsedMs ride along here rather than in their own file: the DLI
// window is the same kind of millis()-anchored accumulator as the grow day,
// it is written on the same tick, and sharing the file means persisting it
// costs ZERO additional flash erases. Restoring the integral (not just the
// window) matters because a reboot at 15:00 otherwise reports "DLI 0.0" for a
// day that already delivered most of its light — the guidance then tells the
// user to add light the plant already got.
struct GrowClockData {
  uint32_t dayElapsedMs;    // progress into the current grow day [0, 24h)
  uint32_t lightElapsedMs;  // position in the 24h photoperiod cycle [0, 24h)
  uint32_t stepElapsedMs;   // progress into the current grow step (grow_steps.h)
  uint8_t  stepIndex;       // epoch tag for stepElapsedMs (see above)
  float    dliMol;          // mol/m2 accumulated in the current DLI window
  uint32_t dliElapsedMs;    // progress into the current DLI window [0, 24h)
};

// A stale or corrupt value must not fire multiple day ticks back-to-back.
static inline uint32_t growclock_clampDayElapsed(uint32_t elapsedMs) {
  return (elapsedMs >= GROWCLOCK_DAY_MS) ? (GROWCLOCK_DAY_MS - 1UL) : elapsedMs;
}

// A restored DLI integral must be plausible before it is trusted: a corrupt
// file must not seed the day with a value the guidance then reads as "target
// already met" (the failure mode that matters — under-lighting is visible in
// the plant, a phantom surplus is not). NaN/negative -> 0; above the ceiling
// -> 0, because a garbage integral is strictly worse than starting over.
static inline float growclock_sanitizeDli(float mol) {
  if (!(mol >= 0.0f)) return 0.0f;            // catches NaN
  if (mol > GROWCLOCK_DLI_MAX_MOL) return 0.0f;
  return mol;
}

// The light cycle is cyclic — wrap instead of clamping.
static inline uint32_t growclock_wrapLightElapsed(uint32_t elapsedMs) {
  return elapsedMs % GROWCLOCK_DAY_MS;
}

// Back-date an anchor so that `now - anchor == elapsed`. Underflow wraps on
// purpose: the scheduler only ever computes the same unsigned difference.
static inline unsigned long growclock_restoredAnchor(unsigned long nowMs,
                                                     uint32_t elapsedMs) {
  return nowMs - (unsigned long)elapsedMs;
}

// Persist on a coarse interval (flash wear), plus immediately when the day
// accumulator rolls backwards — that means a day tick or a grow restart just
// reset the anchor, and losing power right after must not restore yesterday's
// nearly-full accumulator (which would near-double a grow day).
static inline bool growclock_shouldSave(unsigned long nowMs,
                                        unsigned long lastSaveMs,
                                        unsigned long intervalMs,
                                        uint32_t dayElapsedMs,
                                        uint32_t lastSavedDayElapsedMs) {
  if (dayElapsedMs < lastSavedDayElapsedMs) return true;
  return (nowMs - lastSaveMs) >= intervalMs;
}

// ── Grow-step clock (grow_steps.h) ─────────────────────────────────

// Epoch check at restore: the persisted elapsed belongs to exactly one step —
// the one whose index was current when it was written. On mismatch (config.json
// advanced but growclock.json lagged behind, or vice versa) return 0: the step
// clock starts over, which is this module's advertised failure mode, instead of
// silently timing the WRONG step with the old accumulator.
static inline uint32_t growclock_stepElapsedFor(uint32_t persistedElapsedMs,
                                                uint8_t persistedIdx,
                                                uint8_t currentIdx) {
  return (persistedIdx == currentIdx) ? persistedElapsedMs : 0UL;
}

// Save cadence while a step runs. The coarse 30 min interval was sized for
// day-scale clocks; a short TIMER step (soak, 30 min) fits entirely inside one
// save gap, so a power cut would lose the WHOLE step's progress. Tighten to
// 60 s while a countdown step is running. Day-scale BUTTON steps keep the
// coarse interval — flash wear, and losing <30 min of a 5-day wait is noise.
// This does not break the "outage duration is not counted" philosophy: it only
// persists measured powered progress more often.
static inline unsigned long growclock_saveIntervalMs(unsigned long defaultMs,
                                                     bool timerStepRunning) {
  const unsigned long stepIntervalMs = 60000UL;
  if (!timerStepRunning) return defaultMs;
  return (stepIntervalMs < defaultMs) ? stepIntervalMs : defaultMs;
}

// Immediate-save triggers for the step clock, mirroring the day-rollover rule:
// an index change (advance/reset) must persist the new epoch tag right away —
// otherwise a power cut would restore the OLD step's elapsed under the NEW
// index (exactly what the tag exists to prevent, but only if it is current).
// An elapsed rollback means the anchor was reset within the same index.
static inline bool growclock_stepForcesSave(uint8_t stepIndex,
                                            uint8_t lastSavedStepIndex,
                                            uint32_t stepElapsedMs,
                                            uint32_t lastSavedStepElapsedMs) {
  if (stepIndex != lastSavedStepIndex) return true;
  return stepElapsedMs < lastSavedStepElapsedMs;
}

#endif // GROW_CLOCK_MATH_H
