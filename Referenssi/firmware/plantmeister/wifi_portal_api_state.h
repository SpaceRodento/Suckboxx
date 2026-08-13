/*=====================================================================
  wifi_portal_api_state.h - State + history endpoints

  GET /api/state          Full device snapshot for phone-based monitoring
  GET /api/device/history Recent device state transitions (ringbuffer)
  GET /api/intents        Documentation of all available intents

  Included by wifi_portal.h after shared globals.
=====================================================================*/

#ifndef WIFI_PORTAL_API_STATE_H
#define WIFI_PORTAL_API_STATE_H

#include "device_state.h"
#include "ux_indicator.h"
#include "input_router.h"

static void portal_registerStateApi() {
  // GET /api/state — comprehensive state snapshot
  g_webServer.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    // Heap-allokoitu (11.8.2026, M1): growing.targets[] (5 PE-mukavuuskaistaa,
    // ks. alla) olisi tyontanyt StaticJsonDocument<3584>:n + 3072-tavuisen
    // serialize-bufferin yhteensa yli 6.6 kB:iin — juuri se pino jota edellinen
    // kommentti ("elavat async_tcp-taskin PINOSSA ~8 kB, ala kasvata
    // kumpaakaan ilman pinolaskelmaa") varoitti kasvattamasta. DynamicJsonDocument
    // varaa doc-poolin heapista (sama ratkaisu kuin plant_database.h:n
    // save/load-dokumenteilla) — pino kevenee tallakin, koska poistuva
    // 3584-tavun stack-varaus on suurempi kuin serialize-bufferin kasvu alla.
    DynamicJsonDocument doc(4096);

    // Device state
    JsonObject device = doc.createNestedObject("device");
    device["state"]            = device_stateName(g_device.state);
    device["state_num"]        = (uint8_t)g_device.state;
    device["prev_state"]       = device_stateName(g_device.prevState);
    device["entered_at"]       = g_device.enteredAt;
    device["time_in_state_ms"] = device_timeInState();
    device["faults"]           = g_device.faults;
    device["fault_msg"]        = g_device.faultMessage;
    device["advisories"]       = g_device.advisories;
    device["advisory_msg"]     = g_device.advisoryMessage;
    char healthBuf[DEVICE_HEALTH_LEN];
    device_healthSummary(healthBuf, sizeof(healthBuf));
    device["health"]           = healthBuf;
    // Onboarding: true = first-run setup not completed → e-ink shows the
    // join-AP instruction, portal shows the checklist banner. sta_ip lets the
    // e-ink display the device's home-network address once connected.
    device["onboarding"]       = g_portalConfig ? !g_portalConfig->onboardingComplete : false;
    // Which setup steps are done (ONBOARD_STEP_* bits, onboarding.h) → the
    // portal banner renders ✓/○ per step and enables the Valmis button only
    // when the gate opens. Same mask the device gates on: one truth.
    device["onboarding_steps"] = g_portalConfig ? g_portalConfig->onboardingSteps : 0;
    device["sta_ip"]           = (WiFi.status() == WL_CONNECTED)
                                   ? WiFi.localIP().toString() : String("");

    // UX (LED + e-ink view) — sama konteksti kuin ux_tick:lla, jotta API:n
    // led_pattern/message on identtinen fyysisen LEDin kanssa (askel-vilkku).
    PlantConfig* uxPlant = g_portalCurrentPlant;
    if (!uxPlant && g_portalConfig) uxPlant = plants_getById(g_portalConfig->currentPlantId);
    UxFrame frame;
    ux_computeFrame(&frame, g_portalState, g_portalConfig, uxPlant);
    JsonObject ux = doc.createNestedObject("ux");
    const char* colorName = "off";
    switch (frame.ledColor) {
      case LED_GREEN:  colorName = "green";  break;
      case LED_YELLOW: colorName = "yellow"; break;
      case LED_RED:    colorName = "red";    break;
      default: break;
    }
    const char* patternName = "off";
    switch (frame.ledPattern) {
      case LED_PATTERN_SOLID:      patternName = "solid";      break;
      case LED_PATTERN_BLINK_FAST: patternName = "blink_fast"; break;
      case LED_PATTERN_BLINK_SLOW: patternName = "blink_slow"; break;
      default: break;
    }
    ux["led_color"]   = colorName;
    ux["led_pattern"] = patternName;
    ux["message"]     = frame.message;
    ux["action"]      = frame.action;

    // Sensors
    if (g_portalSensors) {
      JsonObject sensors = doc.createNestedObject("sensors");
      sensors["air_temp_c"]      = g_portalSensors->airTempC;
      sensors["air_humidity"]    = g_portalSensors->airHumidity;
      sensors["water_temp_c"]    = g_portalSensors->waterTempC;
      sensors["tds_ppm"]         = g_portalSensors->tdsPpm;
      sensors["plant_height_mm"] = g_portalSensors->plantHeightMm;
      sensors["water_level_ok"]  = g_portalSensors->waterLevelOk;
      sensors["water_overflow"]  = g_portalSensors->waterOverflowActive;
      sensors["env_valid"]       = g_portalSensors->envValid;
      // Sekuntia viimeisimmasta onnistuneesta env-luennasta; -1 = ei koskaan.
      // env_valid=true + env_age_s>0 tarkoittaa etta arvo on pidetty voimassa
      // hukatun naytteen yli (sensor_sticky.h), ei mitattu talla kierroksella.
      sensors["env_age_s"]       = (g_portalSensors->envAgeMs == UINT32_MAX)
                                   ? -1
                                   : (int)(g_portalSensors->envAgeMs / 1000UL);
      sensors["vpd_kpa"]         = g_portalSensors->vpdKpa;
      sensors["vpd_valid"]       = g_portalSensors->vpdValid;
      sensors["leaf_temp_c"]     = g_portalSensors->leafTempC;
      sensors["leaf_ambient_c"]  = g_portalSensors->leafAmbientC;
      sensors["leaf_temp_valid"] = g_portalSensors->leafTempValid;
      // PE Assimilate balance — CO2 (SCD41) + PPFD/DLI (AS7341 via light pipeline).
      // Ungated like vpd/leaf above; *_valid tells the client whether the source
      // sensor is present (value is 0 + valid=false when absent → e-ink shows "--").
      sensors["co2_ppm"]         = g_portalSensors->airCO2Ppm;
      sensors["co2_valid"]       = g_portalSensors->airCO2Valid;
      // ppfd = latvan taso (anturi x geometriakerroin) — tama on se luku jota
      // PE-tavoitteet vertaavat. ppfd_sensor = anturin oma lukema, jotta
      // geometriakertoimen vaikutus on nakyvissa eika naytto valehtele.
      sensors["ppfd"]            = g_portalSensors->ppfdNow;
      sensors["ppfd_sensor"]     = g_portalSensors->ppfdSensor;
      sensors["ppfd_valid"]      = g_portalSensors->ppfdValid;
      // Litteana sisarena ppfd:lle, ei vain spektrilohkossa: e-inkin
      // JSON-suodatin listaa sensors-avaimet litteana eika nae sisakkaista
      // light_spectrum-objektia lainkaan. Ilman tata naytto ei voisi tietaa
      // etta luku on alaraja — se piirtaisi tasanteen faktana.
      sensors["ppfd_saturated"]  = g_portalSensors->lightSpectrum.saturated;
      sensors["dli"]             = g_portalSensors->dliToday;
      sensors["height_valid"]    = g_portalSensors->heightValid;
      sensors["battery_v"]       = g_portalSensors->batteryVoltage;
      sensors["battery_pct"]     = g_portalSensors->batteryPercent;

      // INA228/226/219 power monitoring — power_valid=false when no monitor is
      // present (values 0). current/power are signed; charge/energy are on-chip
      // accumulators (INA228) for multi-day integration + Excel graphing.
      sensors["power_bus_v"]      = g_portalSensors->powerBusV;
      sensors["power_current_ma"] = g_portalSensors->powerCurrentmA;
      sensors["power_mw"]         = g_portalSensors->powerMw;
      sensors["power_charge_mah"] = g_portalSensors->powerChargemAh;
      sensors["power_energy_wh"]  = g_portalSensors->powerEnergyWh;
      sensors["power_valid"]      = g_portalSensors->powerValid;

      // AS7341 raw spectrum — UI ja DLI/PPFD-laskenta (Phase 1) lukevat tasta.
      JsonObject spectrum = sensors.createNestedObject("light_spectrum");
      spectrum["f1_415nm"]      = g_portalSensors->lightSpectrum.f1_415nm;
      spectrum["f2_445nm"]      = g_portalSensors->lightSpectrum.f2_445nm;
      spectrum["f3_480nm"]      = g_portalSensors->lightSpectrum.f3_480nm;
      spectrum["f4_515nm"]      = g_portalSensors->lightSpectrum.f4_515nm;
      spectrum["f5_555nm"]      = g_portalSensors->lightSpectrum.f5_555nm;
      spectrum["f6_590nm"]      = g_portalSensors->lightSpectrum.f6_590nm;
      spectrum["f7_630nm"]      = g_portalSensors->lightSpectrum.f7_630nm;
      spectrum["f8_680nm"]      = g_portalSensors->lightSpectrum.f8_680nm;
      spectrum["clear"]         = g_portalSensors->lightSpectrum.clear;
      spectrum["nir"]           = g_portalSensors->lightSpectrum.nir;
      spectrum["flicker_hz"]    = g_portalSensors->lightSpectrum.flickerHz;
      spectrum["integration_ms"] = g_portalSensors->lightSpectrum.integrationMs;
      spectrum["gain"]          = g_portalSensors->lightSpectrum.gain;
      // true = ADC-katto lyoty -> ppfd on ALARAJA eika mittaus. Kuluttajan
      // (e-ink, UI, lokit) on merkittava luku epavarmaksi, ei esitettava
      // faktana: tasanne nayttaa silta kuin valo lakkaisi kirkastumasta.
      spectrum["saturated"]     = g_portalSensors->lightSpectrum.saturated;
      spectrum["valid"]         = g_portalSensors->spectrumValid;
    }

    // Motor and growing
    if (g_portalConfig) {
      JsonObject motor = doc.createNestedObject("motor");
      motor["current_mm"] = g_portalConfig->motorCurrentMm;
      motor["target_mm"]  = g_portalConfig->motorTargetMm;
      motor["moving"]     = g_portalState ? g_portalState->motorMoving : false;

      PlantConfig* gp_plant = g_portalCurrentPlant;
      if (!gp_plant) gp_plant = plants_getById(g_portalConfig->currentPlantId);
      const GrowPhaseParams* gap = scheduler_getActivePhase(gp_plant, g_portalConfig);

      JsonObject growing = doc.createNestedObject("growing");
      growing["active"]       = g_portalConfig->growActive;
      growing["phase"]        = g_portalConfig->growPhase;
      growing["phase_name"]   = gap ? gap->label : "";
      growing["phase_count"]  = gp_plant ? gp_plant->phaseCount : 0;
      growing["elapsed_days"] = g_portalConfig->growElapsedDays;
      growing["plant_id"]     = g_portalConfig->currentPlantId;
      growing["plant_name"]   = gp_plant ? gp_plant->name : "";
      growing["start_method"] = g_portalConfig->growStartMethod;

      // Valojakso e-inkin DLI-arviointia varten. Ilman naita naytto vertasi
      // kesken vuorokautta kertynytta valosummaa koko paivan tavoitteeseen ja
      // valitti "Valosumma matala" joka aamu joka kasvilla (havaittu 29.7.2026).
      //
      // light_elapsed_h on JOHDETTU arvo, ei raakakonfiguraatio: valojakso on
      // ankkuroitu lightCycleStartMs:aan eika seinakelloon, joten lightOnHour
      // on siirtyma syklin sisalla EIKA kellonaika. Laite vastaa kysymykseen
      // itse, koska vain silla on ankkuri. -1 = syklin pimea osa.
      growing["light_elapsed_h"] = (g_portalState && gp_plant)
          ? scheduler_lightElapsedHours(gp_plant, g_portalState, g_portalConfig)
          : -1.0f;
      growing["light_hours"]   = gap ? gap->lightHours
                                     : (gp_plant ? gp_plant->lightHours : 0);

      // Vaiheohje e-inkille. Tama on koko "laite ohjaa kayttajaa" -lupauksen
      // sauma: ohjetekstit ovat olleet olemassa kasviprofiileissa alusta asti,
      // mutta ne tarjoiltiin vain /api/grow:sta — eli portaalista, selaimella,
      // sille joka osasi etsia. Seinataulu, jonka koko tehtava on kertoa mita
      // tehda seuraavaksi, ei nahnyt niita koskaan. Sama teksti, sama lahde
      // (grow_resolvePhaseGuidance), nyt siina kanavassa jossa se luetaan.
      const char* gp_guidance = "";
      const char* gp_action   = "";
      if (gap) {
        gp_guidance = grow_resolvePhaseGuidance(
            gap, portal_growInstructionText(gap->type, g_portalConfig->growStartMethod));
        // Lyhyt muoto e-inkin banneriin: yksi rivi, ei fonttikoon pienennysta
        // (rautatestattu rajoite, ks. grow_guidance.h). Kasvi- ja paivatietoinen
        // (Fable §3.2/3.4/3.6): vaihepaiva growElapsedDays valitsee ikkunan,
        // basilikalle taytetty taulukko, muille geneerinen switch-fallback.
        gp_action = grow_shortActionFor(g_portalConfig->currentPlantId, gap->type,
                                        g_portalConfig->growStartMethod,
                                        g_portalConfig->growElapsedDays);
      }
      growing["guidance"]        = gp_guidance;
      growing["action"]          = gp_action;
      growing["pending_advance"] = g_portalState ? g_portalState->growPhasePendingAdvance
                                                 : false;

      // M1 (11.8.2026): active phase's PE comfort bands, published so the
      // e-ink panel reads them here instead of keeping its own duplicate
      // table (status_targets.h PE_TARGETS_*, docs/kehitys/Fable_kehityspolku.md
      // § M1). Order matches what the e-ink card view already expects (VPD,
      // DLI, CO2, LEAF, PPFD — status_targets.h header comment: "kortit
      // ottavat 4 ensimmaista kelvollista"). device_acts is false for every
      // row in V1: no PE loop is closed yet (light height/hours stay manual,
      // pe-ohjausmalli.md § 3) — hardcoded here rather than threaded through
      // GrowPhaseParams because there is nothing yet that would set it true.
      // -1.0f = no bound in that direction (leaf temp has a ceiling only);
      // MUST match the e-ink's PE_NO_LIMIT sentinel (status_targets.h).
      if (gap) {
        JsonArray targets = growing.createNestedArray("targets");
        auto pmAddTarget = [&targets](const char* field, float lo, float hi) {
          JsonObject t = targets.createNestedObject();
          t["field"]       = field;
          t["lo"]          = lo;
          t["hi"]          = hi;
          t["device_acts"] = false;
        };
        pmAddTarget("vpd",  gap->vpdMinKpa,   gap->vpdMaxKpa);
        pmAddTarget("dli",  gap->dliMinMol,   gap->dliMaxMol);
        pmAddTarget("co2",  gap->co2MinPpm,   gap->co2MaxPpm);
        pmAddTarget("leaf", -1.0f,            gap->leafTempMaxC);
        pmAddTarget("ppfd", gap->ppfdMinUmol, gap->ppfdMaxUmol);
      }

#if ENABLE_GUIDED_GROWING
      // Askelsarja (grow_steps.h, WIZARD: grow-steps). step_count == 0 on
      // sopimus e-inkille: "ei askelta nakyvissa" — joko listaa ei ole,
      // sarja on valmis (index == count) tai laite ei ole GROWING-tilassa
      // (esim. huoltotila: nappi ei kuittaa, joten laatikkoa ei naiteta).
      // step_text on pitka ohje isoon tekstilaatikkoon; const char* firmware-
      // vakiosta -> ArduinoJson ei kopioi sita dokumenttiin (zero-copy).
      GrowStepView sv;
      uint32_t stepElapsedMs = g_portalState
          ? (uint32_t)(millis() - g_portalState->growStepStartMs) : 0;
      bool stepVisible = (g_device.state == DEVICE_GROWING) && g_portalState &&
                         growstep_buildView(gp_plant, g_portalConfig,
                                            stepElapsedMs, &sv);
      growing["step_count"] = stepVisible ? sv.count : 0;
      // Nappivihje paneelille: opastus on kayty loppuun, joten lyhyt painallus
      // siirtaa vaiheen (button_intent_map.h, 3.8.2026). Ilman tata kentta
      // toiminto olisi olemassa mutta nakymaton — juuri se yhdistelma jonka
      // rautahavainto paljasti: laite kysyi "valmis siirtoon?" eika kertonut
      // etta nappi vastaa siihen. step_count == 0 EI riita paneelille, koska
      // se on sama arvo silloinkin kun askelia ei ole lainkaan.
      growing["button_next_phase"] =
          (g_device.state == DEVICE_GROWING) &&
          growstep_sequenceComplete(gp_plant, g_portalConfig);
      if (stepVisible) {
        growing["step_index"]         = sv.index;
        growing["step_text"]          = sv.text;
        growing["step_late"]          = sv.late;
        growing["step_ack_allowed"]   = sv.ackAllowed;
        growing["step_awaiting"]      = sv.awaitingUser;
        growing["step_timer_min"]     = sv.timerMin;
        growing["step_elapsed_sec"]   = sv.elapsedSec;
        growing["step_remaining_sec"] = sv.remainingSec;
      }
      // Demo/katselmointitila: nappi advanssaa askeleet+vaiheet ilman kelloa.
      // Nakyy statuksessa jotta se ei jaa vahingossa paalle huomaamatta.
      growing["demo"] = g_portalConfig ? g_portalConfig->growDemoMode : false;
#endif
      if (gap && gap->durationDays > 0) {
        uint16_t elapsed = g_portalConfig->growElapsedDays;
        growing["days_left"] = (elapsed >= gap->durationDays)
                                 ? 0 : (gap->durationDays - elapsed);
      } else {
        growing["days_left"] = -1;   // -1 = vaihe kestaa toistaiseksi (sadonkorjuu)
      }

      // Actuators — what the device is physically doing right now.
      JsonObject act = doc.createNestedObject("actuators");
      act["lights_on"]    = g_portalState ? g_portalState->lightsOn : false;
      act["air_pump_on"]  = g_portalState ? g_portalState->airPumpOn : false;
      act["pump_running"] = g_portalState ? g_portalState->pumpRunning : false;

#if ENABLE_EBB_FLOW
      // Ebb&Flow + circulation activity — the device's watering behaviour.
      JsonObject ebb = doc.createNestedObject("ebb");
      const char* ebbStateNames[] = {"IDLE", "FLOOD", "SOAK", "DRAIN", "FAULT"};
      uint8_t es = g_portalState ? g_portalState->ebbFlowState : 0;
      ebb["state"]        = (es <= 4) ? ebbStateNames[es] : "UNKNOWN";
      ebb["fault"]        = g_portalState ? g_portalState->ebbFlowFaultLatched : false;
      uint16_t co = g_portalConfig->ebbFloodIntervalMin;
      bool lo = g_portalState ? g_portalState->lightsOn : false;
      uint16_t eff = scheduler_effectiveFloodIntervalMin(gap, co, lo);
      ebb["effective_interval_min"] = eff;
      char frs[80];
      scheduler_floodReasonStr(frs, sizeof(frs), gap, co, lo);
      ebb["flood_reason"] = frs;
      bool growingNow = (g_device.state == DEVICE_GROWING);
      bool floodScheduled = growingNow && g_portalState && (g_portalState->ebbFlowState == 0);
      ebb["next_cycle_sec"] = scheduler_secondsUntilNextFlood(
        floodScheduled ? eff : 0,
        g_portalState ? g_portalState->lastEbbFlowCycleStartMs : 0, millis());
      ebb["circulate_active"]  = g_portalState ? g_portalState->circulateActive : false;
      ebb["circulate_enabled"] = g_portalConfig->ebbCirculateEnabled;
      ebb["next_circulate_sec"] = scheduler_secondsUntilNextCirculate(
        g_portalConfig->ebbCirculateEnabled && growingNow,
        g_portalConfig->ebbCirculateIntervalMin,
        g_portalState ? g_portalState->lastCirculateStartMs : 0, millis());
#endif
    }

    // Developer diagnostics block — consumed by e-ink dev_view.h when
    // ENABLE_EINK_DEV_VIEW=true. Safe for normal clients to ignore.
    JsonObject dev = doc.createNestedObject("dev");
    dev["device_state"] = (int)g_device.state;
    dev["fault_bits"]   = g_device.faults;
    dev["advisory_bits"] = g_device.advisories;
    dev["free_heap"]    = (uint32_t)ESP.getFreeHeap();
    dev["uptime_s"]     = (uint32_t)(millis() / 1000UL);
    dev["wifi_rssi"]    = WiFi.RSSI();

    // 3400 (nostettu 3072:sta 11.8.2026, M1): growing.targets[] lisaa n. 250-
    // 300 tavua serialisoitua JSON-tekstia (5 riviä: field-avain + lo/hi/
    // device_acts). doc siirtyi heapille samassa muutoksessa (ks. yla), joten
    // tama on nyt ainoa taman kasittelijan iso pino-varaus.
    char buf[3400];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
    (void)n;
  });

  // GET /api/device/history — recent state transitions
  // (Separate from /api/history which serves sensor history with ?field= queries)
  g_webServer.on("/api/device/history", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.to<JsonArray>();

    uint8_t count = g_device.historyCount;
    uint8_t start = (g_device.historyIdx + DEVICE_HISTORY_SIZE - count) % DEVICE_HISTORY_SIZE;
    for (uint8_t i = 0; i < count; i++) {
      uint8_t idx = (start + i) % DEVICE_HISTORY_SIZE;
      const DeviceStateTransition& t = g_device.history[idx];
      JsonObject obj = arr.createNestedObject();
      obj["from"]      = device_stateName(t.from);
      obj["to"]        = device_stateName(t.to);
      obj["timestamp"] = t.timestamp;
      obj["faults"]    = t.faults;
    }

    char buf[2048];
    serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });

  // GET /api/intents — list all intent values with names
  g_webServer.on("/api/intents", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;

    StaticJsonDocument<1024> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < INTENT_COUNT; i++) {
      JsonObject obj = arr.createNestedObject();
      obj["id"]   = i;
      obj["name"] = intent_name((Intent)i);
    }

    char buf[1024];
    serializeJson(doc, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });
}

#endif // WIFI_PORTAL_API_STATE_H
