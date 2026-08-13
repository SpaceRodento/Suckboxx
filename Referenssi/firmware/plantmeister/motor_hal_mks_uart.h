/*=====================================================================
  motor_hal_mks_uart.h - MKS SERVO42C-MT UART protocol implementation

  Protokolla: TTL UART, 8N1, default 38400 baud, slave 0xE0.
  Kehys downlink: [addr][func][data...][crc8]
  Kehys uplink:   [addr][data...][crc8]      (HUOM: ei func uplinkissä)
  CRC8 = sum(all preceding bytes) & 0xFF
  Manuaali: docs/manuals/mks/MKS_SERVO42C_User_Manual_V1.1.2.pdf

  Komennot joita käytetään:
    0x30  read encoder carry+value     (lue absoluuttinen kulma)
    0x39  read shaft angle error       (closed-loop status)
    0x3A  read EN-status
    0x3E  read locked-rotor protection
    0xF3  enable/disable driver        (0x01=enable, 0x00=disable)
    0xF6  run constant speed           (bit7=dir, bit0..6=speed)
    0xF7  stop
    0xFD  run N pulses (relative)      [VAL][pulses uint32 BE]

  Tämä header on includetava VAIN motor_hal.h:sta kun
  MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART.

  Public API (header-only inline):
    mksu_init()          → bool: avaa Serial1, ei lähetä komentoja
    mksu_enable()        → ajaa F3 0x01
    mksu_disable()       → ajaa F3 0x00
    mksu_runPulses()     → ajaa FD VAL pulses (suhteellinen liike)
    mksu_runSpeed()      → ajaa F6 VAL (vakionopeus, dir-bitti)
    mksu_stop()          → ajaa F7
    mksu_readEncoder()   → 0x30 → palauttaa int64 (carry<<16 | value)
    mksu_isMoving()      → softa-side tracker (FD pulssikomennon vastauksien perusteella)
    mksu_poll()          → kutsutaan loop()ista, kerää uplink-tavut ja päivittää tilan
=====================================================================*/

#ifndef MOTOR_HAL_MKS_UART_H
#define MOTOR_HAL_MKS_UART_H

#include "config.h"
#include "structs.h"

#if ENABLE_MOTOR && (MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART)

#include <Arduino.h>

// ── Internal state ──────────────────────────────────────────────────

static bool          g_mksuReady       = false;
static bool          g_mksuMoving      = false;
static bool          g_mksuEnabled     = false;
static unsigned long g_mksuLastPollMs  = 0;
static int64_t       g_mksuEncoder     = 0;     // viimeisin onnistunut 0x30-luenta
static unsigned long g_mksuMoveStartMs = 0;

// Pieni RX-rengaspuskuri uplink-tavuille, jotta poll() voi koota kehyksiä
// loopin yli ilman blokkausta.
#define MKSU_RX_BUF_SIZE 32
static uint8_t  g_mksuRxBuf[MKSU_RX_BUF_SIZE];
static uint8_t  g_mksuRxLen = 0;

// ── Helpers ─────────────────────────────────────────────────────────

// CRC8 = (sum of bytes) & 0xFF
static inline uint8_t mksu_crc8(const uint8_t* buf, uint8_t len) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < len; i++) sum += buf[i];
  return (uint8_t)(sum & 0xFF);
}

// Lähetä kehys (addr + func + data + crc). Tyhjentää RX-puskurin ensin.
static inline void mksu_sendFrame(uint8_t func, const uint8_t* data, uint8_t dataLen) {
  uint8_t frame[16];
  if (dataLen > sizeof(frame) - 3) return;  // turvarajoite, ei pitäisi tapahtua
  frame[0] = (uint8_t)MKS_UART_SLAVE_ADDR;
  frame[1] = func;
  for (uint8_t i = 0; i < dataLen; i++) frame[2 + i] = data[i];
  frame[2 + dataLen] = mksu_crc8(frame, 2 + dataLen);

  // Tyhjennä rx ennen lähetystä, jotta seuraava vastaus ei sotke vanhojen kanssa.
  g_mksuRxLen = 0;
  while (Serial1.available()) Serial1.read();

  Serial1.write(frame, 3 + dataLen);
  Serial1.flush();
}

// Odota response yhden komennon ajan (blocking, lyhyt timeout).
// Palauttaa tavujen määrän jonka luki g_mksuRxBuf:iin.
// Käytä vain init-vaiheessa ja diagnostiikkaan — loop() käyttää mksu_poll().
static inline uint8_t mksu_waitResponse(uint8_t expectedLen, unsigned long timeoutMs) {
  unsigned long deadline = millis() + timeoutMs;
  g_mksuRxLen = 0;
  while (millis() < deadline && g_mksuRxLen < expectedLen) {
    while (Serial1.available() && g_mksuRxLen < MKSU_RX_BUF_SIZE) {
      g_mksuRxBuf[g_mksuRxLen++] = (uint8_t)Serial1.read();
    }
  }
  return g_mksuRxLen;
}

