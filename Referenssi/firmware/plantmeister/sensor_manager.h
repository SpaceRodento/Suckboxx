/*=====================================================================
  sensor_manager.h - Sensor Reading Manager

  Reads all sensors and fills SensorData struct.

  Orchestration model (v2, 2026-05-29):
    - sensors_init() calls drv->init() once per driver at boot.
    - sensors_read() iterates all drivers and:
        * If driver has not been probed in reprobeIntervalMs:
            probe() → on ABSENT clear readyFlag (log "lost"),
                       on OK while !readyFlag call init() (log "re-detected")
        * If readyFlag is set: call read() to fill SensorData.
    - The manager does NOT know which bus (I2C / OneWire / SPI / ...) a
      driver uses. All bus-specific logic lives in the driver itself.

  Libraries required (via driver wrappers):
    - Adafruit VL53L0X / BME280 (+Unified Sensor) / ADS1X15 / INA219
    - OneWire + DallasTemperature
=====================================================================*/

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "config.h"
#include "structs.h"
#include "device_state.h"
#include "calibration_math.h"

#if ENABLE_SENSORS

#include <Wire.h>
#if ENABLE_MCP23017
  // FLOAT_MIN relocates to the MCP23017 (GPA6) when the fan takes native D6.
  #include "mcp23017_hal.h"
#endif

// Sensor driver registry composes SENSOR_DRIVERS[] + SENSOR_DRIVER_COUNT
// from HW_* flags via capability_resolver. See sensor_driver_registry.h.
#include "sensor_driver.h"
#include "sensor_orchestrator.h"
#include "sensor_sticky.h"
#include "sensor_driver_registry.h"
#include "vpd_calc.h"
#include "light_calc.h"      // PPFD estimation from AS7341 spectrum
#include "dli_tracker.h"     // DLI accumulation (mol/m2/d)
#include "sensor_sim.h"      // Optional hardware-free synthetic readings

// Orchestration state per driver, parallel to SENSOR_DRIVERS[].
static SensorOrchState g_sensorOrchState[
  (SENSOR_DRIVER_COUNT > 0) ? SENSOR_DRIVER_COUNT : 1
] = {};

// Last-good environment sample. Air temp and humidity are restored as a pair
// (they feed one VPD — restoring one alone would compute a nonsense number);
// CO2 is tracked separately because a client may reasonably show it stale
// while the VPD pair is fresh, or vice versa. Rationale: sensor_sticky.h.
static StickyState s_envSticky = {};
static float       s_envLastTempC = 0.0f;
static float       s_envLastHumidity = 0.0f;
static StickyState s_co2Sticky = {};
static int         s_co2LastPpm = 0;

// PE light pipeline state (PPFD/DLI). DLI accumulates across a rolling 24h
// window anchored at sensors_init(); s_ppfdCalibFactor scales raw counts to
// umol/m2/s (set via /api/calib/ppfd, defaults to relative 1.0).
static DliState s_dliState = {};
static float    s_ppfdCalibFactor = CALIB_PPFD_FACTOR_DEFAULT;
// Sensor position -> canopy position (set via /api/calib/ppfd/geometry).
static float    s_ppfdGeometryFactor = CALIB_PPFD_GEOMETRY_DEFAULT;

// ── DLI:n nopea palautuskerros (RTC-hidasmuisti) ──────────────────
// Kaksi kerrosta, eri vikatilanteille:
//   flash (growclock.json, 30 min valein) — kestaa VIRTAKATKON, mutta havittaa
//     enintaan yhden tallennusvalin verran kertymaa.
//   RTC-hidasmuisti (tama) — sailyy SW-resetissa, watchdog-reboottissa ja
//     panicissa mutta EI virtakatkossa. Kirjoitus on pelkka RAM-tallennus:
//     ei flash-kulumaa, joten se paivitetaan jokaisella naytteella.
// Kaatumisesta toipuminen on siis tarkka, virtakatkosta karkea. Bootissa
// valitaan se lahde jonka ikkuna on pidemmalla (= tuoreempi).
// Sama RTC_NOINIT_ATTR-kuvio kuin watchdog.h:n kaatumislaskurissa.
#ifndef PLANTMEISTER_NATIVE_TEST
  #define DLI_RTC_MAGIC 0x444C4931UL   // "DLI1"
  RTC_NOINIT_ATTR static uint32_t s_dliRtcMagic;
  RTC_NOINIT_ATTR static float    s_dliRtcMol;
  RTC_NOINIT_ATTR static uint32_t s_dliRtcElapsedMs;
