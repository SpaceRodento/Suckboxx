/*=====================================================================
  test_feature_flags.h — Shared feature flags for native unit tests

  Include this BEFORE any project headers in test files.
  Provides the superset of flags needed across all test suites.
  Individual tests can #define overrides BEFORE including this file.
=====================================================================*/

#ifndef TEST_FEATURE_FLAGS_H
#define TEST_FEATURE_FLAGS_H

// Allow per-test overrides: if a flag is already defined, keep it
#ifndef ENABLE_LORA
  #define ENABLE_LORA             false
#endif
#ifndef ENABLE_WIFI_PORTAL
  #define ENABLE_WIFI_PORTAL      false
#endif
#ifndef ENABLE_SENSORS
  #define ENABLE_SENSORS          true
#endif
#ifndef ENABLE_MOTOR
  #define ENABLE_MOTOR            true
#endif
#ifndef ENABLE_PUMP
  #define ENABLE_PUMP             true
#endif
#ifndef ENABLE_LIGHT_RELAY
  #define ENABLE_LIGHT_RELAY      true
#endif
#ifndef ENABLE_AIR_PUMP
  #define ENABLE_AIR_PUMP         false
#endif
#ifndef ENABLE_BATTERY_MONITOR
  #define ENABLE_BATTERY_MONITOR  false
#endif
#ifndef ENABLE_DEEP_SLEEP
  #define ENABLE_DEEP_SLEEP       false
#endif
#ifndef ENABLE_SENSOR_HISTORY
  #define ENABLE_SENSOR_HISTORY   false
#endif
#ifndef ENABLE_GUIDED_GROWING
  #define ENABLE_GUIDED_GROWING   true
#endif
#ifndef ENABLE_GUIDED_GROWING_UI
  #define ENABLE_GUIDED_GROWING_UI          false
#endif
#ifndef ENABLE_GUIDED_GROWING_SIMULATION
  #define ENABLE_GUIDED_GROWING_SIMULATION  false
#endif
#ifndef ENABLE_EBB_FLOW
  #define ENABLE_EBB_FLOW                   false
#endif
#ifndef ENABLE_RESERVOIR_LEVEL_SAFETY
  #define ENABLE_RESERVOIR_LEVEL_SAFETY     false
#endif
#ifndef ENABLE_OVERFLOW_SWITCH
  #define ENABLE_OVERFLOW_SWITCH            false
#endif
#ifndef ENABLE_BRAKE
  #define ENABLE_BRAKE            false
#endif
#ifndef ENABLE_TDS_SENSOR
  #define ENABLE_TDS_SENSOR       true
#endif
#ifndef ENABLE_WATER_TEMP
  #define ENABLE_WATER_TEMP       false
#endif
#ifndef ENABLE_FLOAT_SWITCH
  #define ENABLE_FLOAT_SWITCH     true
#endif
#ifndef ENABLE_HEIGHT_SENSOR
  #define ENABLE_HEIGHT_SENSOR    true
#endif
#ifndef ENABLE_ENV_SENSOR
  #define ENABLE_ENV_SENSOR       true
#endif
#ifndef ENABLE_MPG
  #define ENABLE_MPG              false
#endif
#ifndef ENABLE_DEBUG
  #define ENABLE_DEBUG            false
#endif

#endif // TEST_FEATURE_FLAGS_H
