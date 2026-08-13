/*=====================================================================
  scheduler.h - Growing Schedule Manager

  Controls light cycle, watering, and height adjustment based on
  plant parameters. All timing is millis()-based (no RTC needed).

  Light cycle: X hours on, (24-X) hours off, repeating from boot.
  Watering: every N hours, dose M ml (if pump enabled).
  Height: raise lamp when plant grows within margin.
=====================================================================*/

#ifndef SCHEDULER_H
#define SCHEDULER_H

// Action bitmask returned by scheduler_update()
#define SCHED_ACTION_LIGHT_CHANGED   0x01
#define SCHED_ACTION_WATER           0x02
#define SCHED_ACTION_HEIGHT          0x04
#define SCHED_ACTION_GROW_CHANGED    0x08

#include "config.h"
#include "structs.h"
#include "calibration_math.h"   // calibration_clampSoakDutyPct (soak-hold duty clamp)
#include "scheduler_ebb_helpers.h"
#include "grow_step_fsm.h"      // askelmoottori: auto-advance + reset vaihesiirtymissa
#if ENABLE_EBB_FLOW && ENABLE_PUMP
#include "ebbflow_fsm.h"
#endif

// ── Active grow phase helper ────────────────────────────────────

// Returns current GrowPhaseParams if grow is active and plant has phases, else NULL.
static const GrowPhaseParams* scheduler_getActivePhase(const PlantConfig* plant,
                                                        const DeviceConfig* cfg) {
  if (!cfg->growActive || !plant || plant->phaseCount == 0) return NULL;
  if (cfg->growPhase >= plant->phaseCount) return NULL;
  return &plant->phases[cfg->growPhase];
}

// Decide whether the ebb&flow FSM should tick this scheduler cycle.
//  - A calibration session (calibIdle == false) blocks everything: it owns the pump.
//  - Periodic flooding runs only when floodEnabled (active phase allows it).
//  - A manual force-flood ALWAYS runs — an explicit override beats a phase that
//    disables auto-flooding (e.g. rooting phase, floodIntervalMin=0).
//  - An in-progress cycle (midCycle) always runs so FLOOD→SOAK→DRAIN→IDLE finishes.
// Pure boolean logic so it is unit-testable without the pump/ebb compile path.
static inline bool scheduler_shouldTickEbbFlow(bool calibIdle, bool floodEnabled,
                                               bool forceRequested, bool midCycle) {
  return calibIdle && (floodEnabled || forceRequested || midCycle);
}

// Whether periodic flooding is enabled this cycle. By default the active grow
// phase governs (floodIntervalMin == 0 means a no-flood phase, e.g. rooting). BUT
// an explicit portal interval override (cfgFloodIntervalMin > 0) also ENABLES
// flooding — consistent with the timing precedence below ("the DeviceConfig
// override wins over the active grow phase"). Without this an operator who set an
// interval in a no-flood phase saw nothing happen: the value was honoured but the
// enable was not. Pure boolean logic so it is unit-testable.
static inline bool scheduler_floodEnabled(const GrowPhaseParams* activePhase,
                                          uint16_t cfgFloodIntervalMin) {
  if (cfgFloodIntervalMin > 0) return true;            // explicit override enables
  return !activePhase || activePhase->floodIntervalMin > 0;  // else phase decides
}

// Continuous-flow soak: pump command derived from an ebb&flow state transition.
// The ebb FSM stays pure (it only sequences IDLE/FLOOD/SOAK/DRAIN); this decides
// whether the SOAK phase should drive the pump at the configured soak duty.
//   - Entering SOAK with a non-zero soak duty -> RUN the pump at that duty.
//   - Leaving SOAK -> STOP the pump (unconditional; harmless if already off).
//   - Staying in SOAK -> NONE (pump keeps the duty set on entry; no restart, so
//     a 5 min absolute-cap trip is NOT auto-restarted mid-soak).
//   - soakDuty == 0 -> legacy soak (pump off): no RUN on entry.
// Pure so it is unit-testable without the pump/ebb compile path.
enum SchedSoakCmd : uint8_t { SOAK_CMD_NONE = 0, SOAK_CMD_RUN = 1, SOAK_CMD_STOP = 2 };

