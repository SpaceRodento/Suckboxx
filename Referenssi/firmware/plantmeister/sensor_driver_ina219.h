// sensor_driver_ina219.h - INA219 battery monitor wrapper

#ifndef SENSOR_DRIVER_INA219_H
#define SENSOR_DRIVER_INA219_H

#include "sensor_driver.h"
#include "config.h"

#if ENABLE_BATTERY_MONITOR
#include <Adafruit_INA219.h>
static Adafruit_INA219 g_ina219(I2C_ADDR_INA219);
static bool g_ina219Ready = false;

static bool ina219_isReady(void) { return g_ina219Ready; }

#ifdef PLANTMEISTER_NATIVE_TEST
static void ina219_resetForTest(void) { g_ina219Ready = false; }
#endif

static bool ina219_init(void) {
  // Presence check
  if (!isI2CDevicePresent(I2C_ADDR_INA219)) {
    g_ina219Ready = false;
    return false;
  }
  if (!g_ina219Ready) {
    if (g_ina219.begin()) {
      g_ina219Ready = true;
    }
  }
  return g_ina219Ready;
}

static SensorProbeResult ina219_probe(void) {
  Wire.beginTransmission(I2C_ADDR_INA219);
  int res = Wire.endTransmission();
  return (res == 0) ? SENSOR_PROBE_OK : SENSOR_PROBE_ABSENT;
}

static void ina219_read(SensorData* out) {
  if (!g_ina219Ready) return;
  out->batteryVoltage = g_ina219.getBusVoltage_V() + (g_ina219.getShuntVoltage_mV() / 1000.0f);
}

static const SensorDriver g_ina219Driver = {
  .name = "INA219",
  .readyFlag = &g_ina219Ready,
  .reprobeIntervalMs = SENSOR_REPROBE_I2C_MS,
  .init = ina219_init,
  .probe = ina219_probe,
  .read = ina219_read
};

#endif // ENABLE_BATTERY_MONITOR

#endif // SENSOR_DRIVER_INA219_H
