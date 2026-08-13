/*=====================================================================
  sensor_capability_resolver.h - Capability → Provider mapping

  Resolves at compile time which HW_* sensor provides each CAP_*
  capability. Single source of truth for "which sensor does what".

  PURPOSE
  -------
  Decouples "what we measure" (CAP_*) from "what hardware is on the
  board" (HW_*). Sensor_manager and post-processing code (vpd_calc,
  scheduler) never reference specific sensor models — they reference
  capabilities. Swapping BME280 → SCD41 is a one-line config change.

  PRIORITY ORDER (when multiple HW_* can satisfy a CAP_*)
  -------------------------------------------------------
  Higher-quality / more capable sensor wins. Priority chosen so that
  if you add a better sensor and leave the cheaper one in place, the
  better one is used automatically.

    CAP_AIR_TEMP_HUMIDITY:  SCD41 > BME280 > BME680
      (SCD41 is the V2 default; BME280 is V1 legacy; BME680 is VOC-first)

    CAP_AIR_CO2:            SCD41   (only NDIR CO2 provider)

    CAP_AIR_PRESSURE:       BME280 > BME680   (SCD41 ei mittaa painetta)

    CAP_AIR_VOC:            BME680   (only VOC provider)

    CAP_LEAF_TEMP:          MLX90614   (only IR leaf-temp provider)

    CAP_LIGHT_PAR:          AS7341 > TSL2591   (AS7341 tarkempi, spektri)

    CAP_LIGHT_SPECTRUM:     AS7341   (vain spektrianturi)

    CAP_PLANT_HEIGHT:       VL53L0X   (ainoa ToF-provider)

    CAP_WATER_TEMP:         DS18B20   (ainoa OneWire-provider)

    CAP_WATER_LEVEL:        FLOAT_SWITCH_MIN

    CAP_WATER_TDS:          ADS1115 + analog probe

    CAP_SUBSTRATE_MOISTURE: ADS1115 + capacitive probe

    CAP_POWER_MONITORING:   INA228 > INA226 > INA219
      (INA228 on 20-bit + energia/varaus-akkumulaattorit; uusin/tarkin)

  Validation: if a CAP_* is enabled but no HW_* can provide it, the
  resolver emits a compile error pointing to the missing HW_*.

  USAGE
  -----
  Include this file from sensor_manager.h (or any code that needs
  to know "which driver provides T+RH"). The USE_<DRIVER>_FOR_<CAP>
  defines that this file generates are the contract.
=====================================================================*/

#ifndef SENSOR_CAPABILITY_RESOLVER_H
#define SENSOR_CAPABILITY_RESOLVER_H

#include "config.h"

// ─── Air temperature + humidity ────────────────────────────────────
#if CAP_AIR_TEMP_HUMIDITY
  #if HW_SCD41
    #define USE_SCD41_FOR_AIR_TH       1
    #define AIR_TH_PROVIDER_NAME       "SCD41"
  #elif HW_BME280
    #define USE_BME280_FOR_AIR_TH      1
    #define AIR_TH_PROVIDER_NAME       "BME280"
  #elif HW_BME680
    #define USE_BME680_FOR_AIR_TH      1
    #define AIR_TH_PROVIDER_NAME       "BME680"
  #else
    #error "CAP_AIR_TEMP_HUMIDITY enabled but no provider HW available (need HW_SCD41, HW_BME280, or HW_BME680)"
  #endif
#endif

// ─── Air CO2 ───────────────────────────────────────────────────────
#if CAP_AIR_CO2
  #if HW_SCD41
    #define USE_SCD41_FOR_CO2          1
    #define AIR_CO2_PROVIDER_NAME      "SCD41"
  #else
    #error "CAP_AIR_CO2 enabled but no provider HW available (only HW_SCD41 supported)"
  #endif
#endif

// ─── Air pressure ──────────────────────────────────────────────────
#if CAP_AIR_PRESSURE
  #if HW_BME280
    #define USE_BME280_FOR_PRESSURE    1
    #define AIR_PRESSURE_PROVIDER_NAME "BME280"
  #elif HW_BME680
    #define USE_BME680_FOR_PRESSURE    1
    #define AIR_PRESSURE_PROVIDER_NAME "BME680"
  #else
    #error "CAP_AIR_PRESSURE enabled but no provider HW available (need HW_BME280 or HW_BME680; SCD41 ei mittaa painetta)"
  #endif
#endif

// ─── Air VOC ───────────────────────────────────────────────────────
#if CAP_AIR_VOC
  #if HW_BME680
    #define USE_BME680_FOR_VOC         1
    #define AIR_VOC_PROVIDER_NAME      "BME680"
  #else
    #error "CAP_AIR_VOC enabled but no provider HW available (only HW_BME680 supported)"
  #endif
#endif

// ─── Leaf temperature ──────────────────────────────────────────────
#if CAP_LEAF_TEMP
  #if HW_MLX90614
    #define USE_MLX90614_FOR_LEAF_T    1
    #define LEAF_TEMP_PROVIDER_NAME    "MLX90614"
  #else
    #error "CAP_LEAF_TEMP enabled but no provider HW available (only HW_MLX90614 supported)"
  #endif
