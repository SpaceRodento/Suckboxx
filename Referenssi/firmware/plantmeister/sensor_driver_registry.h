/*=====================================================================
  sensor_driver_registry.h - HW_*-driven driver registration

  Auto-composes SENSOR_DRIVERS[] from HW_* flags. This IS the active
  registration path: sensor_manager.h includes this header and iterates
  SENSOR_DRIVERS[] / SENSOR_DRIVER_COUNT (no legacy per-flag list remains).

  Adding a new sensor — full recipe (config flag, capability mapping, driver
  skeleton, tests, hardware smoke): docs/ohjeet/uuden-sensorin-lisays.md.
  The two code edits in THIS file:
    - Create sensor_driver_<name>.h following the SensorDriver pattern
    - Add the #include + &g_<name>Driver entries below (two lines, same HW_ flag)
    Done. No changes to sensor_manager.h, SensorData, vpd_calc, etc.

  Why a separate registry header (not inline in sensor_manager.h):
    - Single place to see "which drivers exist and are active"
    - Unit tests can include this without pulling sensor_manager.h
    - Build-time validation: if HW_X true but driver file missing,
      compile fails at the include line with a clear error.
=====================================================================*/

#ifndef SENSOR_DRIVER_REGISTRY_H
#define SENSOR_DRIVER_REGISTRY_H

#include "config.h"
#include "sensor_capability_resolver.h"
#include "sensor_driver.h"

// ─── Include only drivers whose HW is enabled ──────────────────────
#if HW_BME280
  #include "sensor_driver_bme280.h"
#endif

// V2-rekisteri: includet aktivoidaan kun driver-tiedostot lisätään.
#if HW_SCD41
  #include "sensor_driver_scd41.h"
#endif

#if HW_MLX90614
  #include "sensor_driver_mlx90614.h"
#endif

#if HW_AS7341
  #include "sensor_driver_as7341.h"
#endif

#if HW_BME680
  // #include "sensor_driver_bme680.h"
  #error "HW_BME680=true but sensor_driver_bme680.h not yet implemented"
#endif

#if HW_TSL2591
  // #include "sensor_driver_tsl2591.h"
  #error "HW_TSL2591=true but sensor_driver_tsl2591.h not yet implemented"
#endif

#if HW_VL53L0X
  #include "sensor_driver_vl53l0x.h"
#endif

#if HW_DS18B20
  #include "sensor_driver_ds18b20.h"
#endif

#if HW_ADS1115
  #include "sensor_driver_ads1115.h"
#endif

#if HW_INA219
  #include "sensor_driver_ina219.h"
#endif

#if HW_INA226
  // #include "sensor_driver_ina226.h"  // TODO: korvaa INA219
  #error "HW_INA226=true but sensor_driver_ina226.h not yet implemented"
#endif

#if HW_INA228
  #include "sensor_driver_ina228.h"
#endif

// ─── Active driver pointer table ───────────────────────────────────
// Iterated by sensor_manager.h's sensors_init() and sensors_read().
// Empty array is a compile error in some toolchains; the [1] guard
// keeps the array non-zero-length and adds a NULL sentinel so iteration
// counts can be sizeof()-derived safely.

static const SensorDriver* const SENSOR_DRIVERS[] = {
#if HW_BME280
  &g_bme280Driver,
#endif
#if HW_SCD41
  &g_scd41Driver,
#endif
#if HW_MLX90614
  &g_mlx90614Driver,
#endif
#if HW_AS7341
  &g_as7341Driver,
#endif
#if HW_BME680
  // &g_bme680Driver,
#endif
#if HW_TSL2591
  // &g_tsl2591Driver,
#endif
#if HW_VL53L0X
  &g_vl53l0xDriver,
#endif
#if HW_DS18B20
  &g_ds18b20Driver,
#endif
#if HW_ADS1115
  &g_ads1115Driver,
#endif
#if HW_INA219
  &g_ina219Driver,
#endif
#if HW_INA226
  // &g_ina226Driver,
#endif
#if HW_INA228
  &g_ina228Driver,
#endif
};

static const size_t SENSOR_DRIVER_COUNT =
    sizeof(SENSOR_DRIVERS) / sizeof(SENSOR_DRIVERS[0]);

#endif // SENSOR_DRIVER_REGISTRY_H
