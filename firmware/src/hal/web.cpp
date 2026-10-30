// Suckboxx ESP32-liima - web-palvelin ja SSE. Ks. web.h.
//
// AsyncEventSourcen säieturva (ESPAsyncWebServer 3.6.0, tarkistettu lähteestä
// .pio/libdeps/esp32dev/ESPAsyncWebServer/src/AsyncEventSource.cpp):
//  - send() lukitsee asiakaslistan (_client_queue_lock, rivi 402) ja kunkin
//    asiakkaan viestijonon (_lockmq, rivi 228). Listan ja jonojen muokkaus
//    toisesta taskista on siis suojattu (AsyncEventSource.h:216-219).
//  - Suojaamatta jää yhteyden katkeaminen: _onDisconnect nollaa _client-
//    osoittimen ilman lukkoa (rivit 286-291), ja vasta sen jälkeen
//    _handleDisconnect ottaa listalukon (rivi 357). Jos send() on samaan aikaan
//    asiakkaan _queueMessagessa tai _runQueuessa, se lukee _clientin uudelleen
//    (rivit 239, 312, 324) ja voi saada nullptr:n -> kaatuminen.
//  - Siksi send() ja count() kutsutaan vain sbx_push-taskista, joka on samalla
//    ytimellä kuin async_tcp (0) ja sitä korkeammalla prioriteetilla. Silloin
//    async_tcp ei pääse käsittelemään katkeamista kesken send():n. Ainoa aukko:
//    sbx_push odottaa lwIP:tä (tcpip_api_call, AsyncTCP.cpp:552) ja tcpip-taski
//    (prio 18, ydin 0) jää itse odottamaan täyttä async-jonoa (AsyncTCP.cpp:148-150,
//    CONFIG_ASYNC_TCP_QUEUE_SIZE 64). Silloin async_tcp voi ajaa. Jäännösriski on
//    kirjaston ominaisuus (raportoitu).
//  - _handleDisconnect poistaa listasta kesken for-silmukan ja kasvattaa sen
//    jälkeen mitätöityä iteraattoria (rivit 359-362). Kirjaston vika, ei
//    kierrettävissä täältä; korjattu kirjaston uudemmissa versioissa (break).
//  - events.close():a ei kutsuta koskaan: se pitää listalukkoa (rivi 371) ja
//    asiakkaan sulkeminen päätyy _handleDisconnectiin ottamaan saman
//    ei-rekursiivisen std::mutexin (rivi 357) -> lukkiutuminen.
//  - avgPacketsWaiting():a ei kutsuta: se jakaa yhdistettyjen asiakkaiden
//    määrällä (rivit 380-396), joka on 0, kun listassa on vain sulkeutumassa
//    oleva asiakas. AsyncClient::connected() on epätosi heti, kun lwIP-taski
//    on käsitellyt FINin (_pcb = NULL, AsyncTCP.cpp:1044-1062, 1365-1370), mutta
//    async_tcp poistaa asiakkaan listasta vasta myöhemmin -> jako nollalla.
//
// Hidas asiakas (puhelin Wi-Fin reunalla): viesti pysyy asiakaskohtaisessa
// jonossa, kunnes TCP-kuittaus tulee (_onAck, rivit 245-269). Uusi viesti lähtee
// heti vain, jos jonossa on alle SSE_MAX_QUEUED_MESSAGES/4 viestiä (rivi 239),
// muuten kuittauksen tai 500 ms:n pollin yhteydessä. Täysi jono hylkää uusimman
// viestin (rivit 217-223) - vanhat jäävät. Siksi SSE_MAX_QUEUED_MESSAGES = 8
// (platformio.ini): enintään ~0,5 s vanhaa dataa oletuksen 32 (~2 s) sijaan.

#include "hal/web.h"

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <esp_timer.h>

#include <atomic>

#include "hal/diag_page.h"
#include "hal/hal_config.h"

