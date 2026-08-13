/*=====================================================================
  config.h - Master Configuration File

  LoraMeister - ESP32 LoRa General-Purpose Communication System

  PURPOSE:
  Central configuration file containing ALL feature toggles, pin
  definitions, thresholds, and constants.

  HARDWARE:
  ESP32 DevKit V1 + RYLR890/RYLR896 (AT commands, UART)

  Role Detection (hardware jumpers):
  - RECEIVER: GPIO16 connected to GPIO17 (jumper wire)
  - SENDER:   GPIO16 floating (no connection)
  - RELAY:    GPIO19 connected to GPIO20 (jumper wire)

  ADDING NEW SENSORS:
  1. Add ENABLE_xxx and config defines in the SENSORS section
  2. Create a new xxx_sensor.h file (see microphone.h as example)
  3. Add fields to DeviceState in structs.h for sensor data
  4. Add payload fields in firmware.ino buildPayload()/parsePayload()
  5. Add LCD layout in lcd_display.h if needed
=======================================================================*/

#ifndef CONFIG_H
#define CONFIG_H

//=============================================================================
// QUICK SETTINGS - Change these to configure your build
//=============================================================================
#define QS_LCD_LAYOUT              8       // 1=Signal, 2=Range Test, 4=Debug, 8=Microphone
#define QS_LORA_SEND_INTERVAL_MS   3500    // Send interval (ms)
#define QS_ACK_INTERVAL            30      // ACK every Nth packet
#define QS_DEBUG_MIC               false   // MAX4466 debug to Serial

// Compatibility flag for multi-platform builds.
// This project defaults to ESP32 WROOM path unless overridden by build flags.
#ifndef USE_XIAO_SX1262
  #define USE_XIAO_SX1262 false
#endif

//=============================================================================
// PROJECT INFO
//=============================================================================
#define PROJECT_NAME "LoraMeister"
#define PROJECT_VERSION "0.3.0"
#define PROJECT_VERSION_MAJOR 0
#define PROJECT_VERSION_MINOR 3
#define PROJECT_VERSION_PATCH 0
#define PROJECT_DATE "2026-03-20"

//=============================================================================
// PIN DEFINITIONS - ESP32 DevKit V1 + RYLR890/896
//=============================================================================
//
// GPIO  | Function              | Type        | Notes
// ------|----------------------|-------------|---------------------------
// 2     | LED_PIN              | Output      | Built-in LED
// 16    | MODE_SELECT_PIN      | Input       | Receiver detect
// 17    | MODE_GND_PIN         | Output      | Receiver detect GND
// 19    | RELAY_MODE_PIN_A     | Input       | Relay detect
// 20    | RELAY_MODE_PIN_B     | Output      | Relay detect GND
// 21    | I2C SDA              | I2C         | LCD display
// 22    | I2C SCL              | I2C         | LCD display
// 25    | RXD2 (LoRa RX)      | UART        | RYLR890 TX -> ESP32
// 26    | TXD2 (LoRa TX)      | UART        | RYLR890 RX <- ESP32
// 34    | MIC_PIN              | ADC Input   | MAX4466 analog out
//
// SAFE PINS FOR NEW SENSORS:
// GPIO 4, 5, 12, 13, 14, 15, 18, 23, 27, 32, 33 (output capable)
// GPIO 34, 35, 36, 39 (input-only, good for ADC)

// ── LoRa UART ──
#define RXD2 25                     // RYLR890 TX -> ESP32
#define TXD2 26                     // RYLR890 RX <- ESP32

// ── LED ──
#define LED_PIN 2                   // Built-in LED

// ── Mode Detection ──
#define MODE_SELECT_PIN 16          // RECEIVER detect (INPUT_PULLUP)
#define MODE_GND_PIN 17             // RECEIVER GND reference (OUTPUT LOW)
#define RELAY_MODE_PIN_A 19         // RELAY detect (INPUT_PULLUP)
#define RELAY_MODE_PIN_B 20         // RELAY GND reference (OUTPUT LOW)

// ── LCD Display (I2C 16x2) ──
#define I2C_SDA_PIN 21              // I2C Data
#define I2C_SCL_PIN 22              // I2C Clock

// ── Touch Sensor ──
#if USE_XIAO_SX1262
  #define TOUCH_PIN 4               // Use explicit GPIO on ESP32-S3
#else
  #define TOUCH_PIN T0              // GPIO 4 (classic ESP32 touch alias)
#endif

// ── MAX4466 Microphone ──
#define MIC_PIN 34                  // ADC1_CH6 (input-only GPIO, perfect for ADC)

// ═══════════════════════════════════════════════════════
// E-INK DISPLAY (ESP32 WROOM + RYLR896 only)
// ═══════════════════════════════════════════════════════
#if !USE_XIAO_SX1262
// Master toggle - set true to enable e-ink display
#define ENABLE_EINK_DISPLAY false

