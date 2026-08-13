/*=====================================================================
  reboot_request.h - Deferred reboot helper

  ESP.restart() called from an AsyncWebServer callback (AsyncTCP task)
  leaves the WiFi stack in a state where the post-restart STA reconnect
  often fails silently. Defer the actual restart to loop() so the HTTP
  response is flushed, sockets close cleanly and the WiFi driver shuts
  down from its normal context.

  Usage:
    reboot_request_schedule(500);   // from any task
    ...
    reboot_request_tick();          // call from loop()
=====================================================================*/

#ifndef REBOOT_REQUEST_H
#define REBOOT_REQUEST_H

#include <Arduino.h>
#ifndef PLANTMEISTER_NATIVE_TEST
  #include <WiFi.h>
#endif

static volatile bool     g_rebootPending = false;
static volatile uint32_t g_rebootDeadlineMs = 0;

static inline void reboot_request_schedule(uint32_t delayMs) {
  g_rebootDeadlineMs = millis() + delayMs;
  g_rebootPending = true;
  DEBUG_PRINTF("[INFO]  Reboot: scheduled in %u ms\n", (unsigned)delayMs);
}

static inline bool reboot_request_pending() {
  return g_rebootPending;
}

static inline void reboot_request_tick() {
  if (!g_rebootPending) return;
  if ((int32_t)(millis() - g_rebootDeadlineMs) < 0) return;
#ifndef PLANTMEISTER_NATIVE_TEST
  DEBUG_PRINTF("[INFO]  Reboot: shutting down WiFi cleanly...\n");
  // Clean WiFi/AsyncTCP shutdown — without this ESP.restart() leaves XIAO
  // in a zombie state where USB-CDC + WiFi never come back after reboot.
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  DEBUG_PRINTF("[INFO]  Reboot: executing ESP.restart()\n");
  delay(50);  // let UDP/serial flush
  ESP.restart();
#else
  // Native test: just clear flag, ESP.restart() not available
  g_rebootPending = false;
#endif
}

#ifdef PLANTMEISTER_NATIVE_TEST
// Test-only: clear the pending flag between cases. The module state is a
// file-scope global, so a scheduled reboot would otherwise leak into the
// next test and make "did this command schedule a reboot?" always true.
static inline void reboot_request_clearForTest(void) {
  g_rebootPending = false;
  g_rebootDeadlineMs = 0;
}
#endif

#endif // REBOOT_REQUEST_H
