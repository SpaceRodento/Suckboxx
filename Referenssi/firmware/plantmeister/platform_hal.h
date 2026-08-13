/*=====================================================================
  platform_hal.h - Platform Hardware Abstraction Layer

  Unified board abstraction for PlantMeister.
  Supports two hardware platforms:
  1. ESP32 WROOM-32E DevKit V1 (default) + RYLR890 LoRa (UART)
  2. XIAO ESP32S3 (future) + Wio-SX1262 LoRa (SPI)

  Set USE_XIAO_SX1262 in config.h to switch platforms.

=======================================================================*/

#ifndef PLATFORM_HAL_H
#define PLATFORM_HAL_H

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// PLATFORM IDENTIFICATION
// ═══════════════════════════════════════════════════════════════════

typedef enum {
  PLATFORM_UNKNOWN = 0,
  PLATFORM_ESP32_DEVKIT,     // ESP32 WROOM-32E + RYLR890
  PLATFORM_XIAO_ESP32S3      // XIAO ESP32S3 + Wio-SX1262
} PlatformType;

// ═══════════════════════════════════════════════════════════════════
// CAPABILITY FLAGS
// ═══════════════════════════════════════════════════════════════════

#define CAP_LORA_RYLR890      0x01   // AT-command LoRa (UART)
#define CAP_LORA_SX1262       0x02   // SPI-based LoRa (RadioLib)
#define CAP_I2C               0x04   // I2C sensors
#define CAP_USB_CDC           0x08   // USB CDC debug (ESP32S3)
#define CAP_PINS_LIMITED      0x10   // Limited GPIOs (XIAO)

// ═══════════════════════════════════════════════════════════════════
// PIN MAPPING STRUCTURE
// ═══════════════════════════════════════════════════════════════════

typedef struct {
  // LED heartbeat
  int8_t led;
  bool ledActiveHigh;

  // I2C (BME280, VL53L0X, ADS1115)
  int8_t i2cSda;
  int8_t i2cScl;

  // Motor (Stepper)
  int8_t motorStep;
  int8_t motorDir;
  int8_t motorEn;

  // Pump PWM
  int8_t pumpPin;

  // Relays
  int8_t relayLight;       // Grow light 12V
  int8_t relayAirPump;     // Air pump 12V

  // LoRa RYLR890 (UART Serial2)
  int8_t loraRx;
  int8_t loraTx;

  // LoRa SX1262 (SPI)
  int8_t loraNss;
  int8_t loraSclk;
  int8_t loraMosi;
  int8_t loraMiso;
  int8_t loraReset;
  int8_t loraBusy;
  int8_t loraDio1;
  int8_t loraRfSw;       // Wio-SX1262 pin 1: external RF switch control

  // OneWire (DS18B20 water temp)
  int8_t oneWirePin;

  // Float switch (water level)
  int8_t floatSwitchPin;
} PlatformPins;

typedef struct {
  PlatformType type;
  const char* name;
  uint8_t capabilities;
  PlatformPins pins;
} PlatformConfig;

// ═══════════════════════════════════════════════════════════════════
// GLOBAL PLATFORM STATE
// ═══════════════════════════════════════════════════════════════════

static PlatformConfig g_platform;
static bool g_platformInitialized = false;

#define PLATFORM_NAME (g_platformInitialized ? g_platform.name : "UNKNOWN")

// ═══════════════════════════════════════════════════════════════════
// PLATFORM CONFIGURATIONS
// ═══════════════════════════════════════════════════════════════════