// Yllä oleva analyysi koskee täsmälleen näitä versioita (SUUNNITTELU.md D9).
static_assert(ASYNCWEBSERVER_VERSION_MAJOR == 3 && ASYNCWEBSERVER_VERSION_MINOR == 6 &&
                  ASYNCWEBSERVER_VERSION_REVISION == 0,
              "ESPAsyncWebServer vaihtui: tarkista AsyncEventSourcen säieturva uudelleen");
static_assert(ASYNCTCP_VERSION_MAJOR == 3 && ASYNCTCP_VERSION_MINOR == 3 &&
                  ASYNCTCP_VERSION_REVISION == 2,
              "AsyncTCP vaihtui: tarkista säieturva uudelleen");
static_assert(CONFIG_ASYNC_TCP_RUNNING_CORE == 0, "async_tcp kuuluu ytimelle 0 (§4.9)");
// Alle 8:lla ehto size < SSE_MAX_QUEUED_MESSAGES >> 2 ei koskaan toteudu, eikä
// uusi viesti lähtisi heti vaan vasta 500 ms:n pollissa.
static_assert(SSE_MAX_QUEUED_MESSAGES >= 8, "SSE-jono liian lyhyt (AsyncEventSource.cpp:239)");

namespace sbx {
namespace hal {
namespace {

constexpr int kPushCore = CONFIG_ASYNC_TCP_RUNNING_CORE;
constexpr int kPushPrio = CONFIG_ASYNC_TCP_PRIORITY + 1;  // 11 (AsyncTCP.h:67-69: 10)
static_assert(kPushPrio < configMAX_PRIORITIES, "sbx_push-prioriteetti");

AsyncWebServer* g_server = nullptr;
AsyncEventSource* g_events = nullptr;
JsonBuilder g_diag_json = nullptr;
TaskHandle_t g_push_task = nullptr;
char g_push_buf[kJsonBufLen];  // vain sbx_push käyttää

std::atomic<uint32_t> g_clients{0};
std::atomic<uint32_t> g_sent{0};
std::atomic<uint32_t> g_partial{0};
std::atomic<uint32_t> g_dropped{0};
std::atomic<uint32_t> g_build_fail{0};
std::atomic<uint32_t> g_push_us_last{0};
std::atomic<uint32_t> g_push_us_max{0};

// --- Käsittelijät (async_tcp-taski, ydin 0) ----------------------------------
// Eivät kutsu g_eventsiä eivätkä odota mitään pitkää: async_tcp on task
// watchdogin alla (AsyncTCP.cpp:299-310, 5 s).

void handleRoot(AsyncWebServerRequest* req) {
  // AsyncProgmemResponse lukee suoraan flashista, ei kopiota RAMiin.
  AsyncWebServerResponse* r =
      req->beginResponse(200, "text/html; charset=utf-8", reinterpret_cast<const uint8_t*>(kDiagPage),
                         sizeof(kDiagPage) - 1);
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

void handleDiag(AsyncWebServerRequest* req) {
  char buf[kJsonBufLen];  // async_tcp:n pino on 16 kt (AsyncTCP.h:63-65)
  const size_t n = g_diag_json != nullptr ? g_diag_json(buf, sizeof(buf)) : 0;
  if (n == 0) {
    req->send(500, "text/plain; charset=utf-8", "diagnostiikka ei mahtunut puskuriin");
    return;
  }
  // AsyncBasicResponse kopioi sisällön omaan Stringiinsä (WebResponses.cpp:254-264).
  AsyncWebServerResponse* r = req->beginResponse(200, "application/json", buf);
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

void handleNotFound(AsyncWebServerRequest* req) {
  req->send(404, "text/plain; charset=utf-8", "ei löydy");
}

// Reitit rekisteröidään ennen begin()iä: palvelimen käsittelijälistaa ei saa
// muokata, kun async_tcp jo käy sitä läpi (WebServer.cpp:149-158).
void registerRoutes(AsyncWebServer& s) {
  s.on("/", HTTP_GET, handleRoot);
  s.on("/api/diag", HTTP_GET, handleDiag);
  // 7b: §5:n reitit tähän (/api/config, /api/master, /api/cal/*, /api/raw.csv, /api/sim).
  s.onNotFound(handleNotFound);
}

// --- SSE-lähetys (sbx_push, ydin 0, prio async_tcp + 1) -----------------------

void pushLoop(void*) {
  constexpr uint32_t kPeriodUs = 1000000u / kPushHz;  // 66 666 µs -> jaksot 66/67 ms
  TickType_t wake = xTaskGetTickCount();
  uint32_t frac_us = 0;
  for (;;) {
    frac_us += kPeriodUs;
    const TickType_t ticks = frac_us / 1000u;
    frac_us -= ticks * 1000u;
    vTaskDelayUntil(&wake, ticks);
    // Jäljessä yli jakson: ankkuroidaan uudelleen, ettei synny viestipursketta.
    if (static_cast<TickType_t>(xTaskGetTickCount() - wake) > ticks) wake = xTaskGetTickCount();

    const int64_t t0 = esp_timer_get_time();
    const uint32_t clients = static_cast<uint32_t>(g_events->count());
    g_clients.store(clients, std::memory_order_relaxed);
    if (clients == 0) continue;  // ei rakenneta JSONia kenellekään
    // 7b: tilalle snapshotJson(processor, meta, ...) Processorin mutexin alla
    // juuri ennen lähetystä, jotta aaltojono tyhjenee täsmälleen kerran per viesti.
    const size_t n = g_diag_json(g_push_buf, sizeof(g_push_buf));
    if (n == 0) {
      g_build_fail.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    switch (g_events->send(g_push_buf, "diag", 0, 0)) {
      case AsyncEventSource::ENQUEUED:
        g_sent.fetch_add(1, std::memory_order_relaxed);
        break;
      case AsyncEventSource::PARTIALLY_ENQUEUED:
        g_partial.fetch_add(1, std::memory_order_relaxed);
        break;
      case AsyncEventSource::DISCARDED:
      default:
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    g_push_us_last.store(us, std::memory_order_relaxed);
    if (us > g_push_us_max.load(std::memory_order_relaxed)) {
      g_push_us_max.store(us, std::memory_order_relaxed);
    }
  }
}

}  // namespace

bool webStart(JsonBuilder diag_json) {
  if (g_server != nullptr || diag_json == nullptr) return false;
  g_diag_json = diag_json;
  g_server = new AsyncWebServer(kHttpPort);
  g_events = new AsyncEventSource("/events");
  // onConnect ajetaan async_tcp:ssä _addClientin sisällä listalukon alla
  // (AsyncEventSource.cpp:340-351), joten asiakkaan send() on tässä turvallinen.
  // retry: lyhentää selaimen uudelleenyhdistysviivettä katkon jälkeen.
  g_events->onConnect([](AsyncEventSourceClient* c) { c->send("{}", "hello", 0, kSseRetryMs); });
  g_server->addHandler(g_events);  // palvelin omistaa (unique_ptr, ESPAsyncWebServer.h:822)
  registerRoutes(*g_server);
  g_server->begin();
  return xTaskCreatePinnedToCore(pushLoop, "sbx_push", kPushStack, nullptr,
                                 static_cast<UBaseType_t>(kPushPrio), &g_push_task, kPushCore) == pdPASS;
}

void webStats(WebStats& out) {
  out.sse_clients = g_clients.load(std::memory_order_relaxed);
  out.sse_sent = g_sent.load(std::memory_order_relaxed);
  out.sse_partial = g_partial.load(std::memory_order_relaxed);
  out.sse_dropped = g_dropped.load(std::memory_order_relaxed);
  out.build_fail = g_build_fail.load(std::memory_order_relaxed);
  out.push_us_last = g_push_us_last.load(std::memory_order_relaxed);
}

uint32_t webTakePushMax() { return g_push_us_max.exchange(0, std::memory_order_relaxed); }

TaskHandle_t webPushTask() { return g_push_task; }

}  // namespace hal
}  // namespace sbx
