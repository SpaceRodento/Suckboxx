/*=====================================================================
  plant_database.h - Plant Species Database

  Loads plant growing parameters from LittleFS JSON.
  If no file exists, creates one with built-in defaults.

  Requires: ArduinoJson, LittleFS
=====================================================================*/

#ifndef PLANT_DATABASE_H
#define PLANT_DATABASE_H

#include "config.h"
#include "structs.h"
#include "plant_lookup.h"
#include "schema_migration.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

// 48 KB (nostettu 32 KB:sta 11.8.2026, M1): PE-mukavuuskaistat lisasivat 6
// kenttaa lisaa per kasvuvaihe (dli/ppfd/co2 min+max) edellisen 6:n paalle
// (12 numeerista kenttaa yhteensa), ja 8 built-in-kasvia × 4 vaihetta ei
// mahdu enaa 32 KB:hon samalla marginaalilla joka 18.7.2026 lukittiin 16:sta
// 32:een samasta syysta. DynamicJsonDocument varaa taman heapista vain
// save/load-ajaksi (vapautuu heti), joten ESP32-S3:n 512 KB RAM:lla se on
// turvallinen. docs/kehitys/pe-ohjausmalli.md, docs/kehitys/Fable_kehityspolku.md § M1.
#ifndef PLANTS_JSON_DOC_SIZE
  #define PLANTS_JSON_DOC_SIZE 49152
#endif

// Maximum on-disk size we'll attempt to parse. Larger files are rejected
// before deserialization to avoid OOM on the ESP32 heap when a corrupt
// or hostile plants.json would otherwise blow PLANTS_JSON_DOC_SIZE.
#ifndef PLANTS_JSON_MAX_FILE_BYTES
  #define PLANTS_JSON_MAX_FILE_BYTES (PLANTS_JSON_DOC_SIZE - 1024)
#endif

// Forward declaration — plants_save() defined below.
bool plants_save();

// ── Per-entry validation ────────────────────────────────────────
// Validate one plant entry parsed from JSON. Returns true if all
// required fields are present and within sane ranges. On failure
// writes a human-readable reason to errOut.
//
// Sanity ranges are intentionally generous — they exist to catch
// corruption / wildly-out-of-spec values, not to enforce a tight policy.
inline bool plants_validateEntry(const char* id,
                                 const JsonObjectConst plant,
                                 char* errOut,
                                 size_t errLen) {
  if (!id || id[0] == '\0' || strlen(id) >= sizeof(((PlantConfig*)0)->id)) {
    snprintf(errOut, errLen, "invalid id");
    return false;
  }

  struct IntField { const char* key; int minV; int maxV; };
  const IntField intFields[] = {
    { "max_height_mm",        10,  3000 },
    { "light_hours",           0,    24 },
    { "water_ml_per_dose",     1,  5000 },
    { "water_interval_hours",  1,   720 },
    // tds_target_ppm poistettu 18.7.2026 (pe-ohjausmalli.md) — PE-tavoitteet
    // validoidaan per vaihe, ei litteassa plant-tasossa.
  };
  const size_t numIntFields = sizeof(intFields) / sizeof(intFields[0]);

  for (size_t i = 0; i < numIntFields; i++) {
    const IntField& f = intFields[i];
    if (!plant.containsKey(f.key)) {
      snprintf(errOut, errLen, "missing '%s'", f.key);
      return false;
    }
    if (!plant[f.key].is<int>()) {
      snprintf(errOut, errLen, "'%s' not integer", f.key);
      return false;
    }
    int v = plant[f.key].as<int>();
    if (v < f.minV || v > f.maxV) {
      snprintf(errOut, errLen, "'%s' out of range (%d..%d)", f.key, f.minV, f.maxV);
      return false;
    }
  }

  // Lampotila-kentat (temp_min_c/temp_max_c) ja niiden jarjestystarkistus
  // poistettiin 18.7.2026: kasvin lampotavoite on nyt PE-tavoite per vaihe
  // (leafTempMaxC, lehtilampo), ei litteassa plant-tasossa. Vaiheiden
  // PE-tavoitteet ovat rakenteeltaan vapaat (0 = ei asetettu), joten niita
  // ei validoida talla litteal la portilla — ks. pe-ohjausmalli.md §6.

  return true;
}

