/*=====================================================================
  config_defaults.h - Device Configuration Defaults

  Extracted from config_store.h so that config_setDefaults() can be
  unit-tested on native without pulling in ArduinoJson or LittleFS.
=====================================================================*/

#ifndef CONFIG_DEFAULTS_H
#define CONFIG_DEFAULTS_H

#include "config.h"
#include "structs.h"

void config_setDefaults(DeviceConfig* cfg) {
  cfg->loraAddress        = LORA_DEVICE_ADDRESS;
  cfg->loraNetworkId      = LORA_NETWORK_ID;
  cfg->loraTargetAddress  = LORA_TARGET_ADDRESS;

  strncpy(cfg->wifiSsid, WIFI_STA_SSID_DEFAULT, sizeof(cfg->wifiSsid) - 1);
  cfg->wifiSsid[sizeof(cfg->wifiSsid) - 1] = '\0';
  strncpy(cfg->wifiPassword, WIFI_STA_PASSWORD_DEFAULT, sizeof(cfg->wifiPassword) - 1);
  cfg->wifiPassword[sizeof(cfg->wifiPassword) - 1] = '\0';
  cfg->wifiAutoConnect    = WIFI_STA_AUTOCONNECT_DEFAULT;
  cfg->adminPin[0]        = '\0';

  strncpy(cfg->currentPlantId, "basil", sizeof(cfg->currentPlantId) - 1);
  cfg->currentPlantId[sizeof(cfg->currentPlantId) - 1] = '\0';
  cfg->lightOnHour        = 6;
  cfg->motorCurrentMm     = 0;
  cfg->motorTargetMm      = 50;
  cfg->sensorIntervalMs   = 0;
  cfg->loraReportIntervalMs = 0;
  cfg->lightsForceOn      = false;
  cfg->lightsForceOff     = false;

  cfg->growPhase          = 0;
  cfg->growElapsedDays    = 0;
  cfg->growActive         = false;
  cfg->growStartMethod    = 0;
  cfg->growStepIndex      = 0;

  cfg->ebbFlowFaultLatched = false;
  cfg->ebbFlowFaultCode    = 0;
  cfg->testMode            = false;
  cfg->devMode             = false;  // Huolto-valilehti piilossa oletuksena
  cfg->onboardingComplete  = false;  // fresh device (no config.json) → onboarding
  cfg->onboardingSteps     = 0;      // nothing chosen yet → banner shows all steps open

  cfg->buttonAction        = BUTTON_ACTION_NONE;
  cfg->buttonLongAction    = BUTTON_LONG_ACTION_BACK;  // long press = step/phase back
  cfg->growDemoMode        = false;  // review-only; boot-cleared (see clearBootOnly)
  cfg->ebbFloodIntervalMin = 0;   // 0 = use grow-phase / config.h default
  cfg->ebbFloodDurationSec = 0;
  cfg->ebbSoakDurationSec  = 0;
  cfg->ebbDrainTimeoutSec  = 0;
  cfg->ebbSoakPwmPct       = 0;   // 0 = legacy soak (pump off, stand-pipe holds)
  cfg->ebbOverflowAutoClear = false; // OFF = manual FLOAT_OVF clear (safe default)
  cfg->ebbCirculateEnabled     = false; // opt-in: enable from portal once duty verified
  cfg->ebbCirculateIntervalMin = EBB_CIRCULATE_INTERVAL_MIN;
  cfg->ebbCirculateDurationSec = EBB_CIRCULATE_DURATION_SEC;
  cfg->ebbCirculateDutyPct     = EBB_CIRCULATE_DUTY_PCT;
}