// ESP32 WROOM-32E DevKit V1 + RYLR890
static const PlatformConfig PLATFORM_CONFIG_ESP32_DEVKIT = {
  .type = PLATFORM_ESP32_DEVKIT,
  .name = "ESP32 WROOM-32E + RYLR890",
  .capabilities = CAP_LORA_RYLR890 | CAP_I2C,
  .pins = {
    .led = 2,
    .ledActiveHigh = true,
    .i2cSda = 21,
    .i2cScl = 22,
    .motorStep = 12,
    .motorDir = 14,
    .motorEn = 27,
    .pumpPin = 26,
    .relayLight = 15,
    .relayAirPump = 4,
    .loraRx = 16,
    .loraTx = 17,
    .loraNss = -1,
    .loraSclk = -1,
    .loraMosi = -1,
    .loraMiso = -1,
    .loraReset = -1,
    .loraBusy = -1,
    .loraDio1 = -1,
    .loraRfSw = -1,
    .oneWirePin = 13,
    .floatSwitchPin = 25
  }
};

// XIAO ESP32S3 + Wio-SX1262
static const PlatformConfig PLATFORM_CONFIG_XIAO_ESP32S3 = {
  .type = PLATFORM_XIAO_ESP32S3,
  .name = "XIAO ESP32S3 + Wio-SX1262",
#if USE_XIAO_PLUS_PINS
  .capabilities = CAP_LORA_SX1262 | CAP_I2C | CAP_USB_CDC,
#else
  .capabilities = CAP_LORA_SX1262 | CAP_I2C | CAP_USB_CDC | CAP_PINS_LIMITED,
#endif
  .pins = {
    .led = 21,
    .ledActiveHigh = false,
    .i2cSda = 5,
    .i2cScl = 6,
    .motorStep = 1,
    .motorDir = 2,
    .motorEn = 3,
    .pumpPin = 14,         // Expansion-header GPIO; avoids GPIO44=D7/RX
#if USE_XIAO_PLUS_PINS
    .relayLight = 11,
#else
    .relayLight = 14,      // Shared with pump on base XIAO
#endif
    .relayAirPump = 16,    // Expansion-header GPIO; GPIO45 not exposed
    .loraRx = -1,
    .loraTx = -1,
    .loraNss = 41,
    .loraSclk = 7,
    .loraMosi = 9,
    .loraMiso = 8,
    .loraReset = 42,
    .loraBusy = 40,
    .loraDio1 = 39,
    .loraRfSw = 38,
    .oneWirePin = 4,
    .floatSwitchPin = 10
  }
};

// ═══════════════════════════════════════════════════════════════════
// PLATFORM DETECTION & INITIALIZATION
// ═══════════════════════════════════════════════════════════════════

PlatformType platform_detect() {
  // Keep platform choice aligned with config.h feature flag.
  // This avoids board/chip auto-detection ambiguity in mixed setups.
  #if USE_XIAO_SX1262
    return PLATFORM_XIAO_ESP32S3;
  #elif defined(CONFIG_IDF_TARGET_ESP32)
    return PLATFORM_ESP32_DEVKIT;
  #else
    return PLATFORM_ESP32_DEVKIT;
  #endif
}

bool platform_init() {
  if (g_platformInitialized) {
    return true;
  }

  PlatformType type = platform_detect();

  switch (type) {
    case PLATFORM_XIAO_ESP32S3:
      g_platform = PLATFORM_CONFIG_XIAO_ESP32S3;
      break;
    case PLATFORM_ESP32_DEVKIT:
    default:
      g_platform = PLATFORM_CONFIG_ESP32_DEVKIT;
      break;
  }

  g_platformInitialized = true;

  // Note: DEBUG macros not yet defined here (platform_hal.h is included
  // from config.h before the debug section). Use Serial directly.
#if ENABLE_DEBUG
  Serial.printf("[INFO]  Platform: %s (capabilities: 0x%02X)\n",
                g_platform.name, g_platform.capabilities);
#endif

  return true;
}

inline bool platform_hasCapability(uint8_t cap) {
  return (g_platform.capabilities & cap) != 0;
}

inline const PlatformPins* platform_getPins() {
  return &g_platform.pins;
}

inline PlatformType platform_getType() {
  return g_platform.type;
}

inline const char* platform_getName() {
  return g_platform.name;
}

#endif // PLATFORM_HAL_H