static inline SchedSoakCmd scheduler_soakPumpCommand(uint8_t prevState, uint8_t curState,
                                                     uint8_t soakDuty) {
  bool entering = (curState == EBB_STATE_SOAK && prevState != EBB_STATE_SOAK);
  bool leaving  = (prevState == EBB_STATE_SOAK && curState != EBB_STATE_SOAK);
  if (entering && soakDuty > 0) return SOAK_CMD_RUN;
  if (leaving) return SOAK_CMD_STOP;
  return SOAK_CMD_NONE;
}

// ── Ebb&Flow helpers ───────────────────────────────────────────

#if ENABLE_EBB_FLOW && ENABLE_PUMP
static const char* scheduler_ebbStateName(uint8_t state) {
  switch (state) {
    case EBB_STATE_IDLE:  return "IDLE";
    case EBB_STATE_FLOOD: return "FLOOD";
    case EBB_STATE_SOAK:  return "SOAK";
    case EBB_STATE_DRAIN: return "DRAIN";
    case EBB_STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
  }
}

static bool scheduler_updateEbbFlow(const PlantConfig* plant, SensorData* data,
                                    SystemState* state, DeviceConfig* cfg) {
  // Sensor contradiction: water both too low AND overflowing = sensor fault
  if (!data->waterLevelOk && data->waterOverflowActive) {
    if (state->ebbFlowState != EBB_STATE_FAULT) {
      DEBUG_ERROR(F("EbbFlow: sensor contradiction (low + overflow) — entering FAULT"));
      pump_stop();
      state->pumpRunning = false;
      state->ebbFlowState = EBB_STATE_FAULT;
      state->ebbFlowFaultCode = EBB_FAULT_SENSOR_FAULT;
      state->ebbFlowFaultLatched = true;
      state->ebbFlowStateSinceMs = millis();
      return true;
    }
    return false;
  }

  // Timing precedence: DeviceConfig override (>0, set live from the portal) wins
  // over the active grow phase, which wins over the config.h compile-time default.
  const GrowPhaseParams* phase = scheduler_getActivePhase(plant, cfg);
  unsigned long floodIntervalMs = EBB_FLOW_FLOOD_INTERVAL_MIN * 60000UL;
  unsigned long floodDurationMs = EBB_FLOW_FLOOD_DURATION_SEC * 1000UL;
  if (phase && phase->floodIntervalMin > 0) {
    floodIntervalMs = (unsigned long)phase->floodIntervalMin * 60000UL;
    floodDurationMs = (unsigned long)phase->floodDurationSec * 1000UL;
  }
  if (cfg->ebbFloodIntervalMin > 0) floodIntervalMs = (unsigned long)cfg->ebbFloodIntervalMin * 60000UL;
  if (cfg->ebbFloodDurationSec > 0) floodDurationMs = (unsigned long)cfg->ebbFloodDurationSec * 1000UL;

  unsigned long soakMs  = (cfg->ebbSoakDurationSec > 0)
                          ? (unsigned long)cfg->ebbSoakDurationSec * 1000UL
                          : EBB_FLOW_SOAK_DURATION_SEC * 1000UL;
  unsigned long drainMs = (cfg->ebbDrainTimeoutSec > 0)
                          ? (unsigned long)cfg->ebbDrainTimeoutSec * 1000UL
                          : EBB_FLOW_DRAIN_TIMEOUT_SEC * 1000UL;

  // EF-FW-004: reduce flood frequency when lights are off
  if (!state->lightsOn) {
    floodIntervalMs *= EBB_NIGHT_FLOOD_MULTIPLIER;
  }

  ebbflow::Config fsmCfg = {
    floodIntervalMs,
    floodDurationMs,
    soakMs,
    drainMs,
    EBB_FLOW_MAX_OVERFLOW_RETRIES
  };

  ebbflow::Inputs in = {
    millis(),
    data->waterLevelOk,
#if ENABLE_RESERVOIR_LEVEL_SAFETY
    data->waterOverflowActive,
#else
    false,
#endif
    pump_isRunning(),
    state->ebbFlowAckRequested,
    state->ebbFlowForceFloodRequested
  };

  state->ebbFlowAckRequested = false;
  state->ebbFlowForceFloodRequested = false;

  ebbflow::Runtime rt = {
    static_cast<ebbflow::State>(state->ebbFlowState),
    static_cast<ebbflow::Fault>(state->ebbFlowFaultCode),
    state->ebbFlowStateSinceMs,
    state->lastEbbFlowCycleStartMs,
    state->ebbFlowFaultLatched,
    state->ebbFlowOverflowCount
  };

  ebbflow::Result result = ebbflow::step(&fsmCfg, &in, &rt);

  if (result.start_pump) {
    if (!pump_startForMs(floodDurationMs)) {
      if (pump_isOverflowLatched()) {
        // The pump-HAL FLOAT_OVF latch (not a sensor fault) blocked this flood.
        // Don't mislabel it FAULT_SENSOR and don't hard-latch the FSM: relieve by
        // draining and surface an honest, non-latching overflow advisory. With
        // overflow auto-clear enabled the latch releases once the bed drains and
        // the FSM retries (bounded by max_overflow_retries); with it disabled the
        // operator clears the latch manually. The FSM's own overflow_count still
        // hard-latches a chronically overflowing bed.
        rt.overflow_count++;
        DEBUG_WARNF("[WARN]  EbbFlow: pump-HAL FLOAT_OVF latch blocked flood start, draining (overflow_count=%u)\n",
                    rt.overflow_count);
        rt.fault = ebbflow::FAULT_OVERFLOW;
        rt.state = ebbflow::DRAIN;
        rt.state_since_ms = millis();
        state->pumpRunning = false;
        result.fault_changed = true;
        result.state_changed = true;
      } else {
        rt.state = ebbflow::FAULT;
        rt.fault = ebbflow::FAULT_SENSOR;
        rt.fault_latched = true;
        rt.state_since_ms = millis();
        result.stop_pump = true;
        result.fault_changed = true;
        result.state_changed = true;
      }
    } else {
      state->pumpRunning = true;
    }
  }

  if (result.stop_pump) {
    pump_stop();
    state->pumpRunning = false;
  }

  uint8_t prevEbbState = state->ebbFlowState;
  if (state->ebbFlowState != static_cast<uint8_t>(rt.state)) {
    DEBUG_PRINTF("[INFO]  EbbFlow: %s -> %s\n",
                 scheduler_ebbStateName(state->ebbFlowState),
                 scheduler_ebbStateName(static_cast<uint8_t>(rt.state)));
  }

  state->ebbFlowState = static_cast<uint8_t>(rt.state);
  state->ebbFlowFaultCode = static_cast<uint8_t>(rt.fault);
  state->ebbFlowStateSinceMs = rt.state_since_ms;
  state->lastEbbFlowCycleStartMs = rt.last_cycle_start_ms;
  state->ebbFlowFaultLatched = rt.fault_latched;
  state->ebbFlowOverflowCount = rt.overflow_count;

  // Continuous-flow soak: drive the pump at the configured soak duty while in
  // the SOAK phase (0 = legacy pump-off soak). pump_setContinuous() enforces the
  // 5 min absolute cap and FLOAT_OVF latch; an overflow during SOAK is already
  // handled by the FSM (drain/latch via result.stop_pump above) and by
  // pump_update()'s direct FLOAT_OVF read each loop.
  uint8_t soakDuty = calibration_clampSoakDutyPct(cfg->ebbSoakPwmPct);
  switch (scheduler_soakPumpCommand(prevEbbState, state->ebbFlowState, soakDuty)) {
    case SOAK_CMD_RUN:
      if (pump_setContinuous(soakDuty)) {
        state->pumpRunning = true;
        DEBUG_PRINTF("[INFO]  EbbFlow: soak hold @ %u%%\n", (unsigned)soakDuty);
      }
      break;
    case SOAK_CMD_STOP:
      pump_stop();
      state->pumpRunning = false;
      break;
    case SOAK_CMD_NONE:
    default:
      break;
  }

  return result.start_pump || result.stop_pump || result.state_changed || result.fault_changed;
}

