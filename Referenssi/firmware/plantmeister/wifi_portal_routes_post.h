/*=====================================================================
  wifi_portal_routes_post.h - POST route handlers for WiFi portal

  Included by wifi_portal.h after shared portal globals and helpers.

  Split into one portal_register*() function per route group (V3-0,
  docs/kehitys/v3-firmware-suunnitelma.md § 4.1). Each function is
  registered explicitly, in a fixed order, from wifi_portal.h — see the
  ordering comment there before touching that call sequence.
=====================================================================*/

#ifndef WIFI_PORTAL_ROUTES_POST_H
#define WIFI_PORTAL_ROUTES_POST_H

#include "calibration_runtime.h"
#include "grow_step_fsm.h"   // askel-reset kun kasvi/aloitustapa vaihtuu POSTilla
#include "onboarding.h"      // kayttoonoton askelbitit + valmistumisportti
#include "config_reset.h"    // per-nakyman "Palauta oletukset" -whitelist (§8.1)
#include "config_defaults.h" // config_setDefaults: oletusreferenssi resetille

// JSON POST route registrations using AsyncCallbackJsonWebHandler.
static void portal_registerJsonPostRoute(const char* url, ArJsonRequestHandlerFunction handler) {
  auto* h = new AsyncCallbackJsonWebHandler(url, handler);
  h->setMethod(HTTP_POST);
  h->setMaxContentLength(1024);
  g_webServer.addHandler(h);
}

static void portal_registerAuthPostRoutes() {
  portal_registerJsonPostRoute("/api/auth/login",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;
      portal_authHandleLogin(req, payload);
    });

  portal_registerJsonPostRoute("/api/auth/logout",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      portal_authHandleLogout(req);
    });
}

static void portal_registerPlantPostRoutes() {
  // POST /api/plant/phase — update a single grow phase (registered BEFORE /api/plant
  // to ensure the longer URL is matched first by ESPAsyncWebServer handlers)
  portal_registerJsonPostRoute("/api/plant/phase",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      const char* plantId = payload["plant_id"] | "";
      char err[96];
      if (!api_validateConfigPlantId(plantId, err, sizeof(err))) {
        api_sendEnvelope(req, 400, false, false, err, "plant_id");
        return;
      }

      PlantConfig* p = plants_getById(plantId);
      if (!p) {
        api_sendEnvelope(req, 404, false, false, "plant not found", "plant_id");
        return;
      }

      int phaseIdx = payload["phase_index"] | -1;
      if (phaseIdx < 0 || phaseIdx >= (int)p->phaseCount) {
        api_sendEnvelope(req, 400, false, false, "phase_index out of range", "phase_index");
        return;
      }

      GrowPhaseParams& ph = p->phases[phaseIdx];
      if (payload.containsKey("light_hours"))       ph.lightHours       = (uint8_t)payload["light_hours"].as<int>();
      if (payload.containsKey("flood_interval_min")) ph.floodIntervalMin = (uint16_t)payload["flood_interval_min"].as<int>();
      if (payload.containsKey("flood_duration_sec")) ph.floodDurationSec = (uint16_t)payload["flood_duration_sec"].as<int>();
      // PE-tavoitteet (docs/kehitys/pe-ohjausmalli.md). Korvasivat
      // tds_target_ppm/temp_min_c/temp_max_c -kentat 18.7.2026.
      if (payload.containsKey("dli_target_mol"))   ph.dliTargetMol   = (uint8_t)payload["dli_target_mol"].as<int>();
      if (payload.containsKey("ppfd_target_umol")) ph.ppfdTargetUmol = (uint16_t)payload["ppfd_target_umol"].as<int>();
      if (payload.containsKey("vpd_min_kpa"))      ph.vpdMinKpa      = payload["vpd_min_kpa"].as<float>();
      if (payload.containsKey("vpd_max_kpa"))      ph.vpdMaxKpa      = payload["vpd_max_kpa"].as<float>();
      if (payload.containsKey("co2_target_ppm"))   ph.co2TargetPpm   = (uint16_t)payload["co2_target_ppm"].as<int>();
      if (payload.containsKey("leaf_temp_max_c"))  ph.leafTempMaxC   = payload["leaf_temp_max_c"].as<float>();
      if (payload.containsKey("duration_days"))      ph.durationDays     = (uint16_t)payload["duration_days"].as<int>();

      if (!plants_save()) {
        api_sendEnvelope(req, 500, false, false, "failed to save plants");
        return;
      }

      if (g_portalConfig && strcmp(g_portalConfig->currentPlantId, plantId) == 0) {
        g_portalCurrentPlant = p;
      }

      api_sendEnvelope(req, 200, true);
    });

  // POST /api/plant/reset — restore one plant's params + phases to factory
  // default. Registered BEFORE /api/plant (more specific path first).
  portal_registerJsonPostRoute("/api/plant/reset",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      const char* id = payload["id"] | "";
      char err[96];
      if (!api_validateConfigPlantId(id, err, sizeof(err))) {
        api_sendEnvelope(req, 400, false, false, err, "id");
        return;
      }

      if (!plants_resetToDefaultById(id)) {
        api_sendEnvelope(req, 404, false, false, "no factory default for this plant", "id");
        return;
      }
      if (!plants_save()) {
        api_sendEnvelope(req, 500, false, false, "failed to save plants");
        return;
      }
      if (g_portalConfig && strcmp(g_portalConfig->currentPlantId, id) == 0) {
        g_portalCurrentPlant = plants_getById(id);
      }
      api_sendEnvelope(req, 200, true);
    });

  portal_registerJsonPostRoute("/api/plant",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      const char* id = payload["id"] | "";
      char err[96];
      if (!api_validateConfigPlantId(id, err, sizeof(err))) {
        api_sendEnvelope(req, 400, false, false, err, "id");
        return;
      }

      PlantConfig* p = plants_getById(id);
      if (!p) {
        api_sendEnvelope(req, 404, false, false, "plant not found", "id");
        return;
      }

      if (payload.containsKey("max_height_mm"))        p->maxHeightMm = payload["max_height_mm"];
      if (payload.containsKey("light_hours"))           p->lightHours = payload["light_hours"];
      if (payload.containsKey("water_ml_per_dose"))     p->waterMlPerDose = payload["water_ml_per_dose"];
      if (payload.containsKey("water_interval_hours"))  p->waterIntervalHours = payload["water_interval_hours"];
      // Litteat PE-kentat poistettiin 18.7.2026 — PE-tavoitteet elavat per
      // vaihe (/api/plant/phase), eivat plant-tasossa (pe-ohjausmalli.md).

      if (!plants_save()) {
        api_sendEnvelope(req, 500, false, false, "failed to save plants");
        return;
      }

      // If this is the active plant, update config.
      if (g_portalConfig && strcmp(g_portalConfig->currentPlantId, id) == 0) {
        g_portalCurrentPlant = p;
      }

      api_sendEnvelope(req, 200, true);
    });
}

