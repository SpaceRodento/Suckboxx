// sensor_driver_ina228.h - INA228 power monitor -> SensorDriver adapter
//
// Thin wrapper around the portable ina228.h core. Owns a single INA228
// instance, exposes the SensorDriver init/probe/read contract, and fills
// the power-monitoring fields of SensorData. All INA228 scaling/config
// lives in ina228.h — this file only bridges it into PlantMeister.
//
// Enable with HW_INA228=true (+ CAP_POWER_MONITORING=true). Shunt / max
// current / ADC range come from INA228_* defines in config.h.

#ifndef SENSOR_DRIVER_INA228_H
#define SENSOR_DRIVER_INA228_H

#include "sensor_driver.h"
#include "config.h"
#include "ina228.h"

#if HW_INA228

static INA228 g_ina228;
static bool   g_ina228Ready = false;

// Runtime-adjustable config (Level 1: no reflash). Defaults come from the
// compile-time INA228_* macros; the calibration wizard / portal overrides them
// live via ina228_applyRuntimeConfig() and persists them in CalibrationData.
static float   g_ina228ShuntOhms   = (float)INA228_SHUNT_OHMS;
static float   g_ina228MaxCurrentA = (float)INA228_MAX_CURRENT_A;
static uint8_t g_ina228AdcRange    = (uint8_t)INA228_ADC_RANGE;
static float   g_ina228CalFactor   = 1.0f;

static bool ina228_isReady(void) { return g_ina228Ready; }

#ifdef PLANTMEISTER_NATIVE_TEST
static void ina228_resetForTest(void) {
  g_ina228Ready = false;
  g_ina228ShuntOhms   = (float)INA228_SHUNT_OHMS;
  g_ina228MaxCurrentA = (float)INA228_MAX_CURRENT_A;
  g_ina228AdcRange    = (uint8_t)INA228_ADC_RANGE;
  g_ina228CalFactor   = 1.0f;
}
#endif

static INA228Config ina228_currentConfig(void) {
  INA228Config cfg = {
    g_ina228ShuntOhms,
    g_ina228MaxCurrentA,
    g_ina228AdcRange,
    (uint8_t)I2C_ADDR_INA228,
    g_ina228CalFactor
  };
  return cfg;
}

static bool ina228_init(void) {
  // Presence check first — never let a missing device block boot.
  if (!isI2CDevicePresent(I2C_ADDR_INA228)) {
    g_ina228Ready = false;
    return false;
  }
  INA228Config cfg = ina228_currentConfig();
  g_ina228Ready = g_ina228.begin(cfg);
  return g_ina228Ready;
}

// Level 1 runtime reconfiguration: update shunt / max current / ADC range /
// calibration factor and, if the device is live, re-run begin() so the change
// takes effect without a reboot. Called at boot (from persisted CalibrationData)
// and by the power calibration wizard.
static void ina228_applyRuntimeConfig(float shuntOhms, float maxCurrentA,
                                      uint8_t adcRange, float calFactor) {
  g_ina228ShuntOhms   = shuntOhms;
  g_ina228MaxCurrentA = maxCurrentA;
  g_ina228AdcRange    = adcRange ? 1 : 0;
  g_ina228CalFactor   = (calFactor > 0.0f) ? calFactor : 1.0f;
  if (g_ina228Ready) {
    INA228Config cfg = ina228_currentConfig();
    g_ina228.begin(cfg);
  }
}

// Freshest current reading in mA (signed). Used by the calibration wizard's
// apply step to compare measured vs. the user-supplied reference.
static float ina228_readCurrentMa(void) {
  if (!g_ina228Ready) return 0.0f;
  return g_ina228.current_A() * 1000.0f;
}

static SensorProbeResult ina228_probe(void) {
  Wire.beginTransmission(I2C_ADDR_INA228);
  int res = Wire.endTransmission();
  return (res == 0) ? SENSOR_PROBE_OK : SENSOR_PROBE_ABSENT;
}

static void ina228_read(SensorData* out) {
  if (!g_ina228Ready || !out) return;
  float busV = g_ina228.busVoltage_V();
  out->powerBusV      = busV;
  out->powerCurrentmA = g_ina228.current_A() * 1000.0f;
  out->powerMw        = g_ina228.power_W() * 1000.0f;
  out->powerChargemAh = (float)g_ina228.charge_mAh();   // on-chip accumulator
  out->powerEnergyWh  = (float)g_ina228.energy_Wh();    // on-chip accumulator
  out->powerValid     = true;
  // Backward-compat: existing battery/rail display reads batteryVoltage.
  out->batteryVoltage = busV;
}

static const SensorDriver g_ina228Driver = {
  .name = "INA228",
  .readyFlag = &g_ina228Ready,
  .reprobeIntervalMs = SENSOR_REPROBE_I2C_MS,
  .init = ina228_init,
  .probe = ina228_probe,
  .read = ina228_read
};

#endif // HW_INA228

#endif // SENSOR_DRIVER_INA228_H
