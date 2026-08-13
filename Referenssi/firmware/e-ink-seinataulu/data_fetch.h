/*=====================================================================
  data_fetch.h - HTTP Client for PlantMeister /api/state

  Fetches device + sensor snapshot directly from the PlantMeister XIAO
  firmware (firmware/plantmeister/wifi_portal_api_state.h). No separate
  RPi gateway — the XIAO advertises itself on mDNS (plantmeister.local)
  and serves a hierarchical JSON payload.

  Stores last known data in RTC memory for deep sleep persistence.
=====================================================================*/

#ifndef DATA_FETCH_H
#define DATA_FETCH_H

#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// DisplayData struct now lives in display_data.h (extracted so pure-logic
// modules like resolve_field.h can use it without pulling in WiFi/HTTPClient).
#include "display_data.h"
// JSON -> DisplayData field mapping, extracted so it is unit-testable
// without WiFi/HTTPClient (test/test_data_fetch_parse/).
#include "data_fetch_parse.h"
// M2 SD-historia: appendRow() ratsastaa tallaisella onnistuneella haulla,
// ei uutta pollausta. No-op kun ENABLE_SD_LOGGING on false (oletus).
#include "sd_logger.h"

// RTC memory survives deep sleep
RTC_DATA_ATTR DisplayData g_displayData = {};
RTC_DATA_ATTR int g_bootCount = 0;

// ═══════════════════════════════════════════════════════════════════
// WIFI CONNECTION
// ═══════════════════════════════════════════════════════════════════

bool wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  DEBUG_PRINTF("[INFO]  WiFi: yhdistetaan %s...\n", WIFI_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
      DEBUG_PRINT("WiFi: aikakatkaisu!");
      return false;
    }
    delay(250);
  }

  DEBUG_PRINTF("[INFO]  WiFi: yhdistetty, IP=%s\n",
               WiFi.localIP().toString().c_str());
  return true;
}

void wifi_disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// ═══════════════════════════════════════════════════════════════════
// NTP TIME SYNC
// ═══════════════════════════════════════════════════════════════════