static void portal_registerConfigPostRoutes() {
  // POST /api/config/reset — per-nakyman "Palauta oletukset" (§8.1,
  // docs/kehitys/web-ui-uudelleensuunnittelu.md). Body: {"keys":[...]}.
  // Kentat kopioidaan config_setDefaults()-referenssista (config_reset.h
  // whitelist) — oletusarvoja ei ole kirjoitettu tanne eika JS:aan.
  // Validoi-ensin-kirjoita-sitten: yksi tuntematon/kielletty avain hylkaa
  // KOKO pyynnon 400:lla eika mitaan muuteta — osittainen palautus ei ole
  // tila jota kukaan pyysi.
  //
  // Rekisteroity ENNEN /api/config:ia (samalla periaatteella kuin
  // /api/plant/phase ennen /api/plant:ia): pidempi/tarkempi polku ensin,
  // ettei /api/config nappaa /api/config/reset-pyyntoja itselleen
  // (check_invariants.py I7, docs/kehitys/v3-firmware-suunnitelma.md § 4.1b).
  portal_registerJsonPostRoute("/api/config/reset",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalConfig) {
        api_sendEnvelope(req, 500, false, false, "config not ready");
        return;
      }
      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      JsonArrayConst keys = payload["keys"].as<JsonArrayConst>();
      if (keys.isNull() || keys.size() == 0 || keys.size() > 24) {
        api_sendEnvelope(req, 400, false, false, "keys must be a non-empty array", "keys");
        return;
      }

      DeviceConfig defs = {};
      config_setDefaults(&defs);
      DeviceConfig scratch = *g_portalConfig;
      for (JsonVariantConst k : keys) {
        const char* key = k.as<const char*>();
        if (!key || !config_resetField(&scratch, &defs, key)) {
          char err[64];
          snprintf(err, sizeof(err), "key not resettable: %s", key ? key : "(not a string)");
          api_sendEnvelope(req, 400, false, false, err, "keys");
          return;
        }
      }
      *g_portalConfig = scratch;

      if (!config_save(g_portalConfig)) {
        api_sendEnvelope(req, 500, false, false, "failed to save config");
        return;
      }
      DEBUG_PRINTF("[INFO]  Portal: view reset restored %u field(s) to defaults\n",
                   (unsigned)keys.size());
      api_sendEnvelope(req, 200, true);
    });

  // POST /api/config — update device config
  portal_registerJsonPostRoute("/api/config",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      if (!g_portalConfig) {
        api_sendEnvelope(req, 500, false, false, "no config");
        return;
      }

      char err[96];
      bool pinChanged = false;

      if (payload.containsKey("lora_address")) {
        if (!payload["lora_address"].is<int>()) {
          api_sendEnvelope(req, 400, false, false, "lora_address must be integer", "lora_address");
          return;
        }
        int addr = payload["lora_address"].as<int>();
        if (!api_validateConfigLoraAddress(addr, err, sizeof(err))) {
          api_sendEnvelope(req, 400, false, false, err, "lora_address");
          return;
        }
        g_portalConfig->loraAddress = (uint8_t)addr;
      }

      if (payload.containsKey("lora_network_id")) {
        if (!payload["lora_network_id"].is<int>()) {
          api_sendEnvelope(req, 400, false, false, "lora_network_id must be integer", "lora_network_id");
          return;
        }
        int networkId = payload["lora_network_id"].as<int>();
        if (!api_validateConfigLoraNetworkId(networkId, err, sizeof(err))) {
          api_sendEnvelope(req, 400, false, false, err, "lora_network_id");
          return;
        }
        g_portalConfig->loraNetworkId = (uint8_t)networkId;
      }

      if (payload.containsKey("lora_target")) {
        if (!payload["lora_target"].is<int>()) {
          api_sendEnvelope(req, 400, false, false, "lora_target must be integer", "lora_target");
          return;
        }
        int target = payload["lora_target"].as<int>();
        if (!api_validateConfigLoraTarget(target, err, sizeof(err))) {
          api_sendEnvelope(req, 400, false, false, err, "lora_target");
          return;
        }
        g_portalConfig->loraTargetAddress = (uint8_t)target;
      }

      // WiFi STA
      if (payload.containsKey("wifi_ssid")) {
        if (!payload["wifi_ssid"].is<const char*>()) {
          api_sendEnvelope(req, 400, false, false, "wifi_ssid must be string", "wifi_ssid");
          return;
        }
        const char* ssid = payload["wifi_ssid"] | "";
        if (!api_validateConfigWifiSsid(ssid, err, sizeof(err))) {
          api_sendEnvelope(req, 400, false, false, err, "wifi_ssid");
          return;
        }
        strncpy(g_portalConfig->wifiSsid, ssid, sizeof(g_portalConfig->wifiSsid) - 1);
        g_portalConfig->wifiSsid[sizeof(g_portalConfig->wifiSsid) - 1] = '\0';
        // Onboarding step 1 (optional): a non-empty SSID means the user did the
        // home-network step. Clearing it un-ticks the step so the banner never
        // claims something the config does not have.
        if (strlen(g_portalConfig->wifiSsid) > 0) {
          g_portalConfig->onboardingSteps |= ONBOARD_STEP_WIFI;
        } else {
          g_portalConfig->onboardingSteps &= (uint8_t)~ONBOARD_STEP_WIFI;
        }
      }

      if (payload.containsKey("wifi_password")) {
        if (!payload["wifi_password"].is<const char*>()) {
          api_sendEnvelope(req, 400, false, false, "wifi_password must be string", "wifi_password");
          return;
        }

        const char* password = payload["wifi_password"] | "";
        if (!api_isMaskedSentinel(password)) {
          if (!api_validateConfigWifiPassword(password, err, sizeof(err))) {
            api_sendEnvelope(req, 400, false, false, err, "wifi_password");
            return;
          }
          strncpy(g_portalConfig->wifiPassword, password, sizeof(g_portalConfig->wifiPassword) - 1);
          g_portalConfig->wifiPassword[sizeof(g_portalConfig->wifiPassword) - 1] = '\0';
        }
      }

      if (payload.containsKey("wifi_auto_connect")) {
        if (!(payload["wifi_auto_connect"].is<bool>() || payload["wifi_auto_connect"].is<int>())) {
          api_sendEnvelope(req, 400, false, false, "wifi_auto_connect must be bool or 0/1", "wifi_auto_connect");
          return;
        }

        int autoConnect = payload["wifi_auto_connect"].as<int>();
        if (autoConnect != 0 && autoConnect != 1) {
          api_sendEnvelope(req, 400, false, false, "wifi_auto_connect must be 0 or 1", "wifi_auto_connect");
          return;
        }

        g_portalConfig->wifiAutoConnect = (autoConnect == 1);

        // Immediately attempt STA connection if enabled
        if (g_portalConfig->wifiAutoConnect && strlen(g_portalConfig->wifiSsid) > 0) {
          WiFi.begin(g_portalConfig->wifiSsid, g_portalConfig->wifiPassword);
          DEBUG_PRINTF("[INFO]  WiFi STA: Attempting connection to %s...\n", g_portalConfig->wifiSsid);
        }
      }

      if (payload.containsKey("admin_pin")) {
        if (!payload["admin_pin"].is<const char*>()) {
          api_sendEnvelope(req, 400, false, false, "admin_pin must be string", "admin_pin");
          return;
        }

        const char* pin = payload["admin_pin"] | "";
        if (!api_isMaskedSentinel(pin)) {
          if (!api_validateAdminPin(pin, true, err, sizeof(err))) {
            api_sendEnvelope(req, 400, false, false, err, "admin_pin");
            return;
          }

          if (strcmp(g_portalConfig->adminPin, pin) != 0) {
            strncpy(g_portalConfig->adminPin, pin, sizeof(g_portalConfig->adminPin) - 1);
            g_portalConfig->adminPin[sizeof(g_portalConfig->adminPin) - 1] = '\0';
            pinChanged = true;
          }
          // Onboarding step 3 (optional): an empty PIN is a valid choice (PIN
          // off), so the step ticks on any deliberate save, not only a set PIN.
          g_portalConfig->onboardingSteps |= ONBOARD_STEP_PIN;
        }
      }

      // Display / plant
      if (payload.containsKey("light_on_hour")) {
        if (!payload["light_on_hour"].is<int>()) {
          api_sendEnvelope(req, 400, false, false, "light_on_hour must be integer", "light_on_hour");
          return;
        }
        int lightOnHour = payload["light_on_hour"].as<int>();
        if (!api_validateConfigLightOnHour(lightOnHour, err, sizeof(err))) {
          api_sendEnvelope(req, 400, false, false, err, "light_on_hour");
          return;
        }
        g_portalConfig->lightOnHour = lightOnHour;
      }

      if (payload.containsKey("plant_id")) {
        if (!payload["plant_id"].is<const char*>()) {
          api_sendEnvelope(req, 400, false, false, "plant_id must be string", "plant_id");
          return;
        }
        const char* plantId = payload["plant_id"] | "";
        if (!api_validateConfigPlantId(plantId, err, sizeof(err))) {
          api_sendEnvelope(req, 400, false, false, err, "plant_id");
          return;
        }

        PlantConfig* selectedPlant = plants_getById(plantId);
        if (!selectedPlant) {
          api_sendEnvelope(req, 404, false, false, "plant not found", "plant_id");
          return;
        }

        // Askel-reset vain kun kasvi OIKEASTI vaihtuu: sama config-POST
        // uudelleen (esim. kayttoonoton tallennus) ei saa nollata kesken
        // olevaa askelsarjaa (reset-paikka 4/5, grow_step_fsm.h).
        bool plantChanged =
            (strcmp(g_portalConfig->currentPlantId, plantId) != 0);

        strncpy(g_portalConfig->currentPlantId, plantId,
                sizeof(g_portalConfig->currentPlantId) - 1);
        g_portalConfig->currentPlantId[sizeof(g_portalConfig->currentPlantId) - 1] = '\0';

        // Update active plant pointer
        g_portalCurrentPlant = selectedPlant;

        if (plantChanged) {
          grow_resetStepProgress(g_portalState, g_portalConfig, millis());
        }

        // Onboarding step 2a (required). Ticked on every save, not only when
        // the id changes: keeping the default plant (basil) is a deliberate
        // answer and must satisfy the gate — see ONBOARD_STEP_METHOD below.
        g_portalConfig->onboardingSteps |= ONBOARD_STEP_PLANT;
      }

      // Aloitustapa (0 = pistokas, 1 = siemen, 2 = kaupan taimi).
      //
      // Asetetaan tassa eika vain GROW_START:n arvona, koska kayttoonotossa
      // kysytaan "mista aloitit" ennen kuin kasvatusta on aloitettu. Nappi
      // lukee taman configista (plantmeister.ino) — ilman tata polkua napin
      // painallus jaisi aina oletukseen 0 ja siemenen kylvanyt kayttaja saisi
      // pistokasohjeet. Kuuluu plant_id:n viereen: yhdessa ne paattavat mista
      // vaiheesta kasvatus alkaa ja mita ohjetta kayttaja lukee.
      if (payload.containsKey("grow_start_method")) {
        if (!payload["grow_start_method"].is<int>()) {
          api_sendEnvelope(req, 400, false, false,
                           "grow_start_method must be integer", "grow_start_method");
          return;
        }
        int sm = payload["grow_start_method"].as<int>();
        if (sm < 0 || sm > 2) {
          api_sendEnvelope(req, 400, false, false,
                           "grow_start_method must be 0..2", "grow_start_method");
          return;
        }
        // Aloitustapa valitsee askellistan -> muutos nollaa askeleet (reset-
        // paikka 5/5, grow_step_fsm.h). Vain oikeasta muutoksesta: muuten
        // elava indeksi voisi osoittaa lyhyemman listan ulkopuolelle.
        if (g_portalConfig->growStartMethod != (uint8_t)sm) {
          g_portalConfig->growStartMethod = (uint8_t)sm;
          grow_resetStepProgress(g_portalState, g_portalConfig, millis());
        }
        // Onboarding step 2b (required). Ticked on every save, NOT only inside
        // the "value changed" branch above: choosing the default (0 = cutting)
        // is a deliberate answer to "mistä aloitat?" and must satisfy the gate.
        // Inside the branch it never would, and the user could not finish.
        g_portalConfig->onboardingSteps |= ONBOARD_STEP_METHOD;
      }

      // Physical button action (ButtonAction enum, 0..BUTTON_ACTION_MAX)
      if (payload.containsKey("button_action")) {
        if (!payload["button_action"].is<int>()) {
          api_sendEnvelope(req, 400, false, false, "button_action must be integer", "button_action");
          return;
        }
        int ba = payload["button_action"].as<int>();
        if (ba < 0 || ba > BUTTON_ACTION_MAX) {
          api_sendEnvelope(req, 400, false, false, "button_action out of range", "button_action");
          return;
        }
        g_portalConfig->buttonAction = (uint8_t)ba;
      }

      // Pitkan painalluksen merkitys (ButtonLongAction: 0 = peruuta askel/vaihe
      // (oletus), 1 = huoltotila-toggle (legacy)). Kayttajan valinta 22.7.2026:
      // huoltotila siirtyi puhelimelle, nappi peruuttaa opastuksen taaksepain.
      if (payload.containsKey("button_long_action")) {
        if (!payload["button_long_action"].is<int>()) {
          api_sendEnvelope(req, 400, false, false,
                           "button_long_action must be integer", "button_long_action");
          return;
        }
        int bla = payload["button_long_action"].as<int>();
        if (bla < 0 || bla > BUTTON_LONG_ACTION_MAX) {
          api_sendEnvelope(req, 400, false, false,
                           "button_long_action out of range", "button_long_action");
          return;
        }
        g_portalConfig->buttonLongAction = (uint8_t)bla;
      }

      // Ebb&Flow timing overrides (0 = use default). Bounds mirror config_clampBounds().
      struct EbbField { const char* key; uint16_t* dst; int maxVal; };
      EbbField ebbFields[] = {
        {"ebb_flood_interval_min", &g_portalConfig->ebbFloodIntervalMin, 1440},
        {"ebb_flood_duration_sec", &g_portalConfig->ebbFloodDurationSec, 600},
        {"ebb_soak_duration_sec",  &g_portalConfig->ebbSoakDurationSec,  3600},
        {"ebb_drain_timeout_sec",  &g_portalConfig->ebbDrainTimeoutSec,  3600},
        {"ebb_circulate_interval_min", &g_portalConfig->ebbCirculateIntervalMin, 1440},
        {"ebb_circulate_duration_sec", &g_portalConfig->ebbCirculateDurationSec, 290},
      };
      for (auto& ef : ebbFields) {
        if (!payload.containsKey(ef.key)) continue;
        if (!payload[ef.key].is<int>()) {
          api_sendEnvelope(req, 400, false, false, "ebb timing must be integer", ef.key);
          return;
        }
        int v = payload[ef.key].as<int>();
        if (v < 0 || v > ef.maxVal) {
          api_sendEnvelope(req, 400, false, false, "ebb timing out of range", ef.key);
          return;
        }
        *ef.dst = (uint16_t)v;
      }

      // Soak-hold duty (uint8, 0 = legacy pump-off soak). Clamped to the stall
      // floor so a too-low manual value can never command a stalling duty.
      if (payload.containsKey("ebb_soak_pwm_pct")) {
        if (!payload["ebb_soak_pwm_pct"].is<int>()) {
          api_sendEnvelope(req, 400, false, false, "ebb_soak_pwm_pct must be integer", "ebb_soak_pwm_pct");
          return;
        }
        int v = payload["ebb_soak_pwm_pct"].as<int>();
        if (v < 0 || v > 100) {
          api_sendEnvelope(req, 400, false, false, "ebb_soak_pwm_pct out of range (0-100)", "ebb_soak_pwm_pct");
          return;
        }
        g_portalConfig->ebbSoakPwmPct = calibration_clampSoakDutyPct(v);
      }

      // Opt-in overflow auto-clear (bool). ON = FLOAT_OVF latch releases a settle
      // period after the bed drains (unattended recovery); OFF (default) = latch
      // cleared only manually. See OVERFLOW_AUTO_CLEAR_SETTLE_MS.
      if (payload.containsKey("ebb_overflow_auto_clear")) {
        if (!payload["ebb_overflow_auto_clear"].is<bool>()) {
          api_sendEnvelope(req, 400, false, false, "ebb_overflow_auto_clear must be boolean", "ebb_overflow_auto_clear");
          return;
        }
        g_portalConfig->ebbOverflowAutoClear = payload["ebb_overflow_auto_clear"].as<bool>();
      }

      // Circulation ("kierto") opt-in (bool). ON = low-level anti-stagnation pump
      // cycle runs every ebb_circulate_interval_min while GROWING + ebb IDLE.
      if (payload.containsKey("ebb_circulate_enabled")) {
        if (!payload["ebb_circulate_enabled"].is<bool>()) {
          api_sendEnvelope(req, 400, false, false, "ebb_circulate_enabled must be boolean", "ebb_circulate_enabled");
          return;
        }
        g_portalConfig->ebbCirculateEnabled = payload["ebb_circulate_enabled"].as<bool>();
      }

      // Circulation duty (uint8): deliberately LOW, separate from soak. Same stall-
      // floor clamp as soak so a too-low value can never command a stalling duty.
      if (payload.containsKey("ebb_circulate_duty_pct")) {
        if (!payload["ebb_circulate_duty_pct"].is<int>()) {
          api_sendEnvelope(req, 400, false, false, "ebb_circulate_duty_pct must be integer", "ebb_circulate_duty_pct");
          return;
        }
        int v = payload["ebb_circulate_duty_pct"].as<int>();
        if (v < 0 || v > 100) {
          api_sendEnvelope(req, 400, false, false, "ebb_circulate_duty_pct out of range (0-100)", "ebb_circulate_duty_pct");
          return;
        }
        g_portalConfig->ebbCirculateDutyPct = calibration_clampSoakDutyPct(v);
      }

      // Dev mode: Huolto-valilehden nakyvyys. Vaihdetaan portaalista 5x
      // naputuksella "Online"-tilakenttaan. Persistoituu (schema v5).
      if (payload.containsKey("dev_mode")) {
        if (!payload["dev_mode"].is<bool>()) {
          api_sendEnvelope(req, 400, false, false, "dev_mode must be boolean", "dev_mode");
          return;
        }
        g_portalConfig->devMode = payload["dev_mode"].as<bool>();
      }

      if (!config_save(g_portalConfig)) {
        api_sendEnvelope(req, 500, false, false, "failed to save config");
        return;
      }

      if (pinChanged) {
        portal_authClearSessions();
      }

      api_sendEnvelope(req, 200, true);
    });
}

