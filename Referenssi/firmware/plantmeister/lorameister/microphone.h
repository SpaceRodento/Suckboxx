/*=====================================================================
  microphone.h - GY MAX4466 Electret Microphone Module

  LoraMeister - Analog sound level measurement

  HARDWARE:
  - GY MAX4466 module (electret + preamp, adjustable gain)
  - Output: analog voltage centered at VCC/2 (~1.65V @ 3.3V)
  - Audio signal is superimposed on DC bias
  - Gain adjustable via onboard potentiometer (25x - 125x)

  WIRING:
  - OUT -> GPIO 34 (ADC1_CH6, input-only GPIO)
  - VCC -> 3.3V
  - GND -> GND

  READING METHOD:
  - Sample ADC rapidly over a 50ms window
  - Track min and max values
  - Peak-to-peak amplitude = max - min
  - Convert to approximate dB SPL

  NOTE: dB values are approximate. For calibrated readings,
  use a reference sound source and adjust MIC_DB_OFFSET.
=======================================================================*/

#ifndef MICROPHONE_H
#define MICROPHONE_H

#include "config.h"
#include "sensor_registry.h"

#if ENABLE_MICROPHONE

// ═══════════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════════

static uint16_t g_micPeakToPeak = 0;    // Last peak-to-peak ADC value
static uint8_t  g_micDB = 0;            // Estimated dB level
static unsigned long g_micLastRead = 0;  // Last read timestamp

// ═══════════════════════════════════════════════════════════════════
// FUNCTIONS
// ═══════════════════════════════════════════════════════════════════

/**
 * Initialize microphone ADC pin
 */
void mic_init() {
  pinMode(MIC_PIN, INPUT);
  analogReadResolution(12);  // 12-bit ADC (0-4095)

  Serial.print(F("[MIC] MAX4466 on GPIO "));
  Serial.println(MIC_PIN);
}

/**
 * Read sound level from MAX4466
 *
 * Samples ADC rapidly over MIC_SAMPLE_WINDOW_MS and measures
 * peak-to-peak amplitude. Higher p2p = louder sound.
 *
 * @return Peak-to-peak ADC value (0-4095)
 */
uint16_t mic_readPeakToPeak() {
  uint16_t signalMax = 0;
  uint16_t signalMin = 4095;
  unsigned long startMs = millis();

  // Sample for the window duration
  while (millis() - startMs < MIC_SAMPLE_WINDOW_MS) {
    uint16_t sample = analogRead(MIC_PIN);
    if (sample > signalMax) signalMax = sample;
    if (sample < signalMin) signalMin = sample;
  }

  uint16_t peakToPeak = signalMax - signalMin;
  return peakToPeak;
}

/**
 * Convert peak-to-peak ADC value to approximate dB SPL
 *
 * This is a rough logarithmic mapping. The MAX4466 with default
 * gain (~60x) at 3.3V maps roughly:
 *   p2p 0-50:    ~30 dB (quiet room)
 *   p2p 50-200:  ~40-50 dB (normal conversation)
 *   p2p 200-1000: ~50-70 dB (loud talking)
 *   p2p 1000-4000: ~70-100 dB (very loud)
 *
 * @param peakToPeak Raw ADC peak-to-peak value
 * @return Estimated dB SPL (MIC_DB_MIN to MIC_DB_MAX)
 */
uint8_t mic_peakToDb(uint16_t peakToPeak) {
  if (peakToPeak <= MIC_NOISE_FLOOR) return MIC_DB_MIN;

  // Logarithmic mapping: dB = 20 * log10(value)
  // Simplified for ESP32: use integer approximation
  float voltage = (float)peakToPeak / 4095.0 * 3.3;  // Convert to voltage
  float db = 20.0 * log10(voltage / 0.003);           // Reference: ~3mV noise floor

  // Clamp to display range
  if (db < MIC_DB_MIN) db = MIC_DB_MIN;
  if (db > MIC_DB_MAX) db = MIC_DB_MAX;

  return (uint8_t)db;
}

/**
 * Update microphone readings (call from loop)
 * Non-blocking: only reads when interval has elapsed
 */
void mic_update() {
  if (millis() - g_micLastRead < MIC_SEND_INTERVAL_MS) return;

  g_micPeakToPeak = mic_readPeakToPeak();
  g_micDB = mic_peakToDb(g_micPeakToPeak);
  g_micLastRead = millis();

  #if DEBUG_MICROPHONE
    Serial.printf("[MIC] p2p=%d  dB=%d\n", g_micPeakToPeak, g_micDB);
  #endif
}

/**
 * Get last peak-to-peak reading
 */
uint16_t mic_getPeakToPeak() { return g_micPeakToPeak; }

/**
 * Get last dB reading
 */
uint8_t mic_getDB() { return g_micDB; }

/**
 * Generate a simple bar graph string for LCD (16 chars max)
 * Example: "##########      " for ~60% volume
 *
 * @param width Bar width in characters (max 16)
 * @return Number of filled bars
 */
uint8_t mic_getBarLevel(uint8_t width) {
  if (g_micPeakToPeak <= MIC_NOISE_FLOOR) return 0;

  // Map p2p to bar level (logarithmic feel)
  uint16_t level = map(g_micDB, MIC_DB_MIN, MIC_DB_MAX, 0, width);
  if (level > width) level = width;
  return (uint8_t)level;
}

// ═══════════════════════════════════════════════════════════════════
// SENSOR REGISTRY INTEGRATION
// ═══════════════════════════════════════════════════════════════════

// Sensor value indices for receiver-side DeviceState.sensorValues[]
#define SENSOR_IDX_MIC_P2P  0
#define SENSOR_IDX_MIC_DB   1

/**
 * Build microphone payload fields: MIC:<p2p>,MICDB:<db>
 */
int mic_buildPayload(char* buf, int maxLen) {
  return snprintf(buf, maxLen, "MIC:%d,MICDB:%d",
    g_micPeakToPeak, g_micDB);
}

/**
 * Parse MIC/MICDB fields from received payload
 */
bool mic_parseField(const char* key, const char* value, DeviceState& dev) {
  if (strcmp(key, "MIC") == 0) {
    dev.micPeakToPeak = atoi(value);
    dev.sensorValues[SENSOR_IDX_MIC_P2P] = dev.micPeakToPeak;
    return true;
  }
  if (strcmp(key, "MICDB") == 0) {
    dev.micDB = atoi(value);
    dev.sensorValues[SENSOR_IDX_MIC_DB] = dev.micDB;
    return true;
  }
  return false;
}

// Self-register with sensor registry
REGISTER_SENSOR("MAX4466", mic_init, mic_update, mic_buildPayload, mic_parseField);

#endif // ENABLE_MICROPHONE
#endif // MICROPHONE_H
