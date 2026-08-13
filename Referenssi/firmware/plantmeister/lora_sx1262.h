/*=====================================================================
  lora_sx1262.h - LoRa Communication (SX1262 via RadioLib)

  Used when USE_XIAO_SX1262 = true (XIAO ESP32S3 + Wio-SX1262).
  Included by lora_comm.h — do NOT include directly.

  Provides identical public API as the RYLR890 AT-command implementation:
    lora_init(), lora_send(), lora_sendReport(),
    lora_checkIncoming(), lora_parseCommand(),
    lora_getLastRssi(), lora_getLastSnr(), lora_isReady()

  On-air message format (same as RYLR890 data payload):
    Outgoing: KA:T=22.5,H=65,P=1013,...,L=1,AP=1,BAT=4.1,BP=85
    Incoming: CMD:LIGHT=1  CMD:WATER=50  CMD:REBOOT  etc.

  LoRa settings — must match RYLR890 / RPi receiver:
    868.0 MHz, BW 125 kHz, SF 10, CR 4/5, sync 0x34, preamble 4
=====================================================================*/

#ifndef LORA_SX1262_H
#define LORA_SX1262_H

#include <SPI.h>
#include <RadioLib.h>

#include "config.h"
#include "structs.h"
#include "lora_parser.h"
#include "lora_payload.h"

// ── Radio parameters — must match RYLR890 defaults ───────────────
#ifndef SX1262_FREQUENCY
  #define SX1262_FREQUENCY        LORA_RF_FREQUENCY_MHZ
#endif
#ifndef SX1262_BANDWIDTH
  #define SX1262_BANDWIDTH        ((float)LORA_RF_BANDWIDTH_KHZ)
#endif
#ifndef SX1262_SPREADING_FACTOR
  #define SX1262_SPREADING_FACTOR LORA_RF_SPREADING_FACTOR
#endif
#ifndef SX1262_CODING_RATE
  #define SX1262_CODING_RATE      LORA_RF_CODING_RATE_DENOM
#endif
#ifndef SX1262_SYNC_WORD
  #define SX1262_SYNC_WORD        LORA_RF_SYNC_WORD
#endif
#ifndef SX1262_TX_POWER
  #define SX1262_TX_POWER         22        // dBm (max for SX1262)
#endif
#ifndef SX1262_PREAMBLE_LENGTH
  #define SX1262_PREAMBLE_LENGTH  LORA_RF_PREAMBLE_LEN
#endif
#ifndef SX1262_TCXO_VOLTAGE
  #define SX1262_TCXO_VOLTAGE     2.4       // Wio-SX1262 uses TCXO
#endif

// ── Global state ─────────────────────────────────────────────────

// SX1262 radio object — pin order: NSS, DIO1, RESET, BUSY
static SX1262 g_sx1262Radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1,
                                          PIN_LORA_RESET, PIN_LORA_BUSY);

static bool g_sx1262Ready       = false;
static int  g_sx1262LastRssi    = 0;
static int  g_sx1262LastSnr     = 0;
static uint8_t g_sx1262Address  = 0;

// Interrupt flag — set from ISR when packet arrives on DIO1
static volatile bool g_sx1262RxFlag = false;

// ── Interrupt service routine ─────────────────────────────────────

void IRAM_ATTR sx1262_onDio1() {
  g_sx1262RxFlag = true;
}

// ── Public API ───────────────────────────────────────────────────

bool lora_init(uint8_t address, uint8_t networkId) {
  g_sx1262Address = address;

  // SPI on Wio-SX1262 B2B internal pins
  SPI.begin(PIN_LORA_SCLK, PIN_LORA_MISO, PIN_LORA_MOSI);

  // Manual reset
  pinMode(PIN_LORA_RESET, OUTPUT);
  digitalWrite(PIN_LORA_RESET, LOW);
  delay(10);
  digitalWrite(PIN_LORA_RESET, HIGH);
  delay(20);

  int state = g_sx1262Radio.begin(
    SX1262_FREQUENCY,
    SX1262_BANDWIDTH,
    SX1262_SPREADING_FACTOR,
    SX1262_CODING_RATE,
    SX1262_SYNC_WORD,
    SX1262_TX_POWER,
    SX1262_PREAMBLE_LENGTH,
    SX1262_TCXO_VOLTAGE
  );

  if (state != RADIOLIB_ERR_NONE) {
    DEBUG_ERRORF("SX1262 init failed: %d\n", state);
    return false;
  }

  // Required for Wio-SX1262: DIO2 controls RF switch
  g_sx1262Radio.setDio2AsRfSwitch(true);

  // Optimal current limit for SX1262
  g_sx1262Radio.setCurrentLimit(140.0);

  // Attach DIO1 interrupt and start receiving
  g_sx1262Radio.setDio1Action(sx1262_onDio1);
  g_sx1262Radio.startReceive();

  g_sx1262Ready = true;
  DEBUG_PRINTF("[INFO]  SX1262: ready, addr=%d, net=%d, %.1fMHz SF%d\n",
               address, networkId, SX1262_FREQUENCY, SX1262_SPREADING_FACTOR);
  return true;
}

bool lora_send(uint8_t target, const char* message) {
  if (!g_sx1262Ready) return false;
  (void)target;   // SX1262 broadcasts — addressing done at application layer if needed

  int state = g_sx1262Radio.transmit(message);

  // After TX, re-arm interrupt and restart receive
  g_sx1262RxFlag = false;
  g_sx1262Radio.startReceive();

  if (state == RADIOLIB_ERR_NONE) {
    DEBUG_PRINTF("[VERB]  SX1262 TX: %s\n", message);
    return true;
  }
  DEBUG_ERRORF("SX1262 TX failed: %d\n", state);
  return false;
}

// Format and send sensor report — identical payload as RYLR890 version
bool lora_sendReport(const SensorData* data, const SystemState* state,
                     const DeviceConfig* config) {
  char payload[LORA_MAX_PAYLOAD];
  lora_buildReportPayload(data, state, config, payload, sizeof(payload));

  return lora_send(LORA_TARGET_ADDRESS, payload);
}

// Check for incoming data — non-blocking, driven by DIO1 interrupt
bool lora_checkIncoming(char* cmdBuffer, int cmdBufSize) {
  if (!g_sx1262Ready || !g_sx1262RxFlag) return false;
  g_sx1262RxFlag = false;

  String received;
  int state = g_sx1262Radio.readData(received);

  // Re-arm for next packet
  g_sx1262Radio.startReceive();

  if (state != RADIOLIB_ERR_NONE || received.length() == 0) return false;

  g_sx1262LastRssi = (int)g_sx1262Radio.getRSSI();
  g_sx1262LastSnr  = (int)g_sx1262Radio.getSNR();

  int len = received.length();
  if (len >= cmdBufSize) len = cmdBufSize - 1;
  received.toCharArray(cmdBuffer, len + 1);
  cmdBuffer[len] = '\0';

  DEBUG_PRINTF("[INFO]  SX1262 RX: %s (RSSI=%d, SNR=%d)\n",
               cmdBuffer, g_sx1262LastRssi, g_sx1262LastSnr);
  return true;
}

// lora_parseCommand() comes from lora_parser.h (shared with the RYLR890
// path, natively unit-tested in test_lora_parser/) - previously this file
// carried its own copy that had drifted to lack the null/size guards the
// shared version has (found 4.7.2026 while looking for test coverage here).

int  lora_getLastRssi() { return g_sx1262LastRssi; }
int  lora_getLastSnr()  { return g_sx1262LastSnr; }
bool lora_isReady()     { return g_sx1262Ready; }

#endif // LORA_SX1262_H