// Validoi uplink-kehys: addr + data... + crc8.
// Palauttaa true jos crc täsmää ja addr on odotettu.
static inline bool mksu_validateFrame(const uint8_t* buf, uint8_t len) {
  if (len < 3) return false;
  if (buf[0] != (uint8_t)MKS_UART_SLAVE_ADDR) return false;
  uint8_t crc = mksu_crc8(buf, len - 1);
  return crc == buf[len - 1];
}

// ── Public API ─────────────────────────────────────────────────────

static inline bool mksu_init() {
  Serial1.begin(MKS_UART_BAUD, SERIAL_8N1, PIN_MKS_UART_RX, PIN_MKS_UART_TX);
  // Ei sondaa moottoria initissa: 42C-MT ei välttämättä ole CR_UART-tilassa
  // (tehdasoletus on CR_vFOC). Käyttäjän on konfiguroitava OLED:sta.
  // motor_isReady() palauttaa silti true, koska väylä on auki ja komennot
  // lähtevät. Jos moottori ei vastaa, näkyy lokissa.
  g_mksuReady = true;
  g_mksuEnabled = false;
  g_mksuMoving = false;
  g_mksuEncoder = 0;
  g_mksuLastPollMs = 0;
  g_mksuRxLen = 0;
  return true;
}

static inline void mksu_enable() {
  uint8_t data[1] = { 0x01 };
  mksu_sendFrame(0xF3, data, 1);
  g_mksuEnabled = true;
}

static inline void mksu_disable() {
  uint8_t data[1] = { 0x00 };
  mksu_sendFrame(0xF3, data, 1);
  g_mksuEnabled = false;
  g_mksuMoving = false;
}

static inline void mksu_stop() {
  mksu_sendFrame(0xF7, NULL, 0);
  g_mksuMoving = false;
}

// Aja vakionopeudella. dir: false=CW, true=CCW. speed: 0..127.
static inline void mksu_runSpeed(bool reverse, uint8_t speed) {
  if (!g_mksuEnabled) mksu_enable();
  if (speed > 0x7F) speed = 0x7F;
  uint8_t val = (reverse ? 0x80 : 0x00) | (speed & 0x7F);
  uint8_t data[1] = { val };
  mksu_sendFrame(0xF6, data, 1);
  g_mksuMoving = (speed > 0);
  g_mksuMoveStartMs = millis();
}

// Aja N pulssia (suhteellinen liike). pulses on int32, negatiivinen → reverse.
// Manuaali: F6 VAL [pulses uint32 BE], joten otetaan |pulses| ja merkki bit7:ään.
static inline void mksu_runPulses(int32_t pulses, uint8_t speed) {
  if (!g_mksuEnabled) mksu_enable();
  bool reverse = (pulses < 0);
  uint32_t p = (uint32_t)(reverse ? -pulses : pulses);
  if (speed > 0x7F) speed = 0x7F;
  uint8_t val = (reverse ? 0x80 : 0x00) | (speed & 0x7F);
  uint8_t data[5];
  data[0] = val;
  data[1] = (uint8_t)((p >> 24) & 0xFF);  // BE: MSB first
  data[2] = (uint8_t)((p >> 16) & 0xFF);
  data[3] = (uint8_t)((p >>  8) & 0xFF);
  data[4] = (uint8_t)( p        & 0xFF);
  mksu_sendFrame(0xFD, data, 5);
  g_mksuMoving = (p > 0);
  g_mksuMoveStartMs = millis();
}

