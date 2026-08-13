/*=====================================================================
  lora_comm.h - LoRa Communication

  KasviAsema acts as SENDER — periodic reports to RPi RECEIVER.
  Also listens for incoming commands between reports.

  Platform:
    USE_XIAO_SX1262 false  → RYLR890 UART/AT commands (default)
    USE_XIAO_SX1262 true   → SX1262 SPI/RadioLib (lora_sx1262.h)

  Message format (outgoing):
    KA:T=22.5,H=65,P=1013,WT=21.0,TDS=650,PH=150,MH=200,PI=basil,WL=1,L=1,AP=1,BAT=4.1,BP=85

  Command format (incoming):
    CMD:LIGHT=1     CMD:WATER=50    CMD:HEIGHT=200
    CMD:PLANT=basil CMD:AIRPUMP=0   CMD:REBOOT      CMD:AP
=====================================================================*/

#ifndef LORA_COMM_H
#define LORA_COMM_H

#include "config.h"
#include "structs.h"

#if ENABLE_LORA

// ── Platform switch ──────────────────────────────────────────────
// lora_sx1262.h provides the same public API — plantmeister.ino is unchanged.
#if USE_XIAO_SX1262
  #include "lora_sx1262.h"
#else
// ── RYLR890 AT-command implementation (WROOM-32E) ────────────────

#include "lora_parser.h"
#include "lora_payload.h"

static bool g_loraReady = false;
static char g_loraRxBuffer[LORA_MAX_PAYLOAD + 64];
static int g_loraRxIndex = 0;
static int g_loraLastRssi = 0;
static int g_loraLastSnr = 0;

// ── AT command helpers ──────────────────────────────────────────

// Send AT command and wait for response
static bool lora_sendAT(const char* cmd, char* response, int responseSize,
                        unsigned long timeoutMs = LORA_AT_TIMEOUT_MS) {
  // Clear input buffer
  while (LoRaSerial.available()) LoRaSerial.read();

  LoRaSerial.println(cmd);

  unsigned long start = millis();
  int idx = 0;
  bool lineComplete = false;

  while (millis() - start < timeoutMs && !lineComplete) {
    if (LoRaSerial.available()) {
      char c = LoRaSerial.read();
      if (c == '\n') {
        lineComplete = true;
      } else if (c != '\r' && idx < responseSize - 1) {
        response[idx++] = c;
      }
    }
  }
  response[idx] = '\0';

  return lineComplete && (strstr(response, "+OK") != NULL ||
                          strstr(response, "+RCV") != NULL);
}

// Send AT query and return first response line as-is.
static bool lora_queryAT(const char* cmd, char* response, int responseSize,
                         unsigned long timeoutMs = LORA_AT_TIMEOUT_MS) {
  while (LoRaSerial.available()) LoRaSerial.read();

  LoRaSerial.println(cmd);

  unsigned long start = millis();
  int idx = 0;
  bool lineComplete = false;

  while (millis() - start < timeoutMs && !lineComplete) {
    if (LoRaSerial.available()) {
      char c = LoRaSerial.read();
      if (c == '\n') {
        lineComplete = true;
      } else if (c != '\r' && idx < responseSize - 1) {
        response[idx++] = c;
      }
    }
  }
  response[idx] = '\0';

  return lineComplete && idx > 0;
}

static bool lora_parseParameterResponse(const char* response,
                                        int* sf, int* bw, int* cr, int* preamble) {
  if (!response || !sf || !bw || !cr || !preamble) return false;

  const char* p = strstr(response, "+PARAMETER=");
  if (!p) return false;

  return sscanf(p, "+PARAMETER=%d,%d,%d,%d", sf, bw, cr, preamble) == 4;
}

static bool lora_parseBandResponse(const char* response, unsigned long* bandHz) {
  if (!response || !bandHz) return false;

  const char* p = strstr(response, "+BAND=");
  if (!p) return false;

  unsigned long parsed = 0;
  if (sscanf(p, "+BAND=%lu", &parsed) != 1) return false;
  *bandHz = parsed;
  return true;
}

