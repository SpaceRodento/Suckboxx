/*=====================================================================
  dli_expectation.h - judge a running daily light sum against the clock

  Bug this fixes (29.7.2026): DLI is a CUMULATIVE 24 h quantity, but the
  status view compared it straight against the full-day target window.
  The accumulator starts at zero every morning, so it is below target by
  definition until the day has delivered it — "Valosumma matala" fired
  every morning, on every plant, forever. Not a threshold that needed
  tuning: comparing a half-finished sum to a whole-day goal is a
  category error.

  What replaces it: project the day's end from the current rate.

      projected = accumulated + ppfd * remainingHours * 0.0036

  (umol/m2/s * 3600 s / 1e6 = mol/m2.) That answers the question the
  grower actually has — "will the day get there?" — while there is still
  time to act, instead of stating the arithmetic truth that noon is not
  yet evening.

  Deliberately a REACTION, not a forecast. The projection assumes the
  present light holds, which outdoors is false the moment a cloud
  arrives. That is acceptable because the advice it drives is reversible
  and cheap: "this sun is strong, shade is coming due". It must never be
  read as predicting weather.

  Two guards keep it honest:
    - Silence early in the photoperiod. A projection built on the first
      few minutes is noise multiplied by the length of the day.
    - An overshoot ALREADY banked needs no projection. Passing the
      full-day ceiling is true whatever happens next.
=====================================================================*/

#ifndef DLI_EXPECTATION_H
#define DLI_EXPECTATION_H

#include "status_targets.h"   // PeStatus

// umol/m2/s held for one hour -> mol/m2.
#define DLI_UMOL_HOUR_TO_MOL   0.0036f

// Fraction of the photoperiod that must have elapsed before a projection
// is allowed to drive advice. Below this the remaining-hours multiplier is
// large enough that a passing shadow flips the verdict.
#ifndef DLI_JUDGE_MIN_ELAPSED_FRACTION
  #define DLI_JUDGE_MIN_ELAPSED_FRACTION  0.2f
#endif

// NOTE ON THE ANCHOR (29.7.2026): elapsed hours are NOT derived here from a
// wall clock. The device's light cycle is anchored to its own
// lightCycleStartMs, and `light_on_hour` in the config is an offset into that
// cycle rather than an hour of the day — reconstructing "how far into the
// photoperiod" from NTP time drifts by however long ago the grow started.
// The device therefore reports `growing.light_elapsed_h` directly
// (scheduler_lightElapsedHours), and -1 means the cycle is in its dark part.
// This module only consumes it.

// End-of-day light sum implied by the current rate.
static inline float dli_project(float accumulated, float ppfd, float remainingHours) {
  if (remainingHours < 0.0f) remainingHours = 0.0f;
  return accumulated + ppfd * remainingHours * DLI_UMOL_HOUR_TO_MOL;
}

// Time-aware verdict for the daily light sum.
//
// Returns PE_NODATA when the day is too young to judge — the caller shows
// the number without an advice row, which is the honest state: we know the
// value, we do not yet know whether it is a problem.
static inline PeStatus dli_evaluate(float accumulated, float ppfd, bool ppfdValid,
                                    float elapsedHours, int lightHours,
                                    float lo, float hi) {
  // Already over the full-day ceiling. No projection can undo light the
  // plant has taken, so this verdict is time-independent and comes first.
  if (hi != PE_NO_LIMIT && accumulated > hi) return PE_HIGH;

  if (lightHours <= 0) return PE_NODATA;

  // -1 = dark part of the cycle, or the device could not say. Either way
  // there is no honest "how far into the day" to reason from.
  if (elapsedHours < 0.0f) return PE_NODATA;

  const float elapsed = (elapsedHours > (float)lightHours) ? (float)lightHours
                                                           : elapsedHours;
  const float fraction = elapsed / (float)lightHours;
  if (fraction < DLI_JUDGE_MIN_ELAPSED_FRACTION) return PE_NODATA;

  const float remaining = (float)lightHours - elapsed;

  // Without a live PPFD there is nothing to extrapolate from. Fall back to
  // the raw comparison, but only once the photoperiod is essentially over —
  // that is the one moment when accumulated-vs-target is a fair question.
  if (!ppfdValid) {
    if (fraction < 0.8f) return PE_NODATA;
    if (lo != PE_NO_LIMIT && accumulated < lo) return PE_LOW;
    return PE_OK;
  }

  const float projected = dli_project(accumulated, ppfd, remaining);
  if (hi != PE_NO_LIMIT && projected > hi) return PE_HIGH;
  if (lo != PE_NO_LIMIT && projected < lo) return PE_LOW;
  return PE_OK;
}

// Drop-in replacement for status_evalTarget() that routes FIELD_DLI through
// the clock-aware path above and leaves every other measure untouched.
// status_view calls THIS everywhere it used to call status_evalTarget, so the
// three evaluation sites (row filter, preview precompute, footer) cannot
// drift apart — a mismatch there would hide one row while judging another.
static inline PeStatus status_evalTargetTimeAware(const PeTarget* t,
                                                  const DisplayData* d,
                                                  float* value) {
  if (t->field != FIELD_DLI) return status_evalTarget(t, d, value);

  float v;
  if (!status_fieldValue(FIELD_DLI, d, &v)) return PE_NODATA;
  if (value) *value = v;

  return dli_evaluate(v, d->ppfd, d->ppfdValid,
                      d->lightElapsedHours, d->lightHours,
                      t->lo, t->hi);
}

#endif // DLI_EXPECTATION_H
