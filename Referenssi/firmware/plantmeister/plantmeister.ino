/*=====================================================================
  PlantMeister - Autonomous Herb Growing Station

  ESP32 WROOM-32E DevKit V1 + RYLR890 LoRa

  Features:
  - Automated grow light control via relay/MOSFET (12V DC)
  - Height-adjustable lamp (stepper/servo)
  - Peristaltic pump for nutrient dosing
  - Sensor suite: VL53L0X, BME280, TDS, DS18B20
  - LoRa reporting to RPi (Home Assistant)
  - WiFi AP configuration portal (15min after boot)
  - Plant species database with per-species parameters

  Libraries required:
  - AccelStepper
  - ArduinoJson (v6)
  - Adafruit VL53L0X
  - Adafruit BME280 (+ Adafruit Unified Sensor + Adafruit BusIO)
  - Adafruit ADS1X15
  - Adafruit INA219
  - OneWire + DallasTemperature
  - ESPAsyncWebServer + AsyncTCP
  - ESP32Servo (if using servo motor)
=====================================================================*/

// ESP32 loopTask default stack (8 KB) overflows with WiFi+AsyncWebServer+sensors.
// Increase to 16 KB. Must be before any #include.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// Include order per project rules: config → structs → project → stdlib
#include "config.h"
#include "structs.h"
#include "device_state.h"
#include "loop_dispatch.h"

#if ENABLE_SELF_TEST
  #include "self_test.h"
#endif

#if ENABLE_UX_INDICATOR
  #include "ux_indicator.h"
#endif

#if ENABLE_BUTTON_INPUT
  #include "button_input.h"
#endif

#include <LittleFS.h>

// HAL and modules
#include "power_manager.h"
#include "sensor_manager.h"
#include "motor_hal.h"
#include "mpg_hal.h"
#include "pump_hal.h"
#if ENABLE_MCP23017
  #include "mcp23017_hal.h"
#endif
#if ENABLE_FAN
  #include "fan_hal.h"
#endif
#include "lora_comm.h"
#include "plant_database.h"
#include "config_store.h"
#include "grow_clock.h"
#include "calibration_runtime.h"
#include "sensor_history.h"
#include "scheduler.h"
#include "wifi_portal.h"
#include "command_handler.h"
#include "input_router.h"
#if ENABLE_BUTTON_INPUT
  #include "button_intent_map.h"
#endif
#include "eink_display.h"
#include "watchdog.h"
#include "wireless_log.h"
#include "reboot_request.h"
// Riippuu watchdog.h + button_input.h + mcp23017_hal.h:sta -> niiden jalkeen.
#include "factory_reset_gesture.h"

// ═══════════════════════════════════════════════════════════════════
// GLOBAL STATE
// ═══════════════════════════════════════════════════════════════════

static SensorData   g_sensors;
static SystemState  g_state;
static DeviceConfig g_config;
static CalibrationData g_calibration;
static PlantConfig* g_activePlant = NULL;
static unsigned long g_lastEinkUpdateMs = 0;

#if ENABLE_INPUT_ROUTER
static RouterContext g_routerCtx;
#endif

// ═══════════════════════════════════════════════════════════════════
// COMMAND HANDLER — from WiFi portal or LoRa
// ═══════════════════════════════════════════════════════════════════

void handleCommand(const char* cmd, const char* value) {
#if ENABLE_INPUT_ROUTER
  command_handle(cmd, value, &g_sensors, &g_state, &g_config, &g_activePlant, &g_routerCtx);
#else
  command_handle(cmd, value, &g_sensors, &g_state, &g_config, &g_activePlant, nullptr);
#endif
}

// ═══════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════

