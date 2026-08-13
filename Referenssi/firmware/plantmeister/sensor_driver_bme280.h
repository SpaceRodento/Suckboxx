// sensor_driver_bme280.h - BME280 driver wrapper

#ifndef SENSOR_DRIVER_BME280_H
#define SENSOR_DRIVER_BME280_H

#include "sensor_driver.h"
#include "config.h"

#if HW_BME280
#include <Adafruit_BME280.h>
static Adafruit_BME280 g_bme280;
static bool g_bme280Ready = false;

static bool bme280_isReady(void) { return g_bme280Ready; }

#ifdef PLANTMEISTER_NATIVE_TEST
static void bme280_resetForTest(void) { g_bme280Ready = false; }
#endif

static bool bme280_init(void) {
  // Presence check to avoid blocking Adafruit init timeouts
  if (!isI2CDevicePresent(I2C_ADDR_BME280)) {
    g_bme280Ready = false;
    return false;
  }
  if (!g_bme280Ready) {
    if (g_bme280.begin(I2C_ADDR_BME280)) {
      g_bme280.setSampling(Adafruit_BME280::MODE_FORCED,
                           Adafruit_BME280::SAMPLING_X1,
                           Adafruit_BME280::SAMPLING_X1,
                           Adafruit_BME280::SAMPLING_X1,
                           Adafruit_BME280::FILTER_OFF,
                           Adafruit_BME280::STANDBY_MS_1000);
      g_bme280Ready = true;
    }
  }
  return g_bme280Ready;
}

static SensorProbeResult bme280_probe(void) {
  Wire.beginTransmission(I2C_ADDR_BME280);
  int res = Wire.endTransmission();
  return (res == 0) ? SENSOR_PROBE_OK : SENSOR_PROBE_ABSENT;
}

static void bme280_read(SensorData* out) {
  if (!g_bme280Ready) return;
  g_bme280.takeForcedMeasurement();
  float t = g_bme280.readTemperature();
  float h = g_bme280.readHumidity();
  float p = g_bme280.readPressure() / 100.0f;
  if (t > -40.0f && t < 85.0f && h >= 0.0f && h <= 100.0f) {
    out->airTempC = t;
    out->airHumidity = h;
    out->airPressureHpa = p;
    out->envValid = true;
  }
}

static const SensorDriver g_bme280Driver = {
  .name = "BME280",
  .readyFlag = &g_bme280Ready,
  .reprobeIntervalMs = SENSOR_REPROBE_I2C_MS,
  .init = bme280_init,
  .probe = bme280_probe,
  .read = bme280_read
};

#endif // HW_BME280

#endif // SENSOR_DRIVER_BME280_H
