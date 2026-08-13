/*=====================================================================
  sensor_history.h - Ring Buffer Sensor History

  Maintains a ring buffer of sensor readings in LittleFS.
  Stores ~2 hours of 30-second readings (240 entries) without RPi.
  Each entry: timestamp + key fields (temp, humidity, plant height, battery).

  Requires: ArduinoJson, LittleFS
=====================================================================*/

#ifndef SENSOR_HISTORY_H
#define SENSOR_HISTORY_H

#include "config.h"
#include "structs.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstring>

#if ENABLE_SENSOR_HISTORY

// Ring buffer parameters
#define HISTORY_MAX_ENTRIES     240         // ~2 hours @ 30s interval
#define HISTORY_SAVE_INTERVAL   30000UL    // Save to disk every 30s
#define HISTORY_FILE            "/history.json"

struct HistoryEntry {
  unsigned long timestamp;  // millis() when recorded
  float airTempC;
  float airHumidity;
  int plantHeightMm;
  float batteryVoltage;
  int batteryPercent;
  int loraRssi;
};

// Ring buffer state (in RAM)
static HistoryEntry g_historyBuffer[HISTORY_MAX_ENTRIES];
static int g_historyIndex = 0;  // Next write position
static int g_historyCount = 0;  // Total entries filled so far
static unsigned long g_lastHistorySaveMs = 0;
static bool g_historyInitialized = false;

// Forward declarations (needed for circular usage)
bool history_save();

// ── Initialization ──────────────────────────────────────────────────

void history_init() {
  if (g_historyInitialized) return;

  // Try to load previous history from LittleFS
  if (LittleFS.exists(HISTORY_FILE)) {
    File f = LittleFS.open(HISTORY_FILE, "r");
    if (f) {
      StaticJsonDocument<8192> doc;  // Buffer for full file
      if (!deserializeJson(doc, f)) {
        g_historyCount = min((int)doc["count"] | 0, HISTORY_MAX_ENTRIES);
        g_historyIndex = doc["index"] | 0;

        JsonArray entries = doc["entries"];
        int loaded = 0;
        for (JsonObject obj : entries) {
          if (loaded >= HISTORY_MAX_ENTRIES) break;
          g_historyBuffer[loaded].timestamp = obj["ts"] | 0;
          g_historyBuffer[loaded].airTempC = obj["t"] | 0.0f;
          g_historyBuffer[loaded].airHumidity = obj["h"] | 0.0f;
          g_historyBuffer[loaded].plantHeightMm = obj["p"] | 0;
          g_historyBuffer[loaded].batteryVoltage = obj["bv"] | 0.0f;
          g_historyBuffer[loaded].batteryPercent = obj["bp"] | 0;
          g_historyBuffer[loaded].loraRssi = obj["rssi"] | 0;
          loaded++;
        }

        DEBUG_PRINTF("[INFO]  History: loaded %d entries from disk\n", loaded);
      }
      f.close();
    }
  }

  g_historyInitialized = true;
  g_lastHistorySaveMs = millis();
  DEBUG_INFO(F("[INFO]  History: initialized"));
}

// ── Add reading to buffer ────────────────────────────────────────

void history_addReading(const SensorData* sensors) {
  if (!g_historyInitialized) return;
  if (!sensors) return;

  HistoryEntry& entry = g_historyBuffer[g_historyIndex];
  entry.timestamp = sensors->timestamp;
  entry.airTempC = sensors->airTempC;
  entry.airHumidity = sensors->airHumidity;
  entry.plantHeightMm = sensors->plantHeightMm;
  entry.batteryVoltage = sensors->batteryVoltage;
  entry.batteryPercent = sensors->batteryPercent;
  entry.loraRssi = sensors->loraRssi;

  // Next position (wrap around)
  g_historyIndex = (g_historyIndex + 1) % HISTORY_MAX_ENTRIES;

  // Track how many we've filled
  if (g_historyCount < HISTORY_MAX_ENTRIES) {
    g_historyCount++;
  }

  // No log — called every sensor cycle, too noisy
}

// ── Periodically save to disk ────────────────────────────────────

void history_update() {
  if (!g_historyInitialized) return;

  // Save every HISTORY_SAVE_INTERVAL
  if (millis() - g_lastHistorySaveMs > HISTORY_SAVE_INTERVAL) {
    history_save();
    g_lastHistorySaveMs = millis();
  }
}

// ── Save buffer to LittleFS ─────────────────────────────────────

bool history_save() {
  StaticJsonDocument<8192> doc;

  doc["count"] = g_historyCount;
  doc["index"] = g_historyIndex;

  JsonArray entries = doc.createNestedArray("entries");
  for (int i = 0; i < g_historyCount; i++) {
    // Arrange chronologically (oldest first)
    int idx = (g_historyIndex - g_historyCount + i + HISTORY_MAX_ENTRIES) % HISTORY_MAX_ENTRIES;
    const HistoryEntry& e = g_historyBuffer[idx];

    JsonObject obj = entries.createNestedObject();
    obj["ts"] = e.timestamp;
    obj["t"] = serialized(String(e.airTempC, 1));    // 1 decimal
    obj["h"] = serialized(String(e.airHumidity, 1));
    obj["p"] = e.plantHeightMm;
    obj["bv"] = serialized(String(e.batteryVoltage, 2));
    obj["bp"] = e.batteryPercent;
    obj["rssi"] = e.loraRssi;
  }

  File f = LittleFS.open(HISTORY_FILE, "w");
  if (!f) {
    DEBUG_WARN(F("[WARN]  History: failed to open file for writing"));
    return false;
  }

  serializeJson(doc, f);
  f.close();

  // No log — fires every 30s, too noisy
  return true;
}

