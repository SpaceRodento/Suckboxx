/*=====================================================================
  ina228.h - Portable TI INA228 current / voltage / power / energy driver

  SELF-CONTAINED, HEADER-ONLY. Drop into any Arduino/ESP32 project:
  the only dependency is <Wire.h>. No PlantMeister headers, no config.h.
  To reuse elsewhere just copy this one file.

      #include "ina228.h"
      INA228 ina;                                        // defaults to Wire, addr 0x40
      INA228Config cfg = { 1.0f, 0.04f, 1, 0x40, 1.0f }; // 1 ohm shunt, 40 mA max, ADCRANGE=1, no cal
      ina.begin(cfg);
      float mA = ina.current_A() * 1000.0f;
      double mAh = ina.charge_mAh();               // on-chip accumulator (multi-day)

  WHY REGISTER-LEVEL (no Adafruit lib): the INA228 is a 20-bit part with a
  different register map + LSB scheme than the INA219/INA226. The Adafruit
  INA219 library does NOT drive an INA228 correctly. This file implements the
  INA228 map directly.

  SCALING - all constants from the TI INA228 datasheet (SBOS736):
    - VBUS      LSB = 195.3125 uV      (20-bit in bits [23:4], unsigned)
    - VSHUNT    LSB = 312.5 nV (ADCRANGE=0) / 78.125 nV (ADCRANGE=1), signed 20-bit
    - DIETEMP   LSB = 7.8125 m degC    (16-bit, signed)
    - CURRENT_LSB   = Max_Current / 2^19
    - SHUNT_CAL     = 13107.2e6 * CURRENT_LSB * R_shunt   (x4 if ADCRANGE=1)
    - POWER     LSB = 3.2 * CURRENT_LSB               (24-bit, unsigned)
    - ENERGY    LSB = 16 * POWER_LSB                  (40-bit accumulator, unsigned)
    - CHARGE    LSB = CURRENT_LSB                     (40-bit accumulator, signed)

  SMALL-CURRENT / MULTI-DAY USE:
    - Pick R_shunt so typical current develops >= ~50-100 uV across it, else
      the reading sits in the +/- few-uV offset floor. Bigger R = better small-
      current resolution but more burden voltage / self-heating.
    - Use ADCRANGE=1 (+/-40.96 mV full scale, 78.125 nV LSB) for small currents.
    - The 40-bit CHARGE/ENERGY accumulators integrate on-chip, so multi-day
      totals do not depend on host polling rate. Overflow headroom (charge) is
      ~291.3 * Max_Current amp-hours; check DIAG_ALRT bit for CHARGEOF/ENERGYOF
      and call resetAccumulators() to zero them.
=====================================================================*/

#ifndef INA228_H
#define INA228_H

#include <stdint.h>
#include <Wire.h>

// ── Register map ───────────────────────────────────────────────────
enum {
  INA228_REG_CONFIG     = 0x00,  // 16-bit: RST(15) RSTACC(14) ... ADCRANGE(4)
  INA228_REG_ADC_CONFIG = 0x01,  // 16-bit: MODE / conversion times / averaging
  INA228_REG_SHUNT_CAL  = 0x02,  // 16-bit: calibration (15-bit value)
  INA228_REG_VSHUNT     = 0x04,  // 24-bit: shunt voltage (signed)
  INA228_REG_VBUS       = 0x05,  // 24-bit: bus voltage (unsigned)
  INA228_REG_DIETEMP    = 0x06,  // 16-bit: die temperature (signed)
  INA228_REG_CURRENT    = 0x07,  // 24-bit: current (signed)
  INA228_REG_POWER      = 0x08,  // 24-bit: power (unsigned)
  INA228_REG_ENERGY     = 0x09,  // 40-bit: energy accumulator (unsigned)
  INA228_REG_CHARGE     = 0x0A,  // 40-bit: charge accumulator (signed)
  INA228_REG_DIAG_ALRT  = 0x0B,  // 16-bit: alert / overflow flags
  INA228_REG_MANUF_ID   = 0x3E,  // 16-bit: 0x5449 ("TI")
  INA228_REG_DEVICE_ID  = 0x3F   // 16-bit: 0x228x
};