void setup() {
  // TURVA ENNEN KAIKKEA MUUTA: moottorin EN disabled-tilaan.
  //
  // Tama on setup():n ensimmainen rivi tarkoituksella. EN on active-LOW, ja
  // ennen tata pinni on korkeaimpedanssinen tulo -> ajuri lukee LOW = paalla,
  // ja closed-loop-moottori alkaa hakea asemaa. Aiemmin EN nousi vasta
  // motor_init():ssa, joka on sensors_init():n JALKEEN — moottori liikkui
  // useita sekunteja joka bootissa (rautahavainto 17.7.2026).
  //
  // Mikaan alla oleva ei saa siirtya taman edelle. Jos lisaat rivin tahan
  // valiin, kysy itseltasi: liikkuuko moottori sen ajan?
  // ENABLE_MOTOR-suoja (V3-sensoripaketilla ei moottoria — motor_hal.h ei
  // maarittele taman funktiota lainkaan kun lippu on false).
#if ENABLE_MOTOR
  motor_safeDisable();
#endif

  // Debug serial
  DEBUG_SERIAL.begin(DEBUG_BAUD);
  watchdog_logResetReason();
#if ENABLE_DEVICE_STATE
  device_init();
#endif
  delay(500);

  // Initialize platform HAL before using PLATFORM_NAME
  platform_init();

  // Arm the hardware watchdog EARLY — before any peripheral I/O that could
  // block (LittleFS, I2C sensor init/read). With it armed here, a blocking
  // boot-time call becomes a recoverable WDT reboot (visible in boot-cause)
  // instead of a permanent INIT hang masked by the portal task still answering
  // HTTP. WATCHDOG_TIMEOUT_S (60 s) comfortably covers legitimate init.
  // See architecture.md § 8 (graceful degradation) Aukko A.
  watchdog_init();

  // Crash-reboot accounting (must follow watchdog_init): classify the reset and
  // update the RTC crash counter. If crashes stacked up past the threshold, boot
  // a minimal SAFE-MODE image (WiFi + OTA + status only) so a fix can be flashed.
  // Safe mode is a one-boot state — a healthy runtime clears the counter in
  // loop(), so the next reboot retries a normal boot (OTA robustness plan PR-3).
  watchdog_bootAccounting();
  bool safeMode = false;
#if ENABLE_SAFE_MODE
  safeMode = (watchdog_crashCount() >= SAFE_MODE_CRASH_THRESHOLD);
  if (safeMode) {
    DEBUG_ERROR(F("SAFE MODE: boot-loop detected — only WiFi/OTA/status active"));
  #if ENABLE_DEVICE_STATE
    device_setSafeMode(true);
  #endif
  }
#endif

#if USE_XIAO_SX1262
  if (platform_getType() != PLATFORM_XIAO_ESP32S3) {
    DEBUG_WARN(F("Config/board mismatch: USE_XIAO_SX1262=true but board is not ESP32S3"));
  }
  #if USE_XIAO_PLUS_PINS
    DEBUG_INFO(F("XIAO profile: PLUS pin layout active (separate light/overflow/battery pins)"));
  #else
    DEBUG_WARN(F("XIAO profile: BASE pin layout active (limited GPIO, pin sharing expected)"));
  #endif
  #if PUMP_LIGHT_SHARED_PIN
    DEBUG_WARN(F("XIAO limitation: PIN_PUMP and PIN_RELAY_LIGHT share GPIO; light relay control is disabled"));
  #endif
  #if ENABLE_EBB_FLOW && ENABLE_OVERFLOW_SWITCH && (PIN_FLOAT_SWITCH_OVERFLOW < 0)
    DEBUG_WARN(F("XIAO limitation: overflow switch pin missing while ENABLE_OVERFLOW_SWITCH=true"));
  #endif
#else
  if (platform_getType() == PLATFORM_XIAO_ESP32S3) {
    DEBUG_WARN(F("Config/board mismatch: XIAO detected but USE_XIAO_SX1262=false"));
  }
#endif

  DEBUG_SERIAL.println();
  DEBUG_SERIAL.println(F("╔═══════════════════════════════════════════════════╗"));
  DEBUG_SERIAL.println(F("║         PlantMeister v1.0                        ║"));
  DEBUG_SERIAL.println(F("║         Autonomous Herb Growing Station          ║"));
  DEBUG_SERIAL.println(F("╚═══════════════════════════════════════════════════╝"));
  DEBUG_PRINTF("Platform: %s\n", PLATFORM_NAME);

  // Initialize state
  memset(&g_sensors, 0, sizeof(g_sensors));
  memset(&g_state, 0, sizeof(g_state));
  g_state.bootTime = millis();

  // ── Power & GPIO ──
  power_init();

  // ── Filesystem ──
  if (LittleFS.begin(true)) {   // true = format on fail
    g_state.filesystemReady = true;
    DEBUG_INFO(F("LittleFS: mounted"));
  } else {
    DEBUG_ERROR(F("LittleFS: mount failed!"));
  }

  // ── Tehdasreset-ele: nappi pohjassa kaynnistyksessa ──
  //
  // Tasmalleen tassa kohtaa, ja se on koko eleen kevyyden syy: tiedostot
  // poistetaan ENNEN config_load():ia, joten lataus putoaa oletuksiin aivan
  // itsestaan ja boot jatkaa suoraan kayttoonottotilaan. Reboottia ei tarvita
  // — mika samalla valttaa OTA-rollback-ansan johon komentoreitti joutuu
  // (reboot < 60 s vahvistamattomasta imagesta rullaa sen takaisin).
  //
  // Ajetaan myos safe modessa: firmwaren epailyttavyys on syy sallia nollaus,
  // ei syy estaa sita.
  if (factory_gesture_check()) {
    DEBUG_INFO(F("Factory gesture: jatketaan bootia tuoreen laitteen tavoin"));
  }

  // ── Load config ──
  config_load(&g_config);
  config_clearBootOnlyOverrides(&g_config);
  calibration_load(&g_calibration);

  // ── Plant database ──
  plants_init();

  // Load active plant
  g_activePlant = plants_getById(g_config.currentPlantId);
  if (!g_activePlant) {
    g_activePlant = plants_getByIndex(0);
    if (g_activePlant) {
      strncpy(g_config.currentPlantId, g_activePlant->id,
              sizeof(g_config.currentPlantId) - 1);
      g_config.currentPlantId[sizeof(g_config.currentPlantId) - 1] = '\0';
    }
  }

  if (g_activePlant) {
    DEBUG_PRINTF("[INFO]  Active plant: %s (%s)\n",
                 g_activePlant->name, g_activePlant->id);
  } else {
    DEBUG_ERROR(F("No plant configured!"));
  }

  // Safe mode skips all peripheral/actuator inits below: their g_state.xxxReady
  // flags stay false (g_state was memset to 0 above) so loop-side §8 guards skip
  // them. Only WiFi portal + OTA + wireless log + UX + button stay active.

  // ── Sensors ──
#if ENABLE_SENSORS
  if (!safeMode) {
    g_state.sensorsReady = sensors_init();
    sensors_setCalibration(&g_calibration);
#if HW_INA228
    // Apply persisted INA228 runtime config + calibration (no reflash needed)
    ina228_applyRuntimeConfig(g_calibration.powerShuntOhms, g_calibration.powerMaxCurrentA,
                              g_calibration.powerAdcRange, g_calibration.powerCalFactor);
#endif
  }
#endif

  // ── Motor ──
#if ENABLE_MOTOR
  if (!safeMode) {
    g_state.motorReady = motor_init();
    motor_setStepLimits(g_calibration.motorStepsDown, g_calibration.motorStepsUp);
    // Restore saved position
    motor_setPositionMm(g_config.motorCurrentMm);
    if (g_config.motorTargetMm != g_config.motorCurrentMm) {
      motor_moveTo(g_config.motorTargetMm);
    }
  }
#endif

#if ENABLE_MPG
  if (!safeMode) mpg_init();
#endif

  // ── Pump ──
#if ENABLE_PUMP
  if (!safeMode) {
    g_state.pumpReady = pump_init();
    pump_setMlPerSec(g_calibration.pumpMlPerSec);
  }
#endif

  // ── MCP23017 I2C GPIO expander (PCB v2) ── (I2C bus already up via sensors_init)
#if ENABLE_MCP23017
  if (!safeMode) {
    g_state.mcp23017Ready = mcp23017_init();
  }
#endif

  // ── Fan (PCB v2): 25 kHz PWM speed + coarse tacho health ──
#if ENABLE_FAN
  if (!safeMode) {
    g_state.fanReady = fan_init();
  }
#endif

  // ── LoRa ──
#if ENABLE_LORA
  if (!safeMode) {
    g_state.loraReady = lora_init(g_config.loraAddress, g_config.loraNetworkId);
  }
#endif

  // ── WiFi portal ──
#if ENABLE_WIFI_PORTAL
  portal_init(&g_sensors, &g_state, &g_config, &g_calibration, g_activePlant, handleCommand);
  g_state.wifiApActive = true;
#endif

  // ── Wireless log (UDP broadcast mirror) ──
  // Alustetaan WiFi-portalin jälkeen. Logit puskuroidaan kunnes WL_CONNECTED,
  // sitten flushataan. Ei vaikuta logiikkaan jos WiFi ei nouse.
#if ENABLE_WIRELESS_LOG
  wlog_init();
  DEBUG_INFOF("Wireless log: UDP broadcast :%d\n", WIRELESS_LOG_PORT);
#endif

  // ── Sensor history ──
  history_init();

  // ── I2C scan (diagnostics) ── (skip in safe mode: minimise bus I/O)
  // Time-budgeted (architecture.md § 8 Aukko A): a miswired/damaged device that
  // holds SDA/SCL low makes each probe wait ~1 s for the bus to go idle, so an
  // unbounded 126-address scan blows WATCHDOG_TIMEOUT_S and the boot reboot-loops.
  // Abort after I2C_SCAN_BUDGET_MS so a stuck bus degrades to an advisory (the
  // affected driver's presence-check + readyFlag handle the real detection),
  // never a boot loop. A healthy scan finishes far inside the budget.
  if (!safeMode) {
    DEBUG_INFO(F("I2C scan:"));
    uint32_t scanStartMs = millis();
    for (uint8_t addr = 1; addr < 127; addr++) {
      if ((uint32_t)(millis() - scanStartMs) > I2C_SCAN_BUDGET_MS) {
        DEBUG_WARN(F("  I2C scan aborted: bus slow/stuck (device holding SDA/SCL?) — continuing boot"));
        break;
      }
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        DEBUG_PRINTF("  found 0x%02X\n", addr);
      }
    }
  }

  // ── E-Ink display pipeline (software baseline) ──