// Circulation ("kierto"): independent low-level anti-stagnation cycle. Runs the
// pump at the LOW circulation duty (ebbCirculateDutyPct, separate from the soak
// duty) for ebbCirculateDurationSec, keeping the solution moving WITHOUT raising
// water to the bed. Starts only when enabled, due, the ebb FSM is
// IDLE (flood always wins) and no calibration owns the pump; mayStart=false makes
// it stop-only (used in the IDLE dispatch so a cycle started while GROWING still
// stops cleanly). Inherits pump safety: dry-run guard, FLOAT_OVF latch, 5 min cap.
// Returns true if it started or stopped the pump this tick.
static bool scheduler_updateCirculate(SensorData* data, SystemState* state,
                                      DeviceConfig* cfg, bool mayStart) {
  unsigned long now = millis();

  if (state->circulateActive) {
    unsigned long durMs = (unsigned long)cfg->ebbCirculateDurationSec * 1000UL;
    if (now - state->lastCirculateStartMs >= durMs) {
      pump_stop();
      state->pumpRunning = false;
      state->circulateActive = false;
      DEBUG_INFO(F("Circulate: cycle complete"));
      return true;
    }
    return false;  // keep circulating
  }

  if (!mayStart) return false;

  bool ebbIdle   = (state->ebbFlowState == EBB_STATE_IDLE);
  bool calibIdle = (state->ebbCalibPhase == EBB_CALIB_IDLE && !state->soakCalibActive);
  if (!scheduler_shouldStartCirculate(cfg->ebbCirculateEnabled, ebbIdle, calibIdle,
                                      state->circulateActive, cfg->ebbCirculateIntervalMin,
                                      state->lastCirculateStartMs, now)) {
    return false;
  }

  // Dry-run guard + require a positive (low) circulation duty. The circulation
  // duty is deliberately separate from and lower than the soak duty so the level
  // stays below the bed. NOTE: with no basin-level sensor the "ei kosketa kasveihin"
  // guarantee rests on a low duty + short duration verified on the rig; FLOAT_OVF
  // is the overfill backstop. Precise basin-level coordination (and ensuring a
  // flood never starts before circulation water has drained) awaits a dedicated
  // height sensor — a deliberate future enhancement, not handled here.
  if (!data->waterLevelOk) return false;
  uint8_t duty = cfg->ebbCirculateDutyPct;
  if (duty == 0) return false;

  if (pump_setContinuous(duty)) {
    state->pumpRunning = true;
    state->circulateActive = true;
    state->lastCirculateStartMs = now;
    DEBUG_PRINTF("[INFO]  Circulate: start %u s @ %u%%\n",
                 (unsigned)cfg->ebbCirculateDurationSec, (unsigned)duty);
    return true;
  }
  return false;  // pump refused (e.g. FLOAT_OVF latched) — retry next interval
}
#endif

