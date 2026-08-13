/*=====================================================================
  wifi_portal.h - WiFi AP + Web Configuration Portal

  Starts a SoftAP for WIFI_AP_DURATION_MS after boot.
  Serves mobile-friendly web UI for configuration.
  Can be re-activated via LoRa command or button press.

  Requires: ESPAsyncWebServer, AsyncTCP, ArduinoJson
=====================================================================*/

#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include "config.h"
#include "structs.h"
#include "calibration_math.h"
#include "command_validation.h"
#include "api_contract_validation.h"
#include "grow_guidance.h"
#include "wifi_portal_html.h"
#include "ota_manager.h"

#if ENABLE_WIFI_PORTAL

#include "api_envelope.h"

// Forward declaration
void portal_stop();

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#if ENABLE_CAPTIVE_PORTAL
#include <DNSServer.h>
#endif

static AsyncWebServer g_webServer(WIFI_WEB_PORT);
static bool g_portalActive = false;
static unsigned long g_portalStartMs = 0;

#if ENABLE_CAPTIVE_PORTAL
// Captive portal: wildcard-DNS AP-clienteille → setup-sivu avautuu itsestään.
// TUOTEPAATOS 15.7.2026 (rautatestattu puhelimella): DNS pysyy paalla MYOS
// kayttoonoton jalkeen. Sivuvaikutus on OS:n "sign in" -ilmoitus joka
// AP-liitynnalla, mutta valiton portaalin avautuminen voittaa sen — alaa
// gateta tata onboardingComplete-lippuun (kokeiltiin, kayttaja hylkasi).
static DNSServer g_dnsServer;
static bool g_dnsActive = false;
#endif

// External references (set by main program)
static SensorData* g_portalSensors = NULL;
static SystemState* g_portalState = NULL;
static DeviceConfig* g_portalConfig = NULL;
static CalibrationData* g_portalCalibration = NULL;
static PlantConfig* g_portalCurrentPlant = NULL;

#include "wifi_portal_auth.h"

// Callbacks for commands
typedef void (*PortalCommandCallback)(const char* cmd, const char* value);
static PortalCommandCallback g_portalCmdCallback = NULL;

#if ENABLE_GUIDED_GROWING_UI && ENABLE_GUIDED_GROWING_SIMULATION
static int8_t g_portalSimPhaseOverride = -1;
static int16_t g_portalSimDayOverride = -1;
#endif

static bool portal_dispatchCommand(AsyncWebServerRequest* req, const char* cmd, const char* value) {
  char err[96];
  if (!portal_validateCommand(cmd, value, err, sizeof(err))) {
    api_sendEnvelope(req, 400, false, false, err, "cmd");
    return false;
  }

  if (!g_portalCmdCallback) {
    api_sendEnvelope(req, 500, false, false, "command callback missing");
    return false;
  }
  g_portalCmdCallback(cmd, value ? value : "");
  return true;
}

// ── Route handlers ──────────────────────────────────────────────

#if ENABLE_GUIDED_GROWING_UI
static const char* portal_growStartMethodName(uint8_t method) {
  switch (method) {
    case 1: return "siemen";
    case 2: return "ostotaimi";
    default: return "pistokas";
  }
}

static const char* portal_growPhaseTypeName(GrowPhaseType type) {
  switch (type) {
    case GROW_PHASE_ROOTING: return "juurtuminen";
    case GROW_PHASE_SEEDLING: return "taimivaihe";
    case GROW_PHASE_VEGETATIVE: return "kasvuvaihe";
    case GROW_PHASE_HARVEST: return "sadonkorjuu";
    case GROW_PHASE_CUSTOM: return "oma";
    default: return "tuntematon";
  }
}

