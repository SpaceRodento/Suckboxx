/*=====================================================================
  calibration_runtime.h - Calibration persistence and runtime helpers

  Runtime-only wrapper around calibration math and LittleFS storage.
=====================================================================*/

#ifndef CALIBRATION_RUNTIME_H
#define CALIBRATION_RUNTIME_H

#include "calibration_math.h"
#include "schema_migration.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

// Forward declaration — calibration_save() defined below.
inline bool calibration_save(const CalibrationData* calib);

inline void calibration_setDefaults(CalibrationData* calib) {
  if (!calib) return;

  calib->tdsOffset = CALIB_TDS_OFFSET_DEFAULT;
  calib->tdsGain = CALIB_TDS_GAIN_DEFAULT;
  calib->pumpMlPerSec = CALIB_PUMP_ML_PER_SEC_DEFAULT;
  calib->motorStepsDown = calibration_defaultMotorDownSteps();
  calib->motorStepsUp = calibration_defaultMotorUpSteps();
  calib->ppfdCalibrationFactor = CALIB_PPFD_FACTOR_DEFAULT;
  calib->ppfdGeometryFactor = CALIB_PPFD_GEOMETRY_DEFAULT;

  // INA228 power monitor — defaults from compile-time config (WIZARD: power-current-cal)
  calib->powerShuntOhms = (float)INA228_SHUNT_OHMS;
  calib->powerMaxCurrentA = (float)INA228_MAX_CURRENT_A;
  calib->powerAdcRange = (uint8_t)INA228_ADC_RANGE;
  calib->powerCalFactor = 1.0f;
}

inline void calibration_clampBounds(CalibrationData* calib) {
  if (!calib) return;

  if (calib->tdsGain < 0.1f || calib->tdsGain > 5.0f) {
    calib->tdsGain = CALIB_TDS_GAIN_DEFAULT;
  }

  if (calib->tdsOffset < -5000.0f) calib->tdsOffset = -5000.0f;
  if (calib->tdsOffset > 5000.0f) calib->tdsOffset = 5000.0f;

  if (calib->pumpMlPerSec < 0.05f || calib->pumpMlPerSec > 200.0f) {
    calib->pumpMlPerSec = CALIB_PUMP_ML_PER_SEC_DEFAULT;
  }

  // Wide range: raw AS7341 weighted-count scale (gain/integration dependent)
  // vs. absolute umol/m2/s can differ by orders of magnitude. Only guard
  // against non-positive/garbage values, not plausibility.
  if (calib->ppfdCalibrationFactor <= 0.0f || calib->ppfdCalibrationFactor > 1000.0f) {
    calib->ppfdCalibrationFactor = CALIB_PPFD_FACTOR_DEFAULT;
  }

  // Geometry is a distance ratio squared, so it is bounded by plausibility:
  // below 1.0 the sensor would sit closer to the lamp than the canopy (allowed,
  // e.g. sensor on the lamp bracket), above ~50 the ratio exceeds 7x distance.
  if (calib->ppfdGeometryFactor <= 0.0f ||
      calib->ppfdGeometryFactor > CALIB_PPFD_GEOMETRY_MAX) {
    calib->ppfdGeometryFactor = CALIB_PPFD_GEOMETRY_DEFAULT;
  }

  if (calib->motorStepsUp <= calib->motorStepsDown) {
    calib->motorStepsDown = calibration_defaultMotorDownSteps();
    calib->motorStepsUp = calibration_defaultMotorUpSteps();
  }

  // INA228 power monitor bounds. Ranges cover milli-ohm high-current shunts up
  // to ~100 ohm small-current shunts; garbage falls back to compile-time config.
  if (calib->powerShuntOhms < 0.001f || calib->powerShuntOhms > 1000.0f) {
    calib->powerShuntOhms = (float)INA228_SHUNT_OHMS;
  }
  if (calib->powerMaxCurrentA < 0.0001f || calib->powerMaxCurrentA > 20.0f) {
    calib->powerMaxCurrentA = (float)INA228_MAX_CURRENT_A;
  }
  if (calib->powerAdcRange > 1) calib->powerAdcRange = 0;
  if (calib->powerCalFactor < 0.5f || calib->powerCalFactor > 2.0f) {
    calib->powerCalFactor = 1.0f;
  }
}

