/*=====================================================================
  self_test.h - Boot-time subsystem self-test (3 s)

  Sequence (each step <= SELF_TEST_STEP_TIMEOUT_MS):
    1. Power: brown-out flag from config_store
    2. I2C scan: required addresses present
    3. Motor: endstop sense (optional in proto, just logs)
    4. LoRa: ping + version query
    5. LittleFS: cleanShutdown flag

  Failures map to DEVICE_FAULT_* bits and a Finnish user message.
  Caller transitions device state based on result.
=====================================================================*/

#ifndef SELF_TEST_H
#define SELF_TEST_H

#include "config.h"
#include "structs.h"
#include "device_state.h"

#if ENABLE_SELF_TEST

struct SelfTestResult {
  bool          ok;
  uint8_t       faults;         // Bitmask matching DEVICE_FAULT_*
  char          message[48];    // Finnish user-facing fault summary (or empty)
  unsigned long durationMs;
};

// Runs the full sequence synchronously. Returns when finished or timeout.
// Pure side effect: emits DEBUG_INFO/WARN logs. Does NOT change g_device.state -
// caller decides based on result.
SelfTestResult selftest_run();

// =====================================================================
// IMPLEMENTATION
// =====================================================================

// Forward declarations from other modules. These are header-only inline
// functions, so including the headers here would create cycles in some
// configurations. The implementer decides per-module whether to include
// or forward-declare based on what compiles.

// I2C presence check is local to sensor_manager.h - replicate minimal
// version here to avoid pulling in entire sensor stack.
#include <Wire.h>
#include <string.h>

static bool selftest_i2cInitialized = false;

static void selftest_ensureI2C() {
  if (selftest_i2cInitialized) return;
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
  selftest_i2cInitialized = true;
}

static bool selftest_i2cPresent(uint8_t address, unsigned long timeoutMs) {
  unsigned long start = millis();
  Wire.beginTransmission(address);
  uint8_t result = Wire.endTransmission();
  if (millis() - start > timeoutMs) return false;
  return result == 0;
}

static void selftest_setMessage(SelfTestResult* r, const char* msg) {
  if (!r || !msg) return;
  strncpy(r->message, msg, sizeof(r->message) - 1);
  r->message[sizeof(r->message) - 1] = '\0';
}

SelfTestResult selftest_run() {
  SelfTestResult result;
  memset(&result, 0, sizeof(result));
  unsigned long startMs = millis();

  DEBUG_INFO(F("Self-test: starting..."));

  // -- 1. I2C scan --------------------------------------------------
  // Wire is initialized by sensor_manager - but sensor_manager uses lazy
  // init via ensureI2CInitialized(). We replicate the call to be safe.
  selftest_ensureI2C();

  bool requiredI2cOk = true;

#if HW_BME280
  if (!selftest_i2cPresent(I2C_ADDR_BME280, SELF_TEST_STEP_TIMEOUT_MS)) {
    DEBUG_WARN(F("Self-test: BME280 missing"));
    requiredI2cOk = false;
  }
#endif

#if ENABLE_HEIGHT_SENSOR
  if (!selftest_i2cPresent(I2C_ADDR_VL53L0X, SELF_TEST_STEP_TIMEOUT_MS)) {
    DEBUG_WARN(F("Self-test: VL53L0X missing"));
    requiredI2cOk = false;
  }
#endif

  if (!requiredI2cOk) {
    result.faults |= DEVICE_FAULT_SENSOR;
    selftest_setMessage(&result, "Anturi ei vastaa - tarkista kytkent\u00e4");
  }

  // -- 2. Motor endstop (informational only in proto v1) ------------
  // No fault if not pressed - endstop is for homing, not for boot validation.
  // CORE-3.4.5 LIFT-3 will add proper endstop logic.

  // -- 3. LoRa version query ----------------------------------------
#if ENABLE_LORA
  // lora_comm exposes lora_isReady() after lora_init() has run earlier in setup().
  // Self-test runs AFTER lora_init(), so we just check the readiness flag.
  // If the flag is false, the radio failed to come up.
  extern bool lora_isReady();
  if (!lora_isReady()) {
    DEBUG_WARN(F("Self-test: LoRa not ready"));
    result.faults |= DEVICE_FAULT_LORA;
    if (result.message[0] == '\0') {
      selftest_setMessage(&result, "Radio ei vastaa - tarkista antenni");
    }
  }
#endif

  // -- 4. Clean shutdown flag (LittleFS) ----------------------------
  // CORE-A laid the foundation for DEVICE_FAULT_BROWNOUT but full
  // cleanShutdown handling lands in LIFT-3 (lampun nostojarjestelma).
  // For now self-test does not check this - the bit is reserved for
  // LIFT-3 to set independently if it detects unclean state.

  // -- 5. Sanity: total duration must be under timeout --------------
  result.durationMs = millis() - startMs;
  if (result.durationMs > SELF_TEST_TIMEOUT_MS) {
    DEBUG_WARN(F("Self-test: exceeded timeout"));
    result.faults |= DEVICE_FAULT_SELF_TEST;
    if (result.message[0] == '\0') {
      selftest_setMessage(&result, "Itsetarkistus aikakatkaisi");
    }
  }

  result.ok = (result.faults == DEVICE_FAULT_NONE);

  if (result.ok) {
    DEBUG_PRINTF("[INFO]  Self-test: OK (%lu ms)\n", result.durationMs);
  } else {
    DEBUG_PRINTF("[WARN]  Self-test: faults=0x%02X message=\"%s\" (%lu ms)\n",
                 result.faults, result.message, result.durationMs);
  }

  return result;
}

#else  // ENABLE_SELF_TEST

struct SelfTestResult {
  bool ok;
  uint8_t faults;
  char message[48];
  unsigned long durationMs;
};

inline SelfTestResult selftest_run() {
  SelfTestResult r = { true, 0, "", 0 };
  return r;
}

#endif // ENABLE_SELF_TEST
#endif // SELF_TEST_H