#endif

static inline void dliRtc_store(float mol, uint32_t elapsedMs) {
#ifndef PLANTMEISTER_NATIVE_TEST
  s_dliRtcMagic     = DLI_RTC_MAGIC;
  s_dliRtcMol       = mol;
  s_dliRtcElapsedMs = elapsedMs;
#else
  (void)mol; (void)elapsedMs;
#endif
}

// Palauttaa true jos RTC-muistissa oli tunnistettava kertyma.
static inline bool dliRtc_load(float* mol, uint32_t* elapsedMs) {
#ifndef PLANTMEISTER_NATIVE_TEST
  if (s_dliRtcMagic != DLI_RTC_MAGIC) return false;
  *mol       = s_dliRtcMol;
  *elapsedMs = s_dliRtcElapsedMs;
  return true;
#else
  (void)mol; (void)elapsedMs;
  return false;
#endif
}

// ── DLI-tilan rajapinta persistointikerrokselle ──────────────────
static inline float    sensors_dliAccumulatedMol() { return s_dliState.accumulatedMol; }
static inline uint32_t sensors_dliElapsedMs(uint32_t nowMs) {
  return dliTracker_elapsedMs(&s_dliState, nowMs);
}

// Palauttaa DLI-ikkunan bootissa. Kutsutaan growclock_restore():n jalkeen,
// koska flash-arvo tulee sielta. Valitsee pidemmalle ehtineen lahteen.
static inline void sensors_dliRestore(uint32_t nowMs, float flashMol,
                                      uint32_t flashElapsedMs) {
  float    mol       = flashMol;
  uint32_t elapsedMs = flashElapsedMs;

  float    rtcMol       = 0.0f;
  uint32_t rtcElapsedMs = 0;
  if (dliRtc_load(&rtcMol, &rtcElapsedMs) && rtcElapsedMs > elapsedMs) {
    mol       = rtcMol;
    elapsedMs = rtcElapsedMs;
    DEBUG_INFO(F("DLI: restored from RTC memory (crash recovery)"));
  }

  if (elapsedMs == 0 && mol == 0.0f) return;   // ei mitaan palautettavaa
  dliTracker_restore(&s_dliState, nowMs, mol, elapsedMs);
  DEBUG_PRINTF("[INFO]  DLI: restored %.3f mol, window +%lu min\n",
               (double)mol, (unsigned long)(elapsedMs / 60000UL));
}

// ── I2C bus initialization ──────────────────────────────────────
// isI2CDevicePresent() is defined in sensor_driver.h and used by
// the individual driver wrappers before calling library .begin().

static bool i2cInitialized = false;

static void ensureI2CInitialized() {
  if (i2cInitialized) return;
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);                    // 100 kHz — safe for all sensors
  Wire.setTimeOut(I2C_BUS_TIMEOUT_MS);      // prevent clock-stretch jam → WDT reboot
  i2cInitialized = true;
}

// ── Initialization ──────────────────────────────────────────────

bool sensors_init() {
  ensureI2CInitialized();

  DEBUG_INFO(F("Sensors: initializing..."));

  bool anyReady = false;

  for (size_t i = 0; i < SENSOR_DRIVER_COUNT; i++) {
    const SensorDriver* drv = SENSOR_DRIVERS[i];
    bool ok = drv->init();
    DEBUG_PRINTF("[INFO]  Sensor %s: %s\n", drv->name, ok ? "OK" : "FAIL");
    anyReady = anyReady || ok;
    if (!ok) {
      char adv[48];
      snprintf(adv, sizeof(adv), "Anturi ei vastaa: %s", drv->name);
      device_setAdvisory(DEVICE_FAULT_SENSOR, adv);
    }
  }

  // Anchor the DLI accumulation window at boot. First PPFD sample only sets
  // the clock; the integral starts from the second sample (dli_tracker.h).
  dliTracker_init(&s_dliState, millis());

  // Configure float/overflow switches. FLOAT_MIN is native (D6, INPUT_PULLUP)
  // in V1, but relocates to MCP23017 GPA6 on PCB v2 (fan took D6). When
  // relocated (PIN_FLOAT_SWITCH < 0) there is no native pin to configure — the
  // expander pull-up is set in mcp23017_init(); the read path handles the source.
#if ENABLE_FLOAT_SWITCH
  #if PIN_FLOAT_SWITCH >= 0
    pinMode(PIN_FLOAT_SWITCH, INPUT_PULLUP);
    DEBUG_INFO(F("  Float switch (min): native D6"));
  #else
    DEBUG_INFO(F("  Float switch (min): via MCP23017 GPA6"));
  #endif
  anyReady = true;   // Digital float switch is always available once configured
#endif

#if ENABLE_OVERFLOW_SWITCH && (PIN_FLOAT_SWITCH_OVERFLOW >= 0)
  pinMode(PIN_FLOAT_SWITCH_OVERFLOW, INPUT_PULLUP);
  DEBUG_INFO(F("  Overflow switch: configured"));
  anyReady = true;   // Digital overflow switch is always available once configured
#endif

  if (!anyReady) {
    DEBUG_ERROR(F("Sensors: none initialized!"));
  }

  return anyReady;
}

