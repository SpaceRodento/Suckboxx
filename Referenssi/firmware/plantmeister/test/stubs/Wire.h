#pragma once
// Wire stub for native tests.
//
// Two usage modes:
//  1. Legacy simple mode (test_as7341_driver, test_mlx90614_driver,
//     test_scd41_driver, test_sensor_drivers): only `g_wire_endTransmission_result`
//     is set, register plumbing is unused because those tests go through a
//     higher-level driver stub (Adafruit_*.h / SensirionI2cScd4x.h) instead
//     of talking to Wire directly. Default behavior (ACK everywhere, no
//     register storage needed) is preserved for them.
//  2. Register-emulation mode (test_mcp23017_hal): tests write/read actual
//     register bytes through beginTransmission()/write()/endTransmission()/
//     requestFrom()/read(), and can restrict which I2C address ACKs via
//     wire_setAckAddress() to emulate address-range scanning.

#include <cstdint>
#include <map>
#include <vector>

using uint8_t = unsigned char;

// Legacy override: nonzero forces every endTransmission() to fail regardless
// of address, matching how existing sensor-driver tests use this stub.
static int g_wire_endTransmission_result = 0;

class TwoWire {
public:
  void begin(int = -1, int = -1) {}
  void setClock(unsigned long) {}
  void setTimeOut(uint16_t) {}

  void beginTransmission(uint8_t addr) {
    _addr = addr;
    _writeBuf.clear();
  }

  size_t write(uint8_t b) { _writeBuf.push_back(b); return 1; }
  size_t write(const uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; i++) _writeBuf.push_back(buf[i]);
    return n;
  }

  int endTransmission(bool /*sendStop*/ = true) {
    if (g_wire_endTransmission_result != 0) return g_wire_endTransmission_result;
    if (_ackAddressEnabled && _addr != _ackAddress) return 2;  // NACK on address

    if (_writeBuf.size() >= 2) {
      // Register write: byte0 = register, byte1.. = data (MCP23017 writes one byte at a time).
      uint8_t reg = _writeBuf[0];
      for (size_t i = 1; i < _writeBuf.size(); i++) {
        _regs[_addr][(uint8_t)(reg + (i - 1))] = _writeBuf[i];
      }
      _pendingRegSet = false;
    } else if (_writeBuf.size() == 1) {
      // Register-address-only write: repeated-start read setup.
      _pendingReg = _writeBuf[0];
      _pendingRegSet = true;
    }
    return 0;
  }

  int requestFrom(int addr, int len) {
    _readBuf.clear();
    _readPos = 0;
    if (g_wire_endTransmission_result != 0) return 0;
    if (_ackAddressEnabled && (uint8_t)addr != _ackAddress) return 0;

    uint8_t base = _pendingRegSet ? _pendingReg : 0;
    for (int i = 0; i < len; i++) {
      _readBuf.push_back(_regs[(uint8_t)addr][(uint8_t)(base + i)]);
    }
    return (int)_readBuf.size();
  }

  int read() {
    if (_readPos >= _readBuf.size()) return -1;
    return _readBuf[_readPos++];
  }

  int available() { return (int)(_readBuf.size() - _readPos); }

  // ── Test helpers (register-emulation mode) ──────────────────────────
  void wire_reset() {
    _regs.clear();
    _writeBuf.clear();
    _readBuf.clear();
    _readPos = 0;
    _pendingRegSet = false;
    _ackAddressEnabled = false;
    _ackAddress = 0;
  }
  // Only this address ACKs beginTransmission/endTransmission/requestFrom -
  // emulates scanning an address range and finding the device at one slot.
  void wire_setAckAddress(uint8_t addr) { _ackAddress = addr; _ackAddressEnabled = true; }
  void wire_clearAckAddress() { _ackAddressEnabled = false; }
  void wire_setReg(uint8_t addr, uint8_t reg, uint8_t val) { _regs[addr][reg] = val; }
  uint8_t wire_getReg(uint8_t addr, uint8_t reg) { return _regs[addr][reg]; }

private:
  uint8_t _addr = 0;
  std::vector<uint8_t> _writeBuf;
  std::vector<uint8_t> _readBuf;
  size_t _readPos = 0;
  uint8_t _pendingReg = 0;
  bool _pendingRegSet = false;
  bool _ackAddressEnabled = false;
  uint8_t _ackAddress = 0;
  std::map<uint8_t, std::map<uint8_t, uint8_t>> _regs;
};
static TwoWire Wire;