#if ENABLE_EINK_DISPLAY
  if (!safeMode) {
    g_state.einkReady = eink_init();
    if (g_state.einkReady) {
      DEBUG_INFO(F("E-Ink: software pipeline enabled"));
    } else {
      DEBUG_WARN(F("E-Ink: init failed"));
    }
  }
#endif

#if ENABLE_UX_INDICATOR
  ux_init();
#endif

#if ENABLE_INPUT_ROUTER
  g_routerCtx.sensors = &g_sensors;
  g_routerCtx.state = &g_state;
  g_routerCtx.config = &g_config;
  g_routerCtx.activePlant = &g_activePlant;
#endif

#if ENABLE_BUTTON_INPUT
  button_init();
  button_setCallback([](ButtonEvent ev) {
#if ENABLE_INPUT_ROUTER
    // Layer 2: gesture + current state + config + plant -> Intent (pure,
    // tested). Plant is needed for the grow-step context: during an active
    // step sequence a short press is ALWAYS ack-step (never buttonAction).
    // Layer 3 (input_routeIntent) still owns all state validation.
    Intent intent = button_resolveIntent(ev, g_device.state, &g_config, g_activePlant);
    if (intent != INTENT_NONE) {
      DEBUG_PRINTF("[INFO]  Button: gesture %u -> intent %u\n",
                   (unsigned)ev, (unsigned)intent);
      // The value is intent-specific: only INTENT_START_GROWING reads it, as
      // the start method (0 cutting / 1 seed / 2 seedling); every other intent
      // ignores it. Pass the method the user actually chose at setup instead of
      // a hardcoded 0 — otherwise the button silently overwrites their answer
      // with "cutting", and the phase guidance then tells someone who sowed
      // seeds to go take a cutting. config_setDefaults clamps this to 0..2, so
      // it can never be the out-of-range value the router would reject.
      input_routeIntent(intent, g_config.growStartMethod, INTENT_SOURCE_BUTTON,
                        &g_routerCtx);
    } else {
      DEBUG_PRINTF("[INFO]  Button: gesture %u (no action)\n", (unsigned)ev);
    }
#else
    DEBUG_PRINTF("[INFO]  Button: gesture %u detected\n", (unsigned)ev);
#endif
  });
