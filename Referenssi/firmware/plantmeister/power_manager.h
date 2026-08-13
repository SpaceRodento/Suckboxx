/*=====================================================================
  power_manager.h - Power Management

  Handles:
  - Motor driver power-down when idle
  - LED heartbeat
  - Deep sleep preparation (future: battery operation)
  - Relay control for grow light
=====================================================================*/

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "config.h"
#include "structs.h"

static unsigned long g_lastHeartbeat = 0;
static bool g_ledState = false;

#if USE_XIAO_SX1262
  #define LED_ON_LEVEL LOW
  #define LED_OFF_LEVEL HIGH
#else
  #define LED_ON_LEVEL HIGH
  #define LED_OFF_LEVEL LOW
#endif

void power_init() {
  // Status LED — vain jos PIN_LED on konfiguroitu (>= 0). V1 PCB:lla ei status-LEDia.
  if (PIN_LED >= 0) {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LED_OFF_LEVEL);
  }

  // Relay pins — set to OFF state immediately
#if ENABLE_LIGHT_RELAY
#if PUMP_LIGHT_SHARED_PIN
  DEBUG_WARN(F("Light relay disabled: PIN_RELAY_LIGHT shares GPIO with pump"));
#else
  pinMode(PIN_RELAY_LIGHT, OUTPUT);
  #if LIGHT_ACTIVE_LOW
    digitalWrite(PIN_RELAY_LIGHT, HIGH);   // OFF
  #else
    digitalWrite(PIN_RELAY_LIGHT, LOW);    // OFF (MOSFET gate down)
  #endif
#endif
#endif

#if ENABLE_AIR_PUMP
  pinMode(PIN_RELAY_AIR_PUMP, OUTPUT);
  // Air pump starts ON immediately — DWC needs constant aeration
  #if RELAY_ACTIVE_LOW
    digitalWrite(PIN_RELAY_AIR_PUMP, LOW);    // ON
  #else
    digitalWrite(PIN_RELAY_AIR_PUMP, HIGH);   // ON
  #endif
  DEBUG_INFO(F("Air pump: ON (24/7 DWC aeration)"));
#endif

  DEBUG_INFO(F("Power: initialized"));
}

// ── Relay control ───────────────────────────────────────────────

void power_setLight(bool on) {
#if ENABLE_LIGHT_RELAY
#if PUMP_LIGHT_SHARED_PIN
  (void)on;
  DEBUG_WARN(F("Light command ignored: shared pump/light GPIO"));
#else
  #if LIGHT_ACTIVE_LOW
    digitalWrite(PIN_RELAY_LIGHT, on ? LOW : HIGH);
  #else
    digitalWrite(PIN_RELAY_LIGHT, on ? HIGH : LOW);
  #endif
  DEBUG_PRINTF("[INFO]  Light relay: %s\n", on ? "ON" : "OFF");
#endif
#endif
}

void power_setAirPump(bool on) {
#if ENABLE_AIR_PUMP
  #if RELAY_ACTIVE_LOW
    digitalWrite(PIN_RELAY_AIR_PUMP, on ? LOW : HIGH);
  #else
    digitalWrite(PIN_RELAY_AIR_PUMP, on ? HIGH : LOW);
  #endif
  DEBUG_PRINTF("[INFO]  Air pump: %s\n", on ? "ON" : "OFF");
#endif
}

// ── LED heartbeat ───────────────────────────────────────────────

// Short blink every LED_HEARTBEAT_INTERVAL_MS — shows device is alive
void power_heartbeat() {
  // V1 PCB:lla PIN_LED == -1 -> ei status-LEDia. digitalWrite(-1, ...)
  // tuottaa ESP-IDF:lla "gpio_set_level(227): GPIO output gpio_num error"
  // -virheen 5 s valein. Skipataan koko heartbeat jos LEDia ei ole.
  if (PIN_LED < 0) return;

  unsigned long now = millis();

  if (now - g_lastHeartbeat >= LED_HEARTBEAT_INTERVAL_MS) {
    g_lastHeartbeat = now;
    // Quick blink: 50ms on
    digitalWrite(PIN_LED, LED_ON_LEVEL);
    g_ledState = true;
  }

  // Turn off after 50ms
  if (g_ledState && (now - g_lastHeartbeat >= 50)) {
    digitalWrite(PIN_LED, LED_OFF_LEVEL);
    g_ledState = false;
  }
}

// ── Deep sleep (future) ─────────────────────────────────────────

#if ENABLE_DEEP_SLEEP
void power_enterDeepSleep(unsigned long durationMs) {
  DEBUG_PRINTF("[INFO]  Entering deep sleep for %lu ms\n", durationMs);
  DEBUG_SERIAL.flush();

  // Disable peripherals before sleep
  #if ENABLE_MOTOR
    motor_disable();
  #endif

  #if ENABLE_AIR_PUMP
    power_setAirPump(false);   // Save power during sleep
  #endif

  esp_sleep_enable_timer_wakeup(durationMs * 1000ULL);   // microseconds
  esp_deep_sleep_start();
}
#endif

// ── Uptime helper ───────────────────────────────────────────────

void power_formatUptime(char* buf, int bufSize) {
  unsigned long sec = millis() / 1000;
  unsigned long min = sec / 60;
  unsigned long hrs = min / 60;

  if (hrs > 0) {
    snprintf(buf, bufSize, "%luh %lum", hrs, min % 60);
  } else if (min > 0) {
    snprintf(buf, bufSize, "%lum %lus", min, sec % 60);
  } else {
    snprintf(buf, bufSize, "%lus", sec);
  }
}

#endif // POWER_MANAGER_H
