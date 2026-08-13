/*=====================================================================
  structs.h - PlantMeister Data Structures

  All shared structs in one place. Include after config.h.
=====================================================================*/

#ifndef STRUCTS_H
#define STRUCTS_H

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// SENSOR DATA — latest readings from all sensors
// ═══════════════════════════════════════════════════════════════════

// 11-channel spectral readout from AS7341. Raw counts per channel,
// integration time + gain captured for cross-sample comparability.
struct LightSpectrum {
  uint16_t f1_415nm;
  uint16_t f2_445nm;
  uint16_t f3_480nm;
  uint16_t f4_515nm;
  uint16_t f5_555nm;
  uint16_t f6_590nm;
  uint16_t f7_630nm;
  uint16_t f8_680nm;
  uint16_t clear;
  uint16_t nir;
  uint16_t flickerHz;       // 0 = not detected, 50/60/100/120 typical
  uint16_t integrationMs;
  uint16_t gain;             // 0.5x..512x, the gain THIS sample was taken at
  // Vahintaan yksi PAR-kanava lyo ADC-katon -> PPFD on ALARAJA eika mittaus.
  // Kuluttaja ei saa esittaa taman naytteen lukemaa faktana: naytolla se
  // nakyisi tasanteena joka luetaan "valo lakkasi kirkastumasta".
  bool     saturated;
};

struct SensorData {
  // BME280 / SCD41 — air environment (T+RH+optional pressure+optional CO2)
  float airTempC;
  float airHumidity;
  float airPressureHpa;
  float vpdKpa;
  int   airCO2Ppm;          // SCD41-only (0 = invalid)

  // MLX90614 — IR leaf temperature (PE Energy + Plant balance)
  float leafTempC;          // Object temperature (lehti)
  float leafAmbientC;       // Sensor's own ambient (sanity check)

  // AS7341 — light spectrum (PE Assimilate balance)
  LightSpectrum lightSpectrum;
  float ppfdNow;            // Instantaneous PPFD (umol/m2/s) AT THE CANOPY (sensor x geometry)
  float ppfdSensor;         // Same reading at the SENSOR's own position (geometry factor not applied)
  float dliToday;           // Accumulated DLI (mol/m2/d) over rolling 24h since boot (dli_tracker.h)

  // DS18B20
  float waterTempC;

  // TDS (via ADS1115)
  int tdsPpm;

  // VL53L0X
  int plantHeightMm;       // Distance from sensor to plant top

  // Float switch
  bool waterLevelOk;        // true = water present
  bool waterOverflowActive; // true = overflow/high-level condition active

  // Battery
  float batteryVoltage;
  int batteryPercent;

  // INA228 / INA226 / INA219 — power monitoring (CAP_POWER_MONITORING)
  float powerBusV;         // Rail/bus voltage (V)
  float powerCurrentmA;    // Current (mA, signed) — mA keeps small-current resolution in float
  float powerMw;           // Power (mW)
  float powerChargemAh;    // Accumulated charge since boot/reset (mAh) — INA228 accumulator
  float powerEnergyWh;     // Accumulated energy since boot/reset (Wh) — INA228 accumulator

  // LoRa signal quality (last received)
  int loraRssi;
  int loraSnr;

  // Validity flags
  bool envValid;            // T+RH read OK (BME280 or SCD41)
  bool vpdValid;            // VPD calc valid (set when envValid is true)
  bool airCO2Valid;         // CO2 read OK (SCD41 only)
  bool leafTempValid;       // MLX90614 read OK
  bool spectrumValid;       // AS7341 read OK
  bool ppfdValid;           // PPFD estimated this cycle (requires valid spectrum)
  bool heightValid;         // VL53L0X read OK
  bool waterTempValid;      // DS18B20 read OK
  bool tdsValid;            // TDS read OK
  bool powerValid;          // INA228/226/219 power read OK

  // Age of the last successful env read (ms). UINT32_MAX = never read.
  // Non-zero while envValid is true means the value is held over a skipped
  // sample rather than measured this cycle (sensor_sticky.h).
  uint32_t envAgeMs;

