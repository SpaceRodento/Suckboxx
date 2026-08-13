// sensor_driver_ads1115.h - ADS1115 driver wrapper for TDS

#ifndef SENSOR_DRIVER_ADS1115_H
#define SENSOR_DRIVER_ADS1115_H

#include "sensor_driver.h"
#include "config.h"
#include "calibration_math.h"

#if ENABLE_TDS_SENSOR
#include <Adafruit_ADS1X15.h>
static Adafruit_ADS1115 g_ads1115;
static bool g_ads1115Ready = false;
static const CalibrationData* g_sensorCalibration = NULL;

static bool ads1115_isReady(void) { return g_ads1115Ready; }

#ifdef PLANTMEISTER_NATIVE_TEST
static void ads1115_resetForTest(void) { g_ads1115Ready = false; }
#endif

static bool ads1115_init(void) {
  // Presence check to avoid blocking library init timeouts
  if (!isI2CDevicePresent(I2C_ADDR_ADS1115)) {
    g_ads1115Ready = false;
    return false;
  }
  if (!g_ads1115Ready) {
    if (g_ads1115.begin(I2C_ADDR_ADS1115)) {
      g_ads1115.setGain(GAIN_ONE);
      g_ads1115Ready = true;
    }
  }
  return g_ads1115Ready;
}

static SensorProbeResult ads1115_probe(void) {
  Wire.beginTransmission(I2C_ADDR_ADS1115);
  int res = Wire.endTransmission();
  return (res == 0) ? SENSOR_PROBE_OK : SENSOR_PROBE_ABSENT;
}

static void ads1115_read(SensorData* out) {
  if (!g_ads1115Ready) return;
  int16_t raw = g_ads1115.readADC_SingleEnded(0);
  float voltage = g_ads1115.computeVolts(raw);
  float compensationCoeff = 1.0f;
  if (out->waterTempValid) {
    compensationCoeff = 1.0f + 0.02f * (out->waterTempC - 25.0f);
  }
  float compensatedV = voltage / compensationCoeff;
  float rawPpm = ((133.42f * compensatedV * compensatedV * compensatedV
                - 255.86f * compensatedV * compensatedV
                + 857.39f * compensatedV) * 0.5f);
  out->tdsPpm = (int)calibration_applyTds(rawPpm, g_sensorCalibration);
  out->tdsValid = true;
}

static const SensorDriver g_ads1115Driver = {
  .name = "ADS1115",
  .readyFlag = &g_ads1115Ready,
  .reprobeIntervalMs = SENSOR_REPROBE_I2C_MS,
  .init = ads1115_init,
  .probe = ads1115_probe,
  .read = ads1115_read
};

static void ads1115_setCalibration(const CalibrationData* cal) { g_sensorCalibration = cal; }

#endif // ENABLE_TDS_SENSOR

#endif // SENSOR_DRIVER_ADS1115_H
