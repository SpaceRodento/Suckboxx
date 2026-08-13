/*=====================================================================
  lora_handler.h - RYLR896 LoRa Module Communication Handler

  Complete interface for RYLR896 LoRa transceiver module with
  AT command protocol, mesh networking, and reliable messaging.

  PURPOSE:
  Manages all communication with the RYLR896 LoRa radio module,
  providing high-level functions for sending/receiving messages,
  mesh networking, auto-addressing, and signal quality monitoring.

  HARDWARE CONNECTION:
  RYLR896 TX → ESP32 GPIO25 (RXD2, config.h line 97)
  RYLR896 RX → ESP32 GPIO26 (TXD2, config.h line 98)
  Baudrate: 115200 (LORA_BAUD in config.h line 280)

  LORA PARAMETERS (Configured in config.h lines 285-295):
  - Spreading Factor: 12 (maximum range, LORA_PARAMETER)
  - Bandwidth: 125kHz (BW7)
  - Coding Rate: 4/5 (CR1)
  - Preamble: 4
  - Network ID: From config or auto-generated
  - Device Address: Auto-assigned or manual

  FEATURES:

  1. AT COMMAND INTERFACE
     - Reliable command sending with retries
     - Response parsing and error detection
     - Configurable timeouts (LORA_AT_TIMEOUT)

  2. MESH NETWORKING (config.h lines 360-400)
     - Zero-config auto-addressing (ENABLE_AUTO_ADDRESS)
     - Message forwarding and routing
     - TTL and duplicate detection
     - Collision avoidance (ENABLE_COLLISION_AVOIDANCE)

  3. MESSAGE HANDLING
     - Reliable send with ACK (ENABLE_ACK)
     - RSSI and SNR monitoring
     - Encryption support (ENABLE_LORA_ENCRYPTION)

  4. SIGNAL QUALITY
     - RSSI (Received Signal Strength Indicator)
     - SNR (Signal-to-Noise Ratio)
     - Range estimation

  AIR TIME REFERENCE (SF12, BW 125kHz):
  - 10 bytes: ~1.3 seconds
  - 20 bytes: ~2.0 seconds
  - 34 bytes: ~2.6 seconds
  - 36 bytes: ~2.8 seconds
  Important: RYLR896 responds with +OK AFTER transmission completes!
  AT+SEND timeout must exceed air time (3200ms = 2800ms air + 400ms margin)

  CONFIGURATION (config.h):
  See config.h lines 280-420 for all LoRa and mesh settings

  USAGE EXAMPLE:
    #include "lora_handler.h"

    void setup() {
      initLoRa(MY_ADDRESS, NETWORK_ID);  // Initialize LoRa
    }

    void loop() {
      // Send message
      sendLoRaMessage(targetAddr, "Hello", 5);

      // Receive message
      if (receiveLoRaMessage(buffer, &rssi, &snr)) {
        // Process received message
      }
    }

=======================================================================*/

#ifndef LORA_HANDLER_H
#define LORA_HANDLER_H

#include <HardwareSerial.h>
#include <esp_system.h>       // For esp_restart()

// ESP-IDF version check - may not exist in older Arduino ESP32 cores
#if __has_include(<esp_idf_version.h>)
  #include <esp_idf_version.h>
#endif

// Ensure ESP_IDF_VERSION macros are defined (fallback for older cores)
#ifndef ESP_IDF_VERSION_VAL
  #define ESP_IDF_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#endif
#ifndef ESP_IDF_VERSION
  #define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(4, 4, 0)  // Assume v4.x
#endif

// MAC address functions - header location changed in ESP-IDF v5.x
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  #include <esp_mac.h>        // ESP-IDF v5.x: esp_read_mac() and ESP_MAC_WIFI_STA
#else
  #include <esp_wifi.h>       // ESP-IDF v4.x: esp_read_mac() in this header
  #ifndef ESP_MAC_WIFI_STA
    #define ESP_MAC_WIFI_STA 0  // Define if not available
  #endif
#endif

#include "config.h"
#include "structs.h"

// Use Serial1 explicitly for better reliability
HardwareSerial LoRaSerial(1);