// E-ink SPI pin definitions (ESP32 WROOM)
#define EINK_SCLK_PIN    18    // SPI clock
#define EINK_MOSI_PIN    23    // SPI data
#define EINK_CS_PIN       5    // Chip select
#define EINK_DC_PIN      17    // Data/Command
#define EINK_RST_PIN     16    // Reset
#define EINK_BUSY_PIN     4    // Busy signal

// Refresh interval (e-ink is slow, don't refresh too often)
#define EINK_REFRESH_INTERVAL_MS  10000   // 10 seconds minimum
#else
#define ENABLE_EINK_DISPLAY false
#endif

//=============================================================================
// LoRa CONFIGURATION
//=============================================================================
#define LORA_RECEIVER_ADDRESS 1
#define LORA_SENDER_ADDRESS 2
#define LORA_DISPLAY_ADDRESS 3
#define LORA_RELAY_ADDRESS 4
#define LORA_NETWORK_ID 6           // SAME ON ALL DEVICES!
#define LORA_BAUDRATE 115200
#define LORA_BAND 868               // 868 MHz (Europe)
#define LORA_NETWORK_ID_VALUE 6
#define LORA_BROADCAST_ADDR 0

//=============================================================================
// MESH NETWORK
//=============================================================================
#define ENABLE_MESH_NETWORK true
#define MESH_MAX_HOPS 3
#define RELAY_FORWARD_DELAY_MS 100
#define RELAY_STATS_REPORT_INTERVAL 30000
#define MESH_BROADCAST_ID 0

#define ENABLE_MESH_DEDUPLICATION true
#define DEDUP_BUFFER_SIZE 500
#define DEDUP_TIMEOUT_MS 10000

#define ENABLE_MESH_SOURCE_TRACKING true
#define ENABLE_MESH_RSSI_FILTER true
#define MESH_RELAY_MIN_RSSI -120

#define ENABLE_MESH_ENCRYPTION false
#define MESH_ENCRYPTION_KEY 0xA7B3C9D2

#define MESH_RELAY_BEACON true
#define MESH_BEACON_PRIORITY true
#define ENABLE_DUAL_MODE_RELAY false

//=============================================================================
// BIDIRECTIONAL COMMUNICATION
//=============================================================================
#define ENABLE_BIDIRECTIONAL true
#define ACK_INTERVAL QS_ACK_INTERVAL
#define LISTEN_TIMEOUT 150

#define ENABLE_BIDIRECTIONAL_TDMA true
#define BEACON_LISTEN_WINDOW_MS 450
#define BEACON_MAX_WAIT_MS 4000
#define BEACON_MESSAGE "BEACON:READY"

#define ENABLE_COMMAND_RESPONSE true
#define COMMAND_TIMEOUT_MS 5000
#define COMMAND_RETRY_COUNT 2
#define COMMAND_LISTEN_INTERVAL_MS 500
#define COMMAND_LISTEN_DURATION_MS 100

#define ENABLE_COMMAND_DEDUPLICATION true
#define COMMAND_DEDUP_TIMEOUT_MS 5000

//=============================================================================
// AUTO-ADDRESS & SEND INTERVAL
//=============================================================================
#define ENABLE_AUTO_ADDRESS true
#define AUTO_ADDRESS_MIN 10
#define AUTO_ADDRESS_MAX 250

#define LORA_SEND_INTERVAL_MS QS_LORA_SEND_INTERVAL_MS

// TX retry
#define ENABLE_TX_RETRY true
#define TX_MAX_RETRIES 3
#define TX_RETRY_BACKOFF_MS 100

//=============================================================================
// COMMUNICATION
//=============================================================================
#define SERIAL2_BAUDRATE 115200
#define MAX_RX_BUFFER 256
#define RX_TIMEOUT_WARNING 5000

// CSV output (for data logging via USB serial)
#define ENABLE_CSV_OUTPUT true
#define ENABLE_JSON_OUTPUT false
#define DATA_OUTPUT_INTERVAL 2000

//=============================================================================
// DISPLAY
//=============================================================================
#define DISPLAY_MODE 1              // 1 = LCD only
#define FORCE_LCD false

#define ENABLE_LCD              (DISPLAY_MODE == 1)
#define ENABLE_DISPLAY_OUTPUT   false

#define LCD_LAYOUT_NUMBER QS_LCD_LAYOUT
#define LCD_SENDER_ENABLED true

//=============================================================================
// SYSTEM FEATURES
//=============================================================================
#define ENABLE_WATCHDOG true
#define WATCHDOG_TIMEOUT_S 10

#define ENABLE_LED_STATUS true
#define LED_STATUS_BOOT_BLINKS 5
#define LED_STATUS_BOOT_DELAY 100
#define LED_STATUS_ERROR_DELAY 200

#define ENABLE_DEVICE_ID_BLINK true
#define DEVICE_ID_BLINK_DELAY 200
#define DEVICE_ID_DIGIT_PAUSE 500
#define DEVICE_ID_FINAL_PAUSE 1000

