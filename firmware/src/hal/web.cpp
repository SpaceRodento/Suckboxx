// Suckboxx ESP32-liima - web-palvelin ja SSE. Ks. web.h.
//
// AsyncEventSourcen säieturva (ESPAsyncWebServer 3.12.1 + AsyncTCP 3.5.0,
// tarkistettu lähteestä 31.10.2026, WP-13). Rivinumerot viittaavat tiedostoon
// .pio/libdeps/esp32dev/ESPAsyncWebServer/src/AsyncEventSource.cpp, ellei toisin
// mainita; AsyncTCP.cpp = .pio/libdeps/esp32dev/AsyncTCP/src/AsyncTCP.cpp.
//  - send() lukitsee asiakaslistan (_client_queue_lock, rivi 414) ja kunkin
//    asiakkaan viestijonon (_lockmq, rivi 226). Lukot ovat rekursiivisia
//    (ESPAsyncWebServer.h:265). Listan ja jonojen muokkaus toisesta taskista on
//    siis suojattu.
//  - Suojaamatta jää yhä yhteyden katkeaminen: _onDisconnect nollaa _client-
//    osoittimen ilman lukkoa (rivit 292-298), ja vasta sen jälkeen
//    _handleDisconnect ottaa listalukon (rivi 371). 3.9.5:n null-tarkistukset
//    (rivit 233, 246, 333) kaventavat ikkunaa, mutta tarkistus ja käyttö
//    ovat erillisiä lukuja (rivit 246, 333-334), joten send() toisesta taskista
//    samaan aikaan on yhä datakilpa. AsyncClient-olio elää, kunnes
//    _handleDisconnect on saanut listalukon (delete c vasta _onDisconnectin
//    jälkeen, rivi 179).
//  - Siksi send() ja count() kutsutaan vain sbx_push-taskista, joka on samalla
//    ytimellä kuin async_tcp (0) ja sitä korkeammalla prioriteetilla. Silloin
//    async_tcp ei pääse käsittelemään katkeamista kesken send():n. Kesken send():n
//    sbx_push voi odottaa vain lukkoja, jotka async_tcp vapauttaa perittyään
//    prioriteetin (listalukko _addClientissa ja _handleDisconnectissa, rivit 361,
//    371; _lockmq _onAckissa ja _onPollissa, rivit 254, 280), ja lwIP:tä
//    (tcpip_api_call, AsyncTCP.cpp:594, 616). AsyncTCP 3.3.2:ssa tcpip-taski saattoi
//    itse jäädä odottamaan täyttä tapahtumajonoa, jolloin async_tcp pääsi ajoon;
//    3.5.0:ssa jono on rajaton lista (AsyncTCP.cpp:201, 220-226), ja tcpip-taski
//    odottaa enintään jonon mutexia (AsyncTCP.cpp:183-198), jota async_tcp pitää
//    vain jonon käsittelyssä ilman takaisinkutsuja (AsyncTCP.cpp:236-296).
//  - Jos sbx_push herää kesken async_tcp:n katkeamiskäsittelyn, asiakas ohitetaan:
//    AsyncClient::_pcb on NULL ennen _onDisconnectia kaikilla palvelimen poluilla
//    (sulkeminen ja FIN: _fin -> close -> AsyncTCP.cpp:660; virhe: :512), joten
//    connected() (AsyncTCP.cpp:1387-1392, vaatii ESTABLISHED-tilan) on epätosi, ja
//    send() kirjoittaa vain yhdistetyille (rivi 418).
//  - events.close():a ei kutsuta koskaan. 3.7.7:stä lähtien se ei enää lukkiudu
//    (rekursiivinen mutex), mutta AsyncClient::close() kutsuu _onDisconnectin
//    synkronisesti (AsyncTCP.cpp:940-945, 1016-1023), ja _handleDisconnect poistaa
//    asiakkaan listasta (rivi 374) kesken close():n for-silmukan (rivit 386-395)
//    -> mitätöity iteraattori.
//  - Kirjastossa korjattu, ei kiertotietä täällä: _handleDisconnectin iteraattori
//    (break, rivi 375; 3.7.0) ja avgPacketsWaiting():n jako nollalla (rivi 409;
//    3.9.5). avgPacketsWaiting():a ei tarvita.
//
// Hidas asiakas (puhelin Wi-Fin reunalla): viesti pysyy asiakaskohtaisessa
// jonossa, kunnes TCP-kuittaus tulee (_onAck, rivit 252-276). Uusi viesti lähtee
// heti vain, jos jonossa on alle SSE_MAX_QUEUED_MESSAGES/4 viestiä (rivi 246),
// muuten kuittauksen tai 500 ms:n pollin yhteydessä (_onPoll, rivit 278-284;
// AsyncTCP.cpp:109). Täysi jono hylkää uusimman viestin (rivit 228-231) - vanhat
// jäävät. Siksi SSE_MAX_QUEUED_MESSAGES = 8 (platformio.ini): enintään ~0,5 s
// vanhaa dataa oletuksen 32 (~2 s) sijaan.