static const char* portal_growInstructionText(GrowPhaseType type, uint8_t startMethod) {
  switch (type) {
    case GROW_PHASE_ROOTING:
      return (startMethod == 0)
        ? "Pidä korkea kosteus ja kevyt valo. Pistokkaalle riittää puhdas vesi ilman ravinteita."
        : "Pidä kasvuympäristö kosteana vakaana ja varmista juurten muodostuminen.";
    case GROW_PHASE_SEEDLING:
      return "Pidä kansi tai kasvihuone paikoillaan, avaa päivittäin hetkeksi tuuletukseen."
             " Anna mieto ravinne ja vältä liiallista kastelua.";
    case GROW_PHASE_VEGETATIVE:
      return "Siirrä taimi vakaaseen valoon. Nosta ravinnetaso tavoite-PPM arvoon ja seuraa veden kulutusta.";
    case GROW_PHASE_HARVEST:
      return "Korjaa satoa vaiheittain. Pidä valo ja ravinne vakaana, jotta uusi kasvu jatkuu tasaisesti.";
    default:
      return "Seuraa lämpötilaa, valaistusta ja kastelua päivittäin. Tee muutokset pienissä askelissa.";
  }
}

static uint8_t portal_pickSimulatedPhase(const PlantConfig* plant, uint16_t* dayInPhaseOut) {
  if (!plant || plant->phaseCount == 0) {
    *dayInPhaseOut = 0;
    return 0;
  }

#if ENABLE_GUIDED_GROWING_SIMULATION
  if (g_portalSimPhaseOverride >= 0 && g_portalSimPhaseOverride < plant->phaseCount) {
    *dayInPhaseOut = (g_portalSimDayOverride > 0) ? (uint16_t)g_portalSimDayOverride : 1;
    return (uint8_t)g_portalSimPhaseOverride;
  }
#endif

  unsigned long simDay = (millis() / 15000UL) % 56UL;
  unsigned long accDays = 0;

  for (uint8_t i = 0; i < plant->phaseCount; i++) {
    uint16_t phaseDays = plant->phases[i].durationDays;
    if (phaseDays == 0) {
      *dayInPhaseOut = (uint16_t)(simDay - accDays + 1);
      return i;
    }
    if (simDay < accDays + phaseDays) {
      *dayInPhaseOut = (uint16_t)(simDay - accDays + 1);
      return i;
    }
    accDays += phaseDays;
  }

  *dayInPhaseOut = 1;
  return (uint8_t)(plant->phaseCount - 1);
}
#endif

// Globals consumed by the API routes below — defined here so the
// routes can read them, plus by the STA/mDNS management code further
// down. Static-with-initializer at file scope is single-definition in
// C++ even when read from multiple translation units (header-only
// build).
static bool g_mdnsStarted = false;
static unsigned long g_wifiConnectAttemptMs = 0;
static unsigned long g_mdnsLastAttemptMs = 0;
static const unsigned long WIFI_CONNECT_ATTEMPT_INTERVAL = 5000;
static const unsigned long MDNS_RETRY_INTERVAL_MS = 10000;

#include "wifi_portal_routes_post.h"
#include "wifi_portal_routes_get.h"
#include "wifi_portal_api_state.h"
#include "wifi_portal_api_test.h"