  unsigned long timestamp;  // millis() when read
};

// ═══════════════════════════════════════════════════════════════════
// GROW PHASES — per-phase parameters for guided cultivation
// ═══════════════════════════════════════════════════════════════════

#define GROW_PHASE_MAX 6

enum GrowPhaseType : uint8_t {
  GROW_PHASE_ROOTING    = 0,   // Cuttings develop roots (no flooding)
  GROW_PHASE_SEEDLING   = 1,   // Seeds germinate / young plants establish
  GROW_PHASE_VEGETATIVE = 2,   // Active growth, full nutrient load
  GROW_PHASE_HARVEST    = 3,   // Harvest window (indefinite)
  GROW_PHASE_CUSTOM     = 4    // User-defined
};

// Plant empowerment -tavoitteet per kasvuvaihe. Malli: docs/kehitys/
// pe-ohjausmalli.md. Vanhat arvaus-parametrit (tempMin/Max = vain
// loki-varoitus, tdsTargetPpm = kuollut anturi) korvattiin PE-tavoitteilla
// jotka nojaavat siihen mita laite OIKEASTI mittaa. "Aja mita voit, valmenna
// mita et": DLI+PPFD ajettavissa (valo + korkeusmoottori), VPD/CO2/lehti
// vain advisory (ei kostutinta/CO2-injektoria/lammitinta).
//
// V1 (nyt): tavoitteet NAYTETAAN (tavoite vs. nykyarvo), laite ei viela aja
// niita silmukkana — valo pysyy tuntipohjaisena (lightHours). Suljettu
// silmukka on §5 vaihe 3, erikseen rautatodennettava.
struct GrowPhaseParams {
  GrowPhaseType type;
  char          label[16];        // Display name: "Juurtuminen", "Taimivaihe"...
  char          guidance[96];     // Phase-specific user guidance text
  uint16_t      durationDays;     // 0 = indefinite (stay until manually advanced)

  // ── PE-tavoitteet (mukavuusalue) ──
  uint8_t       dliTargetMol;     // DLI-tavoite mol/m²/vrk (AJA: valo — V3)
  uint16_t      ppfdTargetUmol;   // PPFD latvustossa µmol/m²/s (AJA: korkeus — V3)
  float         vpdMinKpa;        // VPD-mukavuusalue ala (VALMENNA)
  float         vpdMaxKpa;        // VPD-mukavuusalue yla (VALMENNA)
  uint16_t      co2TargetPpm;     // CO₂-tavoite ppm (VALMENNA)
  float         leafTempMaxC;     // Lehtilampo-katto °C (VALMENNA)

  // ── Juuristo (ajastus, ei kosteusanturia silmukkaan) ──
  uint16_t      floodIntervalMin; // 0 = no Ebb&Flow flooding this phase
  uint16_t      floodDurationSec; // Flood on-time per cycle

  // ── Aktuaattorisilta: valon tuntimaara kunnes DLI-silmukka (V3) todennettu ──
  uint8_t       lightHours;       // Hours of light per 24h

  // ── PE-mukavuuskaistat (M1, docs/kehitys/Fable_kehityspolku.md § M1) ──
  // Nama olivat aiemmin OLEMASSA VAIN e-inkin status_targets.h:n PE_TARGETS_*-
  // taulukoissa, kasin ylläpidettyna kaksoiskappaleena tama structin pisteesta
  // (dliTargetMol jne). /api/state julkaisee nama, ja e-ink lukee ne sielta
  // status_targetsForPhase()-taulukoiden sijaan — duplikaatti poistuu.
  // PE_NO_LIMIT (-1.0f, status_targets.h) = ei rajaa tahan suuntaan; kaytossa
  // vain lehtilammolle (vain katto, ei ala-rajaa — ei omaa kentaa tassa,
  // /api/state-rakentaja laittaa -1.0f suoraan).
  float         dliMinMol;   float dliMaxMol;    // DLI-mukavuusalue mol/m²/vrk
  float         ppfdMinUmol; float ppfdMaxUmol;  // PPFD-mukavuusalue µmol/m²/s
  float         co2MinPpm;   float co2MaxPpm;    // CO2-mukavuusalue ppm
};