bool time_sync() {
  configTime(NTP_GMT_OFFSET_SEC, NTP_DST_OFFSET_SEC, NTP_SERVER);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {
    DEBUG_PRINT("NTP: aikasynkronointi epaonnistui");
    return false;
  }

  DEBUG_PRINTF("[INFO]  NTP: %02d:%02d:%02d %d.%d.%d\n",
               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
               timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  return true;
}

// ═══════════════════════════════════════════════════════════════════
// API FETCH
// ═══════════════════════════════════════════════════════════════════

// Resolve PLANTMEISTER_HOST. For ".local" names we must use mDNS query
// explicitly — WiFi.hostByName() does plain unicast DNS and fails for
// link-local names unless the router happens to forward them.
static bool resolvePlantmeisterHost(IPAddress* out, bool* viaMdns) {
  if (viaMdns) *viaMdns = false;
  const char* host = PLANTMEISTER_HOST;
  size_t hlen = strlen(host);
  const char* localSuffix = ".local";
  size_t slen = strlen(localSuffix);

  bool isMdns = (hlen > slen) &&
                (strcmp(host + (hlen - slen), localSuffix) == 0);

  if (isMdns) {
    char shortName[40];
    size_t copyLen = hlen - slen;
    if (copyLen >= sizeof(shortName)) copyLen = sizeof(shortName) - 1;
    memcpy(shortName, host, copyLen);
    shortName[copyLen] = '\0';

    if (!MDNS.begin("e-ink-seinataulu")) {
      DEBUG_PRINT("mDNS.begin epaonnistui");
      // fall through to hostByName anyway
    }

    DEBUG_PRINTF("[INFO]  mDNS query: %s\n", shortName);
    IPAddress ip = MDNS.queryHost(shortName, 3000);
    if (ip != IPAddress()) {
      *out = ip;
      if (viaMdns) *viaMdns = true;
      DEBUG_PRINTF("[INFO]  mDNS -> %s\n", ip.toString().c_str());
      return true;
    }
    DEBUG_PRINT("mDNS: ei vastausta, kokeillaan DNS-fallbackia");
  }

  // Fallback: plain DNS (works for IP literals and routed hostnames)
  if (WiFi.hostByName(host, *out) == 1) {
    DEBUG_PRINTF("[INFO]  DNS -> %s\n", out->toString().c_str());
    return true;
  }

  DEBUG_PRINTF("[ERROR] Host-resolusion epaonnistui: %s\n", host);
  return false;
}

bool data_fetch() {
  unsigned long fetchStart = millis();

  IPAddress hostIp;
  bool viaMdns = false;
  if (!resolvePlantmeisterHost(&hostIp, &viaMdns)) {
    g_displayData.fetchFailCount++;
    g_displayData.lastMdnsResolved = false;
    g_displayData.lastResolvedIp[0] = '\0';
    g_displayData.lastHttpCode = -1;
    g_displayData.lastFetchDurationMs = millis() - fetchStart;
    strncpy(g_displayData.lastErrorMsg, "host resolve failed",
            sizeof(g_displayData.lastErrorMsg) - 1);
    g_displayData.lastErrorMsg[sizeof(g_displayData.lastErrorMsg) - 1] = '\0';
    return false;
  }

  g_displayData.lastMdnsResolved = viaMdns;
  strncpy(g_displayData.lastResolvedIp, hostIp.toString().c_str(),
          sizeof(g_displayData.lastResolvedIp) - 1);
  g_displayData.lastResolvedIp[sizeof(g_displayData.lastResolvedIp) - 1] = '\0';

  HTTPClient http;
  char url[128];
  snprintf(url, sizeof(url), "http://%s:%d%s",
           hostIp.toString().c_str(), PLANTMEISTER_PORT, API_STATE_PATH);

  DEBUG_PRINTF("[INFO]  Haetaan: %s\n", url);

  http.begin(url);
  http.setTimeout(API_TIMEOUT_MS);
  int code = http.GET();
  g_displayData.lastHttpCode = code;

  if (code != 200) {
    DEBUG_PRINTF("[WARN]  HTTP virhe: %d\n", code);
    http.end();
    g_displayData.fetchFailCount++;
    g_displayData.lastFetchDurationMs = millis() - fetchStart;
    snprintf(g_displayData.lastErrorMsg, sizeof(g_displayData.lastErrorMsg),
             "HTTP %d", code);
    return false;
  }

  String payload = http.getString();
  http.end();

  // /api/state is hierarchical; enriched payload (device/ux/sensors+PE/motor/
  // growing+step/actuators/ebb/dev) serializes ~2.1 kB. Deserialize doc must be
  // comfortably larger than the PM serialize buffer (3072) — 5 kB gives margin
  // (getString() copies keys+values, not zero-copy). Doc elaa PINOSSA (taskin
  // oletuspino ~8 kB) — ala kasvata <5120>:ta, karsi sisaltoa filtterilla.
  // Filtteri (inkluusiolista) pudottaa mm. sensors.light_spectrum:n; rakennus +
  // avainlista ovat data_fetch_parse.h data_buildStateFilter():ssa (natiivitesti
  // test_state_filter vartioi ettei se ylivuoda -> ei "--"-regressiota).
  //
  // Rakennetaan KERRAN: createNestedObject("sensors") luo uuden objektin joka
  // kutsulla, joten toistuva rakennus samaan static-doc:iin kasvattaisi sita.
  // <2048> static elaa BSS:ssa (ei fetchin pinobudjetista).
  static StaticJsonDocument<2048> filter;
  static bool filterReady = false;
  if (!filterReady) {
    data_buildStateFilter(filter);
    filterReady = true;
    if (filter.overflowed()) {
      DEBUG_PRINT("VAROITUS: /api/state-filtteri ylivuoti - sensoreita pudonnut, kasvata kokoa");
    }
  }

  StaticJsonDocument<5120> doc;
  DeserializationError err = deserializeJson(doc, payload,
                                             DeserializationOption::Filter(filter));
  if (err) {
    DEBUG_PRINTF("[ERROR] JSON virhe: %s\n", err.c_str());
    g_displayData.fetchFailCount++;
    g_displayData.lastFetchDurationMs = millis() - fetchStart;
    snprintf(g_displayData.lastErrorMsg, sizeof(g_displayData.lastErrorMsg),
             "JSON: %s", err.c_str());
    return false;
  }

  data_fetch_populateFromJson(doc, &g_displayData);

  // M2: kirjaa rivi juuri parsitusta datasta - loggaus ratsastaa taman
  // hakusyklin paalla, ei uutta pollausta eika uutta verkkoliikennetta.
  sdlog_appendRow(&g_displayData);

  // Get current time as epoch
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1000)) {
    time_t now;
    time(&now);
    g_displayData.lastFetchEpoch = (unsigned long)now;
  }

  g_displayData.dataValid = true;
  g_displayData.fetchFailCount = 0;
  g_displayData.lastFetchDurationMs = millis() - fetchStart;
  g_displayData.lastErrorMsg[0] = '\0';

  DEBUG_PRINT("Data haettu onnistuneesti");
  return true;
}

// Helper: minutes since last fetch
int data_minutesSinceUpdate() {
  if (!g_displayData.dataValid) return -1;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) return -1;

  time_t now;
  time(&now);
  return (int)difftime(now, (time_t)g_displayData.lastFetchEpoch) / 60;
}

#endif // DATA_FETCH_H
