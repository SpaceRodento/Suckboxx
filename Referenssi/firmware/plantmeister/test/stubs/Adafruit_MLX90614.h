#pragma once
// Minimal Adafruit_MLX90614 stub for native unit tests.

#include <math.h>
#include <stdint.h>

inline bool  g_stub_mlx_begin_result   = true;
inline float g_stub_mlx_object_temp_c  = 21.5f;
inline float g_stub_mlx_ambient_temp_c = 22.5f;

class Adafruit_MLX90614 {
 public:
  bool begin(uint8_t /*addr*/ = 0x5A, void* /*wire*/ = nullptr) {
    return g_stub_mlx_begin_result;
  }
  float readObjectTempC()  { return g_stub_mlx_object_temp_c; }
  float readAmbientTempC() { return g_stub_mlx_ambient_temp_c; }
};
