/*=====================================================================
  sensor_sticky.h - hold the last good sample while a sensor skips a beat

  Problem this solves (found 29.7.2026):
    sensors_read() clears every *Valid flag at the top of the cycle and
    only re-raises it if the driver produced a reading on THAT tick. A
    single skipped sample therefore looks identical to a dead sensor:
    envValid drops, and with it vpdValid, air temp and humidity all at
    once. The user saw VPD vanish from the wall panel at random.

    SCD41 is the concrete case. It samples every 5 s in periodic mode
    while sensors_read() runs every 30 s (growing) / 60 s (idle). Those
    periods are commensurate, so the poll can phase-lock just ahead of
    the sensor's conversion and miss several cycles in a row — not the
    single harmless glitch the naive reading suggests.

  Why the fix belongs here and not in the display:
    e-ink already had a sticky layer (EINK_STICKY_MAX_MISSES). It works,
    but it makes the panel paper over the device reporting something
    untrue, and every OTHER consumer — history buffer, /api/state, the
    scheduler — still sees the hole. Holding the value at the source
    fixes all of them at once. The e-ink layer stays as a second line of
    defence against network loss, which is a different failure.

  Design: this is a GATE, not a value store. Fields that must move
  together (air temp + humidity feed one VPD; restoring one without the
  other would compute a nonsense number) are restored by the caller as a
  group once the gate says the cached sample is still young enough.

  Age is tracked in milliseconds rather than a miss COUNT so the grace
  period does not silently change when a read interval is retuned.
=====================================================================*/

#ifndef SENSOR_STICKY_H
#define SENSOR_STICKY_H

#include <stdint.h>
#include <stdbool.h>

// Bookkeeping for one group of fields that succeed or fail together.
typedef struct {
  uint32_t lastGoodMs;   // millis() of the most recent successful read
  bool     haveSample;   // false until the first success — never invent data
} StickyState;

static inline void sticky_reset(StickyState* s) {
  s->lastGoodMs = 0;
  s->haveSample = false;
}

// Record that a fresh reading arrived. Caller caches the values themselves.
static inline void sticky_markGood(StickyState* s, uint32_t now) {
  s->lastGoodMs = now;
  s->haveSample = true;
}

// Age of the cached sample. UINT32_MAX when there has never been one, so
// callers can print/compare it without a separate "unknown" branch.
// Unsigned subtraction makes the 32-bit millis() rollover (~49.7 d) work.
static inline uint32_t sticky_ageMs(const StickyState* s, uint32_t now) {
  if (!s->haveSample) return UINT32_MAX;
  return (uint32_t)(now - s->lastGoodMs);
}

// True when the caller should restore its cached values for this cycle.
// Deliberately false when no sample was ever taken: a boot with the sensor
// missing must show "--", not a fabricated zero.
static inline bool sticky_shouldReuse(const StickyState* s, uint32_t now,
                                      uint32_t maxAgeMs) {
  if (!s->haveSample) return false;
  return sticky_ageMs(s, now) < maxAgeMs;
}

#endif // SENSOR_STICKY_H