// ═══════════════════════════════════════════════════════════════════
// PLANT CONFIG — per-species growing parameters
// ═══════════════════════════════════════════════════════════════════

struct PlantConfig {
  char id[24];              // "basil", "parsley", etc.
  char name[32];            // "Basilika", "Persilja" (display)
  int maxHeightMm;
  int lightHours;           // Hours of light per 24h (legacy / phase fallback)
  int waterMlPerDose;
  int waterIntervalHours;
  // PE-tavoitteet elavat per vaihe (GrowPhaseParams), eivat litteassa
  // plant-tasossa: tavoite riippuu kasvuvaiheesta. tempMin/Max ja tdsTargetPpm
  // poistettiin 18.7.2026 (docs/kehitys/pe-ohjausmalli.md) — edellinen oli
  // vain loki-varoitus, jalkimmainen kuollut (TDS-anturi pois).
  // Guided grow phases (phaseCount=0 means legacy flat-params mode)
  uint8_t        phaseCount;
  GrowPhaseParams phases[GROW_PHASE_MAX];
};

#define MAX_PLANTS 16

// ═══════════════════════════════════════════════════════════════════
// DEVICE CONFIG — saved to LittleFS, persists across reboots
// ═══════════════════════════════════════════════════════════════════

struct DeviceConfig {
  // LoRa
  uint8_t loraAddress;
  uint8_t loraNetworkId;
  uint8_t loraTargetAddress;

  // WiFi STA (home network)
  char wifiSsid[32];        // Home network SSID (empty = skip STA)
  char wifiPassword[64];    // Home network password
  bool wifiAutoConnect;     // Try to connect on boot
  char adminPin[12];        // Portal PIN (empty = auth disabled)

  // Current plant
  char currentPlantId[24];

  // Light schedule
  int lightOnHour;          // Hour offset for light cycle start (0-23)

  // Motor
  int motorCurrentMm;       // Current lamp height
  int motorTargetMm;        // Target lamp height

  // Timing overrides (0 = use plant defaults)
  unsigned long sensorIntervalMs;
  unsigned long loraReportIntervalMs;

  // State
  bool lightsForceOn;       // Manual override: force lights on
  bool lightsForceOff;      // Manual override: force lights off

  // Grow phase tracking (persisted)
  uint8_t  growPhase;           // Current phase index into activePlant->phases[]
  uint16_t growElapsedDays;     // Days elapsed in current phase (saved across reboots)
  bool     growActive;          // Guided grow cycle in progress
  uint8_t  growStartMethod;     // 0=cutting, 1=seed, 2=store-bought seedling
  uint8_t  growStepIndex;       // Step within the current phase's step list (grow_steps.h).
                                // >= list count = sequence finished (inert; e-ink returns to
                                // metric cards). MUST be reset via grow_resetStepProgress()
                                // whenever the list under it can change: grow start, phase
                                // advance (manual AND 3-day auto), plant change, start-method
                                // change — otherwise the index points into the wrong list.

  // EbbFlow fault latch (persisted — survives reboot, cleared on boot via config_clearBootOnlyOverrides)
  bool    ebbFlowFaultLatched;  // true = fault has triggered, operator must ACK
  uint8_t ebbFlowFaultCode;     // EbbFlowFaultCode enum value at time of fault

  // Test mode (persisted): true = portal AP stays up indefinitely, /api/test/* and
  // /api/command remain reachable without timing out. Manual switch in portal UI.
  bool    testMode;

  // Dev mode (persisted): true = Huolto-valilehti nakyvissa portaalissa.
  // Oletus false -> tuotannossa Huolto piilossa. Vaihdetaan 5x naputuksella
  // "Online"-tilakenttaan. EI DeviceState — pelkka UI-nakyvyyslippu, ei
  // vaikuta toiminnallisuuteen. Schema v5 (additiivinen, oletus false).
  bool    devMode;

