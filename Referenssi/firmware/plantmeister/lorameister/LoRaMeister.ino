/*=====================================================================
  firmware.ino

  LoraMeister - General-purpose ESP32 LoRa Communication System

  Features:
  - Auto role detection (GPIO jumpers)
  - LoRa communication (RYLR890/896 AT commands)
  - LCD display (16x2 I2C)
  - MAX4466 microphone sensor
  - Mesh network / relay support
  - Bidirectional ACK with RSSI/SNR
  - CSV data logging (USB serial)

  Hardware:
  - ESP32 DevKit V1
  - RYLR890 TX -> GPIO25 (RX), RX -> GPIO26 (TX)
  - LCD 16x2 I2C SDA=GPIO21, SCL=GPIO22
  - MAX4466 OUT -> GPIO34

  Role Detection:
  - Receiver: GPIO16 connected to GPIO17
  - Sender:   GPIO16 floating
  - Relay:    GPIO19 connected to GPIO20

  Both devices run IDENTICAL code!
=======================================================================*/

#include "config.h"
#include "structs.h"
#include "non_blocking_state_v2.h"

// Hardware Abstraction Layers
#include "platform_hal.h"
#include "debug_hal.h"
#include "display_hal.h"

#include <esp_system.h>
#include <esp_sleep.h>

#if __has_include(<esp_idf_version.h>)
  #include <esp_idf_version.h>
#else
  #define ESP_IDF_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
  #define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(4, 4, 0)
#endif

#if ENABLE_WATCHDOG
  #include <esp_task_wdt.h>
#endif

#if ENABLE_NVS_STORAGE
  #include <Preferences.h>
  Preferences nvs;
#endif

// LoRa + Monitoring
#include "lora_hal.h"
#include "system_monitoring.h"

// Sensor registry (must be included before individual sensors)
#include "sensor_registry.h"

// Sensors (each self-registers via REGISTER_SENSOR macro)
#if ENABLE_MICROPHONE
  #include "microphone.h"
#endif
// Add new sensors here — each self-registers via REGISTER_SENSOR:
#if ENABLE_DHT22
  #include "dht22_sensor.h"
#endif
#if ENABLE_BATTERY_MONITOR
  #include "battery_monitor.h"
#endif

// Display
#include "lcd_display.h"

// Power management
#include "power_management.h"

// ═══════════════════════════════════════════════════════════════════
// KILL-SWITCH CONFIG (GPIO13↔14, hold 3s = restart)
// ═══════════════════════════════════════════════════════════════════
#define KILLSWITCH_GND_PIN 14
#define KILLSWITCH_READ_PIN 13
#define KILLSWITCH_HOLD_TIME 3000
#define KILLSWITCH_DEBUG false

// ═══════════════════════════════════════════════════════════════════
// GLOBALS
// ═══════════════════════════════════════════════════════════════════
bool bRECEIVER = false;
bool bRELAY = false;
uint8_t MY_LORA_ADDRESS = 0;
uint8_t TARGET_LORA_ADDRESS = 0;

DeviceState local;
DeviceState remote;
TimingData timing;
SpinnerData spinner;
HealthMonitor health;
RelayStats relayStats;
DeviceTracker deviceTracker;

// Non-blocking relay queue
RelayQueue relayQueue;

// Kill-switch state
unsigned long killSwitchPressStart = 0;
unsigned long lastKillSwitchDebug = 0;

// Bidirectional ACK stats
int ackReceived = 0;
int ackExpected = 0;
int ackTimeout = 0;
unsigned long lastAckTime = 0;

// Remote control
bool transmissionStopped = false;

// Command deduplication
#if ENABLE_COMMAND_DEDUPLICATION
SeenCommand seenCommands[SEEN_COMMANDS_BUFFER];
int seenCommandsIndex = 0;
#endif

// Touch command
#if ENABLE_TOUCH_COMMAND
unsigned long lastTouchCommandTime = 0;
#endif

// ═══════════════════════════════════════════════════════════════════
// KILL-SWITCH (GPIO13↔14)
// ═══════════════════════════════════════════════════════════════════

void initKillSwitch() {
  pinMode(KILLSWITCH_GND_PIN, OUTPUT);
  digitalWrite(KILLSWITCH_GND_PIN, LOW);
  pinMode(KILLSWITCH_READ_PIN, INPUT_PULLUP);

  Serial.println("✓ Kill-switch initialized");
  Serial.print("  Connect GPIO");
  Serial.print(KILLSWITCH_READ_PIN);
  Serial.print(" ↔ GPIO");
  Serial.print(KILLSWITCH_GND_PIN);
  Serial.println(" and hold 3s to restart");
}