// =============== LoRa PARAMETERS (read from module) ================================
// Global variables to store actual LoRa parameters read from RYLR896
// Updated by readLoRaParameters() after initialization
int g_spreadingFactor = 10;   // Default SF10 (faster air time for ACK)
int g_bandwidth = 125;         // Default 125 kHz
int g_codingRate = 1;          // Default 4/5 (1=4/5, 2=4/6, 3=4/7, 4=4/8)
int g_preamble = 4;            // Default preamble length
int g_txPower = 22;            // Default 22 dBm (max power)

// =============== AUTO-ADDRESS FROM MAC ================================
// getAutoDeviceAddress() moved to platform_hal.h (shared utility)
// Available when ENABLE_AUTO_ADDRESS is true in config.h

// =============== AT COMMAND FUNCTION ================================
inline String sendLoRaCommand(String command, int timeout = 500) {
  // Clear any pending data before sending command
  while (LoRaSerial.available()) {
    LoRaSerial.read();
  }

  #if DEBUG_LORA_AT
  Serial.print("→ AT CMD: ");
  Serial.println(command);
  #endif

  LoRaSerial.println(command);

  unsigned long start = millis();
  unsigned long lastWatchdogFeed = millis();
  unsigned long lastCharTime = millis();  // Track time since last character

  // FIXED v2.3.2: Use char buffer instead of String concatenation
  // Old: response += c caused fragmentation on every AT command
  char response[512];  // Buffer for response (LoRa responses typically <200 bytes)
  int idx = 0;

  while (millis() - start < timeout) {
    // Feed watchdog every 500ms during long waits (prevents timeout in setup)
    #if ENABLE_WATCHDOG
      if (millis() - lastWatchdogFeed >= 500) {
        extern void feedWatchdog();
        feedWatchdog();
        lastWatchdogFeed = millis();
      }
    #endif

    if (LoRaSerial.available()) {
      char c = LoRaSerial.read();
      if (idx < sizeof(response) - 1) {  // Prevent buffer overflow
        response[idx++] = c;
      }
      lastCharTime = millis();  // Update last character time

      // Smart break: If we see complete response with OK, break early
      // This prevents waiting full timeout when response is ready
      if (idx >= 3 && c == '\n') {
        // Null-terminate temporarily to check for OK
        response[idx] = '\0';
        // Check if response contains "+OK" or ends with "OK"
        if (strstr(response, "+OK") != NULL || strstr(response, "OK") != NULL) {
          break;  // Complete response received, exit early
        }
      }
    } else {
      // No data available - check if we should give up
      // If 100ms since last character and we have some data, probably done
      if (idx > 0 && (millis() - lastCharTime > 100)) {
        break;  // No more data coming, exit
      }
    }
  }

  response[idx] = '\0';  // Null-terminate

  #if DEBUG_LORA_AT
  unsigned long elapsed = millis() - start;
  Serial.print("← Response (");
  Serial.print(idx);
  Serial.print(" bytes, ");
  Serial.print(elapsed);
  Serial.print(" ms): [");
  Serial.print(response);
  Serial.println("]");
  #endif

  String result = String(response);
  result.trim();
  return result;
}

// Helper: Clear serial buffer and wait for READY signal
inline void waitForReady(int timeout = 5000) {
  Serial.println("Waiting for +READY signal...");
  unsigned long start = millis();
  unsigned long lastWatchdogFeed = millis();
  String buffer = "";

  while (millis() - start < timeout) {
    // Feed watchdog every 500ms during long waits (prevents timeout in setup)
    #if ENABLE_WATCHDOG
      if (millis() - lastWatchdogFeed >= 500) {
        extern void feedWatchdog();
        feedWatchdog();
        lastWatchdogFeed = millis();
      }
    #endif

    if (LoRaSerial.available()) {
      char c = LoRaSerial.read();
      buffer += c;

      // Check if we got READY signal
      if (buffer.indexOf("READY") >= 0) {
        Serial.println("✓ Module ready!");
        delay(100);
        // Clear any remaining data
        while (LoRaSerial.available()) LoRaSerial.read();
        return;
      }

      // Keep buffer size manageable
      if (buffer.length() > 50) {
        buffer = buffer.substring(buffer.length() - 30);
      }
    }
    delay(10);  // Small delay to prevent tight loop
  }
  Serial.println("⚠ READY signal timeout (continuing anyway)");
}

