/*=====================================================================
  wifi_portal_api_test.h - Per-module functionality tests

  POST /api/test/{module}

  Modules: motor_up_10mm, motor_down_10mm, motor_home,
           pump_3s, light_toggle, air_pump_toggle,
           lora_ping, eink_redraw, led_cycle

  Test commands only succeed in IDLE or FAULT state — never during
  INIT, SELF_TEST, GROWING, or SHUTDOWN.
  Returns JSON {ok: bool, message: string} or {ok: false, error: string}.

  Included by wifi_portal.h after shared globals.
=====================================================================*/

#ifndef WIFI_PORTAL_API_TEST_H
#define WIFI_PORTAL_API_TEST_H

#if ENABLE_API_TEST_ENDPOINTS

#include "device_state.h"
#include "ux_indicator.h"

// Allow only IDLE and FAULT — tests are destructive (motor moves, pump runs).
static bool portal_testGuardState(AsyncWebServerRequest* req) {
  DeviceState s = g_device.state;
  if (s != DEVICE_IDLE && s != DEVICE_FAULT) {
    api_sendEnvelope(req, 409, false, false,
                     "refused: tests only allowed in IDLE or FAULT state",
                     "device_state");
    return false;
  }
  return true;
}

// Test-endpointit kayttavat samaa envelopea kuin /api/command — message-kentta
// on epavirallinen lisays (vain test-endpointeilla) kayttajalle nakyvaa
// statusta varten. Asiakas voi luottaa ok-kenttaan kuten muissa endpointeissa.
static void portal_testRespondOk(AsyncWebServerRequest* req, const char* msg) {
  StaticJsonDocument<128> out;
  out["ok"]              = true;
  out["restartRequired"] = false;
  out["message"]         = msg;
  char buf[128];
  serializeJson(out, buf, sizeof(buf));
  req->send(200, "application/json", buf);
}

static void portal_testRespondFail(AsyncWebServerRequest* req, const char* msg) {
  api_sendEnvelope(req, 500, false, false, msg);
}

static void portal_registerTestApi() {
#if ENABLE_MOTOR
  g_webServer.on("/api/test/motor_up_10mm", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;
    motor_moveBy(10);
    portal_testRespondOk(req, "motor moving up 10mm");
  });

  g_webServer.on("/api/test/motor_down_10mm", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;
    motor_moveBy(-10);
    portal_testRespondOk(req, "motor moving down 10mm");
  });

  // motor_home: not implemented until LIFT-3 (homing logic). Stub returns 501.
  g_webServer.on("/api/test/motor_home", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    api_sendEnvelope(req, 501, false, false, "not implemented (LIFT-3)");
  });
#endif

#if ENABLE_PUMP
  g_webServer.on("/api/test/pump_3s", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;
    pump_dose(20);   // ~3 s typical proto pump
    if (g_portalState) g_portalState->pumpRunning = true;
    portal_testRespondOk(req, "pump running 20 ml dose");
  });

  // Continuous-flow mode for ebb&flow rate calibration. Body: ?duty=NN (0-100).
  g_webServer.on("/api/test/pump_continuous", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;

    int duty = -1;
    if (req->hasParam("duty", true)) {
      duty = req->getParam("duty", true)->value().toInt();
    } else if (req->hasParam("duty")) {
      duty = req->getParam("duty")->value().toInt();
    }
    if (duty < 0 || duty > 100) {
      portal_testRespondFail(req, "duty must be 0-100");
      return;
    }
    if (!pump_setContinuous((uint8_t)duty)) {
      portal_testRespondFail(req, "pump refused (overflow latched?)");
      return;
    }
    if (g_portalState) g_portalState->pumpRunning = (duty > 0);
    char msg[64];
    snprintf(msg, sizeof(msg), "pump continuous duty=%d%%", duty);
    portal_testRespondOk(req, msg);
  });

  g_webServer.on("/api/test/pump_off", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    // pump_off is always allowed — it's a safety command.
    pump_stop();
    if (g_portalState) g_portalState->pumpRunning = false;
    portal_testRespondOk(req, "pump stopped");
  });

  g_webServer.on("/api/test/pump_clear_overflow", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    // Refuse if FLOAT_OVF is still physically active — operator must drain first.
  #if ENABLE_OVERFLOW_SWITCH && (PIN_FLOAT_SWITCH_OVERFLOW >= 0)
    if (digitalRead(PIN_FLOAT_SWITCH_OVERFLOW) == LOW) {
      portal_testRespondFail(req, "FLOAT_OVF still active — drain reservoir first");
      return;
    }
  #endif
    pump_clearOverflowLatch();
    portal_testRespondOk(req, "overflow latch cleared");
  });
