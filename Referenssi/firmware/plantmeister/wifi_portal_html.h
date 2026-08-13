/*=====================================================================
  wifi_portal_html.h - Portal HTML composition

  Real content lives in:
    - wifi_portal_html_styles.h
    - wifi_portal_html_body.h
    - wifi_portal_html_script.h

  The portal serves HTML via a runtime concatenation of these parts.
  Concatenation is done at request time to avoid doubling RAM use
  (each part stays in PROGMEM).
=====================================================================*/

#ifndef WIFI_PORTAL_HTML_H
#define WIFI_PORTAL_HTML_H

#include "wifi_portal_html_styles.h"
#include "wifi_portal_html_body.h"
#include "wifi_portal_html_script.h"
#include "wifi_portal_html_setup.h"

#if ENABLE_WIFI_PORTAL
#include <ESPAsyncWebServer.h>

// Helper: write all three parts to AsyncWebServerResponse.
// Used by GET / route in wifi_portal.h.
inline void portal_sendHtml(AsyncWebServerRequest* req) {
  AsyncResponseStream* response = req->beginResponseStream("text/html; charset=utf-8");
  response->print(F("<!DOCTYPE html><html lang=\"fi\"><head><meta charset=\"UTF-8\">"));
  response->print(F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,user-scalable=no\">"));
  response->print(F("<title>PlantMeister</title>"));
  response->print(FPSTR(PORTAL_HTML_STYLES));
  response->print(F("</head>"));
  response->print(FPSTR(PORTAL_HTML_BODY));
  response->print(FPSTR(PORTAL_HTML_SCRIPT));
  response->print(F("</html>"));
  req->send(response);
}

// /setup — oma onboarding-sivu (wifi_portal_html_setup.h). Jakaa saman
// CSS:n paasivun kanssa, joten ilme pysyy yhtenaisena eika tyyleista ole
// kahta kopiota. Reititys + redirect-logiikka: wifi_portal.h.
inline void portal_sendSetupHtml(AsyncWebServerRequest* req) {
  AsyncResponseStream* response = req->beginResponseStream("text/html; charset=utf-8");
  response->print(F("<!DOCTYPE html><html lang=\"fi\"><head><meta charset=\"UTF-8\">"));
  response->print(F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,user-scalable=no\">"));
  response->print(F("<title>PlantMeister — k&auml;ytt&ouml;&ouml;notto</title>"));
  response->print(FPSTR(PORTAL_HTML_STYLES));
  response->print(F("</head>"));
  response->print(FPSTR(PORTAL_SETUP_HTML));
  response->print(F("</html>"));
  req->send(response);
}
#endif // ENABLE_WIFI_PORTAL

#endif // WIFI_PORTAL_HTML_H