#if ENABLE_GUIDED_GROWING
static void portal_registerGrowPostRoutes() {
  // POST /api/grow/start — starts guided grow cycle with selected method
  portal_registerJsonPostRoute("/api/grow/start",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      int startMethod = payload["start_method"] | 0;
      char err[96];
      if (!api_validateGrowStartMethod(startMethod, err, sizeof(err))) {
        api_sendEnvelope(req, 400, false, false, err, "start_method");
        return;
      }

      char valBuf[8];
      snprintf(valBuf, sizeof(valBuf), "%d", startMethod);

      if (!portal_dispatchCommand(req, "GROW_START", valBuf)) return;
      api_sendEnvelope(req, 200, true);
    });

  // POST /api/grow/next — user accepts pending phase advance
  portal_registerJsonPostRoute("/api/grow/next",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!portal_dispatchCommand(req, "GROW_NEXT", "")) return;
      api_sendEnvelope(req, 200, true);
    });

  // POST /api/grow/delay — user postpones pending phase advance
  portal_registerJsonPostRoute("/api/grow/delay",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!portal_dispatchCommand(req, "GROW_DELAY", "")) return;
      api_sendEnvelope(req, 200, true);
    });

  // POST /api/grow/stop — stop guided grow cycle
  portal_registerJsonPostRoute("/api/grow/stop",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!portal_dispatchCommand(req, "GROW_STOP", "")) return;
      api_sendEnvelope(req, 200, true);
    });

  // POST /api/grow/simulate — debug simulation phase/day override
  portal_registerJsonPostRoute("/api/grow/simulate",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
#if ENABLE_GUIDED_GROWING_UI && ENABLE_GUIDED_GROWING_SIMULATION
      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      bool clear = payload["clear"] | false;
      if (clear) {
        g_portalSimPhaseOverride = -1;
        g_portalSimDayOverride = -1;
      } else {
        PlantConfig* simPlant = g_portalCurrentPlant;
        if (!simPlant && g_portalConfig) {
          simPlant = plants_getById(g_portalConfig->currentPlantId);
        }

        if (!simPlant || simPlant->phaseCount == 0) {
          api_sendEnvelope(req, 400, false, false, "plant has no phases");
          return;
        }

        int phase = payload.containsKey("phase") ? (int)payload["phase"] : (int)g_portalSimPhaseOverride;
        int day = payload.containsKey("day") ? (int)payload["day"] : (int)g_portalSimDayOverride;

        if (phase < 0 || phase >= simPlant->phaseCount) {
          api_sendEnvelope(req, 400, false, false, "phase out of range", "phase");
          return;
        }
        if (day <= 0) day = 1;

        g_portalSimPhaseOverride = (int8_t)phase;
        g_portalSimDayOverride = (int16_t)day;
      }

      StaticJsonDocument<128> out;
      out["ok"] = true;
      out["phase"] = g_portalSimPhaseOverride;
      out["day"] = g_portalSimDayOverride;
      char outBuf[128];
      serializeJson(out, outBuf, sizeof(outBuf));
      req->send(200, "application/json", outBuf);
#else
      (void)json;
      api_sendEnvelope(req, 400, false, false, "simulation disabled");
#endif
    });

  // POST /api/grow — generic action fallback (start/next/delay/stop)
  portal_registerJsonPostRoute("/api/grow",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      const char* action = payload["action"] | "";
      char err[96];
      if (!api_validateGrowAction(action, err, sizeof(err))) {
        api_sendEnvelope(req, 400, false, false, err, "action");
        return;
      }

      if (strcmp(action, "start") == 0) {
        int startMethod = payload["start_method"] | 0;
        if (!api_validateGrowStartMethod(startMethod, err, sizeof(err))) {
          api_sendEnvelope(req, 400, false, false, err, "start_method");
          return;
        }
        char valBuf[8];
        snprintf(valBuf, sizeof(valBuf), "%d", startMethod);
        if (!portal_dispatchCommand(req, "GROW_START", valBuf)) return;
      } else if (strcmp(action, "next") == 0) {
        if (!portal_dispatchCommand(req, "GROW_NEXT", "")) return;
      } else if (strcmp(action, "delay") == 0) {
        if (!portal_dispatchCommand(req, "GROW_DELAY", "")) return;
      } else if (strcmp(action, "stop") == 0) {
        if (!portal_dispatchCommand(req, "GROW_STOP", "")) return;
      }

      api_sendEnvelope(req, 200, true);
    });
}
#else
static void portal_registerGrowPostRoutes() {}
#endif // ENABLE_GUIDED_GROWING