#endif

#if ENABLE_FAN
  // Fan speed (PCB v2). Body/query: ?duty=NN (0-100). Health (spins/stalls) then
  // surfaces in /api/status fan_spinning/fan_stalled after FAN_TACHO_POLL_MS.
  g_webServer.on("/api/test/fan", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;
    int duty = -1;
    if (req->hasParam("duty", true)) {
      duty = req->getParam("duty", true)->value().toInt();
    } else if (req->hasParam("duty")) {
      duty = req->getParam("duty")->value().toInt();
    }
    if (duty < 0 || duty > 100) {
      portal_testRespondFail(req, "duty must be 0-100");
      return;
    }
    if (!fan_isReady()) {
      portal_testRespondFail(req, "fan not ready");
      return;
    }
    fan_setDutyPct((uint8_t)duty);
    char msg[48];
    snprintf(msg, sizeof(msg), "fan duty=%d%%", duty);
    portal_testRespondOk(req, msg);
  });

  g_webServer.on("/api/test/fan_off", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    // fan_off is always allowed — stopping an actuator is a safe command.
    fan_setDutyPct(0);
    portal_testRespondOk(req, "fan stopped");
  });
#endif

#if ENABLE_LIGHT_RELAY
  g_webServer.on("/api/test/light_toggle", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;
    if (!g_portalConfig) {
      portal_testRespondFail(req, "config not initialized");
      return;
    }
    if (g_portalState && g_portalState->lightsOn) {
      g_portalConfig->lightsForceOn  = false;
      g_portalConfig->lightsForceOff = true;
    } else {
      g_portalConfig->lightsForceOn  = true;
      g_portalConfig->lightsForceOff = false;
    }
    portal_testRespondOk(req, "light toggled");
  });
#endif

#if ENABLE_AIR_PUMP
  g_webServer.on("/api/test/air_pump_toggle", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;
    if (!g_portalState) {
      portal_testRespondFail(req, "state not initialized");
      return;
    }
    bool newState = !g_portalState->airPumpOn;
    power_setAirPump(newState);
    g_portalState->airPumpOn = newState;
    portal_testRespondOk(req, newState ? "air pump on" : "air pump off");
  });
#endif

#if ENABLE_LORA
  g_webServer.on("/api/test/lora_ping", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;
    if (lora_isReady()) {
      portal_testRespondOk(req, "lora ready");
    } else {
      portal_testRespondFail(req, "lora not ready");
    }
  });
#endif

#if ENABLE_EINK_DISPLAY
  g_webServer.on("/api/test/eink_redraw", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;
    ux_forceRefresh();
    portal_testRespondOk(req, "eink refresh requested");
  });
#endif

#if ENABLE_UX_INDICATOR
  g_webServer.on("/api/test/led_cycle", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!portal_requireAuthorized(req)) return;
    if (!portal_testGuardState(req)) return;
    // Cycle LEDs: green 500ms → yellow 500ms → red 500ms → off
    // delay() is acceptable here: this is a manual test endpoint, not main-loop code.
    // Skip any LED whose pin is -1 (V1: only green present).
    if (PIN_LED_GREEN >= 0) {
      digitalWrite(PIN_LED_GREEN, LED_ACTIVE_LOW ? LOW : HIGH); delay(500);
      digitalWrite(PIN_LED_GREEN, LED_ACTIVE_LOW ? HIGH : LOW);
    }
    if (PIN_LED_YELLOW >= 0) {
      digitalWrite(PIN_LED_YELLOW, LED_ACTIVE_LOW ? LOW : HIGH); delay(500);
      digitalWrite(PIN_LED_YELLOW, LED_ACTIVE_LOW ? HIGH : LOW);
    }
    if (PIN_LED_RED >= 0) {
      digitalWrite(PIN_LED_RED, LED_ACTIVE_LOW ? LOW : HIGH); delay(500);
      digitalWrite(PIN_LED_RED, LED_ACTIVE_LOW ? HIGH : LOW);
    }
    ux_forceRefresh();
    portal_testRespondOk(req, "led cycle done");
  });
#endif
}

#else  // ENABLE_API_TEST_ENDPOINTS

static void portal_registerTestApi() {}

#endif // ENABLE_API_TEST_ENDPOINTS
#endif // WIFI_PORTAL_API_TEST_H