// ── Float / overflow switches (safety inputs) ───────────────────
// Digital GPIO inputs that are always available once pinMode is set in
// sensors_init(). They are read independently of I2C driver readiness so the
// overflow switch (flood safety) keeps working even when every "nice to have"
// I2C sensor is absent or has failed. Called both from sensors_read() and from
// the main loop on every iteration (see loop() in plantmeister.ino).

// Debounce: commit a new raw reading only after it has been stable for
// FLOAT_DEBOUNCE_MS. A single noisy sample (mechanical bounce, electrical spike)
// never reaches the ebb&flow FSM, so it cannot trigger a spurious pump stop,
// dry-run, overflow reaction or sensor-contradiction latch.
struct FloatDebounce {
  bool committed;
  bool candidate;
  uint32_t candidateSinceMs;
  bool initialized;
};

inline bool float_debounce(FloatDebounce* d, bool raw, uint32_t now) {
  if (!d->initialized) {                 // first read reflects reality immediately
    d->committed = raw;
    d->candidate = raw;
    d->candidateSinceMs = now;
    d->initialized = true;
    return d->committed;
  }
  if (raw != d->candidate) {             // new value seen -> restart stability timer
    d->candidate = raw;
    d->candidateSinceMs = now;
  }
  if (raw != d->committed && (uint32_t)(now - d->candidateSinceMs) >= FLOAT_DEBOUNCE_MS) {
    d->committed = raw;                  // stable long enough -> accept
  }
  return d->committed;
}

inline void sensors_readFloatSwitches(SensorData* data) {
#if ENABLE_FLOAT_SWITCH
  // FLOAT_MIN (reservoir): hardware verified 2026-06-08 reads LOW when the tank
  // is EMPTY (opposite rest polarity to the overflow float below). So water is
  // present when the pin reads HIGH. Inverting here keeps "OK" = water present.
  static FloatDebounce s_minDebounce = {true, true, 0, false};
  bool rawMin;
  #if ENABLE_MCP23017 && (PIN_FLOAT_SWITCH < 0)
    // FLOAT_MIN relocated to MCP23017 GPA6 (PCB v2: fan took native D6). Same
    // polarity as the native switch: HIGH = water present. If the expander is
    // absent, assume water present so a missing expander never false-triggers a
    // dry-run stop (§ 8: missing peripheral degrades safe, does not fault).
    rawMin = mcp23017_isReady()
               ? (mcp23017_cachedPin(MCP_PIN_FLOAT_MIN) == HIGH)
               : true;
  #else
    rawMin = (digitalRead(PIN_FLOAT_SWITCH) == HIGH);
  #endif
  data->waterLevelOk = float_debounce(&s_minDebounce, rawMin, millis());
#else
  data->waterLevelOk = true;   // Assume OK if no sensor
#endif

#if ENABLE_OVERFLOW_SWITCH && (PIN_FLOAT_SWITCH_OVERFLOW >= 0)
  static FloatDebounce s_ovfDebounce = {false, false, 0, false};
  bool rawOvf = (digitalRead(PIN_FLOAT_SWITCH_OVERFLOW) == LOW);  // Active LOW with pullup
  data->waterOverflowActive = float_debounce(&s_ovfDebounce, rawOvf, millis());
#else
  data->waterOverflowActive = false;
#endif
}

