/*=====================================================================
  sensor_sim.h - Hardware-free sensor simulation

  Fills SensorData with plausible, slowly-drifting synthetic readings so
  the UI (e-ink wall display, web portal) can be exercised and demoed
  WITHOUT any physical sensors connected. Enabled at compile time via
  ENABLE_SENSOR_SIMULATION (config.h, default false).

  Design:
    - Fills only RAW inputs (air T/RH, leaf temp, CO2, spectrum, height,
      water temp, battery). The derived metrics — VPD (vpd_calc.h),
      PPFD/DLI (light_calc.h + dli_tracker.h) — are then computed by the
      normal sensors_read() post-process from these synthetic inputs, so
      the simulated values flow through the exact same pipeline as real
      ones. No duplicated derivation logic.
    - Fallback semantics: only fills a field whose *Valid flag is still
      false (i.e. no real driver provided it this cycle). Harmless if a
      real sensor is present — the real reading wins.
    - Values drift slowly (sine over wall-clock) so the display looks
      alive across refreshes without flicker-fast changes.

  This complements ENABLE_GUIDED_GROWING_SIMULATION (which only simulates
  the grow phase/day, not sensor values).

  Dependencies: config.h, structs.h, <math.h>.
=====================================================================*/

#ifndef SENSOR_SIM_H
#define SENSOR_SIM_H

#include "config.h"
#include "structs.h"

#if ENABLE_SENSOR_SIMULATION

#include <math.h>

// Fill plausible synthetic readings for any field a real driver did not
// provide this cycle. `now` is millis() (used for slow drift).
static void sensorSim_fill(SensorData* data, uint32_t now) {
  float t     = (float)(now / 1000UL);   // seconds since boot
  float slow  = sinf(t / 120.0f);        // ~12.6 min period, [-1, 1]
  float slow2 = sinf(t / 300.0f);        // ~31 min period, [-1, 1]

  // Air environment → drives VPD post-process (needs envValid).
  if (!data->envValid) {
    data->airTempC       = 22.5f + 1.5f * slow;     // ~21..24 C
    data->airHumidity    = 60.0f + 8.0f * slow2;    // ~52..68 %
    data->airPressureHpa = 1013.0f;
    data->envValid       = true;
  }

  // Leaf IR (MLX90614) → leaf-based VPD + e-ink "Lehti-ero" card.
  if (!data->leafTempValid) {
    data->leafTempC     = data->airTempC + 1.0f + 0.5f * slow; // ~1.0..1.5 C warmer
    data->leafAmbientC  = data->airTempC;
    data->leafTempValid = true;
  }

  // CO2 (SCD41) → e-ink "CO2" card.
  if (!data->airCO2Valid) {
    data->airCO2Ppm   = 650 + (int)(150.0f * slow2);   // ~500..800 ppm
    data->airCO2Valid = true;
  }

  // Light spectrum (AS7341) → PPFD/DLI post-process (needs spectrumValid).
  // Counts tuned so lightCalc_estimatePPFD yields ~400 umol/m2/s (realistic
  // indoor grow), at 100 ms / gain 256. Red-heavy like an LED grow light.
  if (!data->spectrumValid) {
    LightSpectrum* s = &data->lightSpectrum;
    s->f1_415nm = 24; s->f2_445nm = 60; s->f3_480nm = 56; s->f4_515nm = 44;
    s->f5_555nm = 40; s->f6_590nm = 48; s->f7_630nm = 104; s->f8_680nm = 120;
    s->clear = 480; s->nir = 36; s->flickerHz = 0;
    s->integrationMs = 100; s->gain = 256;
    data->spectrumValid = true;
  }

  // Plant height (VL53L0X).
  if (!data->heightValid) {
    data->plantHeightMm = 170 + (int)(20.0f * slow);   // ~150..190 mm
    data->heightValid   = true;
  }

  // Water temperature (DS18B20).
  if (!data->waterTempValid) {
    data->waterTempC     = 20.5f + 0.5f * slow2;
    data->waterTempValid = true;
  }

  // Battery: only if no real source set it (voltage stays ~0 without INA219).
  if (data->batteryVoltage < 1.0f) {
    data->batteryVoltage = 12.4f - 0.1f * slow2;        // ~12.3..12.5 V
    data->batteryPercent = 92;
  }
}

#endif // ENABLE_SENSOR_SIMULATION
#endif // SENSOR_SIM_H