#include "hal/web.h"

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <esp_timer.h>

#include <atomic>
#include <memory>

#include "core/form.h"
#include "core/json_writer.h"
#include "core/sim_api.h"
#include "core/snapshot.h"
#include "hal/diag_page.h"
#include "hal/hal_config.h"
#include "hal/proc_task.h"
#include "hal/sampler.h"
#include "web/web_page.h"

// Yllä oleva analyysi koskee täsmälleen näitä versioita (SUUNNITTELU.md D9).
static_assert(ASYNCWEBSERVER_VERSION_NUM == ASYNCWEBSERVER_VERSION_VAL(3, 12, 1),
              "ESPAsyncWebServer vaihtui: tarkista AsyncEventSourcen säieturva uudelleen");
static_assert(ASYNCTCP_VERSION_NUM == ASYNCTCP_VERSION_VAL(3, 5, 0),
              "AsyncTCP vaihtui: tarkista säieturva uudelleen");
static_assert(CONFIG_ASYNC_TCP_RUNNING_CORE == 0, "async_tcp kuuluu ytimelle 0 (§4.9)");
// Alle 8:lla ehto size < SSE_MAX_QUEUED_MESSAGES >> 2 ei koskaan toteudu, eikä
// uusi viesti lähtisi heti vaan vasta 500 ms:n pollissa.
static_assert(SSE_MAX_QUEUED_MESSAGES >= 8, "SSE-jono liian lyhyt (AsyncEventSource.cpp:246)");