  // Onboarding (persisted): true = first-run setup completed (or device predates
  // this field — see config_load migration). false = fresh device → portal shows
  // the setup checklist banner and the e-ink shows the join-AP instruction.
  // NOT a DeviceState: the device is functionally IDLE during onboarding, and
  // completing onboarding never auto-starts growing (growActive stays false).
  bool    onboardingComplete;

  // Onboarding (persisted): which setup steps the user has actually done,
  // as ONBOARD_STEP_* bits (onboarding.h). Needed because the config alone
  // cannot answer it — currentPlantId defaults to "basil" and growStartMethod
  // to 0, so a default is indistinguishable from a deliberate choice. Gates
  // /api/onboarding/complete and drives the ✓ marks in the portal banner.
  uint8_t onboardingSteps;

  // Physical button — what a short press does (long press = ButtonLongAction,
  // default back-navigation). Selectable from the portal for testing.
  // ButtonAction enum value.
  uint8_t buttonAction;

  // Physical button — what a long press means (ButtonLongAction enum value).
  // Runtime-selectable like buttonAction so the choice needs no reflash.
  uint8_t buttonLongAction;

  // Grow demo mode (persisted BUT boot-cleared via config_clearBootOnlyOverrides).
  // true = a short press during GROWING advances ALWAYS: through the current
  // step (even a TIMER step — no soak wait) and, when the step sequence ends,
  // on to the next phase. Lets the whole guidance sequence be walked screen by
  // screen at the device to review it, as if growing a real plant. Boot-clear
  // is deliberate: it must never silently linger into a real grow, where it
  // would let a stray press shorten a soak (grow_step_fsm.h TIMER safety).
  bool    growDemoMode;

  // Ebb&Flow timing overrides (persisted). 0 = use grow-phase / config.h default.
  // Let the operator tune the cycle live from the portal during a test run
  // without reflashing. Precedence: override (>0) > active grow phase > config.h.
  uint16_t ebbFloodIntervalMin;   // minutes between flood cycles
  uint16_t ebbFloodDurationSec;   // pump on-time per flood
  uint16_t ebbSoakDurationSec;    // flooded soak time
  uint16_t ebbDrainTimeoutSec;    // advisory drain timeout (never latches)
  // Continuous-flow soak hold: pump duty (%) the SOAK phase runs at to hold the
  // water level steady against drainage. 0 = legacy soak (pump off, stand-pipe
  // holds). >0 is clamped to [PUMP_SOAK_MIN_DUTY_PCT..100]. Set by the
  // soak-hold calibration wizard. See docs/ohjeet/wizardit.md (pump-soak).
  uint8_t  ebbSoakPwmPct;
  // Opt-in: auto-clear the pump FLOAT_OVF latch a settle period after the bed
  // drains (OVERFLOW_AUTO_CLEAR_SETTLE_MS), so an isolated overflow recovers
  // unattended. false (default) = latch clears only manually (portal/button).
  bool     ebbOverflowAutoClear;
  // Circulation ("kierto"): an independent low-level pump cycle that keeps the
  // nutrient solution flowing between full floods WITHOUT raising water to the
  // plant bed. Runs the pump at the soak duty (ebbSoakPwmPct) for
  // ebbCirculateDurationSec every ebbCirculateIntervalMin, only while ebb is IDLE.
  // Opt-in (disabled by default) until the duty is verified safe on the rig.
  bool     ebbCirculateEnabled;
  uint16_t ebbCirculateIntervalMin;
  uint16_t ebbCirculateDurationSec;
  // Separate, deliberately LOW pump duty for circulation — distinct from the soak
  // duty (which holds water at the flooded/root level). A lower duty keeps the
  // circulation level below the plant bed so it "ei kosketa kasveihin". Clamped to
  // [PUMP_SOAK_MIN_DUTY_PCT..100]; the operator verifies the level on the rig.
  uint8_t  ebbCirculateDutyPct;
};

