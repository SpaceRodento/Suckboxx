/*=====================================================================
  mcp23017_hal.h - MCP23017 I2C GPIO expander (PCB v2)

  16-bit I2C GPIO expander at 0x20 (A0/A1/A2 = GND). Adds the I/O that
  V1's 11 native GPIOs no longer have room for. PORTA pin map (pcb_v2_
  layout.md § 1.1b):

    GPA0 FAN_TACHO   IN  (interrupt-on-change, coarse spin/no-spin)
    GPA1 AIRPUMP_EN  OUT (reserve — ENABLE_AIR_PUMP)
    GPA2 LED_R       OUT (illuminated-button reserve, § 7.2)
    GPA3 LED_G       OUT (reserve)
    GPA4 LED_B       OUT (reserve)
    GPA5 BTN_USER    IN  (illuminated-button reserve)
    GPA6 FLOAT_MIN   IN  (relocated from native D6 when the fan takes D6)
    GPA7 (free)

  Only FAN_TACHO (GPA0) and FLOAT_MIN (GPA6) are used by firmware today;
  the rest are PCB reserves. INTA/INTB are NOT wired — the tacho's
  interrupt-on-change is polled over I2C (INTFA flag register), so no
  physical INT line is needed (pcb_v2_layout.md § 3).

  Architecture § 8 graceful degradation: presence-check before configure,
  readyFlag, periodic re-probe. A missing expander never blocks boot; the
  relocated FLOAT_MIN then falls back to "assume water present" and the fan
  simply loses tacho health monitoring (both advisory, non-blocking).

  Design note — single cached PORTA snapshot:
    Reading GPIOA clears the interrupt-on-change latch, so the tacho edge
    read (INTFA) and the FLOAT_MIN level read must not each poke GPIOA
    independently or they would clear each other's captures. Instead
    mcp23017_tick() does ONE rate-limited read per refresh (INTFA then
    GPIOA), caches the levels and OR-accumulates the edge flags. Callers
    read the cache: mcp23017_cachedPin() for levels, consumeEdgesA() for
    tacho edges.
=====================================================================*/

#ifndef MCP23017_HAL_H
#define MCP23017_HAL_H

#include "config.h"

#if ENABLE_MCP23017

#include <Wire.h>

// MCP23017 register addresses (IOCON.BANK = 0, the power-on default).
#define MCP23017_REG_IODIRA    0x00
#define MCP23017_REG_IPOLA     0x02
#define MCP23017_REG_GPINTENA  0x04
#define MCP23017_REG_DEFVALA   0x06
#define MCP23017_REG_INTCONA   0x08
#define MCP23017_REG_IOCON     0x0A
#define MCP23017_REG_GPPUA     0x0C
#define MCP23017_REG_INTFA     0x0E
#define MCP23017_REG_INTCAPA   0x10
#define MCP23017_REG_GPIOA     0x12
#define MCP23017_REG_OLATA     0x14

// PORTA masks derived from the pin map above:
//   inputs  = GPA0 tacho, GPA5 button, GPA6 float_min, GPA7 free  → 0xE1
//   outputs = GPA1 airpump, GPA2..4 button LEDs                    → 0x1E
#define MCP23017_PORTA_INPUT_MASK  0xE1
#define MCP23017_PORTA_PULLUP_MASK 0xE1                          // pull-ups on all inputs
#define MCP23017_PORTA_IOC_MASK    (1 << MCP_PIN_FAN_TACHO)      // interrupt-on-change: tacho only

static bool     g_mcp23017Ready  = false;
static uint8_t  g_mcpAddr        = I2C_ADDR_MCP23017;  // bound address (scan 0x20-0x27)
static uint8_t  g_mcpOlatA       = 0x00;   // shadow of PORTA output latch
static uint8_t  g_mcpPortA       = 0xFF;   // cached GPIOA levels (default high = pulled-up input)
static uint8_t  g_mcpEdgeAccumA  = 0x00;   // OR-accumulated INTFA edges since last consume
static uint32_t g_mcpLastRefreshMs = 0;    // mcp23017_tick() rate-limit state
static uint32_t g_mcpReprobeMs     = 0;    // mcp23017_tick() re-probe-while-absent state