// ── Load from LittleFS ──────────────────────────────────────────

bool plants_load() {
  if (!LittleFS.exists(PATH_PLANTS_DB)) {
    DEBUG_WARN(F("Plants: no plants.json, using defaults"));
    plants_loadDefaults();
    return true;
  }

  File f = LittleFS.open(PATH_PLANTS_DB, "r");
  if (!f) {
    DEBUG_ERROR(F("Plants: failed to open plants.json"));
    plants_loadDefaults();
    return false;
  }

  size_t fileSize = f.size();
  if (fileSize == 0) {
    DEBUG_WARN(F("Plants: plants.json empty, using defaults"));
    f.close();
    plants_loadDefaults();
    return false;
  }
  if (fileSize > PLANTS_JSON_MAX_FILE_BYTES) {
    DEBUG_PRINTF("[ERROR] Plants: plants.json too large (%u > %u), using defaults\n",
                 (unsigned)fileSize, (unsigned)PLANTS_JSON_MAX_FILE_BYTES);
    f.close();
    plants_loadDefaults();
    return false;
  }

  DynamicJsonDocument doc(PLANTS_JSON_DOC_SIZE);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    DEBUG_ERRORF("Plants: JSON parse error: %s\n", err.c_str());
    plants_loadDefaults();
    return false;
  }

  JsonObject root = doc.as<JsonObject>();

  // ── Schema version gate ────────────────────────────────────────
  // Plants stores species as top-level keys; schema_version sits alongside
  // them and is filtered out of the iteration below.
  uint8_t fileVersion = (uint8_t)(root["schema_version"] | 0);
  SchemaDecision decision = schema_decide(fileVersion,
                                          SCHEMA_VERSION_PLANTS_MIN,
                                          SCHEMA_VERSION_PLANTS_CURRENT);

  if (decision == SCHEMA_RESET_DEFAULTS) {
    DEBUG_PRINTF("[WARN]  Plants: schema v%u unsupported (current v%u), using defaults\n",
                 fileVersion, SCHEMA_VERSION_PLANTS_CURRENT);
    plants_loadDefaults();
    return true;
  }

  g_plantCount = 0;
  int rejectedCount = 0;

  for (JsonPair kv : root) {
    // Skip schema_version meta key — not a plant.
    if (strcmp(kv.key().c_str(), "schema_version") == 0) continue;
    if (g_plantCount >= MAX_PLANTS) break;

    JsonObjectConst plant = kv.value().as<JsonObjectConst>();
    if (plant.isNull()) {
      DEBUG_PRINTF("[WARN]  Plants: entry '%s' not an object, skipping\n", kv.key().c_str());
      rejectedCount++;
      continue;
    }

    char vErr[64];
    if (!plants_validateEntry(kv.key().c_str(), plant, vErr, sizeof(vErr))) {
      DEBUG_PRINTF("[WARN]  Plants: skipping '%s': %s\n", kv.key().c_str(), vErr);
      rejectedCount++;
      continue;
    }

    PlantConfig& p = g_plants[g_plantCount];
    memset(&p, 0, sizeof(p));
    strncpy(p.id, kv.key().c_str(), sizeof(p.id) - 1);
    p.id[sizeof(p.id) - 1] = '\0';

    strncpy(p.name, plant["name"] | p.id, sizeof(p.name) - 1);
    p.name[sizeof(p.name) - 1] = '\0';
    p.maxHeightMm        = plant["max_height_mm"];
    p.lightHours         = plant["light_hours"];
    p.waterMlPerDose     = plant["water_ml_per_dose"];
    p.waterIntervalHours = plant["water_interval_hours"];

    // Load grow phases if present (phases array is optional; missing
    // phases is not an error — built-in defaults provide them on demand).
    p.phaseCount = 0;
    JsonArrayConst phasesArr = plant["phases"].as<JsonArrayConst>();
    if (!phasesArr.isNull()) {
      for (JsonObjectConst ph : phasesArr) {
        if (p.phaseCount >= GROW_PHASE_MAX) break;
        GrowPhaseParams& gp = p.phases[p.phaseCount];
        gp.type             = (GrowPhaseType)(ph["type"]             | 0);
        const char* lbl     = ph["label"] | "";
        strncpy(gp.label, lbl, sizeof(gp.label) - 1);
        gp.label[sizeof(gp.label) - 1] = '\0';
        const char* guidance = ph["guidance"] | "";
        strncpy(gp.guidance, guidance, sizeof(gp.guidance) - 1);
        gp.guidance[sizeof(gp.guidance) - 1] = '\0';
        gp.durationDays     = ph["duration_days"]      | 0;
        gp.lightHours       = ph["light_hours"]        | 12;
        gp.floodIntervalMin = ph["flood_interval_min"] | 180;
        gp.floodDurationSec = ph["flood_duration_sec"] | 45;
        gp.dliTargetMol     = ph["dli_target_mol"]     | 0;
        gp.ppfdTargetUmol   = ph["ppfd_target_umol"]   | 0;
        gp.vpdMinKpa        = ph["vpd_min_kpa"]         | 0.0f;
        gp.vpdMaxKpa        = ph["vpd_max_kpa"]         | 0.0f;
        gp.co2TargetPpm     = ph["co2_target_ppm"]      | 0;
        gp.leafTempMaxC     = ph["leaf_temp_max_c"]     | 0.0f;
        // M1 PE-mukavuuskaistat (11.8.2026). Puuttuvat -> 0.0f, backfillataan
        // alla plants_fillMissingBands():lla ennen kuin tata plantia kaytetaan.
        gp.dliMinMol        = ph["dli_min_mol"]         | 0.0f;
        gp.dliMaxMol        = ph["dli_max_mol"]         | 0.0f;
        gp.ppfdMinUmol      = ph["ppfd_min_umol"]       | 0.0f;
        gp.ppfdMaxUmol      = ph["ppfd_max_umol"]       | 0.0f;
        gp.co2MinPpm        = ph["co2_min_ppm"]         | 0.0f;
        gp.co2MaxPpm        = ph["co2_max_ppm"]         | 0.0f;
        p.phaseCount++;
      }
    }
    plants_fillMissingBands(&p);

    g_plantCount++;
  }

  // F1: Inherit built-in phases for any loaded plant with phaseCount==0
  // and a matching built-in ID. A plant.json entry can omit "phases" when
  // the operator only wants to override flat fields (name, light_hours…).
  // Without inheritance such a plant would shadow the built-in, leaving
  // phaseCount==0 and silently breaking scheduled flooding (I1).
  // Plants with an unknown custom ID keep phaseCount==0 — their phases
  // are simply not defined yet and INTENT_START_GROWING will reject them
  // with an explicit log message (input_router.h F3).
  if (g_plantCount > 0) {
    bool anyNeedsPhases = false;
    for (int i = 0; i < g_plantCount; i++) {
      if (g_plants[i].phaseCount == 0) { anyNeedsPhases = true; break; }
    }
    if (anyNeedsPhases) {
      size_t sz = (size_t)g_plantCount * sizeof(PlantConfig);
      PlantConfig* saved = (PlantConfig*)malloc(sz);
      if (saved) {
        int savedCount = g_plantCount;
        memcpy(saved, g_plants, sz);
        plants_loadDefaults();  // populates g_plants with canonical built-ins
        for (int i = 0; i < savedCount; i++) {
          if (saved[i].phaseCount == 0) {
            PlantConfig* b = plants_getById(saved[i].id);
            if (b && b->phaseCount > 0) {
              saved[i].phaseCount = b->phaseCount;
              memcpy(saved[i].phases, b->phases,
                     sizeof(GrowPhaseParams) * (size_t)b->phaseCount);
              DEBUG_PRINTF("[INFO]  Plants: '%s' inherited %u built-in phases\n",
                           saved[i].id, (unsigned)saved[i].phaseCount);
            }
          }
        }
        g_plantCount = savedCount;
        memcpy(g_plants, saved, sz);
        free(saved);
      } else {
        DEBUG_WARN(F("Plants: malloc failed — built-in phases not inherited"));
      }
    }
  }

  // If every entry was rejected, fall back to built-in defaults rather
  // than booting with zero plants (which breaks plant selection in UI).
  if (g_plantCount == 0) {
    DEBUG_ERROR(F("Plants: every entry rejected, using defaults"));
    plants_loadDefaults();
    return false;
  }

  // Merge in built-in plants missing from plants.json so new firmware species
  // (e.g. salaatti, testikasvi) — and any built-ins dropped from an older file —
  // surface without wiping user customizations of existing plants. Matched by id.
  {
    static PlantConfig defs[MAX_PLANTS];
    int defCount = 0;
    plants_buildDefaults(defs, &defCount);
    int added = 0;
    for (int i = 0; i < defCount && g_plantCount < MAX_PLANTS; i++) {
      if (plants_getById(defs[i].id) == NULL) {
        g_plants[g_plantCount++] = defs[i];
        added++;
      }
    }
    if (added > 0) {
      DEBUG_PRINTF("[INFO]  Plants: merged %d missing built-in(s)\n", added);
      plants_save();
    }
  }

  if (decision == SCHEMA_NEEDS_MIGRATE) {
    DEBUG_PRINTF("[INFO]  Plants: migrated v%u -> v%u (%d plants, %d rejected), rewriting\n",
                 fileVersion, SCHEMA_VERSION_PLANTS_CURRENT, g_plantCount, rejectedCount);
    plants_save();
  } else if (rejectedCount > 0) {
    DEBUG_PRINTF("[INFO]  Plants: loaded %d, rejected %d invalid entries\n",
                 g_plantCount, rejectedCount);
  } else {
    DEBUG_PRINTF("[INFO]  Plants: loaded %d from plants.json\n", g_plantCount);
  }
  return true;
}