// ── Light schedule ──────────────────────────────────────────────

// Determine if lights should be on based on elapsed time
bool scheduler_shouldLightBeOn(const PlantConfig* plant, const SystemState* state,
                               const DeviceConfig* cfg) {
  // Manual overrides
  if (cfg->lightsForceOn) return true;
  if (cfg->lightsForceOff) return false;

  // Etiolaation esto (Fable-kasikirjoitus §3.1): idatysaskelsarjan aikana valo
  // pysyy OFF. Siemenaloituksessa taimivaihe (SEEDLING) alkaa askelsarjalla
  // liota->kylva->idata; pimeydessa venynyt taimi kaatuu myohemmin omaan
  // painoonsa. Valo syttyy vasta kun kayttaja kuittaa sirkkalehdet nakyviin ja
  // sarja paattyy (growstep_sequenceActive -> false). Vain SEEDLING: pistokkaan
  // juurtuminen (ROOTING) haluaa epasuoran valon, ei pimeaa.
  if (growstep_sequenceActive(plant, cfg) &&
      cfg->growPhase < plant->phaseCount &&
      plant->phases[cfg->growPhase].type == GROW_PHASE_SEEDLING) {
    return false;
  }

  // Use grow phase light hours if active, else fall back to plant flat param
  const GrowPhaseParams* phase = scheduler_getActivePhase(plant, cfg);
  int lightHours = phase ? phase->lightHours : plant->lightHours;

  // Calculate where we are in the 24h cycle
  unsigned long cycleMs = 24UL * 3600000UL;
  unsigned long lightOnMs = (unsigned long)lightHours * 3600000UL;
  unsigned long offsetMs = (unsigned long)cfg->lightOnHour * 3600000UL;

  // Time since light cycle reference point
  unsigned long elapsed = (millis() - state->lightCycleStartMs + offsetMs) % cycleMs;

  return elapsed < lightOnMs;
}