// Default ADC_CONFIG: MODE=continuous(all), VBUS/VSH/VT conv time 1052 us,
// 16-sample averaging. Good low-noise default for slow (multi-day) logging.
#ifndef INA228_ADC_CONFIG_DEFAULT
  #define INA228_ADC_CONFIG_DEFAULT 0xFB6A
#endif

// ── Pure conversion math (no I/O — unit-testable without hardware) ──
//
// These are the entire "correctness surface" of the driver. Keeping them
// free functions lets native unit tests feed known raw register values and
// assert exact physical results, independent of any Wire traffic.

static inline float ina228_currentLsb(float maxCurrentA) {
  return maxCurrentA / 524288.0f;   // 2^19
}

// SHUNT_CAL register value from CURRENT_LSB + shunt resistance. Clamped to
// the 16-bit register range. ADCRANGE=1 requires a x4 multiplier.
static inline uint16_t ina228_shuntCalReg(float currentLsb, float shuntOhms,
                                          uint8_t adcRange) {
  double cal = 13107200000.0 * (double)currentLsb * (double)shuntOhms; // 13107.2e6
  if (adcRange) cal *= 4.0;
  if (cal < 0.0) cal = 0.0;
  if (cal > 65535.0) cal = 65535.0;
  return (uint16_t)(cal + 0.5);
}

// Sign-extend the 20-bit ADC field that lives in bits [23:4] of a 24-bit read.
static inline int32_t ina228_sx20(uint32_t raw24) {
  int32_t v = (int32_t)(raw24 >> 4);
  if (v & 0x00080000) v -= 0x00100000;   // bit19 = sign
  return v;
}

// Sign-extend a 40-bit two's-complement value.
static inline int64_t ina228_sx40(uint64_t raw40) {
  if (raw40 & 0x8000000000ULL) return (int64_t)(raw40 - 0x10000000000ULL);
  return (int64_t)raw40;
}

static inline float ina228_rawToBusV(uint32_t raw24) {
  return (float)(raw24 >> 4) * 195.3125e-6f;      // 195.3125 uV/LSB, unsigned
}

static inline float ina228_rawToShuntuV(uint32_t raw24, uint8_t adcRange) {
  const float lsb_uV = adcRange ? 0.078125f : 0.3125f;  // 78.125 nV / 312.5 nV
  return (float)ina228_sx20(raw24) * lsb_uV;
}

static inline float ina228_rawToCurrentA(uint32_t raw24, float currentLsb) {
  return (float)ina228_sx20(raw24) * currentLsb;
}

static inline float ina228_rawToDieTempC(uint16_t raw16) {
  return (float)(int16_t)raw16 * 0.0078125f;      // 7.8125 m degC/LSB
}

static inline float ina228_rawToPowerW(uint32_t raw24, float currentLsb) {
  return 3.2f * currentLsb * (float)raw24;         // POWER is unsigned 24-bit
}

static inline double ina228_rawToEnergyJ(uint64_t raw40, float currentLsb) {
  return 16.0 * 3.2 * (double)currentLsb * (double)raw40;
}

static inline double ina228_rawToChargeC(uint64_t raw40, float currentLsb) {
  return (double)currentLsb * (double)ina228_sx40(raw40);
}

// ── Runtime configuration ──────────────────────────────────────────
struct INA228Config {
  float   shuntOhms;    // Shunt resistor value in ohms
  float   maxCurrentA;  // Max expected current -> CURRENT_LSB = maxCurrentA / 2^19
  uint8_t adcRange;     // 0 = +/-163.84 mV, 1 = +/-40.96 mV (small currents)
  uint8_t address;      // I2C address (0 -> default 0x40)
  float   calFactor;    // Calibration multiplier on SHUNT_CAL (0 or 1 = none).
                        // Set by a reference-current calibration to absorb shunt
                        // tolerance without knowing the exact resistance.
};

// ── Driver object ──────────────────────────────────────────────────
class INA228 {
public:
  explicit INA228(TwoWire& wire = Wire) : _wire(wire) {}

