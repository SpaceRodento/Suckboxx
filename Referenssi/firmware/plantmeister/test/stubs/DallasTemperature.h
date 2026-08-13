#pragma once
// Minimal DallasTemperature stub

// Configurable test hooks
static int g_ds18b20_device_count = 1;
static float g_ds18b20_getTemp_result = 24.0f;

class DallasTemperature {
public:
  DallasTemperature(OneWire*) {}
  void begin() {}
  int getDeviceCount() { return g_ds18b20_device_count; }
  void setResolution(int) {}
  void setWaitForConversion(bool) {}
  void requestTemperatures() {}
  float getTempCByIndex(int) { return g_ds18b20_getTemp_result; }
};

#define DEVICE_DISCONNECTED_C -127
