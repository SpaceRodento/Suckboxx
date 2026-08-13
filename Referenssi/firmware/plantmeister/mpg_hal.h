/*=====================================================================
  mpg_hal.h - Manual Pulse Generator (handwheel encoder) HAL

  Device: Micronor MR 190.7 — RS422, 100 PPR, 5V (via level shifter)
  Pins:   PIN_MPG_A (GPIO 32), PIN_MPG_B (GPIO 33)

  Reads quadrature encoder and accumulates delta mm for motor control.
  Call mpg_update() from loop(). Use mpg_consumeDelta() to get pending
  movement and reset accumulator.

  Enable with ENABLE_MPG true in config.h.
  Tune MPG_MM_PER_DETENT for sensitivity (default 2 mm/click).
=====================================================================*/

#ifndef MPG_HAL_H
#define MPG_HAL_H

#include "config.h"

#if ENABLE_MPG

static volatile int g_mpgDeltaSteps = 0;  // raw encoder steps (ISR-accumulated)
static uint8_t      g_mpgLastState  = 0;  // last AB state

// Quadrature decode table: index = (lastAB << 2) | currentAB
// +1 = CW step, -1 = CCW step, 0 = invalid/no change
static const int8_t MPG_DECODE_TABLE[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

void IRAM_ATTR mpg_isr() {
  uint8_t a = digitalRead(PIN_MPG_A);
  uint8_t b = digitalRead(PIN_MPG_B);
  uint8_t current = (a << 1) | b;
  uint8_t idx = (g_mpgLastState << 2) | current;
  g_mpgDeltaSteps += MPG_DECODE_TABLE[idx];
  g_mpgLastState = current;
}

void mpg_init() {
  pinMode(PIN_MPG_A, INPUT);
  pinMode(PIN_MPG_B, INPUT);
  g_mpgLastState = (digitalRead(PIN_MPG_A) << 1) | digitalRead(PIN_MPG_B);
  g_mpgDeltaSteps = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_MPG_A), mpg_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_MPG_B), mpg_isr, CHANGE);
  DEBUG_INFO(F("MPG: initialized (GPIO 32+33, quadrature)"));
}

// Returns pending mm movement and resets accumulator.
// MPG has 100 PPR, quadrature = 4 edges/step = 400 counts/rev.
// We use detents: accumulate until threshold reached.
// Returns 0 if no full detent yet.
int mpg_consumeDelta() {
  // Each detent on MR 190.7 = 4 quadrature counts (100 detents × 4 = 400 counts/rev)
  static const int COUNTS_PER_DETENT = 4;

  int raw;
  noInterrupts();
  raw = g_mpgDeltaSteps;
  interrupts();

  int detents = raw / COUNTS_PER_DETENT;
  if (detents == 0) return 0;

  noInterrupts();
  g_mpgDeltaSteps -= detents * COUNTS_PER_DETENT;
  interrupts();

  return detents * MPG_MM_PER_DETENT;
}

#endif // ENABLE_MPG
#endif // MPG_HAL_H