// ── Public API ──────────────────────────────────────────────────

bool lora_init(uint8_t address, uint8_t networkId) {
  LoRaSerial.begin(LORA_BAUD, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
  delay(100);   // Module boot time

  char response[64];
  char cmd[32];

  // Test connection
  if (!lora_sendAT("AT", response, sizeof(response))) {
    DEBUG_ERROR(F("LoRa: no response from RYLR890"));
    return false;
  }
  DEBUG_INFO(F("LoRa: RYLR890 responding"));

  // Set address
  snprintf(cmd, sizeof(cmd), "AT+ADDRESS=%d", address);
  if (!lora_sendAT(cmd, response, sizeof(response))) {
    DEBUG_ERROR(F("LoRa: failed to set address"));
    return false;
  }

  // Set network ID
  snprintf(cmd, sizeof(cmd), "AT+NETWORKID=%d", networkId);
  if (!lora_sendAT(cmd, response, sizeof(response))) {
    DEBUG_ERROR(F("LoRa: failed to set network ID"));
    return false;
  }

  // Set explicit RF frequency (Hz) for RYLR890.
  snprintf(cmd, sizeof(cmd), "AT+BAND=%lu", (unsigned long)LORA_RF_FREQUENCY_HZ);
  if (!lora_sendAT(cmd, response, sizeof(response))) {
    DEBUG_WARN(F("LoRa: AT+BAND rejected, keeping module frequency"));
  } else {
    unsigned long bandRead = 0;
    if (lora_queryAT("AT+BAND?", response, sizeof(response)) &&
        lora_parseBandResponse(response, &bandRead)) {
      if (bandRead != (unsigned long)LORA_RF_FREQUENCY_HZ) {
        DEBUG_PRINTF("[WARN]  LoRa BAND mismatch: expected %lu got %lu\n",
                     (unsigned long)LORA_RF_FREQUENCY_HZ, bandRead);
      } else {
        DEBUG_PRINTF("[INFO]  LoRa BAND verified: %lu\n", bandRead);
      }
    } else {
      DEBUG_WARN(F("LoRa: AT+BAND? readback unavailable"));
    }
  }

  // Apply shared RF contract so RYLR890 matches SX1262 profile.
  // RYLR890 format: AT+PARAMETER=<SF>,<BW>,<CR>,<PREAMBLE>
  snprintf(cmd, sizeof(cmd), "AT+PARAMETER=%d,%d,%d,%d",
           RYLR_PARAM_SF, RYLR_PARAM_BW, RYLR_PARAM_CR, RYLR_PARAM_PREAMBLE);
  if (!lora_sendAT(cmd, response, sizeof(response))) {
    DEBUG_WARN(F("LoRa: AT+PARAMETER rejected, keeping module RF defaults"));
  } else {
    int sfRead = 0;
    int bwRead = 0;
    int crRead = 0;
    int preambleRead = 0;

    if (lora_queryAT("AT+PARAMETER?", response, sizeof(response)) &&
        lora_parseParameterResponse(response, &sfRead, &bwRead, &crRead, &preambleRead)) {
      bool matches = (sfRead == RYLR_PARAM_SF) &&
                     (bwRead == RYLR_PARAM_BW) &&
                     (crRead == RYLR_PARAM_CR) &&
                     (preambleRead == RYLR_PARAM_PREAMBLE);

      if (!matches) {
        DEBUG_PRINTF("[WARN]  LoRa RF mismatch: expected %d,%d,%d,%d got %d,%d,%d,%d\n",
                     RYLR_PARAM_SF, RYLR_PARAM_BW, RYLR_PARAM_CR, RYLR_PARAM_PREAMBLE,
                     sfRead, bwRead, crRead, preambleRead);
      } else {
        DEBUG_PRINTF("[INFO]  LoRa RF verified: %d,%d,%d,%d\n",
                     sfRead, bwRead, crRead, preambleRead);
      }
    } else {
      DEBUG_WARN(F("LoRa: AT+PARAMETER? readback unavailable"));
    }
  }

  DEBUG_PRINTF("[INFO]  LoRa: address=%d, networkId=%d\n", address, networkId);
  DEBUG_PRINTF("[INFO]  LoRa RF: %.1fMHz BW=%dkHz SF=%d CR=4/%d SW=0x%02X PRE=%d\n",
               LORA_RF_FREQUENCY_MHZ,
               LORA_RF_BANDWIDTH_KHZ,
               LORA_RF_SPREADING_FACTOR,
               LORA_RF_CODING_RATE_DENOM,
               LORA_RF_SYNC_WORD,
               LORA_RF_PREAMBLE_LEN);
  g_loraReady = true;
  return true;
}

// Send a text message to target address
bool lora_send(uint8_t target, const char* message) {
  if (!g_loraReady) return false;

  int len = strlen(message);
  if (len > LORA_MAX_PAYLOAD) {
    DEBUG_WARN(F("LoRa: message too long, truncating"));
    len = LORA_MAX_PAYLOAD;
  }

  char cmd[LORA_MAX_PAYLOAD + 32];
  snprintf(cmd, sizeof(cmd), "AT+SEND=%d,%d,%s", target, len, message);

  char response[32];
  bool ok = lora_sendAT(cmd, response, sizeof(response), 5000);

  if (ok) {
    DEBUG_PRINTF("[VERB]  LoRa TX -> %d: %s\n", target, message);
  } else {
    DEBUG_ERRORF("LoRa TX failed -> %d\n", target);
  }
  return ok;
}

// Format and send sensor report
bool lora_sendReport(const SensorData* data, const SystemState* state,
                     const DeviceConfig* config) {
  char payload[LORA_MAX_PAYLOAD];
  lora_buildReportPayload(data, state, config, payload, sizeof(payload));

  return lora_send(LORA_TARGET_ADDRESS, payload);
}

// Check for incoming data — non-blocking
// Returns true if a command was received, fills cmdBuffer
// Format: +RCV=<addr>,<len>,<data>,<rssi>,<snr>
bool lora_checkIncoming(char* cmdBuffer, int cmdBufSize) {
  while (LoRaSerial.available()) {
    char c = LoRaSerial.read();

    if (c == '\n') {
      g_loraRxBuffer[g_loraRxIndex] = '\0';

      // Parse +RCV=addr,len,data,rssi,snr
      if (strncmp(g_loraRxBuffer, "+RCV=", 5) == 0) {
        char* p = g_loraRxBuffer + 5;

        // Skip address
        char* comma = strchr(p, ',');
        if (!comma) { g_loraRxIndex = 0; return false; }
        p = comma + 1;

        // Skip length
        comma = strchr(p, ',');
        if (!comma) { g_loraRxIndex = 0; return false; }
        p = comma + 1;

        // Extract data (ends at next comma)
        comma = strchr(p, ',');
        if (comma) {
          int dataLen = comma - p;
          if (dataLen >= cmdBufSize) dataLen = cmdBufSize - 1;
          strncpy(cmdBuffer, p, dataLen);
          cmdBuffer[dataLen] = '\0';

          // Parse RSSI
          p = comma + 1;
          g_loraLastRssi = atoi(p);

          // Parse SNR
          comma = strchr(p, ',');
          if (comma) {
            g_loraLastSnr = atoi(comma + 1);
          }

          g_loraRxIndex = 0;
          DEBUG_PRINTF("[INFO]  LoRa RX: %s (RSSI=%d, SNR=%d)\n",
                       cmdBuffer, g_loraLastRssi, g_loraLastSnr);
          return true;
        }
      }

      g_loraRxIndex = 0;
    } else if (c != '\r' && g_loraRxIndex < (int)sizeof(g_loraRxBuffer) - 1) {
      g_loraRxBuffer[g_loraRxIndex++] = c;
    }
  }
  return false;
}

int lora_getLastRssi() { return g_loraLastRssi; }
int lora_getLastSnr()  { return g_loraLastSnr; }
bool lora_isReady()    { return g_loraReady; }

#endif // !USE_XIAO_SX1262

#endif // ENABLE_LORA
#endif // LORA_COMM_H