// ── Save to LittleFS ────────────────────────────────────────────

bool plants_save() {
  DynamicJsonDocument doc(PLANTS_JSON_DOC_SIZE);

  doc["schema_version"] = SCHEMA_VERSION_PLANTS_CURRENT;

  for (int i = 0; i < g_plantCount; i++) {
    PlantConfig& p = g_plants[i];
    JsonObject plant = doc.createNestedObject(p.id);
    plant["name"]                 = p.name;
    plant["max_height_mm"]        = p.maxHeightMm;
    plant["light_hours"]          = p.lightHours;
    plant["water_ml_per_dose"]    = p.waterMlPerDose;
    plant["water_interval_hours"] = p.waterIntervalHours;

    if (p.phaseCount > 0) {
      JsonArray phasesArr = plant.createNestedArray("phases");
      for (int j = 0; j < p.phaseCount; j++) {
        const GrowPhaseParams& gp = p.phases[j];
        JsonObject ph = phasesArr.createNestedObject();
        ph["type"]              = (int)gp.type;
        ph["label"]             = gp.label;
        ph["guidance"]          = gp.guidance;
        ph["duration_days"]     = gp.durationDays;
        ph["light_hours"]       = gp.lightHours;
        ph["flood_interval_min"] = gp.floodIntervalMin;
        ph["flood_duration_sec"] = gp.floodDurationSec;
        ph["dli_target_mol"]    = gp.dliTargetMol;
        ph["ppfd_target_umol"]  = gp.ppfdTargetUmol;
        ph["vpd_min_kpa"]       = gp.vpdMinKpa;
        ph["vpd_max_kpa"]       = gp.vpdMaxKpa;
        ph["co2_target_ppm"]    = gp.co2TargetPpm;
        ph["leaf_temp_max_c"]   = gp.leafTempMaxC;
        ph["dli_min_mol"]       = gp.dliMinMol;
        ph["dli_max_mol"]       = gp.dliMaxMol;
        ph["ppfd_min_umol"]     = gp.ppfdMinUmol;
        ph["ppfd_max_umol"]     = gp.ppfdMaxUmol;
        ph["co2_min_ppm"]       = gp.co2MinPpm;
        ph["co2_max_ppm"]       = gp.co2MaxPpm;
      }
    }
  }

  File f = LittleFS.open(PATH_PLANTS_DB, "w");
  if (!f) {
    DEBUG_ERROR(F("Plants: failed to write plants.json"));
    return false;
  }

  size_t written = serializeJsonPretty(doc, f);
  f.close();

  if (written == 0) {
    DEBUG_ERROR(F("Plants: serializeJson wrote 0 bytes"));
    return false;
  }

  DEBUG_PRINTF("[INFO]  Plants: saved %d entries (%u bytes) to plants.json\n",
               g_plantCount, (unsigned)written);
  return true;
}

// ── Init ────────────────────────────────────────────────────────

bool plants_init() {
  return plants_load();
}

#endif // PLANT_DATABASE_H