#endif

  // ── Motor GPIO diagnostic: cycle all 4 pins slowly so you can measure with multimeter ──
#if MOTOR_DIAG_ON_BOOT && MOTOR_DRIVER == MOTOR_DRIVER_L298N
  DEBUG_INFO(F("Motor diag: cycling IN1-IN4 (500ms each)..."));
  const uint8_t diag_pins[] = {PIN_MOTOR_IN1, PIN_MOTOR_IN2, PIN_MOTOR_IN3, PIN_MOTOR_IN4};
  for (uint8_t i = 0; i < 4; i++) {
    DEBUG_PRINTF("  IN%d (GPIO%d) HIGH\n", i+1, diag_pins[i]);
    digitalWrite(diag_pins[i], HIGH);
    delay(500);
    digitalWrite(diag_pins[i], LOW);
  }
  DEBUG_INFO(F("Motor diag: done"));
#endif

  // ── Air pump — starts on by default (24/7 aeration; skipped in safe mode) ──
#if ENABLE_AIR_PUMP
  if (!safeMode) g_state.airPumpOn = true;
#endif

  // ── Light cycle start ──
  g_state.lightCycleStartMs = millis();

  // ── Ebb&Flow state init ──
#if ENABLE_EBB_FLOW
  g_state.ebbFlowState = EBB_STATE_IDLE;
  g_state.ebbFlowFaultCode = EBB_FAULT_NONE;
  g_state.ebbFlowFaultLatched = false;
  g_state.ebbFlowAckRequested = false;
  g_state.ebbFlowStateSinceMs = millis();
  g_state.lastEbbFlowCycleStartMs = millis();
  g_state.lastCirculateStartMs = millis();
  g_state.circulateActive = false;
#endif

  // ── Grow phase runtime init ──
  g_state.growDayStartMs          = millis();
  g_state.growPhasePendingAdvance = false;
  g_state.growAdvanceProposedMs   = 0;
  g_state.growStepStartMs         = millis();  // growclock_restore back-dates if persisted
  if (g_config.growActive) {
    DEBUG_PRINTF("[INFO]  Grow: phase %d, day %d\n",
                 g_config.growPhase, g_config.growElapsedDays);
  }

  // Back-date the day/photoperiod anchors from the persisted grow clock so a
  // power cycle mid-grow continues the day counter and light cycle instead of
  // restarting them (must run AFTER the default anchor init above).
  GrowClockData restoredClock = {};
  growclock_restore(&g_state, &g_config, &restoredClock);

  // DLI-ikkuna samasta tiedostosta (tai RTC-muistista jos kaatuminen oli
  // tuoreempi). Ilman tata reboot klo 15 nollaa paivan valokertyman ja
  // ohjeistus kehottaa lisaamaan valoa jonka kasvi jo sai.
  sensors_dliRestore(millis(), restoredClock.dliMol, restoredClock.dliElapsedMs);

  // ── Initial sensor reading: intentionally deferred to loop() ──
  // No blocking sensor read in setup(). A sensor that is electrically present
  // but hangs its read (clock-stretch, half-connected, library wait without
  // timeout) must NOT block boot (architecture.md § 8 Aukko A). The loop()
  // scheduler performs the first read within a few hundred ms; xxxValid flags
  // stay false until then and consumers honour them. lastSensorRead stays 0 so
  // the scheduler reads as soon as loop() starts.

  DEBUG_SERIAL.println();
  DEBUG_SERIAL.println(F("═══════════════════════════════════════════════════"));
  DEBUG_PRINTF("Sensors: %s\n", g_state.sensorsReady ? "OK" : "FAIL");
  DEBUG_PRINTF("LoRa:    %s\n", g_state.loraReady ? "OK" : "FAIL");
  DEBUG_PRINTF("Motor:   %s\n", g_state.motorReady ? "OK" : "FAIL");
  DEBUG_PRINTF("Pump:    %s\n", g_state.pumpReady ? "OK" : "FAIL");
#if ENABLE_MCP23017
  DEBUG_PRINTF("MCP23017:%s\n", g_state.mcp23017Ready ? "OK" : "FAIL (advisory)");
#endif
#if ENABLE_FAN
  DEBUG_PRINTF("Fan:     %s\n", g_state.fanReady ? "OK" : "FAIL");
#endif
#if ENABLE_EINK_DISPLAY
  DEBUG_PRINTF("E-Ink:   %s\n", g_state.einkReady ? "OK" : "FAIL");
#endif
#if ENABLE_EBB_FLOW
  DEBUG_PRINTF("EbbFlow: %s\n", g_state.ebbFlowState == EBB_STATE_IDLE ? "READY" : "ACTIVE");
  #if !ENABLE_FLOAT_SWITCH
    DEBUG_WARN(F("EbbFlow: no float switch — water level assumed OK (no dry-run protection)"));
  #endif
  #if !ENABLE_OVERFLOW_SWITCH
    DEBUG_WARN(F("EbbFlow: no overflow switch — overflow detection disabled"));
  #endif
#endif
  DEBUG_PRINTF("Air:     %s\n", g_state.airPumpOn ? "ON (24/7)" : "OFF");
  DEBUG_PRINTF("WiFi AP: %s (SSID: %s)\n",
               g_state.wifiApActive ? "ON" : "OFF", WIFI_AP_SSID);
  if (safeMode) {
    DEBUG_PRINTF("Safe mode: ACTIVE (crash_reboots=%u)\n", (unsigned)watchdog_crashCount());
  }
  DEBUG_SERIAL.println(F("═══════════════════════════════════════════════════"));
  DEBUG_SERIAL.println();

  // OTA rollback confirmation happens in loop() via ota_confirmTick() — see ota_manager.h.

  // (Watchdog already armed early in setup, before peripheral I/O — see above.)