static void portal_registerModePostRoute() {
  // POST /api/mode — runtime Normal/Test switch.
  // Body: {"test_mode": true|false}. Test mode keeps the AP/web server up
  // indefinitely so manual actuator testing from the phone is not cut off
  // by the 15 min WIFI_AP_DURATION timeout. Setting persists across reboots.
  portal_registerJsonPostRoute("/api/mode",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      if (!g_portalConfig) {
        api_sendEnvelope(req, 500, false, false, "no config");
        return;
      }
      if (!payload.containsKey("test_mode")) {
        api_sendEnvelope(req, 400, false, false, "missing test_mode", "test_mode");
        return;
      }
      g_portalConfig->testMode = (bool)payload["test_mode"];
      config_save(g_portalConfig);
      DEBUG_PRINTF("[INFO]  Portal: test_mode=%s\n", g_portalConfig->testMode ? "on" : "off");
      api_sendEnvelope(req, 200, true);
    });
}

static void portal_registerOnboardingPostRoute() {
  // POST /api/onboarding/complete — first-run setup finished (empty body {}).
  // Persists onboardingComplete=true: the portal banner and the e-ink join-AP
  // screen disappear. Deliberately does NOT touch growActive — onboarding ends
  // in a confirmed safe IDLE, and growing starts only from an explicit user
  // action (button K-A / portal). An AP-only offline setup calls this with
  // wifiSsid left empty; wifi_portal.h then keeps the AP up indefinitely.
  //
  // GATE (onboarding.h): rejects while a required step is missing. The gate
  // lives here, not only in the browser, because a disabled button is a
  // usability affordance while the API is the truth — and because this route
  // used to accept anything: one click marked setup done with nothing chosen,
  // the banner vanished for good (only a factory reset brings it back) and the
  // user was left in a bare IDLE. The missing steps are named in the response
  // so the caller can always say WHY, never just "no".
  portal_registerJsonPostRoute("/api/onboarding/complete",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      (void)json;

      if (!g_portalConfig) {
        api_sendEnvelope(req, 500, false, false, "no config");
        return;
      }

      if (!onboarding_canComplete(g_portalConfig->onboardingSteps)) {
        uint8_t missing = onboarding_missingRequired(g_portalConfig->onboardingSteps);
        char err[96];
        snprintf(err, sizeof(err), "onboarding incomplete: %s%s%s",
                 (missing & ONBOARD_STEP_PLANT)  ? "kasvi valitsematta" : "",
                 (missing == ONBOARD_REQUIRED_MASK) ? ", " : "",
                 (missing & ONBOARD_STEP_METHOD) ? "aloitustapa valitsematta" : "");
        DEBUG_PRINTF("[WARN]  Portal: onboarding complete rejected (missing=0x%02X)\n",
                     (unsigned)missing);
        api_sendEnvelope(req, 400, false, false, err, "onboarding_steps");
        return;
      }

      g_portalConfig->onboardingComplete = true;
      config_save(g_portalConfig);
      DEBUG_INFO(F("Portal: onboarding complete — device stays in safe IDLE"));
      api_sendEnvelope(req, 200, true);
    });
}

static void portal_registerCommandPostRoute() {
  // POST /api/command — manual controls
  portal_registerJsonPostRoute("/api/command",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      const char* cmd = payload["cmd"] | "";
      const char* val = payload["value"] | "";

      if (!portal_dispatchCommand(req, cmd, val)) return;

      api_sendEnvelope(req, 200, true);
    });
}

