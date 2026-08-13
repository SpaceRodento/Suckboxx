/*=====================================================================
  command_handler.h - Command handling for WiFi portal and LoRa

  Extracted command logic from plantmeister.ino to keep main file focused
  on orchestration and loop scheduling.
=====================================================================*/

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "config.h"
#include "structs.h"
#include "command_validation.h"
#include "reboot_request.h"
#include "ota_manager.h"    // FACTORY_RESET: ala rebootaa vahvistamattoman imagen paalle
#include "factory_reset.h"  // FACTORY_RESET: jaettu tiedostolista (myos bootin ele)
#include "grow_step_fsm.h"  // askel-reset kasvinvaihdossa + GROW_STEP_*-komennot
#include "lora_comm.h"
#include "motor_hal.h"
#include "plant_lookup.h"
#include "power_manager.h"
#include "pump_hal.h"
#include "wifi_portal.h"

#if ENABLE_INPUT_ROUTER
  #include "input_router.h"
#else
  struct RouterContext;
#endif

#if ENABLE_DEVICE_STATE
  #include "device_state.h"
#endif

#if ENABLE_SENSORS
  #include "sensor_manager.h"
  #if ENABLE_SENSOR_HISTORY
    #include "sensor_history.h"
  #endif
#endif

#include <string.h>

// Persist updated device config (implemented in config_store.h).
bool config_save(const DeviceConfig* cfg);

#if ENABLE_GUIDED_GROWING
static inline int command_findPhaseIndexByType(const PlantConfig* plant, GrowPhaseType type) {
  if (!plant) return -1;
  for (uint8_t i = 0; i < plant->phaseCount; i++) {
    if (plant->phases[i].type == type) return i;
  }
  return -1;
}

static inline uint8_t command_pickStartPhase(const PlantConfig* plant, uint8_t startMethod) {
  if (!plant || plant->phaseCount == 0) return 0;

  GrowPhaseType preferredType = GROW_PHASE_ROOTING;
  switch (startMethod) {
    case 1: preferredType = GROW_PHASE_SEEDLING; break;
    case 2: preferredType = GROW_PHASE_VEGETATIVE; break;
    default: preferredType = GROW_PHASE_ROOTING; break;
  }

  int idx = command_findPhaseIndexByType(plant, preferredType);
  if (idx >= 0) return (uint8_t)idx;
  return 0;
}
#endif

#if ENABLE_INPUT_ROUTER
static inline bool command_tryRouteIntent(Intent intent, uint8_t value, const RouterContext* ctx) {
  if (!ctx) return false;
  IntentResult result = input_routeIntent(intent, value, INTENT_SOURCE_UNKNOWN, ctx);
  if (!result.accepted && result.rejectReason) {
    DEBUG_PRINTF("[WARN]  Intent %u rejected: %s\n",
                 (unsigned)intent, result.rejectReason);
  }
  return true;
}
#endif

