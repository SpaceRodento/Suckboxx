// sensor_driver_scd41.h - Sensirion SCD41 driver wrapper (CO2 + T + RH)

#ifndef SENSOR_DRIVER_SCD41_H
#define SENSOR_DRIVER_SCD41_H

#include "sensor_driver.h"
#include "config.h"

#if HW_SCD41
#include <SensirionI2cScd4x.h>

static SensirionI2cScd4x g_scd41;
static bool g_scd41Ready = false;
static bool g_scd41MeasuringStarted = false;

static bool scd41_isReady(void) { return g_scd41Ready; }

#ifdef PLANTMEISTER_NATIVE_TEST
static void scd41_resetForTest(void) {
  g_scd41Ready = false;
  g_scd41MeasuringStarted = false;
}
#endif

static bool scd41_init(void) {
  if (!isI2CDevicePresent(I2C_ADDR_SCD41)) {
    g_scd41Ready = false;
    return false;
  }
  if (g_scd41Ready) return true;

  g_scd41.begin(Wire, I2C_ADDR_SCD41);

  // Stop any in-progress measurement before configuring (datasheet:
  // setAutomaticSelfCalibration requires idle state).
  g_scd41.stopPeriodicMeasurement();
  delay(500);

  // ASC off — suljetussa kotelossa SCD41 ei näe 400 ppm raitista ilmaa
  // joka päivä, joten itsekalibrointi ajautuisi. Käyttäjä voi tehdä
  // manuaalisen kalibroinnin myöhemmin (V2 UI).
  uint16_t err = g_scd41.setAutomaticSelfCalibrationEnabled(0);
  if (err != 0) {
    DEBUG_PRINTF("[WARN]  SCD41: setAutomaticSelfCalibration failed (err=%u)\n", err);
  }

  err = g_scd41.startPeriodicMeasurement();
  if (err != 0) {
    DEBUG_PRINTF("[WARN]  SCD41: startPeriodicMeasurement failed (err=%u)\n", err);
    return false;
  }

  g_scd41MeasuringStarted = true;
  g_scd41Ready = true;
  return true;
}

static SensorProbeResult scd41_probe(void) {
  Wire.beginTransmission(I2C_ADDR_SCD41);
  int res = Wire.endTransmission();
  return (res == 0) ? SENSOR_PROBE_OK : SENSOR_PROBE_ABSENT;
}

static void scd41_read(SensorData* out) {
  if (!g_scd41Ready) return;

  // Two different non-results, deliberately kept apart. "No new sample yet"
  // is routine: the sensor converts every 5 s while we poll every 30 s, and
  // those periods are commensurate enough to phase-lock. An I2C error is not
  // routine and deserves a log line. Both leave envValid false for this
  // cycle; sensor_sticky.h in sensor_manager decides whether the previous
  // sample may stand in, so neither case blanks VPD on the wall panel.
  bool dataReady = false;
  uint16_t err = g_scd41.getDataReadyStatus(dataReady);
  if (err != 0) {
    DEBUG_PRINTF("[WARN]  SCD41: getDataReadyStatus err=%u\n", err);
    return;
  }
  if (!dataReady) return;

  uint16_t co2;
  float tC;
  float rh;
  err = g_scd41.readMeasurement(co2, tC, rh);
  if (err != 0) {
    DEBUG_PRINTF("[WARN]  SCD41: readMeasurement err=%u\n", err);
    return;
  }

  if (co2 == 0 || co2 > 40000) return;
  if (tC < -40.0f || tC > 85.0f) return;
  if (rh < 0.0f || rh > 100.0f) return;

  out->airCO2Ppm = (int)co2;
  out->airCO2Valid = true;
  out->airTempC = tC;
  out->airHumidity = rh;
  out->envValid = true;
}

static const SensorDriver g_scd41Driver = {
  .name = "SCD41",
  .readyFlag = &g_scd41Ready,
  .reprobeIntervalMs = SENSOR_REPROBE_I2C_MS,
  .init = scd41_init,
  .probe = scd41_probe,
  .read = scd41_read
};

#endif // HW_SCD41

#endif // SENSOR_DRIVER_SCD41_H