#if ENABLE_PUMP
static void portal_registerPumpCalibRoutes() {
  // WIZARD: pump-throughput — ks. docs/ohjeet/wizardit.md
  portal_registerJsonPostRoute("/api/calib/pump/test",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;

      if (!g_portalState || g_portalState->ebbFlowState != EBB_STATE_IDLE || pump_isRunning()) {
        api_sendEnvelope(req, 409, false, false, "pump busy");
        return;
      }

      if (!pump_startForMs(10000UL)) {
        api_sendEnvelope(req, 500, false, false, "pump test failed");
        return;
      }

      if (g_portalState) {
        g_portalState->pumpRunning = true;
        g_portalState->lastWaterDose = millis();
      }

      api_sendEnvelope(req, 200, true);
    });

  // POST /api/calib/pump/start — kalibrointi-wizardin pumppu-step.
  // Body: {"durationMs": <100..PUMP_ABSOLUTE_MAX_ON_MS>}.
  // Toisin kuin /api/calib/pump/test (kova 10 s), tama hyvaksyy kayttajan
  // valitseman keston jonka jalkeen kayttaja mittaa ml:t ja kutsuu /save.
  portal_registerJsonPostRoute("/api/calib/pump/start",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      if (!g_portalState || g_portalState->ebbFlowState != EBB_STATE_IDLE || pump_isRunning()) {
        api_sendEnvelope(req, 409, false, false, "pump busy");
        return;
      }

      if (!payload.containsKey("durationMs") || !payload["durationMs"].is<uint32_t>()) {
        api_sendEnvelope(req, 400, false, false, "durationMs must be uint", "durationMs");
        return;
      }
      uint32_t durationMs = payload["durationMs"].as<uint32_t>();
      if (durationMs < 100UL || durationMs > (uint32_t)PUMP_ABSOLUTE_MAX_ON_MS) {
        api_sendEnvelope(req, 400, false, false,
                         "durationMs out of range (100..PUMP_ABSOLUTE_MAX_ON_MS)",
                         "durationMs");
        return;
      }

      if (!pump_startForMs((unsigned long)durationMs)) {
        api_sendEnvelope(req, 500, false, false, "pump start failed");
        return;
      }

      if (g_portalState) {
        g_portalState->pumpRunning = true;
        g_portalState->lastWaterDose = millis();
      }

      StaticJsonDocument<96> out;
      out["ok"] = true;
      out["restartRequired"] = false;
      out["durationMs"] = durationMs;
      char buf[96];
      serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
    });

  // POST /api/calib/pump/save — kalibrointi-wizardin valmistusstep.
  // Body: {"measuredMl": <float>, "durationMs": <uint>}.
  // Laskee uuden pumpMlPerSec:n ja tallentaa kalibrointiin.
  portal_registerJsonPostRoute("/api/calib/pump/save",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalCalibration) {
        api_sendEnvelope(req, 500, false, false, "no calibration");
        return;
      }

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      if (!payload.containsKey("measuredMl") || !payload["measuredMl"].is<float>()) {
        api_sendEnvelope(req, 400, false, false, "measuredMl must be number", "measuredMl");
        return;
      }
      if (!payload.containsKey("durationMs") || !payload["durationMs"].is<uint32_t>()) {
        api_sendEnvelope(req, 400, false, false, "durationMs must be uint", "durationMs");
        return;
      }

      float measuredMl = payload["measuredMl"].as<float>();
      uint32_t durationMs = payload["durationMs"].as<uint32_t>();

      float newMlPerSec = calibration_computePumpMlPerSec(measuredMl, durationMs);
      if (newMlPerSec <= 0.0f) {
        api_sendEnvelope(req, 400, false, false, "invalid measurement (would yield <= 0)");
        return;
      }

      float oldMlPerSec = g_portalCalibration->pumpMlPerSec;
      g_portalCalibration->pumpMlPerSec = newMlPerSec;
      calibration_clampBounds(g_portalCalibration);

      if (!calibration_save(g_portalCalibration)) {
        api_sendEnvelope(req, 500, false, false, "failed to save calibration");
        return;
      }

      // Paivita pumpHal:n runtime-arvo niin ettei tarvitse rebootta.
      pump_setMlPerSec(g_portalCalibration->pumpMlPerSec);

      StaticJsonDocument<160> out;
      out["ok"] = true;
      out["restartRequired"] = false;
      out["oldMlPerSec"] = oldMlPerSec;
      out["newMlPerSec"] = g_portalCalibration->pumpMlPerSec;
      char buf[160];
      serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);

      DEBUG_PRINTF("[INFO]  Calib: pump ml/s %.3f -> %.3f (measured %.2f ml / %lu ms)\n",
                   oldMlPerSec, g_portalCalibration->pumpMlPerSec,
                   measuredMl, (unsigned long)durationMs);
    });
}
#else
static void portal_registerPumpCalibRoutes() {}
#endif // ENABLE_PUMP

#if ENABLE_MOTOR
static void portal_registerMotorCalibRoutes() {
  // WIZARD: motor-limits — ks. docs/ohjeet/wizardit.md
  portal_registerJsonPostRoute("/api/calib/motor/probe_up",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState || g_portalState->ebbFlowState != EBB_STATE_IDLE) {
        api_sendEnvelope(req, 409, false, false, "ebbflow not idle");
        return;
      }
      if (motor_isMoving()) {
        api_sendEnvelope(req, 409, false, false, "motor busy");
        return;
      }
      motor_moveTo(MOTOR_MAX_HEIGHT_MM);
      api_sendEnvelope(req, 200, true);
    });

  portal_registerJsonPostRoute("/api/calib/motor/probe_down",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState || g_portalState->ebbFlowState != EBB_STATE_IDLE) {
        api_sendEnvelope(req, 409, false, false, "ebbflow not idle");
        return;
      }
      if (motor_isMoving()) {
        api_sendEnvelope(req, 409, false, false, "motor busy");
        return;
      }
      motor_moveTo(0);
      api_sendEnvelope(req, 200, true);
    });

  // POST /api/calib/motor/capture_limit — tallentaa moottorin nykyisen
  // step-aseman ylä- tai alarajaksi. Body: {"side":"up"|"down"}.
  // Käyttö: aja moottori manuaalisesti haluttuun ääriasentoon, sitten kutsu.
  portal_registerJsonPostRoute("/api/calib/motor/capture_limit",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalCalibration) {
        api_sendEnvelope(req, 500, false, false, "no calibration");
        return;
      }
      if (motor_isMoving()) {
        api_sendEnvelope(req, 409, false, false, "motor busy");
        return;
      }

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      const char* side = payload["side"] | "";
      bool isUp;
      if (strcmp(side, "up") == 0) {
        isUp = true;
      } else if (strcmp(side, "down") == 0) {
        isUp = false;
      } else {
        api_sendEnvelope(req, 400, false, false, "side must be \"up\" or \"down\"", "side");
        return;
      }

      long curSteps = motor_getPositionSteps();
      if (curSteps < 0) curSteps = 0;
      if (curSteps > 100000) curSteps = 100000;

      if (isUp) {
        g_portalCalibration->motorStepsUp = (int32_t)curSteps;
      } else {
        g_portalCalibration->motorStepsDown = (int32_t)curSteps;
      }

      calibration_clampBounds(g_portalCalibration);
      motor_setStepLimits(g_portalCalibration->motorStepsDown,
                          g_portalCalibration->motorStepsUp);

      if (!calibration_save(g_portalCalibration)) {
        api_sendEnvelope(req, 500, false, false, "failed to save calibration");
        return;
      }

      StaticJsonDocument<128> out;
      out["ok"] = true;
      out["restartRequired"] = false;
      out["side"] = side;
      out["steps"] = curSteps;
      char buf[128];
      serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);

      DEBUG_PRINTF("[INFO]  Calib: %s limit captured at %ld steps\n",
                   side, curSteps);
    });
}
#else
static void portal_registerMotorCalibRoutes() {}
#endif // ENABLE_MOTOR

