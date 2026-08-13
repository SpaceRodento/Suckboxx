#pragma once
// Minimal Adafruit_AS7341 stub for native unit tests.
// Mirrors only the symbols sensor_driver_as7341.h touches.

#include <stdint.h>

enum {
  AS7341_GAIN_0_5X = 0,
  AS7341_GAIN_1X,
  AS7341_GAIN_2X,
  AS7341_GAIN_4X,
  AS7341_GAIN_8X,
  AS7341_GAIN_16X,
  AS7341_GAIN_32X,
  AS7341_GAIN_64X,
  AS7341_GAIN_128X,
  AS7341_GAIN_256X,
  AS7341_GAIN_512X,
};

// Test hooks — toggled by individual test cases.
inline bool g_stub_as7341_begin_result   = true;
inline bool g_stub_as7341_readAll_result = true;
inline uint16_t g_stub_as7341_flicker_hz = 100;
inline uint16_t g_stub_as7341_channels[12] = {
  100, 200, 300, 400, 500, 600, 700, 800,   // F1..F8
  1500, 250,                                  // clear, NIR
  0, 0
};

class Adafruit_AS7341 {
 public:
  bool begin(uint8_t /*addr*/ = 0, void* /*wire*/ = nullptr) {
    return g_stub_as7341_begin_result;
  }
  void setATIME(uint8_t)   {}
  void setASTEP(uint16_t)  {}
  void setGain(int)        {}
  bool readAllChannels(uint16_t* out) {
    if (!g_stub_as7341_readAll_result) return false;
    for (int i = 0; i < 12; i++) out[i] = g_stub_as7341_channels[i];
    return true;
  }
  int detectFlickerHz() { return (int)g_stub_as7341_flicker_hz; }
};
