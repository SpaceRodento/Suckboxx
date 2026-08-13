/*=====================================================================
  eink_dev_view.h - Pure helpers backing the e-ink developer dashboard.

  This header is intentionally hardware-independent: the actual rendering
  lives in firmware/e-ink-seinataulu/dev_view.h, which is built on top of
  GxEPD2. Here we provide the data shape and formatting helpers that the
  plantmeister firmware uses when publishing developer-facing fields via
  /api/state, plus pure functions that can be unit-tested natively.

  Why split this way:
    - The e-ink wall display is a separate ESP32 module fetching from the
      gateway/plantmeister over HTTP, so renderer code is not linked into
      the main firmware.
    - But the *contract* (which fields exist, how they format) belongs
      next to the firmware so any future inline e-ink driver can reuse
      it without duplicating logic.
=====================================================================*/

#ifndef EINK_DEV_VIEW_H
#define EINK_DEV_VIEW_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct EinkDevFields {
  int       deviceState;       // numeric DeviceState (-1 = unknown)
  uint8_t   faultBits;         // DEVICE_FAULT_* bitmask
  uint32_t  freeHeapBytes;
  uint32_t  uptimeSec;
  int       wifiRssiDbm;       // 0 = unknown
  int       loraRssiDbm;       // 0 = unknown
  int       schedulerTicks;    // 0 if not tracked
  char      lastIntent[20];
  char      lastIntentSource[12];
};

// Map numeric DeviceState to short label (4 chars max). This switch's own
// numbering does not match device_state.h's current enum ordering (INIT=0,
// SELF_TEST, IDLE, GROWING, FAULT, SHUTDOWN, MAINTENANCE post-D14) — that
// mismatch predates D14 and is tracked separately; this dev-view diagnostics
// screen is out of scope here (docs/kehitys/m0.1-toteutussuunnitelma.md § 2).
inline const char* eink_dev_stateLabel(int s) {
  switch (s) {
    case 0:  return "BOOT";
    case 1:  return "SELF";
    case 2:  return "IDLE";
    case 3:  return "WAIT";
    case 4:  return "GROW";
    case 5:  return "PAUS";
    case 6:  return "FAULT";
    case 7:  return "SHDN";
    case 8:  return "TEST";
    default: return "?";
  }
}

// Format uptime as "<h>h <mm>m" for >=1h, else "<m>m <ss>s".
// Returns number of chars written (excluding null), capped to bufLen-1.
inline size_t eink_dev_formatUptime(uint32_t s, char* out, size_t bufLen) {
  if (!out || bufLen == 0) return 0;
  int n;
  if (s >= 3600) {
    uint32_t h = s / 3600;
    uint32_t m = (s % 3600) / 60;
    n = snprintf(out, bufLen, "%uh %02um", (unsigned)h, (unsigned)m);
  } else {
    uint32_t m = s / 60;
    uint32_t sec = s % 60;
    n = snprintf(out, bufLen, "%um %02us", (unsigned)m, (unsigned)sec);
  }
  if (n < 0) {
    if (bufLen > 0) out[0] = '\0';
    return 0;
  }
  return (size_t)n < bufLen ? (size_t)n : bufLen - 1;
}

// Format heap as "<kB>" units. Returns chars written.
inline size_t eink_dev_formatHeap(uint32_t bytes, char* out, size_t bufLen) {
  if (!out || bufLen == 0) return 0;
  int n = snprintf(out, bufLen, "%lu kB", (unsigned long)(bytes / 1024UL));
  if (n < 0) {
    if (bufLen > 0) out[0] = '\0';
    return 0;
  }
  return (size_t)n < bufLen ? (size_t)n : bufLen - 1;
}

// Format fault bitmask as 8-bit hex, always 4 chars: "0xNN" + null.
inline size_t eink_dev_formatFaults(uint8_t bits, char* out, size_t bufLen) {
  if (!out || bufLen == 0) return 0;
  int n = snprintf(out, bufLen, "0x%02X", bits);
  if (n < 0) {
    if (bufLen > 0) out[0] = '\0';
    return 0;
  }
  return (size_t)n < bufLen ? (size_t)n : bufLen - 1;
}

// Compose the bottom-line "Last intent" string. Falls back gracefully
// when fields are missing.
inline size_t eink_dev_formatLastIntent(const char* intent,
                                        const char* source,
                                        char* out, size_t bufLen) {
  if (!out || bufLen == 0) return 0;
  if (!intent || intent[0] == '\0') {
    int n = snprintf(out, bufLen, "Last intent: (none reported)");
    return (n > 0 && (size_t)n < bufLen) ? (size_t)n : bufLen - 1;
  }
  const char* src = (source && source[0]) ? source : "?";
  int n = snprintf(out, bufLen, "Last intent: %s  src=%s", intent, src);
  return (n > 0 && (size_t)n < bufLen) ? (size_t)n : bufLen - 1;
}

#endif // EINK_DEV_VIEW_H
