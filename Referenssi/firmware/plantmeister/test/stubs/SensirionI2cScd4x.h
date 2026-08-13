#pragma once
// Minimal SensirionI2cScd4x stub for native tests.
// HUOM: metodinimien on vastattava OIKEAA kirjastoa (Sensirion I2C SCD4x):
// stubin ja kirjaston API-ajautuminen piilottaa rautabuildin käännösvirheet
// (esim. setAutomaticSelfCalibrationEnabled vs vanha ...SelfCalibration).
// Oikea gate on `pio run -e xiao_esp32s3_v2`, ei pelkkä natiivitesti.

#include <stdint.h>

class TwoWire;
extern TwoWire Wire;

extern bool     g_scd41_stub_dataReady;
extern uint16_t g_scd41_stub_co2;
extern float    g_scd41_stub_tempC;
extern float    g_scd41_stub_humidity;
extern uint16_t g_scd41_stub_readErr;
extern uint16_t g_scd41_stub_dataReadyErr;
extern uint16_t g_scd41_stub_startErr;

class SensirionI2cScd4x {
public:
  void begin(TwoWire&, uint8_t) {}
  uint16_t stopPeriodicMeasurement() { return 0; }
  uint16_t setAutomaticSelfCalibrationEnabled(uint16_t) { return 0; }
  uint16_t startPeriodicMeasurement() { return g_scd41_stub_startErr; }
  uint16_t getDataReadyStatus(bool& ready) {
    ready = g_scd41_stub_dataReady;
    return g_scd41_stub_dataReadyErr;
  }
  uint16_t readMeasurement(uint16_t& co2, float& tC, float& rh) {
    if (g_scd41_stub_readErr != 0) return g_scd41_stub_readErr;
    co2 = g_scd41_stub_co2;
    tC  = g_scd41_stub_tempC;
    rh  = g_scd41_stub_humidity;
    return 0;
  }
};