void checkKillSwitch() {
  bool pressed = (digitalRead(KILLSWITCH_READ_PIN) == LOW);

  if (pressed) {
    if (killSwitchPressStart == 0) {
      killSwitchPressStart = millis();
    }

    unsigned long held = millis() - killSwitchPressStart;

    #if KILLSWITCH_DEBUG
    if (millis() - lastKillSwitchDebug > 500) {
      Serial.printf("[KILL] Held %lu ms / %d ms\n", held, KILLSWITCH_HOLD_TIME);
      lastKillSwitchDebug = millis();
    }
    #endif

    if (held >= KILLSWITCH_HOLD_TIME) {
      Serial.println("\n╔════════════════════════════════════════╗");
      Serial.println("║    KILL-SWITCH ACTIVATED               ║");
      Serial.println("║    Restarting device...                ║");
      Serial.println("╚════════════════════════════════════════╝");
      delay(100);
      ESP.restart();
    }
  } else {
    killSwitchPressStart = 0;
  }
}

// ═══════════════════════════════════════════════════════════════════
// DEVICE TRACKER (receiver: track multiple senders)
// ═══════════════════════════════════════════════════════════════════

void initDeviceTracker() {
  deviceTracker.totalDevicesEverSeen = 0;
  deviceTracker.currentActiveCount = 0;
  for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
    deviceTracker.devices[i].active = false;
    deviceTracker.devices[i].deviceId = 0;
  }
  Serial.println("✓ Device tracker initialized");
}

void registerDevice(uint8_t sourceId, int rssi, int snr) {
  if (sourceId == 0) return;

  // Find existing or free slot
  int freeSlot = -1;
  for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
    if (deviceTracker.devices[i].active && deviceTracker.devices[i].deviceId == sourceId) {
      // Update existing
      deviceTracker.devices[i].lastSeen = millis();
      deviceTracker.devices[i].lastRssi = rssi;
      deviceTracker.devices[i].lastSnr = snr;
      deviceTracker.devices[i].messageCount++;
      return;
    }
    if (!deviceTracker.devices[i].active && freeSlot == -1) {
      freeSlot = i;
    }
  }

  // Register new device
  if (freeSlot >= 0) {
    deviceTracker.devices[freeSlot].active = true;
    deviceTracker.devices[freeSlot].deviceId = sourceId;
    deviceTracker.devices[freeSlot].lastRssi = rssi;
    deviceTracker.devices[freeSlot].lastSnr = snr;
    deviceTracker.devices[freeSlot].lastSeen = millis();
    deviceTracker.devices[freeSlot].messageCount = 1;
    deviceTracker.totalDevicesEverSeen++;
    deviceTracker.currentActiveCount++;
    Serial.printf("[TRACKER] New device #%d (total: %d)\n", sourceId, deviceTracker.currentActiveCount);
  }
}

int getActiveDeviceCount(DeviceTracker& tracker) {
  int count = 0;
  unsigned long now = millis();
  for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
    if (tracker.devices[i].active) {
      if (now - tracker.devices[i].lastSeen > DEVICE_TIMEOUT_MS) {
        tracker.devices[i].active = false;
      } else {
        count++;
      }
    }
  }
  tracker.currentActiveCount = count;
  return count;
}

// ═══════════════════════════════════════════════════════════════════
// COMMAND HANDLING
// ═══════════════════════════════════════════════════════════════════

void executeRestart() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║    RESTART COMMAND RECEIVED             ║");
  Serial.println("╚════════════════════════════════════════╝");
  delay(100);
  ESP.restart();
}

void executeCommand(const char* action) {
  if (strcmp(action, "PING") == 0) {
    Serial.println("[CMD] PING received — PONG!");
  } else if (strcmp(action, "LED_BLINK") == 0 || strncmp(action, "LED_BLINK:", 10) == 0) {
    int count = 3;
    if (strncmp(action, "LED_BLINK:", 10) == 0) {
      count = atoi(action + 10);
      if (count < 1 || count > 20) count = 3;
    }
    platform_blinkLed(count, 200);
  } else if (strcmp(action, "RESTART") == 0) {
    executeRestart();
  } else if (strcmp(action, "GET_STATUS") == 0) {
    Serial.printf("[STATUS] Uptime: %lu s, Heap: %lu, RSSI: %d\n",
      millis() / 1000, ESP.getFreeHeap(), lora_getRSSI());
  } else {
    Serial.printf("[CMD] Unknown command: %s\n", action);
  }
}

