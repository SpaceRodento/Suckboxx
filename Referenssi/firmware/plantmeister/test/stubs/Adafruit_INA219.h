#pragma once
// Minimal INA219 stub

class Adafruit_INA219 {
public:
  Adafruit_INA219(int) {}
  bool begin() { return true; }
  float getBusVoltage_V() { return 3.7f; }
  float getShuntVoltage_mV() { return 100.0f; }
};
