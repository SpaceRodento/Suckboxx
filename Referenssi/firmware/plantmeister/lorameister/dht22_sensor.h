/*=====================================================================
  dht22_sensor.h - DHT22 Temperature & Humidity Sensor

  LoraMeister - Example sensor module using sensor_registry.h

  HARDWARE:
  - DHT22 (AM2302) digital temperature/humidity sensor
  - Output: 1-Wire digital protocol
  - Range: -40°C to +80°C, 0-100% RH
  - Accuracy: ±0.5°C, ±2-5% RH

  WIRING:
  - DATA -> GPIO 4 (or configure DHT22_PIN in config.h)
  - VCC  -> 3.3V
  - GND  -> GND
  - 10kΩ pull-up resistor between DATA and VCC

  ADDING THIS SENSOR:
  1. In config.h, set: #define ENABLE_DHT22 true
  2. In firmware.ino, add: #include "dht22_sensor.h"
  3. Install library: DHT sensor library by Adafruit (PlatformIO)
  4. That's it! The sensor auto-registers with the registry.

  PAYLOAD FIELDS:
  - TEMP:<value>  (temperature in °C, 1 decimal)
  - HUM:<value>   (humidity in %, 1 decimal)

  NOTE: This is a TEMPLATE/EXAMPLE. The DHT library include is
  conditional — if the library is not installed, this file compiles
  but sensors report 0. Install the library for real readings.
=======================================================================*/

#ifndef DHT22_SENSOR_H
#define DHT22_SENSOR_H

#include "config.h"
#include "sensor_registry.h"

#if ENABLE_DHT22

// ═══════════════════════════════════════════════════════════════════
// CONFIGURATION (defaults, override in config.h)
// ═══════════════════════════════════════════════════════════════════

#ifndef DHT22_PIN
  #define DHT22_PIN 4               // Default: GPIO4 (shared with touch — pick one!)
#endif

#ifndef DHT22_READ_INTERVAL_MS
  #define DHT22_READ_INTERVAL_MS 2000  // DHT22 minimum: 2 seconds between reads
#endif

// Sensor value indices in DeviceState.sensorValues[]
#define SENSOR_IDX_TEMPERATURE  2
#define SENSOR_IDX_HUMIDITY     3

// ═══════════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════════

static float g_dht22_temperature = 0.0;
static float g_dht22_humidity = 0.0;
static unsigned long g_dht22_lastRead = 0;
static bool g_dht22_valid = false;

// ═══════════════════════════════════════════════════════════════════
// DHT22 DRIVER (minimal, no library dependency)
// ═══════════════════════════════════════════════════════════════════
// Lightweight bit-bang DHT22 reader. No external library needed.
// Protocol: host pulls low 1ms, releases, sensor responds with
// 40 bits (16 humidity + 16 temperature + 8 checksum).

static bool dht22_readRaw(uint8_t pin, float& temp, float& hum) {
  uint8_t data[5] = {0};

  // Host start signal: pull low 1ms, then release
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(1);
  digitalWrite(pin, HIGH);
  delayMicroseconds(30);
  pinMode(pin, INPUT_PULLUP);

  // Wait for sensor response (low 80µs, high 80µs)
  unsigned long timeout = micros();
  while (digitalRead(pin) == HIGH) {
    if (micros() - timeout > 100) return false;
  }
  timeout = micros();
  while (digitalRead(pin) == LOW) {
    if (micros() - timeout > 100) return false;
  }
  timeout = micros();
  while (digitalRead(pin) == HIGH) {
    if (micros() - timeout > 100) return false;
  }

  // Read 40 bits (5 bytes)
  for (int i = 0; i < 40; i++) {
    // Wait for low to end (start of bit)
    timeout = micros();
    while (digitalRead(pin) == LOW) {
      if (micros() - timeout > 100) return false;
    }

    // Measure high duration: <28µs = 0, >70µs = 1
    unsigned long highStart = micros();
    timeout = micros();
    while (digitalRead(pin) == HIGH) {
      if (micros() - timeout > 100) return false;
    }
    unsigned long highDuration = micros() - highStart;

    data[i / 8] <<= 1;
    if (highDuration > 40) {
      data[i / 8] |= 1;
    }
  }

  // Checksum verification
  uint8_t checksum = data[0] + data[1] + data[2] + data[3];
  if (checksum != data[4]) return false;

  // Parse humidity (16 bits, unsigned, /10)
  hum = ((data[0] << 8) | data[1]) * 0.1f;

  // Parse temperature (16 bits, MSB = sign, /10)
  int16_t rawTemp = ((data[2] & 0x7F) << 8) | data[3];
  if (data[2] & 0x80) rawTemp = -rawTemp;
  temp = rawTemp * 0.1f;

  return true;
}

// ═══════════════════════════════════════════════════════════════════
// SENSOR REGISTRY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════

void dht22_init() {
  pinMode(DHT22_PIN, INPUT_PULLUP);
  Serial.printf("[DHT22] Pin GPIO%d, interval %dms\n", DHT22_PIN, DHT22_READ_INTERVAL_MS);
}

void dht22_update() {
  if (millis() - g_dht22_lastRead < DHT22_READ_INTERVAL_MS) return;
  g_dht22_lastRead = millis();

  float temp, hum;
  if (dht22_readRaw(DHT22_PIN, temp, hum)) {
    g_dht22_temperature = temp;
    g_dht22_humidity = hum;
    g_dht22_valid = true;

    #if DEBUG_DHT22
      Serial.printf("[DHT22] T=%.1f°C  H=%.1f%%\n", temp, hum);
    #endif
  } else {
    g_dht22_valid = false;
    #if DEBUG_DHT22
      Serial.println("[DHT22] Read failed");
    #endif
  }
}

int dht22_buildPayload(char* buf, int maxLen) {
  if (!g_dht22_valid) return 0;
  return snprintf(buf, maxLen, "TEMP:%.1f,HUM:%.1f",
    g_dht22_temperature, g_dht22_humidity);
}

bool dht22_parseField(const char* key, const char* value, DeviceState& dev) {
  if (strcmp(key, "TEMP") == 0) {
    dev.sensorValues[SENSOR_IDX_TEMPERATURE] = atof(value);
    return true;
  }
  if (strcmp(key, "HUM") == 0) {
    dev.sensorValues[SENSOR_IDX_HUMIDITY] = atof(value);
    return true;
  }
  return false;
}

// Getter functions
float dht22_getTemperature() { return g_dht22_temperature; }
float dht22_getHumidity() { return g_dht22_humidity; }
bool dht22_isValid() { return g_dht22_valid; }

// Auto-register with sensor registry
REGISTER_SENSOR("DHT22", dht22_init, dht22_update, dht22_buildPayload, dht22_parseField);

#endif // ENABLE_DHT22
#endif // DHT22_SENSOR_H