// ── Ebb&Flow fill + soak-hold calibration ──────────────────────────
// WIZARD: ebb-fill-drain, pump-soak — ks. docs/ohjeet/wizardit.md
// fill/drain: time-based capture (vs motorin step-asema): pumppu käy,
// käyttäjä merkitsee "täysi" ja "tyhjä", ja kuluneet sekunnit kirjautuvat
// flood/drain-config-kenttiin. soak-hold: käyttäjä nostaa veden, painaa
// "pidä" → pumppu putoaa matalalle soak-dutylle, hienosäätää +/- (delta)
// tai numerolla kunnes pinta pysyy paikallaan, tallentaa. Molemmat
// kirjoittavat olemassa oleviin DeviceConfig-kenttiin.
#if ENABLE_EBB_FLOW && ENABLE_PUMP
static void portal_registerEbbSoakCalibRoutes() {
  // POST /api/calib/ebb/fill_start — käynnistä pumppu, aloita täytön ajastus.
  portal_registerJsonPostRoute("/api/calib/ebb/fill_start",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState) { api_sendEnvelope(req, 500, false, false, "no state"); return; }

      // Safety cap: pump auto-stops at hard max even if user never marks "full".
      if (!pump_startForMs(PUMP_ABSOLUTE_MAX_ON_MS)) {
        api_sendEnvelope(req, 409, false, false, "pump start refused");
        return;
      }
      g_portalState->ebbCalibPhase = EBB_CALIB_FILLING;
      g_portalState->ebbCalibMarkMs = millis();
      g_portalState->pumpRunning = true;
      DEBUG_INFO(F("EbbCalib: fill started — mark 'full' at target level"));
      api_sendEnvelope(req, 200, true);
    });

  // POST /api/calib/ebb/capture_full — pysäytä pumppu, tallenna täyttöaika,
  // aloita tyhjenemisen ajastus.
  portal_registerJsonPostRoute("/api/calib/ebb/capture_full",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState || !g_portalConfig) {
        api_sendEnvelope(req, 500, false, false, "no state"); return;
      }
      if (g_portalState->ebbCalibPhase != EBB_CALIB_FILLING) {
        api_sendEnvelope(req, 409, false, false, "not filling — call fill_start first");
        return;
      }

      pump_stop();
      g_portalState->pumpRunning = false;
      unsigned long fillMs = millis() - g_portalState->ebbCalibMarkMs;
      uint32_t fillSec = (uint32_t)((fillMs + 500) / 1000);   // round to nearest s
      if (fillSec < 1) fillSec = 1;
      if (fillSec > 600) fillSec = 600;                       // matches config clamp
      g_portalConfig->ebbFloodDurationSec = (uint16_t)fillSec;

      g_portalState->ebbCalibPhase = EBB_CALIB_DRAINING;
      g_portalState->ebbCalibMarkMs = millis();

      if (!config_save(g_portalConfig)) {
        api_sendEnvelope(req, 500, false, false, "failed to save config");
        return;
      }
      StaticJsonDocument<96> out;
      out["ok"] = true;
      out["flood_duration_sec"] = fillSec;
      char buf[96]; serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
      DEBUG_PRINTF("[INFO]  EbbCalib: full at %lus — now timing drain\n",
                   (unsigned long)fillSec);
    });

  // POST /api/calib/ebb/capture_empty — tallenna tyhjenemisaika, lopeta sessio.
  portal_registerJsonPostRoute("/api/calib/ebb/capture_empty",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState || !g_portalConfig) {
        api_sendEnvelope(req, 500, false, false, "no state"); return;
      }
      if (g_portalState->ebbCalibPhase != EBB_CALIB_DRAINING) {
        api_sendEnvelope(req, 409, false, false, "not draining — capture full first");
        return;
      }

      unsigned long drainMs = millis() - g_portalState->ebbCalibMarkMs;
      uint32_t drainSec = (uint32_t)((drainMs + 500) / 1000);
      if (drainSec < 1) drainSec = 1;
      if (drainSec > 3600) drainSec = 3600;                   // matches config clamp
      g_portalConfig->ebbDrainTimeoutSec = (uint16_t)drainSec;

      g_portalState->ebbCalibPhase = EBB_CALIB_IDLE;

      if (!config_save(g_portalConfig)) {
        api_sendEnvelope(req, 500, false, false, "failed to save config");
        return;
      }
      StaticJsonDocument<96> out;
      out["ok"] = true;
      out["drain_timeout_sec"] = drainSec;
      char buf[96]; serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
      DEBUG_PRINTF("[INFO]  EbbCalib: empty at %lus drain — calibration done\n",
                   (unsigned long)drainSec);
    });

  // POST /api/calib/ebb/cancel — keskeytä sessio, pysäytä pumppu, ei config-muutosta.
  portal_registerJsonPostRoute("/api/calib/ebb/cancel",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState) { api_sendEnvelope(req, 500, false, false, "no state"); return; }
      pump_stop();
      g_portalState->pumpRunning = false;
      g_portalState->ebbCalibPhase = EBB_CALIB_IDLE;
      DEBUG_INFO(F("EbbCalib: cancelled"));
      api_sendEnvelope(req, 200, true);
    });

  // ── Ebb&Flow soak-hold calibration (continuous-flow level hold) ──
  // Toisin kuin ebb-fill-drain (aikakaappaus), tästä kaapataan PUMPUN DUTY:
  // nosta vesi halutulle korkeudelle, paina "pidä" → pumppu putoaa matalalle
  // soak-dutylle, hienosäädä +/- (delta) tai numerolla kunnes pinta pysyy
  // paikallaan, tallenna. Save kirjoittaa ebbSoakPwmPct:n (+ valinnainen kesto)
  // DeviceConfigiin; SOAK-vaihe pitää sen jälkeen tason tällä dutylla. Pumppu on
  // jatkuva PWM, rajattu PUMP_ABSOLUTE_MAX_ON_MS:llä ja FLOAT_OVF-salvalla.

  // POST /api/calib/soak/start — aloita sessio, pumppu nostaa veden.
  // Body (valinnainen): {"fill_pct": 1..100} (oletus 100 = nopea täyttö).
  portal_registerJsonPostRoute("/api/calib/soak/start",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState) { api_sendEnvelope(req, 500, false, false, "no state"); return; }
      if (g_portalState->ebbFlowState != EBB_STATE_IDLE ||
          g_portalState->ebbCalibPhase != EBB_CALIB_IDLE || pump_isRunning()) {
        api_sendEnvelope(req, 409, false, false, "pump busy");
        return;
      }

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;
      int fillPct = payload.containsKey("fill_pct") ? (int)payload["fill_pct"] : 100;
      if (fillPct < 1 || fillPct > 100) {
        api_sendEnvelope(req, 400, false, false, "fill_pct out of range (1-100)", "fill_pct");
        return;
      }

      if (!pump_setContinuous((uint8_t)fillPct)) {
        api_sendEnvelope(req, 409, false, false, "pump refused (overflow latched?)");
        return;
      }
      g_portalState->soakCalibActive = true;
      g_portalState->pumpRunning = true;
      DEBUG_PRINTF("[INFO]  SoakCalib: started fill @ %d%%\n", fillPct);

      StaticJsonDocument<96> out;
      out["ok"] = true;
      out["duty_pct"] = pump_getDutyPct();
      char buf[96]; serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
    });

  // POST /api/calib/soak/hold — pudota soak-pidätysdutylle ("pidä tässä").
  // Body (valinnainen): {"pct": N} (oletus PUMP_SOAK_HOLD_START_PCT ~30 %).
  portal_registerJsonPostRoute("/api/calib/soak/hold",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState || !g_portalState->soakCalibActive) {
        api_sendEnvelope(req, 409, false, false, "no soak session — call start first");
        return;
      }
      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;
      int pct = payload.containsKey("pct") ? (int)payload["pct"] : PUMP_SOAK_HOLD_START_PCT;
      uint8_t duty = calibration_clampSoakDutyPct(pct);
      if (!pump_setContinuous(duty)) {
        api_sendEnvelope(req, 409, false, false, "pump refused (overflow latched?)");
        return;
      }
      g_portalState->pumpRunning = (duty > 0);

      StaticJsonDocument<96> out;
      out["ok"] = true;
      out["duty_pct"] = pump_getDutyPct();
      char buf[96]; serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
      DEBUG_PRINTF("[INFO]  SoakCalib: hold @ %u%%\n", (unsigned)duty);
    });

  // POST /api/calib/soak/adjust — hienosäätö. Body: {"pct": N} (absoluuttinen)
  // tai {"delta": ±N} (suhteessa nykyiseen dutyyn). Clampataan soak-alueelle.
  portal_registerJsonPostRoute("/api/calib/soak/adjust",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState || !g_portalState->soakCalibActive) {
        api_sendEnvelope(req, 409, false, false, "no soak session — call start first");
        return;
      }
      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;
      int newPct;
      if (payload.containsKey("pct")) {
        newPct = (int)payload["pct"];
      } else if (payload.containsKey("delta")) {
        newPct = (int)pump_getDutyPct() + (int)payload["delta"];
      } else {
        api_sendEnvelope(req, 400, false, false, "need pct or delta");
        return;
      }
      uint8_t duty = calibration_clampSoakDutyPct(newPct);
      if (!pump_setContinuous(duty)) {
        api_sendEnvelope(req, 409, false, false, "pump refused (overflow latched?)");
        return;
      }
      g_portalState->pumpRunning = (duty > 0);

      StaticJsonDocument<96> out;
      out["ok"] = true;
      out["duty_pct"] = pump_getDutyPct();
      char buf[96]; serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
    });

  // POST /api/calib/soak/save — tallenna soak-duty (nykyinen live-duty) ja
  // valinnaisesti soak-kesto; pysäytä pumppu ja lopeta sessio.
  // Body (valinnainen): {"duration_sec": 1..3600}.
  portal_registerJsonPostRoute("/api/calib/soak/save",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState || !g_portalConfig) {
        api_sendEnvelope(req, 500, false, false, "no state"); return;
      }
      if (!g_portalState->soakCalibActive) {
        api_sendEnvelope(req, 409, false, false, "no soak session — call start first");
        return;
      }
      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      uint8_t duty = calibration_clampSoakDutyPct((int)pump_getDutyPct());
      g_portalConfig->ebbSoakPwmPct = duty;

      if (payload.containsKey("duration_sec")) {
        int d = (int)payload["duration_sec"];
        if (d < 1 || d > 3600) {
          api_sendEnvelope(req, 400, false, false, "duration_sec out of range (1-3600)", "duration_sec");
          return;
        }
        g_portalConfig->ebbSoakDurationSec = (uint16_t)d;
      }

      pump_stop();
      g_portalState->pumpRunning = false;
      g_portalState->soakCalibActive = false;

      if (!config_save(g_portalConfig)) {
        api_sendEnvelope(req, 500, false, false, "failed to save config");
        return;
      }

      StaticJsonDocument<128> out;
      out["ok"] = true;
      out["soak_pwm_pct"] = g_portalConfig->ebbSoakPwmPct;
      out["soak_duration_sec"] = g_portalConfig->ebbSoakDurationSec;
      char buf[128]; serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
      DEBUG_PRINTF("[INFO]  SoakCalib: saved soak hold @ %u%%\n", (unsigned)duty);
    });

  // POST /api/calib/soak/cancel — pysäytä pumppu, lopeta sessio, ei config-muutosta.
  portal_registerJsonPostRoute("/api/calib/soak/cancel",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalState) { api_sendEnvelope(req, 500, false, false, "no state"); return; }
      pump_stop();
      g_portalState->pumpRunning = false;
      g_portalState->soakCalibActive = false;
      DEBUG_INFO(F("SoakCalib: cancelled"));
      api_sendEnvelope(req, 200, true);
    });
}
#else
static void portal_registerEbbSoakCalibRoutes() {}
#endif // ENABLE_EBB_FLOW && ENABLE_PUMP

