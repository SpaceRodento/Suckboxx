#pragma once
// Minimal VL53L0X stub

typedef struct { int RangeStatus; int RangeMilliMeter; } VL53L0X_RangingMeasurementData_t;

class Adafruit_VL53L0X {
public:
  bool begin(int) { return true; }
  void rangingTest(VL53L0X_RangingMeasurementData_t* m, bool) { m->RangeStatus = 0; m->RangeMilliMeter = 123; }
};