// Hours into the current light period, or -1 when the cycle is in its dark
// part. Deliberately mirrors the arithmetic above rather than re-deriving it
// from a wall clock: `lightOnHour` is an OFFSET into a cycle anchored at
// state->lightCycleStartMs, NOT an hour of the day. A consumer that assumed
// wall-clock time would drift by however long ago the grow started — the
// e-ink DLI projection made exactly that assumption before this was exposed
// (29.7.2026), which is why the device now answers the question itself
// instead of publishing raw config for someone else to misread.
float scheduler_lightElapsedHours(const PlantConfig* plant, const SystemState* state,
                                  const DeviceConfig* cfg) {
  if (!plant) return -1.0f;

  const GrowPhaseParams* phase = scheduler_getActivePhase(plant, cfg);
  int lightHours = phase ? phase->lightHours : plant->lightHours;
  if (lightHours <= 0) return -1.0f;

  unsigned long cycleMs = 24UL * 3600000UL;
  unsigned long lightOnMs = (unsigned long)lightHours * 3600000UL;
  unsigned long offsetMs = (unsigned long)cfg->lightOnHour * 3600000UL;
  unsigned long elapsed = (millis() - state->lightCycleStartMs + offsetMs) % cycleMs;

  if (elapsed >= lightOnMs) return -1.0f;      // dark part of the cycle
  return (float)elapsed / 3600000.0f;
}

// Apply a manual light override (lightsForceOn/Off) outside the grow
// scheduler. scheduler_update() — the normal consumer of these flags — only
// runs in DEVICE_GROWING, so a portal/CLI light toggle issued in IDLE would
// otherwise set a flag nothing reads. Called from the actuator loop task
// (runs in IDLE and GROWING); idempotent with scheduler_update(), which
// evaluates the same override first. Returns true if the light state changed.
bool scheduler_applyLightOverride(SystemState* state, const DeviceConfig* cfg) {
#if ENABLE_LIGHT_RELAY
  if (!cfg->lightsForceOn && !cfg->lightsForceOff) return false;
  bool shouldBeOn = cfg->lightsForceOn;
  if (shouldBeOn != state->lightsOn) {
    power_setLight(shouldBeOn);
    state->lightsOn = shouldBeOn;
    DEBUG_PRINTF("[INFO]  Light override: %s\n", shouldBeOn ? "ON" : "OFF");
    return true;
  }
#else
  (void)state; (void)cfg;
#endif
  return false;
}

// ── Watering schedule ───────────────────────────────────────────

