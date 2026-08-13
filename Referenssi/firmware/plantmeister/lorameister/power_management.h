/*=====================================================================
  power_management.h - Unified Power Management System

  LoraMeister - CPU frequency control, sleep modes, and GPIO power
  management for ESP32 power optimization.

  FEATURES:

  1. CPU FREQUENCY CONTROL
     - Reduce CPU frequency from 240 MHz to 80 MHz for lower power
     - Saves ~50-60% power consumption during normal operation
     - Configure: ENABLE_POWER_SAVE in config.h

  2. SLEEP MODES (Foundation - Not yet implemented)
     - Light sleep: Quick wake, maintains RAM
     - Deep sleep: Full power down, ESP32 resets on wake
     - Configure: POWER_SLEEP_MODE in config.h

  3. GPIO POWER OUTPUTS
     - Use GPIO pins as 3.3V power sources for low-power sensors
     - Solves VCC pin shortage on ESP32 DevKit V1
     - Soft-start delays prevent inrush current surge
     - Configure: *_USE_GPIO_POWER, *_VCC_PIN in config.h

  3B. BATTERY MONITORING
     - Read battery voltage via external voltage divider + ADC
     - BatteryStatus struct: voltage, percent, isLow, isCritical
     - Uses HAL pin (g_platform.pins.batteryAdc) for platform independence
     - Configure: ENABLE_BATTERY_MONITOR in config.h

  CONFIGURATION (config.h):
  ═══════════════════════════════════════════════════════════════════

  // Master power save enable
  #define ENABLE_POWER_SAVE false  // Set true to enable

  // Sleep mode selection (not yet implemented)
  #define POWER_SLEEP_MODE 0  // 0=none, 1=light, 2=deep

  // GPIO power for LCD (example)
  #define ENABLE_GPIO_POWER true
  #define LCD_USE_GPIO_POWER true
  #define LCD_VCC_PIN 19
  #define LCD_EXPECTED_CURRENT_MA 15

  See config.h lines 150-200 for complete GPIO power configuration.

  USAGE:
  ═══════════════════════════════════════════════════════════════════

  void setup() {
    setCpuFrequencyMhz(80);  // Reduce CPU freq for power savings
    initPowerManagement();   // Initialize both systems
    // ... initialize sensors AFTER GPIO power is enabled ...
  }

  void loop() {
    // ... do work ...
    powerManagerIdle(1900);  // Sleep 1.9s between actions (future)
  }

  // Power cycle a frozen sensor
  powerCycleDevice(LCD_VCC_PIN, 500);

  SAFETY LIMITS (ESP32 datasheet):
  ═══════════════════════════════════════════════════════════════════

  ⚠️  CRITICAL - DO NOT EXCEED:
  - Maximum 40mA per GPIO pin (28mA recommended continuous)
  - Maximum 200mA total for ALL GPIO pins combined
  - Never use GPIO power for high-current devices (>30mA)

  Safe for: LCD I2C, light sensors, proximity sensors (<20mA each)
  NEVER for: LoRa modules, relays, motors (>100mA)

  VERSION HISTORY:
  ═══════════════════════════════════════════════════════════════════

  2026-01-05: Unified power_manager.h and gpio_power_manager.h
              - Clearer structure with labeled sections
              - English comments throughout
              - Added config.h line references

=======================================================================*/

#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <Arduino.h>
#include "config.h"

// ESP32 sleep libraries
#include <esp_sleep.h>
#include <esp_pm.h>
#include <driver/gpio.h>    // gpio_wakeup_enable/disable, GPIO_INTR_HIGH_LEVEL

// USBSerial (HWCDC) is available globally when ARDUINO_USB_CDC_ON_BOOT is set

// flash_logger.h not included (ENABLE_FLASH_LOGGER = false)

// ═══════════════════════════════════════════════════════════════════
// SECTION 1: CPU FREQUENCY & SLEEP MODE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════

// Master power save enable (configured in config.h, fallback below)
#ifndef ENABLE_POWER_SAVE
#define ENABLE_POWER_SAVE false
#endif