static void portal_setupRoutes() {
  // Paasivu — kayttoonoton aikana juuri ohjaa /setup-sivulle, jotta ummikko
  // ei putoa asiantuntijasivulle (captive-DNS tuo AP-clientit juureen ->
  // redirect vie suoraan oikeaan polkuun). Portti on silti laitteessa
  // (onboarding.h): osoiterivin kautta / on yha saavutettavissa, mutta
  // /api/onboarding/complete hylkaa vajaan kuittauksen joka tapauksessa.
  g_webServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (g_portalConfig && !g_portalConfig->onboardingComplete) {
      req->redirect("/setup");
      return;
    }
    portal_sendHtml(req);
  });

  // /setup — oma onboarding-sivu. Kuitatun kayttoonoton jalkeen ohjaa
  // juureen (sivu itsekin siirtyy pois heti kun status kertoo valmiin).
  g_webServer.on("/setup", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (g_portalConfig && g_portalConfig->onboardingComplete) {
      req->redirect("/");
      return;
    }
    portal_sendSetupHtml(req);
  });

  portal_registerStatusGetRoute();
  portal_registerGrowGetRoute();
  portal_registerPlantGetRoutes();

  // POST routes — register JSON handlers via AsyncCallbackJsonWebHandler.
  //
  // ORDER IS LOAD-BEARING. ESPAsyncWebServer matches URIs with its
  // BackwardCompatible matcher, documented in ESPAsyncWebServer.h as the
  // regex ^{_uri}(/.*)?$ — a PATH-SEGMENT prefix, first registration wins.
  // So "/api/calib" registered before "/api/calib/pump/test" swallows it.
  // (Plain string prefixes are safe: "/api/test/fan" does NOT capture
  // "/api/test/fan_off", because there is no "/" boundary.)
  //
  // This is not theory. Until 11.8.2026 "/api/config" was registered before
  // "/api/config/reset", so every reset request landed in the general config
  // handler instead — the factory-defaults button in the portal silently did
  // nothing. Nobody noticed, because a wrong order compiles clean and every
  // test stays green; it only shows up on real hardware. Invariant I7 in
  // scripts/check_invariants.py now enforces this rule, and it is what found
  // that bug.
  //
  // Do not reorder these calls, and do not add a new /api/calib/* route
  // after the /api/calib catch-all.
  portal_registerAuthPostRoutes();
  portal_registerPlantPostRoutes();
  portal_registerConfigPostRoutes();
  portal_registerGrowPostRoutes();
  portal_registerModePostRoute();
  portal_registerOnboardingPostRoute();
  portal_registerCommandPostRoute();
  portal_registerPumpCalibRoutes();
  portal_registerMotorCalibRoutes();
  portal_registerEbbSoakCalibRoutes();
  portal_registerPowerCalibRoutes();
  portal_registerPpfdCalibRoutes();
  portal_registerCalibrationPostRoute();

  portal_registerConfigGetRoute();
  portal_registerCalibrationGetRoute();
  portal_registerHistoryAndEbbRoutes();

  // CORE-F: state snapshot + device history + intents + per-module test endpoints
  portal_registerStateApi();
  portal_registerTestApi();

  // OTA shares this AsyncWebServer instance — must be registered before begin().
  ota_begin(&g_webServer);
}