void processRemoteCommand(const char* payload) {
  // Format: CMD:<action>:<id>:<timestamp>
  if (strncmp(payload, "CMD:", 4) != 0) return;

  char action[64];
  int id = 0;
  if (sscanf(payload, "CMD:%63[^:]:%d", action, &id) >= 1) {
    Serial.printf("[CMD] Remote command: %s (id=%d)\n", action, id);
    executeCommand(action);
  }
}

// ═══════════════════════════════════════════════════════════════════
// LED STATUS
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_LED_STATUS
void ledStatusBoot() {
  platform_blinkLed(LED_STATUS_BOOT_BLINKS, LED_STATUS_BOOT_DELAY);
}

void ledStatusError() {
  // SOS pattern: 3 short, 3 long, 3 short
  for (int r = 0; r < 3; r++) {
    for (int i = 0; i < 3; i++) { platform_setLed(true); delay(100); platform_setLed(false); delay(100); }
    for (int i = 0; i < 3; i++) { platform_setLed(true); delay(300); platform_setLed(false); delay(100); }
    for (int i = 0; i < 3; i++) { platform_setLed(true); delay(100); platform_setLed(false); delay(100); }
    delay(500);
    #if ENABLE_WATCHDOG
      esp_task_wdt_reset();
    #endif
  }
}
#endif

#if ENABLE_DEVICE_ID_BLINK
void blinkDeviceAddress(uint8_t addr) {
  char addrStr[4];
  snprintf(addrStr, 4, "%d", addr);
  int len = strlen(addrStr);

  Serial.printf("[LED] Blinking address: %d (%d digits)\n", addr, len);

  delay(500);
  for (int d = 0; d < len; d++) {
    int digit = addrStr[d] - '0';
    if (digit == 0) digit = 10;

    for (int b = 0; b < digit; b++) {
      platform_setLed(true);
      delay(DEVICE_ID_BLINK_DELAY);
      platform_setLed(false);
      delay(DEVICE_ID_BLINK_DELAY);
    }
    delay(DEVICE_ID_DIGIT_PAUSE);
  }
  delay(DEVICE_ID_FINAL_PAUSE);
}
#endif

// ═══════════════════════════════════════════════════════════════════
// NVS STORAGE (simplified)
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_NVS_STORAGE
void initNVS() {
  nvs.begin(NVS_NAMESPACE, false);

  uint32_t bootCount = nvs.getUInt("bootCount", 0) + 1;
  nvs.putUInt("bootCount", bootCount);

  Serial.printf("✓ NVS initialized (namespace: %s, boot #%lu)\n", NVS_NAMESPACE, bootCount);

  // Print reset reason
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("  Reset reason: ");
  switch (reason) {
    case ESP_RST_POWERON:  Serial.println("Power-on"); break;
    case ESP_RST_SW:       Serial.println("Software restart"); break;
    case ESP_RST_PANIC:    Serial.println("⚠️ PANIC (crash)"); break;
    case ESP_RST_INT_WDT:  Serial.println("⚠️ INT_WDT timeout"); break;
    case ESP_RST_TASK_WDT: Serial.println("⚠️ TASK_WDT timeout"); break;
    case ESP_RST_BROWNOUT: Serial.println("⚠️ Brownout"); break;
    default:               Serial.printf("Other (%d)\n", reason); break;
  }
}
#endif

// ═══════════════════════════════════════════════════════════════════
// WATCHDOG
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_WATCHDOG
void initWatchdog() {
  #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t config = {
      .timeout_ms = (uint32_t)(WATCHDOG_TIMEOUT_S * 1000),
      .idle_core_mask = 0,
      .trigger_panic = true
    };
    esp_task_wdt_init(&config);
  #else
    esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);
  #endif
  esp_task_wdt_add(NULL);
  Serial.printf("✓ Watchdog timer: %d seconds\n", WATCHDOG_TIMEOUT_S);
}

void feedWatchdog() {
  esp_task_wdt_reset();
}
#endif

// ═══════════════════════════════════════════════════════════════════
// PAYLOAD FORMAT
// ═══════════════════════════════════════════════════════════════════
// Sender broadcasts: SEQ:x,LED:x,TOUCH:x,SPIN:x,COUNT:x,MIC:x,MICDB:x
// Receiver ACK:      ACK:PING:<id>:OK:RSSI:<rssi>,SNR:<snr>

void updateSpinner() {
  if (millis() - spinner.lastUpdate > 250) {
    spinner.index = (spinner.index + 1) % 4;
    spinner.lastUpdate = millis();
  }
}

/**
 * Parse received payload into remote DeviceState
 * Core fields are parsed directly, sensor fields via registry.
 */