// Lue encoder-asema (0x30). Palauttaa true ja täyttää outEncoder:n jos onnistui.
// Blocking-luenta lyhyellä timeoutilla — käytä vain initissa tai erikseen,
// ei joka loop-kierroksella (loop käyttää mksu_poll()).
static inline bool mksu_readEncoder(int64_t* outEncoder) {
  mksu_sendFrame(0x30, NULL, 0);
  // Odotettu vastaus: addr (1) + carry int32 BE (4) + value uint16 BE (2) + crc (1) = 8 tavua
  uint8_t got = mksu_waitResponse(8, MKS_UART_TIMEOUT_MS);
  if (got < 8) return false;
  if (!mksu_validateFrame(g_mksuRxBuf, 8)) return false;
  // carry on signed int32 BE
  int32_t carry =
    ((int32_t)g_mksuRxBuf[1] << 24) |
    ((int32_t)g_mksuRxBuf[2] << 16) |
    ((int32_t)g_mksuRxBuf[3] <<  8) |
    ((int32_t)g_mksuRxBuf[4]);
  uint16_t value = ((uint16_t)g_mksuRxBuf[5] << 8) | g_mksuRxBuf[6];
  // Kombinoi: täysi 48-bittinen asema = carry * 0x4000 + value.
  // Manuaali: yksi kierros = 0x4000 yksikköä (14-bit raw encoder).
  int64_t enc = (int64_t)carry * (int64_t)0x4000 + (int64_t)value;
  *outEncoder = enc;
  g_mksuEncoder = enc;
  return true;
}

// Non-blocking poll: kerää uplink-tavut, käsittelee F6/FD-vastaukset (status 01/02).
// Päivittää g_mksuMoving kun status=02 (run complete) tulee.
static inline void mksu_poll() {
  while (Serial1.available()) {
    if (g_mksuRxLen >= MKSU_RX_BUF_SIZE) {
      // Puskuri täynnä — pudota vanhin, jotta tahdistuminen toipuu.
      for (uint8_t i = 0; i + 1 < g_mksuRxLen; i++) g_mksuRxBuf[i] = g_mksuRxBuf[i + 1];
      g_mksuRxLen--;
    }
    g_mksuRxBuf[g_mksuRxLen++] = (uint8_t)Serial1.read();
  }

  // F6/FD-vastaus on lyhyt 3-tavuinen: [addr][status][crc]
  // status: 0x01 = käynnistetty, 0x02 = valmis, 0x00 = epäonnistui
  while (g_mksuRxLen >= 3) {
    if (g_mksuRxBuf[0] != (uint8_t)MKS_UART_SLAVE_ADDR) {
      // Synkronointi: pudota tavu kunnes addr matchaa
      for (uint8_t i = 0; i + 1 < g_mksuRxLen; i++) g_mksuRxBuf[i] = g_mksuRxBuf[i + 1];
      g_mksuRxLen--;
      continue;
    }
    // Yritä 3-tavuisena ensin (F6/FD/F7/F3 status response)
    if (mksu_validateFrame(g_mksuRxBuf, 3)) {
      uint8_t status = g_mksuRxBuf[1];
      if (status == 0x02) {
        // Run complete
        g_mksuMoving = false;
      } else if (status == 0x01) {
        // Run starting — pidä moving=true
        g_mksuMoving = true;
      }
      // Kuluta käsitelty kehys
      for (uint8_t i = 3; i < g_mksuRxLen; i++) g_mksuRxBuf[i - 3] = g_mksuRxBuf[i];
      g_mksuRxLen -= 3;
      continue;
    }
    // 3-tavuinen ei validinen → odota lisää tavuja tai pudota header
    if (g_mksuRxLen < 4) break;
    // Yritä 4-tavuisena (esim. 0x39 shaft angle error: addr + int16 BE + crc)
    if (mksu_validateFrame(g_mksuRxBuf, 4)) {
      for (uint8_t i = 4; i < g_mksuRxLen; i++) g_mksuRxBuf[i - 4] = g_mksuRxBuf[i];
      g_mksuRxLen -= 4;
      continue;
    }
    // Ei validi → pudota header ja yritä synkronoida uudelleen
    for (uint8_t i = 1; i < g_mksuRxLen; i++) g_mksuRxBuf[i - 1] = g_mksuRxBuf[i];
    g_mksuRxLen--;
  }
}

static inline bool mksu_isReady()    { return g_mksuReady; }
static inline bool mksu_isMoving()   { return g_mksuMoving; }
static inline bool mksu_isEnabled()  { return g_mksuEnabled; }
static inline int64_t mksu_getCachedEncoder() { return g_mksuEncoder; }

// Käytetään motor_hal.h:n mm→steps-konversiossa. 42C-MT:n encoder antaa
// 0x4000 (16384) yksikköä/kierros, joten "yksi step" softan tasolla =
// 200 stepin koko revoluutiosta * MKS_UART_MICROSTEPS microstepiä.
// Tätä käytetään motor_hal:n yleisissä helperissä — header tarjoaa
// vakion, ei laske itse mm-puolelle.
#define MKSU_STEPS_PER_REV  ((long)MOTOR_STEPS_PER_REV * (long)MKS_UART_MICROSTEPS)

#endif // ENABLE_MOTOR && MKS_SERVO42C_UART
#endif // MOTOR_HAL_MKS_UART_H
