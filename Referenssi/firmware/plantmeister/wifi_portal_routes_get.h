/*=====================================================================
  wifi_portal_routes_get.h - GET route handlers for WiFi portal

  Included by wifi_portal.h after shared portal globals and helpers.
=====================================================================*/

#ifndef WIFI_PORTAL_ROUTES_GET_H
#define WIFI_PORTAL_ROUTES_GET_H

// watchdog.h is included after wifi_portal.h in the .ino, so pull it in
// explicitly for the boot_cause/crash_reboots status fields (guarded, no dup).
#include "watchdog.h"
#include "scheduler_ebb_helpers.h"
// Toimintopalkin resolveri (/api/status "primary_action") + askel-view sen
// syotteeksi. grow_step_fsm.h eksplisiittisesti: api_state.h includaa sen
// vasta taman tiedoston jalkeen .ino-TU:ssa.
#include "portal_primary_action.h"
#if ENABLE_GUIDED_GROWING
#include "grow_step_fsm.h"
#endif

static void portal_registerPlantGetRoutes() {
  // GET /api/plants — list all plants
  g_webServer.on("/api/plants", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    StaticJsonDocument<1024> doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < plants_getCount(); i++) {
      PlantConfig* p = plants_getByIndex(i);
      if (!p) continue;
      JsonObject obj = arr.createNestedObject();
      obj["id"]   = p->id;
      obj["name"] = p->name;
    }

    char buf[1024];
    serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });

  // GET /api/plant?id=basil — get single plant params
  g_webServer.on("/api/plant", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    if (!req->hasParam("id")) {
      api_sendEnvelope(req, 400, false, false, "missing id", "id");
      return;
    }

    const char* id = req->getParam("id")->value().c_str();
    PlantConfig* p = plants_getById(id);
    if (!p) {
      api_sendEnvelope(req, 404, false, false, "plant not found", "id");
      return;
    }

    StaticJsonDocument<1536> doc;
    doc["id"]                   = p->id;
    doc["name"]                 = p->name;
    doc["max_height_mm"]        = p->maxHeightMm;
    doc["light_hours"]          = p->lightHours;
    doc["water_ml_per_dose"]    = p->waterMlPerDose;
    doc["water_interval_hours"] = p->waterIntervalHours;
    // PE-tavoitteet elavat per vaihe (phases[]), eivat litteassa tasossa —
    // tds_target/temp_min/temp_max poistettiin 18.7.2026 (pe-ohjausmalli.md).

    JsonArray phases = doc.createNestedArray("phases");
    for (uint8_t i = 0; i < p->phaseCount; i++) {
      const GrowPhaseParams& ph = p->phases[i];
      JsonObject pobj = phases.createNestedObject();
      pobj["index"]             = i;
      pobj["label"]             = ph.label;
      pobj["type"]              = portal_growPhaseTypeName(ph.type);
      pobj["duration_days"]     = ph.durationDays;
      pobj["light_hours"]       = ph.lightHours;
      pobj["flood_interval_min"]  = ph.floodIntervalMin;
      pobj["flood_duration_sec"]  = ph.floodDurationSec;
      // PE-tavoitteet (docs/kehitys/pe-ohjausmalli.md). V1: naytetaan
      // portaalissa/e-inkissa tavoite vs. nykyarvo; laite ei viela aja niita.
      pobj["dli_target_mol"]    = ph.dliTargetMol;
      pobj["ppfd_target_umol"]  = ph.ppfdTargetUmol;
      pobj["vpd_min_kpa"]       = ph.vpdMinKpa;
      pobj["vpd_max_kpa"]       = ph.vpdMaxKpa;
      pobj["co2_target_ppm"]    = ph.co2TargetPpm;
      pobj["leaf_temp_max_c"]   = ph.leafTempMaxC;
    }

    char buf[1536];
    serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });
}

