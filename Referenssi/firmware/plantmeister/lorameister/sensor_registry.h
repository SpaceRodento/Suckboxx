/*=====================================================================
  sensor_registry.h - Generic Sensor Registration System

  LoraMeister - Automatic sensor management

  PURPOSE:
  Provides a zero-boilerplate sensor registration system.
  Adding a new sensor requires ONLY:
  1. Create xxx_sensor.h with REGISTER_SENSOR() call
  2. Add ENABLE_XXX flag in config.h
  3. #include "xxx_sensor.h" in firmware.ino

  The registry automatically handles:
  - Sensor initialization at boot
  - Periodic sensor updates in main loop
  - Payload building (appending sensor data to LoRa message)
  - Payload parsing (extracting sensor data on receiver)

  USAGE:
    // In your_sensor.h:
    #include "sensor_registry.h"

    static float g_temperature = 0;

    void myInit() { ... }
    void myUpdate() { ... }
    int myBuildPayload(char* buf, int maxLen) {
      return snprintf(buf, maxLen, "TEMP:%.1f", g_temperature);
    }
    void myParsePayload(const char* key, const char* val, DeviceState& dev) {
      if (strcmp(key, "TEMP") == 0) dev.sensorValues[0] = atof(val);
    }

    REGISTER_SENSOR("DHT22", myInit, myUpdate, myBuildPayload, myParsePayload);

  ARCHITECTURE:
  Uses a static array of SensorEntry structs. Sensors self-register
  via REGISTER_SENSOR macro which creates a static initializer.
  The registry is iterated by firmware.ino at key points:
  - setup() calls sensorRegistry_initAll()
  - loop() calls sensorRegistry_updateAll()
  - buildPayload() calls sensorRegistry_buildAll()
  - parsePayload() calls sensorRegistry_parseField()
=======================================================================*/

#ifndef SENSOR_REGISTRY_H
#define SENSOR_REGISTRY_H

#include "config.h"

// Forward declare DeviceState (defined in structs.h, included before this)
struct DeviceState;

// ═══════════════════════════════════════════════════════════════════
// SENSOR REGISTRY TYPES
// ═══════════════════════════════════════════════════════════════════

// Maximum number of registered sensors
#define MAX_REGISTERED_SENSORS 8

// Generic sensor values stored in DeviceState (for receiver-side parsing)
#define MAX_SENSOR_VALUES 8

// Function pointer types for sensor operations
typedef void (*SensorInitFn)();
typedef void (*SensorUpdateFn)();
typedef int  (*SensorBuildPayloadFn)(char* buffer, int maxLen);
typedef bool (*SensorParseFieldFn)(const char* key, const char* value, DeviceState& dev);

// ═══════════════════════════════════════════════════════════════════
// SENSOR ENTRY STRUCTURE
// ═══════════════════════════════════════════════════════════════════

struct SensorEntry {
  const char* name;               // Sensor name (e.g., "DHT22", "MAX4466")
  SensorInitFn init;              // Called once in setup()
  SensorUpdateFn update;          // Called every loop() iteration
  SensorBuildPayloadFn buildPayload;  // Append sensor data to payload
  SensorParseFieldFn parseField;  // Parse a key:value pair from received payload
  bool active;                    // Is this entry in use?
};

// ═══════════════════════════════════════════════════════════════════
// REGISTRY STATE
// ═══════════════════════════════════════════════════════════════════

static SensorEntry g_sensorRegistry[MAX_REGISTERED_SENSORS];
static int g_sensorCount = 0;
static bool g_registryInitialized = false;

// ═══════════════════════════════════════════════════════════════════
// REGISTRATION FUNCTION
// ═══════════════════════════════════════════════════════════════════

/**
 * Register a sensor with the registry.
 * Called automatically by REGISTER_SENSOR macro.
 * Returns the sensor index (for verification).
 */
inline int sensorRegistry_add(const char* name,
                               SensorInitFn initFn,
                               SensorUpdateFn updateFn,
                               SensorBuildPayloadFn buildFn,
                               SensorParseFieldFn parseFn) {
  if (g_sensorCount >= MAX_REGISTERED_SENSORS) {
    return -1;  // Registry full
  }

  int idx = g_sensorCount;
  g_sensorRegistry[idx].name = name;
  g_sensorRegistry[idx].init = initFn;
  g_sensorRegistry[idx].update = updateFn;
  g_sensorRegistry[idx].buildPayload = buildFn;
  g_sensorRegistry[idx].parseField = parseFn;
  g_sensorRegistry[idx].active = true;
  g_sensorCount++;

  return idx;
}