  // Configures the device. Returns false if it does not ACK on the bus.
  bool begin(const INA228Config& cfg) {
    _cfg = cfg;
    if (_cfg.address == 0) _cfg.address = 0x40;
    _wire.beginTransmission(_cfg.address);
    if (_wire.endTransmission() != 0) return false;

    _currentLsb = ina228_currentLsb(_cfg.maxCurrentA);

    uint16_t config = 0;
    if (_cfg.adcRange) config |= (1 << 4);          // ADCRANGE
    _write16(INA228_REG_CONFIG, config);
    _write16(INA228_REG_ADC_CONFIG, INA228_ADC_CONFIG_DEFAULT);

    // Nominal calibration, then apply the optional correction factor (re-clamped
    // to the 16-bit register). calFactor <= 0 means "no correction".
    uint32_t cal = ina228_shuntCalReg(_currentLsb, _cfg.shuntOhms, _cfg.adcRange);
    if (_cfg.calFactor > 0.0f) {
      double scaled = (double)cal * (double)_cfg.calFactor + 0.5;
      cal = (scaled > 65535.0) ? 65535u : (uint32_t)scaled;
    }
    _write16(INA228_REG_SHUNT_CAL, (uint16_t)cal);
    return true;
  }

  bool present() {
    _wire.beginTransmission(_cfg.address);
    return _wire.endTransmission() == 0;
  }

  float  busVoltage_V()  { return ina228_rawToBusV(_read24(INA228_REG_VBUS)); }
  float  shuntVoltage_uV() { return ina228_rawToShuntuV(_read24(INA228_REG_VSHUNT), _cfg.adcRange); }
  float  current_A()     { return ina228_rawToCurrentA(_read24(INA228_REG_CURRENT), _currentLsb); }
  float  power_W()       { return ina228_rawToPowerW(_read24(INA228_REG_POWER), _currentLsb); }
  float  dieTemp_C()     { return ina228_rawToDieTempC(_read16(INA228_REG_DIETEMP)); }
  double energy_J()      { return ina228_rawToEnergyJ(_read40(INA228_REG_ENERGY), _currentLsb); }
  double charge_C()      { return ina228_rawToChargeC(_read40(INA228_REG_CHARGE), _currentLsb); }
  double charge_mAh()    { return charge_C() / 3.6; }   // 1 mAh = 3.6 C
  double energy_Wh()     { return energy_J() / 3600.0; }

  // Zero the on-chip ENERGY + CHARGE accumulators (RSTACC, self-clearing).
  void resetAccumulators() {
    uint16_t config = (1 << 14);                    // RSTACC
    if (_cfg.adcRange) config |= (1 << 4);
    _write16(INA228_REG_CONFIG, config);
  }

  float currentLsb() const { return _currentLsb; }

private:
  TwoWire&     _wire;
  INA228Config _cfg   = { 0.015f, 5.0f, 0, 0x40, 1.0f };
  float        _currentLsb = 0.0f;

  void _write16(uint8_t reg, uint16_t val) {
    _wire.beginTransmission(_cfg.address);
    _wire.write(reg);
    _wire.write((uint8_t)(val >> 8));
    _wire.write((uint8_t)(val & 0xFF));
    _wire.endTransmission();
  }

  uint16_t _read16(uint8_t reg) {
    _wire.beginTransmission(_cfg.address);
    _wire.write(reg);
    if (_wire.endTransmission(false) != 0) return 0;
    _wire.requestFrom((int)_cfg.address, 2);
    uint16_t v = 0;
    for (int i = 0; i < 2 && _wire.available(); i++) v = (v << 8) | (uint8_t)_wire.read();
    return v;
  }

  uint32_t _read24(uint8_t reg) {
    _wire.beginTransmission(_cfg.address);
    _wire.write(reg);
    if (_wire.endTransmission(false) != 0) return 0;
    _wire.requestFrom((int)_cfg.address, 3);
    uint32_t v = 0;
    for (int i = 0; i < 3 && _wire.available(); i++) v = (v << 8) | (uint8_t)_wire.read();
    return v;
  }

  uint64_t _read40(uint8_t reg) {
    _wire.beginTransmission(_cfg.address);
    _wire.write(reg);
    if (_wire.endTransmission(false) != 0) return 0;
    _wire.requestFrom((int)_cfg.address, 5);
    uint64_t v = 0;
    for (int i = 0; i < 5 && _wire.available(); i++) v = (v << 8) | (uint8_t)_wire.read();
    return v;
  }
};

#endif // INA228_H