static void portal_registerConfigGetRoute() {
  // GET /api/config — current device config
  g_webServer.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    if (!g_portalConfig) {
      api_sendEnvelope(req, 500, false, false, "no config");
      return;
    }

    StaticJsonDocument<768> doc;
    char maskedWifi[40];
    char maskedPin[40];

    // LoRa
    doc["lora_address"]      = g_portalConfig->loraAddress;
    doc["lora_network_id"]   = g_portalConfig->loraNetworkId;
    doc["lora_target"]       = g_portalConfig->loraTargetAddress;
    // WiFi STA
    doc["wifi_ssid"]         = g_portalConfig->wifiSsid;
    api_maskSecret(g_portalConfig->wifiPassword, maskedWifi, sizeof(maskedWifi));
    doc["wifi_password"]     = maskedWifi;
    doc["wifi_password_set"] = (strlen(g_portalConfig->wifiPassword) > 0);
    doc["wifi_auto_connect"] = g_portalConfig->wifiAutoConnect;
    api_maskSecret(g_portalConfig->adminPin, maskedPin, sizeof(maskedPin));
    doc["admin_pin"]         = maskedPin;
    doc["admin_pin_set"]     = (strlen(g_portalConfig->adminPin) > 0);
    // Display / plant
    doc["light_on_hour"]     = g_portalConfig->lightOnHour;
    doc["dev_mode"]          = g_portalConfig->devMode;  // Huolto-valilehden nakyvyys
    doc["plant_id"]          = g_portalConfig->currentPlantId;
    // Button + Ebb&Flow timing overrides (0 = use default)
    doc["button_action"]          = g_portalConfig->buttonAction;
    doc["button_long_action"]     = g_portalConfig->buttonLongAction;
    doc["grow_start_method"]      = g_portalConfig->growStartMethod;
    doc["ebb_flood_interval_min"] = g_portalConfig->ebbFloodIntervalMin;
    doc["ebb_flood_duration_sec"] = g_portalConfig->ebbFloodDurationSec;
    doc["ebb_soak_duration_sec"]  = g_portalConfig->ebbSoakDurationSec;
    doc["ebb_drain_timeout_sec"]  = g_portalConfig->ebbDrainTimeoutSec;
    doc["ebb_soak_pwm_pct"]       = g_portalConfig->ebbSoakPwmPct;
    doc["ebb_overflow_auto_clear"] = g_portalConfig->ebbOverflowAutoClear;
    doc["ebb_circulate_enabled"]      = g_portalConfig->ebbCirculateEnabled;
    doc["ebb_circulate_interval_min"] = g_portalConfig->ebbCirculateIntervalMin;
    doc["ebb_circulate_duration_sec"] = g_portalConfig->ebbCirculateDurationSec;
    doc["ebb_circulate_duty_pct"]     = g_portalConfig->ebbCirculateDutyPct;
    // WiFi status
    int staStatus = WiFi.status();
    doc["wifi_sta_connected"] = (staStatus == WL_CONNECTED);
    if (staStatus == WL_CONNECTED) {
      doc["wifi_sta_ip"] = WiFi.localIP().toString();
      doc["wifi_sta_rssi"] = WiFi.RSSI();
    }

    char buf[768];
    serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });

  // GET /api/wifi/scan — async scan state and available networks
  g_webServer.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    int status = WiFi.scanComplete();

    if (status == WIFI_SCAN_RUNNING) {
      req->send(200, "application/json", "{\"status\":\"scanning\"}");
      return;
    }

    if (status == WIFI_SCAN_FAILED || status < 0) {
      WiFi.scanDelete();
      WiFi.scanNetworks(true, true);
      req->send(200, "application/json", "{\"status\":\"scanning\"}");
      return;
    }

    StaticJsonDocument<3072> doc;
    doc["status"] = "done";
    JsonArray networks = doc.createNestedArray("networks");

    int maxEntries = (status > 20) ? 20 : status;
    for (int i = 0; i < maxEntries; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;

      JsonObject n = networks.createNestedObject();
      n["ssid"] = ssid;
      n["rssi"] = WiFi.RSSI(i);
      n["encType"] = (int)WiFi.encryptionType(i);
      n["channel"] = WiFi.channel(i);
    }

    char outBuf[3072];
    serializeJson(doc, outBuf, sizeof(outBuf));
    req->send(200, "application/json", outBuf);

    WiFi.scanDelete();
  });
}

static void portal_registerCalibrationGetRoute() {
  g_webServer.on("/api/calib", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    StaticJsonDocument<640> doc;
    doc["tds_offset"] = g_portalCalibration ? g_portalCalibration->tdsOffset : CALIB_TDS_OFFSET_DEFAULT;
    doc["tds_gain"] = g_portalCalibration ? g_portalCalibration->tdsGain : CALIB_TDS_GAIN_DEFAULT;
    doc["pump_ml_per_sec"] = g_portalCalibration ? g_portalCalibration->pumpMlPerSec : CALIB_PUMP_ML_PER_SEC_DEFAULT;
    doc["motor_steps_up"] = g_portalCalibration ? g_portalCalibration->motorStepsUp : calibration_defaultMotorUpSteps();
    doc["motor_steps_down"] = g_portalCalibration ? g_portalCalibration->motorStepsDown : calibration_defaultMotorDownSteps();
    doc["ppfd_calibration_factor"] = g_portalCalibration ? g_portalCalibration->ppfdCalibrationFactor : CALIB_PPFD_FACTOR_DEFAULT;
    doc["ppfd_geometry_factor"] = g_portalCalibration ? g_portalCalibration->ppfdGeometryFactor : CALIB_PPFD_GEOMETRY_DEFAULT;
#if ENABLE_MOTOR
    doc["motor_position_steps"] = motor_getPositionSteps();
    doc["motor_target_steps"] = motor_getTargetSteps();
#endif
#if HW_INA228
    // INA228 power monitor runtime config + calibration (WIZARD: power-current-cal)
    doc["power_shunt_ohms"]   = g_portalCalibration ? g_portalCalibration->powerShuntOhms   : (float)INA228_SHUNT_OHMS;
    doc["power_max_current_a"]= g_portalCalibration ? g_portalCalibration->powerMaxCurrentA : (float)INA228_MAX_CURRENT_A;
    doc["power_adc_range"]    = g_portalCalibration ? g_portalCalibration->powerAdcRange    : (uint8_t)INA228_ADC_RANGE;
    doc["power_cal_factor"]   = g_portalCalibration ? g_portalCalibration->powerCalFactor   : 1.0f;
#endif

    char buf[640];
    serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });
}

