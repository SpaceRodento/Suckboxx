/*=====================================================================
  calibration_math.h - Pure calibration math helpers

  Header-only helpers shared by runtime modules and native unit tests.
=====================================================================*/

#ifndef CALIBRATION_MATH_H
#define CALIBRATION_MATH_H

#include "config.h"
#include "structs.h"

#include <stdint.h>
#include <math.h>    // sqrtf — calibration_computeGeometryFactorOffset

inline int32_t calibration_defaultMotorDownSteps() {
  return 0;
}

inline int32_t calibration_defaultMotorUpSteps() {
#if MOTOR_DRIVER == MOTOR_DRIVER_TMC2208
  return (int32_t)MOTOR_MAX_HEIGHT_MM * MOTOR_STEPS_PER_REV * MOTOR_TMC_MICROSTEPS / MOTOR_LEAD_MM;
#elif MOTOR_DRIVER == MOTOR_DRIVER_L298N
  return (int32_t)MOTOR_MAX_HEIGHT_MM * MOTOR_STEPS_PER_REV * 2 / MOTOR_LEAD_MM;
#else
  return (int32_t)MOTOR_MAX_HEIGHT_MM;
#endif
}

inline float calibration_applyTds(float rawPpm, const CalibrationData* calib) {
  float gain = CALIB_TDS_GAIN_DEFAULT;
  float offset = CALIB_TDS_OFFSET_DEFAULT;

  if (calib) {
    gain = calib->tdsGain;
    offset = calib->tdsOffset;
  }

  float adjusted = (rawPpm * gain) + offset;
  if (adjusted < 0.0f) adjusted = 0.0f;
  return adjusted;
}

// Inverse of calcPumpDurationMs: given measured ml output and run duration,
// compute throughput. Used by the pump-throughput calibration wizard
// (/api/calib/pump/*, see docs/ohjeet/wizardit.md).
inline float calibration_computePumpMlPerSec(float measuredMl, uint32_t runDurationMs) {
  if (runDurationMs == 0 || measuredMl <= 0.0f) return 0.0f;
  return measuredMl / ((float)runDurationMs / 1000.0f);
}

// Scale factor that converts the AS7341's raw McCree-weighted relative PPFD
// (light_calc.h, calibFactor=1.0) into absolute umol/m2/s, given one
// reference reading (e.g. a phone lux meter converted to PPFD) taken at the
// same time/place as rawPpfdRelative. Used by the ppfd calibration wizard
// (/api/calib/ppfd/save, see docs/ohjeet/wizardit.md).
inline float calibration_computePpfdFactor(float referencePpfdUmol, float rawPpfdRelative) {
  if (referencePpfdUmol <= 0.0f || rawPpfdRelative <= 0.0f) return 0.0f;
  return referencePpfdUmol / rawPpfdRelative;
}

// Sensor-position -> canopy-position PPFD scale from the two lamp distances,
// via the inverse-square law: E ~ 1/d^2, so canopy/sensor = (d_sensor/d_canopy)^2.
//
// Both distances are measured FROM THE LAMP: sensorMm to the AS7341, canopyMm to
// the plant tops. Sensor further away than the canopy -> factor > 1 (canopy gets
// more light than the sensor reports). Returns 0.0f on invalid input so callers
// can reject it; the caller clamps the result to CALIB_PPFD_GEOMETRY_MAX.
//
// Accuracy note: the inverse-square law assumes a point source. Close to a wide
// lamp or a reflector it overestimates the ratio, so this is a starting estimate
// to be refined against a reference meter — not a substitute for one.
inline float calibration_computeGeometryFactor(float sensorMm, float canopyMm) {
  if (sensorMm <= 0.0f || canopyMm <= 0.0f) return 0.0f;
  const float ratio = sensorMm / canopyMm;
  return ratio * ratio;
}