#define ENABLE_NVS_STORAGE true
#define NVS_NAMESPACE "lorameister"

#define ENABLE_OTA false
#define ENABLE_PERFORMANCE_MONITOR true
#define PERF_REPORT_INTERVAL 60000

//=============================================================================
// SENSORS
//=============================================================================

// ── Touch Sensor ──
#define ENABLE_TOUCH_SENSOR true
#define ENABLE_TOUCH_COMMAND true
#define TOUCH_THRESHOLD 50
#define TOUCH_LOGIC_INVERTED false
#define TOUCH_SENSOR_DEBUG false
#define TOUCH_COMMAND_ACTION "LED_BLINK:3"
#define TOUCH_COMMAND_COOLDOWN_MS 2000

// ── MAX4466 Microphone (example sensor) ──
// Analog electret microphone module, reads sound level
// Connect: OUT -> GPIO34 (ADC), VCC -> 3.3V, GND -> GND
// Output: VCC/2 DC bias (~1.65V), audio signal superimposed
// Reading: sample peak-to-peak over window, convert to dB
#define ENABLE_MICROPHONE true
#define MIC_SAMPLE_WINDOW_MS 50     // Sample window (ms) for peak-to-peak
#define MIC_SEND_INTERVAL_MS 500    // How often to include mic data in LoRa payload
#define MIC_NOISE_FLOOR 50          // ADC values below this = silence
#define MIC_DB_MIN 30               // Minimum dB to display (ambient)
#define MIC_DB_MAX 100              // Maximum dB to display (loud)
#define DEBUG_MICROPHONE QS_DEBUG_MIC

// ── DHT22 Temperature & Humidity (example sensor) ──
// Digital temp/humidity sensor, 1-wire protocol
// Connect: DATA -> GPIO4, VCC -> 3.3V, GND -> GND, 10kΩ pull-up
#define ENABLE_DHT22 false          // Set true when DHT22 is connected
#define DHT22_PIN 4                 // Data pin (GPIO4)
#define DHT22_READ_INTERVAL_MS 2000 // Min 2s between reads
#define DEBUG_DHT22 false

// ── Disabled sensor stubs (enable when adding hardware) ──
#define ENABLE_AUDIO_DETECTION false
#define ENABLE_LIGHT_DETECTION false
#define ENABLE_PROXIMITY_DETECTION false
// ── Battery Monitor (ADC via voltage divider) ──
// Reads battery voltage via voltage divider on ADC pin
// Connect: VBAT -> R1(100k) -> GPIO35 -> R2(100k) -> GND
#define ENABLE_BATTERY_MONITOR false  // Set true when divider is connected
#define BATTERY_ADC_PIN 35            // ADC1_CH7 (input-only)
#define BATTERY_DIVIDER_FACTOR 2.0    // (R1+R2)/R2
#define BATTERY_READ_INTERVAL_MS 10000

#define ENABLE_CURRENT_MONITOR false

//=============================================================================
// ADVANCED
//=============================================================================

// Extended telemetry (disabled)
#define ENABLE_EXTENDED_TELEMETRY false

// Power management
// POWER_SLEEP_MODE: 0=none, 1=light sleep, 2=deep sleep
// Light sleep: CPU stops between TX cycles, ~0.8mA, maintains RAM
// Deep sleep: full power down, ESP32 reboots on wake
#define ENABLE_POWER_SAVE false     // Set true for battery-powered nodes
#define POWER_SLEEP_MODE 0          // 0=none, 1=light, 2=deep
#define POWER_LIGHT_SLEEP_MS 1900   // Light sleep between TX (ms)
#define POWER_DEEP_SLEEP_S 60       // Deep sleep duration (seconds)
// #define POWER_WAKE_PIN 4         // Optional: GPIO pin that wakes from light sleep (HIGH)
#define ENABLE_GPIO_POWER true
#define ENABLE_FLASH_LOGGER false

// GPIO power for sensors (when ENABLE_GPIO_POWER is true)
#define LCD_USE_GPIO_POWER false
#define LCD_VCC_PIN 19
#define LCD_EXPECTED_CURRENT_MA 15

// Debug
#define DEBUG_LORA_AT false
#define ENABLE_MANUAL_AT_COMMANDS false
#define DEBUG_STATE_MACHINE true

//=============================================================================
// VALIDATION
//=============================================================================

#if AUTO_ADDRESS_MIN <= 4
  #error "AUTO_ADDRESS_MIN must be > 4"
#endif
#if AUTO_ADDRESS_MAX > 250
  #error "AUTO_ADDRESS_MAX must be <= 250"
#endif

#if LORA_SEND_INTERVAL_MS < 3000
  #warning "LORA_SEND_INTERVAL_MS < 3000ms may cause overlap with SF12"
#endif

// Helper macros
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

#pragma message "LoraMeister v" PROJECT_VERSION " - ESP32 DevKit + RYLR890"

#endif // CONFIG_H
