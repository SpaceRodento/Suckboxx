/*=====================================================================
  sensor_orchestrator.h - Generic per-driver runtime orchestration.

  Header-only orchestration helper that:
    - Tracks last-probe timestamps per driver
    - Calls drv->probe() at drv->reprobeIntervalMs cadence
    - On SENSOR_PROBE_ABSENT + readyFlag was set → clears readyFlag
    - On SENSOR_PROBE_OK    + readyFlag was clear → calls drv->init()
    - Returns whether the driver should be read() this cycle

  The orchestrator knows nothing about which bus the driver uses. All
  bus-specific logic stays inside the driver. The orchestrator is the
  single source of truth for runtime presence and re-initialization,
  used by sensor_manager.h.

  Designed so it can be exercised by native unit tests without pulling
  in the full sensor_manager.h (which depends on Wire, calibration_math,
  and the physical SensorDriver instances).
=====================================================================*/

#ifndef SENSOR_ORCHESTRATOR_H
#define SENSOR_ORCHESTRATOR_H

#include "sensor_driver.h"

// Logging hook — defined as a macro so the orchestrator does not depend
// on config.h's DEBUG_PRINTF directly. Tests can override or disable it.
#ifndef SENSOR_ORCH_LOG
  #if defined(DEBUG_PRINTF)
    #define SENSOR_ORCH_LOG(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
  #else
    #define SENSOR_ORCH_LOG(fmt, ...) ((void)0)
  #endif
#endif

// State block for one driver. Caller (sensor_manager) owns an array of
// these, parallel to the SENSOR_DRIVERS[] table.
typedef struct {
  uint32_t lastProbeMs;        // 0 = never probed
  uint8_t  consecutiveErrors;  // For SENSOR_PROBE_ERROR backoff (unused for now)
  uint8_t  budgetOverruns;     // consecutive read() calls exceeding SENSOR_READ_BUDGET_MS
} SensorOrchState;

// Check if reprobe is due for this driver at time `now`.
// Returns true on first call (lastProbeMs == 0) only if reprobeIntervalMs > 0
// AND now == 0 case is intentionally suppressed — see sensor_manager.h.
static inline bool sensorOrch_reprobeDue(const SensorDriver* drv,
                                          const SensorOrchState* state,
                                          uint32_t now) {
  if (drv->reprobeIntervalMs == 0) return false;
  // Never-probed: wait one full interval (skip "hot init" probe at boot).
  if (state->lastProbeMs == 0) {
    return now >= drv->reprobeIntervalMs;
  }
  // Use unsigned subtraction so 32-bit rollover at ~49.7 days works.
  return (uint32_t)(now - state->lastProbeMs) >= drv->reprobeIntervalMs;
}

// Orchestrate one driver at time `now`. Mutates state and *(drv->readyFlag).
// Returns true if read() should be called for this driver this cycle.
static inline bool sensorOrch_tick(const SensorDriver* drv,
                                    SensorOrchState* state,
                                    uint32_t now) {
  if (sensorOrch_reprobeDue(drv, state, now)) {
    state->lastProbeMs = now;

    SensorProbeResult result = drv->probe();
    bool wasReady = *(drv->readyFlag);

    if (result == SENSOR_PROBE_ABSENT) {
      if (wasReady) {
        *(drv->readyFlag) = false;
        SENSOR_ORCH_LOG("[WARN]  Sensor %s: lost\n", drv->name);
      }
      state->consecutiveErrors = 0;
    } else if (result == SENSOR_PROBE_OK) {
      if (!wasReady) {
        if (drv->init()) {
          SENSOR_ORCH_LOG("[INFO]  Sensor %s: re-detected\n", drv->name);
        }
      }
      state->consecutiveErrors = 0;
    } else {
      // SENSOR_PROBE_ERROR: leave readyFlag alone, next probe will retry.
      if (state->consecutiveErrors < 255) state->consecutiveErrors++;
    }
  }

  return *(drv->readyFlag);
}

// Reset bookkeeping for one slot. Called from sensor_manager test helper.
static inline void sensorOrch_resetState(SensorOrchState* state) {
  state->lastProbeMs = 0;
  state->consecutiveErrors = 0;
  state->budgetOverruns = 0;
}

// Track whether this driver's read() completed within the allowed budget.
// elapsedMs: measured wall-clock duration of the last read() call.
// budgetMs:  maximum allowed duration (SENSOR_READ_BUDGET_MS from config.h).
//
// Behaviour:
//   overrun  → WARN log + increment budgetOverruns
//              3rd consecutive overrun: clear readyFlag + return true (caller
//              should raise an advisory for this driver)
//   on time  → reset budgetOverruns to 0 (streak broken)
//   returns  → true only when the readyFlag was NEWLY dropped this call
static inline bool sensorOrch_trackBudget(const SensorDriver* drv,
                                           SensorOrchState* state,
                                           uint32_t elapsedMs,
                                           uint32_t budgetMs) {
  if (elapsedMs > budgetMs) {
    SENSOR_ORCH_LOG("[WARN]  Sensor %s: luku kesti %lums (budjetti %lums)\n",
                    drv->name,
                    (unsigned long)elapsedMs,
                    (unsigned long)budgetMs);
    if (state->budgetOverruns < 255) state->budgetOverruns++;
    if (state->budgetOverruns >= 3 && *(drv->readyFlag)) {
      *(drv->readyFlag) = false;
      SENSOR_ORCH_LOG("[WARN]  Sensor %s: pudotettu (3 peraekkaista budjettirikkoa)\n",
                      drv->name);
      return true;  // newly dropped — caller should raise advisory
    }
  } else {
    state->budgetOverruns = 0;  // successful read within budget resets streak
  }
  return false;
}

#endif // SENSOR_ORCHESTRATOR_H