void parsePayload(const char* payload, DeviceState& dev) {
  const char* p = payload;
  char key[16];
  char value[32];

  while (*p) {
    // Extract key (up to ':')
    int ki = 0;
    while (*p && *p != ':' && *p != ',' && ki < (int)sizeof(key) - 1) {
      key[ki++] = *p++;
    }
    key[ki] = '\0';

    // Extract value (after ':', up to ',' or end)
    value[0] = '\0';
    if (*p == ':') {
      p++;  // skip ':'
      int vi = 0;
      while (*p && *p != ',' && vi < (int)sizeof(value) - 1) {
        value[vi++] = *p++;
      }
      value[vi] = '\0';
    }

    // Parse core fields
    if (strcmp(key, "SEQ") == 0) dev.sequenceNumber = atoi(value);
    else if (strcmp(key, "LED") == 0) dev.ledState = atoi(value);
    else if (strcmp(key, "TOUCH") == 0) dev.touchState = atoi(value);
    else if (strcmp(key, "SPIN") == 0) dev.spinnerIndex = atoi(value);
    else if (strcmp(key, "COUNT") == 0) dev.messageCount = atoi(value);
    else if (strcmp(key, "LIT") == 0) dev.lightState = atoi(value);
    // Sensor fields: try registry (handles MIC, MICDB, TEMP, HUM, etc.)
    else sensorRegistry_parseField(key, value, dev);

    // Skip comma separator
    if (*p == ',') p++;
  }
}

/**
 * Build sender payload
 * Core fields are written directly, sensor fields via registry.
 */
void buildPayload(char* buffer, int bufSize) {
  // Core fields (always present)
  int written = snprintf(buffer, bufSize,
    "SEQ:%d,LED:%d,TOUCH:%d,SPIN:%d,COUNT:%d",
    local.sequenceNumber,
    local.ledState ? 1 : 0,
    local.touchState ? 1 : 0,
    spinner.index,
    local.messageCount
  );

  // Sensor fields (appended by registry — MIC, DHT22, etc.)
  sensorRegistry_buildAll(buffer, bufSize, written);
}

// ═══════════════════════════════════════════════════════════════════
// CSV OUTPUT (simplified)
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_CSV_OUTPUT
void printDataCSV() {
  // Format: DATA_CSV,V3,timestamp,role,rssi,snr,seq,count,mic_p2p,mic_db,heap
  char csv[256];
  snprintf(csv, sizeof(csv),
    "DATA_CSV,V3,%lu,%s,%d,%d,%d,%d,%d,%d,%lu",
    millis(),
    bRECEIVER ? "RX" : (bRELAY ? "RELAY" : "TX"),
    remote.rssi,
    remote.snr,
    remote.sequenceNumber,
    remote.messageCount,
    remote.micPeakToPeak,
    remote.micDB,
    ESP.getFreeHeap()
  );
  Serial.println(csv);
}
#endif

#if ENABLE_JSON_OUTPUT
void printDataJSON() {
  char json[384];
  snprintf(json, sizeof(json),
    "{\"t\":%lu,\"role\":\"%s\",\"rssi\":%d,\"snr\":%d,"
    "\"seq\":%d,\"count\":%d,\"mic_p2p\":%d,\"mic_db\":%d,"
    "\"heap\":%lu,\"src\":%d,\"hops\":%d}",
    millis(),
    bRECEIVER ? "RX" : (bRELAY ? "RELAY" : "TX"),
    remote.rssi,
    remote.snr,
    remote.sequenceNumber,
    remote.messageCount,
    remote.micPeakToPeak,
    remote.micDB,
    ESP.getFreeHeap(),
    remote.sourceId,
    remote.hopCount
  );
  Serial.println(json);
}
#endif

// ═══════════════════════════════════════════════════════════════════
// MESH HELPERS
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_MESH_NETWORK
void processRelayMessage() {
  if (!lora_available()) return;

  char buffer[256];
  int rssi = 0, snr = 0;
  if (!lora_receive(buffer, &rssi, &snr)) return;

  relayStats.messagesReceived++;

  // Parse mesh header: HOP:x/y,SRC:z,DST:w,...
  int hops = 0, maxHops = MESH_MAX_HOPS;
  int srcId = 0, dstId = 0;
  char* dataStart = buffer;

  if (strncmp(buffer, "HOP:", 4) == 0) {
    sscanf(buffer, "HOP:%d/%d,SRC:%d,DST:%d", &hops, &maxHops, &srcId, &dstId);
    dataStart = strchr(buffer, ',');
    if (dataStart) { dataStart = strchr(dataStart + 1, ','); }
    if (dataStart) { dataStart = strchr(dataStart + 1, ','); }
    if (dataStart) { dataStart++; } else { dataStart = buffer; }
  }

  // Check hop limit
  if (hops >= maxHops) {
    relayStats.messagesDropped++;
    return;
  }

  // Deduplication
  #if ENABLE_MESH_DEDUPLICATION
    if (isMessageSeen(srcId, remote.sequenceNumber)) {
      return;
    }
    markMessageSeen(srcId, remote.sequenceNumber);
  #endif

  // Forward with incremented hop count
  char fwdMsg[256];
  snprintf(fwdMsg, sizeof(fwdMsg), "HOP:%d/%d,SRC:%d,DST:%d,%s",
    hops + 1, maxHops, srcId, dstId, dataStart);

  delay(RELAY_FORWARD_DELAY_MS + random(50));
  lora_send(LORA_BROADCAST_ADDR, fwdMsg, strlen(fwdMsg));
  relayStats.messagesRelayed++;
  relayStats.totalHops += hops;
}
#endif

