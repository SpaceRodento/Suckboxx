// sensor_driver_vl53l0x.h - VL53L0X driver wrapper for sensor_driver.h

#ifndef SENSOR_DRIVER_VL53L0X_H
#define SENSOR_DRIVER_VL53L0X_H

#include "sensor_driver.h"
#include "config.h"

#if ENABLE_HEIGHT_SENSOR

#include <Adafruit_VL53L0X.h>
// Local hardware instance and ready flag (header-only, internal linkage)
static Adafruit_VL53L0X g_vl53l0x;
static bool g_vl53l0xReady = false;

static bool vl53l0x_isReady(void) { return g_vl53l0xReady; }

#ifdef PLANTMEISTER_NATIVE_TEST
static void vl53l0x_resetForTest(void) { g_vl53l0xReady = false; }
#endif

static bool vl53l0x_init(void) {
  // Presence check to avoid blocking internal timeouts.
  if (!isI2CDevicePresent(I2C_ADDR_VL53L0X)) {
    g_vl53l0xReady = false;
    return false;
  }
  if (!g_vl53l0xReady) {
    if (g_vl53l0x.begin(I2C_ADDR_VL53L0X)) {
      g_vl53l0xReady = true;
    }
  }
  return g_vl53l0xReady;
}

static SensorProbeResult vl53l0x_probe(void) {
  // Cheap I2C presence check
  Wire.beginTransmission(I2C_ADDR_VL53L0X);
  int res = Wire.endTransmission();
  return (res == 0) ? SENSOR_PROBE_OK : SENSOR_PROBE_ABSENT;
}

static void vl53l0x_read(SensorData* out) {
  if (!g_vl53l0xReady) return;
  VL53L0X_RangingMeasurementData_t measure;
  g_vl53l0x.rangingTest(&measure, false);
  if (measure.RangeStatus != 4) {
    out->plantHeightMm = measure.RangeMilliMeter;
    out->heightValid = true;
  }
}

static const SensorDriver g_vl53l0xDriver = {
  .name = "VL53L0X",
  .readyFlag = &g_vl53l0xReady,
  .reprobeIntervalMs = SENSOR_REPROBE_I2C_MS,
  .init = vl53l0x_init,
  .probe = vl53l0x_probe,
  .read = vl53l0x_read
};

#endif // ENABLE_HEIGHT_SENSOR

#endif // SENSOR_DRIVER_VL53L0X_H