#if ENABLE_SELF_TEST && ENABLE_DEVICE_STATE
  if (safeMode) {
    // Safe mode: no peripherals were initialised, so there is nothing to
    // self-test and no fault bits — go straight to IDLE so WiFi/OTA/status
    // serve normally while the crash cause is investigated.
    device_setState(DEVICE_IDLE);
  } else {
    device_setState(DEVICE_SELF_TEST);
    SelfTestResult st = selftest_run();
    // device_setFault routes each bit: blocking faults lock into DEVICE_FAULT,
    // advisory bits (missing optional sensor, LoRa down) only record info.
    if (st.faults) device_setFault(st.faults, st.message);
    // Reach IDLE unless a blocking fault actually locked the device. A missing
    // advisory sensor must NOT block boot (architecture.md § 8).
    if (g_device.state != DEVICE_FAULT) device_setState(DEVICE_IDLE);
  }
#elif ENABLE_DEVICE_STATE
  device_setState(DEVICE_IDLE);
#endif

#if ENABLE_DEVICE_STATE
  // Resume a persisted grow program. A clean reboot (power cycle, OTA) restores
  // config->growActive but boot otherwise lands in IDLE — re-enter DEVICE_GROWING
  // so the persistent flag and the runtime state stay in sync and periodic
  // flooding keeps running. Uses the normal IDLE→GROWING transition; skipped in
  // safe mode and when a blocking FAULT holds the device (current != IDLE).
  if (device_shouldResumeGrowing(safeMode, g_config.growActive, g_device.state)) {
    device_setState(DEVICE_GROWING);
    DEBUG_INFO(F("Grow: resumed GROWING from persisted growActive"));
  }
#endif
}

// ═══════════════════════════════════════════════════════════════════
// MAIN LOOP — millis()-based, never blocks
// ═══════════════════════════════════════════════════════════════════

// ── MPG TEST — remove after verification (MPG_TEST_ENABLED in config.h) ────
#if MPG_TEST_ENABLED
static bool lastA = false, lastB = false;
void mpg_test_update() {
  bool a = digitalRead(PIN_MPG_A);
  bool b = digitalRead(PIN_MPG_B);
  if (a != lastA || b != lastB) {
    DEBUG_PRINTF("[MPG]   A=%d B=%d\n", a, b);
    lastA = a; lastB = b;
  }
}
#endif
// ────────────────────────────────────────────────────────────────────

static void loopBuildDispatchOps(LoopDispatchOps* ops);
static void loopTaskHeartbeat(const LoopDispatchContext* ctx);
static void loopTaskSensorsIdle(const LoopDispatchContext* ctx);
static void loopTaskSensorsGrowing(const LoopDispatchContext* ctx);
static void loopTaskLora(const LoopDispatchContext* ctx);
static void loopTaskWifiPortal(const LoopDispatchContext* ctx);
static void loopTaskHistory(const LoopDispatchContext* ctx);
static void loopTaskUiIdle(const LoopDispatchContext* ctx);
static void loopTaskUiAwaiting(const LoopDispatchContext* ctx);
static void loopTaskUiFault(const LoopDispatchContext* ctx);
static void loopTaskUiMaintenance(const LoopDispatchContext* ctx);
static void loopTaskActuators(const LoopDispatchContext* ctx);
static void loopTaskGrowScheduler(const LoopDispatchContext* ctx);
static void loopTaskEbbFlow(const LoopDispatchContext* ctx);
static void loopTaskSerialInput(const LoopDispatchContext* ctx);
static void loopTaskShutdown(const LoopDispatchContext* ctx);