// Sleep mode selection
#ifndef POWER_SLEEP_MODE
#define POWER_SLEEP_MODE 0  // 0=none, 1=light, 2=deep
#endif

// Sleep durations
#ifndef POWER_LIGHT_SLEEP_MS
#define POWER_LIGHT_SLEEP_MS 1900  // Light sleep duration (ms)
#endif

#ifndef POWER_DEEP_SLEEP_S
#define POWER_DEEP_SLEEP_S 60  // Deep sleep duration (seconds)
#endif

// Power states
typedef enum {
    POWER_STATE_ACTIVE,      // Full operation
    POWER_STATE_IDLE,        // Reduced activity
    POWER_STATE_LIGHT_SLEEP, // Light sleep (quick wake)
    POWER_STATE_DEEP_SLEEP   // Deep sleep (full reset on wake)
} PowerState;

// Power statistics
typedef struct {
    unsigned long activeTimeMs;      // Time in active state
    unsigned long idleTimeMs;        // Time in idle state
    unsigned long sleepTimeMs;       // Time in sleep states
    unsigned long lastStateChange;   // Timestamp of last state change
    PowerState currentState;         // Current power state
    uint8_t cpuFreqMHz;             // Current CPU frequency
} PowerStats;

// Global power statistics
PowerStats powerStats = {
    .activeTimeMs = 0,
    .idleTimeMs = 0,
    .sleepTimeMs = 0,
    .lastStateChange = 0,
    .currentState = POWER_STATE_ACTIVE,
    .cpuFreqMHz = 80  // Set by setup()
};

// ═══════════════════════════════════════════════════════════════════
// SECTION 2: GPIO POWER MANAGEMENT CONFIGURATION
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_GPIO_POWER

// Safety limits (ESP32 datasheet)
#define GPIO_MAX_CURRENT_PER_PIN 40      // mA (absolute maximum)
#define GPIO_SAFE_CURRENT_PER_PIN 28     // mA (recommended continuous)
#define GPIO_MAX_TOTAL_CURRENT 200       // mA (all GPIO pins combined)

// Soft start delay (prevents inrush current surge)
#define GPIO_POWER_SOFT_START_MS 100     // ms between power-ups
#define GPIO_POWER_STABILIZE_MS 200      // ms to wait after all powered

// Default current estimates (if not specified in config.h)
#ifndef LCD_EXPECTED_CURRENT_MA
  #define LCD_EXPECTED_CURRENT_MA 15
#endif
#ifndef LIGHT_EXPECTED_CURRENT_MA
  #define LIGHT_EXPECTED_CURRENT_MA 5
#endif
#ifndef PROXIMITY_EXPECTED_CURRENT_MA
  #define PROXIMITY_EXPECTED_CURRENT_MA 12
#endif
#ifndef AUDIO_EXPECTED_CURRENT_MA
  #define AUDIO_EXPECTED_CURRENT_MA 10
#endif

// Power pin structure
struct PowerPin {
  uint8_t vccPin;               // GPIO pin for VCC (3.3V)
  int8_t gndPin;                // GPIO pin for GND (0V) - optional (-1 = use board GND)
  const char* name;             // Device name (for logging)
  uint16_t expectedCurrent_mA;  // Expected current draw (mA)
  bool enabled;                 // Is this power pin enabled?
  bool powered;                 // Is power currently ON?
  unsigned long powerOnTime;    // When was power turned ON?
};

// Track all power pins
static PowerPin powerPins[8] = {0};  // Max 8 devices
static uint8_t powerPinCount = 0;
static uint16_t totalEstimatedCurrent = 0;
static bool gpioPowerInitialized = false;
static bool safetyShutdown = false;

#endif // ENABLE_GPIO_POWER

// ═══════════════════════════════════════════════════════════════════
// SECTION 3: CPU FREQUENCY & SLEEP MODE FUNCTIONS
// ═══════════════════════════════════════════════════════════════════

/**
 * @brief Initialize CPU frequency management
 *
 * Called once in setup() - sets CPU frequency and logs status.
 * Currently only reduces CPU frequency. Sleep modes are stubs for
 * future implementation.
 */