// =============== RYLR896 STATE TRACKING ================================
// Global state for RYLR896 module (used by lora_hal.h compatibility layer)
static uint8_t g_rylr896_address = 0;
static bool g_rylr896_initialized = false;

// Get current LoRa address
inline uint8_t getLoRaAddress() {
  return g_rylr896_address;
}

// Check if LoRa module is initialized
inline bool isLoRaInitialized() {
  return g_rylr896_initialized;
}

// =============== INITIALIZE LoRa ================================
inline bool initLoRa(uint8_t myAddress, uint8_t networkID) {
  g_rylr896_address = myAddress;  // Store address for getLoRaAddress()
  Serial.println("\n============================");
  Serial.println("=== LoRa Init ===");
  Serial.println("============================");

  // Start serial connection
  LoRaSerial.begin(LORA_BAUDRATE, SERIAL_8N1, RXD2, TXD2);
  delay(1000);

  // Feed watchdog after initial delay
  #if ENABLE_WATCHDOG
    extern void feedWatchdog();
    feedWatchdog();
  #endif

  // Clear serial buffer
  while (LoRaSerial.available()) LoRaSerial.read();

  // Reset module
  Serial.println("Resetting module...");
  String response = sendLoRaCommand("AT+RESET", 2000);
  waitForReady(5000);

  // Test connection
  Serial.println("Testing connection...");
  response = sendLoRaCommand("AT", 1500);
  if (response.indexOf("OK") < 0) {
    Serial.println("❌ Module not responding!");
    return false;
  }
  Serial.println("✓ Module responding");

  // Get version
  response = sendLoRaCommand("AT+VER?", 1000);

  // Set address
  Serial.print("Setting address to ");
  Serial.print(myAddress);
  Serial.println("...");
  response = sendLoRaCommand("AT+ADDRESS=" + String(myAddress), 1000);
  if (response.indexOf("OK") < 0) {
    Serial.println("❌ Address failed!");
    return false;
  }
  Serial.println("✓ Address set");

  // Set network ID
  Serial.print("Setting network ID to ");
  Serial.print(networkID);
  Serial.println("...");
  response = sendLoRaCommand("AT+NETWORKID=" + String(networkID), 1000);
  if (response.indexOf("OK") < 0) {
    Serial.println("❌ Network ID failed!");
    return false;
  }
  Serial.println("✓ Network ID set");

  // Set parameters (SF10 = faster air time, good for ACK)
  Serial.println("Setting parameters...");
  response = sendLoRaCommand("AT+PARAMETER=10,7,1,4", 1000);
  if (response.indexOf("OK") >= 0) {
    Serial.println("✓ Parameters: SF10, BW125kHz");
  }
  
  Serial.println("============================");
  Serial.println("✓ RYLR896 Ready!");
  Serial.println("============================\n");

  g_rylr896_initialized = true;  // Mark as initialized
  return true;
}

// =============== READ LoRa PARAMETERS ================================
// Read actual LoRa parameters from RYLR896 module
// Updates global variables (g_spreadingFactor, g_bandwidth, etc.)
// Call this after initLoRa() to get real values for telemetry
inline void readLoRaParameters() {
  Serial.println("Reading LoRa parameters from module...");

  // Read parameters: AT+PARAMETER?
  // Response format: +PARAMETER=SF,BW,CR,PREAMBLE
  // Example: +PARAMETER=12,7,1,4
  // Where: SF=12, BW=7 (125kHz), CR=1 (4/5), PREAMBLE=4
  String response = sendLoRaCommand("AT+PARAMETER?", 1000);

  if (response.indexOf("+PARAMETER=") >= 0) {
    int start = response.indexOf('=') + 1;
    String params = response.substring(start);
    params.trim();

    // Parse: SF,BW,CR,PREAMBLE
    int comma1 = params.indexOf(',');
    int comma2 = params.indexOf(',', comma1 + 1);
    int comma3 = params.indexOf(',', comma2 + 1);

    if (comma1 > 0 && comma2 > 0 && comma3 > 0) {
      g_spreadingFactor = params.substring(0, comma1).toInt();
      int bw_code = params.substring(comma1 + 1, comma2).toInt();
      g_codingRate = params.substring(comma2 + 1, comma3).toInt();
      g_preamble = params.substring(comma3 + 1).toInt();

      // Convert BW code to actual kHz value
      // BW code: 7=125kHz, 8=250kHz, 9=500kHz
      if (bw_code == 7) g_bandwidth = 125;
      else if (bw_code == 8) g_bandwidth = 250;
      else if (bw_code == 9) g_bandwidth = 500;

      Serial.print("✓ Read parameters: SF");
      Serial.print(g_spreadingFactor);
      Serial.print(", BW");
      Serial.print(g_bandwidth);
      Serial.print("kHz, CR4/");
      Serial.print(g_codingRate + 4);  // 1=4/5, 2=4/6, etc.
      Serial.print(", Preamble ");
      Serial.println(g_preamble);
    }
  }

  // Read TX power: AT+CRFOP?
  // Response format: +CRFOP=power
  // Example: +CRFOP=22
  response = sendLoRaCommand("AT+CRFOP?", 1000);

  if (response.indexOf("+CRFOP=") >= 0) {
    int start = response.indexOf('=') + 1;
    g_txPower = response.substring(start).toInt();

    Serial.print("✓ TX Power: ");
    Serial.print(g_txPower);
    Serial.println(" dBm");
  }

  Serial.println("✓ Parameter reading complete\n");
}

