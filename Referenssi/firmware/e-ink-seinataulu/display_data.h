/*=====================================================================
  display_data.h - DisplayData snapshot (pure data, no hardware deps)

  Extracted from data_fetch.h so pure-logic modules (resolve_field.h,
  growth_animation.h) can use the DisplayData shape without pulling in
  WiFi/HTTPClient/ESPmDNS/ArduinoJson. Populated by data_fetch() from the
  PlantMeister /api/state JSON payload; consumed by the renderers
  (display_layout.h, dev_view.h, dev_table_view.h).
=====================================================================*/

#ifndef DISPLAY_DATA_H
#define DISPLAY_DATA_H

#include <stdint.h>

// Grow-phase indices as reported by PM /api/state growing.phase — must match
// GrowPhaseType order in the PlantMeister firmware (plant_lookup.h). Only the
// two early phases are named here; they select the guided-step framing on the
// status line (idatys / juurtuminen kaynnissa) in display_status_text.h.
#define EINK_PHASE_ROOTING   0
#define EINK_PHASE_SEEDLING  1

// PE-mukavuuskaista (M1, 11.8.2026): sentinel "ei rajaa tahan suuntaan".
// Jaettu vakio parse-puolelle (data_fetch_parse.h, tulkitsee PM:n /api/state
// growing.targets[].lo/hi) ja kulutuspuolelle (status_targets.h). MUST match
// PlantMeister-puolen wifi_portal_api_state.h:n -1.0f-literaalia.
#define PE_NO_LIMIT (-1.0f)

// Yhden PE-mitan mukavuuskaista, sellaisenaan PM:n /api/state growing.targets[]
// -taulukosta (M1). field on lyhyt avain ("vpd","dli","co2","leaf","ppfd") —
// status_targets.h mappaa sen FieldId:ksi (layout_def.h) kulutushetkella,
// jotta tama tiedosto pysyy laitteisto-/enum-riippumattomana muun DisplayDatan
// tapaan (tiedoston yla-kommentti).
struct DisplayPeTarget {
  char  field[8];
  float lo;
  float hi;
  bool  deviceActs;
};
// 8, ei 5: PM julkaisee tanaan tasan 5 kaistaa (vpd/dli/co2/leaf/ppfd), joten
// 5 olisi tayteen mitoitettu. data_fetch_parse.h katkaisee ylimenevat HILJAA
// (`if (n >= DISPLAY_PE_TARGET_MAX) break;`), joten kuudes kaista — jollainen
// KYSYMYKSET.md Q8:n vaihtoehto B nimenomaan lisaisi (ilmalampo) — katoaisi
// paneelilta ilman virhetta, ja vika nakyisi vasta puuttuvana rivina naytolla.
// Vara maksaa 3 x 20 B = 60 B DisplayDatassa. Tama on ainoa paikka jossa PM:n
// kenttamaara ja paneelin puskuri voivat ajautua erilleen — pida vara.
#define DISPLAY_PE_TARGET_MAX 8

struct DisplayData {
  float   airTemp;
  float   airHumidity;
  float   airPressure;
  float   waterTemp;
  int     tdsPpm;

  // ── Plant empowerment -mittarit (PE-tasapainot) ────────────────────────
  // Vesi: VPD (lehtipohjainen kun MLX läsnä). Energia: lehtilämpö + lehti-ilma-Δ.
  // Assimilaatio: PPFD/DLI (AS7341) + CO2 (SCD41). *Valid = lähdeanturi läsnä.
  float   vpdKpa;
  bool    vpdValid;
  float   leafTempC;       // MLX90614 object temperature
  float   leafAmbientC;
  bool    leafTempValid;
  float   ppfd;            // umol/m2/s, instantaneous — CANOPY (geometry applied)
  float   ppfdSensor;      // same reading at the sensor's own position
  bool    ppfdValid;
  // AS7341:n ADC on katossa -> ppfd on ALARAJA eika mittaus. Naytto merkitsee
  // luvun ">"-etuliitteella eika valmenna alivalotuksesta: kirkkaassa
  // auringossa "valoteho heikko" olisi suoraan vaara neuvo.
  bool    ppfdSaturated;