static void portal_registerHistoryAndEbbRoutes() {
  // GET /api/ebb_status — Ebb&Flow state machine status
#if ENABLE_EBB_FLOW
  g_webServer.on("/api/ebb_status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    StaticJsonDocument<256> doc;
    const char* stateNames[] = {"IDLE", "FLOOD", "SOAK", "DRAIN", "FAULT"};
    const char* faultNames[] = {"NONE", "DRY_RUN", "OVERFLOW", "FLOOD_TIMEOUT", "DRAIN_TIMEOUT", "SENSOR_FAULT"};

    uint8_t st = g_portalState->ebbFlowState;
    uint8_t fc = g_portalState->ebbFlowFaultCode;
    doc["state"] = (st <= 4) ? stateNames[st] : "UNKNOWN";
    doc["state_code"] = st;
    doc["fault"] = (fc <= 5) ? faultNames[fc] : "UNKNOWN";
    doc["fault_code"] = fc;
    doc["fault_latched"] = g_portalState->ebbFlowFaultLatched;
    doc["state_since_ms"] = g_portalState->ebbFlowStateSinceMs;
    doc["last_cycle_ms"] = g_portalState->lastEbbFlowCycleStartMs;
    doc["pump_running"] = g_portalState->pumpRunning;
    doc["uptime_ms"] = millis();

    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });
#endif

  // GET /api/history — sensor history from local buffer (no RPi needed)
  // Usage: GET /api/history?field=air_temp&max=50
  g_webServer.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    const char* field = req->hasParam("field") ? req->getParam("field")->value().c_str() : "air_temp";
    int maxEntries = req->hasParam("max") ? req->getParam("max")->value().toInt() : 100;

    char err[96];
    char selectedField[32];
    if (!api_validateHistoryRequest(field, maxEntries, selectedField, sizeof(selectedField), err, sizeof(err))) {
      StaticJsonDocument<128> out;
      out["error"] = err;
      char outBuf[128];
      serializeJson(out, outBuf, sizeof(outBuf));
      req->send(400, "application/json", outBuf);
      return;
    }

    char buf[4096];
    if (history_getField(selectedField, buf, sizeof(buf), maxEntries)) {
      req->send(200, "application/json", buf);
    } else {
      req->send(200, "application/json", "[]");
    }
  });
}

