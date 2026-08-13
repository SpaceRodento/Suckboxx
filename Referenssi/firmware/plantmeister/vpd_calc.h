/*=====================================================================
  vpd_calc.h - Vapor Pressure Deficit (VPD) calculation.

  Pure math, no hardware dependencies. Uses the Tetens approximation
  for saturation vapor pressure.

  V1:  T_leaf == T_air (vpd_calculate).
  V2+: MLX90614 provides T_leaf for plant-empowerment VPD. Use
       vpd_selectAndCalculate() which detects aim-loss and falls back
       to T_air when the leaf reading is implausible.

  Reference: docs/liiketoiminta/markkinatutkimus/muistiot/3-6_Muistio_Waaksykaynti.md § 2.
=====================================================================*/

#ifndef VPD_CALC_H
#define VPD_CALC_H

#include <math.h>
#include "config.h"

// Saturation vapor pressure (kPa) at temperature T (°C), Tetens formula.
static inline float vpd_saturationKpa(float tempC) {
  return 0.6108f * expf((17.27f * tempC) / (tempC + 237.3f));
}

// VPD in kPa from air temperature (°C) and relative humidity (0..100 %).
// Returns a non-negative value; clamps negative results (RH > 100 noise) to 0.
static inline float vpd_calculate(float tempC, float humidityPct) {
  if (humidityPct > 100.0f) humidityPct = 100.0f;
  const float svp = vpd_saturationKpa(tempC);
  const float vpd = svp * (1.0f - humidityPct / 100.0f);
  return (vpd < 0.0f) ? 0.0f : vpd;
}

// Dew point (°C), Magnus approximation. Accurate within ~0.5°C for RH > 50%.
static inline float vpd_dewPointC(float airTempC, float humidityPct) {
  if (humidityPct <= 0.0f) return airTempC - 50.0f;  // degenerate: very dry
  if (humidityPct > 100.0f) humidityPct = 100.0f;
  const float gamma = logf(humidityPct / 100.0f) + (17.27f * airTempC) / (airTempC + 237.3f);
  return (237.3f * gamma) / (17.27f - gamma);
}

// Leaf VPD: SVP(T_leaf) − AVP(T_air, RH). Plant-empowerment formula.
// Represents the driving gradient at the leaf surface.
// Only call after verifying the reading is plausible (vpd_isLeafTempPlausible).
static inline float vpd_calculateLeafVpd(float airTempC, float humidityPct, float leafTempC) {
  if (humidityPct > 100.0f) humidityPct = 100.0f;
  const float svpLeaf  = vpd_saturationKpa(leafTempC);
  const float actualVp = vpd_saturationKpa(airTempC) * (humidityPct / 100.0f);
  const float vpd = svpLeaf - actualVp;
  return (vpd < 0.0f) ? 0.0f : vpd;
}

// Returns true if the MLX90614 leaf temperature reading is plausible for VPD.
// Conditions that flag aim-loss (see config.h LEAF_IR_* thresholds):
//   1. T_leaf below dew point - LEAF_IR_MIN_ABOVE_DEWPOINT_C (condensing surface)
//   2. |T_leaf - T_air| > LEAF_IR_MAX_DELTA_FROM_AIR_C (gross misaim / noise)
//   3. |T_leaf - T_water| < LEAF_IR_WATER_PROXIMITY_C when water temp known
//      (sensor pointed at reservoir instead of canopy)
static inline bool vpd_isLeafTempPlausible(
    float leafTempC, float airTempC, float humidityPct,
    float waterTempC, bool waterTempValid) {
  const float dewPoint = vpd_dewPointC(airTempC, humidityPct);
  if (leafTempC < dewPoint + LEAF_IR_MIN_ABOVE_DEWPOINT_C) return false;
  if (fabsf(leafTempC - airTempC) > LEAF_IR_MAX_DELTA_FROM_AIR_C) return false;
  if (waterTempValid && fabsf(leafTempC - waterTempC) < LEAF_IR_WATER_PROXIMITY_C) return false;
  return true;
}

// Unified VPD selector with aim-loss fallback.
//
// If leafTempValid and reading is plausible → returns leaf VPD (PE formula),
//   *aimLostOut = false.
// If leafTempValid but implausible → returns air VPD (V1 fallback),
//   *aimLostOut = true (caller should raise advisory).
// If leafTempValid is false → returns air VPD, *aimLostOut = false.
//
// aimLostOut may be NULL.
static inline float vpd_selectAndCalculate(
    float airTempC, float humidityPct,
    float leafTempC, bool leafTempValid,
    float waterTempC, bool waterTempValid,
    bool* aimLostOut) {
  if (aimLostOut) *aimLostOut = false;

  if (leafTempValid) {
    if (vpd_isLeafTempPlausible(leafTempC, airTempC, humidityPct, waterTempC, waterTempValid)) {
      return vpd_calculateLeafVpd(airTempC, humidityPct, leafTempC);
    }
    if (aimLostOut) *aimLostOut = true;
  }

  return vpd_calculate(airTempC, humidityPct);
}

#endif // VPD_CALC_H