  // Sticky-validiteetin laskurit: montako perakkaista nautetta on hukattu.
  // Nollautuu heti kun arvo saadaan. Ks. data_fetch_parse.h.
  uint8_t missVpd;
  uint8_t missLeaf;
  uint8_t missPpfd;
  uint8_t missCo2;
  uint8_t missEnv;
  float   dli;             // mol/m2/d, accumulated today
  int     co2Ppm;
  bool    co2Valid;
  int     plantHeightMm;
  int     motorHeightMm;
  bool    waterLevelOk;
  bool    lightsOn;
  float   batteryVoltage;
  int     batteryPercent;

  // ── Power monitoring (INA228, from PM /api/state sensors.power_*) ──────
  // 12 V oletetaan vakioksi (kiinteä virtalähde) → teho lasketaan 12 V * virta.
  // charge = sirun oma latausakkumulaattori bootista (integraali, nollautuu
  // reboottiin). devUptimeS jakajana → keskiteho = energia/aika.
  float   powerCurrentMa;   // shunt current (mA), signed
  float   powerChargeMah;   // charge accumulator since boot (mAh)
  bool    powerValid;       // INA228 present + reading valid
  int     loraRssi;
  char    plantId[24];

  // Sensor validity flags (from PM /api/state sensors block)
  bool    envValid;
  bool    heightValid;

  // Motor target + motion (from PM motor block)
  int     motorTargetMm;
  bool    motorMoving;

  // Optional guided-grow fields (if gateway provides them)
  bool    growActive;
  bool    growPendingAdvance;
  // growing.button_next_phase (added 10.8.2026) — true when the guided step
  // sequence for the current phase has been walked through and a short
  // button press now advances the phase (growstep_sequenceComplete(),
  // button_intent_map.h). Distinct from growPendingAdvance above: that one
  // tracks the calendar-based phase-duration signal, which can still say
  // "not yet" while the step guidance is already done — the mismatch that
  // left a hardware tester pressing a dead button on 3.8.2026 (calendar
  // still showed 11/14 while the button already worked). Missing field
  // means false, never guessed true (see display_status_text.h).
  bool    buttonNextPhase;
  int     growPhase;
  int     growElapsedDays;
  bool    growFieldsPresent;
  char    growStartMethod[16];
  char    plantName[32];       // growing.plant_name (display name, e.g. "Persilja")
  char    phaseName[16];       // growing.phase_name (e.g. "Juurtuminen")
  int     phaseCount;          // growing.phase_count
  // growing.targets[] (M1, 11.8.2026) — active phase's PE comfort bands,
  // published by PM instead of the e-ink keeping its own per-phase duplicate
  // (status_targets.h). peTargetCount == 0 means the device sent none (old
  // firmware without this field, or no active grow phase) — status_targets.h
  // falls back to a single generic table in that case, same as it always did
  // for an unrecognized phase name.
  DisplayPeTarget peTargets[DISPLAY_PE_TARGET_MAX];
  uint8_t peTargetCount;
  // growing.action — lyhyt "tee tama nyt" -ohje nykyiselle vaiheelle, valmiiksi
  // yhdelle banneririville mitoitettu ja aloitustavan mukaan valittu
  // PM-puolella (grow_guidance.h grow_shortAction). Tyhja = ei kasvatusta.
  char    growAction[48];
  int     growDaysLeft;        // growing.days_left (-1 = vaihe kestaa toistaiseksi)

