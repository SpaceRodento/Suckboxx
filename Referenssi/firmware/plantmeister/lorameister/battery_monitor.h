/*=====================================================================
  battery_monitor.h - Battery Voltage Monitor

  LoraMeister - ADC-based battery voltage monitoring

  PURPOSE:
  Monitors battery voltage via ADC through a voltage divider.
  Reports battery percentage and low/critical warnings.
  Essential for field-deployed, battery-powered sensor nodes.

  HARDWARE:
  - Voltage divider: VBAT -> R1(100kΩ) -> ADC_PIN -> R2(100kΩ) -> GND
  - This gives 1:2 ratio (max 8.4V LiPo 2S → 4.2V ADC safe)
  - For single Li-ion (3.0-4.2V), use R1=100k, R2=100k (factor=2.0)

  WIRING:
  - ADC_PIN -> GPIO35 (ADC1_CH7, input-only, perfect for ADC)
  - Voltage divider between battery+ and GND

  PAYLOAD FIELDS:
  - VBAT:<voltage_x100>  (e.g., VBAT:382 = 3.82V)
  - BPCT:<percent>        (e.g., BPCT:65 = 65%)

  ADDING THIS SENSOR:
  1. In config.h, set: #define ENABLE_BATTERY_MONITOR true
  2. In firmware.ino, add: #include "battery_monitor.h"
  3. Connect voltage divider to ADC pin
  4. Done — auto-registers with sensor registry.
=======================================================================*/

#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include "config.h"
#include "sensor_registry.h"

#if ENABLE_BATTERY_MONITOR

// ═══════════════════════════════════════════════════════════════════
// CONFIGURATION (defaults, override in config.h)
// ═══════════════════════════════════════════════════════════════════

#ifndef BATTERY_ADC_PIN
  #define BATTERY_ADC_PIN 35          // GPIO35 (ADC1_CH7, input-only)
#endif

#ifndef BATTERY_DIVIDER_FACTOR
  #define BATTERY_DIVIDER_FACTOR 2.0  // Voltage divider ratio (R1+R2)/R2
#endif

#ifndef BATTERY_READ_INTERVAL_MS
  #define BATTERY_READ_INTERVAL_MS 10000  // Read every 10 seconds
#endif

#ifndef BATTERY_VREF
  #define BATTERY_VREF 3.3            // ESP32 ADC reference voltage
#endif

// Li-ion voltage curve (single cell)
#ifndef BATTERY_VOLTAGE_FULL
  #define BATTERY_VOLTAGE_FULL 4.20   // 100% charge
#endif
#ifndef BATTERY_VOLTAGE_EMPTY
  #define BATTERY_VOLTAGE_EMPTY 3.00  // 0% charge (cutoff)
#endif
#ifndef BATTERY_LOW_THRESHOLD
  #define BATTERY_LOW_THRESHOLD 20    // Low battery warning (%)
#endif
#ifndef BATTERY_CRITICAL_THRESHOLD
  #define BATTERY_CRITICAL_THRESHOLD 5 // Critical battery warning (%)
#endif

// Sensor value indices in DeviceState.sensorValues[]
#define SENSOR_IDX_BATTERY_V    4
#define SENSOR_IDX_BATTERY_PCT  5

// ═══════════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════════

static float g_batteryVoltage = 0.0;
static int g_batteryPercent = 0;
static bool g_batteryLow = false;
static bool g_batteryCritical = false;
static unsigned long g_batteryLastRead = 0;

// ═══════════════════════════════════════════════════════════════════
// FUNCTIONS
// ═══════════════════════════════════════════════════════════════════

/**
 * Convert Li-ion voltage to percentage (approximate curve)
 * Li-ion discharge is roughly:
 *   4.2V=100%, 4.1V=90%, 3.9V=70%, 3.8V=50%, 3.7V=30%, 3.5V=10%, 3.0V=0%
 */
static int batteryVoltageToPercent(float voltage) {
  if (voltage >= BATTERY_VOLTAGE_FULL) return 100;
  if (voltage <= BATTERY_VOLTAGE_EMPTY) return 0;

  // Linear mapping (good enough for monitoring, not lab-precise)
  float range = BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY;
  float level = voltage - BATTERY_VOLTAGE_EMPTY;
  return (int)(level / range * 100.0);
}

void battery_init() {
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);  // 12-bit ADC (0-4095)
  // Use ADC attenuation for full range (up to ~3.3V)
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);

  Serial.printf("[BAT] Monitor on GPIO%d, divider=%.1fx\n",
    BATTERY_ADC_PIN, BATTERY_DIVIDER_FACTOR);
}

void battery_update() {
  if (millis() - g_batteryLastRead < BATTERY_READ_INTERVAL_MS) return;
  g_batteryLastRead = millis();

  // Read ADC (average 8 samples for stability)
  uint32_t adcSum = 0;
  for (int i = 0; i < 8; i++) {
    adcSum += analogRead(BATTERY_ADC_PIN);
  }
  float adcAvg = adcSum / 8.0;

  // Convert to voltage
  float adcVoltage = (adcAvg / 4095.0) * BATTERY_VREF;
  g_batteryVoltage = adcVoltage * BATTERY_DIVIDER_FACTOR;

  // Calculate percentage
  g_batteryPercent = batteryVoltageToPercent(g_batteryVoltage);

  // Check thresholds
  bool wasLow = g_batteryLow;
  bool wasCritical = g_batteryCritical;
  g_batteryLow = (g_batteryPercent <= BATTERY_LOW_THRESHOLD);
  g_batteryCritical = (g_batteryPercent <= BATTERY_CRITICAL_THRESHOLD);

  // Warn on state change
  if (g_batteryCritical && !wasCritical) {
    Serial.printf("[BAT] CRITICAL: %.2fV (%d%%)\n", g_batteryVoltage, g_batteryPercent);
  } else if (g_batteryLow && !wasLow) {
    Serial.printf("[BAT] LOW: %.2fV (%d%%)\n", g_batteryVoltage, g_batteryPercent);
  }
}

int battery_buildPayload(char* buf, int maxLen) {
  // Send voltage as integer x100 (e.g., 3.82V → 382) to avoid float in payload
  int vx100 = (int)(g_batteryVoltage * 100);
  return snprintf(buf, maxLen, "VBAT:%d,BPCT:%d", vx100, g_batteryPercent);
}

bool battery_parseField(const char* key, const char* value, DeviceState& dev) {
  if (strcmp(key, "VBAT") == 0) {
    dev.sensorValues[SENSOR_IDX_BATTERY_V] = atoi(value) / 100.0f;
    return true;
  }
  if (strcmp(key, "BPCT") == 0) {
    dev.sensorValues[SENSOR_IDX_BATTERY_PCT] = atoi(value);
    return true;
  }
  return false;
}

// Getter functions
float battery_getVoltage() { return g_batteryVoltage; }
int battery_getPercent() { return g_batteryPercent; }
bool battery_isLow() { return g_batteryLow; }
bool battery_isCritical() { return g_batteryCritical; }

// Auto-register with sensor registry
REGISTER_SENSOR("Battery", battery_init, battery_update, battery_buildPayload, battery_parseField);

#endif // ENABLE_BATTERY_MONITOR
#endif // BATTERY_MONITOR_H