void initCpuPowerManagement() {
    powerStats.lastStateChange = millis();
    powerStats.cpuFreqMHz = getCpuFrequencyMhz();

    Serial.println("----------------------------------------");
    Serial.println("CPU Power Management initialized");
    Serial.print("  CPU frequency: ");
    Serial.print(powerStats.cpuFreqMHz);
    Serial.println(" MHz");

    #if ENABLE_POWER_SAVE
    Serial.println("  Power save: ENABLED");
    Serial.print("  Sleep mode: ");
    #if POWER_SLEEP_MODE == 1
    Serial.println("Light sleep");
    #elif POWER_SLEEP_MODE == 2
    Serial.println("Deep sleep");
    #else
    Serial.println("None");
    #endif
    #else
    Serial.println("  Power save: DISABLED (development mode)");
    Serial.println("  To enable: Set ENABLE_POWER_SAVE true in config.h");
    #endif

    Serial.println("----------------------------------------");
}

/**
 * @brief Enter idle/sleep state for specified duration
 *
 * CURRENTLY: Just uses delay() - no actual sleep
 * FUTURE: Will use light sleep when ENABLE_POWER_SAVE is true
 *
 * @param ms Duration in milliseconds
 */
void powerManagerIdle(uint32_t ms) {
    #if ENABLE_POWER_SAVE && POWER_SLEEP_MODE == 1
    unsigned long sleepStart = millis();

    // Feed watchdog before sleep to prevent spurious reset
    #if ENABLE_WATCHDOG
    esp_task_wdt_reset();
    #endif

    // Optional GPIO wake (configure POWER_WAKE_PIN in config.h)
    #ifdef POWER_WAKE_PIN
    gpio_wakeup_enable((gpio_num_t)POWER_WAKE_PIN, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    #endif

    // Timer wake (primary wake source)
    if (ms > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
    }

    // Enter light sleep — CPU stops, RAM maintained, ~0.8mA
    esp_light_sleep_start();

    // Post-wake cleanup
    #ifdef POWER_WAKE_PIN
    gpio_wakeup_disable((gpio_num_t)POWER_WAKE_PIN);
    #endif
    if (ms > 0) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }

    // Update sleep stats
    powerStats.sleepTimeMs += (millis() - sleepStart);

    // Feed watchdog immediately after wake
    #if ENABLE_WATCHDOG
    esp_task_wdt_reset();
    #endif

    #else
    // Power save disabled or mode != 1: normal delay
    delay(ms > 0 ? ms : 10);
    powerStats.idleTimeMs += (ms > 0 ? ms : 10);
    #endif
}

/**
 * @brief Enter deep sleep for specified duration
 *
 * CURRENTLY: Stub - does nothing
 * FUTURE: Will enter deep sleep (ESP32 resets on wake)
 *
 * WARNING: Deep sleep causes full reset! All variables lost.
 * Use RTC_DATA_ATTR for persistent data.
 *
 * @param seconds Duration in seconds
 */
void powerManagerDeepSleep(uint32_t seconds) {
    #if ENABLE_POWER_SAVE && POWER_SLEEP_MODE == 2

    Serial.println();
    Serial.println(F("======================================================="));
    Serial.println(F("         ENTERING DEEP SLEEP                           "));
    Serial.println(F("======================================================="));
    Serial.print(F("  Duration:    "));
    Serial.print(seconds);
    Serial.println(F(" seconds"));
    Serial.println(F("  Wake cause:  TIMER"));
    Serial.println(F("  Radio:       sleeping (lora_sleep called by caller)"));
    Serial.println(F("  Next boot:   full reboot → setup()"));
    Serial.println(F("======================================================="));

    // Päivitä tilastot
    powerStats.sleepTimeMs += (unsigned long)seconds * 1000UL;
    powerStats.currentState = POWER_STATE_DEEP_SLEEP;

    // Tallenna sleep-statistiikka NVS:ään
    #if ENABLE_NVS_STORAGE
    nvs.putULong("sleepTimeMs", powerStats.sleepTimeMs);
    #endif

    Serial.println(F("[PowerManager] Serial flush + sleep..."));
    Serial.flush();  // Varmista kaiken tulostuksen lähteminen

    // Pysäytä watchdog (deep sleep = reboot, WDT ei saa laueta ennen sitä)
    #if ENABLE_WATCHDOG
    esp_task_wdt_deinit();
    #endif

    // Aseta timer-herätys
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);

    esp_deep_sleep_start();
    // Suoritus EI koskaan jatku tänne — ESP32 rebootaa herätessään

    #else
    Serial.println(F("[PowerManager] Deep sleep disabled (POWER_SLEEP_MODE != 2)"));
    #endif
}