// ── WIZARD: power-current-cal — INA228 runtime config + calibration ──
// Level 1 (config, no reflash) + Level 2 (reference-current self-correction).
// See docs/ohjeet/wizardit.md and docs/arkisto/kehitys/ina228-virtamittaus.md.
#if HW_INA228
static void portal_registerPowerCalibRoutes() {
  // POST /api/calib/power/config — set shunt / max current / ADC range live.
  // Body (all optional, missing = keep current): {"shunt_ohms":<f>,"max_current_a":<f>,"adc_range":0|1}
  portal_registerJsonPostRoute("/api/calib/power/config",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalCalibration) { api_sendEnvelope(req, 500, false, false, "no calibration"); return; }
      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      g_portalCalibration->powerShuntOhms   = payload["shunt_ohms"]    | g_portalCalibration->powerShuntOhms;
      g_portalCalibration->powerMaxCurrentA = payload["max_current_a"] | g_portalCalibration->powerMaxCurrentA;
      g_portalCalibration->powerAdcRange    = (uint8_t)(payload["adc_range"] | (int)g_portalCalibration->powerAdcRange);
      calibration_clampBounds(g_portalCalibration);

      if (!calibration_save(g_portalCalibration)) {
        api_sendEnvelope(req, 500, false, false, "failed to save calibration"); return;
      }
      ina228_applyRuntimeConfig(g_portalCalibration->powerShuntOhms,
                                g_portalCalibration->powerMaxCurrentA,
                                g_portalCalibration->powerAdcRange,
                                g_portalCalibration->powerCalFactor);

      StaticJsonDocument<256> out;
      out["ok"] = true;
      out["restartRequired"] = false;
      out["shunt_ohms"] = g_portalCalibration->powerShuntOhms;
      out["max_current_a"] = g_portalCalibration->powerMaxCurrentA;
      out["adc_range"] = g_portalCalibration->powerAdcRange;
      out["cal_factor"] = g_portalCalibration->powerCalFactor;
      char buf[256]; serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
      DEBUG_PRINTF("[INFO]  Calib: power config shunt=%.4f max=%.4f range=%u\n",
                   g_portalCalibration->powerShuntOhms, g_portalCalibration->powerMaxCurrentA,
                   g_portalCalibration->powerAdcRange);
    });

  // POST /api/calib/power/apply — apply a known reference current. Device reads
  // its own live current and refines the correction factor to match the reference.
  // Body: {"i_ref_ma":<float > 0>}.
  portal_registerJsonPostRoute("/api/calib/power/apply",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalCalibration) { api_sendEnvelope(req, 500, false, false, "no calibration"); return; }
      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;
      if (!payload.containsKey("i_ref_ma") || !payload["i_ref_ma"].is<float>()) {
        api_sendEnvelope(req, 400, false, false, "i_ref_ma must be number", "i_ref_ma"); return;
      }
      float iRef = payload["i_ref_ma"].as<float>();
      if (iRef <= 0.0f) { api_sendEnvelope(req, 400, false, false, "i_ref_ma must be > 0"); return; }

      float measured = ina228_readCurrentMa();
      float oldFactor = g_portalCalibration->powerCalFactor;
      float newFactor = calibration_refinePowerCalFactor(oldFactor, iRef, measured);
      g_portalCalibration->powerCalFactor = newFactor;
      calibration_clampBounds(g_portalCalibration);

      if (!calibration_save(g_portalCalibration)) {
        api_sendEnvelope(req, 500, false, false, "failed to save calibration"); return;
      }
      ina228_applyRuntimeConfig(g_portalCalibration->powerShuntOhms,
                                g_portalCalibration->powerMaxCurrentA,
                                g_portalCalibration->powerAdcRange,
                                g_portalCalibration->powerCalFactor);

      StaticJsonDocument<256> out;
      out["ok"] = true;
      out["restartRequired"] = false;
      out["measured_ma"] = measured;
      out["i_ref_ma"] = iRef;
      out["old_factor"] = oldFactor;
      out["new_factor"] = g_portalCalibration->powerCalFactor;
      char buf[256]; serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
      DEBUG_PRINTF("[INFO]  Calib: power factor %.4f -> %.4f (measured %.2f mA, ref %.2f mA)\n",
                   oldFactor, g_portalCalibration->powerCalFactor, measured, iRef);
    });

  // POST /api/calib/power/reset — reset correction factor to 1.0 (uncalibrated).
  portal_registerJsonPostRoute("/api/calib/power/reset",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalCalibration) { api_sendEnvelope(req, 500, false, false, "no calibration"); return; }
      g_portalCalibration->powerCalFactor = 1.0f;
      if (!calibration_save(g_portalCalibration)) {
        api_sendEnvelope(req, 500, false, false, "failed to save calibration"); return;
      }
      ina228_applyRuntimeConfig(g_portalCalibration->powerShuntOhms,
                                g_portalCalibration->powerMaxCurrentA,
                                g_portalCalibration->powerAdcRange,
                                g_portalCalibration->powerCalFactor);
      api_sendEnvelope(req, 200, true);
      DEBUG_INFO(F("Calib: power factor reset to 1.0"));
    });
}
#else
static void portal_registerPowerCalibRoutes() {}
#endif // HW_INA228