#endif

// ─── Light PAR / PPFD ──────────────────────────────────────────────
#if CAP_LIGHT_PAR
  #if HW_AS7341
    #define USE_AS7341_FOR_PAR         1
    #define LIGHT_PAR_PROVIDER_NAME    "AS7341"
  #elif HW_TSL2591
    #define USE_TSL2591_FOR_PAR        1
    #define LIGHT_PAR_PROVIDER_NAME    "TSL2591"
  #else
    #error "CAP_LIGHT_PAR enabled but no provider HW available (need HW_AS7341 or HW_TSL2591)"
  #endif
#endif

// ─── Light spectrum ────────────────────────────────────────────────
#if CAP_LIGHT_SPECTRUM
  #if HW_AS7341
    #define USE_AS7341_FOR_SPECTRUM    1
    #define LIGHT_SPECTRUM_PROVIDER_NAME "AS7341"
  #else
    #error "CAP_LIGHT_SPECTRUM enabled but no provider HW available (only HW_AS7341 supported)"
  #endif
#endif

// ─── Plant height (ToF) ────────────────────────────────────────────
#if CAP_PLANT_HEIGHT
  #if HW_VL53L0X
    #define USE_VL53L0X_FOR_HEIGHT     1
    #define PLANT_HEIGHT_PROVIDER_NAME "VL53L0X"
  #else
    #error "CAP_PLANT_HEIGHT enabled but no provider HW available (only HW_VL53L0X supported)"
  #endif
#endif

// ─── Water temperature ─────────────────────────────────────────────
#if CAP_WATER_TEMP
  #if HW_DS18B20
    #define USE_DS18B20_FOR_WATER_T    1
    #define WATER_TEMP_PROVIDER_NAME   "DS18B20"
  #else
    #error "CAP_WATER_TEMP enabled but no provider HW available (only HW_DS18B20 supported)"
  #endif
#endif

// ─── Water level (min) ─────────────────────────────────────────────
#if CAP_WATER_LEVEL
  #if HW_FLOAT_SWITCH_MIN
    #define USE_FLOAT_SWITCH_FOR_LEVEL 1
    #define WATER_LEVEL_PROVIDER_NAME  "FLOAT_SWITCH"
  #else
    #error "CAP_WATER_LEVEL enabled but no provider HW available (need HW_FLOAT_SWITCH_MIN)"
  #endif
#endif

// ─── Water overflow ────────────────────────────────────────────────
#if CAP_WATER_OVERFLOW
  #if HW_FLOAT_SWITCH_OVERFLOW
    #define USE_FLOAT_SWITCH_FOR_OVERFLOW 1
  #else
    #error "CAP_WATER_OVERFLOW enabled but HW_FLOAT_SWITCH_OVERFLOW=false"
  #endif
#endif

// ─── Water TDS ─────────────────────────────────────────────────────
#if CAP_WATER_TDS
  #if HW_ADS1115
    #define USE_ADS1115_FOR_TDS        1
    #define WATER_TDS_PROVIDER_NAME    "ADS1115+TDS-probe"
  #else
    #error "CAP_WATER_TDS enabled but no provider HW available (need HW_ADS1115 + analog probe)"
  #endif
#endif

// ─── Substrate moisture ────────────────────────────────────────────
#if CAP_SUBSTRATE_MOISTURE
  #if HW_ADS1115
    #define USE_ADS1115_FOR_SUBSTRATE_MOISTURE 1
    #define SUBSTRATE_MOISTURE_PROVIDER_NAME "ADS1115+cap-probe"
  #else
    #error "CAP_SUBSTRATE_MOISTURE enabled but no provider HW available (need HW_ADS1115 + DFRobot SEN0193)"
  #endif
#endif

// ─── Power monitoring ──────────────────────────────────────────────
#if CAP_POWER_MONITORING
  #if HW_INA228
    #define USE_INA228_FOR_POWER       1
    #define POWER_MON_PROVIDER_NAME    "INA228"
  #elif HW_INA226
    #define USE_INA226_FOR_POWER       1
    #define POWER_MON_PROVIDER_NAME    "INA226"
  #elif HW_INA219
    #define USE_INA219_FOR_POWER       1
    #define POWER_MON_PROVIDER_NAME    "INA219"
  #else
    #error "CAP_POWER_MONITORING enabled but no provider HW available (need HW_INA228, HW_INA226 or HW_INA219)"
  #endif
#endif

// ─── Sanity check: INA219 / INA226 / INA228 all share I2C address 0x40 ──
#if (HW_INA219 && HW_INA226) || (HW_INA219 && HW_INA228) || (HW_INA226 && HW_INA228)
  #error "Only one of HW_INA219 / HW_INA226 / HW_INA228 may be enabled — they share I2C 0x40"
#endif

// ─── Sanity check: BME280 and BME680 default address overlap ───────
// BME280 default 0x76, BME680 default 0x77 — OK if SDO straps differ.
// No error, just a note for the user to verify wiring.

#endif // SENSOR_CAPABILITY_RESOLVER_H
