/*=====================================================================
  as7341_autorange.h - Pure gain auto-ranging decision for AS7341

  The AS7341's usable range is set by gain × integration time, and the
  ADC saturates at (ATIME+1)×(ASTEP+1) counts — NOT at the 65535 register
  width. With the production settings (ATIME 49, ASTEP 999, gain 16x) the
  ceiling is 50 000 counts, which corresponds to roughly 98 umol/m2/s in
  daylight: LESS THAN THE SEEDLING TARGET OF 150. The device could never
  report "enough light" outdoors no matter how bright it got, and the
  coaching loop was structurally broken (measured 29.7.2026, balcony:
  f5 at 79 % of ceiling in what was merely bright shade).

  A single fixed gain cannot cover both a 120 umol grow lamp and full sun
  (~2000 umol) — that is a 16x span before headroom. So the gain moves at
  runtime and light_calc.h normalises it away.

  This header holds ONLY the decision (natively tested in
  test_as7341_autorange); the I2C writes live in sensor_driver_as7341.h.

  Dependency: <stdint.h>. No Arduino, no Adafruit library.
=====================================================================*/

#ifndef AS7341_AUTORANGE_H
#define AS7341_AUTORANGE_H

#include <stdint.h>

// Gain index domain, matching Adafruit's as7341_gain_t ordering so the index
// can be passed straight to setGain():
//   0 = 0.5x, 1 = 1x, 2 = 2x, 3 = 4x, ... 10 = 512x
#define AS7341_GAIN_IDX_MIN   0
#define AS7341_GAIN_IDX_MAX  10

// Step down when the brightest channel reaches this share of the ADC ceiling.
// 85 % leaves room for the light to rise between two reads (~5 min apart)
// without clipping the sample that triggers the correction.
#define AS7341_AUTORANGE_HIGH_PCT  85

// Step up when the brightest channel falls below this share. One gain step is
// 2x, so a down-step from 85 % lands at ~42 % and an up-step from 6 % lands at
// ~12 % — both far from either threshold, so the gain cannot oscillate.
#define AS7341_AUTORANGE_LOW_PCT    6

// Multiplier for a gain index. Index 0 is the odd one out (0.5x); every
// other index is 2^(index-1).
static inline float as7341_gainMultiplier(uint8_t idx) {
  if (idx == 0) return 0.5f;
  if (idx > AS7341_GAIN_IDX_MAX) idx = AS7341_GAIN_IDX_MAX;
  return (float)(1UL << (idx - 1));
}

// ADC saturation count for the configured integration timing. The AS7341
// accumulates (ATIME+1)×(ASTEP+1) and cannot exceed the 16-bit register.
static inline uint32_t as7341_adcCeiling(uint16_t atime, uint16_t astep) {
  uint32_t ceiling = ((uint32_t)atime + 1UL) * ((uint32_t)astep + 1UL);
  return (ceiling > 65535UL) ? 65535UL : ceiling;
}

// Largest of the eight PAR channels — the one that clips first and therefore
// the only one that matters for ranging. Clear/NIR are excluded: they are not
// part of the PPFD sum, and letting them drive the gain would sacrifice PAR
// resolution to an unused channel.
static inline uint16_t as7341_maxParChannel(const uint16_t* channels8) {
  if (channels8 == 0) return 0;
  uint16_t maxV = 0;
  for (int i = 0; i < 8; i++) {
    if (channels8[i] > maxV) maxV = channels8[i];
  }
  return maxV;
}

// as7341_autorangeNextGain — decide the gain index for the NEXT read.
//
// currentIdx - gain index the sample was taken at.
// maxCount   - brightest PAR channel in that sample.
// adcCeiling - from as7341_adcCeiling().
//
// Returns the index to use next; equal to currentIdx when no change is due.
// Moves ONE step at a time: a single step is 2x, and the sensor is read every
// few minutes, so converging over a few samples is fast enough while keeping
// every sample interpretable (a big jump would make the intermediate readings
// hard to reconcile in the DLI integral).
static inline uint8_t as7341_autorangeNextGain(uint8_t currentIdx,
                                               uint16_t maxCount,
                                               uint32_t adcCeiling) {
  if (adcCeiling == 0) return currentIdx;
  if (currentIdx > AS7341_GAIN_IDX_MAX) currentIdx = AS7341_GAIN_IDX_MAX;

  const uint32_t highMark = (adcCeiling * AS7341_AUTORANGE_HIGH_PCT) / 100UL;
  const uint32_t lowMark  = (adcCeiling * AS7341_AUTORANGE_LOW_PCT) / 100UL;

  if ((uint32_t)maxCount >= highMark && currentIdx > AS7341_GAIN_IDX_MIN) {
    return (uint8_t)(currentIdx - 1);
  }
  if ((uint32_t)maxCount <= lowMark && currentIdx < AS7341_GAIN_IDX_MAX) {
    return (uint8_t)(currentIdx + 1);
  }
  return currentIdx;
}

// True when the sample is clipped and its PPFD is therefore a FLOOR, not a
// measurement. The consumer must not present a clipped value as fact — the
// display would show a plateau that reads as "the light stopped rising".
static inline bool as7341_isSaturated(uint16_t maxCount, uint32_t adcCeiling) {
  if (adcCeiling == 0) return false;
  return (uint32_t)maxCount >= adcCeiling;
}

#endif // AS7341_AUTORANGE_H
