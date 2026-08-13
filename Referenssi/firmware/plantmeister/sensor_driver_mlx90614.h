// sensor_driver_mlx90614.h - Melexis MLX90614 IR leaf-temperature driver
//
// Phase 0: read object temperature (leaf) + ambient (sanity check)
// into SensorData.leafTempC / leafAmbientC.
//
// Used by VPD-improvement (Phase 1, light_calc / vpd_calc upgrade) — for now
// the value is exposed in /api/state and SensorData but VPD still uses T_air.

#ifndef SENSOR_DRIVER_MLX90614_H
#define SENSOR_DRIVER_MLX90614_H

#include "sensor_driver.h"
#include "config.h"

#if HW_MLX90614
#include <Adafruit_MLX90614.h>

static Adafruit_MLX90614 g_mlx90614;
static bool g_mlx90614Ready = false;

static bool mlx90614_isReady(void) { return g_mlx90614Ready; }

#ifdef PLANTMEISTER_NATIVE_TEST
static void mlx90614_resetForTest(void) {
  g_mlx90614Ready = false;
}
#endif

static bool mlx90614_init(void) {
  if (!isI2CDevicePresent(I2C_ADDR_MLX90614)) {
    g_mlx90614Ready = false;
    return false;
  }
  if (g_mlx90614Ready) return true;

  if (!g_mlx90614.begin(I2C_ADDR_MLX90614, &Wire)) {
    DEBUG_WARN(F("MLX90614: begin() failed despite I2C presence"));
    g_mlx90614Ready = false;
    return false;
  }
  g_mlx90614Ready = true;
  return true;
}

static SensorProbeResult mlx90614_probe(void) {
  Wire.beginTransmission(I2C_ADDR_MLX90614);
  int res = Wire.endTransmission();
  return (res == 0) ? SENSOR_PROBE_OK : SENSOR_PROBE_ABSENT;
}

static void mlx90614_read(SensorData* out) {
  if (!g_mlx90614Ready) return;

  // readObjectTempC + readAmbientTempC kumpikin tekee yhden 2-tavun I2C
  // -luennan; ei blokkaa pitkaa. NaN tarkoittaa lukuvirhetta.
  float tObj = g_mlx90614.readObjectTempC();
  float tAmb = g_mlx90614.readAmbientTempC();

  // Sanity bounds: PlantMeisterin operating range -10..+60 C. NaN ja
  // raja-arvojen ulkopuoliset arvot kuvastavat sensori- tai johtovirheita.
  if (isnan(tObj) || isnan(tAmb)) return;
  if (tObj < -40.0f || tObj > 125.0f) return;
  if (tAmb < -40.0f || tAmb > 125.0f) return;

  out->leafTempC      = tObj;
  out->leafAmbientC   = tAmb;
  out->leafTempValid  = true;
}

static const SensorDriver g_mlx90614Driver = {
  .name = "MLX90614",
  .readyFlag = &g_mlx90614Ready,
  .reprobeIntervalMs = SENSOR_REPROBE_I2C_MS,
  .init = mlx90614_init,
  .probe = mlx90614_probe,
  .read = mlx90614_read
};

#endif // HW_MLX90614

#endif // SENSOR_DRIVER_MLX90614_H
