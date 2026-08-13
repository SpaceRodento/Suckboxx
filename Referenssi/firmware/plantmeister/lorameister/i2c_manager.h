/*=====================================================================
  i2c_manager.h - Unified I2C Bus Coordinator

  Central I2C bus management preventing multiple Wire.begin() calls
  and providing device detection and diagnostics.

  PURPOSE:
  Coordinates all I2C communication on the shared I2C bus to prevent
  initialization conflicts between multiple I2C peripherals (LCD,
  sensors). Call once before initializing any I2C devices.

  I2C BUS CONFIGURATION (ESP32 Hardware I2C):
  - SDA: GPIO 21 (fixed on ESP32)
  - SCL: GPIO 22 (fixed on ESP32)
  - Speed: 100 kHz (standard mode, default)
  - Pull-ups: External 4.7kΩ recommended (most modules have built-in)

  SUPPORTED DEVICES:
  - LCD 16x2 (0x27) - I2C backpack, receiver only
    Config: ENABLE_LCD in config.h

  - INA219 (0x40) - Current/voltage/power monitor
    Config: ENABLE_CURRENT_MONITOR in config.h

  Note: Light detection uses LM393 (non-I2C, GPIO analog/digital)

  FEATURES:
  - Single Wire.begin() initialization (prevents conflicts)
  - I2C bus scanning and device detection
  - Device presence verification
  - Startup diagnostics

  USAGE:
  1. Call ensureI2CInitialized() BEFORE initializing any I2C devices
  2. Optionally call scanI2CBus() for diagnostics during development

  EXAMPLE:
    #include "i2c_manager.h"

    void setup() {
      ensureI2CInitialized();  // Initialize I2C bus once

      // Now safe to initialize I2C devices:
      #if ENABLE_LCD
        lcd.init();
      #endif

      #if ENABLE_LIGHT_DETECTION
        tcs.begin();
      #endif

      #if ENABLE_CURRENT_MONITOR
        ina219.begin();
      #endif
    }

  TROUBLESHOOTING:
  If devices not detected:
  - Check I2C address with scanI2CBus()
  - Verify SDA/SCL connections (GPIO 21/22)
  - Check pull-up resistors (4.7kΩ to 3.3V)
  - Try reducing I2C speed if cable is long

=======================================================================*/

#ifndef I2C_MANAGER_H
#define I2C_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// I2C bus state
static bool i2cInitialized = false;
static unsigned long i2cInitTime = 0;

// I2C device addresses
#define I2C_LCD_ADDRESS      0x27  // LCD 16x2 (receiver only)
#define I2C_INA219_ADDRESS   0x40  // INA219 current sensor

/**
 * Initialize I2C bus (only once).
 * Safe to call multiple times - initializes only on first call.
 * Uses I2C_SDA_PIN and I2C_SCL_PIN from config.h
 */
void ensureI2CInitialized() {
  if (!i2cInitialized) {
    // Use pins from config.h (supports both ESP32 DevKit and XIAO ESP32S3)
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    i2cInitialized = true;
    i2cInitTime = millis();

    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║    I2C BUS INITIALIZED                 ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.print("  SDA: GPIO ");
    Serial.println(I2C_SDA_PIN);
    Serial.print("  SCL: GPIO ");
    Serial.println(I2C_SCL_PIN);
    Serial.println("  Speed: 100 kHz (default)");
    Serial.println();

    // List expected devices
    Serial.println("Expected I2C devices:");
    Serial.println("  - 0x27: LCD 16x2 (receiver only)");

    // APDS9960 (0x39) removed - replaced by LM393 (non-I2C)

    #if ENABLE_CURRENT_MONITOR
    Serial.println("  - 0x40: INA219 current sensor");
    #endif

    Serial.println();
  }
}

/**
 * Scan I2C bus and list found devices.
 * Use for diagnostics and device identification.
 */