// =============== SEND MESSAGE ================================
inline bool sendLoRaMessage(String message, uint8_t targetAddress) {
  // FIXED v2.3.2: Use char buffer instead of String concatenation to prevent heap fragmentation
  // Old version caused memory fragmentation after ~100-200 messages → send failures
  // This was the root cause of ~50% packet loss after hours of operation
  char command[256];  // Buffer for AT+SEND command (max payload ~240 bytes)

  snprintf(command, sizeof(command), "AT+SEND=%d,%d,%s",
           targetAddress, message.length(), message.c_str());

  // SF10 is faster! Air time for 36 bytes: ~900ms
  // Must wait for +OK response AFTER message is transmitted
  // Timeout: 1200ms = 900ms air time + 300ms margin (sufficient without excessive blocking)
  String response = sendLoRaCommand(command, 1200);

  // DEBUG: Show what response we got (helps diagnose false failures)
  #if DEBUG_LORA_AT
  Serial.print("🔍 Response: [");
  Serial.print(response);
  Serial.print("] len=");
  Serial.println(response.length());
  #endif

  // Check for +OK or OK in response
  // LoRa module may return "+OK" or just "OK"
  if (response.indexOf("OK") >= 0) {
    return true;
  } else {
    // Send failed - show diagnostic info
    Serial.print("❌ LoRa send failed: ");
    Serial.println(message);

    // Always show response and command when failure occurs (regardless of DEBUG_LORA_AT)
    Serial.print("   AT command: ");
    Serial.println(command);
    Serial.print("   Response was: [");
    Serial.print(response);
    Serial.print("] (");
    Serial.print(response.length());
    Serial.println(" bytes)");

    return false;
  }
}

// =============== SEND MESSAGE WITH RETRY ================================
// Automatic retry with exponential backoff for improved reliability
// Retries failed transmissions with increasing delays: 100ms, 200ms, 400ms, ...
// Use this for critical messages that must get through
#if ENABLE_TX_RETRY
inline bool sendLoRaMessageReliable(String message, uint8_t targetAddress, int maxRetries = TX_MAX_RETRIES) {
  for (int attempt = 0; attempt <= maxRetries; attempt++) {
    if (sendLoRaMessage(message, targetAddress)) {
      if (attempt > 0) {
        Serial.print("✓ Success on retry #");
        Serial.println(attempt);
      }
      return true;  // Success!
    }

    if (attempt < maxRetries) {
      // Exponential backoff: 100ms, 200ms, 400ms, 800ms...
      int backoff = TX_RETRY_BACKOFF_MS * (1 << attempt);
      Serial.print("⏱️  Retry ");
      Serial.print(attempt + 1);
      Serial.print("/");
      Serial.print(maxRetries);
      Serial.print(" in ");
      Serial.print(backoff);
      Serial.println("ms...");
      delay(backoff);

      // Feed watchdog during retry delay
      #if ENABLE_WATCHDOG
        extern void feedWatchdog();
        feedWatchdog();
      #endif
    }
  }

  Serial.print("❌ All ");
  Serial.print(maxRetries);
  Serial.println(" retry attempts failed");
  return false;  // Failed after all retries
}
#else
// If TX_RETRY is disabled, just call the basic function
inline bool sendLoRaMessageReliable(String message, uint8_t targetAddress, int maxRetries = 0) {
  return sendLoRaMessage(message, targetAddress);
}
#endif

