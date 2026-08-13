#pragma once
// Minimal Adafruit_BME280 stub for native tests

class Adafruit_BME280 {
public:
  bool begin(int) { return true; }
  void setSampling(int, int, int, int, int, int) {}
  void takeForcedMeasurement() {}
  float readTemperature() { return 22.5f; }
  float readHumidity() { return 50.0f; }
  float readPressure() { return 101325.0f; }
  enum { MODE_FORCED, SAMPLING_X1, FILTER_OFF, STANDBY_MS_1000 };
};