void loop() {
#if ENABLE_DEVICE_STATE
  #if ENABLE_BUTTON_INPUT
    button_tick();
  #endif

  static LoopDispatchOps ops = []() {
    LoopDispatchOps o = {};
    loopBuildDispatchOps(&o);
    return o;
  }();
  // Maintenance lock: derived from DeviceState every iteration, BEFORE dispatch,
  // so no task can start an actuator while the operator has hands in the device.
  // Deriving it (rather than latching it on the transition) means the lock can
  // never outlive or lag behind the state it mirrors — g_device.state stays the
  // single source of truth (architecture.md § 2). Runs state-independently for
  // the same reason as the float poll below: safety must not depend on which
  // branch of the dispatch switch we happen to be in.
  #if ENABLE_PUMP
    pump_setMaintenanceLock(g_device.state == DEVICE_MAINTENANCE);
  #endif
  #if ENABLE_MOTOR
    motor_setMaintenanceLock(g_device.state == DEVICE_MAINTENANCE);
  #endif

  LoopDispatchContext ctx = { nullptr };
  // While a reboot is pending, skip state dispatch so DEVICE_SHUTDOWN cannot
  // take us into deep sleep before the deferred ESP.restart() runs.
  if (!reboot_request_pending()) {
    loop_tick_for_state(g_device.state, &ctx, &ops);
  }

  // Safety inputs: poll the digital float/overflow switches every loop in all
  // device states, independent of I2C sensor readiness or FAULT state. The
  // overflow switch is flood safety and must not depend on "nice to have" I2C
  // sensors being present.
  // MCP23017 (PCB v2): refresh the cached PORTA snapshot BEFORE the float read
  // below — it feeds both the relocated FLOAT_MIN level and the fan tacho edges.
  // State-independent + rate-limited (MCP23017_REFRESH_MS), like the float poll.
  #if ENABLE_MCP23017
    mcp23017_tick(millis());
  #endif

  #if ENABLE_SENSORS && (ENABLE_FLOAT_SWITCH || (ENABLE_OVERFLOW_SWITCH && PIN_FLOAT_SWITCH_OVERFLOW >= 0))
    sensors_readFloatSwitches(&g_sensors);
  #endif

  // Fan health (PCB v2): poll the coarse tacho and raise/clear the fan advisory.
  // State-independent so a fan commanded on in any state is monitored; the poll
  // is rate-limited to FAN_TACHO_POLL_MS and never blocks (advisory only).
  #if ENABLE_FAN
    fan_update();
  #endif

  // State-independent network recovery (mDNS, OTA reboot timer, STA reconnect).
  // Must run in EVERY iteration regardless of DeviceState — otherwise a FAULT
  // state (eg. sensors disconnected) would lock out OTA updates and WiFi
  // reconnection. Same escape-from-state-dispatch pattern as the safety inputs
  // above. portal_update() in loopTaskWifiPortal handles only the AP-timeout.
  #if ENABLE_WIFI_PORTAL
    portal_loopCore();
  #endif

  #if ENABLE_UX_INDICATOR
    ux_tick(&g_sensors, &g_state, &g_config, g_activePlant);
  #endif

  watchdog_feed();
  ota_confirmTick();  // cancel OTA rollback after BOOT_HEALTHY_RUNTIME_MS of healthy loop

  // One-shot: after BOOT_HEALTHY_RUNTIME_MS of healthy loop the boot is proven
  // good — clear the crash-reboot chain so safe mode stays a one-boot state (the
  // next reboot retries a normal boot, OTA robustness plan D3).
  static bool s_bootMarkedHealthy = false;
  if (!s_bootMarkedHealthy && millis() >= BOOT_HEALTHY_RUNTIME_MS) {
    watchdog_clearCrashCount();
    s_bootMarkedHealthy = true;
  }

  #if ENABLE_WIRELESS_LOG
    wlog_loop();  // Flush puskuroidut logit kun WiFi nousee
  #endif

  reboot_request_tick();
#endif
}
static void loopBuildDispatchOps(LoopDispatchOps* ops) {
  if (!ops) return;
  *ops = {};
  ops->heartbeat = loopTaskHeartbeat;
  ops->sensorsIdle = loopTaskSensorsIdle;
  ops->sensorsGrowing = loopTaskSensorsGrowing;
  ops->loraPoll = loopTaskLora;
  ops->wifiPortal = loopTaskWifiPortal;
  ops->growScheduler = loopTaskGrowScheduler;
  ops->ebbFlow = loopTaskEbbFlow;
  ops->actuators = loopTaskActuators;
  ops->uiIdle = loopTaskUiIdle;
  ops->uiAwaiting = loopTaskUiAwaiting;
  ops->uiFault = loopTaskUiFault;
  ops->uiMaintenance = loopTaskUiMaintenance;
  ops->history = loopTaskHistory;
  ops->serialInput = loopTaskSerialInput;
  ops->shutdown = loopTaskShutdown;
}

static void loopTaskHeartbeat(const LoopDispatchContext* ctx) {
  (void)ctx;
  power_heartbeat();
}

// Throttle the VERB sensor line so it does not flood the wireless log every
// cycle. Emit only when a rounded reading actually changed, or once every
// SENSOR_LOG_HEARTBEAT_MS as a liveness tick. With sensors disconnected the
// readings are constant (0.0C/0%/0mm) — this then prints once and goes quiet,
// keeping the log focused on flooding/state activity.
static void logSensorsThrottled() {
  static const unsigned long SENSOR_LOG_HEARTBEAT_MS = 600000UL;  // 10 min
  static bool havePrev = false;
  static long prevT10 = 0, prevH = 0, prevPH = 0, prevBat10 = 0;
  static unsigned long lastLogMs = 0;

  long t10   = lroundf(g_sensors.airTempC * 10.0f);
  long h     = lroundf(g_sensors.airHumidity);
  long ph    = g_sensors.plantHeightMm;
  long bat10 = lroundf(g_sensors.batteryVoltage * 10.0f);
  unsigned long now = millis();

  bool changed = !havePrev || t10 != prevT10 || h != prevH ||
                 ph != prevPH || bat10 != prevBat10;
  bool heartbeat = (now - lastLogMs) >= SENSOR_LOG_HEARTBEAT_MS;
  if (!changed && !heartbeat) return;

  havePrev = true;
  prevT10 = t10; prevH = h; prevPH = ph; prevBat10 = bat10;
  lastLogMs = now;

  DEBUG_PRINTF("[VERB]  Sensors: T=%.1fC H=%.0f%% PH=%dmm BAT=%.1fV\n",
               g_sensors.airTempC, g_sensors.airHumidity,
               g_sensors.plantHeightMm, g_sensors.batteryVoltage);
}

static void loopTaskSensorsIdle(const LoopDispatchContext* ctx) {
  (void)ctx;
  unsigned long now = millis();

#if ENABLE_SENSORS
  const unsigned long idleSensorIntervalMs = 60000UL;
  unsigned long sensorInterval = idleSensorIntervalMs;
  if (g_config.sensorIntervalMs > sensorInterval) {
    sensorInterval = g_config.sensorIntervalMs;
  }
  if (g_state.sensorsReady && (now - g_state.lastSensorRead >= sensorInterval)) {
    sensors_read(&g_sensors);
    g_state.lastSensorRead = now;
    g_sensors.timestamp = now;

    // Add to history buffer for local trends/charts
    history_addReading(&g_sensors);

    logSensorsThrottled();
  }
#endif
}