// ═══════════════════════════════════════════════════════════════════
// RELAY STATE MACHINE
// ═══════════════════════════════════════════════════════════════════

void handleRelayStateMachine() {
  // Relay queue processing handled by processRelayMessage in relay mode
}

// ═══════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════

void setup() {
  // Reduce CPU to 80 MHz (saves power, sufficient for LoRa)
  setCpuFrequencyMhz(80);

  // Initialize HALs
  debug_init();
  platform_init();

  // Banner
  Serial.println();
  Serial.println(F("╔════════════════════════════════════════╗"));
  Serial.println(F("║         LoraMeister v0.3.0             ║"));
  Serial.println(F("║    ESP32 DevKit + RYLR890              ║"));
  Serial.println(F("╚════════════════════════════════════════╝"));

  // LED boot sequence
  #if ENABLE_LED_STATUS
    ledStatusBoot();
  #endif

  // NVS & Watchdog
  #if ENABLE_NVS_STORAGE
    initNVS();
  #endif
  #if ENABLE_WATCHDOG
    initWatchdog();
  #endif

  // Power management
  initPowerManagement();

  // Random seed
  randomSeed(analogRead(0) ^ micros() ^ (uint32_t)(ESP.getEfuseMac() >> 32));

  // Kill-switch
  initKillSwitch();

  // ── Role Detection ──
  // RELAY check first (GPIO19↔20)
  pinMode(RELAY_MODE_PIN_B, OUTPUT);
  digitalWrite(RELAY_MODE_PIN_B, LOW);
  pinMode(RELAY_MODE_PIN_A, INPUT_PULLUP);
  delay(50);

  if (digitalRead(RELAY_MODE_PIN_A) == LOW) {
    bRELAY = true;
    bRECEIVER = false;
    Serial.println("═══ MODE: RELAY (mesh forwarder) ═══");
  } else {
    // RECEIVER check (GPIO16↔17)
    pinMode(MODE_GND_PIN, OUTPUT);
    digitalWrite(MODE_GND_PIN, LOW);
    pinMode(MODE_SELECT_PIN, INPUT_PULLUP);
    delay(50);

    if (digitalRead(MODE_SELECT_PIN) == LOW) {
      bRECEIVER = true;
      Serial.println("═══ MODE: RECEIVER ═══");
    } else {
      bRECEIVER = false;
      Serial.println("═══ MODE: SENDER ═══");
    }
  }

  // ── Address Assignment ──
  if (bRECEIVER) {
    MY_LORA_ADDRESS = LORA_RECEIVER_ADDRESS;  // Always 1
    TARGET_LORA_ADDRESS = LORA_BROADCAST_ADDR;
  } else if (bRELAY) {
    #if ENABLE_AUTO_ADDRESS
      MY_LORA_ADDRESS = getAutoDeviceAddress();
    #else
      MY_LORA_ADDRESS = LORA_RELAY_ADDRESS;
    #endif
    TARGET_LORA_ADDRESS = LORA_RECEIVER_ADDRESS;
  } else {
    // SENDER
    #if ENABLE_AUTO_ADDRESS
      MY_LORA_ADDRESS = getAutoDeviceAddress();
    #else
      MY_LORA_ADDRESS = LORA_SENDER_ADDRESS;
    #endif
    TARGET_LORA_ADDRESS = LORA_RECEIVER_ADDRESS;
  }

  Serial.printf("  My address: %d\n", MY_LORA_ADDRESS);
  Serial.printf("  Target: %d\n", TARGET_LORA_ADDRESS);

  // ── LCD Display ──
  display_init();
  initLCDDisplay();

  // Set LCD layout from config
  LCDLayout layout = (LCDLayout)(LCD_LAYOUT_NUMBER - 1);
  if (LCD_LAYOUT_NUMBER == 8) layout = LAYOUT_8;
  setLCDLayout(layout);

  showLCDStartup(bRECEIVER, MY_LORA_ADDRESS);

  // ── Device Tracker (receiver only) ──
  if (bRECEIVER) {
    initDeviceTracker();
  }

  // ── LoRa ──
  Serial.println("\n--- LoRa Initialization ---");
  if (!lora_init(MY_LORA_ADDRESS, LORA_NETWORK_ID)) {
    Serial.println("⚠️  LoRa init FAILED!");
    #if ENABLE_LED_STATUS
      ledStatusError();
    #endif
  } else {
    Serial.println("✓ LoRa radio initialized");
    Serial.printf("  Type: %s\n", LORA_HAL_TYPE);
    Serial.printf("  Band: %d MHz\n", LORA_BAND);
  }

  // ── Sensors (via registry — auto-initializes all registered sensors) ──
  sensorRegistry_initAll();

  // ── Health Monitor ──
  initHealthMonitor(health);

  // ── Relay Queue ──
  initRelayQueue(relayQueue);

  // ── Initialize timing ──
  memset(&timing, 0, sizeof(timing));
  spinner.symbols[0] = '<'; spinner.symbols[1] = '^';
  spinner.symbols[2] = '>'; spinner.symbols[3] = 'v';
  spinner.index = 0;
  spinner.lastUpdate = millis();

  // ── Device ID blink ──
  #if ENABLE_DEVICE_ID_BLINK
    blinkDeviceAddress(MY_LORA_ADDRESS);
  #endif

  #if ENABLE_WATCHDOG
    feedWatchdog();
  #endif

  Serial.println("\n✓ Setup complete — entering main loop\n");
}