// Same, but for the real mounting geometry: the AS7341 is deliberately offset
// sideways from the canopy (docs/laitteisto/anturien_sijoittelu.md — it would
// otherwise shade the plant or sit in its shadow), so the lamp is BOTH above
// and beside it.
//
//   heightMm - vertical lamp -> sensor distance
//   offsetMm - horizontal lamp -> sensor offset (0 = directly below the lamp)
//   canopyMm - vertical lamp -> canopy distance (canopy assumed under the lamp)
//
// Two effects stack, and using only the first underestimates canopy light:
//   1. Inverse square over the SLANT distance d = hypot(height, offset).
//   2. Cosine (Lambert): a level, upward-facing sensor collects off-axis light
//      scaled by cos(theta) = height/d, while the canopy sees the lamp head-on.
//
// factor = (d/canopy)^2 / cos(theta) = d^3 / (canopy^2 * height)
//
// Measured case 28.7.2026 (height 26, offset 15, canopy 16 cm): the slant-only
// form gives 3.52 and this gives 4.06 — a 15 % underestimate of canopy PPFD.
//
// Note this is still a floor, not a ceiling: AS7341 is not cosine-corrected and
// its interference filters attenuate off-axis light MORE than cos(theta), so a
// reference meter would likely land above this. Returns 0.0f on invalid input.
inline float calibration_computeGeometryFactorOffset(float heightMm, float offsetMm,
                                                     float canopyMm) {
  if (heightMm <= 0.0f || offsetMm < 0.0f || canopyMm <= 0.0f) return 0.0f;
  const float d2 = heightMm * heightMm + offsetMm * offsetMm;
  const float d = sqrtf(d2);
  return (d2 * d) / (canopyMm * canopyMm * heightMm);
}

// Clamp a requested soak-hold duty (%) to the safe range used by the soak-hold
// wizard (+/- and numeric trim) and the SOAK phase. 0 stays 0 ("soak with pump
// off"). Any non-zero value is forced into [PUMP_SOAK_MIN_DUTY_PCT..100] so the
// peristaltic pump never gets a stalling/chattering duty. Pure — unit-tested.
inline uint8_t calibration_clampSoakDutyPct(int pct) {
  if (pct <= 0) return 0;
  if (pct < PUMP_SOAK_MIN_DUTY_PCT) return PUMP_SOAK_MIN_DUTY_PCT;
  if (pct > 100) return 100;
  return (uint8_t)pct;
}

// Refine the INA228 SHUNT_CAL correction factor from a reference measurement.
// Given the current stored factor, the user's reference current and the current
// the device measured, returns the new factor so the device reads the reference:
//   new = old * (I_reference / I_measured)
// Multiplicative (re-runnable to converge) and clamped to [0.5, 2.0] to reject
// gross mismeasurement (wrong load, saturated range). Uses magnitudes — polarity
// is a wiring concern the factor cannot fix. Pure — unit-tested.
// WIZARD: power-current-cal — see docs/ohjeet/wizardit.md.
inline float calibration_refinePowerCalFactor(float oldFactor, float iRef, float iMeasured) {
  if (oldFactor <= 0.0f) oldFactor = 1.0f;
  float meas = iMeasured < 0.0f ? -iMeasured : iMeasured;
  if (meas < 1e-6f || iRef <= 0.0f) return oldFactor;   // guard: no usable reading
  float k = oldFactor * (iRef / meas);
  if (k < 0.5f) k = 0.5f;
  if (k > 2.0f) k = 2.0f;
  return k;
}

inline unsigned long calibration_calcPumpDurationMs(int ml, const CalibrationData* calib) {
  if (ml <= 0) return 0;

  float mlPerSec = CALIB_PUMP_ML_PER_SEC_DEFAULT;
  if (calib && calib->pumpMlPerSec > 0.01f) {
    mlPerSec = calib->pumpMlPerSec;
  }

  return (unsigned long)((ml / mlPerSec) * 1000.0f);
}

inline int32_t calibration_clampMotorStepTarget(int32_t target, const CalibrationData* calib) {
  int32_t low = calibration_defaultMotorDownSteps();
  int32_t high = calibration_defaultMotorUpSteps();

  if (calib) {
    low = calib->motorStepsDown;
    high = calib->motorStepsUp;
  }

  if (high <= low) {
    low = calibration_defaultMotorDownSteps();
    high = calibration_defaultMotorUpSteps();
  }

  if (target < low) return low;
  if (target > high) return high;
  return target;
}

#endif // CALIBRATION_MATH_H