static bool mcp23017_writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(g_mcpAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool mcp23017_readReg(uint8_t reg, uint8_t* out) {
  Wire.beginTransmission(g_mcpAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;       // repeated start
  if (Wire.requestFrom((int)g_mcpAddr, 1) != 1) return false;
  *out = (uint8_t)Wire.read();
  return true;
}

bool mcp23017_isReady() { return g_mcp23017Ready; }
uint8_t mcp23017_addr() { return g_mcpAddr; }

// Scan the MCP23017 address range and bind to the first device that ACKs.
// A0/A1/A2 are strap-selectable (0x20..0x27); many breakout boards tie them
// high by default (→ 0x27) with no exposed jumper. Scanning the range makes
// the expander plug-and-play regardless of strapping — no jumper change
// needed. Safe on this bus: 0x20..0x27 collides with no project device
// (SCD41 0x62, AS7341 0x39, MLX 0x5A, VL53L0X 0x29, DHT20 0x38).
#ifndef MCP23017_ADDR_MIN
#define MCP23017_ADDR_MIN 0x20
#endif
#ifndef MCP23017_ADDR_MAX
#define MCP23017_ADDR_MAX 0x27
#endif
static bool mcp23017_scanAddr() {
  for (uint8_t a = MCP23017_ADDR_MIN; a <= MCP23017_ADDR_MAX; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { g_mcpAddr = a; return true; }
  }
  return false;
}

// Configure the expander. Address scan first (§ 8) — never blocks boot.
bool mcp23017_init() {
  g_mcp23017Ready = false;
  if (!mcp23017_scanAddr()) {
    DEBUG_WARN(F("MCP23017: not found on I2C (scanned 0x20-0x27)"));
    return false;
  }
  bool ok = true;
  ok &= mcp23017_writeReg(MCP23017_REG_IODIRA,   MCP23017_PORTA_INPUT_MASK);
  ok &= mcp23017_writeReg(MCP23017_REG_GPPUA,    MCP23017_PORTA_PULLUP_MASK);
  ok &= mcp23017_writeReg(MCP23017_REG_INTCONA,  0x00);   // compare-to-previous = interrupt-on-change
  ok &= mcp23017_writeReg(MCP23017_REG_GPINTENA, MCP23017_PORTA_IOC_MASK);
  g_mcpOlatA = 0x00;
  ok &= mcp23017_writeReg(MCP23017_REG_OLATA,    g_mcpOlatA);   // outputs low (air pump / LEDs off)
  g_mcp23017Ready = ok;
  DEBUG_PRINTF("[INFO]  MCP23017: %s (0x%02X)\n", ok ? "initialized" : "config FAIL", g_mcpAddr);
  return ok;
}

// Cached PORTA pin level (HIGH/LOW). Reflects the last mcp23017_tick() read.
int mcp23017_cachedPin(uint8_t pin) {
  if (pin > 7) return LOW;
  return (g_mcpPortA & (1 << pin)) ? HIGH : LOW;
}

// Return and clear the PORTA edge flags accumulated since the last call.
// Bit N set = pin GPAn changed at least once in the window (coarse tacho).
uint8_t mcp23017_consumeEdgesA() {
  uint8_t e = g_mcpEdgeAccumA;
  g_mcpEdgeAccumA = 0x00;
  return e;
}

// Drive one PORTA output pin (0..7) via the OLAT shadow.
void mcp23017_writePin(uint8_t pin, bool high) {
  if (!g_mcp23017Ready || pin > 7) return;
  if (high) g_mcpOlatA |= (uint8_t)(1 << pin);
  else      g_mcpOlatA &= (uint8_t)~(1 << pin);
  mcp23017_writeReg(MCP23017_REG_OLATA, g_mcpOlatA);
}

// Loop tick — call every loop(); internally rate-limited to MCP23017_REFRESH_MS.
// When ready: reads INTFA (edges) + GPIOA (levels, clears latch) into the cache.
// When not ready: periodically re-probes and re-inits (runtime degradation, § 8).
void mcp23017_tick(uint32_t nowMs) {
  if ((uint32_t)(nowMs - g_mcpLastRefreshMs) < MCP23017_REFRESH_MS) return;
  g_mcpLastRefreshMs = nowMs;

  if (!g_mcp23017Ready) {
    if ((uint32_t)(nowMs - g_mcpReprobeMs) < 2000U) return;    // ~0.5 Hz, silent while absent
    g_mcpReprobeMs = nowMs;
    if (!mcp23017_scanAddr()) return;                       // still absent (scans 0x20-0x27)
    if (mcp23017_init()) DEBUG_INFO(F("MCP23017: re-detected"));
    return;
  }

  uint8_t intf = 0, gpio = 0;
  bool okIntf = mcp23017_readReg(MCP23017_REG_INTFA, &intf);
  if (!mcp23017_readReg(MCP23017_REG_GPIOA, &gpio)) {       // clears IOC latch
    g_mcp23017Ready = false;                                // lost (§ 8)
    DEBUG_WARN(F("MCP23017: lost"));
    return;
  }
  g_mcpPortA = gpio;
  if (okIntf) g_mcpEdgeAccumA |= intf;
}

#ifdef PLANTMEISTER_NATIVE_TEST
// Test-only: reset module state between test cases (globals otherwise leak
// across tests since this module has no per-instance object).
void mcp23017_resetForTest(void) {
  g_mcp23017Ready    = false;
  g_mcpAddr          = I2C_ADDR_MCP23017;
  g_mcpOlatA         = 0x00;
  g_mcpPortA         = 0xFF;
  g_mcpEdgeAccumA    = 0x00;
  g_mcpLastRefreshMs = 0;
  g_mcpReprobeMs     = 0;
}
#endif

#endif // ENABLE_MCP23017
#endif // MCP23017_HAL_H