// ═══════════════════════════════════════════════════════════════════
// COMMON TASKS (called every loop iteration)
// ═══════════════════════════════════════════════════════════════════

const char* getRoleName() {
  if (bRECEIVER) return "RECEIVER";
  if (bRELAY) return "RELAY";
  return "SENDER";
}

bool isAlarmActive() {
  // Keep alarm logic simple for v1 e-ink support: connection loss or sensor alarm state.
  return (health.state == CONN_LOST) || (local.lightState == 4) || (remote.lightState == 4);
}

void updateDisplays() {
  updateLCDDisplay(local, remote, health);
  display_showStatus(getRoleName(), timing.lastSend, remote.rssi, isAlarmActive());
}

void handleCommonTasks() {
  #if ENABLE_WATCHDOG
    feedWatchdog();
  #endif

  checkKillSwitch();
  handleRelayStateMachine();
  updateSpinner();

  // Update all registered sensors (each manages its own timing)
  if (!bRECEIVER) {  // Only SENDER reads sensors
    sensorRegistry_updateAll();
  }

  // Touch sensor
  #if ENABLE_TOUCH_SENSOR
    local.touchValue = touchRead(TOUCH_PIN);
    bool newTouchState = TOUCH_LOGIC_INVERTED ?
      (local.touchValue > TOUCH_THRESHOLD) :
      (local.touchValue < TOUCH_THRESHOLD);

    if (newTouchState && !local.touchState) {
      Serial.printf("[TOUCH] Detected! Value: %lu\n", local.touchValue);
    }
    local.touchState = newTouchState;
  #endif

  // Touch -> send command
  #if ENABLE_TOUCH_COMMAND
    if (local.touchState && bRECEIVER) {
      if (millis() - lastTouchCommandTime > TOUCH_COMMAND_COOLDOWN_MS) {
        char cmdMsg[64];
        snprintf(cmdMsg, sizeof(cmdMsg), "CMD:%s:%d:%lu",
          TOUCH_COMMAND_ACTION, MY_LORA_ADDRESS, millis());
        lora_send(TARGET_LORA_ADDRESS, cmdMsg, strlen(cmdMsg));
        lastTouchCommandTime = millis();
        Serial.printf("[TOUCH] Sent command: %s\n", TOUCH_COMMAND_ACTION);
      }
    }
  #endif

  // Manual AT commands (debug)
  #if ENABLE_MANUAL_AT_COMMANDS
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      if (input.startsWith("AT")) {
        sendLoRaCommand(input);
      }
    }
  #endif
}

// ═══════════════════════════════════════════════════════════════════
// RELAY MODE
// ═══════════════════════════════════════════════════════════════════

void handleRelayMode() {
  #if ENABLE_MESH_NETWORK
    processRelayMessage();

    // Print stats periodically
    static unsigned long lastRelayReport = 0;
    if (millis() - lastRelayReport > RELAY_STATS_REPORT_INTERVAL) {
      Serial.printf("[RELAY] RX:%lu FWD:%lu DRP:%lu\n",
        relayStats.messagesReceived, relayStats.messagesRelayed, relayStats.messagesDropped);
      lastRelayReport = millis();
    }

    // Update displays
    updateDisplays();
  #endif
}

