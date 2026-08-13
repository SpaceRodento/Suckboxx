/*=====================================================================
  data_fetch_parse.h - /api/state JSON -> DisplayData mapping

  Extracted from data_fetch.h so the JSON->struct mapping is unit-testable
  (test/test_data_fetch_parse/) independent of the network fetch. Pure
  function: no WiFi/HTTPClient/mDNS, and no getLocalTime()/millis() — those
  stay in data_fetch.h's data_fetch(), which deserializes the HTTP response
  and then calls this. Caller still owns dataValid/fetchFailCount/
  lastFetchDurationMs/lastErrorMsg/lastFetchEpoch (those need HTTP timing /
  RTC time, not JSON).

  Keep this in sync with the /api/state contract on the PlantMeister side
  (firmware/plantmeister/wifi_portal_api_state.h).
=====================================================================*/

#ifndef DATA_FETCH_PARSE_H
#define DATA_FETCH_PARSE_H

#include <stdint.h>
#include <string.h>
#include <ArduinoJson.h>

#include "display_data.h"
#include "utf8_latin1.h"

// ── /api/state-inkluusiofiltteri (irrotettu data_fetch():sta natiivitestiin) ──
// Sensori-avaimet jotka suodatin paastaa DisplayDataan. UUSI sensors-kentta
// lisataan SEKA tanne ETTA populateFromJson():iin — muuten se nakyy nayttolla
// ikuisena "--":na. Muut lohkot (device/ux/motor/growing/actuators/ebb/dev)
// kopioidaan kokonaisina, joten niihin lisays ei vaadi filtterimuutosta.
// ── Sticky-validiteetti ───────────────────────────────────────────────
// Montako perakkaista hukattua nautetta siedetaan ennen kuin arvo vaihtuu
// "--":ksi. Yksi sykli on UPDATE_INTERVAL_SEC, joten 3 = muutama minuutti.
// Tarpeeksi kattamaan yksittainen I2C-glitch tai kiireinen laite, liian
// lyhyt piilottamaan oikean anturivian.
#ifndef EINK_STICKY_MAX_MISSES
  #define EINK_STICKY_MAX_MISSES 3
#endif

// Sailyta edellinen arvo jos tuore naute on invalid mutta armonaikaa on
// jaljella. Nollaa laskurin kun tuore naute on kunnossa. Puhdas logiikka —
// test_data_fetch_parse ajaa taman natiivisti.
static inline void data_stickyKeepFloat(float* value, bool* valid,
                                        float prevValue, bool prevValid,
                                        uint8_t* missCount) {
  if (*valid) { *missCount = 0; return; }
  if (!prevValid || *missCount >= EINK_STICKY_MAX_MISSES) return;
  *value = prevValue;
  *valid = true;
  (*missCount)++;
}

static inline void data_stickyKeepInt(int* value, bool* valid,
                                      int prevValue, bool prevValid,
                                      uint8_t* missCount) {
  if (*valid) { *missCount = 0; return; }
  if (!prevValid || *missCount >= EINK_STICKY_MAX_MISSES) return;
  *value = prevValue;
  *valid = true;
  (*missCount)++;
}

static const char* const kStateSensorKeys[] = {
  "air_temp_c", "air_humidity", "water_temp_c", "tds_ppm",
  "plant_height_mm", "water_level_ok", "water_overflow", "env_valid",
  "height_valid", "vpd_kpa", "vpd_valid", "leaf_temp_c", "leaf_ambient_c",
  "leaf_temp_valid", "ppfd", "ppfd_sensor", "ppfd_valid", "ppfd_saturated", "dli",
  "co2_ppm", "co2_valid",
  "battery_v", "battery_pct", "power_bus_v", "power_current_ma",
  "power_mw", "power_charge_mah", "power_energy_wh", "power_valid",
};
static const int kStateSensorKeyCount =
    (int)(sizeof(kStateSensorKeys) / sizeof(kStateSensorKeys[0]));