// Check if it's time to water
bool scheduler_shouldWater(const PlantConfig* plant, const SystemState* state,
                           const SensorData* data) {
#if ENABLE_PUMP
  // Don't water if pump is already running
  if (state->pumpRunning) return false;

  // Don't water if water level is low
  if (!data->waterLevelOk) return false;

  // Check interval
  unsigned long intervalMs = (unsigned long)plant->waterIntervalHours * 3600000UL;
  if (millis() - state->lastWaterDose < intervalMs) return false;

  // TDS-pohjainen annostelun ohitus poistettiin 18.7.2026: TDS-anturi on
  // pois (ENABLE_TDS_SENSOR=false) ja tdsTargetPpm oli kuollut kentta
  // (docs/kehitys/pe-ohjausmalli.md). Ravinnetavoite palaa jos/kun EC/TDS-
  // anturi lisataan — silloin PE-tavoitteena per vaihe, ei litteana lukuna.

  return true;
#else
  return false;
#endif
}

// ── Height adjustment ───────────────────────────────────────────

// Check if lamp needs to be raised
// VL53L0X measures distance from sensor (at lamp level) down to plant top.
// When distance gets small, plant is close to lamp — raise it.
bool scheduler_shouldRaiseHeight(const PlantConfig* plant, const SystemState* state,
                                 const SensorData* data, int currentHeightMm) {
#if ENABLE_MOTOR && ENABLE_HEIGHT_SENSOR
  if (!data->heightValid) return false;

  // Check interval
  if (millis() - state->lastHeightCheck < HEIGHT_CHECK_INTERVAL_MS) return false;

  // If distance to plant is less than margin, raise lamp
  if (data->plantHeightMm < MOTOR_HEIGHT_MARGIN_MM) {
    // Don't exceed max height
    if (currentHeightMm >= plant->maxHeightMm) return false;
    return true;
  }

  return false;
#else
  return false;
#endif
}

// Calculate new lamp height (raise by 10mm increments)
int scheduler_calcNewHeight(int currentMm, int maxMm) {
  int newHeight = currentMm + 10;
  if (newHeight > maxMm) newHeight = maxMm;
  return newHeight;
}

// Ilman lampotilan raja-varoitus (scheduler_checkTemp) poistettiin
// 18.7.2026. Se luki plant->tempMinC/tempMaxC (poistetut kentat) ja tulosti
// vain loki-[WARN]in — ei aktuoinut mitaan. PE-mallissa (docs/kehitys/
// pe-ohjausmalli.md) kasvin lampoa valvotaan LEHTILAMMON kautta
// (leafTempMaxC per vaihe, MLX90614), joka on se mika kasville merkitsee —
// ei ilman lampo. Lehtilampo-valmennus tulee V2:ssa (advisory-logiikka).

// ── Grow phase tracking ─────────────────────────────────────────

