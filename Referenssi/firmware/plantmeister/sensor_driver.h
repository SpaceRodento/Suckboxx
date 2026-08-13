/* sensor_driver.h - common sensor driver interface (header-only)

   Lightweight function-pointer table for sensor drivers.
   Designed for header-only inclusion and native unit testing.

   v2 (2026-05-29): driver expose readyFlag pointer and reprobeIntervalMs so
   sensor_manager can orchestrate runtime presence checks + re-initialization
   without knowing anything about the underlying bus (I2C / OneWire / SPI / ...).
*/

#ifndef SENSOR_DRIVER_H
#define SENSOR_DRIVER_H

#include "structs.h"

#include <Wire.h>

// Cheap I2C presence helper used by drivers to avoid blocking library
// .begin() calls when device is absent. Returns true if device ACKs.
static inline bool isI2CDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

typedef enum {
  SENSOR_PROBE_OK,         // device responded
  SENSOR_PROBE_ABSENT,     // not on bus
  SENSOR_PROBE_ERROR       // bus busy / unknown error
} SensorProbeResult;

// Lightweight driver interface. Implementations must be header-only
// static functions/objects so they can be included in native unit tests.
//
// Manager contract:
//   - sensor_manager calls init() once at boot.
//   - At runtime sensor_manager calls probe() every reprobeIntervalMs ms.
//       * SENSOR_PROBE_ABSENT + readyFlag was true => clear readyFlag, log "lost"
//       * SENSOR_PROBE_OK     + readyFlag was false => call init() again, log "re-detected" on success
//   - read() is called every cycle but only when *readyFlag is true.
//
// The driver owns the storage for its ready flag (static bool g_xxxReady)
// and exposes a pointer so the manager can both read and mutate it without
// knowing per-driver function names.
typedef struct {
  const char* name;

  // Pointer to driver-owned ready flag. MUST be non-null.
  bool* readyFlag;

  // Reprobe interval in milliseconds. 0 disables runtime probing for this
  // driver (init-only — once ready, stays ready until reboot).
  uint32_t reprobeIntervalMs;

  // Called from sensors_init() and from sensor_manager when probe() returns
  // SENSOR_PROBE_OK while readyFlag is false. Drivers must perform the
  // presence check themselves and only flip readyFlag to true on success.
  // Return true if ready (mirrors *readyFlag after call).
  bool (*init)(void);

  // Cheap periodic probe. Should perform a minimal bus-level presence check
  // and return one of SensorProbeResult values. MUST NOT touch readyFlag —
  // that is the manager's responsibility.
  SensorProbeResult (*probe)(void);

  // Fill the provided SensorData struct with latest values. Set validity
  // flags inside the struct as appropriate. Implementations should be
  // non-blocking and safe to call frequently. Called only when *readyFlag.
  void (*read)(SensorData* out);
} SensorDriver;

// Suggested reprobe intervals (drivers may override).
#ifndef SENSOR_REPROBE_I2C_MS
#define SENSOR_REPROBE_I2C_MS      10000   // 10 s — cheap ACK check
#endif
#ifndef SENSOR_REPROBE_ONEWIRE_MS
#define SENSOR_REPROBE_ONEWIRE_MS  30000   // 30 s — OneWire bus scan
#endif

#endif // SENSOR_DRIVER_H
