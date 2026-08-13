/*=====================================================================
  config_store.h - Device Configuration Persistence

  Saves/loads DeviceConfig to/from LittleFS as JSON.
  Persists: current plant, LoRa settings, motor position, overrides.

  Requires: ArduinoJson, LittleFS
=====================================================================*/

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include "config_defaults.h"
#include "schema_migration.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

// Forward declaration — config_save() defined below.
bool config_save(const DeviceConfig* cfg);

// ── Load from LittleFS ──────────────────────────────────────────

bool config_load(DeviceConfig* cfg) {
  config_setDefaults(cfg);

  if (!LittleFS.exists(PATH_DEVICE_CONFIG)) {
    DEBUG_WARN(F("Config: no config.json, using defaults"));
    return true;
  }

  File f = LittleFS.open(PATH_DEVICE_CONFIG, "r");
  if (!f) {
    DEBUG_ERROR(F("Config: failed to open config.json"));
    return false;
  }

  // NOTE: this must be MUCH larger than config_save()'s document, not just
  // equal. Deserializing from a File stream is NOT zero-copy: ArduinoJson
  // duplicates every KEY *and* every string value into the document. config_save
  // by contrast stores key literals as flash pointers (no copy), so the save doc
  // can be far smaller than the load doc for the same data. With ~34 keys plus a
  // long wifi SSID/password the parsed tree needs well over 2 KB. If this is too
  // small, deserializeJson() returns NoMemory, config_load() returns false, and
  // boot silently falls back to defaults — i.e. the device "forgets" the selected
  // plant + grow state after every reboot or OTA update. 4 KB gives real headroom.
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    DEBUG_ERRORF("Config: JSON parse error: %s\n", err.c_str());
    return false;
  }

  // ── Schema version gate ────────────────────────────────────────
  uint8_t fileVersion = (uint8_t)(doc["schema_version"] | 0);
  SchemaDecision decision = schema_decide(fileVersion,
                                          SCHEMA_VERSION_CONFIG_MIN,
                                          SCHEMA_VERSION_CONFIG_CURRENT);

  if (decision == SCHEMA_RESET_DEFAULTS) {
    DEBUG_PRINTF("[WARN]  Config: schema v%u unsupported (current v%u), using defaults\n",
                 fileVersion, SCHEMA_VERSION_CONFIG_CURRENT);
    // cfg already at defaults from above; return true so boot continues.
    return true;
  }

  int loraAddress = doc["lora_address"] | (int)LORA_DEVICE_ADDRESS;
  if (loraAddress < 1 || loraAddress > 255) loraAddress = LORA_DEVICE_ADDRESS;
  cfg->loraAddress = (uint8_t)loraAddress;

  int loraNetworkId = doc["lora_network_id"] | (int)LORA_NETWORK_ID;
  if (loraNetworkId < 0 || loraNetworkId > 255) loraNetworkId = LORA_NETWORK_ID;
  cfg->loraNetworkId = (uint8_t)loraNetworkId;

  int loraTarget = doc["lora_target"] | (int)LORA_TARGET_ADDRESS;
  if (loraTarget < 0 || loraTarget > 255) loraTarget = LORA_TARGET_ADDRESS;
  cfg->loraTargetAddress = (uint8_t)loraTarget;

  const char* wifiSsid = doc["wifi_ssid"] | WIFI_STA_SSID_DEFAULT;
  strncpy(cfg->wifiSsid, wifiSsid, sizeof(cfg->wifiSsid) - 1);
  cfg->wifiSsid[sizeof(cfg->wifiSsid) - 1] = '\0';

  const char* wifiPass = doc["wifi_password"] | WIFI_STA_PASSWORD_DEFAULT;
  strncpy(cfg->wifiPassword, wifiPass, sizeof(cfg->wifiPassword) - 1);
  cfg->wifiPassword[sizeof(cfg->wifiPassword) - 1] = '\0';

  cfg->wifiAutoConnect    = doc["wifi_auto_connect"]  | false;

  const char* adminPin = doc["admin_pin"] | "";
  strncpy(cfg->adminPin, adminPin, sizeof(cfg->adminPin) - 1);
  cfg->adminPin[sizeof(cfg->adminPin) - 1] = '\0';

  cfg->lightOnHour        = doc["light_on_hour"]      | 6;
  cfg->motorCurrentMm     = doc["motor_position_mm"]  | 0;
  cfg->motorTargetMm      = doc["motor_target_mm"]    | 50;
  cfg->sensorIntervalMs   = doc["sensor_interval_ms"] | 0;
  cfg->loraReportIntervalMs = doc["lora_report_ms"]   | 0;
  cfg->lightsForceOn      = doc["lights_force_on"]    | false;
  cfg->lightsForceOff     = doc["lights_force_off"]   | false;

  cfg->growPhase          = doc["grow_phase"]         | 0;
  cfg->growElapsedDays    = doc["grow_elapsed_days"]  | 0;
  cfg->growActive         = doc["grow_active"]        | false;
  cfg->growStartMethod    = doc["grow_start_method"]  | 0;
  cfg->growStepIndex      = (uint8_t)(doc["grow_step_index"] | 0);

  cfg->ebbFlowFaultLatched = doc["ebbflow_fault_latched"] | false;
  cfg->ebbFlowFaultCode    = (uint8_t)(doc["ebbflow_fault_code"] | 0);
  cfg->testMode            = doc["test_mode"] | false;
  // Additiivinen schema v5 -kentta: puuttuu v4-tiedostosta -> oletus false
  // (Huolto piilossa). `| false` on koko migraatio, kuten test_mode yllä.
  cfg->devMode             = doc["dev_mode"] | false;

  // Migration guard: a config.json that predates this field belongs to a device
  // that was already set up manually — default to TRUE so an OTA update never
  // throws an operating device back into onboarding. Only a truly fresh device
  // (no config.json at all → config_setDefaults) starts with false. Deliberately
  // no schema bump: the `| true` default is the whole migration, and not bumping
  // keeps OTA rollback safe (older firmware would wipe a higher-versioned file).
  cfg->onboardingComplete  = doc["onboarding_complete"] | true;

  // Same migration logic, mirrored: `| 0` is the whole migration. A file that
  // predates this field loads onboardingComplete=true above, so the banner is
  // never shown and the step bits are not read — an empty mask on an already
  // set-up device is harmless. Only a fresh device (config_setDefaults) starts
  // at 0 with the banner visible, which is exactly the intended state.
  cfg->onboardingSteps     = (uint8_t)(doc["onboarding_steps"] | 0);

  cfg->buttonAction        = (uint8_t)(doc["button_action"] | 0);
  cfg->buttonLongAction    = (uint8_t)(doc["button_long_action"] | 0);
  cfg->growDemoMode        = doc["grow_demo_mode"] | false;  // boot-cleared anyway
  cfg->ebbFloodIntervalMin = (uint16_t)(doc["ebb_flood_interval_min"] | 0);
  cfg->ebbFloodDurationSec = (uint16_t)(doc["ebb_flood_duration_sec"] | 0);
  cfg->ebbSoakDurationSec  = (uint16_t)(doc["ebb_soak_duration_sec"]  | 0);
  cfg->ebbDrainTimeoutSec  = (uint16_t)(doc["ebb_drain_timeout_sec"]  | 0);
  cfg->ebbSoakPwmPct       = (uint8_t)(doc["ebb_soak_pwm_pct"]        | 0);
  cfg->ebbOverflowAutoClear = doc["ebb_overflow_auto_clear"] | false;
  cfg->ebbCirculateEnabled     = doc["ebb_circulate_enabled"] | false;
  cfg->ebbCirculateIntervalMin = (uint16_t)(doc["ebb_circulate_interval_min"] | EBB_CIRCULATE_INTERVAL_MIN);
  cfg->ebbCirculateDurationSec = (uint16_t)(doc["ebb_circulate_duration_sec"] | EBB_CIRCULATE_DURATION_SEC);
  cfg->ebbCirculateDutyPct     = (uint8_t)(doc["ebb_circulate_duty_pct"] | EBB_CIRCULATE_DUTY_PCT);

  const char* plantId = doc["plant_id"] | "basil";
  strncpy(cfg->currentPlantId, plantId, sizeof(cfg->currentPlantId) - 1);
  cfg->currentPlantId[sizeof(cfg->currentPlantId) - 1] = '\0';

  config_clampBounds(cfg);

  if (decision == SCHEMA_NEEDS_MIGRATE) {
    DEBUG_PRINTF("[INFO]  Config: migrated v%u -> v%u, rewriting\n",
                 fileVersion, SCHEMA_VERSION_CONFIG_CURRENT);
    config_save(cfg);
  } else {
    DEBUG_INFO(F("Config: loaded from config.json"));
  }
  return true;
}