inline bool calibration_load(CalibrationData* calib) {
  calibration_setDefaults(calib);

  if (!LittleFS.exists(PATH_CALIBRATION)) {
    DEBUG_WARN(F("Calibration: no calibration.json, using defaults"));
    return true;
  }

  File f = LittleFS.open(PATH_CALIBRATION, "r");
  if (!f) {
    DEBUG_ERROR(F("Calibration: failed to open calibration.json"));
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    DEBUG_ERRORF("Calibration: JSON parse error: %s\n", err.c_str());
    return false;
  }

  // ── Schema version gate ────────────────────────────────────────
  uint8_t fileVersion = (uint8_t)(doc["schema_version"] | 0);
  SchemaDecision decision = schema_decide(fileVersion,
                                          SCHEMA_VERSION_CALIBRATION_MIN,
                                          SCHEMA_VERSION_CALIBRATION_CURRENT);

  if (decision == SCHEMA_RESET_DEFAULTS) {
    DEBUG_PRINTF("[WARN]  Calibration: schema v%u unsupported (current v%u), using defaults\n",
                 fileVersion, SCHEMA_VERSION_CALIBRATION_CURRENT);
    return true;
  }

  calib->tdsOffset = doc["tds_offset"] | CALIB_TDS_OFFSET_DEFAULT;
  calib->tdsGain = doc["tds_gain"] | CALIB_TDS_GAIN_DEFAULT;
  calib->pumpMlPerSec = doc["pump_ml_per_sec"] | CALIB_PUMP_ML_PER_SEC_DEFAULT;
  calib->motorStepsUp = doc["motor_steps_up"] | calibration_defaultMotorUpSteps();
  calib->motorStepsDown = doc["motor_steps_down"] | calibration_defaultMotorDownSteps();
  calib->ppfdCalibrationFactor = doc["ppfd_calibration_factor"] | CALIB_PPFD_FACTOR_DEFAULT;
  calib->ppfdGeometryFactor = doc["ppfd_geometry_factor"] | CALIB_PPFD_GEOMETRY_DEFAULT;
  calib->powerShuntOhms = doc["power_shunt_ohms"] | (float)INA228_SHUNT_OHMS;
  calib->powerMaxCurrentA = doc["power_max_current_a"] | (float)INA228_MAX_CURRENT_A;
  calib->powerAdcRange = (uint8_t)(doc["power_adc_range"] | (int)INA228_ADC_RANGE);
  calib->powerCalFactor = doc["power_cal_factor"] | 1.0f;

  calibration_clampBounds(calib);

  if (decision == SCHEMA_NEEDS_MIGRATE) {
    DEBUG_PRINTF("[INFO]  Calibration: migrated v%u -> v%u, rewriting\n",
                 fileVersion, SCHEMA_VERSION_CALIBRATION_CURRENT);
    calibration_save(calib);
  } else {
    DEBUG_INFO(F("Calibration: loaded from calibration.json"));
  }
  return true;
}

inline bool calibration_save(const CalibrationData* calib) {
  StaticJsonDocument<512> doc;

  doc["schema_version"] = SCHEMA_VERSION_CALIBRATION_CURRENT;
  doc["tds_offset"] = calib->tdsOffset;
  doc["tds_gain"] = calib->tdsGain;
  doc["pump_ml_per_sec"] = calib->pumpMlPerSec;
  doc["motor_steps_up"] = calib->motorStepsUp;
  doc["motor_steps_down"] = calib->motorStepsDown;
  doc["ppfd_calibration_factor"] = calib->ppfdCalibrationFactor;
  doc["ppfd_geometry_factor"] = calib->ppfdGeometryFactor;
  doc["power_shunt_ohms"] = calib->powerShuntOhms;
  doc["power_max_current_a"] = calib->powerMaxCurrentA;
  doc["power_adc_range"] = calib->powerAdcRange;
  doc["power_cal_factor"] = calib->powerCalFactor;

  File f = LittleFS.open(PATH_CALIBRATION, "w");
  if (!f) {
    DEBUG_ERROR(F("Calibration: failed to write calibration.json"));
    return false;
  }

  serializeJsonPretty(doc, f);
  f.close();
  DEBUG_INFO(F("Calibration: saved to calibration.json"));
  return true;
}

#endif // CALIBRATION_RUNTIME_H
