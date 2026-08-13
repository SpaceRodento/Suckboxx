// sensor_driver_ds18b20.h - DS18B20 OneWire driver wrapper

#ifndef SENSOR_DRIVER_DS18B20_H
#define SENSOR_DRIVER_DS18B20_H

#include "sensor_driver.h"
#include "config.h"

#if ENABLE_WATER_TEMP
#include <OneWire.h>
#include <DallasTemperature.h>
// Local OneWire / Dallas instance (header-only)
static OneWire g_oneWire(PIN_DS18B20);
static DallasTemperature g_ds18b20(&g_oneWire);
static bool g_ds18b20Ready = false;

static bool ds18b20_isReady(void) { return g_ds18b20Ready; }

#ifdef PLANTMEISTER_NATIVE_TEST
static void ds18b20_resetForTest(void) { g_ds18b20Ready = false; }
#endif

static bool ds18b20_init(void) {
  g_ds18b20.begin();
  if (g_ds18b20.getDeviceCount() > 0) {
    g_ds18b20.setResolution(12);
    g_ds18b20.setWaitForConversion(false);
    g_ds18b20Ready = true;
  } else {
    g_ds18b20Ready = false;
  }
  return g_ds18b20Ready;
}

static SensorProbeResult ds18b20_probe(void) {
  // OneWire presence: device count > 0
  return (g_ds18b20.getDeviceCount() > 0) ? SENSOR_PROBE_OK : SENSOR_PROBE_ABSENT;
}

static void ds18b20_read(SensorData* out) {
  // Manager has already verified readyFlag before calling.
  float temp = DEVICE_DISCONNECTED_C;
  for (int attempt = 0; attempt < 2; attempt++) {
    g_ds18b20.requestTemperatures();
    temp = g_ds18b20.getTempCByIndex(0);
    if (temp != DEVICE_DISCONNECTED_C) break;
  }

  if (temp != DEVICE_DISCONNECTED_C && temp > -20.0f && temp < 60.0f) {
    out->waterTempC = temp;
    out->waterTempValid = true;
  }
  // Note: do NOT clear readyFlag here. sensor_manager handles
  // runtime-degradation via probe() at SENSOR_REPROBE_ONEWIRE_MS cadence.
}

static const SensorDriver g_ds18b20Driver = {
  .name = "DS18B20",
  .readyFlag = &g_ds18b20Ready,
  .reprobeIntervalMs = SENSOR_REPROBE_ONEWIRE_MS,
  .init = ds18b20_init,
  .probe = ds18b20_probe,
  .read = ds18b20_read
};

#endif // ENABLE_WATER_TEMP

#endif // SENSOR_DRIVER_DS18B20_H