static void loopTaskSensorsGrowing(const LoopDispatchContext* ctx) {
  (void)ctx;
  unsigned long now = millis();

#if ENABLE_SENSORS
  unsigned long sensorInterval = g_config.sensorIntervalMs > 0
                                 ? g_config.sensorIntervalMs
                                 : SENSOR_READ_INTERVAL_MS;
  if (g_state.sensorsReady && (now - g_state.lastSensorRead >= sensorInterval)) {
    sensors_read(&g_sensors);
    g_state.lastSensorRead = now;
    g_sensors.timestamp = now;

    // Add to history buffer for local trends/charts
    history_addReading(&g_sensors);

    logSensorsThrottled();
  }
#endif
}

static void loopTaskLora(const LoopDispatchContext* ctx) {
  (void)ctx;
  unsigned long now = millis();

#if ENABLE_LORA
  if (g_state.loraReady) {
    unsigned long loraInterval = g_config.loraReportIntervalMs > 0
                                 ? g_config.loraReportIntervalMs
                                 : LORA_REPORT_INTERVAL_MS;
    if (now - g_state.lastLoraReport >= loraInterval) {
      if (lora_sendReport(&g_sensors, &g_state, &g_config)) {
        g_state.loraPacketsSent++;
      }
      g_state.lastLoraReport = now;
    }

    // Check for incoming commands
    char cmdBuf[64];
    if (lora_checkIncoming(cmdBuf, sizeof(cmdBuf))) {
      g_state.loraPacketsReceived++;
      g_sensors.loraRssi = lora_getLastRssi();
      g_sensors.loraSnr = lora_getLastSnr();

      char key[16], val[32];
      if (lora_parseCommand(cmdBuf, key, sizeof(key), val, sizeof(val))) {
        handleCommand(key, val);
      }
    }
  }
#endif
}

static void loopTaskWifiPortal(const LoopDispatchContext* ctx) {
  (void)ctx;

#if ENABLE_WIFI_PORTAL
  portal_update();
  if (g_state.wifiApActive && !portal_isActive()) {
    g_state.wifiApActive = false;
    DEBUG_INFO(F("WiFi AP timeout — portal closed"));
  }
#endif
}

static void loopTaskHistory(const LoopDispatchContext* ctx) {
  (void)ctx;
  history_update();
}

static void loopTaskUiIdle(const LoopDispatchContext* ctx) {
  (void)ctx;

#if ENABLE_EINK_DISPLAY
  if (!g_state.einkReady) return;
  unsigned long now = millis();
  if (eink_shouldUpdate(now, &g_lastEinkUpdateMs)) {
    eink_updateFromRuntime(&g_sensors, &g_state, &g_config, g_activePlant);
  }
#endif
}

static void loopTaskUiAwaiting(const LoopDispatchContext* ctx) {
  (void)ctx;

#if ENABLE_EINK_DISPLAY
  if (!g_state.einkReady) return;
  unsigned long now = millis();
  if (eink_shouldUpdate(now, &g_lastEinkUpdateMs)) {
    EinkFrameData frame;
    eink_buildFrameData(&g_sensors, &g_state, &g_config, g_activePlant, &frame);
    eink_copyText(frame.phaseLabel, sizeof(frame.phaseLabel), "Kuittaus");
    eink_copyText(frame.phaseGuidance, sizeof(frame.phaseGuidance), "Odottaa kuittausta");
    eink_updateFrame(&frame);
  }
#endif
}

static void loopTaskUiFault(const LoopDispatchContext* ctx) {
  loopTaskUiIdle(ctx);
}

static void loopTaskUiMaintenance(const LoopDispatchContext* ctx) {
  // Same UI tick as IDLE; the maintenance-specific presentation lives in
  // ux_indicator (button LED) and the e-ink advisory banner, both of which
  // read g_device.state rather than being told by this task.
  loopTaskUiIdle(ctx);
}

