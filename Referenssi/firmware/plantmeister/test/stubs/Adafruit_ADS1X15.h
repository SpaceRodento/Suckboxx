#pragma once
// Minimal ADS1115 stub

using int16_t = short;

class Adafruit_ADS1115 {
public:
  bool begin(int) { return true; }
  void setGain(int) {}
  int16_t readADC_SingleEnded(int) { return 1000; }
  float computeVolts(int16_t raw) { return (float)raw / 1000.0f; }
};

enum { GAIN_ONE };