// ── Get history as JSON (for API) ───────────────────────────────

bool history_getAsJson(char* buffer, size_t bufferSize, int maxEntries = 100) {
  if (!g_historyInitialized) {
    snprintf(buffer, bufferSize, "[]");
    return false;
  }

  // Return the most recent maxEntries
  int count = min(maxEntries, g_historyCount);

  // Capacity must scale with count - a fixed 4096 silently truncated the
  // array (as few as ~16 of 240 requested entries survived) with no error
  // returned. Found via test_sensor_history/test_wraparound_keeps_chronological_order.
  DynamicJsonDocument doc(256 + (size_t)count * 320);
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < count; i++) {
    int idx = (g_historyIndex - count + i + HISTORY_MAX_ENTRIES) % HISTORY_MAX_ENTRIES;
    const HistoryEntry& e = g_historyBuffer[idx];

    JsonObject obj = arr.createNestedObject();
    obj["timestamp"] = e.timestamp;
    obj["air_temp"] = serialized(String(e.airTempC, 1));
    obj["air_humidity"] = serialized(String(e.airHumidity, 1));
    obj["plant_height_mm"] = e.plantHeightMm;
    obj["battery_voltage"] = serialized(String(e.batteryVoltage, 2));
    obj["battery_percent"] = e.batteryPercent;
    obj["lora_rssi"] = e.loraRssi;
  }

  serializeJson(arr, buffer, bufferSize);
  return true;
}

// ── Get single field history (for charts) ────────────────────────

bool history_getField(const char* field, char* buffer, size_t bufferSize, int maxEntries = HISTORY_MAX_ENTRIES) {
  if (!g_historyInitialized || !g_historyCount) {
    snprintf(buffer, bufferSize, "[]");
    return false;
  }

  int count = min(maxEntries, g_historyCount);

  // Same fixed-capacity truncation risk as history_getAsJson() above - scale
  // with count instead of a flat 4096.
  DynamicJsonDocument doc(256 + (size_t)count * 160);
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < count; i++) {
    int idx = (g_historyIndex - count + i + HISTORY_MAX_ENTRIES) % HISTORY_MAX_ENTRIES;
    const HistoryEntry& e = g_historyBuffer[idx];

    JsonObject obj = arr.createNestedObject();
    obj["timestamp"] = e.timestamp;

    if (strcmp(field, "air_temp") == 0) {
      obj["value"] = serialized(String(e.airTempC, 1));
    } else if (strcmp(field, "air_humidity") == 0) {
      obj["value"] = serialized(String(e.airHumidity, 1));
    } else if (strcmp(field, "plant_height_mm") == 0) {
      obj["value"] = e.plantHeightMm;
    } else if (strcmp(field, "battery_voltage") == 0) {
      obj["value"] = serialized(String(e.batteryVoltage, 2));
    } else if (strcmp(field, "battery_percent") == 0) {
      obj["value"] = e.batteryPercent;
    } else if (strcmp(field, "lora_rssi") == 0) {
      obj["value"] = e.loraRssi;
    } else {
      continue;
    }
  }

  serializeJson(arr, buffer, bufferSize);
  return true;
}

// ── Stats ────────────────────────────────────────────────────────

int history_getCount() {
  return g_historyCount;
}

#ifdef PLANTMEISTER_NATIVE_TEST
// Test-only: reset ring buffer state between test cases (globals otherwise
// leak across tests since this module has no per-instance object).
void history_resetForTest(void) {
  g_historyIndex = 0;
  g_historyCount = 0;
  g_lastHistorySaveMs = 0;
  g_historyInitialized = false;
  memset(g_historyBuffer, 0, sizeof(g_historyBuffer));
}
#endif

#else

inline void history_init() {}
inline void history_addReading(const SensorData* sensors) { (void)sensors; }
inline void history_update() {}
inline bool history_save() { return true; }
inline bool history_getAsJson(char* buffer, size_t bufferSize, int maxEntries = 100) {
  (void)maxEntries;
  if (buffer && bufferSize > 0) {
    snprintf(buffer, bufferSize, "[]");
  }
  return false;
}
inline bool history_getField(const char* field, char* buffer, size_t bufferSize, int maxEntries = 100) {
  (void)field;
  (void)maxEntries;
  if (buffer && bufferSize > 0) {
    snprintf(buffer, bufferSize, "[]");
  }
  return false;
}
inline int history_getCount() { return 0; }

#endif // ENABLE_SENSOR_HISTORY

#endif // SENSOR_HISTORY_H