static void portal_registerStatusGetRoute() {
  // GET /api/status — sensor readings + system state
  // /api/status — UI:n pollaus-endpoint (flat-rakenne, kentat lyhyilla nimilla).
  // Erotettu /api/state:sta joka tarjoaa e-ink-naytolle stable contractin
  // (sisakkainen rakenne, EinkDevFields-yhteensopiva). Molemmat palauttavat
  // osittain samat tiedot. Tulevaisuudessa kannattaa yhdistaa kun /api/state:n
  // sisakkainen rakenne kattaa myos UI:n tarvitsemat puuttuvat kentat
  // (ap_remaining, lights_on, ebb_*, jne). Katso TEKNOLOGIA.md:n audit-merkinta.
  g_webServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    // Auth-fields lasketaan aina (UI tarvitsee tiedon onko PIN voimassa
    // ennen kirjautumismodaalin n\xe4ytt\xe4mist\xe4). Auth-vaatimus tarkistetaan
    // vasta n\xe4iden j\xe4lkeen.
    bool authRequired = portal_authIsRequired();
    bool authAuthorized = portal_isAuthorized(req);

    // ~70 kenttaa ei mahdu 1800 tavuun -> pool-ylivuoto serialisoi viimeiset
    // kentat roskatavuina (havaittu 3.7.2026: /api/status rikki wifi_sta_ip:n
    // jalkeen). Heap-doc (DynamicJsonDocument) valttaa myos async-taskin
    // pinovuodon jonka 4 KB stack-doc aiheuttaisi.
    DynamicJsonDocument doc(4096);
    doc["auth_required"] = authRequired;
    doc["auth_authorized"] = authAuthorized;

    // Jos PIN on asetettu eik\xe4 client ole kirjautunut, palauta vain
    // auth-statuskent\xe4t. T\xe4st\xe4 UI tiet\xe4\xe4 n\xe4ytt\xe4\xe4 modal:n.
    if (authRequired && !authAuthorized) {
      char buf[96];
      serializeJson(doc, buf, sizeof(buf));
      req->send(200, "application/json", buf);
      return;
    }

    if (g_portalSensors) {
      doc["air_temp"]       = g_portalSensors->airTempC;
      doc["air_humidity"]   = g_portalSensors->airHumidity;
      doc["air_pressure"]   = g_portalSensors->airPressureHpa;
      doc["water_temp"]     = g_portalSensors->waterTempC;
      doc["tds_ppm"]        = g_portalSensors->tdsPpm;
      doc["plant_height"]   = g_portalSensors->plantHeightMm;
      doc["water_ok"]       = g_portalSensors->waterLevelOk;
      doc["water_overflow"] = g_portalSensors->waterOverflowActive;
      doc["battery_v"]      = g_portalSensors->batteryVoltage;
      doc["battery_pct"]    = g_portalSensors->batteryPercent;
      doc["lora_rssi"]      = g_portalSensors->loraRssi;
      doc["env_valid"]        = g_portalSensors->envValid;
      doc["height_valid"]     = g_portalSensors->heightValid;
      doc["water_temp_valid"] = g_portalSensors->waterTempValid;
      doc["tds_valid"]        = g_portalSensors->tdsValid;

      // V2 PE-anturit (WP-F0-2): paljastetaan vain kun HW_* päällä jotta
      // V1-clientit eivät näe tyhjää/nollakenttää.
#if HW_SCD41
      doc["co2_ppm"]          = g_portalSensors->airCO2Ppm;
      doc["air_co2_valid"]    = g_portalSensors->airCO2Valid;
      doc["scd41_ready"]      = scd41_isReady();
#endif
#if HW_MLX90614
      doc["leaf_temp_c"]      = g_portalSensors->leafTempC;
      doc["leaf_ambient_c"]   = g_portalSensors->leafAmbientC;
      doc["leaf_temp_valid"]  = g_portalSensors->leafTempValid;
      doc["mlx90614_ready"]   = mlx90614_isReady();
#endif
#if HW_AS7341
      doc["ppfd"]             = g_portalSensors->ppfdNow;
      doc["ppfd_valid"]       = g_portalSensors->ppfdValid;
      doc["dli"]              = g_portalSensors->dliToday;
      doc["spectrum_valid"]   = g_portalSensors->spectrumValid;
      doc["as7341_ready"]     = as7341_isReady();
#endif
#if HW_INA228
      // Live-arvot Huolto-tabin PPFD/INA228-kalibrointikorteille (WIZARD:
      // ppfd-cal / power-current-cal). Loput power_*-kentat ovat vain
      // /api/state:ssa - tama on kevyt polling-status, ei laaja snapshot.
      doc["power_current_ma"] = g_portalSensors->powerCurrentmA;
      doc["power_valid"]      = g_portalSensors->powerValid;
#endif
      // VPD on johdettu (ilma-T/RH + lehtilampo), ei oma anturi — mukaan aina.
      // Portaalin PE-tavoitenaytto vertaa nykyarvoja vaiheen tavoitteisiin.
      doc["vpd_kpa"]          = g_portalSensors->vpdKpa;
      doc["vpd_valid"]        = g_portalSensors->vpdValid;
    }

    if (g_portalState) {
      doc["sensors_ready"]  = g_portalState->sensorsReady;
      doc["lora_ready"]     = g_portalState->loraReady;
    #if ENABLE_MCP23017
      // Laajentimen tila näkyviin: kun nappi ja merkkivalo ovat sen takana
      // (PCB v2), tämä on ainoa etäkeino erottaa "nappi ei toimi" -vika
      // johdotuksesta vs. kadonneesta laajentimesta (hw_integrointi.md askel 4).
      doc["mcp23017_ready"] = mcp23017_isReady();
      doc["mcp23017_addr"]  = mcp23017_addr();
      doc["button_pressed"] = button_isPressed();
    #endif
      doc["lights_on"]      = g_portalState->lightsOn;
      doc["motor_moving"]   = g_portalState->motorMoving;
      doc["pump_running"]   = g_portalState->pumpRunning;
    #if ENABLE_PUMP
      doc["pump_continuous"]    = pump_isContinuous();
      doc["pump_duty_pct"]      = pump_getDutyPct();
      doc["pump_overflow_latched"] = pump_isOverflowLatched();
    #endif
    #if ENABLE_FAN
      // Fan health (PCB v2): duty is what we commanded, spinning/stalled is what
      // the coarse tacho reports. fan_stalled mirrors the DEVICE_FAULT_FAN advisory.
      doc["fan_ready"]      = fan_isReady();
      doc["fan_duty_pct"]   = fan_getDutyPct();
      doc["fan_spinning"]   = fan_isSpinning();
      doc["fan_stalled"]    = fan_isStalled();
    #endif
    #if ENABLE_MCP23017
      doc["mcp23017_ready"] = mcp23017_isReady();
      doc["mcp23017_addr"]  = mcp23017_isReady() ? mcp23017_addr() : 0;
    #endif
      doc["air_pump_on"]    = g_portalState->airPumpOn;
      doc["uptime_ms"]      = millis();
      doc["fw_version"] = FIRMWARE_VERSION;
      // build_id: compile timestamp of the single .ino TU — changes on every
      // rebuild, so a successful OTA activation is provable by comparing it.
      doc["build_id"]   = __DATE__ " " __TIME__;
      // fw_git/fw_env: which commit and which PIO env this image was built
      // from (scripts/pio_git_version.py). Answers "is the device running the
      // code I think it is" without guessing from timestamps — the wrong-env
      // flash silently disabling features has bitten three times.
      doc["fw_git"]     = FW_GIT_SHA;
      doc["fw_env"]     = FW_PIO_ENV;
      // Boot-loop diagnostics: reset cause + consecutive crash reboots. Lets a
      // remote reader spot a crash-reboot loop without serial access.
      doc["boot_cause"]    = watchdog_resetReasonShort();
      doc["crash_reboots"] = watchdog_crashCount();

    #if ENABLE_DEVICE_STATE
      doc["device_state"]      = (int)g_device.state;
      doc["device_state_name"] = device_stateName(g_device.state);
      doc["device_faults"]     = g_device.faults;
      doc["device_advisories"] = g_device.advisories;
      doc["advisory_msg"]      = g_device.advisoryMessage;
      doc["safe_mode"]         = device_inSafeMode();
      // health: ihmisluettava yhteenveto — lue tämä ensin. Kertoo suoraan onko
      // laite toiminnassa, degraded (advisory), FAULT, vai jumissa bootissa.
      char healthBuf[DEVICE_HEALTH_LEN];
      device_healthSummary(healthBuf, sizeof(healthBuf));
      // F4: override health when active plant has no grow phases. This is the
      // most common cause of silent flooding failures (I1): JSON entry shadows
      // the built-in but omits "phases", phaseCount==0, scheduling never runs.
      // Only overrides plain "Toiminnassa" — degraded/FAULT messages take priority.
      if (strcmp(healthBuf, "Toiminnassa") == 0) {
        PlantConfig* ap = plants_getById(g_portalConfig->currentPlantId);
        if (ap && ap->phaseCount == 0) {
          strncpy(healthBuf, "Tulvitus ei ajastu: aktiivikasvilla ei kasvuvaiheita",
                  sizeof(healthBuf) - 1);
          healthBuf[sizeof(healthBuf) - 1] = '\0';
        }
      }
      doc["health"]            = healthBuf;
    #endif

      // Onboarding: portal JS shows the setup checklist banner while true.
      doc["onboarding"] = g_portalConfig ? !g_portalConfig->onboardingComplete : false;
      // ONBOARD_STEP_* mask (onboarding.h): banner renders ✓/○ per step and
      // enables the Valmis button only when the device-side gate would open.
      doc["onboarding_steps"] = g_portalConfig ? g_portalConfig->onboardingSteps : 0;

      // primary_action: toimintopalkin YKSI ensisijainen toiminto, laskettu
      // laitepaassa (portal_primary_action.h, test_portal_primary_action) —
      // sama vastaus kaikille pinnoille, prioriteetit yhdessa paikassa.
      // step_ack-tilassa mukana myos step_text palkin ohjeteksti (zero-copy
      // firmware-vakiosta, sama lahde kuin /api/state growing.step_text).
      {
        PrimaryActionInput pa;
        pa.onboarding = g_portalConfig ? !g_portalConfig->onboardingComplete : false;
      #if ENABLE_DEVICE_STATE
        pa.fault          = (g_device.state == DEVICE_FAULT);
        pa.advisoryActive = (g_device.advisories != 0);
      #else
        pa.fault          = false;
        pa.advisoryActive = false;
      #endif
      #if ENABLE_GUIDED_GROWING
        pa.growActive          = g_portalConfig ? g_portalConfig->growActive : false;
        pa.phasePendingAdvance = g_portalState ? g_portalState->growPhasePendingAdvance : false;
        pa.stepAckAllowed      = false;
        if (g_portalState && g_portalConfig) {
        #if ENABLE_DEVICE_STATE
          bool inGrowState = (g_device.state == DEVICE_GROWING);
        #else
          bool inGrowState = pa.growActive;
        #endif
          PlantConfig* paPlant = g_portalCurrentPlant;
          if (!paPlant) paPlant = plants_getById(g_portalConfig->currentPlantId);
          GrowStepView sv;
          uint32_t stepElapsedMs = (uint32_t)(millis() - g_portalState->growStepStartMs);
          if (inGrowState &&
              growstep_buildView(paPlant, g_portalConfig, stepElapsedMs, &sv) &&
              sv.ackAllowed) {
            pa.stepAckAllowed = true;
            doc["step_text"]  = sv.text;
          }
        }
      #else
        pa.growActive          = false;
        pa.phasePendingAdvance = false;
        pa.stepAckAllowed      = false;
      #endif
        doc["primary_action"] = portal_primaryActionName(portal_resolvePrimaryAction(&pa));
      }

    #if ENABLE_EBB_FLOW
      const char* stateNames[] = {"IDLE", "FLOOD", "SOAK", "DRAIN", "FAULT"};
      const char* faultNames[] = {"NONE", "DRY_RUN", "OVERFLOW", "FLOOD_TIMEOUT", "DRAIN_TIMEOUT", "SENSOR_FAULT"};
      doc["ebb_state"]      = g_portalState->ebbFlowState;
      doc["ebb_fault"]      = g_portalState->ebbFlowFaultLatched;
      doc["ebb_fault_code"] = g_portalState->ebbFlowFaultCode;
      doc["ebb_state_name"] = (g_portalState->ebbFlowState <= 4)
        ? stateNames[g_portalState->ebbFlowState]
        : "UNKNOWN";
      doc["ebb_fault_name"] = (g_portalState->ebbFlowFaultCode <= 5)
        ? faultNames[g_portalState->ebbFlowFaultCode]
        : "UNKNOWN";
      doc["ebb_last_cycle_ms"] = g_portalState->lastEbbFlowCycleStartMs;
      doc["ebb_calib_phase"] = g_portalState->ebbCalibPhase;  // 0=idle,1=fill,2=drain
      doc["soak_calib_active"] = g_portalState->soakCalibActive;
      {
        PlantConfig* sp = g_portalCurrentPlant;
        if (!sp && g_portalConfig) sp = plants_getById(g_portalConfig->currentPlantId);
        const GrowPhaseParams* ap = g_portalConfig
          ? scheduler_getActivePhase(sp, g_portalConfig) : NULL;
        uint16_t co = g_portalConfig ? g_portalConfig->ebbFloodIntervalMin : 0;
        bool lo = g_portalState->lightsOn;
        uint16_t eff = scheduler_effectiveFloodIntervalMin(ap, co, lo);
        doc["ebb_effective_interval_min"] = eff;
        doc["ebb_phase_flood_enabled"]    = scheduler_floodEnabled(ap, co);
        doc["ebb_night_active"]           = (!lo && EBB_NIGHT_FLOOD_MULTIPLIER > 1);
        char frs[80];
        scheduler_floodReasonStr(frs, sizeof(frs), ap, co, lo);
        doc["ebb_flood_reason"] = frs;
        doc["grow_phase_name"] = ap ? ap->label : "";
        // Next periodic flood only ticks while GROWING + ebb IDLE; otherwise -1.
        bool floodScheduled =
        #if ENABLE_DEVICE_STATE
          (g_device.state == DEVICE_GROWING) &&
        #endif
          (g_portalState->ebbFlowState == 0 /* EBB_STATE_IDLE */);
        doc["ebb_next_cycle_sec"] = scheduler_secondsUntilNextFlood(
          floodScheduled ? eff : 0, g_portalState->lastEbbFlowCycleStartMs, millis());

        // Circulation ("kierto") — runs only while GROWING; next time shown only then.
        bool growingNow =
        #if ENABLE_DEVICE_STATE
          (g_device.state == DEVICE_GROWING);
        #else
          true;
        #endif
        doc["circulate_active"] = g_portalState->circulateActive;
        doc["ebb_circulate_enabled"] = g_portalConfig->ebbCirculateEnabled;
        doc["ebb_next_circulate_sec"] = scheduler_secondsUntilNextCirculate(
          g_portalConfig->ebbCirculateEnabled && growingNow,
          g_portalConfig->ebbCirculateIntervalMin,
          g_portalState->lastCirculateStartMs, millis());
      }
    #endif

      char uptimeBuf[16];
      power_formatUptime(uptimeBuf, sizeof(uptimeBuf));
      doc["uptime"] = uptimeBuf;

      // AP remaining time
      unsigned long elapsed = millis() - g_portalStartMs;
      if (elapsed < WIFI_AP_DURATION_MS) {
        unsigned long remaining = (WIFI_AP_DURATION_MS - elapsed) / 1000;
        char apBuf[16];
        snprintf(apBuf, sizeof(apBuf), "%lum %lus", remaining / 60, remaining % 60);
        doc["ap_remaining"] = apBuf;
      } else {
        doc["ap_remaining"] = "sulkeutuu pian";
      }
    }

    if (g_portalConfig) {
      doc["motor_pos"]      = g_portalConfig->motorCurrentMm;
      doc["motor_target_mm"] = g_portalConfig->motorTargetMm;
    #if ENABLE_MOTOR
      doc["motor_pos_steps"] = (long)motor_getPositionSteps();
    #endif
      doc["current_plant"]  = g_portalConfig->currentPlantId;
      // Human-readable name + start method for the portal's ready card, which
      // names what the user actually chose ("Basilika · siemenestä") instead of
      // the internal id. Falls back to the id if the plant is not resolvable.
      PlantConfig* statusPlant = g_portalCurrentPlant;
      if (!statusPlant) statusPlant = plants_getById(g_portalConfig->currentPlantId);
      doc["plant_name"]        = statusPlant ? statusPlant->name
                                             : g_portalConfig->currentPlantId;
      doc["grow_start_method"] = g_portalConfig->growStartMethod;
      doc["test_mode"]      = g_portalConfig->testMode;

      int staStatus = WiFi.status();
      doc["wifi_sta_connected"] = (staStatus == WL_CONNECTED);
      if (staStatus == WL_CONNECTED) {
        doc["wifi_sta_ip"] = WiFi.localIP().toString();
        doc["wifi_sta_rssi"] = WiFi.RSSI();
      }

    #if ENABLE_GUIDED_GROWING
      doc["grow_active"]    = g_portalConfig->growActive;
      doc["grow_phase"]     = g_portalConfig->growPhase;
      doc["grow_day"]       = g_portalConfig->growElapsedDays;
    #endif
    }

#if ENABLE_GUIDED_GROWING
    if (g_portalState) {
      doc["grow_pending"] = g_portalState->growPhasePendingAdvance;
    }
#endif

    char buf[4096];
    serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });
}