// ═══════════════════════════════════════════════════════════════════
// RECEIVER MODE
// ═══════════════════════════════════════════════════════════════════

void handleReceiverMode() {
  if (!lora_available()) return;

  char buffer[256];
  int rssi = 0, snr = 0;
  if (!lora_receive(buffer, &rssi, &snr)) return;

  remote.rssi = rssi;
  remote.snr = snr;
  remote.lastMessageTime = millis();
  remote.messageCount++;

  // Parse mesh header
  char* dataStart = buffer;
  uint8_t srcId = 0;
  uint8_t hops = 0;

  if (strncmp(buffer, "HOP:", 4) == 0) {
    int maxH = 0, dst = 0;
    sscanf(buffer, "HOP:%hhu/%d,SRC:%hhu,DST:%d", &hops, &maxH, &srcId, &dst);
    // Skip past mesh header to data
    dataStart = buffer;
    for (int skip = 0; skip < 3 && *dataStart; skip++) {
      dataStart = strchr(dataStart, ',');
      if (dataStart) dataStart++;
    }
    if (!dataStart) dataStart = buffer;
  }

  remote.sourceId = srcId;
  remote.hopCount = hops;

  // Check if it's an ACK response (not regular data)
  if (strncmp(dataStart, "ACK:", 4) == 0) {
    Serial.printf("[RX] ACK received: %s\n", dataStart);
    return;
  }

  // Check for command
  if (strncmp(dataStart, "CMD:", 4) == 0) {
    processRemoteCommand(dataStart);
    return;
  }

  // Parse payload into remote struct
  parsePayload(dataStart, remote);

  // Track device
  registerDevice(srcId > 0 ? srcId : 2, rssi, snr);

  // Health monitor update
  updateRSSI(health, rssi);
  trackPacket(health, remote.sequenceNumber, srcId);

  // Status output
  static bool wasConnected = false;
  if (!wasConnected) {
    Serial.println("✓ Signal acquired!");
    wasConnected = true;
  }

  // Print received data
  Serial.printf("[RX] SEQ:%d RSSI:%d SNR:%d SRC:%d HOP:%d MIC:%ddB\n",
    remote.sequenceNumber, rssi, snr, srcId, hops, remote.micDB);

  // Send ACK periodically
  #if ENABLE_BIDIRECTIONAL
    if (remote.messageCount % ACK_INTERVAL == 0) {
      char ackMsg[128];
      snprintf(ackMsg, sizeof(ackMsg), "ACK:PING:%d:OK:RSSI:%d,SNR:%d",
        MY_LORA_ADDRESS, rssi, snr);

      #if ENABLE_MESH_NETWORK
        // Wrap in mesh header for relay
        char meshAck[256];
        snprintf(meshAck, sizeof(meshAck), "HOP:0/%d,SRC:%d,DST:%d,%s",
          MESH_MAX_HOPS, MY_LORA_ADDRESS, srcId > 0 ? srcId : TARGET_LORA_ADDRESS, ackMsg);
        lora_send(srcId > 0 ? srcId : TARGET_LORA_ADDRESS, meshAck, strlen(meshAck));
      #else
        lora_send(TARGET_LORA_ADDRESS, ackMsg, strlen(ackMsg));
      #endif

      Serial.printf("[ACK] Sent to %d (RSSI:%d, SNR:%d)\n",
        srcId > 0 ? srcId : TARGET_LORA_ADDRESS, rssi, snr);
    }
  #endif

  // Update displays
  updateDisplays();
}

// ═══════════════════════════════════════════════════════════════════
// SENDER MODE
// ═══════════════════════════════════════════════════════════════════

