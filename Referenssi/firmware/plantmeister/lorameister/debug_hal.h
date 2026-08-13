/*=====================================================================
  debug_hal.h - Debug Output Hardware Abstraction Layer

  LoraMeister - Debug output routing for ESP32 DevKit.
  Uses hardware UART (Serial) for all debug output.

  USAGE:
    #include "debug_hal.h"

    void setup() {
      debug_init();
      DEBUG_PRINT("Hello debug world");
      DEBUG_PRINTF("Value: %d", 42);
    }

  MACROS:
    DEBUG_PRINT(msg)           - Print without newline
    DEBUG_PRINTLN(msg)         - Print with newline
    DEBUG_PRINTF(fmt, ...)     - Printf-style output

  LEVELED MACROS:
    DEBUG_ERROR(msg)           - Error level
    DEBUG_WARN(msg)            - Warning level
    DEBUG_INFO(msg)            - Info level
    DEBUG_VERBOSE(msg)         - Verbose level
=======================================================================*/

#ifndef DEBUG_HAL_H
#define DEBUG_HAL_H

#include <Arduino.h>
#include "platform_hal.h"

// ═══════════════════════════════════════════════════════════════════
// DEBUG CONFIGURATION
// ═══════════════════════════════════════════════════════════════════

#ifndef DEBUG_BAUDRATE
  #define DEBUG_BAUDRATE 115200
#endif

#ifndef DEBUG_ENABLED
  #define DEBUG_ENABLED 1
#endif

// Debug verbosity levels
#define DEBUG_LEVEL_NONE    0
#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_WARN    2
#define DEBUG_LEVEL_INFO    3
#define DEBUG_LEVEL_VERBOSE 4

#ifndef DEBUG_LEVEL
  #define DEBUG_LEVEL DEBUG_LEVEL_INFO
#endif

// ═══════════════════════════════════════════════════════════════════
// DEBUG STATE
// ═══════════════════════════════════════════════════════════════════

static Stream* g_debugStream = nullptr;
static bool g_debugInitialized = false;

// ═══════════════════════════════════════════════════════════════════
// DEBUG HAL PUBLIC API
// ═══════════════════════════════════════════════════════════════════

bool debug_init() {
  if (g_debugInitialized) {
    return true;
  }

  Serial.begin(DEBUG_BAUDRATE);
  g_debugStream = &Serial;
  g_debugInitialized = true;

  g_debugStream->println();
  g_debugStream->println(F("======================================================="));
  g_debugStream->println(F("         DEBUG HAL - Output Routing                   "));
  g_debugStream->println(F("======================================================="));
  g_debugStream->println(F("Interface:  Hardware UART (Serial)"));
  g_debugStream->print(F("Baudrate:   ")); g_debugStream->println(DEBUG_BAUDRATE);
  g_debugStream->println(F("=======================================================\n"));

  return true;
}

Stream* debug_getStream() {
  return g_debugStream;
}

bool debug_isAvailable() {
  return g_debugInitialized && g_debugStream != nullptr;
}

const char* debug_getInterfaceName() {
  return "UART";
}

// ═══════════════════════════════════════════════════════════════════
// DEBUG OUTPUT FUNCTIONS
// ═══════════════════════════════════════════════════════════════════

void debug_print(const char* msg) {
  #if DEBUG_ENABLED
    if (g_debugStream) g_debugStream->print(msg);
  #endif
}

void debug_println(const char* msg) {
  #if DEBUG_ENABLED
    if (g_debugStream) g_debugStream->println(msg);
  #endif
}

void debug_newline() {
  #if DEBUG_ENABLED
    if (g_debugStream) g_debugStream->println();
  #endif
}

void debug_printf(const char* format, ...) {
  #if DEBUG_ENABLED
    if (!g_debugStream) return;
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    g_debugStream->print(buffer);
  #endif
}

void debug_printfln(const char* format, ...) {
  #if DEBUG_ENABLED
    if (!g_debugStream) return;
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    g_debugStream->println(buffer);
  #endif
}

void debug_printInt(int value) {
  #if DEBUG_ENABLED
    if (g_debugStream) g_debugStream->print(value);
  #endif
}

void debug_printHex(uint32_t value) {
  #if DEBUG_ENABLED
    if (g_debugStream) {
      g_debugStream->print(F("0x"));
      g_debugStream->print(value, HEX);
    }
  #endif
}

void debug_printBin(uint32_t value) {
  #if DEBUG_ENABLED
    if (g_debugStream) {
      g_debugStream->print(F("0b"));
      g_debugStream->print(value, BIN);
    }
  #endif
}

// ═══════════════════════════════════════════════════════════════════
// LEVELED DEBUG MACROS
// ═══════════════════════════════════════════════════════════════════

