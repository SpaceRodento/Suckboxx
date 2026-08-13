/*=====================================================================
  lora_hal.h - LoRa Hardware Abstraction Layer

  LoraMeister - RYLR890/RYLR896 AT-command LoRa driver

  USAGE:
    #include "lora_hal.h"

    void setup() {
      if (!lora_init(deviceAddress, networkId)) {
        Serial.println("LoRa init failed!");
      }
      lora_startReceive();
    }

    void loop() {
      if (lora_available()) {
        char buffer[256];
        int rssi, snr;
        if (lora_receive(buffer, &rssi, &snr)) {
          // Process message
        }
        lora_startReceive();
      }

      if (shouldSend) {
        lora_send(target, message, strlen(message));
      }
    }

  API:
  - lora_init(address, networkId)  -> Initialize radio
  - lora_send(target, msg, len)    -> Send message
  - lora_receive(buf, rssi, snr)   -> Receive message
  - lora_available()               -> Check if packet received
  - lora_startReceive()            -> Enable RX mode
  - lora_getRSSI()                 -> Get last RSSI
  - lora_getSNR()                  -> Get last SNR
  - lora_isReady()                 -> Check if initialized
  - lora_getAddress()              -> Get device address
=======================================================================*/

#ifndef LORA_HAL_H
#define LORA_HAL_H

#include "config.h"
#include "structs.h"

// ═══════════════════════════════════════════════════════════════════
// RYLR890/RYLR896 (AT Commands, UART)
// ═══════════════════════════════════════════════════════════════════

#include "lora_handler.h"

#define LORA_HAL_TYPE "RYLR890 (AT)"

// State tracking
static int g_rylr_lastRSSI = 0;
static int g_rylr_lastSNR = 0;
static unsigned long g_rylr_txCount = 0;
static unsigned long g_rylr_rxCount = 0;

// Unified API -> RYLR functions
inline bool lora_init(uint8_t address, uint8_t networkId) {
  return initLoRa(address, networkId);
}

inline bool lora_send(uint8_t target, const char* message, int length) {
  bool result = sendLoRaMessage(String(message), target);
  if (result) g_rylr_txCount++;
  return result;
}

inline bool lora_sendRaw(const char* message) {
  return sendLoRaMessage(String(message), LORA_BROADCAST_ADDR);
}

// Dummy DeviceState for lora_receive compatibility
static DeviceState g_rylr_tempRemote;

inline bool lora_receive(char* buffer, int* rssi, int* snr) {
  String payload;
  bool result = receiveLoRaMessage(g_rylr_tempRemote, payload);
  if (result) {
    g_rylr_rxCount++;
    payload.toCharArray(buffer, 256);
    if (rssi) {
      *rssi = g_rylr_tempRemote.rssi;
      g_rylr_lastRSSI = *rssi;
    }
    if (snr) {
      *snr = g_rylr_tempRemote.snr;
      g_rylr_lastSNR = *snr;
    }
  }
  return result;
}

inline bool lora_receiveTimeout(char* buffer, int* rssi, int* snr, unsigned long timeout_ms) {
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    if (LoRaSerial.available()) {
      String payload;
      if (receiveLoRaMessage(g_rylr_tempRemote, payload)) {
        g_rylr_rxCount++;
        payload.toCharArray(buffer, 256);
        if (rssi) {
          *rssi = g_rylr_tempRemote.rssi;
          g_rylr_lastRSSI = *rssi;
        }
        if (snr) {
          *snr = g_rylr_tempRemote.snr;
          g_rylr_lastSNR = *snr;
        }
        return true;
      }
    }
    delay(1);
  }
  return false;
}

inline bool lora_available() {
  return LoRaSerial.available() > 0;
}

inline void lora_startReceive() {
  // RYLR890: Always listening, just clear pending data
  while (LoRaSerial.available()) {
    LoRaSerial.read();
  }
}

inline int lora_getRSSI() { return g_rylr_lastRSSI; }
inline int lora_getSNR() { return g_rylr_lastSNR; }
inline uint8_t lora_getAddress() { return getLoRaAddress(); }
inline bool lora_isReady() { return isLoRaInitialized(); }
inline unsigned long lora_getTxCount() { return g_rylr_txCount; }
inline unsigned long lora_getRxCount() { return g_rylr_rxCount; }

// Runtime configuration (limited on RYLR890)
inline bool lora_setSpreadingFactor(int sf) {
  Serial.println(F("[RYLR890] SF change requires restart"));
  return false;
}

inline bool lora_setBandwidth(float bw) {
  Serial.println(F("[RYLR890] BW change requires restart"));
  return false;
}

inline bool lora_setTxPower(int power) {
  Serial.println(F("[RYLR890] Power change not implemented"));
  return false;
}

inline bool lora_setFrequency(float freq) {
  Serial.println(F("[RYLR890] Freq change requires restart"));
  return false;
}

// Power management
inline bool lora_sleep() {
  Serial.println(F("[RYLR890] Sleep not implemented"));
  return false;
}

inline bool lora_wake() {
  return true;  // Always awake
}

// Diagnostics
inline void lora_printDiagnostics() {
  Serial.println();
  Serial.println(F("╔═══════════════════════════════════════════════════╗"));
  Serial.println(F("║    RYLR890 Radio Diagnostics                     ║"));
  Serial.println(F("╚═══════════════════════════════════════════════════╝"));
  Serial.print(F("Address: ")); Serial.println(getLoRaAddress());
  Serial.print(F("TX count: ")); Serial.println(g_rylr_txCount);
  Serial.print(F("RX count: ")); Serial.println(g_rylr_rxCount);
  Serial.print(F("Last RSSI: ")); Serial.println(g_rylr_lastRSSI);
  Serial.print(F("Last SNR: ")); Serial.println(g_rylr_lastSNR);
  Serial.println();
}

#endif // LORA_HAL_H