static inline void command_handle(const char* cmd, const char* value,
                                  SensorData* sensors,
                                  SystemState* state,
                                  DeviceConfig* config,
                                  PlantConfig** activePlant,
                                  const RouterContext* ctx) {
  if (!cmd || !value || !sensors || !state || !config || !activePlant) return;
  PlantConfig* activePlantPtr = *activePlant;

  DEBUG_PRINTF("[INFO]  Command: %s = %s\n", cmd, value);

#if ENABLE_MOTOR
  auto applyMotorTestProfile = [](uint8_t profileId) {
    switch (profileId) {
      case 1:
        motor_setSpeed(1500.0f);
        motor_setAcceleration(10000.0f);
        motor_setDeceleration(4500.0f);
        motor_setFinalDeceleration(3600.0f);
        motor_setFinalSpeed(1400.0f);
        break;
      case 2:
        motor_setSpeed(1500.0f);
        motor_setAcceleration(11000.0f);
        motor_setDeceleration(5000.0f);
        motor_setFinalDeceleration(3300.0f);
        motor_setFinalSpeed(1350.0f);
        break;
      case 3:
        motor_setSpeed(1500.0f);
        motor_setAcceleration(12000.0f);
        motor_setDeceleration(5200.0f);
        motor_setFinalDeceleration(3500.0f);
        motor_setFinalSpeed(1450.0f);
        break;
      case 4:
        motor_setSpeed(1500.0f);
        motor_setAcceleration(9500.0f);
        motor_setDeceleration(4200.0f);
        motor_setFinalDeceleration(3800.0f);
        motor_setFinalSpeed(1300.0f);
        break;
      case 5:
        motor_setSpeed(1500.0f);
        motor_setAcceleration(11000.0f);
        motor_setDeceleration(5200.0f);
        motor_setFinalDeceleration(3600.0f);
        motor_setFinalSpeed(1450.0f);
        break;
      case 6:
        motor_setSpeed(1500.0f);
        motor_setAcceleration(10000.0f);
        motor_setDeceleration(4800.0f);
        motor_setFinalDeceleration(3400.0f);
        motor_setFinalSpeed(1400.0f);
        break;
      default:
        return false;
    }
    return true;
  };
#endif

  if (strcmp(cmd, "light_toggle") == 0 || strcmp(cmd, "LIGHT") == 0) {
    // Toggle or set light
    if (value[0] == '1') {
      config->lightsForceOn = true;
      config->lightsForceOff = false;
    } else if (value[0] == '0') {
      config->lightsForceOn = false;
      config->lightsForceOff = true;
    } else {
      // Toggle
      if (state->lightsOn) {
        config->lightsForceOn = false;
        config->lightsForceOff = true;
      } else {
        config->lightsForceOn = true;
        config->lightsForceOff = false;
      }
    }
  }

  else if (strcmp(cmd, "water") == 0 || strcmp(cmd, "WATER") == 0) {
#if ENABLE_PUMP
    int ml = 0;
    if (cmd_isEmpty(value)) {
      if (activePlantPtr) ml = activePlantPtr->waterMlPerDose;
      if (ml <= 0) ml = 20;
    } else {
      if (!cmd_parseIntStrict(value, &ml) || ml <= 0 || ml > 1000) {
        DEBUG_WARN(F("WATER: invalid value (must be 1..1000 or empty)"));
        return;
      }
    }
    pump_dose(ml);
    state->pumpRunning = true;
    state->lastWaterDose = millis();
#endif
  }

  else if (strcmp(cmd, "water_stop") == 0 || strcmp(cmd, "WATER_STOP") == 0) {
#if ENABLE_PUMP
    pump_stop();
    state->pumpRunning = false;
    DEBUG_INFO(F("Pump: stopped by user"));
#endif
  }

  else if (strcmp(cmd, "motor_up") == 0) {
#if ENABLE_MOTOR
    motor_moveBy(10);
    config->motorTargetMm = motor_getTargetMm();
#endif
  }

  else if (strcmp(cmd, "motor_down") == 0) {
#if ENABLE_MOTOR
    motor_moveBy(-10);
    config->motorTargetMm = motor_getTargetMm();
#endif
  }

  else if (strcmp(cmd, "motor_stop") == 0 || strcmp(cmd, "MOTOR_STOP") == 0) {
#if ENABLE_MOTOR
    // Soft stop: aja moottori nykyiseen sijaintiinsa, jolloin AccelStepper
    // jarruttaa hellästi alas eikä menetä step-laskuria.
    int pos = motor_getPositionMm();
    motor_moveTo(pos);
    config->motorTargetMm = pos;
    DEBUG_PRINTF("[INFO]  Motor: stop requested at %d mm\n", pos);
#endif
  }

  else if (strcmp(cmd, "HEIGHT") == 0) {
#if ENABLE_MOTOR
    int mm = 0;
    if (!cmd_parseIntStrict(value, &mm) || mm < 0 || mm > MOTOR_MAX_HEIGHT_MM) {
      DEBUG_WARN(F("HEIGHT: invalid value"));
      return;
    }
    motor_moveTo(mm);
    config->motorTargetMm = mm;
#endif
  }

  else if (strcmp(cmd, "MOTOR_SPEED") == 0) {
#if ENABLE_MOTOR
    float spd = 0.0f;
    if (cmd_parseFloatStrict(value, &spd) && spd > 0 && spd <= 20000.0f) {
      motor_setSpeed(spd);
      // Jos moottori on jo liikkeessä, AccelStepper laskee profile:n vain
      // moveTo()-hetkellä. Uudelleenajoitus samaan targetiin saa uuden
      // nopeuden voimaan myös käynnissä olevassa liikkeessä.
      if (motor_isMoving()) {
        int target = motor_getTargetMm();
        motor_moveTo(target);
      }
      DEBUG_PRINTF("[INFO]  MOTOR_SPEED OK: %.0f half-steps/s (acc=%.0f dec=%.0f fdec=%.0f fend=%.0f)\n",
                   motor_getSpeed(), motor_getAcceleration(), motor_getDeceleration(),
                   motor_getFinalDeceleration(), motor_getFinalSpeed());
    } else {
      DEBUG_WARN(F("MOTOR_SPEED: invalid value (must be >0 and <=20000)"));
    }
#endif
  }

  else if (strcmp(cmd, "MOTOR_ACC") == 0) {
#if ENABLE_MOTOR
    float acc = 0.0f;
    if (cmd_parseFloatStrict(value, &acc) && acc > 0 && acc <= 20000.0f) {
      motor_setAcceleration(acc);
      DEBUG_PRINTF("[INFO]  MOTOR_ACC OK: %.0f half-steps/s^2 (dec=%.0f fdec=%.0f speed=%.0f)\n",
                   motor_getAcceleration(), motor_getDeceleration(),
                   motor_getFinalDeceleration(), motor_getSpeed());
    } else {
      DEBUG_WARN(F("MOTOR_ACC: invalid value (must be >0 and <=20000)"));
    }
#endif
  }

  else if (strcmp(cmd, "MOTOR_DEC") == 0) {
#if ENABLE_MOTOR
    float dec = 0.0f;
    if (cmd_parseFloatStrict(value, &dec) && dec > 0 && dec <= 20000.0f) {
      motor_setDeceleration(dec);
      DEBUG_PRINTF("[INFO]  MOTOR_DEC OK: %.0f half-steps/s^2 (fdec=%.0f accel=%.0f speed=%.0f)\n",
                   motor_getDeceleration(), motor_getFinalDeceleration(),
                   motor_getAcceleration(), motor_getSpeed());
    } else {
      DEBUG_WARN(F("MOTOR_DEC: invalid value (must be >0 and <=20000)"));
    }
#endif
  }

  else if (strcmp(cmd, "MOTOR_FDEC") == 0) {
#if ENABLE_MOTOR
    float fdec = 0.0f;
    if (cmd_parseFloatStrict(value, &fdec) && fdec > 0 && fdec <= 20000.0f) {
      motor_setFinalDeceleration(fdec);
      DEBUG_PRINTF("[INFO]  MOTOR_FDEC OK: %.0f half-steps/s^2 (dec=%.0f accel=%.0f)\n",
                   motor_getFinalDeceleration(), motor_getDeceleration(), motor_getAcceleration());
    } else {
      DEBUG_WARN(F("MOTOR_FDEC: invalid value (must be >0 and <=20000)"));
    }
#endif
  }

  else if (strcmp(cmd, "MOTOR_FEND") == 0) {
#if ENABLE_MOTOR
    float fend = 0.0f;
    if (cmd_parseFloatStrict(value, &fend) && fend > 0 && fend <= 20000.0f) {
      motor_setFinalSpeed(fend);
      DEBUG_PRINTF("[INFO]  MOTOR_FEND OK: %.0f half-steps/s (speed=%.0f)\n",
                   motor_getFinalSpeed(), motor_getSpeed());
    } else {
      DEBUG_WARN(F("MOTOR_FEND: invalid value (must be >0 and <=20000)"));
    }
#endif
  }

  else if (strcmp(cmd, "MOTOR_TEST") == 0) {
#if ENABLE_MOTOR
    int profileInt = 0;
    if (cmd_parseIntStrict(value, &profileInt) && profileInt >= 1 && profileInt <= 6 &&
        applyMotorTestProfile((uint8_t)profileInt)) {
      DEBUG_PRINTF("[INFO]  MOTOR_TEST %u OK: speed=%.0f acc=%.0f dec=%.0f fdec=%.0f fend=%.0f\n",
                   (uint8_t)profileInt, motor_getSpeed(), motor_getAcceleration(), motor_getDeceleration(),
                   motor_getFinalDeceleration(), motor_getFinalSpeed());
    } else {
      DEBUG_WARN(F("MOTOR_TEST: invalid profile (use 1-6)"));
    }
#endif
  }

  else if (strcmp(cmd, "calibrate") == 0) {
#if ENABLE_MOTOR
    motor_setPositionMm(0);
    config->motorCurrentMm = 0;
    config->motorTargetMm = 0;
#endif
  }

  else if (strcmp(cmd, "PLANT") == 0) {
    PlantConfig* p = plants_getById(value);
    if (p) {
      activePlantPtr = p;
      *activePlant = p;
      strncpy(config->currentPlantId, value, sizeof(config->currentPlantId) - 1);
      config->currentPlantId[sizeof(config->currentPlantId) - 1] = '\0';
      // Kasvi vaihtui -> askellista sen alla vaihtui (reset-paikka 4/5,
      // grow_step_fsm.h). Ilman tata indeksi voisi osoittaa uuden kasvin
      // lyhyemman listan ulkopuolelle tai keskelle vaaria ohjeita.
      grow_resetStepProgress(state, config, millis());
      config_save(config);
      DEBUG_PRINTF("[INFO]  Plant changed to: %s\n", p->name);
    }
  }

  else if (strcmp(cmd, "AP") == 0) {
#if ENABLE_WIFI_PORTAL
#if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_REACTIVATE_AP, 0, ctx)) return;
#endif
    portal_reactivate();
#endif
  }

  else if (strcmp(cmd, "air_pump") == 0 || strcmp(cmd, "AIRPUMP") == 0) {
#if ENABLE_AIR_PUMP
    if (!(value[0] == '0' || value[0] == '1') || value[1] != '\0') {
      DEBUG_WARN(F("AIRPUMP: invalid value (use 0 or 1)"));
      return;
    }
    bool on = (value[0] == '1');
    power_setAirPump(on);
    state->airPumpOn = on;
#else
    DEBUG_WARN(F("Air pump command ignored: feature disabled"));
#endif
  }

  else if (strcmp(cmd, "EBB_ACK") == 0 || strcmp(cmd, "ebb_ack") == 0) {
#if ENABLE_EBB_FLOW
#if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_ACK_FAULT, 0, ctx)) return;
#endif
    if (!state->ebbFlowFaultLatched) {
      DEBUG_INFO(F("EbbFlow: no latched fault"));
      return;
    }
    state->ebbFlowAckRequested = true;
    DEBUG_INFO(F("EbbFlow: ACK requested"));
#else
    DEBUG_WARN(F("EBB_ACK ignored: ENABLE_EBB_FLOW=false"));
#endif
  }

  else if (strcmp(cmd, "EBB_FLOOD") == 0 || strcmp(cmd, "ebb_flood") == 0) {
#if ENABLE_EBB_FLOW
#if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_EBB_FLOOD_NOW, 0, ctx)) return;
#endif
    state->ebbFlowForceFloodRequested = true;
    DEBUG_INFO(F("EbbFlow: force flood requested"));
#else
    DEBUG_WARN(F("EBB_FLOOD ignored: ENABLE_EBB_FLOW=false"));
#endif
  }

  else if (strcmp(cmd, "GROW_START") == 0 || strcmp(cmd, "grow_start") == 0) {
#if ENABLE_GUIDED_GROWING
    if (!activePlantPtr || activePlantPtr->phaseCount == 0) {
      DEBUG_WARN(F("Grow: active plant has no phase program"));
      return;
    }

    int startMethodInt = 0;
    if (!cmd_isEmpty(value)) {
      if (!cmd_parseIntStrict(value, &startMethodInt) || startMethodInt < 0 || startMethodInt > 2) {
        DEBUG_WARN(F("Grow: start method must be 0..2"));
        return;
      }
    }
    uint8_t startMethod = (uint8_t)startMethodInt;

  #if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_START_GROWING, startMethod, ctx)) return;
  #endif

    config->growPhase       = command_pickStartPhase(activePlantPtr, startMethod);
    config->growElapsedDays = 0;
    config->growActive      = true;
    config->growStartMethod = startMethod;
    state->growDayStartMs          = millis();
    state->growPhasePendingAdvance = false;
    grow_resetStepProgress(state, config, millis());  // ei-router-fallback: sama reset
    config_save(config);
    DEBUG_PRINTF("[INFO]  Grow: started phase %d (%s), method=%d\n",
                 config->growPhase,
                 activePlantPtr->phases[config->growPhase].label,
                 config->growStartMethod);
#else
    DEBUG_WARN(F("Grow start ignored: ENABLE_GUIDED_GROWING=false"));
#endif
  }

  else if (strcmp(cmd, "GROW_NEXT") == 0 || strcmp(cmd, "grow_next") == 0) {
#if ENABLE_GUIDED_GROWING
#if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_NEXT_PHASE, 0, ctx)) return;
#endif
    if (!config->growActive || !activePlantPtr || activePlantPtr->phaseCount == 0) {
      DEBUG_WARN(F("Grow: no active grow cycle"));
      return;
    }
    uint8_t next = config->growPhase + 1;
    if (next >= activePlantPtr->phaseCount) {
      DEBUG_INFO(F("Grow: already at last phase"));
      return;
    }
    config->growPhase       = next;
    config->growElapsedDays = 0;
    state->growPhasePendingAdvance = false;
    state->growDayStartMs = millis();
    grow_resetStepProgress(state, config, millis());  // ei-router-fallback: sama reset
    config_save(config);
    DEBUG_PRINTF("[INFO]  Grow: advanced to phase %d (%s)\n",
                 config->growPhase, activePlantPtr->phases[config->growPhase].label);
#else
    DEBUG_WARN(F("Grow next ignored: ENABLE_GUIDED_GROWING=false"));
#endif
  }

  else if (strcmp(cmd, "GROW_DELAY") == 0 || strcmp(cmd, "grow_delay") == 0) {
#if ENABLE_GUIDED_GROWING
#if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_DELAY_PHASE, 0, ctx)) return;
#endif
    if (!state->growPhasePendingAdvance) {
      DEBUG_INFO(F("Grow: no pending phase transition to delay"));
      return;
    }
    state->growAdvanceProposedMs = millis();
    DEBUG_INFO(F("Grow: phase transition postponed by user"));
#else
    DEBUG_WARN(F("Grow delay ignored: ENABLE_GUIDED_GROWING=false"));
#endif
  }

  else if (strcmp(cmd, "GROW_STOP") == 0 || strcmp(cmd, "grow_stop") == 0) {
#if ENABLE_GUIDED_GROWING
#if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_STOP_GROWING, 0, ctx)) return;
#endif
    config->growActive             = false;
    state->growPhasePendingAdvance = false;
    config_save(config);
    DEBUG_INFO(F("Grow: stopped"));
#else
    DEBUG_WARN(F("Grow stop ignored: ENABLE_GUIDED_GROWING=false"));
#endif
  }

  // ── Askelkomennot (WIZARD: grow-steps) — reititys intent-jarjestelman
  // kautta, jotta validointi (tila, TIMER-askeleen kuittauskielto, rajat)
  // asuu yhdessa paikassa (input_router.h). Ilman routeria naita ei ole.
  else if (strcmp(cmd, "GROW_STEP_ACK") == 0 || strcmp(cmd, "grow_step_ack") == 0) {
#if ENABLE_GUIDED_GROWING && ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_ACK_STEP, 0, ctx)) return;
    DEBUG_WARN(F("GROW_STEP_ACK: no router context"));
#else
    DEBUG_WARN(F("GROW_STEP_ACK ignored: guided growing / router disabled"));
#endif
  }

  else if (strcmp(cmd, "GROW_STEP_SKIP") == 0 || strcmp(cmd, "grow_step_skip") == 0) {
#if ENABLE_GUIDED_GROWING && ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_SKIP_STEP, 0, ctx)) return;
    DEBUG_WARN(F("GROW_STEP_SKIP: no router context"));
#else
    DEBUG_WARN(F("GROW_STEP_SKIP ignored: guided growing / router disabled"));
#endif
  }

  else if (strcmp(cmd, "GROW_STEP_BACK") == 0 || strcmp(cmd, "grow_step_back") == 0) {
#if ENABLE_GUIDED_GROWING && ENABLE_INPUT_ROUTER
    // Peruuta askel/vaihe taaksepain — sama kuin napin pitka painallus (K-C
    // 22.7.2026). Etatestaus ilman fyysista nappia: `pm.py cmd GROW_STEP_BACK`.
    if (command_tryRouteIntent(INTENT_PREV_STEP, 0, ctx)) return;
    DEBUG_WARN(F("GROW_STEP_BACK: no router context"));
#else
    DEBUG_WARN(F("GROW_STEP_BACK ignored: guided growing / router disabled"));
#endif
  }

  else if (strcmp(cmd, "GROW_STEP_SET") == 0 || strcmp(cmd, "grow_step_set") == 0) {
#if ENABLE_GUIDED_GROWING && ENABLE_INPUT_ROUTER
    // Turvaverkko harhapainallukselle (kayttajan valinta 16.7.2026): palauta
    // askel puhelimella, esim. `pm.py cmd GROW_STEP_SET --value 2`. Arvo on
    // 0-pohjainen askelindeksi; rajavalidointi listaa vasten on routerissa.
    int stepInt = 0;
    if (!cmd_parseIntStrict(value, &stepInt) || stepInt < 0 || stepInt > 255) {
      DEBUG_WARN(F("GROW_STEP_SET: value must be 0..255 (step index)"));
      return;
    }
    if (command_tryRouteIntent(INTENT_SET_STEP, (uint8_t)stepInt, ctx)) return;
    DEBUG_WARN(F("GROW_STEP_SET: no router context"));
#else
    DEBUG_WARN(F("GROW_STEP_SET ignored: guided growing / router disabled"));
#endif
  }

  else if (strcmp(cmd, "GROW_DEMO") == 0 || strcmp(cmd, "grow_demo") == 0) {
#if ENABLE_GUIDED_GROWING
    // Katselmointitila: kun paalla, laitteen nappi vie koko opastusputken lapi
    // ilman kelloa (button_intent_map.h demo-haara). Boot-clear estaa jaamisen
    // paalle oikeaan kasvatukseen (config_defaults.h). value 1 = paalle, 0 pois.
    bool on = (value[0] == '1');
    config->growDemoMode = on;
    config_save(config);
    DEBUG_PRINTF("[INFO]  Grow demo mode: %s (nappi advanssaa askeleet+vaiheet)\n",
                 on ? "ON" : "OFF");
#else
    DEBUG_WARN(F("GROW_DEMO ignored: ENABLE_GUIDED_GROWING=false"));
#endif
  }

  // ── Huolto/katselmointi: aseta vaihe ja vaihepaiva suoraan (WIZARD: grow-
  // steps -perheen jatke). Reititys intentin kautta (input_router.h) — sama
  // validointi (GROWING-tila, rajat) yhdessa paikassa. Kayttö: portaalin
  // Huolto-valilehti, tai `pm.py cmd GROW_PHASE_SET --value 2`. Arvo on
  // 0-pohjainen vaiheindeksi; rajavalidointi kasvin phaseCountia vasten on
  // routerissa (lista riippuu aktiivisesta kasvista).
  else if (strcmp(cmd, "GROW_PHASE_SET") == 0 || strcmp(cmd, "grow_phase_set") == 0) {
#if ENABLE_GUIDED_GROWING && ENABLE_INPUT_ROUTER
    int phaseInt = 0;
    if (!cmd_parseIntStrict(value, &phaseInt) || phaseInt < 0 || phaseInt > 255) {
      DEBUG_WARN(F("GROW_PHASE_SET: value must be 0..255 (phase index)"));
      return;
    }
    if (command_tryRouteIntent(INTENT_SET_PHASE, (uint8_t)phaseInt, ctx)) return;
    DEBUG_WARN(F("GROW_PHASE_SET: no router context"));
#else
    DEBUG_WARN(F("GROW_PHASE_SET ignored: guided growing / router disabled"));
#endif
  }

  else if (strcmp(cmd, "GROW_DAY_SET") == 0 || strcmp(cmd, "grow_day_set") == 0) {
#if ENABLE_GUIDED_GROWING && ENABLE_INPUT_ROUTER
    int dayInt = 0;
    if (!cmd_parseIntStrict(value, &dayInt) || dayInt < 0 || dayInt > 255) {
      DEBUG_WARN(F("GROW_DAY_SET: value must be 0..255 (phase day)"));
      return;
    }
    if (command_tryRouteIntent(INTENT_SET_DAY, (uint8_t)dayInt, ctx)) return;
    DEBUG_WARN(F("GROW_DAY_SET: no router context"));
#else
    DEBUG_WARN(F("GROW_DAY_SET ignored: guided growing / router disabled"));
#endif
  }

  else if (strcmp(cmd, "sensor_read") == 0 || strcmp(cmd, "SENSOR_READ") == 0) {
#if ENABLE_SENSORS
    if (state->sensorsReady) {
      sensors_read(sensors);
      state->lastSensorRead = millis();
      sensors->timestamp = millis();
      #if ENABLE_SENSOR_HISTORY
        history_addReading(sensors);
      #endif
      DEBUG_INFO(F("Sensors: forced read"));
    }
#endif
  }

  else if (strcmp(cmd, "CLEAR_FAULT") == 0 || strcmp(cmd, "clear_fault") == 0) {
    // FAULT-RECOVERY: operaattori voi kuitata raudasta puuttuvasta anturista
    // johtuvan self-test-faultin ilman fyysista RST-painallusta.
#if ENABLE_DEVICE_STATE
    DEBUG_INFO(F("CLEAR_FAULT: clearing all faults"));
    device_clearAllFaults();
#else
    DEBUG_WARN(F("CLEAR_FAULT: device state disabled, noop"));
#endif
  }
  else if (strcmp(cmd, "MAINTENANCE_ON") == 0 || strcmp(cmd, "maintenance_on") == 0) {
    // Remote entry matters as much as the button: the operator may be at the
    // portal on a phone deciding to go service the device.
#if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_MAINTENANCE_ENTER, 0, ctx)) return;
#endif
    DEBUG_WARN(F("MAINTENANCE_ON ignored: ENABLE_INPUT_ROUTER=false"));
  }
  else if (strcmp(cmd, "MAINTENANCE_OFF") == 0 || strcmp(cmd, "maintenance_off") == 0) {
#if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_MAINTENANCE_EXIT, 0, ctx)) return;
#endif
    DEBUG_WARN(F("MAINTENANCE_OFF ignored: ENABLE_INPUT_ROUTER=false"));
  }
  else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "REBOOT") == 0) {
    DEBUG_INFO(F("Rebooting..."));
    config_save(config);
#if ENABLE_INPUT_ROUTER
    if (command_tryRouteIntent(INTENT_REBOOT, 0, ctx)) return;
#endif
    reboot_request_schedule(100);
  }

  // Palauta laite kayttoonottotilaan (verkkoreitti).
  //
  // Mita poistetaan ja miksi kalibrointi jaa: factory_reset.h. Sama nollaus on
  // saatavilla myos ilman verkkoa nappia pohjassa pitamalla kaynnistyksessa
  // (factory_reset_gesture.h) — molemmat kutsuvat factory_reset_wipe():a, joten
  // tiedostolista ei paase eriytymaan.
  else if (strcmp(cmd, "FACTORY_RESET") == 0 || strcmp(cmd, "factory_reset") == 0) {
    // Vahvistus pakollinen: tata ei saa laukaista lipsahtaneella
    // `pm.py cmd FACTORY_RESET`-nappaimenpainalluksella.
    if (strcmp(value, "CONFIRM") != 0) {
      DEBUG_WARN(F("Factory reset: vaatii value=CONFIRM — ei tehty"));
      return;
    }

#if ENABLE_OTA
    // Ala tee hiljaista firmware-downgradea. Reset rebootaa, ja vahvistamaton
    // OTA-image rullautuu rebootissa takaisin edelliseen versioon — jolloin
    // laite paitsi nollaantuu myos palaa vanhaan firmwareen, ja regeneroi
    // oletustiedostot SEN sisaanrakennetuista arvoista. Kayttaja ei pyytanyt
    // sita eika nae sita mistaan. Odota vahvistus (ota_manager.h).
    if (ota_isPendingVerify()) {
      DEBUG_PRINTF("[WARN]  Factory reset: uusi firmware viela vahvistamatta — "
                   "reboot rullaisi sen takaisin. Odota %u s ja yrita uudelleen.\n",
                   (unsigned)ota_secondsUntilConfirm());
      return;
    }
#endif

#if ENABLE_PUMP
    pump_stop();   // ei jateta pumppua kayntiin rebootia odottamaan
#endif

    const FactoryResetResult r = factory_reset_wipe();
    DEBUG_PRINTF("[INFO]  Factory reset: config %s, growclock %s, plants %s, "
                 "kalibrointi SAILYTETTY — reboot\n",
                 r.configRemoved    ? "poistettu" : "ei ollut",
                 r.growClockRemoved ? "poistettu" : "ei ollut",
                 r.plantsRemoved    ? "poistettu" : "ei ollut");

    // EI config_save():ta — se kirjoittaisi juuri poistetun tiedoston takaisin.
    // Reboot on ainoa rehellinen tapa saada puhdas tila: puolittain nollattu
    // RAM (growActive true, kasvi ladattuna) olisi arvaamaton. Odota hetki
    // jotta HTTP-vastaus ehtii lahtea. reboot_request_pending() sulkee
    // tiladispatchin heti, joten scheduler ei ehdi kirjoittaa mitaan takaisin.
    reboot_request_schedule(500);
  }
}

#endif // COMMAND_HANDLER_H