// Rakenna filtteridokumentti. KUTSUJAN VASTUU: tarkista filter.overflowed()
// JA rakenna vain KERRAN (createNestedObject luo uuden objektin joka kutsulla
// -> toistuva rakennus samaan static-doc:iin kasvattaa sita ja lopulta pudottaa
// avaimia hiljaa). Bugi 19.7.2026: filter<512> ylivuoti -> vpd/co2/dli/ppfd
// pudotettiin -> kaikki mittarikortit "--" (osui COACH- ja STATUS-profiiliin).
// Filtterin doc-koko: sensori-avaimet + 7 lohkoa mahtuvat <2048>:aan reilusti
// (natiivi 64-bit ~1120 B, ESP32 32-bit ~puolet). Testi test_state_filter vartioi.
static void data_buildStateFilter(JsonDocument& filter) {
  filter["device"]    = true;
  filter["ux"]        = true;
  filter["motor"]     = true;
  filter["growing"]   = true;
  filter["actuators"] = true;
  filter["ebb"]       = true;
  filter["dev"]       = true;   // devUptimeS ym. luetaan tasta — ALA pudota
  JsonObject fs = filter.createNestedObject("sensors");
  for (int i = 0; i < kStateSensorKeyCount; i++) fs[kStateSensorKeys[i]] = true;
}

