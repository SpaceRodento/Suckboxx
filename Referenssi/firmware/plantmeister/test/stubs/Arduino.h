#pragma once
// Minimal Arduino stub for native (host) unit tests.
// Provides enough surface area for config.h, structs.h, and scheduler.h
// to compile without the ESP32 framework.

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdarg>

// GPIO constants
#define HIGH 1
#define LOW  0
#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2

// F() macro — passthrough on native (no PROGMEM)
#define F(x) (x)
#define PROGMEM
#define PGM_P const char*

// min / max / abs / constrain
using std::min;
using std::max;
using std::abs;
#define constrain(val, lo, hi) ((val) < (lo) ? (lo) : ((val) > (hi) ? (hi) : (val)))

// ── millis stub — controllable from tests ──────────────────────────
// Stored as uint32_t so that rollover at 0xFFFFFFFF behaves like the
// MCU. On 64-bit hosts unsigned long is 64-bit, which would otherwise
// hide rollover bugs that bite on 32-bit ESP32.
static uint32_t g_stub_millis = 0;
inline unsigned long millis() { return (unsigned long)(uint32_t)g_stub_millis; }
inline unsigned long micros()  { return (unsigned long)(uint32_t)(g_stub_millis * 1000UL); }
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned long) {}

// ── GPIO no-ops ────────────────────────────────────────────────────
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int  digitalRead(int) { return LOW; }
inline int  analogRead(int)  { return 0; }
inline void analogWrite(int, int) {}

// ── Serial stub ────────────────────────────────────────────────────
struct HardwareSerial {
  void begin(long) {}
  void print(const char*) {}
  void print(int) {}
  void print(float) {}
  void print(long) {}
  void println(const char*) {}
  void println(int) {}
  void println(float) {}
  void println(long) {}
  void println() {}
  void printf(const char*, ...) {}
  void flush() {}
  int  available() { return 0; }
  int  read()      { return -1; }
  int  peek()      { return -1; }
  size_t write(uint8_t) { return 1; }
  explicit operator bool() { return true; }
};
static HardwareSerial Serial;
static HardwareSerial Serial1;
static HardwareSerial Serial2;

// ── ESP system stub ───────────────────────────────────────────────
struct ESPClass {
  void restart() {}
};
static ESPClass ESP;

// ── String class — minimal ─────────────────────────────────────────
class String {
  std::string _s;
public:
  String() {}
  explicit String(const char* s) : _s(s ? s : "") {}
  String(int v)          { char b[24]; snprintf(b, sizeof(b), "%d",  v);    _s = b; }
  String(long v)         { char b[24]; snprintf(b, sizeof(b), "%ld", v);    _s = b; }
  String(unsigned int v) { char b[24]; snprintf(b, sizeof(b), "%u",  v);    _s = b; }
  String(float v, int d = 2) { char b[32]; snprintf(b, sizeof(b), "%.*f", d, v); _s = b; }
  const char* c_str()   const { return _s.c_str(); }
  int  length()         const { return (int)_s.size(); }
  bool operator==(const String& o) const { return _s == o._s; }
  bool operator==(const char* o)   const { return _s == o; }
  String operator+(const String& o) const { String r; r._s = _s + o._s; return r; }
  operator const char*() const { return _s.c_str(); }
};

// NOTE: serialized() is intentionally NOT stubbed here. ArduinoJson.h ships
// its own template `serialized(T str)` that wraps any type exposing
// c_str()/length() (our String does) into a SerializedValue<T> that
// bypasses JSON quoting — exactly what production code (sensor_history.h)
// relies on. A custom overload here would shadow that template with an
// exact-match non-template String overload and break the ArduinoJson
// conversion path (observed 4.7.2026 while adding test_sensor_history).

// pgm_read passthrough
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