// =============== MESH NETWORK HELPERS ================================

// Global seen messages buffer for deduplication
#if ENABLE_MESH_DEDUPLICATION
SeenMessage seenMessages[DEDUP_BUFFER_SIZE];
uint8_t seenMessagesIndex = 0;
bool seenMessagesInitialized = false;

// Initialize seen messages buffer
inline void initSeenMessages() {
  for (int i = 0; i < DEDUP_BUFFER_SIZE; i++) {
    seenMessages[i].valid = false;
  }
  seenMessagesInitialized = true;
}

// Check if message was already seen (duplicate)
inline bool isMessageSeen(uint8_t sourceId, uint16_t seqNum) {
  if (!seenMessagesInitialized) initSeenMessages();

  unsigned long now = millis();
  for (int i = 0; i < DEDUP_BUFFER_SIZE; i++) {
    if (seenMessages[i].valid &&
        seenMessages[i].sourceId == sourceId &&
        seenMessages[i].sequenceNumber == seqNum &&
        (now - seenMessages[i].timestamp) < DEDUP_TIMEOUT_MS) {
      return true;  // Duplicate!
    }
  }
  return false;
}

// Mark message as seen
inline void markMessageSeen(uint8_t sourceId, uint16_t seqNum) {
  if (!seenMessagesInitialized) initSeenMessages();

  seenMessages[seenMessagesIndex].sourceId = sourceId;
  seenMessages[seenMessagesIndex].sequenceNumber = seqNum;
  seenMessages[seenMessagesIndex].timestamp = millis();
  seenMessages[seenMessagesIndex].valid = true;

  seenMessagesIndex = (seenMessagesIndex + 1) % DEDUP_BUFFER_SIZE;
}

// Cleanup old entries from seen messages buffer (call periodically)
// This frees up slots for new messages and prevents false negatives
inline void cleanupSeenMessages() {
  if (!seenMessagesInitialized) return;

  unsigned long now = millis();
  int cleaned = 0;
  for (int i = 0; i < DEDUP_BUFFER_SIZE; i++) {
    if (seenMessages[i].valid &&
        (now - seenMessages[i].timestamp) > DEDUP_TIMEOUT_MS) {
      seenMessages[i].valid = false;  // Mark as expired
      cleaned++;
    }
  }
  if (cleaned > 0) {
    Serial.print("🧹 Dedup buffer cleanup: ");
    Serial.print(cleaned);
    Serial.println(" old entries removed");
  }
}
#endif

// =============== MESH ENCRYPTION ================================
#if ENABLE_MESH_ENCRYPTION
// Simple XOR encryption for basic obfuscation
// ⚠️  NOT cryptographically secure - for privacy only, not security!
// Method: XOR each byte with rotating 32-bit key
inline String encryptPayload(String payload) {
  // FIXED v2.3.2: Pre-allocate buffer instead of char-by-char concatenation
  // Old version: encrypted += (char)... caused 36 reallocations for 36-byte payload!
  // This was a MAJOR source of heap fragmentation
  int len = payload.length();
  char encrypted[256];  // Pre-allocated buffer (max 240 bytes payload + overhead)

  uint32_t key = MESH_ENCRYPTION_KEY;
  for (int i = 0; i < len; i++) {
    // XOR with rotating key bytes
    uint8_t keyByte = (key >> ((i % 4) * 8)) & 0xFF;
    encrypted[i] = payload[i] ^ keyByte;
  }
  encrypted[len] = '\0';  // Null-terminate

  return String(encrypted);  // Convert to String only once
}

// XOR decryption is identical to encryption (symmetric)
inline String decryptPayload(String encrypted) {
  return encryptPayload(encrypted);
}
#endif

// =============== MESH MESSAGE PARSING ================================
// Extended format: "HOP:1/3,SRC:2,DST:0,ENC:0,SEQ:42,LED:1,..."
// Legacy format:   "HOP:1/3,SEQ:42,LED:1,..." or "SEQ:42,LED:1,..."