static void data_fetch_populateFromJson(const JsonDocument& doc, DisplayData* out) {
  JsonObjectConst device  = doc["device"];
  JsonObjectConst ux      = doc["ux"];
  JsonObjectConst sensors = doc["sensors"];
  JsonObjectConst motor   = doc["motor"];
  JsonObjectConst growing = doc["growing"];
  JsonObjectConst dev     = doc["dev"];

  // ── Device block ─────────────────────────────────────────────────────
  {
    const char* sn = device["state"]      | "";
    const char* pn = device["prev_state"] | "";
    const char* fm = device["fault_msg"]  | "";
    utf8ToLatin1(sn, out->deviceStateName, sizeof(out->deviceStateName));
    utf8ToLatin1(pn, out->devicePrevStateName, sizeof(out->devicePrevStateName));
    utf8ToLatin1(fm, out->deviceFaultMsg, sizeof(out->deviceFaultMsg));
  }
  out->deviceEnteredAt      = (uint32_t)(device["entered_at"]       | 0);
  out->deviceTimeInStateMs  = (uint32_t)(device["time_in_state_ms"] | 0);

  // Onboarding: PM reports first-run setup incomplete → onboarding_view.h
  // overrides every profile with the join-AP / open-this-address screen.
  out->onboarding = device["onboarding"] | false;
  {
    const char* sip = device["sta_ip"] | "";
    utf8ToLatin1(sip, out->staIp, sizeof(out->staIp));
  }

  // ── UX block ─────────────────────────────────────────────────────────
  bool hasUx = !ux.isNull();
  out->uxFieldsPresent = hasUx;
  {
    const char* lc = ux["led_color"]   | "";
    const char* lp = ux["led_pattern"] | "";
    const char* ms = ux["message"]     | "";
    const char* ac = ux["action"]      | "";
    utf8ToLatin1(lc, out->uxLedColor, sizeof(out->uxLedColor));
    utf8ToLatin1(lp, out->uxLedPattern, sizeof(out->uxLedPattern));
    utf8ToLatin1(ms, out->uxMessage, sizeof(out->uxMessage));
    utf8ToLatin1(ac, out->uxAction, sizeof(out->uxAction));
  }

  // ── Sticky-validiteetti: ota edelliset arvot talteen ─────────────────
  // Yksi hukattu anturinaute ei saa tyhjentaa korttia. Laite pudottaa
  // xxx_validin heti kun luku epaonnistuu (architecture.md § 8), mika on
  // oikein sen paassa — mutta naytolla se nakyy arvon KATOAMISENA ja palaa
  // itsestaan seuraavalla syklilla (kayttajan havainto 28.7.2026: "VPD katosi
  // ja palautui itsekseen"). Vilkkuva "--" on hairitsevampi kuin muutaman
  // minuutin vanha luku, joten pidetaan viimeisin hyva arvo lyhyen armonajan.
  const DisplayData prev = *out;

  // ── Sensors ──────────────────────────────────────────────────────────
  out->airTemp       = sensors["air_temp_c"]      | 0.0f;
  out->airHumidity   = sensors["air_humidity"]    | 0.0f;
  out->airPressure   = 0.0f;  // not exposed by PlantMeister yet
  out->waterTemp     = sensors["water_temp_c"]    | 0.0f;
  out->tdsPpm        = sensors["tds_ppm"]         | 0;
  out->plantHeightMm = sensors["plant_height_mm"] | 0;
  out->waterLevelOk  = sensors["water_level_ok"]  | true;
  out->envValid      = sensors["env_valid"]       | false;
  out->heightValid   = sensors["height_valid"]    | false;

  // ── Plant empowerment -mittarit (vpd/leaf jo kontraktissa; co2/ppfd/dli
  //    lisätty PR feat/v2-pe-state-fields; puuttuvat → default → "--") ──────
  out->vpdKpa        = sensors["vpd_kpa"]         | 0.0f;
  out->vpdValid      = sensors["vpd_valid"]       | false;
  out->leafTempC     = sensors["leaf_temp_c"]     | 0.0f;
  out->leafAmbientC  = sensors["leaf_ambient_c"]  | 0.0f;
  out->leafTempValid = sensors["leaf_temp_valid"] | false;
  out->ppfd          = sensors["ppfd"]            | 0.0f;
  // Vanhempi firmware ei laheta ppfd_sensoria -> peila ppfd:hen, jolloin
  // erotus on 0 eika naytolle tule harhaanjohtavaa nollaa.
  out->ppfdSensor    = sensors["ppfd_sensor"]     | out->ppfd;
  out->ppfdValid     = sensors["ppfd_valid"]      | false;
  // Vanhempi PM-firmware ei laheta tata -> false = "ei tiedossa saturoituneeksi",
  // mika on sama kaytos kuin ennen lipun olemassaoloa.
  out->ppfdSaturated = sensors["ppfd_saturated"]  | false;
  out->dli           = sensors["dli"]             | 0.0f;
  out->co2Ppm        = sensors["co2_ppm"]         | 0;
  out->co2Valid      = sensors["co2_valid"]       | false;

  // ── Sticky-validiteetti: palauta viimeisin hyva arvo jos naute hukkui ──
  // Ehtona on pelkka prevValid: nollatussa DisplayDatassa se on false, joten
  // ensimmainen haku ei voi keksia arvoja tyhjasta. Armonajan umpeuduttua
  // arvo vaihtuu "--":ksi — pysyva anturivika kuuluu nakya.
  data_stickyKeepFloat(&out->vpdKpa, &out->vpdValid, prev.vpdKpa, prev.vpdValid,
                       &out->missVpd);
  data_stickyKeepFloat(&out->leafTempC, &out->leafTempValid,
                       prev.leafTempC, prev.leafTempValid, &out->missLeaf);
  data_stickyKeepFloat(&out->ppfd, &out->ppfdValid, prev.ppfd, prev.ppfdValid,
                       &out->missPpfd);
  if (out->missPpfd > 0) out->ppfdSensor = prev.ppfdSensor;
  data_stickyKeepInt(&out->co2Ppm, &out->co2Valid, prev.co2Ppm, prev.co2Valid,
                     &out->missCo2);
  // Ilma (T/RH) jakaa yhden validiteettilipun -> oma haara.
  if (!out->envValid && prev.envValid && out->missEnv < EINK_STICKY_MAX_MISSES) {
    out->airTemp = prev.airTemp;
    out->airHumidity = prev.airHumidity;
    out->envValid = true;
    out->missEnv++;
  } else if (out->envValid) {
    out->missEnv = 0;
  }

  // ── Motor (current_mm is the live position, target_mm is requested) ──
  out->motorHeightMm = motor["current_mm"] | 0;
  out->motorTargetMm = motor["target_mm"]  | 0;
  out->motorMoving   = motor["moving"]     | false;

  // Actuators + battery now exposed by PM /api/state.
  JsonObjectConst actuators = doc["actuators"];
  out->lightsOn    = actuators["lights_on"]    | false;
  out->airPumpOn   = actuators["air_pump_on"]  | false;
  out->pumpRunning = actuators["pump_running"] | false;
  out->batteryVoltage = sensors["battery_v"]   | 0.0f;
  out->batteryPercent = sensors["battery_pct"] | 0;
  out->loraRssi       = 0;  // not in /api/state contract

  // ── Power monitoring (INA228, sensors.power_* block) ─────────────────
  out->powerCurrentMa = sensors["power_current_ma"] | 0.0f;
  out->powerChargeMah = sensors["power_charge_mah"] | 0.0f;
  out->powerValid     = sensors["power_valid"]      | false;

  // ── Growing ──────────────────────────────────────────────────────────
  bool hasGrow = !growing.isNull();
  out->growActive         = growing["active"]       | false;
  out->growPhase          = growing["phase"]        | 0;
  out->growElapsedDays    = growing["elapsed_days"] | 0;
  // pending_advance ja action lisattiin /api/state-sopimukseen 15.7.2026:
  // aiemmin vaiheohje elettiin vain /api/grow:ssa (portaali), joten seinataulu
  // - jonka koko tehtava on kertoa mita tehda seuraavaksi - ei nahnyt sita.
  out->growPendingAdvance = growing["pending_advance"] | false;
  // button_next_phase added 10.8.2026 (see display_data.h for why this is
  // not the same signal as pending_advance). Missing field -> false: an
  // older PM firmware or a panel updated ahead of the main unit must not
  // claim the button does something it does not.
  out->buttonNextPhase    = growing["button_next_phase"] | false;
  out->growDaysLeft       = growing["days_left"]       | -1;
  // Oletukset = "ei tietoa" -> DLI-arvio vaikenee. Vanha firmware ei laheta
  // naita kenttia lainkaan, eika arvausta saa tehda: vaara valojakso tuottaisi
  // vaaran neuvon joka nayttaa yhta luotettavalta kuin oikea.
  out->lightElapsedHours  = growing["light_elapsed_h"] | -1.0f;
  out->lightHours         = growing["light_hours"]     | 0;
  out->growFieldsPresent  = hasGrow;

  const char* gact = growing["action"] | "";
  utf8ToLatin1(gact, out->growAction, sizeof(out->growAction));

  const char* pid = growing["plant_id"] | "?";
  utf8ToLatin1(pid, out->plantId, sizeof(out->plantId));

  const char* sm = growing["start_method"] | "";
  utf8ToLatin1(sm, out->growStartMethod, sizeof(out->growStartMethod));

  const char* pname = growing["plant_name"] | "";
  utf8ToLatin1(pname, out->plantName, sizeof(out->plantName));
  const char* phname = growing["phase_name"] | "";
  utf8ToLatin1(phname, out->phaseName, sizeof(out->phaseName));
  out->phaseCount = growing["phase_count"] | 0;

  // ── PE-mukavuuskaistat (growing.targets[], M1 11.8.2026) ────────────
  // Puuttuu kokonaan vanhemmasta PM-firmwaresta ja kun mikaan vaihe ei ole
  // aktiivinen -> peTargetCount jaa 0:ksi, ja status_targets.h putoaa omaan
  // fallback-taulukkoonsa (ei arvausta, sama sopimus kuin muillakin
  // "puuttuva kentta" -tapauksilla tassa tiedostossa). field-avaimet
  // kopioidaan sellaisenaan (ASCII: "vpd"/"dli"/"co2"/"leaf"/"ppfd") — ei
  // UTF-8-muunnosta, tama ei ole nayttoteksti.
  {
    JsonArrayConst targetsArr = growing["targets"].as<JsonArrayConst>();
    uint8_t n = 0;
    if (!targetsArr.isNull()) {
      for (JsonObjectConst t : targetsArr) {
        if (n >= DISPLAY_PE_TARGET_MAX) break;
        const char* fk = t["field"] | "";
        strncpy(out->peTargets[n].field, fk, sizeof(out->peTargets[n].field) - 1);
        out->peTargets[n].field[sizeof(out->peTargets[n].field) - 1] = '\0';
        out->peTargets[n].lo         = t["lo"]          | PE_NO_LIMIT;
        out->peTargets[n].hi         = t["hi"]          | PE_NO_LIMIT;
        out->peTargets[n].deviceActs = t["device_acts"] | false;
        n++;
      }
    }
    out->peTargetCount = n;
  }

  // ── Askelsarja (growing.step_*, 16.7.2026) ───────────────────────────
  // step_count == 0 (tai kentat puuttuvat kokonaan vanhasta firmwaresta)
  // -> ei askelta nakyvissa, kaikki muut kentat inertteja. step_text on
  // ainoa pitka teksti: sama UTF-8 -> Latin-1 -raja kuin muillakin.
  out->stepCount        = growing["step_count"]         | 0;
  out->stepIndex        = growing["step_index"]         | 0;
  out->stepLate         = growing["step_late"]          | false;
  out->stepAckAllowed   = growing["step_ack_allowed"]   | false;
  out->stepAwaiting     = growing["step_awaiting"]      | false;
  out->stepTimerMin     = (uint32_t)(growing["step_timer_min"]   | 0);
  out->stepElapsedSec   = (uint32_t)(growing["step_elapsed_sec"] | 0);
  out->stepRemainingSec = growing["step_remaining_sec"] | -1;
  {
    const char* stext = growing["step_text"] | "";
    utf8ToLatin1(stext, out->stepText, sizeof(out->stepText));
  }

  // ── Ebb&Flow + circulation activity (what the device is doing now) ──────
  JsonObjectConst ebb = doc["ebb"];
  {
    const char* est = ebb["state"]        | "IDLE";
    utf8ToLatin1(est, out->ebbState, sizeof(out->ebbState));
    const char* efr = ebb["flood_reason"] | "";
    utf8ToLatin1(efr, out->ebbFloodReason, sizeof(out->ebbFloodReason));
  }
  out->ebbFault            = ebb["fault"]              | false;
  out->ebbNextCycleSec     = ebb["next_cycle_sec"]     | -1;
  out->circulateActive     = ebb["circulate_active"]   | false;
  out->circulateEnabled    = ebb["circulate_enabled"]  | false;
  out->ebbNextCirculateSec = ebb["next_circulate_sec"] | -1;

  // ── Developer diagnostics (dev block) ────────────────────────────────
  bool hasDev = !dev.isNull();
  out->devFieldsPresent  = hasDev;
  out->devDeviceState    = dev["device_state"] | (device["state_num"] | -1);
  out->devFaultBits      = (uint8_t)(dev["fault_bits"] | (device["faults"] | 0));
  out->devFreeHeap       = (uint32_t)(dev["free_heap"] | 0);
  out->devUptimeS        = (uint32_t)(dev["uptime_s"]  | 0);
  out->devWifiRssi       = dev["wifi_rssi"] | 0;
  out->devSchedulerTicks = 0;  // not exposed
  out->devLastIntent[0]    = '\0';  // not exposed via /api/state
  out->devLastIntentSrc[0] = '\0';
}

#endif // DATA_FETCH_PARSE_H