// Start/refresh mDNS advertisement once STA is connected. Safe to call
// from loop() unconditionally — bails out if WiFi isn't ready, retries
// every MDNS_RETRY_INTERVAL_MS if MDNS.begin() failed previously.
static void mdns_ensure() {
  if (g_mdnsStarted) return;
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();
  if (g_mdnsLastAttemptMs != 0 && (now - g_mdnsLastAttemptMs) < MDNS_RETRY_INTERVAL_MS) {
    return;
  }
  g_mdnsLastAttemptMs = now;

  if (MDNS.begin(WIFI_MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", WIFI_WEB_PORT);
    DEBUG_PRINTF("[INFO]  mDNS: %s.local -> %s:%d\n",
                 WIFI_MDNS_HOSTNAME,
                 WiFi.localIP().toString().c_str(),
                 (int)WIFI_WEB_PORT);
    g_mdnsStarted = true;
  } else {
    DEBUG_WARN(F("mDNS: begin() failed, will retry"));
  }
}

// ── Public API ──────────────────────────────────────────────────

bool portal_init(SensorData* sensors, SystemState* state,
                 DeviceConfig* config, CalibrationData* calibration,
                 PlantConfig* plant,
                 PortalCommandCallback cmdCallback) {
  g_portalSensors = sensors;
  g_portalState = state;
  g_portalConfig = config;
  g_portalCalibration = calibration;
  g_portalCurrentPlant = plant;
  g_portalCmdCallback = cmdCallback;

  portal_authClearSessions();

  // Start AP + STA (dual mode). Hostname must be set BEFORE mode() so
  // both interfaces pick it up; otherwise MDNS.begin() can collide
  // with the framework-default "espressif-XXXXXX" hostname.
  WiFi.setHostname(WIFI_MDNS_HOSTNAME);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CLIENTS);

  DEBUG_PRINTF("[INFO]  WiFi AP: SSID=%s, IP=%s\n",
               WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

#if ENABLE_CAPTIVE_PORTAL
  // Answer every DNS query on the AP with our own IP → phone pops the
  // setup page on join. STA-side DNS is untouched (queries only come from
  // AP clients; the STA interface uses the router's DNS).
  g_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  g_dnsActive = g_dnsServer.start(53, "*", WiFi.softAPIP());
  DEBUG_PRINTF("[INFO]  Captive portal DNS: %s\n", g_dnsActive ? "active" : "FAILED");
#endif

  // Try STA if configured
  if (config && config->wifiAutoConnect && strlen(config->wifiSsid) > 0) {
    DEBUG_PRINTF("[INFO]  WiFi STA: Connecting to %s...\n", config->wifiSsid);
    WiFi.begin(config->wifiSsid, config->wifiPassword);
    g_wifiConnectAttemptMs = millis();
  } else {
    DEBUG_INFO(F("WiFi STA: disabled (not configured)"));
  }

  // Setup web routes
  portal_setupRoutes();

#if ENABLE_CAPTIVE_PORTAL
  // Captive redirect: unknown URLs from the AP interface (incl. OS captive
  // probes like generate_204 / hotspot-detect.html) land on the setup page.
  // Requests arriving via STA get a plain 404 — no redirect loops on the LAN.
  g_webServer.onNotFound([](AsyncWebServerRequest* req) {
    if (req->client() && req->client()->localIP() == WiFi.softAPIP()) {
      req->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
    } else {
      req->send(404, "text/plain", "Not found");
    }
  });
#endif

  g_webServer.begin();

  g_portalActive = true;
  g_portalStartMs = millis();

  DEBUG_PRINTF("[INFO]  Portal: active for %lu minutes\n",
               WIFI_AP_DURATION_MS / 60000);
  return true;
}

// State-independent recovery + connectivity tasks. Call from loop() in EVERY
// iteration regardless of DeviceState — these must keep running in FAULT/
// SHUTDOWN states too. Same pattern as sensors_readFloatSwitches(): safety
// and recovery escape state dispatch.
//
// Why each one is here:
//   - mdns_ensure: e-ink display + phone resolve plantmeister.local even when
//     the device is in FAULT and operator needs to investigate.
//   - ota_loop: drives ElegantOTA.loop() each iteration. The post-upload reboot
//     itself no longer rides ElegantOTA's internal ESP.restart() — that is
//     disabled (ota_manager.h setAutoReboot(false)) because it skips the clean
//     WiFi shutdown and leaves XIAO with zombie radio. The reboot is scheduled
//     in onEnd via reboot_request_schedule() and executed by reboot_request_tick()
//     below — also state-independent — so the new firmware actually boots and STA
//     comes back. Without a gating-safe reboot_request_tick(), ota_upload.py would
//     report "device back online" but uptime would never reset.
//   - STA reconnect: a mid-day WiFi outage during FAULT (or after AP timeout
//     collapses g_portalActive) would otherwise never recover automatically.
void portal_loopCore() {
  mdns_ensure();
  ota_loop();

  // STA reconnect — runs independently of g_portalActive so recovery works
  // after AP timeout and during FAULT. Throttled by WIFI_CONNECT_ATTEMPT_INTERVAL.
  // Logging only on state transitions, not every check (avoid spamming
  // "WiFi STA: connected!" every 5s when stably connected).
  if (g_portalConfig && g_portalConfig->wifiAutoConnect && strlen(g_portalConfig->wifiSsid) > 0) {
    if (millis() - g_wifiConnectAttemptMs >= WIFI_CONNECT_ATTEMPT_INTERVAL) {
      static bool s_wifiStaConnectedLast = false;
      int status = WiFi.status();
      bool nowConnected = (status == WL_CONNECTED);

      if (!nowConnected) {
        DEBUG_PRINTF("[VERB]  WiFi STA: status=%d, retrying...\n", status);
        WiFi.begin(g_portalConfig->wifiSsid, g_portalConfig->wifiPassword);
      } else if (!s_wifiStaConnectedLast) {
        DEBUG_PRINTF("[INFO]  WiFi STA: connected! IP=%s\n", WiFi.localIP().toString().c_str());
      }

      s_wifiStaConnectedLast = nowConnected;
      g_wifiConnectAttemptMs = millis();
    }
  }
}

// State-dependent portal lifecycle. Call from loop_dispatch tasks (wifiPortal).
// Only the AP-timeout decision lives here now — connectivity/recovery moved
// into portal_loopCore() above so they survive FAULT-state dispatch gating.
void portal_update() {
  if (!g_portalActive) return;

#if ENABLE_CAPTIVE_PORTAL
  // Non-blocking: answers at most one queued DNS query per call.
  if (g_dnsActive) g_dnsServer.processNextRequest();
#endif

  // Don't shut the portal down mid-OTA — would kill the upload.
  // Dev-tilassa (WIFI_PORTAL_NO_TIMEOUT) AP + STA pysyvät ikuisesti pystyssä.
  // Test mode (runtime, persistoitu DeviceConfigiin): sama efekti — AP ei sammu,
  // joten manuaalitestaus puhelimella ei katkea 15 min jälkeen.
  bool testModeActive = (g_portalConfig != NULL) && g_portalConfig->testMode;
  // AP:n on pysyttävä pystyssä myös: (a) käyttöönoton aikana — setup-portaali
  // ei saa kadota kesken; (b) AP-only offline -laitteella (käyttöönotettu ilman
  // kotiverkkoa) — AP on laitteen AINOA käyttöliittymä. Ks.
  // docs/kehitys/kayttoonotto-onboarding-kartoitus.md §4.3.
  bool apMustStay = (g_portalConfig != NULL) &&
                    (!g_portalConfig->onboardingComplete ||
                     strlen(g_portalConfig->wifiSsid) == 0);
  if (!WIFI_PORTAL_NO_TIMEOUT && !testModeActive && !apMustStay &&
      millis() - g_portalStartMs >= WIFI_AP_DURATION_MS &&
      !ota_isActive()) {
    portal_stop();
    return;
  }

}

void portal_stop() {
  if (!g_portalActive) return;

#if ENABLE_CAPTIVE_PORTAL
  if (g_dnsActive) {
    g_dnsServer.stop();
    g_dnsActive = false;
  }
#endif

#if WIFI_PORTAL_KEEP_STA
  // Production behaviour: shut down only the SoftAP, keep STA + web
  // server up so the e-ink wall display can keep polling /api/state
  // and the user can reach the portal from the home network.
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  // ESP32 ESPmDNS often fails to advertise reliably while in AP+STA
  // mode; once we collapse to STA-only, restart it so e-ink can
  // resolve plantmeister.local.
  if (g_mdnsStarted) {
    MDNS.end();
    g_mdnsStarted = false;
  }
  g_mdnsLastAttemptMs = 0;  // force mdns_ensure() to retry immediately
  g_portalActive = false;
  DEBUG_INFO(F("Portal: AP stopped, STA + web server stay up"));
#else
  g_webServer.end();
  if (g_mdnsStarted) {
    MDNS.end();
    g_mdnsStarted = false;
  }
  WiFi.mode(WIFI_OFF);

  g_portalActive = false;
  DEBUG_INFO(F("Portal: WiFi stopped (AP+STA)"));
#endif
}

// Re-activate portal (e.g., via LoRa command)
void portal_reactivate() {
  if (g_portalActive) return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CLIENTS);
  g_webServer.begin();

#if ENABLE_CAPTIVE_PORTAL
  g_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  g_dnsActive = g_dnsServer.start(53, "*", WiFi.softAPIP());
#endif

  g_portalActive = true;
  g_portalStartMs = millis();
  g_wifiConnectAttemptMs = millis();

  DEBUG_INFO(F("Portal: re-activated (AP+STA)"));
}

bool portal_isActive() { return g_portalActive; }

#endif // ENABLE_WIFI_PORTAL
#endif // WIFI_PORTAL_H