void scanI2CBus() {
  if (!i2cInitialized) {
    Serial.println("⚠️  I2C not initialized! Call ensureI2CInitialized() first.");
    return;
  }

  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║    I2C BUS SCAN                        ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println();

  int devicesFound = 0;

  Serial.println("Scanning I2C bus (0x01 - 0x7F)...");
  Serial.println();

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();

    if (error == 0) {
      // Device found
      devicesFound++;

      Serial.print("✓ Device found at 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      Serial.print("  ");

      // Identify device
      switch (addr) {
        case 0x27:
          Serial.println("(LCD 16x2)");
          break;
        case 0x29:
          Serial.println("(TCS34725 RGB sensor - not used)");
          break;
        case 0x3C:
          Serial.println("(OLED display)");
          break;
        case 0x40:
          Serial.println("(INA219 current sensor)");
          break;
        case 0x48:
          Serial.println("(ADS1115 ADC)");
          break;
        case 0x68:
          Serial.println("(MPU6050 / DS1307 RTC)");
          break;
        case 0x76:
        case 0x77:
          Serial.println("(BME280 / BMP280)");
          break;
        default:
          Serial.println("(Unknown device)");
      }
    }
    else if (error == 4) {
      // Unknown error
      Serial.print("⚠️  Error at address 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
    }
  }

  Serial.println();

  if (devicesFound == 0) {
    Serial.println("❌ No I2C devices found!");
    Serial.println();
    Serial.println("Troubleshooting:");
    Serial.println("  1. Check wiring (SDA=21, SCL=22, GND, VCC)");
    Serial.println("  2. Verify device has power");
    Serial.println("  3. Check pull-up resistors (usually built-in)");
    Serial.println("  4. Try different I2C address (some devices configurable)");
  } else {
    Serial.print("✓ Found ");
    Serial.print(devicesFound);
    Serial.println(" device(s) on I2C bus.");
  }

  Serial.println();
}

/**
 * Check if I2C bus is initialized.
 * @return true if initialized, false otherwise
 */
bool isI2CInitialized() {
  return i2cInitialized;
}

/**
 * Get I2C initialization time (milliseconds).
 * @return Time when I2C was initialized
 */
unsigned long getI2CInitTime() {
  return i2cInitTime;
}

/**
 * Check if specific I2C device is present and responding.
 *
 * @param address I2C address (0x00-0x7F)
 * @return true if device responds, false otherwise
 */
bool isI2CDevicePresent(byte address) {
  if (!i2cInitialized) {
    return false;
  }

  Wire.beginTransmission(address);
  byte error = Wire.endTransmission();

  return (error == 0);
}

/**
 * Print I2C bus diagnostics.
 * Shows initialization status and detected devices.
 */
void printI2CDiagnostics() {
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║    I2C DIAGNOSTICS                     ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println();

  Serial.print("Initialized: ");
  Serial.println(i2cInitialized ? "YES" : "NO");

  if (i2cInitialized) {
    Serial.print("Init time: ");
    Serial.print(i2cInitTime);
    Serial.println(" ms");

    Serial.print("Uptime: ");
    Serial.print((millis() - i2cInitTime) / 1000);
    Serial.println(" seconds");

    Serial.println();
    Serial.println("Expected devices:");

    // LCD (always on receiver)
    Serial.print("  LCD 16x2 (0x27): ");
    Serial.println(isI2CDevicePresent(I2C_LCD_ADDRESS) ? "✓ FOUND" : "❌ NOT FOUND");

    // TCS34725 (0x29) removed - light detection uses LM393 (non-I2C)

    #if ENABLE_CURRENT_MONITOR
    Serial.print("  INA219 (0x40): ");
    Serial.println(isI2CDevicePresent(I2C_INA219_ADDRESS) ? "✓ FOUND" : "❌ NOT FOUND");
    #endif
  }

  Serial.println();
}

#endif // I2C_MANAGER_H