// ═══════════════════════════════════════════════════════════════════
// REGISTRY OPERATIONS (called by firmware.ino)
// ═══════════════════════════════════════════════════════════════════

/**
 * Initialize all registered sensors.
 * Called once from setup().
 */
inline void sensorRegistry_initAll() {
  Serial.printf("[SENSORS] Initializing %d registered sensor(s)...\n", g_sensorCount);

  for (int i = 0; i < g_sensorCount; i++) {
    if (g_sensorRegistry[i].active && g_sensorRegistry[i].init) {
      Serial.printf("  [%d] %s... ", i, g_sensorRegistry[i].name);
      g_sensorRegistry[i].init();
      Serial.println("OK");
    }
  }

  g_registryInitialized = true;
  Serial.printf("[SENSORS] %d sensor(s) ready\n", g_sensorCount);
}

/**
 * Update all registered sensors.
 * Called every loop() iteration. Each sensor manages its own timing.
 */
inline void sensorRegistry_updateAll() {
  for (int i = 0; i < g_sensorCount; i++) {
    if (g_sensorRegistry[i].active && g_sensorRegistry[i].update) {
      g_sensorRegistry[i].update();
    }
  }
}

/**
 * Build payload from all registered sensors.
 * Each sensor appends its data as KEY:VALUE pairs.
 * Returns total characters written.
 *
 * @param buffer Output buffer
 * @param maxLen Maximum buffer length
 * @param offset Starting offset in buffer (to append after base payload)
 * @return Total bytes written
 */
inline int sensorRegistry_buildAll(char* buffer, int maxLen, int offset) {
  int pos = offset;

  for (int i = 0; i < g_sensorCount; i++) {
    if (g_sensorRegistry[i].active && g_sensorRegistry[i].buildPayload) {
      // Add comma separator if we have existing data
      if (pos > 0 && pos < maxLen - 1) {
        buffer[pos++] = ',';
      }

      int written = g_sensorRegistry[i].buildPayload(buffer + pos, maxLen - pos);
      if (written > 0) {
        pos += written;
      } else {
        // Sensor wrote nothing — remove the comma we just added
        if (pos > offset) pos--;
      }
    }
  }

  buffer[pos] = '\0';
  return pos;
}

/**
 * Try to parse a key:value field with registered sensors.
 * Called from parsePayload() for each field.
 *
 * @param key Field key (e.g., "TEMP")
 * @param value Field value (e.g., "22.5")
 * @param dev DeviceState to update
 * @return true if any sensor claimed the field
 */
inline bool sensorRegistry_parseField(const char* key, const char* value, DeviceState& dev) {
  for (int i = 0; i < g_sensorCount; i++) {
    if (g_sensorRegistry[i].active && g_sensorRegistry[i].parseField) {
      if (g_sensorRegistry[i].parseField(key, value, dev)) {
        return true;  // Field handled
      }
    }
  }
  return false;  // No sensor claimed this field
}

/**
 * Get number of registered sensors.
 */
inline int sensorRegistry_getCount() {
  return g_sensorCount;
}

/**
 * Get sensor name by index.
 */
inline const char* sensorRegistry_getName(int idx) {
  if (idx >= 0 && idx < g_sensorCount) {
    return g_sensorRegistry[idx].name;
  }
  return "unknown";
}

// ═══════════════════════════════════════════════════════════════════
// SELF-REGISTRATION MACRO
// ═══════════════════════════════════════════════════════════════════

/**
 * REGISTER_SENSOR - Self-register a sensor at static initialization time
 *
 * Usage:
 *   REGISTER_SENSOR("SensorName", initFn, updateFn, buildPayloadFn, parseFieldFn);
 *
 * This creates a static variable whose constructor calls sensorRegistry_add().
 * The sensor is registered before setup() runs.
 */
struct _SensorRegistrar {
  _SensorRegistrar(const char* name,
                   SensorInitFn initFn,
                   SensorUpdateFn updateFn,
                   SensorBuildPayloadFn buildFn,
                   SensorParseFieldFn parseFn) {
    sensorRegistry_add(name, initFn, updateFn, buildFn, parseFn);
  }
};

#define _SENSOR_REG_CONCAT(a, b) a##b
#define _SENSOR_REG_UNIQUE(prefix, line) _SENSOR_REG_CONCAT(prefix, line)

#define REGISTER_SENSOR(name, initFn, updateFn, buildFn, parseFn) \
  static _SensorRegistrar _SENSOR_REG_UNIQUE(_sensorReg_, __LINE__) \
    (name, initFn, updateFn, buildFn, parseFn)

#endif // SENSOR_REGISTRY_H