void handleSenderMode() {
  unsigned long now = millis();

  // Listen for incoming messages (commands, ACKs)
  if (lora_available()) {
    char buffer[256];
    int rssi = 0, snr = 0;
    if (lora_receive(buffer, &rssi, &snr)) {
      // Parse mesh header if present
      char* dataStart = buffer;
      if (strncmp(buffer, "HOP:", 4) == 0) {
        for (int skip = 0; skip < 3 && *dataStart; skip++) {
          dataStart = strchr(dataStart, ',');
          if (dataStart) dataStart++;
        }
        if (!dataStart) dataStart = buffer;
      }

      if (strncmp(dataStart, "ACK:", 4) == 0) {
        // Parse ACK: extract RSSI and SNR from receiver
        int rxRssi = 0, rxSnr = 0;
        char* rssiStr = strstr(dataStart, "RSSI:");
        char* snrStr = strstr(dataStart, "SNR:");
        if (rssiStr) rxRssi = atoi(rssiStr + 5);
        if (snrStr) rxSnr = atoi(snrStr + 4);

        ackReceived++;
        lastAckTime = now;
        remote.rssi = rxRssi;  // Store receiver's measured RSSI
        remote.snr = rxSnr;    // Store receiver's measured SNR

        Serial.printf("[ACK] #%d RX_RSSI:%d RX_SNR:%d (my_RSSI:%d)\n",
          ackReceived, rxRssi, rxSnr, rssi);
      } else if (strncmp(dataStart, "CMD:", 4) == 0) {
        processRemoteCommand(dataStart);
      }
    }
  }

  // Send data at interval
  if (now - timing.lastSend < LORA_SEND_INTERVAL_MS) return;
  timing.lastSend = now;

  if (transmissionStopped) return;

  // Update local state
  local.sequenceNumber = (local.sequenceNumber + 1) % 256;
  local.messageCount++;
  local.ledState = !local.ledState;

  // Build payload
  char payload[128];
  buildPayload(payload, sizeof(payload));

  // Wrap with mesh header
  char message[256];
  #if ENABLE_MESH_NETWORK
    snprintf(message, sizeof(message), "HOP:0/%d,SRC:%d,DST:%d,%s",
      MESH_MAX_HOPS, MY_LORA_ADDRESS, TARGET_LORA_ADDRESS, payload);
  #else
    strncpy(message, payload, sizeof(message));
  #endif

  // Send
  bool ok = lora_send(TARGET_LORA_ADDRESS, message, strlen(message));

  if (ok) {
    Serial.printf("[TX] #%d SEQ:%d MIC:%ddB -> addr %d\n",
      local.messageCount, local.sequenceNumber,
      #if ENABLE_MICROPHONE
        mic_getDB(),
      #else
        0,
      #endif
      TARGET_LORA_ADDRESS);

    // Toggle LED
    platform_setLed(local.ledState);
  } else {
    Serial.println("[TX] Send FAILED");
  }

  // ACK listen window
  #if ENABLE_BIDIRECTIONAL
    if (local.messageCount % ACK_INTERVAL == 0) {
      ackExpected++;

      // Listen for ACK
      unsigned long ackStart = millis();
      while (millis() - ackStart < LISTEN_TIMEOUT) {
        if (lora_available()) {
          char ackBuf[256];
          int ackRssi = 0, ackSnr = 0;
          if (lora_receive(ackBuf, &ackRssi, &ackSnr)) {
            char* ackData = ackBuf;
            if (strncmp(ackBuf, "HOP:", 4) == 0) {
              for (int s = 0; s < 3 && *ackData; s++) {
                ackData = strchr(ackData, ',');
                if (ackData) ackData++;
              }
              if (!ackData) ackData = ackBuf;
            }

            if (strncmp(ackData, "ACK:", 4) == 0) {
              int rxRssi = 0, rxSnr = 0;
              char* r = strstr(ackData, "RSSI:");
              char* s = strstr(ackData, "SNR:");
              if (r) rxRssi = atoi(r + 5);
              if (s) rxSnr = atoi(s + 4);

              ackReceived++;
              lastAckTime = millis();
              remote.rssi = rxRssi;
              remote.snr = rxSnr;

              Serial.printf("[ACK] ✓ RX_RSSI:%d RX_SNR:%d\n", rxRssi, rxSnr);
              break;
            }
          }
        }
        delay(1);
      }
    }
  #endif

  // Stale RSSI cleanup (no ACK for 2x send interval)
  if (remote.rssi != 0 && lastAckTime > 0) {
    if (now - lastAckTime > LORA_SEND_INTERVAL_MS * ACK_INTERVAL * 2) {
      remote.rssi = 0;
      remote.snr = 0;
      Serial.println("[RSSI] Stale — zeroed");
    }
  }

  // Update displays
  updateDisplays();
}

// ═══════════════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════════════

void loop() {
  handleCommonTasks();

  if (bRELAY)         handleRelayMode();
  else if (bRECEIVER) handleReceiverMode();
  else                handleSenderMode();

  // Data output (CSV and/or JSON)
  {
    unsigned long now = millis();
    if (now - timing.lastDataOutput >= DATA_OUTPUT_INTERVAL) {
      timing.lastDataOutput = now;
      #if ENABLE_CSV_OUTPUT
        printDataCSV();
      #endif
      #if ENABLE_JSON_OUTPUT
        printDataJSON();
      #endif
    }
  }

  // No delay — non-blocking loop
}