// ── Read all sensors ────────────────────────────────────────────

void sensors_read(SensorData* data) {
  data->timestamp = millis();

  // Reset validity — include leaf and spectrum so they don't carry stale
  // true state if their driver goes absent between readings.
  data->envValid = false;
  data->vpdValid = false;
  data->airCO2Valid = false;
  data->leafTempValid = false;
  data->spectrumValid = false;
  data->ppfdValid = false;
  data->heightValid = false;
  data->waterTempValid = false;
  data->tdsValid = false;

  uint32_t now = data->timestamp;
  for (size_t i = 0; i < SENSOR_DRIVER_COUNT; i++) {
    const SensorDriver* drv = SENSOR_DRIVERS[i];
    if (sensorOrch_tick(drv, &g_sensorOrchState[i], now)) {
      uint32_t t0 = millis();
      drv->read(data);
      uint32_t elapsed = (uint32_t)(millis() - t0);
      if (sensorOrch_trackBudget(drv, &g_sensorOrchState[i], elapsed, SENSOR_READ_BUDGET_MS)) {
        char adv[64];
        snprintf(adv, sizeof(adv), "Anturi hidas, pudotettu: %s", drv->name);
        device_setAdvisory(DEVICE_FAULT_SENSOR, adv);
      }
    }
  }

#if ENABLE_SENSOR_SIMULATION
  // Hardware-free demo: fill plausible raw inputs for fields no real driver
  // provided, BEFORE the VPD/PPFD/DLI post-process so the derived metrics
  // compute from synthetic inputs through the normal pipeline.
  sensorSim_fill(data, now);
#endif

  // Post-process: hold the last good environment sample across a skipped
  // read. Runs BEFORE the VPD derivation below so a restored T/RH pair still
  // produces a VPD — that is the whole point, VPD was the field the user saw
  // vanish. A driver that is genuinely gone stops refreshing lastGoodMs, so
  // the grace window expires on its own and the value goes invalid for real.
  if (data->envValid) {
    s_envLastTempC = data->airTempC;
    s_envLastHumidity = data->airHumidity;
    sticky_markGood(&s_envSticky, now);
  } else if (sticky_shouldReuse(&s_envSticky, now, SENSOR_ENV_STICKY_MS)) {
    data->airTempC = s_envLastTempC;
    data->airHumidity = s_envLastHumidity;
    data->envValid = true;
  }
  data->envAgeMs = sticky_ageMs(&s_envSticky, now);

  if (data->airCO2Valid) {
    s_co2LastPpm = data->airCO2Ppm;
    sticky_markGood(&s_co2Sticky, now);
  } else if (sticky_shouldReuse(&s_co2Sticky, now, SENSOR_ENV_STICKY_MS)) {
    data->airCO2Ppm = s_co2LastPpm;
    data->airCO2Valid = true;
  }

  // Post-process: derive VPD. Use leaf temperature when MLX90614 is present
  // and plausible (plant-empowerment formula); fall back to T_air (V1 behaviour)
  // if the sensor is absent, invalid, or aimed away from the canopy.
  if (data->envValid) {
    bool aimLost = false;
    data->vpdKpa = vpd_selectAndCalculate(
        data->airTempC, data->airHumidity,
        data->leafTempC, data->leafTempValid,
        data->waterTempC, data->waterTempValid,
        &aimLost);
    data->vpdValid = true;
    if (aimLost) {
      device_setAdvisory(DEVICE_FAULT_SENSOR, "MLX90614 aim lost - VPD laskettu T_air");
    }
  }

  // Post-process: PE light pipeline (PE Assimilate balance). When the AS7341
  // spectrum is valid, estimate instantaneous PPFD (light_calc.h) and feed it
  // to the DLI accumulator (dli_tracker.h). dliToday mirrors the running 24h
  // integral so /api/state can expose "light delivered so far today".
  // ppfdSensor is what the AS7341 physically sees; ppfdNow scales that to the
  // canopy (s_ppfdGeometryFactor). DLI integrates the CANOPY value because the
  // PE targets (docs/kehitys/pe-ohjausmalli.md §4) describe what reaches the
  // plant, not what reaches the sensor. Geometry defaults to 1.0, so a device
  // that never runs the geometry wizard behaves exactly as before.
  if (data->spectrumValid) {
    data->ppfdSensor = lightCalc_estimatePPFD(&data->lightSpectrum, s_ppfdCalibFactor);
    data->ppfdNow = data->ppfdSensor * s_ppfdGeometryFactor;
    data->ppfdValid = true;
    dliTracker_addSample(&s_dliState, data->ppfdNow, now);
  } else {
    data->ppfdSensor = 0.0f;
    data->ppfdNow = 0.0f;
  }
  // Reset the accumulator once the 24h window rolls over (return value is the
  // completed day's DLI; not retained here, the running total is what we show).
  dliTracker_checkRollover(&s_dliState, now, DLI_DAY_LENGTH_MS);
  data->dliToday = s_dliState.accumulatedMol;

  // Peilaa kertyma RTC-hidasmuistiin joka naytteella. Pelkka RAM-kirjoitus:
  // ei flash-kulumaa, joten tahti voi olla tiheä. Tama on se kerros joka tekee
  // watchdog-reboottista ja panicista havioittoman DLI:n kannalta.
  dliRtc_store(s_dliState.accumulatedMol, dliTracker_elapsedMs(&s_dliState, now));

  sensors_readFloatSwitches(data);

#if ENABLE_BATTERY_MONITOR
  if (ina219_isReady()) {
    // `ina219_read()` driver already filled `data->batteryVoltage` when ready.
    // Nothing to do here.
  } else {
#if ENABLE_SENSOR_SIMULATION
    // Battery already synthesized by sensorSim_fill — skip the ADC fallback so
    // a floating PIN_BATTERY_ADC does not overwrite the demo value.
#else
    // Fallback: read from ADC pin directly
    uint32_t adcSum = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
      adcSum += analogRead(PIN_BATTERY_ADC);
    }
    float adcAvg = (float)adcSum / BATTERY_ADC_SAMPLES;
    // ESP32 ADC: 12-bit (0-4095), 0-3.3V reference
    data->batteryVoltage = (adcAvg / 4095.0f) * 3.3f * BATTERY_VOLTAGE_DIVIDER;
#endif
  }

  // Calculate battery percentage (linear approximation)
  int mv = (int)(data->batteryVoltage * 1000.0f);
  if (mv >= BATTERY_FULL_MV) {
    data->batteryPercent = 100;
  } else if (mv <= BATTERY_EMPTY_MV) {
    data->batteryPercent = 0;
  } else {
    data->batteryPercent = (mv - BATTERY_EMPTY_MV) * 100 /
                           (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
  }
#else
  data->batteryVoltage = 5.0f;   // USB powered
  data->batteryPercent = 100;
#endif
}