// Advances day counter and detects when a phase duration has elapsed.
// Returns true if growElapsedDays or growPhase changed (caller should save config).
static bool scheduler_updateGrowPhase(const PlantConfig* plant, SystemState* state,
                                       DeviceConfig* cfg) {
  if (!cfg->growActive || !plant || plant->phaseCount == 0) return false;
  if (cfg->growPhase >= plant->phaseCount) return false;

  unsigned long now = millis();
  bool changed = false;

  // Day tick: every 24h increment elapsed days
  if (now - state->growDayStartMs >= 86400000UL) {
    state->growDayStartMs += 86400000UL;  // Advance by exactly 24h to avoid drift
    cfg->growElapsedDays++;
    changed = true;
    DEBUG_PRINTF("[INFO]  Grow: phase %d (%s) day %d\n",
                 cfg->growPhase, plant->phases[cfg->growPhase].label,
                 cfg->growElapsedDays);
  }

  const GrowPhaseParams& phase = plant->phases[cfg->growPhase];

  // Propose phase advance when duration elapsed (durationDays=0 means indefinite)
  if (!state->growPhasePendingAdvance &&
      phase.durationDays > 0 &&
      cfg->growElapsedDays >= phase.durationDays) {
    uint8_t nextPhase = cfg->growPhase + 1;
    if (nextPhase < plant->phaseCount) {
      state->growPhasePendingAdvance = true;
      state->growAdvanceProposedMs = now;
      DEBUG_PRINTF("[INFO]  Grow: phase %d (%s) complete — advance to %s? (3-day timeout)\n",
                   cfg->growPhase, phase.label,
                   plant->phases[nextPhase].label);
    }
    // If already at last phase, stay indefinitely
  }

  // 3-day auto-advance if user hasn't responded
  if (state->growPhasePendingAdvance &&
      (now - state->growAdvanceProposedMs >= 3UL * 86400000UL)) {
    uint8_t nextPhase = cfg->growPhase + 1;
    if (nextPhase < plant->phaseCount) {
      cfg->growPhase = nextPhase;
      cfg->growElapsedDays = 0;
      state->growPhasePendingAdvance = false;
      state->growDayStartMs = now;
      // Vaihe vaihtui -> askellista sen alla vaihtui. Ilman nollausta indeksi
      // osoittaisi uuden vaiheen listaan vanhalla arvolla (grow_step_fsm.h).
      // Tama on se viidesta reset-paikasta joka unohtuu helpoimmin, koska
      // siirtyma tapahtuu ilman kayttajan komentoa.
      grow_resetStepProgress(state, cfg, now);
      changed = true;
      DEBUG_PRINTF("[INFO]  Grow: auto-advanced to phase %d (%s)\n",
                   cfg->growPhase, plant->phases[cfg->growPhase].label);
    } else {
      state->growPhasePendingAdvance = false;
    }
  }

  return changed;
}

// ── Grow step tracking (grow_steps.h + grow_step_fsm.h) ─────────────
// Etenee TIMER-askeleen itsestaan kun sen kello tayttyy (liotus 30 min).
// BUTTON-askeleet etenevat vain intentilla (input_router.h INTENT_ACK_STEP/
// SKIP). Palauttaa true kun indeksi muuttui -> kutsuja persistoi configin
// (SCHED_ACTION_GROW_CHANGED-polku, sama kuin vaihesiirtymilla).
static bool scheduler_updateGrowStep(const PlantConfig* plant, SystemState* state,
                                     DeviceConfig* cfg) {
  uint8_t count = 0;
  const GrowStep* steps = growstep_stepsForConfig(plant, cfg, &count);
  if (!steps || cfg->growStepIndex >= count) return false;

  unsigned long now = millis();
  uint32_t elapsedMs = (uint32_t)(now - state->growStepStartMs);
  if (!growstep_autoAdvanceDue(&steps[cfg->growStepIndex], elapsedMs)) return false;

  growstep_advance(state, cfg, now);
  DEBUG_PRINTF("[INFO]  Grow: step timer done, advanced to step %u/%u\n",
               (unsigned)(cfg->growStepIndex + 1), (unsigned)count);
  return true;
}

// ── Main scheduler update ───────────────────────────────────────