// Clamp loaded config values to safe bounds.
// Call after config_load() so corrupted JSON cannot leave invalid runtime values.
inline void config_clampBounds(DeviceConfig* cfg) {
  if (!cfg) return;

  if (cfg->lightOnHour < 0 || cfg->lightOnHour > 23) {
    cfg->lightOnHour = 6;
  }

  if (cfg->motorCurrentMm < 0) {
    cfg->motorCurrentMm = 0;
  } else if (cfg->motorCurrentMm > MOTOR_MAX_HEIGHT_MM) {
    cfg->motorCurrentMm = MOTOR_MAX_HEIGHT_MM;
  }

  if (cfg->motorTargetMm < 0) {
    cfg->motorTargetMm = 0;
  } else if (cfg->motorTargetMm > MOTOR_MAX_HEIGHT_MM) {
    cfg->motorTargetMm = MOTOR_MAX_HEIGHT_MM;
  }

  if (cfg->growStartMethod > 2) {
    cfg->growStartMethod = 0;
  }

  if (cfg->growPhase >= GROW_PHASE_MAX) {
    cfg->growPhase = 0;
  }

  // sensorIntervalMs and loraReportIntervalMs: 0 = "use plant defaults".
  // Any positive value < 1000 ms would starve loop() and is treated as
  // corruption — fall back to 0 (defaults).
  if (cfg->sensorIntervalMs > 0 && cfg->sensorIntervalMs < 1000UL) {
    cfg->sensorIntervalMs = 0;
  }
  if (cfg->loraReportIntervalMs > 0 && cfg->loraReportIntervalMs < 1000UL) {
    cfg->loraReportIntervalMs = 0;
  }

  if (cfg->currentPlantId[0] == '\0') {
    strncpy(cfg->currentPlantId, "basil", sizeof(cfg->currentPlantId) - 1);
    cfg->currentPlantId[sizeof(cfg->currentPlantId) - 1] = '\0';
  }

  // Button action: unknown value -> NONE (safe).
  if (cfg->buttonAction > BUTTON_ACTION_MAX) {
    cfg->buttonAction = BUTTON_ACTION_NONE;
  }

  // Long-press action: unknown value -> back-navigation (the new default).
  if (cfg->buttonLongAction > BUTTON_LONG_ACTION_MAX) {
    cfg->buttonLongAction = BUTTON_LONG_ACTION_BACK;
  }

  // Step index: no upper clamp here — the valid bound depends on the active
  // plant's step list, which config_load cannot see. growstep_sequenceActive/
  // buildView treat index >= count as an inert finished sequence, so a stale
  // value can never index out of bounds (grow_step_fsm.h).

  // Ebb&Flow timing overrides: 0 = "use default" (allowed). Clamp positive
  // values to sane upper bounds so corrupt JSON cannot starve or stall a cycle.
  if (cfg->ebbFloodIntervalMin > 1440) cfg->ebbFloodIntervalMin = 1440;  // <= 24 h
  if (cfg->ebbFloodDurationSec > 600)  cfg->ebbFloodDurationSec = 600;   // <= 10 min
  if (cfg->ebbSoakDurationSec  > 3600) cfg->ebbSoakDurationSec  = 3600;  // <= 60 min
  if (cfg->ebbDrainTimeoutSec  > 3600) cfg->ebbDrainTimeoutSec  = 3600;  // <= 60 min

  // Soak hold duty: 0 = pump off (legacy). Any non-zero value is clamped up to
  // the stall floor so corrupt/too-low JSON never commands a stalling duty.
  if (cfg->ebbSoakPwmPct > 100) {
    cfg->ebbSoakPwmPct = 100;
  } else if (cfg->ebbSoakPwmPct > 0 && cfg->ebbSoakPwmPct < PUMP_SOAK_MIN_DUTY_PCT) {
    cfg->ebbSoakPwmPct = PUMP_SOAK_MIN_DUTY_PCT;
  }

  // Circulation: interval <= 24 h; duration must stay under the pump's 5 min
  // absolute cap (PUMP_ABSOLUTE_MAX_ON_MS) — clamp to 290 s with margin. 0 is
  // not meaningful (would never run); leave any positive value, default is set.
  if (cfg->ebbCirculateIntervalMin > 1440) cfg->ebbCirculateIntervalMin = 1440;
  if (cfg->ebbCirculateDurationSec > 290)  cfg->ebbCirculateDurationSec = 290;
  // Avoid pointless sub-10s pump blips from a too-small (or corrupt) duration.
  if (cfg->ebbCirculateDurationSec > 0 && cfg->ebbCirculateDurationSec < 10) {
    cfg->ebbCirculateDurationSec = 10;
  }
  // Circulation duty: same stall-floor clamp as soak (0 = off → won't circulate).
  if (cfg->ebbCirculateDutyPct > 100) {
    cfg->ebbCirculateDutyPct = 100;
  } else if (cfg->ebbCirculateDutyPct > 0 && cfg->ebbCirculateDutyPct < PUMP_SOAK_MIN_DUTY_PCT) {
    cfg->ebbCirculateDutyPct = PUMP_SOAK_MIN_DUTY_PCT;
  }
}

// Clear runtime overrides that must not survive a reboot.
// Call once during boot AFTER config_load() so that a power cycle resets
// manual lights-force flags back to scheduler control. Reasoning: if the
// user toggles lightsForceOn for a test and the device reboots (brown-out,
// watchdog, manual reset), the lamp should not stay forced on indefinitely.
inline void config_clearBootOnlyOverrides(DeviceConfig* cfg) {
  if (!cfg) return;

  if (cfg->lightsForceOn || cfg->lightsForceOff) {
    DEBUG_INFO(F("Config: clearing light force overrides on boot"));
    cfg->lightsForceOn = false;
    cfg->lightsForceOff = false;
  }

  if (cfg->ebbFlowFaultLatched) {
    DEBUG_INFO(F("Config: clearing ebbflow fault latch on boot"));
    cfg->ebbFlowFaultLatched = false;
    cfg->ebbFlowFaultCode    = 0;
  }

  if (cfg->growDemoMode) {
    DEBUG_INFO(F("Config: clearing grow demo mode on boot"));
    cfg->growDemoMode = false;
  }
}

#endif // CONFIG_DEFAULTS_H
