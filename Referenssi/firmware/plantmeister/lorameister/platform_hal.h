/*=====================================================================
  platform_hal.h - Platform Hardware Abstraction Layer

  LoraMeister - ESP32 DevKit V1 + RYLR890 platform support.

  FEATURES:
  - Platform configuration and pin mapping
  - Capability flags
  - LED control, debug stream routing
  - Auto device address from MAC
=======================================================================*/

#ifndef PLATFORM_HAL_H
#define PLATFORM_HAL_H

#include <Arduino.h>

#if __has_include(<esp_mac.h>)
  #include <esp_mac.h>
#else
  #include <esp_system.h>
#endif

// ═══════════════════════════════════════════════════════════════════
// PLATFORM IDENTIFICATION
// ═══════════════════════════════════════════════════════════════════

typedef enum {
  PLATFORM_UNKNOWN = 0,
  PLATFORM_ESP32_DEVKIT      // ESP32 DevKit V1 + RYLR890
} PlatformType;

// ═══════════════════════════════════════════════════════════════════
// CAPABILITY FLAGS
// ═══════════════════════════════════════════════════════════════════

#define CAP_NONE              0x0000
#define CAP_LORA_RYLR896      0x0001  // AT-command LoRa (UART)
#define CAP_LCD_I2C           0x0004  // I2C LCD 16x2
#define CAP_TFT_UART          0x0008  // UART TFT display
#define CAP_UART_DEBUG        0x0010  // Hardware UART debug
#define CAP_USB_CDC_DEBUG     0x0020  // (not used on DevKit)
#define CAP_TOUCH_SENSOR      0x0040  // Capacitive touch
#define CAP_ANALOG_INPUT      0x0080  // ADC input
#define CAP_GPIO_POWER        0x0100  // Can power sensors from GPIO
#define CAP_PINS_ABUNDANT     0x0200  // Many GPIOs available
#define CAP_PINS_LIMITED      0x0400  // (not used on DevKit)
// Keep SX1262 cap defined for compatibility with code that checks it
#define CAP_LORA_SX1262       0x0002

// ═══════════════════════════════════════════════════════════════════
// PIN MAPPING
// ═══════════════════════════════════════════════════════════════════

typedef struct {
  int8_t led;
  bool ledActiveHigh;

  int8_t modeSelect;
  int8_t modeGnd;
  int8_t relayModeA;
  int8_t relayModeB;

  int8_t i2cSda;
  int8_t i2cScl;

  int8_t displayTx;
  int8_t displayRx;

  int8_t loraRx;              // ESP32 RX <- RYLR890 TX
  int8_t loraTx;              // ESP32 TX -> RYLR890 RX

  int8_t touchPin;
  int8_t lightPin;
  int8_t lightAnalog;
  int8_t proximityPin;

  int8_t batteryAdc;
  int8_t userButton;
} PlatformPins;

typedef struct {
  PlatformType type;
  const char* name;
  uint16_t capabilities;
  PlatformPins pins;
  uint8_t freePinCount;
} PlatformConfig;

// ═══════════════════════════════════════════════════════════════════
// GLOBAL PLATFORM STATE
// ═══════════════════════════════════════════════════════════════════

static PlatformConfig g_platform;
static bool g_platformInitialized = false;

// ESP32 DevKit V1 + RYLR890 configuration
static const PlatformConfig PLATFORM_CONFIG_ESP32_DEVKIT = {
  .type = PLATFORM_ESP32_DEVKIT,
  .name = "ESP32 DevKit V1 + RYLR890",
  .capabilities = CAP_LORA_RYLR896 | CAP_LCD_I2C | CAP_TFT_UART |
                  CAP_UART_DEBUG | CAP_TOUCH_SENSOR | CAP_ANALOG_INPUT |
                  CAP_GPIO_POWER | CAP_PINS_ABUNDANT,
  .pins = {
    .led = 2,
    .ledActiveHigh = true,

    .modeSelect = 16,
    .modeGnd = 17,
    .relayModeA = 19,
    .relayModeB = 20,

    .i2cSda = 21,
    .i2cScl = 22,

    .displayTx = 23,
    .displayRx = 22,

    .loraRx = 25,
    .loraTx = 26,

    .touchPin = 4,
    .lightPin = 34,
    .lightAnalog = 32,
    .proximityPin = 12,

    .batteryAdc = 35,
    .userButton = -1
  },
  .freePinCount = 20
};

// ═══════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════

PlatformType platform_detect() {
  return PLATFORM_ESP32_DEVKIT;
}