static void loopTaskActuators(const LoopDispatchContext* ctx) {
  (void)ctx;
  unsigned long now = millis();
  static bool motorDisablePending = false;
  static unsigned long motorDisableAtMs = 0;

#if MPG_TEST_ENABLED
  mpg_test_update();
#endif

#if ENABLE_MOTOR
  bool wasMoving = g_state.motorMoving;
  g_state.motorMoving = motor_update();

  if (motorDisablePending && !g_state.motorMoving && now >= motorDisableAtMs) {
    motor_disable();
    motorDisablePending = false;
    DEBUG_INFO(F("Motor: outputs released after idle delay"));
  }

  if (wasMoving && !g_state.motorMoving) {
    // Motor arrived — save position and disable driver to save power
    g_config.motorCurrentMm = motor_getPositionMm();
    #if MOTOR_DRIVER == MOTOR_DRIVER_L298N
      #if MOTOR_L298N_HOLD_ON_IDLE
        motorDisablePending = false;
        DEBUG_INFO(F("Motor: arrived (L298N hold-on-idle active)"));
      #else
        motorDisablePending = true;
        motorDisableAtMs = now + MOTOR_L298N_RELEASE_DELAY_MS;
        DEBUG_PRINTF("[INFO]  Motor: arrived, release in %lums\n", MOTOR_L298N_RELEASE_DELAY_MS);
      #endif
    #else
      motor_disable();
    #endif
    config_save(&g_config);
    DEBUG_PRINTF("[INFO]  Motor: arrived at %d mm\n", g_config.motorCurrentMm);
  }
#endif

#if ENABLE_MPG && ENABLE_MOTOR
  {
    static int mpgPendingMm = 0;
    static unsigned long mpgLastInputMs = 0;

    int delta = mpg_consumeDelta();
    if (delta != 0) {
      mpgPendingMm += delta;
      mpgLastInputMs = now;
    }

    if (mpgPendingMm != 0) {
      bool idleWindowElapsed = (now - mpgLastInputMs) >= MPG_BATCH_WINDOW_MS;
      bool batchReached = abs(mpgPendingMm) >= MPG_BATCH_MAX_MM;
      if (idleWindowElapsed || batchReached) {
        motor_moveBy(mpgPendingMm);
        g_config.motorTargetMm = motor_getTargetMm();
        DEBUG_PRINTF("[INFO]  MPG: %+d mm (batched) → target %d mm\n",
                     mpgPendingMm, g_config.motorTargetMm);
        mpgPendingMm = 0;
      }
    }
  }
#endif

#if ENABLE_LIGHT_RELAY
  // Manual light toggle (portal / CLI / LIGHT command) must drive the MOSFET
  // in IDLE too — the grow scheduler that normally consumes the force flags
  // only runs in DEVICE_GROWING.
  scheduler_applyLightOverride(&g_state, &g_config);
#endif

#if ENABLE_PUMP
  // Always call pump_update() so the FLOAT_OVF safety check runs each loop
  // (catches overflow even when the pump is idle but could be started shortly).
  g_state.pumpRunning = pump_update();
  // Opt-in: release the FLOAT_OVF latch a settle period after the bed drains so an
  // isolated overflow recovers unattended. Default OFF keeps the manual-clear latch.
  pump_overflowAutoClearTick(g_config.ebbOverflowAutoClear, OVERFLOW_AUTO_CLEAR_SETTLE_MS);
#endif
}

static void loopTaskGrowScheduler(const LoopDispatchContext* ctx) {
  (void)ctx;
  if (g_activePlant) {
    uint8_t actions = scheduler_update(g_activePlant, &g_sensors, &g_state, &g_config);
    if (actions & SCHED_ACTION_GROW_CHANGED) {
      // Grow day or phase changed — persist to LittleFS
      config_save(&g_config);
    }
    // Persist the day/photoperiod/step accumulators (interval-gated inside;
    // also fires right after a day tick or step advance so a power cut cannot
    // restore stale progress). GROWING-only task → IDLE never writes flash.
    growclock_tick(&g_state, &g_config, g_activePlant,
                   sensors_dliAccumulatedMol(), sensors_dliElapsedMs(millis()));
  }
}

static void loopTaskEbbFlow(const LoopDispatchContext* ctx) {
  (void)ctx;
#if ENABLE_EBB_FLOW && ENABLE_PUMP
  // Stop-only: a circulation ("kierto") cycle started while GROWING must finish
  // cleanly even if the device dropped to IDLE. mayStart=false never starts one
  // in IDLE (circulation belongs to DEVICE_GROWING, like periodic flooding).
  scheduler_updateCirculate(&g_sensors, &g_state, &g_config, /*mayStart=*/false);

  // In DEVICE_IDLE the grow scheduler does not run, so a manual force-flood
  // (button / portal "Aja tulva nyt") would set ebbFlowForceFloodRequested with
  // nothing to consume it. Tick the FSM here for exactly two cases: a pending
  // force-flood request, and an already-running cycle that must finish
  // (FLOOD→SOAK→DRAIN→IDLE). Periodic interval flooding is intentionally NOT
  // triggered in IDLE — that belongs to DEVICE_GROWING via the grow scheduler.
  if (!g_activePlant) return;
  // A fill- or soak-hold calibration session owns the pump — don't tick the FSM.
  if (g_state.ebbCalibPhase != EBB_CALIB_IDLE || g_state.soakCalibActive) return;
  bool midCycle = (g_state.ebbFlowState != ebbflow::IDLE);
  if (g_state.ebbFlowForceFloodRequested || midCycle) {
    scheduler_updateEbbFlow(g_activePlant, &g_sensors, &g_state, &g_config);
  }
#endif
}

static void loopTaskSerialInput(const LoopDispatchContext* ctx) {
  (void)ctx;

  // Format: CMD VALUE<newline>  e.g. "MOTOR_SPEED 500" or "motor_up"
  static char serialBuf[48];
  static uint8_t serialIdx = 0;
  while (DEBUG_SERIAL.available()) {
    char c = DEBUG_SERIAL.read();
    if (c == '\n' || c == '\r') {
      if (serialIdx > 0) {
        serialBuf[serialIdx] = '\0';
        // Split on first space into cmd + value
        char* sp = strchr(serialBuf, ' ');
        char val[32] = "";
        if (sp) { *sp = '\0'; strncpy(val, sp + 1, sizeof(val) - 1); }
        DEBUG_PRINTF("[INFO]  Serial cmd: '%s' val: '%s'\n", serialBuf, val);
        handleCommand(serialBuf, val);
        serialIdx = 0;
      }
    } else if (serialIdx < sizeof(serialBuf) - 1) {
      serialBuf[serialIdx++] = c;
    }
  }
}

static void loopTaskShutdown(const LoopDispatchContext* ctx) {
  (void)ctx;
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  esp_deep_sleep_start();
#endif
}

