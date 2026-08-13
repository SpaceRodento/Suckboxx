/*=====================================================================
  light_calc.h - PPFD estimation from AS7341 spectral counts

  Header-only helper for estimating Photosynthetic Photon Flux Density
  (PPFD, µmol/m²/s) from raw AS7341 channel counts using McCree-weighted
  channel summation.

  Dependency: structs.h (LightSpectrum), config.h (CALIB_PPFD_FACTOR_DEFAULT).
  No dependency on calibration_math.h — callers pass the scale factor
  directly to avoid circular includes.

  Weighting table reference: McCree (1972) relative quantum efficiency,
  normalized so that 680 nm (peak red) = 1.00.
=====================================================================*/

#ifndef LIGHT_CALC_H
#define LIGHT_CALC_H

#include "config.h"
#include "structs.h"

// Gain that PPFD counts are normalized to. MUST stay 16: every calibration
// factor in the field was derived at 16x, and this constant is what keeps
// those factors valid across a gain change. See lightCalc_estimatePPFD.
#define LIGHTCALC_REFERENCE_GAIN 16.0f

// McCree relative quantum efficiency weights, one entry per AS7341 channel.
// Index mapping: 0=F1(415nm) .. 7=F8(680nm), normalized to max=1.0 at 680nm.
static const float kMccreeWeights[8] = {
  0.38f,  // F1 415 nm — violet
  0.72f,  // F2 445 nm — deep blue
  0.85f,  // F3 480 nm — blue
  0.65f,  // F4 515 nm — cyan
  0.54f,  // F5 555 nm — green
  0.52f,  // F6 590 nm — yellow
  0.93f,  // F7 630 nm — red
  1.00f   // F8 680 nm — deep red (peak PAR)
};

// lightCalc_estimatePPFD — estimate PPFD from a raw LightSpectrum reading.
//
// spec        - pointer to LightSpectrum; returns 0.0f if NULL or
//               integrationMs == 0 (would cause divide-by-zero).
// calibFactor - scale factor from CalibrationData.ppfdCalibrationFactor.
//               1.0f gives a relative (sensor-relative) value.
//               A user-calibrated value converts to absolute µmol/m²/s.
//
// Algorithm:
//   1. Weighted sum over the 8 PAR channels using McCree weights.
//   2. Normalize by (integrationMs / 100.0f) so that readings taken at
//      different integration times are comparable.
//   3. Multiply by calibFactor to scale to absolute µmol/m²/s.
//   4. Clamp to >= 0.0f (sensor noise can occasionally yield negatives).
static inline float lightCalc_estimatePPFD(const LightSpectrum* spec, float calibFactor) {
  if (spec == 0) return 0.0f;
  if (spec->integrationMs == 0) return 0.0f;

  const uint16_t channels[8] = {
    spec->f1_415nm,
    spec->f2_445nm,
    spec->f3_480nm,
    spec->f4_515nm,
    spec->f5_555nm,
    spec->f6_590nm,
    spec->f7_630nm,
    spec->f8_680nm
  };

  float weightedSum = 0.0f;
  for (int i = 0; i < 8; i++) {
    weightedSum += (float)channels[i] * kMccreeWeights[i];
  }

  // Normalize for integration time: reference point is 100 ms.
  float normalized = weightedSum / ((float)spec->integrationMs / 100.0f);

  // Normalize for gain so the calibration factor is a property of the SENSOR
  // and not of the range it happened to be on. Without this the factor is only
  // valid at one gain, which makes runtime auto-ranging (as7341_autorange.h)
  // impossible — every gain step would silently rescale PPFD by that step.
  //
  // The reference is 16x, the gain every existing calibration was derived at,
  // so this normalization is a NO-OP for stored factors: no migration, no
  // schema bump, no recalibration. Pick a different reference and every
  // deployed device's factor becomes wrong by that ratio.
  if (spec->gain > 0) {
    normalized *= (LIGHTCALC_REFERENCE_GAIN / (float)spec->gain);
  }

  float result = normalized * calibFactor;
  if (result < 0.0f) result = 0.0f;
  return result;
}

// lightCalc_estimatePPFD_uncalibrated — convenience wrapper that applies no
// absolute calibration (factor = 1.0f). Returns a relative sensor value that
// can be used for relative comparisons or displayed as an index.
static inline float lightCalc_estimatePPFD_uncalibrated(const LightSpectrum* spec) {
  return lightCalc_estimatePPFD(spec, 1.0f);
}

#endif // LIGHT_CALC_H