const char* platform_getName() { return g_platform.name; }
PlatformType platform_getType() { return g_platform.type; }

bool platform_init() {
  if (g_platformInitialized) return true;

  g_platform = PLATFORM_CONFIG_ESP32_DEVKIT;
  g_platformInitialized = true;

  Serial.println();
  Serial.println(F("======================================================="));
  Serial.println(F("         PLATFORM HAL - Hardware Detection            "));
  Serial.println(F("======================================================="));
  Serial.print(F("Platform:     ")); Serial.println(g_platform.name);
  Serial.print(F("Free GPIOs:   ")); Serial.println(g_platform.freePinCount);
  Serial.println(F("=======================================================\n"));

  return true;
}

// ═══════════════════════════════════════════════════════════════════
// CAPABILITY & PIN API
// ═══════════════════════════════════════════════════════════════════

bool platform_hasCapability(uint16_t capability) {
  return (g_platform.capabilities & capability) != 0;
}

uint16_t platform_getCapabilities() { return g_platform.capabilities; }

const PlatformPins* platform_getPins() { return &g_platform.pins; }

inline int8_t platform_getLedPin() { return g_platform.pins.led; }
inline bool platform_isLedActiveHigh() { return g_platform.pins.ledActiveHigh; }
inline int8_t platform_getModeSelectPin() { return g_platform.pins.modeSelect; }
inline int8_t platform_getModeGndPin() { return g_platform.pins.modeGnd; }
inline int8_t platform_getRelayModeAPin() { return g_platform.pins.relayModeA; }
inline int8_t platform_getRelayModeBPin() { return g_platform.pins.relayModeB; }
inline int8_t platform_getI2cSdaPin() { return g_platform.pins.i2cSda; }
inline int8_t platform_getI2cSclPin() { return g_platform.pins.i2cScl; }
inline int8_t platform_getDisplayTxPin() { return g_platform.pins.displayTx; }
inline int8_t platform_getDisplayRxPin() { return g_platform.pins.displayRx; }
inline int8_t platform_getLoraRxPin() { return g_platform.pins.loraRx; }
inline int8_t platform_getLoraTxPin() { return g_platform.pins.loraTx; }
inline int8_t platform_getTouchPin() { return g_platform.pins.touchPin; }
inline int8_t platform_getLightPin() { return g_platform.pins.lightPin; }

// ═══════════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════

void platform_setLed(bool on) {
  int8_t pin = g_platform.pins.led;
  if (pin < 0) return;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, on ? HIGH : LOW);
}

void platform_blinkLed(int count, int delayMs) {
  for (int i = 0; i < count; i++) {
    platform_setLed(true);
    delay(delayMs);
    platform_setLed(false);
    delay(delayMs);
  }
}

bool platform_isPinValid(int8_t pin) { return pin >= 0; }
uint8_t platform_getFreePinCount() { return g_platform.freePinCount; }

Stream* platform_getDebugStream() { return &Serial; }
bool platform_isDebugAvailable() { return true; }

void platform_printInfo() {
  Serial.println();
  Serial.println(F("======================================================="));
  Serial.print(F("Platform: ")); Serial.println(g_platform.name);
  Serial.print(F("  LED:          GPIO ")); Serial.println(g_platform.pins.led);
  Serial.print(F("  Mode Select:  GPIO ")); Serial.println(g_platform.pins.modeSelect);
  Serial.print(F("  I2C SDA:      GPIO ")); Serial.println(g_platform.pins.i2cSda);
  Serial.print(F("  I2C SCL:      GPIO ")); Serial.println(g_platform.pins.i2cScl);
  Serial.print(F("  LoRa RX:      GPIO ")); Serial.println(g_platform.pins.loraRx);
  Serial.print(F("  LoRa TX:      GPIO ")); Serial.println(g_platform.pins.loraTx);
  Serial.println(F("=======================================================\n"));
}

// ═══════════════════════════════════════════════════════════════════
// AUTO DEVICE ADDRESS (from MAC)
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_AUTO_ADDRESS
inline uint8_t getAutoDeviceAddress() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  uint32_t hash = 5381;
  for (int i = 0; i < 6; i++) {
    hash = ((hash << 5) + hash) + mac[i];
  }

  uint8_t addr = AUTO_ADDRESS_MIN + (hash % (AUTO_ADDRESS_MAX - AUTO_ADDRESS_MIN + 1));

  Serial.print("MAC: ");
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 0x10) Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" -> Auto Address: ");
  Serial.println(addr);

  return addr;
}
#endif

#endif // PLATFORM_HAL_H