  // Valojakso (growing.light_*). Ilman naita DLI-arvio vertaisi kesken
  // vuorokautta kertynytta valosummaa koko paivan tavoitteeseen. 0 = laite ei
  // kertonut -> dli_expectation.h vaikenee sen sijaan etta arvaisi. Kts.
  // dli_expectation.h kommentti juurisyysta.
  float   lightElapsedHours;   // growing.light_elapsed_h; -1 = syklin pimea osa
  int     lightHours;          // valotunteja/vrk TASSA vaiheessa

  // ── Askelsarja (growing.step_*, iso ohjelaatikko COACH-profiilissa) ────
  // stepCount == 0 on PM:n sopimus "ei askelta nakyvissa" (ei listaa, sarja
  // valmis tai laite ei GROWING-tilassa) -> naytto palaa mittarikortteihin.
  int      stepCount;
  int      stepIndex;          // 0-pohjainen
  char     stepText[384];      // pitka ohje; Latin-1 parse-muunnoksen jalkeen
  bool     stepLate;           // myohaisteksti kaytossa (esim. idatys > 5 vrk)
  bool     stepAckAllowed;     // nappi kuittaa (BUTTON-askel)
  bool     stepAwaiting;       // vilkkuvihje: on jotain tehtavaa
  uint32_t stepTimerMin;       // 0 = ei ajastinta
  uint32_t stepElapsedSec;     // kulunut askeleessa
  int32_t  stepRemainingSec;   // laskee alas (TIMER); -1 = ei alaslaskuria

  // ── Actuators (what the device is physically doing now) ────────────────
  bool    airPumpOn;
  bool    pumpRunning;

  // ── Ebb&Flow + circulation activity (from PM /api/state ebb block) ──────
  char    ebbState[8];         // "IDLE"/"FLOOD"/"SOAK"/"DRAIN"/"FAULT"
  bool    ebbFault;
  char    ebbFloodReason[80];  // human-readable schedule explanation
  int     ebbNextCycleSec;     // -1 = not scheduled
  bool    circulateActive;
  bool    circulateEnabled;
  int     ebbNextCirculateSec; // -1 = not scheduled

  bool    dataValid;           // True if at least one successful fetch
  int     fetchFailCount;      // Consecutive failures
  unsigned long lastFetchEpoch; // Unix timestamp of last successful fetch

  // ── Device block (from PM /api/state device) ───────────────────────────
  char     deviceStateName[16];
  char     devicePrevStateName[16];
  uint32_t deviceEnteredAt;
  uint32_t deviceTimeInStateMs;
  char     deviceFaultMsg[40];
  bool     onboarding;         // PM first-run setup incomplete → join-AP screen
  char     staIp[16];          // PM home-network IP ("" until STA connected)

  // ── UX block (LED + e-ink action hints from PM) ────────────────────────
  char     uxLedColor[10];
  char     uxLedPattern[14];
  char     uxMessage[40];
  char     uxAction[40];
  bool     uxFieldsPresent;

  // ── Developer / diagnostics fields (populated if gateway exposes them) ──
  // All optional: defaults make them inert when missing from /api/current.
  int     devDeviceState;      // numeric DeviceState (-1 if missing)
  uint8_t devFaultBits;        // DEVICE_FAULT_* bitmask
  uint32_t devFreeHeap;        // bytes
  uint32_t devUptimeS;         // seconds since boot
  int     devWifiRssi;         // dBm (0 = unknown)
  int     devSchedulerTicks;   // optional counter from gateway
  char    devLastIntent[20];   // last routed intent name
  char    devLastIntentSrc[12];// "BUTTON","SERIAL","LORA","HTTP"
  bool    devFieldsPresent;    // true if any dev field was in payload

  // ── Local e-ink diagnostics (not from gateway) ─────────────────────────
  // Updated by data_fetch on every attempt so the dev profiles can show
  // the e-ink side of the link, not just the PM side.
  uint32_t lastFetchDurationMs;
  int      lastHttpCode;
  bool     lastMdnsResolved;
  char     lastResolvedIp[20];
  char     lastErrorMsg[40];
};

#endif // DISPLAY_DATA_H