inline MeshMessage parseMeshMessage(String rawPayload) {
  MeshMessage msg;
  msg.hopCount = 0;
  msg.maxHops = MESH_MAX_HOPS;
  msg.sourceId = 0;
  msg.destinationId = MESH_BROADCAST_ID;
  msg.sequenceNumber = 0;
  msg.isEncrypted = false;
  msg.valid = true;  // Assume valid, set false on critical parse failure
  msg.payload = rawPayload;

  // Empty payload is invalid
  if (rawPayload.length() == 0) {
    msg.valid = false;
    return msg;
  }

  String remaining = rawPayload;

  // Parse HOP:current/max
  int hopIdx = remaining.indexOf("HOP:");
  if (hopIdx >= 0) {
    int slashIdx = remaining.indexOf('/', hopIdx);
    int commaIdx = remaining.indexOf(',', hopIdx);
    if (slashIdx > hopIdx && commaIdx > slashIdx) {
      msg.hopCount = remaining.substring(hopIdx + 4, slashIdx).toInt();
      msg.maxHops = remaining.substring(slashIdx + 1, commaIdx).toInt();
      remaining = remaining.substring(commaIdx + 1);
    }
  }

  // Parse SRC:id (source ID)
  int srcIdx = remaining.indexOf("SRC:");
  if (srcIdx >= 0) {
    int commaIdx = remaining.indexOf(',', srcIdx);
    if (commaIdx > srcIdx) {
      msg.sourceId = remaining.substring(srcIdx + 4, commaIdx).toInt();
      remaining = remaining.substring(0, srcIdx) + remaining.substring(commaIdx + 1);
    }
  }

  // Parse DST:id (destination ID)
  int dstIdx = remaining.indexOf("DST:");
  if (dstIdx >= 0) {
    int commaIdx = remaining.indexOf(',', dstIdx);
    if (commaIdx > dstIdx) {
      msg.destinationId = remaining.substring(dstIdx + 4, commaIdx).toInt();
      remaining = remaining.substring(0, dstIdx) + remaining.substring(commaIdx + 1);
    }
  }

  // Parse ENC:0/1 (encryption flag)
  int encIdx = remaining.indexOf("ENC:");
  if (encIdx >= 0) {
    int commaIdx = remaining.indexOf(',', encIdx);
    if (commaIdx > encIdx) {
      msg.isEncrypted = remaining.substring(encIdx + 4, commaIdx).toInt() == 1;
      remaining = remaining.substring(0, encIdx) + remaining.substring(commaIdx + 1);
    }
  }

  // Parse SEQ:num for deduplication
  int seqIdx = remaining.indexOf("SEQ:");
  if (seqIdx >= 0) {
    int commaIdx = remaining.indexOf(',', seqIdx);
    if (commaIdx > seqIdx) {
      msg.sequenceNumber = remaining.substring(seqIdx + 4, commaIdx).toInt();
    } else {
      msg.sequenceNumber = remaining.substring(seqIdx + 4).toInt();
    }
  }

  // Remaining is the payload (without mesh headers)
  msg.payload = remaining;

  // Validate parsed values - detect parse errors
  // hopCount/maxHops > 255 indicates toInt() overflow or malformed data
  if (msg.hopCount > 100 || msg.maxHops > 100 || msg.maxHops == 0) {
    msg.valid = false;
    Serial.println("⚠️ Mesh parse error: invalid hop count");
  }

  // Decrypt if needed
  #if ENABLE_MESH_ENCRYPTION
  if (msg.isEncrypted) {
    msg.payload = decryptPayload(msg.payload);
    msg.isEncrypted = false;  // Now decrypted
  }
  #endif

  return msg;
}

// Build mesh message with all headers
// Supports optional XOR encryption for basic obfuscation
inline String buildMeshMessage(String originalPayload, uint8_t hopCount, uint8_t maxHops,
                                uint8_t sourceId, uint8_t destinationId, bool encrypt) {
  String header = "HOP:" + String(hopCount) + "/" + String(maxHops);

  #if ENABLE_MESH_SOURCE_TRACKING
  header += ",SRC:" + String(sourceId);
  #endif

  // Note: Targeted routing has been removed - all messages use broadcast
  // (destinationId parameter kept for API compatibility)

  String payload = originalPayload;

  #if ENABLE_MESH_ENCRYPTION
  if (encrypt) {
    payload = encryptPayload(payload);
    header += ",ENC:1";
  } else {
    header += ",ENC:0";
  }
  #endif

  return header + "," + payload;
}