/**
 * @brief Set LoRa module sleep mode (legacy wrapper)
 *
 * Radio sleep/wake delegated to lora_hal.h: lora_sleep() / lora_wake()
 * Call those directly from firmware.ino instead of this function.
 *
 * @param sleep true=sleep, false=wake
 */
void setLoRaSleepMode(bool sleep) {
    #if ENABLE_POWER_SAVE
    // Radio sleep/wake handled by lora_hal.h: lora_sleep() / lora_wake()
    // This function is kept for backward compatibility but callers should
    // use lora_sleep()/lora_wake() directly for SX1262.
    Serial.print(F("[PowerManager] LoRa "));
    Serial.println(sleep ? F("sleep → use lora_sleep()") : F("wake → use lora_wake()"));

    #else
    // Power save disabled - ignore
    (void)sleep;
    #endif
}

/**
 * @brief Get current power statistics
 * @return PowerStats structure
 */
PowerStats getPowerStats() {
    // Update active time before returning
    unsigned long now = millis();
    if (powerStats.currentState == POWER_STATE_ACTIVE) {
        powerStats.activeTimeMs += now - powerStats.lastStateChange;
        powerStats.lastStateChange = now;
    }
    return powerStats;
}

/**
 * @brief Print power report to Serial
 */
void printCpuPowerReport() {
    PowerStats stats = getPowerStats();

    unsigned long totalMs = stats.activeTimeMs + stats.idleTimeMs + stats.sleepTimeMs;
    if (totalMs == 0) totalMs = 1;  // Prevent division by zero

    Serial.println("======== CPU POWER REPORT ========");
    Serial.print("CPU: ");
    Serial.print(stats.cpuFreqMHz);
    Serial.println(" MHz");

    Serial.print("Active: ");
    Serial.print(stats.activeTimeMs / 1000);
    Serial.print("s (");
    Serial.print((stats.activeTimeMs * 100) / totalMs);
    Serial.println("%)");

    Serial.print("Idle: ");
    Serial.print(stats.idleTimeMs / 1000);
    Serial.print("s (");
    Serial.print((stats.idleTimeMs * 100) / totalMs);
    Serial.println("%)");

    Serial.print("Sleep: ");
    Serial.print(stats.sleepTimeMs / 1000);
    Serial.print("s (");
    Serial.print((stats.sleepTimeMs * 100) / totalMs);
    Serial.println("%)");

    Serial.println("==================================");
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 3B: BATTERY MONITORING
// Battery monitoring has been moved to battery_monitor.h
// which uses the sensor_registry pattern for automatic registration.
// Enable with ENABLE_BATTERY_MONITOR in config.h.
// ═══════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════
// SECTION 4: GPIO POWER MANAGEMENT FUNCTIONS
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_GPIO_POWER

/**
 * @brief Register a GPIO pin as power output
 * @param vccPin GPIO pin for VCC (3.3V)
 * @param gndPin GPIO pin for GND (0V) - use -1 for board GND
 * @param name Device name (for logging)
 * @param expectedCurrent_mA Expected current draw (mA)
 * @return true if registered successfully, false if limit exceeded
 */
bool registerPowerPin(uint8_t vccPin, int8_t gndPin, const char* name, uint16_t expectedCurrent_mA) {
  // Check if we have space
  if (powerPinCount >= 8) {
    Serial.println("❌ GPIO Power: Too many power pins (max 8)");
    return false;
  }

  // Check if total current would exceed limit
  if (totalEstimatedCurrent + expectedCurrent_mA > GPIO_MAX_TOTAL_CURRENT) {
    Serial.print("❌ GPIO Power: Total current would exceed ");
    Serial.print(GPIO_MAX_TOTAL_CURRENT);
    Serial.println("mA!");
    Serial.print("   Current total: ");
    Serial.print(totalEstimatedCurrent);
    Serial.print("mA + new device: ");
    Serial.print(expectedCurrent_mA);
    Serial.println("mA");
    return false;
  }

  // Check per-pin limit
  if (expectedCurrent_mA > GPIO_SAFE_CURRENT_PER_PIN) {
    Serial.print("⚠️  GPIO Power: Device ");
    Serial.print(name);
    Serial.print(" draws ");
    Serial.print(expectedCurrent_mA);
    Serial.print("mA (exceeds safe limit ");
    Serial.print(GPIO_SAFE_CURRENT_PER_PIN);
    Serial.println("mA)");

    if (expectedCurrent_mA > GPIO_MAX_CURRENT_PER_PIN) {
      Serial.println("❌ UNSAFE - Aborting!");
      return false;
    }
  }

  // Register pin
  powerPins[powerPinCount].vccPin = vccPin;
  powerPins[powerPinCount].gndPin = gndPin;
  powerPins[powerPinCount].name = name;
  powerPins[powerPinCount].expectedCurrent_mA = expectedCurrent_mA;
  powerPins[powerPinCount].enabled = true;
  powerPins[powerPinCount].powered = false;
  powerPins[powerPinCount].powerOnTime = 0;

  totalEstimatedCurrent += expectedCurrent_mA;
  powerPinCount++;

  return true;
}

/**
 * @brief Power ON a specific pin
 * @param index Index in powerPins array
 */
void powerOnPin(uint8_t index) {
  if (index >= powerPinCount) return;
  if (!powerPins[index].enabled) return;
  if (powerPins[index].powered) return;  // Already on
  if (safetyShutdown) return;            // Safety shutdown active

  // Configure GND pin first (if used)
  if (powerPins[index].gndPin >= 0) {
    pinMode(powerPins[index].gndPin, OUTPUT);
    digitalWrite(powerPins[index].gndPin, LOW);  // GND = 0V
  }

  // Configure VCC pin as OUTPUT and set HIGH (3.3V)
  pinMode(powerPins[index].vccPin, OUTPUT);
  digitalWrite(powerPins[index].vccPin, HIGH);

  powerPins[index].powered = true;
  powerPins[index].powerOnTime = millis();

  Serial.print("🔌 GPIO Power ON:  ");
  Serial.print(powerPins[index].name);
  Serial.print(" (VCC=GPIO");
  Serial.print(powerPins[index].vccPin);
  if (powerPins[index].gndPin >= 0) {
    Serial.print(", GND=GPIO");
    Serial.print(powerPins[index].gndPin);
  }
  Serial.print(", ");
  Serial.print(powerPins[index].expectedCurrent_mA);
  Serial.println("mA)");
}

/**
 * @brief Power OFF a specific pin
 * @param index Index in powerPins array
 */
void powerOffPin(uint8_t index) {
  if (index >= powerPinCount) return;
  if (!powerPins[index].powered) return;  // Already off

  // Set VCC pin LOW (0V)
  digitalWrite(powerPins[index].vccPin, LOW);

  // Set GND pin to INPUT (high-impedance) if used
  if (powerPins[index].gndPin >= 0) {
    pinMode(powerPins[index].gndPin, INPUT);
  }

  powerPins[index].powered = false;

  Serial.print("🔌 GPIO Power OFF: ");
  Serial.print(powerPins[index].name);
  Serial.print(" (VCC=GPIO");
  Serial.print(powerPins[index].vccPin);
  if (powerPins[index].gndPin >= 0) {
    Serial.print(", GND=GPIO");
    Serial.print(powerPins[index].gndPin);
  }
  Serial.println(")");
}

/**
 * @brief Initialize GPIO power manager
 *
 * Call this in setup() BEFORE initializing sensors!
 * Powers up all registered GPIO power pins with soft-start delays.
 */
void initGpioPowerManagement() {
  if (gpioPowerInitialized) {
    Serial.println("⚠️  GPIO Power already initialized");
    return;
  }

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  GPIO POWER MANAGER                    ║");
  Serial.println("╚════════════════════════════════════════╝");

  // Register all enabled power pins
  powerPinCount = 0;
  totalEstimatedCurrent = 0;

  #if defined(LCD_USE_GPIO_POWER) && LCD_USE_GPIO_POWER && defined(ENABLE_LCD) && ENABLE_LCD
    #if defined(LCD_GND_PIN)
      if (!registerPowerPin(LCD_VCC_PIN, LCD_GND_PIN, "LCD Display", LCD_EXPECTED_CURRENT_MA)) {
    #else
      if (!registerPowerPin(LCD_VCC_PIN, -1, "LCD Display", LCD_EXPECTED_CURRENT_MA)) {
    #endif
      Serial.println("❌ Failed to register LCD power pin");
    }
  #endif

  #if defined(LIGHT_USE_GPIO_POWER) && LIGHT_USE_GPIO_POWER && defined(ENABLE_LIGHT_DETECTION) && ENABLE_LIGHT_DETECTION
    #if defined(LIGHT_GND_PIN)
      if (!registerPowerPin(LIGHT_VCC_PIN, LIGHT_GND_PIN, "Light Sensor", LIGHT_EXPECTED_CURRENT_MA)) {
    #else
      if (!registerPowerPin(LIGHT_VCC_PIN, -1, "Light Sensor", LIGHT_EXPECTED_CURRENT_MA)) {
    #endif
      Serial.println("❌ Failed to register Light sensor power pin");
    }
  #endif

  // Additional sensors can register their GPIO power pins here
  // Example:
  // registerPowerPin(SENSOR_VCC_PIN, SENSOR_GND_PIN, "My Sensor", 10);

  // Summary
  Serial.print("  Registered:  ");
  Serial.print(powerPinCount);
  Serial.println(" power pins");
  Serial.print("  Total draw:  ");
  Serial.print(totalEstimatedCurrent);
  Serial.print(" mA / ");
  Serial.print(GPIO_MAX_TOTAL_CURRENT);
  Serial.println(" mA limit");

  // Safety check
  if (totalEstimatedCurrent > GPIO_MAX_TOTAL_CURRENT) {
    Serial.println("\n❌ SAFETY ERROR: Total current exceeds limit!");
    Serial.println("   Reduce number of GPIO-powered devices.");
    safetyShutdown = true;

    return;
  }

  // Power up all pins with soft-start delays
  Serial.println("\n  Powering up devices (soft start):");

  for (uint8_t i = 0; i < powerPinCount; i++) {
    powerOnPin(i);

    // Soft-start delay between power-ups
    if (i < powerPinCount - 1) {
      delay(GPIO_POWER_SOFT_START_MS);
    }
  }

  // Final stabilization delay
  Serial.print("\n  Waiting ");
  Serial.print(GPIO_POWER_STABILIZE_MS);
  Serial.println("ms for power stabilization...");
  delay(GPIO_POWER_STABILIZE_MS);

  gpioPowerInitialized = true;
  Serial.println("  ✓ GPIO Power Manager ready\n");
}

/**
 * @brief Power cycle a device (OFF → wait → ON)
 * @param vccPin GPIO VCC pin number
 * @param delayMs Delay between OFF and ON (ms)
 *
 * Useful for resetting frozen sensors.
 */
void powerCycleDevice(uint8_t vccPin, uint16_t delayMs = 500) {
  // Find device by VCC pin
  for (uint8_t i = 0; i < powerPinCount; i++) {
    if (powerPins[i].vccPin == vccPin) {
      Serial.print("🔄 Power cycling ");
      Serial.println(powerPins[i].name);

      powerOffPin(i);
      delay(delayMs);
      powerOnPin(i);
      delay(GPIO_POWER_STABILIZE_MS);

      return;
    }
  }

  Serial.print("❌ Power cycle failed: VCC GPIO ");
  Serial.print(vccPin);
  Serial.println(" not registered");
}

/**
 * @brief Emergency shutdown of all GPIO power
 *
 * Turns off all GPIO power pins. Call in case of detected issues.
 */
void emergencyShutdownGPIOPower() {
  Serial.println("\n⚠️⚠️⚠️  EMERGENCY GPIO POWER SHUTDOWN  ⚠️⚠️⚠️");

  for (uint8_t i = 0; i < powerPinCount; i++) {
    powerOffPin(i);
  }

  safetyShutdown = true;

  Serial.println("All GPIO power OFF. System requires reset.\n");
}

/**
 * @brief Check if GPIO power is within safe limits
 * @return true if safe, false if exceeded
 */
bool isGPIOPowerSafe() {
  return !safetyShutdown && totalEstimatedCurrent <= GPIO_MAX_TOTAL_CURRENT;
}

/**
 * @brief Get total estimated current draw
 * @return Current in mA
 */
uint16_t getTotalGPIOCurrent() {
  return totalEstimatedCurrent;
}

/**
 * @brief Print GPIO power status
 */
void printGPIOPowerStatus() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  GPIO POWER STATUS                     ║");
  Serial.println("╚════════════════════════════════════════╝");

  for (uint8_t i = 0; i < powerPinCount; i++) {
    Serial.print("  ");
    Serial.print(powerPins[i].powered ? "✓" : "✗");
    Serial.print(" ");
    Serial.print(powerPins[i].name);
    Serial.print(": VCC=GPIO");
    Serial.print(powerPins[i].vccPin);
    if (powerPins[i].gndPin >= 0) {
      Serial.print(", GND=GPIO");
      Serial.print(powerPins[i].gndPin);
    }
    Serial.print(" (");
    Serial.print(powerPins[i].expectedCurrent_mA);
    Serial.print("mA)");

    if (powerPins[i].powered) {
      unsigned long uptime = (millis() - powerPins[i].powerOnTime) / 1000;
      Serial.print(" - ON for ");
      Serial.print(uptime);
      Serial.print("s");
    }

    Serial.println();
  }

  Serial.print("\n  Total:       ");
  Serial.print(totalEstimatedCurrent);
  Serial.print(" / ");
  Serial.print(GPIO_MAX_TOTAL_CURRENT);
  Serial.println(" mA");

  Serial.print("  Status:      ");
  if (safetyShutdown) {
    Serial.println("❌ SAFETY SHUTDOWN");
  } else if (totalEstimatedCurrent > GPIO_SAFE_CURRENT_PER_PIN * powerPinCount) {
    Serial.println("⚠️  WARNING - High current");
  } else {
    Serial.println("✓ OK");
  }

  Serial.println();
}

#endif // ENABLE_GPIO_POWER

// ═══════════════════════════════════════════════════════════════════
// SECTION 5: UNIFIED INITIALIZATION
// ═══════════════════════════════════════════════════════════════════

/**
 * @brief Initialize complete power management system
 *
 * Call this once in setup() to initialize both CPU power management
 * and GPIO power management. This is the main entry point.
 */
void initPowerManagement() {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║  POWER MANAGEMENT SYSTEM               ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();

    // Initialize CPU frequency and sleep modes
    initCpuPowerManagement();
    Serial.println();

    // Initialize GPIO power management
    #if ENABLE_GPIO_POWER
    initGpioPowerManagement();
    #else
    Serial.println("GPIO Power Management: DISABLED");
    Serial.println("  To enable: Set ENABLE_GPIO_POWER true in config.h");
    Serial.println();
    #endif

    Serial.println("✓ Power Management System ready\n");
}

/**
 * @brief Print complete power status report
 *
 * Shows both CPU power statistics and GPIO power status
 */
void printPowerStatus() {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║  POWER MANAGEMENT STATUS               ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();

    printCpuPowerReport();
    Serial.println();

    #if ENABLE_GPIO_POWER
    printGPIOPowerStatus();
    #endif
}

#endif // POWER_MANAGEMENT_H
