/*=====================================================================
  wireless_log.h - UDP broadcast log sink

  Mirror-sink: logit menevät sekä Serialiin että UDP broadcastiin
  (255.255.255.255:WIRELESS_LOG_PORT). Mahdollistaa langattoman
  serial-monitorin kun XIAO on protoboardilla eikä USB-johtoa
  saa kiinni.

  Ennen WiFi-yhteyttä viestit kerätään ring-bufferiin ja flushataan
  kun yhteys nousee. Jos buffer täyttyy ennen yhteyttä, vanhin
  viesti dropataan (head-overwrite).

  Kuuntelu PC:n päässä: scripts/wireless_log_listener.py
=====================================================================*/

#ifndef WIRELESS_LOG_H
#define WIRELESS_LOG_H

#include "config.h"

#if ENABLE_WIRELESS_LOG && !defined(UNIT_TEST)
  #include <WiFi.h>
  #include <WiFiUdp.h>
#endif

// ── Ring-buffer (header-only, testattava natiivisti) ────────────────
//
// Vain raaka tavuvirta. Linjojen eheys (newline) on kutsujan vastuulla.
// Overflow-strategia: drop-oldest, jotta uusin loki on aina sisällä.

#ifndef WIRELESS_LOG_BUFFER_SIZE
  #define WIRELESS_LOG_BUFFER_SIZE 2048
#endif

#ifndef WIRELESS_LOG_PORT
  #define WIRELESS_LOG_PORT 14533
#endif

struct WirelessLogRing {
  uint8_t  data[WIRELESS_LOG_BUFFER_SIZE];
  uint16_t head;     // luku-indeksi
  uint16_t tail;     // kirjoitus-indeksi
  uint16_t count;    // bytes in buffer
  uint32_t dropped;  // overwritten bytes (diagnostiikkaa)
};

static WirelessLogRing g_wlog_ring = { {0}, 0, 0, 0, 0 };

inline void wlog_ring_reset() {
  g_wlog_ring.head = 0;
  g_wlog_ring.tail = 0;
  g_wlog_ring.count = 0;
  g_wlog_ring.dropped = 0;
}

inline void wlog_ring_pushByte(uint8_t b) {
  if (g_wlog_ring.count == WIRELESS_LOG_BUFFER_SIZE) {
    // Drop oldest
    g_wlog_ring.head = (uint16_t)((g_wlog_ring.head + 1) % WIRELESS_LOG_BUFFER_SIZE);
    g_wlog_ring.count--;
    g_wlog_ring.dropped++;
  }
  g_wlog_ring.data[g_wlog_ring.tail] = b;
  g_wlog_ring.tail = (uint16_t)((g_wlog_ring.tail + 1) % WIRELESS_LOG_BUFFER_SIZE);
  g_wlog_ring.count++;
}

inline void wlog_ring_pushBytes(const uint8_t* src, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) wlog_ring_pushByte(src[i]);
}

inline uint16_t wlog_ring_count() { return g_wlog_ring.count; }
inline uint32_t wlog_ring_dropped() { return g_wlog_ring.dropped; }

// Pop up to `maxLen` bytes into `dst`. Returns number popped.
inline uint16_t wlog_ring_pop(uint8_t* dst, uint16_t maxLen) {
  uint16_t n = 0;
  while (n < maxLen && g_wlog_ring.count > 0) {
    dst[n++] = g_wlog_ring.data[g_wlog_ring.head];
    g_wlog_ring.head = (uint16_t)((g_wlog_ring.head + 1) % WIRELESS_LOG_BUFFER_SIZE);
    g_wlog_ring.count--;
  }
  return n;
}

// ── Transport (WiFiUDP) ─────────────────────────────────────────────

#if ENABLE_WIRELESS_LOG && !defined(UNIT_TEST)
static WiFiUDP g_wlog_udp;
static bool    g_wlog_initialized = false;

inline bool wlog_wifiReady() {
  return WiFi.status() == WL_CONNECTED;
}

inline void wlog_sendChunk(const uint8_t* data, uint16_t len) {
  if (!g_wlog_initialized || !wlog_wifiReady() || len == 0) return;
  // Broadcast → mikä tahansa kuuntelija subnetissä saa.
  g_wlog_udp.beginPacket(IPAddress(255, 255, 255, 255), WIRELESS_LOG_PORT);
  g_wlog_udp.write(data, len);
  g_wlog_udp.endPacket();
}

inline void wlog_flushBuffered() {
  if (!wlog_wifiReady()) return;
  uint8_t chunk[256];
  while (wlog_ring_count() > 0) {
    uint16_t n = wlog_ring_pop(chunk, sizeof(chunk));
    wlog_sendChunk(chunk, n);
  }
}
#endif

// ── Public API ──────────────────────────────────────────────────────

inline void wlog_init() {
#if ENABLE_WIRELESS_LOG && !defined(UNIT_TEST)
  g_wlog_udp.begin(WIRELESS_LOG_PORT);
  g_wlog_initialized = true;
#endif
}

inline void wlog_write(const char* s) {
  if (!s) return;
  uint16_t len = 0;
  while (s[len] && len < 1024) len++;  // safety cap per call
  // Aina bufferiin: wlog_loop() hoitaa UDP-flushauksen seuraavalla loop-tickillä.
  // Synkroninen UDP-lähetys täältä blokkasi loopin ja häiritsi USB-CDC-monitoria.
  wlog_ring_pushBytes((const uint8_t*)s, len);
}

inline void wlog_writeln(const char* s) {
  wlog_write(s);
  wlog_write("\n");
}

// Pump-funktio: kutsutaan main loopissa jotta puskuroidut viestit
// flushataan heti kun WiFi nousee. Halpa kun puskuri tyhjä / WiFi alhaalla.
inline void wlog_loop() {
#if ENABLE_WIRELESS_LOG && !defined(UNIT_TEST)
  if (g_wlog_initialized && wlog_ring_count() > 0 && wlog_wifiReady()) {
    wlog_flushBuffered();
  }
#endif
}

#endif // WIRELESS_LOG_H