static void portal_registerGrowGetRoute() {
#if ENABLE_GUIDED_GROWING_UI
  // GET /api/grow — guided growing status, instructions and optional simulation
  g_webServer.on("/api/grow", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    // 3072 (nostettu 2048:sta 18.7.2026): 6 PE-tavoitekenttaa (dli/ppfd/
    // vpd_min/vpd_max/co2/leaf) + pitka phase_guidance ylittivat 2048:n, ja
    // StaticJsonDocument pudottaa ylivuodossa MYOHEMMAT kentat hiljaa —
    // plant_id/phase_count/phase_* katosivat vastauksesta vaikka grow_active
    // oli true. docs/kehitys/pe-ohjausmalli.md.
    // 4096 (nostettu 3072:sta 18.7.2026): sisakkainen "step"-objekti tuo koko
    // aktiivisen alitehtavan (askelteksti ~300 merkkia) web-Kasvatus-valilehteen.
    StaticJsonDocument<4096> doc;

    PlantConfig* plant = g_portalCurrentPlant;
    if (!plant && g_portalConfig) {
      plant = plants_getById(g_portalConfig->currentPlantId);
    }

    doc["enabled"] = ENABLE_GUIDED_GROWING;
    doc["simulation_enabled"] = ENABLE_GUIDED_GROWING_SIMULATION;
    if (!plant) {
      doc["ok"] = false;
      doc["error"] = "active plant not found";
      char errBuf[256];
      serializeJson(doc, errBuf, sizeof(errBuf));
      req->send(500, "application/json", errBuf);
      return;
    }

    bool growActive = g_portalConfig ? g_portalConfig->growActive : false;
    bool pending = g_portalState ? g_portalState->growPhasePendingAdvance : false;
    uint8_t phaseIndex = g_portalConfig ? g_portalConfig->growPhase : 0;
    uint16_t elapsedDays = g_portalConfig ? g_portalConfig->growElapsedDays : 0;
    uint8_t startMethod = g_portalConfig ? g_portalConfig->growStartMethod : 0;

    bool requestSimulation = true;
    if (req->hasParam("simulate")) {
      requestSimulation = (req->getParam("simulate")->value().toInt() != 0);
    }

    bool useSimulation = (!growActive && ENABLE_GUIDED_GROWING_SIMULATION && requestSimulation);
    if (useSimulation) {
      phaseIndex = portal_pickSimulatedPhase(plant, &elapsedDays);
      pending = false;
    }

    if (plant->phaseCount > 0 && phaseIndex >= plant->phaseCount) {
      phaseIndex = (uint8_t)(plant->phaseCount - 1);
    }

    const GrowPhaseParams* activePhase = (plant->phaseCount > 0) ? &plant->phases[phaseIndex] : NULL;

  #if ENABLE_EBB_FLOW
    uint16_t cfgFloodMin = g_portalConfig ? g_portalConfig->ebbFloodIntervalMin : 0;
    bool lightsOnG = g_portalState ? g_portalState->lightsOn : true;
    doc["phase_flood_enabled"] = scheduler_floodEnabled(activePhase, cfgFloodMin);
  #endif

    doc["ok"] = true;
    doc["simulated"] = useSimulation;
    doc["plant_id"] = plant->id;
    doc["plant_name"] = plant->name;
    doc["phase_count"] = plant->phaseCount;
    doc["grow_active"] = growActive;
    doc["grow_pending_advance"] = pending;
    doc["grow_start_method"] = startMethod;
    doc["grow_start_method_name"] = portal_growStartMethodName(startMethod);
    doc["phase_index"] = phaseIndex;
    doc["phase_elapsed_days"] = elapsedDays;
  #if ENABLE_GUIDED_GROWING_SIMULATION
    doc["sim_override_active"] = (g_portalSimPhaseOverride >= 0);
    doc["sim_override_phase"] = g_portalSimPhaseOverride;
    doc["sim_override_day"] = g_portalSimDayOverride;
  #endif

    if (activePhase) {
      doc["phase_label"] = activePhase->label;
      doc["phase_type"] = portal_growPhaseTypeName(activePhase->type);
      doc["phase_duration_days"] = activePhase->durationDays;
      doc["phase_light_hours"] = activePhase->lightHours;
      // Aktiivivaiheen PE-tavoitteet (docs/kehitys/pe-ohjausmalli.md) —
      // portaali/e-ink nayttaa nama tavoite vs. nykyarvo -parina.
      doc["phase_dli_target_mol"] = activePhase->dliTargetMol;
      doc["phase_ppfd_target_umol"] = activePhase->ppfdTargetUmol;
      doc["phase_vpd_min_kpa"] = activePhase->vpdMinKpa;
      doc["phase_vpd_max_kpa"] = activePhase->vpdMaxKpa;
      doc["phase_co2_target_ppm"] = activePhase->co2TargetPpm;
      doc["phase_leaf_temp_max_c"] = activePhase->leafTempMaxC;
      const char* fallbackInstruction = portal_growInstructionText(activePhase->type, startMethod);
      doc["instruction"] = fallbackInstruction;
      doc["phase_guidance"] = grow_resolvePhaseGuidance(activePhase, fallbackInstruction);
    #if ENABLE_EBB_FLOW
      doc["phase_flood_interval_min"] = scheduler_effectiveFloodIntervalMin(activePhase, cfgFloodMin, lightsOnG);
      char growFR[80];
      scheduler_floodReasonStr(growFR, sizeof(growFR), activePhase, cfgFloodMin, lightsOnG);
      doc["phase_flood_reason"] = growFR;
    #endif
      if (activePhase->durationDays > 0) {
        uint16_t remaining = (elapsedDays >= activePhase->durationDays)
          ? 0
          : (activePhase->durationDays - elapsedDays);
        doc["phase_days_left"] = remaining;
      } else {
        doc["phase_days_left"] = -1;
      }
    }

    if (pending && plant->phaseCount > 0) {
      uint8_t next = phaseIndex + 1;
      if (next < plant->phaseCount) {
        doc["pending_next_label"] = plant->phases[next].label;
      }
    }

    // Aktiivinen alitehtava (askel) — web-Kasvatus-valilehti nayttaa taman
    // korttina (renderGrow). Rakennetaan REAALITILASTA, EI simulaatiosta:
    // askelkello kulkee g_portalState->growStepStartMs:sta ja indeksi
    // g_portalConfig->growStepIndex:sta. growstep_buildView palauttaa false
    // kun kasvatus ei ole kaynnissa tai listaa ei ole -> step:{active:false}.
    {
      JsonObject step = doc.createNestedObject("step");
      GrowStepView sv;
      uint32_t stepElapsedMs = g_portalState
        ? (uint32_t)(millis() - g_portalState->growStepStartMs) : 0;
      if (g_portalState && g_portalConfig &&
          growstep_buildView(plant, g_portalConfig, stepElapsedMs, &sv)) {
        step["active"]        = sv.active;
        step["index"]         = sv.index;
        step["count"]         = sv.count;
        step["text"]          = sv.text;
        step["late"]          = sv.late;
        step["ack_allowed"]   = sv.ackAllowed;
        step["awaiting_user"] = sv.awaitingUser;
        step["timer_min"]     = sv.timerMin;
        step["elapsed_sec"]   = sv.elapsedSec;
        step["remaining_sec"] = sv.remainingSec;
      } else {
        step["active"] = false;
      }
    }

    JsonArray phases = doc.createNestedArray("phases");
    for (uint8_t i = 0; i < plant->phaseCount; i++) {
      JsonObject ph = phases.createNestedObject();
      ph["index"] = i;
      ph["label"] = plant->phases[i].label;
      ph["type"] = portal_growPhaseTypeName(plant->phases[i].type);
      ph["duration_days"] = plant->phases[i].durationDays;
      ph["is_current"] = (i == phaseIndex);
    }

    char buf[4096];   // vastaa nostettua StaticJsonDocument<4096>-kokoa (yllä)
    serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });
#endif
}

#endif // WIFI_PORTAL_ROUTES_GET_H