// What the physical button's SHORT press triggers. Persisted in DeviceConfig.
// All actions route through the Intent system (input_router.h).
enum ButtonAction : uint8_t {
  BUTTON_ACTION_NONE      = 0,   // short press does nothing (default)
  BUTTON_ACTION_EBB_FLOOD = 1,   // force an immediate ebb&flow flood now
  BUTTON_ACTION_SHUTDOWN  = 2,   // opt-in: shut down (deep sleep). No longer
                                 // wired to LONG_PRESS (K-A, 14.7.2026) --
                                 // only reachable via this portal setting.
  BUTTON_ACTION_REBOOT    = 3    // reboot the device
};
#define BUTTON_ACTION_MAX 3

// What the physical button's LONG press means. Persisted in DeviceConfig.
//
// K-C delta (user decision 22.7.2026): the long press is BACK-navigation by
// default. In DEVICE_GROWING it steps the guided sequence BACKWARDS — previous
// step, or the previous phase's last step when already at the first step
// (input_router.h INTENT_PREV_STEP, floored at the grow's start phase). It no
// longer enters maintenance: that is now a phone/CLI action only (pm.py
// maintenance on|off, MAINTENANCE_ON/OFF commands). Reclaiming the gesture for
// "undo a mis-press" is what the operator actually reaches for during a
// walkthrough; a deliberate, non-physical channel (the phone) suits entering a
// safety lock better than a gesture that is easy to trigger by accident.
//
// A long press while ALREADY in maintenance still EXITS it (button_intent_map.h)
// — the lock must never become un-exitable from the device even if the phone
// is unavailable.
//
// D14 (2a, docs/kehitys/m0.1-toteutussuunnitelma.md § 2): the legacy opt-in
// that let a config value re-enter maintenance from any state via long press
// (BUTTON_LONG_ACTION_MAINTENANCE) is removed — it was a second road into
// maintenance, which made D6 ("long press is always back-navigation")
// impossible to state. config_clampBounds() still folds any old stored value
// above BUTTON_LONG_ACTION_MAX back to BACK, so a device upgrading from a
// config with the legacy opt-in set simply loses it, silently and safely.
enum ButtonLongAction : uint8_t {
  BUTTON_LONG_ACTION_BACK        = 0,  // step/phase back-navigation (only meaning)
};
#define BUTTON_LONG_ACTION_MAX 0

// ═══════════════════════════════════════════════════════════════════
// CALIBRATION DATA — saved to LittleFS, runtime tuning values
// ═══════════════════════════════════════════════════════════════════

struct CalibrationData {
  float tdsOffset;          // ppm offset after raw conversion
  float tdsGain;            // multiplier after raw conversion
  float pumpMlPerSec;       // measured pump throughput
  int32_t motorStepsUp;     // upper travel limit in motor steps
  int32_t motorStepsDown;   // lower travel limit in motor steps
  float ppfdCalibrationFactor; // PPFD calibration scale factor (1.0 = relative, set via /api/calib/ppfd)
  // Sensor position -> canopy position scale. The AS7341 rarely sits exactly at
  // the canopy (it would shade the plant or be shaded by it), so its reading is
  // systematically off by a fixed ratio. 1.0 = sensor sits at canopy level.
  // Set via /api/calib/ppfd/geometry — see docs/kasvatus/ppfd-mittausopas.md.
  float ppfdGeometryFactor;

  // INA228 power monitor — runtime config + calibration (WIZARD: power-current-cal).
  // shunt/maxCurrent/adcRange are Level-1 runtime config (adjust without reflash);
  // calFactor is the reference-current correction (1.0 = uncalibrated).
  float   powerShuntOhms;    // physical shunt resistance (ohms)
  float   powerMaxCurrentA;  // expected max current -> CURRENT_LSB
  uint8_t powerAdcRange;     // 0 = +/-163.84 mV, 1 = +/-40.96 mV
  float   powerCalFactor;    // SHUNT_CAL correction multiplier from calibration
};

// ═══════════════════════════════════════════════════════════════════
// SYSTEM STATE — runtime, not saved
// ═══════════════════════════════════════════════════════════════════

struct SystemState {
  bool sensorsReady;
  bool loraReady;
  bool motorReady;
  bool pumpReady;
  bool einkReady;
  bool filesystemReady;
  bool wifiApActive;