// Simplified build for relay (just increment hop count, keep other fields)
inline String buildRelayMessage(MeshMessage msg) {
  return buildMeshMessage(msg.payload, msg.hopCount + 1, msg.maxHops,
                          msg.sourceId, msg.destinationId, false);
}

// Check if message should be relayed
inline bool shouldRelay(MeshMessage msg, int rssi, uint8_t myAddress) {
  // Check hop count
  if (msg.hopCount >= msg.maxHops) {
    return false;
  }

  // Check RSSI filter
  #if ENABLE_MESH_RSSI_FILTER
  if (rssi < MESH_RELAY_MIN_RSSI) {
    Serial.print("⚠️ RSSI too weak (");
    Serial.print(rssi);
    Serial.println(" dBm) - not relaying");
    return false;
  }
  #endif

  // Note: Targeted routing has been removed - all messages use broadcast

  // Check deduplication
  #if ENABLE_MESH_DEDUPLICATION
  if (isMessageSeen(msg.sourceId, msg.sequenceNumber)) {
    Serial.println("🔁 Duplicate message - not relaying");
    return false;
  }
  #endif

  return true;
}

// Check if message is for this device
inline bool isMessageForMe(MeshMessage msg, uint8_t myAddress) {
  // Targeted routing has been removed - all messages use broadcast
  return true;  // All messages are for everyone in broadcast mode
}

// Legacy compatibility wrapper
inline String incrementHopCount(MeshMessage msg) {
  return buildRelayMessage(msg);
}

// =============== RECEIVE MESSAGE ================================
inline bool receiveLoRaMessage(DeviceState& remote, String& payload) {
  if (!LoRaSerial.available()) {
    return false;
  }

  // Use readStringUntil() - simple, single allocation
  // This is the same approach that worked perfectly in November 2025 with 0% packet loss for days
  // Simple = Reliable. Avoid complex nested conditions that can drop messages.
  String response = LoRaSerial.readStringUntil('\n');
  response.trim();

  // Check if it's a received message
  if (!response.startsWith("+RCV=")) {
    return false;
  }

  // Parse: +RCV=sender,length,data,RSSI,SNR
  // NOTE: Data field may contain commas! Use length to parse correctly.
  int start = 5;  // Skip "+RCV="
  int comma1 = response.indexOf(',', start);
  int comma2 = response.indexOf(',', comma1 + 1);

  if (comma1 <= 0 || comma2 <= 0) {
    Serial.println("⚠️  RX parse error: missing commas");
    return false;
  }

  // Parse data length using substring + toInt() - simple and reliable
  String lengthStr = response.substring(comma1 + 1, comma2);
  int dataLength = lengthStr.toInt();

  if (dataLength <= 0 || dataLength > 200) {
    Serial.print("⚠️  RX parse error: invalid length ");
    Serial.println(dataLength);
    return false;
  }

  // Extract payload (exact length - data may contain commas!)
  int dataStart = comma2 + 1;
  int dataEnd = dataStart + dataLength;

  // Bounds check - prevent substring out of range
  if (dataEnd > response.length()) {
    Serial.println("⚠️  RX parse error: data exceeds response");
    return false;
  }

  payload = response.substring(dataStart, dataEnd);

  // RSSI and SNR are after the data
  int rssiStart = dataEnd + 1;  // +1 for comma
  int comma3 = response.indexOf(',', rssiStart);

  if (comma3 <= 0) {
    Serial.println("⚠️  RX parse error: missing RSSI/SNR comma");
    return false;
  }

  // Parse RSSI and SNR - simple substring + toInt()
  String rssiStr = response.substring(rssiStart, comma3);
  String snrStr = response.substring(comma3 + 1);

  remote.rssi = rssiStr.toInt();
  remote.snr = snrStr.toInt();
  remote.lastMessageTime = millis();

  Serial.print("📥 RX [");
  Serial.print(payload);
  Serial.print("] RSSI:");
  Serial.print(remote.rssi);
  Serial.print(" SNR:");
  Serial.println(remote.snr);

  return true;
}

#endif // LORA_HANDLER_H