#if DEBUG_ENABLED

  #define DEBUG_PRINT(msg)       debug_print(msg)
  #define DEBUG_PRINTLN(msg)     debug_println(msg)
  #define DEBUG_PRINTF(...)      debug_printf(__VA_ARGS__)
  #define DEBUG_PRINTFLN(...)    debug_printfln(__VA_ARGS__)
  #define DEBUG_HEX(val)         debug_printHex(val)
  #define DEBUG_BIN(val)         debug_printBin(val)
  #define DEBUG_NEWLINE()        debug_newline()

  #if DEBUG_LEVEL >= DEBUG_LEVEL_ERROR
    #define DEBUG_ERROR(msg)     do { debug_print("[ERROR] "); debug_println(msg); } while(0)
    #define DEBUG_ERRORF(...)    do { debug_print("[ERROR] "); debug_printfln(__VA_ARGS__); } while(0)
  #else
    #define DEBUG_ERROR(msg)
    #define DEBUG_ERRORF(...)
  #endif

  #if DEBUG_LEVEL >= DEBUG_LEVEL_WARN
    #define DEBUG_WARN(msg)      do { debug_print("[WARN] "); debug_println(msg); } while(0)
    #define DEBUG_WARNF(...)     do { debug_print("[WARN] "); debug_printfln(__VA_ARGS__); } while(0)
  #else
    #define DEBUG_WARN(msg)
    #define DEBUG_WARNF(...)
  #endif

  #if DEBUG_LEVEL >= DEBUG_LEVEL_INFO
    #define DEBUG_INFO(msg)      do { debug_print("[INFO] "); debug_println(msg); } while(0)
    #define DEBUG_INFOF(...)     do { debug_print("[INFO] "); debug_printfln(__VA_ARGS__); } while(0)
  #else
    #define DEBUG_INFO(msg)
    #define DEBUG_INFOF(...)
  #endif

  #if DEBUG_LEVEL >= DEBUG_LEVEL_VERBOSE
    #define DEBUG_VERBOSE(msg)   do { debug_print("[VERBOSE] "); debug_println(msg); } while(0)
    #define DEBUG_VERBOSEF(...)  do { debug_print("[VERBOSE] "); debug_printfln(__VA_ARGS__); } while(0)
  #else
    #define DEBUG_VERBOSE(msg)
    #define DEBUG_VERBOSEF(...)
  #endif

#else // DEBUG_ENABLED == 0

  #define DEBUG_PRINT(msg)
  #define DEBUG_PRINTLN(msg)
  #define DEBUG_PRINTF(...)
  #define DEBUG_PRINTFLN(...)
  #define DEBUG_HEX(val)
  #define DEBUG_BIN(val)
  #define DEBUG_NEWLINE()

  #define DEBUG_ERROR(msg)
  #define DEBUG_ERRORF(...)
  #define DEBUG_WARN(msg)
  #define DEBUG_WARNF(...)
  #define DEBUG_INFO(msg)
  #define DEBUG_INFOF(...)
  #define DEBUG_VERBOSE(msg)
  #define DEBUG_VERBOSEF(...)

#endif // DEBUG_ENABLED

// ═══════════════════════════════════════════════════════════════════
// TIMING & SECTION HELPERS
// ═══════════════════════════════════════════════════════════════════

#if DEBUG_ENABLED

void debug_timestamp(const char* msg) {
  if (!g_debugStream) return;
  unsigned long ms = millis();
  unsigned long sec = ms / 1000;
  unsigned long min = sec / 60;
  g_debugStream->print(F("["));
  if (min < 10) g_debugStream->print("0");
  g_debugStream->print(min);
  g_debugStream->print(F(":"));
  if ((sec % 60) < 10) g_debugStream->print("0");
  g_debugStream->print(sec % 60);
  g_debugStream->print(F("."));
  if ((ms % 1000) < 100) g_debugStream->print("0");
  if ((ms % 1000) < 10) g_debugStream->print("0");
  g_debugStream->print(ms % 1000);
  g_debugStream->print(F("] "));
  g_debugStream->println(msg);
}

void debug_section(const char* title) {
  if (!g_debugStream) return;
  g_debugStream->println();
  g_debugStream->println(F("-------------------------------------------------------"));
  g_debugStream->print(F("  ")); g_debugStream->println(title);
  g_debugStream->println(F("-------------------------------------------------------"));
}

#define DEBUG_TIMESTAMP(msg) debug_timestamp(msg)
#define DEBUG_SECTION(title) debug_section(title)

#else
  #define DEBUG_TIMESTAMP(msg)
  #define DEBUG_SECTION(title)
#endif

#endif // DEBUG_HAL_H