  bool lightsOn;
  bool airPumpOn;
  bool pumpRunning;
  bool motorMoving;

  // Fan (PCB v2) — PWM speed + coarse tacho health. mcp23017Ready gates the
  // relocated FLOAT_MIN read and the tacho; fanReady is the PWM output alone.
  bool mcp23017Ready;   // MCP23017 expander present on I2C
  bool fanReady;        // fan PWM output initialized (LEDC attached)
  uint8_t fanDutyPct;   // commanded fan speed 0-100
  bool fanSpinning;     // coarse tacho: fan is turning
  bool fanStalled;      // health advisory: commanded on but tacho shows no spin

  unsigned long bootTime;
  unsigned long lastSensorRead;
  unsigned long lastLoraReport;
  unsigned long lastWaterDose;
  unsigned long lastHeightCheck;
  unsigned long lightCycleStartMs;

  unsigned long loraPacketsSent;
  unsigned long loraPacketsReceived;

  // Pump tracking
  unsigned long pumpStartTime;
  unsigned long pumpDurationMs;     // How long to run for current dose

  // Ebb&Flow runtime state
  uint8_t ebbFlowState;             // EbbFlowState enum value
  uint8_t ebbFlowFaultCode;         // EbbFlowFaultCode enum value
  unsigned long ebbFlowStateSinceMs;
  unsigned long lastEbbFlowCycleStartMs;
  bool ebbFlowFaultLatched;
  bool ebbFlowAckRequested;
  bool ebbFlowForceFloodRequested;  // one-shot: user asked for an immediate flood (button/portal)
  uint8_t ebbFlowOverflowCount;     // consecutive overflowing floods (FSM retry counter)

  // Circulation ("kierto") runtime — independent low-level anti-stagnation cycle.
  unsigned long lastCirculateStartMs;  // millis() when the last circulation began
  bool circulateActive;                // pump currently running a circulation cycle

  // Ebb&Flow fill calibration session (runtime-only, not persisted).
  // Captures fill/drain durations by button press, mirroring the motor
  // limit-capture UX but timing-based instead of step-position-based.
  uint8_t ebbCalibPhase;            // EbbCalibPhase: 0=idle, 1=filling, 2=draining
  unsigned long ebbCalibMarkMs;     // millis() at start of current phase (fill or drain)

  // Soak-hold calibration session (runtime-only, not persisted). While true the
  // wizard owns the pump (continuous PWM trim) and the ebb&flow FSM tick is
  // blocked, mirroring how ebbCalibPhase != IDLE blocks it. WIZARD: pump-soak.
  bool soakCalibActive;

  // Grow phase runtime state (not persisted — reset on boot)
  unsigned long growDayStartMs;          // millis() when day-tick tracking started
  bool          growPhasePendingAdvance; // System proposed phase transition, awaiting confirm
  unsigned long growAdvanceProposedMs;   // millis() when proposal was made (for 3-day timeout)
  unsigned long growStepStartMs;         // millis() anchor: elapsed in current grow step
                                         // (grow_steps.h; restored via growclock like day/light)
};

enum EbbFlowState {
  EBB_STATE_IDLE = 0,
  EBB_STATE_FLOOD = 1,
  EBB_STATE_SOAK = 2,
  EBB_STATE_DRAIN = 3,
  EBB_STATE_FAULT = 4
};

enum EbbCalibPhase : uint8_t {
  EBB_CALIB_IDLE = 0,
  EBB_CALIB_FILLING = 1,   // pump on, waiting for user to mark "full"
  EBB_CALIB_DRAINING = 2   // pump off, waiting for user to mark "empty"
};

enum EbbFlowFaultCode {
  EBB_FAULT_NONE = 0,
  EBB_FAULT_DRY_RUN = 1,
  EBB_FAULT_OVERFLOW = 2,
  EBB_FAULT_FLOOD_TIMEOUT = 3,
  EBB_FAULT_DRAIN_TIMEOUT = 4,
  EBB_FAULT_SENSOR_FAULT = 5
};

#endif // STRUCTS_H