#if HW_AS7341
static void portal_registerPpfdCalibRoutes() {
  // WIZARD: ppfd-cal — ks. docs/ohjeet/wizardit.md
  // POST /api/calib/ppfd/save — Body: {"referencePpfdUmol": <float>}.
  // Kayttaja mittaa PPFD:n (umol/m2/s) ulkoisella referenssilla (esim.
  // puhelimen lux-mittari muunnettuna) samaan aikaan/paikkaan kuin anturi,
  // ja lahettaa lukeman. Laite laskee skaalauskertoimen omasta raa'asta
  // (kalibroimattomasta) AS7341-lukemastaan ja tallentaa sen.
  portal_registerJsonPostRoute("/api/calib/ppfd/save",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalCalibration) {
        api_sendEnvelope(req, 500, false, false, "no calibration");
        return;
      }
      if (!g_portalSensors || !g_portalSensors->spectrumValid) {
        api_sendEnvelope(req, 409, false, false, "AS7341-spektri ei ole validi juuri nyt");
        return;
      }

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      if (!payload.containsKey("referencePpfdUmol") || !payload["referencePpfdUmol"].is<float>()) {
        api_sendEnvelope(req, 400, false, false, "referencePpfdUmol must be number", "referencePpfdUmol");
        return;
      }
      float referencePpfdUmol = payload["referencePpfdUmol"].as<float>();

      float rawPpfdRelative = lightCalc_estimatePPFD_uncalibrated(&g_portalSensors->lightSpectrum);
      float newFactor = calibration_computePpfdFactor(referencePpfdUmol, rawPpfdRelative);
      if (newFactor <= 0.0f) {
        api_sendEnvelope(req, 400, false, false, "invalid measurement (would yield <= 0)");
        return;
      }

      float oldFactor = g_portalCalibration->ppfdCalibrationFactor;
      g_portalCalibration->ppfdCalibrationFactor = newFactor;
      calibration_clampBounds(g_portalCalibration);

      if (!calibration_save(g_portalCalibration)) {
        api_sendEnvelope(req, 500, false, false, "failed to save calibration");
        return;
      }

      // Paivita sensor_managerin runtime-arvo niin ettei tarvitse rebootta.
      sensors_setCalibration(g_portalCalibration);

      StaticJsonDocument<192> out;
      out["ok"] = true;
      out["restartRequired"] = false;
      out["rawPpfdRelative"] = rawPpfdRelative;
      out["oldFactor"] = oldFactor;
      out["newFactor"] = g_portalCalibration->ppfdCalibrationFactor;
      char buf[192];
      serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
      DEBUG_INFO(F("Calib: PPFD-kerroin tallennettu"));
    });

  // POST /api/calib/ppfd/reset — palauttaa kertoimen 1.0:aan (kalibroimaton/suhteellinen).
  portal_registerJsonPostRoute("/api/calib/ppfd/reset",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      (void)json;
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalCalibration) {
        api_sendEnvelope(req, 500, false, false, "no calibration");
        return;
      }
      g_portalCalibration->ppfdCalibrationFactor = CALIB_PPFD_FACTOR_DEFAULT;
      if (!calibration_save(g_portalCalibration)) {
        api_sendEnvelope(req, 500, false, false, "failed to save calibration");
        return;
      }
      sensors_setCalibration(g_portalCalibration);
      api_sendEnvelope(req, 200, true);
      DEBUG_INFO(F("Calib: PPFD-kerroin nollattu (1.0)"));
    });

  // WIZARD: ppfd-geometry — ks. docs/ohjeet/wizardit.md
  // POST /api/calib/ppfd/geometry — Body joko {"factor": <float>} TAI
  // {"sensorDistanceMm": <float>, "canopyDistanceMm": <float>} (molemmat
  // lampusta mitattuna, kerroin kaanteisesta neliolaista).
  //
  // Miksi erillaan absoluuttikertoimesta: naiden elinkaari on eri. Absoluutti-
  // kerroin on anturin ominaisuus (muuttuu vain gainin tai referenssimittarin
  // myota), geometriakerroin muuttuu joka kerta kun lamppua tai anturia
  // siirretaan. Yhdistettyna kayttaja joutuisi ajamaan koko absoluutti-
  // kalibroinnin uusiksi jokaisen lampun siirron jalkeen.
  portal_registerJsonPostRoute("/api/calib/ppfd/geometry",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;
      if (!g_portalCalibration) {
        api_sendEnvelope(req, 500, false, false, "no calibration");
        return;
      }

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      float newFactor = 0.0f;
      if (payload.containsKey("factor")) {
        if (!payload["factor"].is<float>()) {
          api_sendEnvelope(req, 400, false, false, "factor must be number", "factor");
          return;
        }
        newFactor = payload["factor"].as<float>();
      } else if (payload.containsKey("sensorDistanceMm") &&
                 payload.containsKey("canopyDistanceMm")) {
        // sensorOffsetMm mukana -> sensorDistanceMm on PYSTYkorkeus ja kerroin
        // saa kosinikorjauksen (anturi on sivussa, ks. calibration_math.h).
        // Ilman sita sensorDistanceMm on suora etaisyys, vanha kayttaytyminen.
        if (payload.containsKey("sensorOffsetMm")) {
          newFactor = calibration_computeGeometryFactorOffset(
              payload["sensorDistanceMm"].as<float>(),
              payload["sensorOffsetMm"].as<float>(),
              payload["canopyDistanceMm"].as<float>());
        } else {
          newFactor = calibration_computeGeometryFactor(
              payload["sensorDistanceMm"].as<float>(),
              payload["canopyDistanceMm"].as<float>());
        }
      } else {
        api_sendEnvelope(req, 400, false, false,
                         "need factor or sensorDistanceMm+canopyDistanceMm");
        return;
      }

      if (newFactor <= 0.0f || newFactor > CALIB_PPFD_GEOMETRY_MAX) {
        api_sendEnvelope(req, 400, false, false, "factor out of range");
        return;
      }

      float oldFactor = g_portalCalibration->ppfdGeometryFactor;
      g_portalCalibration->ppfdGeometryFactor = newFactor;
      calibration_clampBounds(g_portalCalibration);

      if (!calibration_save(g_portalCalibration)) {
        api_sendEnvelope(req, 500, false, false, "failed to save calibration");
        return;
      }
      sensors_setCalibration(g_portalCalibration);

      StaticJsonDocument<192> out;
      out["ok"] = true;
      out["restartRequired"] = false;
      out["oldFactor"] = oldFactor;
      out["newFactor"] = g_portalCalibration->ppfdGeometryFactor;
      char buf[192];
      serializeJson(out, buf, sizeof(buf));
      req->send(200, "application/json", buf);
      DEBUG_INFO(F("Calib: PPFD-geometriakerroin tallennettu"));
    });
}
#else
static void portal_registerPpfdCalibRoutes() {}
#endif // HW_AS7341

static void portal_registerCalibrationPostRoute() {
  // /api/calib REKISTEROIDAAN VIIMEISENA kaikista /api/calib/*-reiteista.
  // ESPAsyncWebServer:n BackwardCompatible URI-match tekee prefix-matchin
  // "/api/calib/..."-poluille, joten /api/calib varastaisi pyynnot kaikilta
  // alipoluilta (pump/test, motor/probe_*, ebb/*, soak/*, power/*, ppfd/*)
  // jos se rekisteroitaisiin ennen niita. Jarjestys ei ole tamän funktion
  // sisainen asia enaa splitin jalkeen (V3-0) — sen takaa call-jarjestys
  // wifi_portal.h:n portal_setupRoutes():ssa. AELA muuta sita jarjestysta
  // ilman etta luet sen kommentin ensin.
  //
  // Kentta-kohtaiset guardit (ei koko-reitin guard): reitti sailyy V3:lla
  // (ei moottoria/pumppua) — pump/motor-kentat vain ovat poissa/ohitetaan.
  portal_registerJsonPostRoute("/api/calib",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!portal_requireAuthorized(req)) return;

      JsonObjectConst payload;
      if (!api_parseJsonVariant(req, json, payload)) return;

      if (!g_portalCalibration) {
        api_sendEnvelope(req, 500, false, false, "no calibration");
        return;
      }

      if (!api_updateFloatField(req, payload, "tds_offset",     -10000.0f, 10000.0f, &g_portalCalibration->tdsOffset)) return;
      if (!api_updateFloatField(req, payload, "tds_gain",       0.05f,    10.0f,    &g_portalCalibration->tdsGain)) return;
#if ENABLE_PUMP
      if (!api_updateFloatField(req, payload, "pump_ml_per_sec", 0.01f,   500.0f,   &g_portalCalibration->pumpMlPerSec)) return;
#endif
#if ENABLE_MOTOR
      if (!api_updateIntField  (req, payload, "motor_steps_up",   0,    100000,    &g_portalCalibration->motorStepsUp)) return;
      if (!api_updateIntField  (req, payload, "motor_steps_down", 0,    100000,    &g_portalCalibration->motorStepsDown)) return;
#endif

#if ENABLE_PUMP
      if (payload.containsKey("pump_ml_per_sec")) {
        pump_setMlPerSec(g_portalCalibration->pumpMlPerSec);
      }
#endif

      calibration_clampBounds(g_portalCalibration);
#if ENABLE_MOTOR
      motor_setStepLimits(g_portalCalibration->motorStepsDown, g_portalCalibration->motorStepsUp);
#endif
      sensors_setCalibration(g_portalCalibration);

      if (!calibration_save(g_portalCalibration)) {
        api_sendEnvelope(req, 500, false, false, "failed to save calibration");
        return;
      }

      api_sendEnvelope(req, 200, true);
    });
}

#endif // WIFI_PORTAL_ROUTES_POST_H