// ── Save to LittleFS ────────────────────────────────────────────

bool config_save(const DeviceConfig* cfg) {
  // Keep in sync with config_load()'s capacity (>=). Sized with headroom so new
  // fields don't silently overflow the document and produce truncated JSON.
  StaticJsonDocument<2048> doc;

  doc["schema_version"]     = SCHEMA_VERSION_CONFIG_CURRENT;
  doc["lora_address"]       = cfg->loraAddress;
  doc["lora_network_id"]    = cfg->loraNetworkId;
  doc["lora_target"]        = cfg->loraTargetAddress;
  doc["wifi_ssid"]          = cfg->wifiSsid;
  doc["wifi_password"]      = cfg->wifiPassword;
  doc["wifi_auto_connect"]  = cfg->wifiAutoConnect;
  doc["admin_pin"]          = cfg->adminPin;
  doc["plant_id"]           = cfg->currentPlantId;
  doc["light_on_hour"]      = cfg->lightOnHour;
  doc["motor_position_mm"]  = cfg->motorCurrentMm;
  doc["motor_target_mm"]    = cfg->motorTargetMm;
  doc["sensor_interval_ms"] = cfg->sensorIntervalMs;
  doc["lora_report_ms"]     = cfg->loraReportIntervalMs;
  doc["lights_force_on"]    = cfg->lightsForceOn;
  doc["lights_force_off"]   = cfg->lightsForceOff;
  doc["grow_phase"]         = cfg->growPhase;
  doc["grow_elapsed_days"]  = cfg->growElapsedDays;
  doc["grow_active"]        = cfg->growActive;
  doc["grow_start_method"]  = cfg->growStartMethod;
  doc["grow_step_index"]    = cfg->growStepIndex;
  doc["ebbflow_fault_latched"] = cfg->ebbFlowFaultLatched;
  doc["ebbflow_fault_code"]    = cfg->ebbFlowFaultCode;
  doc["test_mode"]             = cfg->testMode;
  doc["dev_mode"]              = cfg->devMode;
  doc["onboarding_complete"]   = cfg->onboardingComplete;
  doc["onboarding_steps"]      = cfg->onboardingSteps;
  doc["button_action"]          = cfg->buttonAction;
  doc["button_long_action"]     = cfg->buttonLongAction;
  doc["grow_demo_mode"]         = cfg->growDemoMode;
  doc["ebb_flood_interval_min"] = cfg->ebbFloodIntervalMin;
  doc["ebb_flood_duration_sec"] = cfg->ebbFloodDurationSec;
  doc["ebb_soak_duration_sec"]  = cfg->ebbSoakDurationSec;
  doc["ebb_drain_timeout_sec"]  = cfg->ebbDrainTimeoutSec;
  doc["ebb_soak_pwm_pct"]       = cfg->ebbSoakPwmPct;
  doc["ebb_overflow_auto_clear"] = cfg->ebbOverflowAutoClear;
  doc["ebb_circulate_enabled"]      = cfg->ebbCirculateEnabled;
  doc["ebb_circulate_interval_min"] = cfg->ebbCirculateIntervalMin;
  doc["ebb_circulate_duration_sec"] = cfg->ebbCirculateDurationSec;
  doc["ebb_circulate_duty_pct"]     = cfg->ebbCirculateDutyPct;

  File f = LittleFS.open(PATH_DEVICE_CONFIG, "w");
  if (!f) {
    DEBUG_ERROR(F("Config: failed to write config.json"));
    return false;
  }

  serializeJsonPretty(doc, f);
  f.close();
  DEBUG_INFO(F("Config: saved to config.json"));
  return true;
}

#endif // CONFIG_STORE_H