namespace sbx {
namespace hal {
namespace {

constexpr int kPushCore = CONFIG_ASYNC_TCP_RUNNING_CORE;
constexpr int kPushPrio = CONFIG_ASYNC_TCP_PRIORITY + 1;  // 11 (AsyncTCP.h:49-51: 10)
static_assert(kPushPrio < configMAX_PRIORITIES, "sbx_push-prioriteetti");

AsyncWebServer* g_server = nullptr;
AsyncEventSource* g_events = nullptr;
JsonBuilder g_diag_json = nullptr;
TaskHandle_t g_push_task = nullptr;
char g_push_buf[kJsonBufLen];       // vain sbx_push käyttää (diag)
char g_snap_buf[kSnapshotJsonSize];  // vain sbx_push käyttää (snap)
// Diagnostiikkasivun "diag" lähetetään joka kolmas kierros (5 Hz): /diag on
// huoltonäkymä, ja pääsivun snap on tärkeämpi kaistalle.
constexpr uint32_t kDiagEvery = 3;

std::atomic<uint32_t> g_clients{0};
std::atomic<uint32_t> g_sent{0};
std::atomic<uint32_t> g_partial{0};
std::atomic<uint32_t> g_dropped{0};
std::atomic<uint32_t> g_build_fail{0};
std::atomic<uint32_t> g_push_us_last{0};
std::atomic<uint32_t> g_push_us_max{0};

// --- Käsittelijät (async_tcp-taski, ydin 0) ----------------------------------
// Eivät kutsu g_eventsiä eivätkä odota mitään pitkää: async_tcp on task
// watchdogin alla (AsyncTCP.cpp:331-352, 5 s).

void sendPage(AsyncWebServerRequest* req, const char* page, size_t len) {
  // AsyncProgmemResponse lukee suoraan flashista, ei kopiota RAMiin.
  AsyncWebServerResponse* r = req->beginResponse(200, "text/html; charset=utf-8",
                                                 reinterpret_cast<const uint8_t*>(page), len);
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

void handleRoot(AsyncWebServerRequest* req) { sendPage(req, WEB_PAGE_HTML, sizeof(WEB_PAGE_HTML) - 1); }
void handleDiagPage(AsyncWebServerRequest* req) { sendPage(req, kDiagPage, sizeof(kDiagPage) - 1); }

void sendJson(AsyncWebServerRequest* req, const char* json) {
  // AsyncBasicResponse kopioi sisällön omaan Stringiinsä (WebRequest.cpp:1208-1213,
  // WebResponses.cpp:260-271).
  AsyncWebServerResponse* r = req->beginResponse(200, "application/json", json);
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

// {"ok":ok,"err":err} tai {"ok":ok,"msg":msg} (§5, mock_server.py). text = nullptr -> pelkkä ok.
void sendOk(AsyncWebServerRequest* req, bool ok, const char* key = nullptr, const char* text = nullptr) {
  char buf[512];
  JsonWriter w(buf, sizeof(buf));
  w.beginObject();
  w.key("ok").value(ok);
  if (key != nullptr) w.key(key).value(text != nullptr ? text : "");
  w.endObject();
  sendJson(req, w.ok() ? buf : "{\"ok\":false,\"err\":\"vastaus ei mahtunut\"}");
}

// Lomakkeen parit (POST-runko, form-urlencoded) coren FormPair-muotoon. Merkkijonot
// ovat pyynnön omia ja elävät käsittelijän ajan.
constexpr int kMaxPairs = 64;
int collectForm(AsyncWebServerRequest* req, FormPair* out) {
  int n = 0;
  const size_t count = req->params();
  for (size_t i = 0; i < count && n < kMaxPairs; ++i) {
    const AsyncWebParameter* p = req->getParam(i);
    if (p == nullptr || !p->isPost() || p->isFile()) continue;
    out[n].key = p->name().c_str();
    out[n].value = p->value().c_str();
    ++n;
  }
  return n;
}

void handleConfigGet(AsyncWebServerRequest* req) {
  std::unique_ptr<char[]> buf(new (std::nothrow) char[kConfigJsonSize]);
  if (!buf) {
    req->send(503, "text/plain; charset=utf-8", "muisti loppui");
    return;
  }
  procLock();
  const size_t n = processor().configJson(buf.get(), kConfigJsonSize);
  procUnlock();
  if (n == 0) {
    req->send(500, "text/plain; charset=utf-8", "konfiguraatio ei mahtunut puskuriin");
    return;
  }
  sendJson(req, buf.get());
}

void handleConfigSet(AsyncWebServerRequest* req) {
  FormPair pairs[kMaxPairs];
  const int n = collectForm(req, pairs);
  char err[kFormErrLen];
  procLock();
  const bool ok = processor().applyConfigForm(pairs, n, err, sizeof(err));
  procUnlock();
  sendOk(req, ok, ok ? nullptr : "err", err);
}

void handleConfigReset(AsyncWebServerRequest* req) {
  // resetConfig palauttaa myös lähteen simulaattoriksi. Laitteella lähteen
  // oletus on käännöksen (SBX_SAMPLE_SOURCE), jotta asetusten nollaus ei
  // vaihda oikeita antureita simulaattoriin.
  char src[4];
  snprintf(src, sizeof(src), "%d", SBX_SAMPLE_SOURCE);
  const FormPair keep[1] = {{"source", src}};
  procLock();
  processor().resetConfig();
  processor().applyConfigForm(keep, 1, nullptr, 0);
  procUnlock();
  sendOk(req, true);
}

void handleMaster(AsyncWebServerRequest* req) {
  FormPair pairs[kMaxPairs];
  const int n = collectForm(req, pairs);
  char err[kFormErrLen];
  procLock();
  const bool ok = processor().applyMasterForm(pairs, n, err, sizeof(err));
  procUnlock();
  sendOk(req, ok, ok ? nullptr : "err", err);
}

void handleCalZero(AsyncWebServerRequest* req) {
  procLock();
  const bool ok = processor().zeroNow() == CalResult::Ok;
  const char* msg = processor().calStatus().msg;  // staattinen teksti (processor.h)
  procUnlock();
  sendOk(req, ok, "msg", msg);
}

void handleCalGain(AsyncWebServerRequest* req) {
  procLock();
  const bool ok = processor().startGainCapture();
  const char* msg = processor().calStatus().msg;
  procUnlock();
  sendOk(req, ok, "msg", msg);
}

void handleCalReset(AsyncWebServerRequest* req) {
  procLock();
  processor().resetGain();
  const char* msg = processor().calStatus().msg;
  procUnlock();
  sendOk(req, true, "msg", msg);
}

void handleSimGet(AsyncWebServerRequest* req) {
  SimParams p;
  samplerGetSim(p);
  char buf[kSimJsonSize];
  if (simToJson(p, buf, sizeof(buf)) == 0) {
    req->send(500, "text/plain; charset=utf-8", "sim-JSON ei mahtunut puskuriin");
    return;
  }
  sendJson(req, buf);
}

void handleSimSet(AsyncWebServerRequest* req) {
  FormPair pairs[kMaxPairs];
  const int n = collectForm(req, pairs);
  SimParams p;
  samplerGetSim(p);
  bool rpm_set = false;
  char err[kFormErrLen];
  if (!simApplyForm(p, pairs, n, rpm_set, err, sizeof(err))) {
    sendOk(req, false, "err", err);
    return;
  }
  procLock();
  const SensorParams sensor = processor().config().sensor;
  procUnlock();
  samplerSetSim(p, sensor, rpm_set);
  sendOk(req, true);
}

// GET /api/raw.csv: rengas jäädytetään koko latauksen ajaksi (§4.5). Jokaista
// freeze()a vastaa täsmälleen yksi unfreeze(): latauksen lopussa tai, jos
// asiakas katkaisee, onDisconnectissa - kumpi tahansa ensin.
struct CsvState {
  std::unique_ptr<CsvWriter> writer;
  bool frozen = false;
  void release() {
    if (!frozen) return;
    frozen = false;
    procLock();
    processor().ring().unfreeze();
    procUnlock();
  }
  ~CsvState() { release(); }
};

void handleRawCsv(AsyncWebServerRequest* req) {
  std::shared_ptr<CsvState> st = std::make_shared<CsvState>();
  procLock();
  Processor& p = processor();
  p.ring().freeze();
  st->frozen = true;
  // CsvWriter kopioi anturiparametrit; kalibrointiviite pysyy Processorissa.
  st->writer.reset(new (std::nothrow) CsvWriter(p.ring(), p.config(), p.calibration()));
  procUnlock();
  if (!st->writer) {
    st->release();
    req->send(503, "text/plain; charset=utf-8", "muisti loppui");
    return;
  }
  AsyncWebServerResponse* r = req->beginChunkedResponse(
      "text/csv; charset=utf-8", [st](uint8_t* buf, size_t max_len, size_t) -> size_t {
        if (!st->frozen) return 0;
        procLock();
        const size_t n = st->writer->fill(reinterpret_cast<char*>(buf), max_len);
        procUnlock();
        if (n == 0) st->release();
        return n;
      });
  r->addHeader("Content-Disposition", "attachment; filename=\"suckboxx_raw.csv\"");
  r->addHeader("Cache-Control", "no-store");
  req->onDisconnect([st]() { st->release(); });
  req->send(r);
}

void handleDiag(AsyncWebServerRequest* req) {
  char buf[kJsonBufLen];  // async_tcp:n pino on 16 kt (AsyncTCP.h:45-47)
  const size_t n = g_diag_json != nullptr ? g_diag_json(buf, sizeof(buf)) : 0;
  if (n == 0) {
    req->send(500, "text/plain; charset=utf-8", "diagnostiikka ei mahtunut puskuriin");
    return;
  }
  // AsyncBasicResponse kopioi sisällön omaan Stringiinsä (WebResponses.cpp:260-271).
  AsyncWebServerResponse* r = req->beginResponse(200, "application/json", buf);
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

void handleNotFound(AsyncWebServerRequest* req) {
  req->send(404, "text/plain; charset=utf-8", "ei löydy");
}

// Reitit rekisteröidään ennen begin()iä: palvelimen käsittelijälistaa ei saa
// muokata, kun async_tcp jo käy sitä läpi (WebServer.cpp:142-151, ilman lukkoa).
void registerRoutes(AsyncWebServer& s) {
  // Tarkemmat polut ensin: AsyncCallbackWebHandler vertaa myös etuliitettä
  // ("/api/config" osuisi muuten "/api/config/reset"iin).
  s.on("/api/config/reset", HTTP_POST, handleConfigReset);
  s.on("/api/config", HTTP_GET, handleConfigGet);
  s.on("/api/config", HTTP_POST, handleConfigSet);
  s.on("/api/master", HTTP_POST, handleMaster);
  s.on("/api/cal/zero", HTTP_POST, handleCalZero);
  s.on("/api/cal/gain", HTTP_POST, handleCalGain);
  s.on("/api/cal/reset", HTTP_POST, handleCalReset);
  s.on("/api/sim", HTTP_GET, handleSimGet);
  s.on("/api/sim", HTTP_POST, handleSimSet);
  s.on("/api/raw.csv", HTTP_GET, handleRawCsv);
  s.on("/api/diag", HTTP_GET, handleDiag);
  s.on("/diag", HTTP_GET, handleDiagPage);
  s.on("/", HTTP_GET, handleRoot);
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

    // Diagnostiikkasivulle harvemmin (kDiagEvery). Epäonnistuminen ei estä snapia.
    static uint32_t diag_round = 0;
    if (++diag_round % kDiagEvery == 0 && g_diag_json(g_push_buf, sizeof(g_push_buf)) > 0) {
      g_events->send(g_push_buf, "diag", 0, 0);
    }

    // Snapshot Processorin mutexin alla juuri ennen lähetystä, jotta aaltojono
    // tyhjenee täsmälleen kerran per viesti (§4.8).
    ProcWindow win;
    procWindow(win);
    SnapshotMeta meta;
    meta.t_ms = millis();
    meta.fs_hz = win.fs_hz;
    meta.heap = ESP.getFreeHeap();
    procLock();
    const size_t n = snapshotJson(processor(), meta, g_snap_buf, sizeof(g_snap_buf));
    procUnlock();
    if (n == 0) {
      g_build_fail.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    switch (g_events->send(g_snap_buf, "snap", 0, 0)) {
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
  // onConnect ajetaan async_tcp:ssä _addClientissa ennen kuin asiakas lisätään
  // listaan (AsyncEventSource.cpp:357-362), joten sbx_push ei vielä näe sitä, ja
  // asiakkaan send() on tässä turvallinen.
  // retry: lyhentää selaimen uudelleenyhdistysviivettä katkon jälkeen.
  g_events->onConnect([](AsyncEventSourceClient* c) { c->send("{}", "hello", 0, kSseRetryMs); });
  // Palvelin omistaa (unique_ptr-lista, ESPAsyncWebServer.h:1684, WebServer.cpp:97-100).
  g_server->addHandler(g_events);
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