// ── Individual sensor getters (for diagnostics) ─────────────────

bool sensors_isHeightReady() {
#if ENABLE_HEIGHT_SENSOR
  return vl53l0x_isReady();
#else
  return false;
#endif
}

bool sensors_isEnvReady() {
#if HW_BME280
  return bme280_isReady();
#else
  return false;
#endif
}

bool sensors_isTdsReady() {
#if ENABLE_TDS_SENSOR
  return ads1115_isReady();
#else
  return false;
#endif
}

void sensors_setCalibration(const CalibrationData* calibration) {
  if (calibration && calibration->ppfdCalibrationFactor > 0.0f) {
    s_ppfdCalibFactor = calibration->ppfdCalibrationFactor;
  }
  if (calibration && calibration->ppfdGeometryFactor > 0.0f) {
    s_ppfdGeometryFactor = calibration->ppfdGeometryFactor;
  }
#if ENABLE_TDS_SENSOR
  ads1115_setCalibration(calibration);
#else
  (void)calibration;
#endif
}

#ifdef PLANTMEISTER_NATIVE_TEST
// Test-only: reset orchestration bookkeeping so each test starts fresh.
static void sensors_resetProbeStateForTest() {
  for (size_t i = 0; i < SENSOR_DRIVER_COUNT; i++) {
    sensorOrch_resetState(&g_sensorOrchState[i]);
  }
  // Sticky-valimuisti kuuluu samaan nollaukseen: ilman tata edellisen testin
  // hyva naute vuotaisi seuraavaan ja "anturi puuttuu" -testi nayttaisi arvon.
  sticky_reset(&s_envSticky);
  sticky_reset(&s_co2Sticky);
}
#endif

#endif // ENABLE_SENSORS
#endif // SENSOR_MANAGER_H