// Call from loop(). Orchestrates light, water, height, and grow phase tracking.
// Returns a bitmask of actions taken:
//   bit 0: light state changed
//   bit 1: watering/pump action
//   bit 2: height adjustment started
//   bit 3: grow phase or day counter changed (caller should save config)
uint8_t scheduler_update(const PlantConfig* plant, SensorData* data,
                         SystemState* state, DeviceConfig* cfg) {
  uint8_t actions = 0;

#if ENABLE_LIGHT_RELAY
  // ── Light ──
  bool shouldBeOn = scheduler_shouldLightBeOn(plant, state, cfg);
  if (shouldBeOn != state->lightsOn) {
    power_setLight(shouldBeOn);
    state->lightsOn = shouldBeOn;
    actions |= SCHED_ACTION_LIGHT_CHANGED;
    DEBUG_PRINTF("[INFO]  Scheduler: light %s\n", shouldBeOn ? "ON" : "OFF");
  }
#endif

  // ── Water ──
#if ENABLE_EBB_FLOW && ENABLE_PUMP
  // A running circulation ("kierto") cycle owns the pump — service it (it stops
  // itself after its duration) and defer the flood until it finishes. Circulation
  // only starts when the ebb FSM is IDLE, so a flood is never mid-cycle here and
  // deferring it by at most one short circulation is harmless (floods are far apart).
  if (state->circulateActive) {
    if (scheduler_updateCirculate(data, state, cfg, /*mayStart=*/true)) {
      actions |= SCHED_ACTION_WATER;
    }
  } else {
    // Periodic flooding is skipped if the active grow phase disables it (e.g. the
    // rooting phase has floodIntervalMin=0). BUT a manual force-flood ("Aja tulva
    // nyt" / button) and an already-running cycle must still run — a manual
    // override beats the phase's auto-flood-disable, and a started cycle must
    // finish (FLOOD→SOAK→DRAIN→IDLE). Without this, force-flood silently did
    // nothing in DEVICE_GROWING during a flood-disabled phase.
    const GrowPhaseParams* activePhase = scheduler_getActivePhase(plant, cfg);
    bool floodEnabled = scheduler_floodEnabled(activePhase, cfg->ebbFloodIntervalMin);
    // A fill- OR soak-hold calibration session owns the pump — its states call
    // pump_setContinuous()/pump_stop() and would fight a manual session, so either
    // one blocks the FSM entirely.
    if (scheduler_shouldTickEbbFlow(state->ebbCalibPhase == EBB_CALIB_IDLE &&
                                      !state->soakCalibActive,
                                    floodEnabled,
                                    state->ebbFlowForceFloodRequested,
                                    state->ebbFlowState != EBB_STATE_IDLE) &&
        scheduler_updateEbbFlow(plant, data, state, cfg)) {
      actions |= SCHED_ACTION_WATER;
    }
    // Flood has priority: start a circulation only if the flood tick left the ebb
    // FSM IDLE. scheduler_updateCirculate re-checks ebbIdle internally.
    if (scheduler_updateCirculate(data, state, cfg, /*mayStart=*/true)) {
      actions |= SCHED_ACTION_WATER;
    }
  }
#elif ENABLE_PUMP
  if (scheduler_shouldWater(plant, state, data)) {
    pump_dose(plant->waterMlPerDose);
    state->lastWaterDose = millis();
    state->pumpRunning = true;
    actions |= SCHED_ACTION_WATER;
    DEBUG_PRINTF("[INFO]  Scheduler: watering %d ml\n", plant->waterMlPerDose);
  }
#endif

  // ── Height ──
#if ENABLE_MOTOR
  if (scheduler_shouldRaiseHeight(plant, state, data, cfg->motorCurrentMm)) {
    int newHeight = scheduler_calcNewHeight(cfg->motorCurrentMm, plant->maxHeightMm);
    motor_moveTo(newHeight);
    cfg->motorTargetMm = newHeight;
    state->lastHeightCheck = millis();
    state->motorMoving = true;
    actions |= SCHED_ACTION_HEIGHT;
    DEBUG_PRINTF("[INFO]  Scheduler: raising to %d mm\n", newHeight);
  }
#endif

  // (Ilman lampovaroitus poistettu 18.7.2026 — ks. scheduler_checkTemp-
  //  poiston kommentti yllä. Lehtilampo-valmennus tulee V2:ssa.)

  // ── Grow phase tracking ──
  if (scheduler_updateGrowPhase(plant, state, cfg)) {
    actions |= SCHED_ACTION_GROW_CHANGED;
  }

  // ── Grow step tracking (TIMER-askeleen auto-advance) ──
  if (scheduler_updateGrowStep(plant, state, cfg)) {
    actions |= SCHED_ACTION_GROW_CHANGED;
  }

  return actions;
}

#endif // SCHEDULER_H
