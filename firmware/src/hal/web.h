#pragma once
// Suckboxx ESP32-liima - web-palvelin ja SSE (docs/firmware.md §4.9, §5, D9).
//
// ESPAsyncWebServer 3.6.0 + AsyncTCP 3.3.2. Käsittelijät ajetaan async_tcp-
// taskissa ytimellä 0 (CONFIG_ASYNC_TCP_RUNNING_CORE=0). SSE-viestit lähettää
// oma sbx_push-taski samalla ytimellä async_tcp:tä korkeammalla prioriteetilla;
// perustelu web.cpp:n alussa.
//
// WP-7a:n reitit: GET / (väliaikainen diagnostiikkasivu), GET /api/diag (sama
// JSONina) ja GET /events (SSE, tapahtuma "diag" 15 Hz). WP-7b lisää §5:n reitit
// registerRoutes()-funktioon (web.cpp), vaihtaa sivun src/web/web_page.h:hon ja
// tapahtuman "snap":ksi.

#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace sbx {
namespace hal {

// Kirjoittaa yhden rivin JSONia puskuriin, palauttaa pituuden; 0 = ei mahtunut.
// Kutsutaan sekä sbx_push- että async_tcp-taskista, joten oltava säieturvallinen.
using JsonBuilder = size_t (*)(char* buf, size_t n);

struct WebStats {
  uint32_t sse_clients = 0;   // yhdistetyt SSE-asiakkaat (AsyncEventSource::count)
  uint32_t sse_sent = 0;      // viestit, jotka menivät kaikkien asiakkaiden jonoon
  uint32_t sse_partial = 0;   // osa asiakkaista hylkäsi (asiakkaan jono täynnä)
  uint32_t sse_dropped = 0;   // kukaan ei ottanut vastaan
  uint32_t build_fail = 0;    // JSON ei mahtunut puskuriin
  uint32_t push_us_last = 0;  // viimeisimmän rakennus + lähetys -kierroksen kesto
};

bool webStart(JsonBuilder diag_json);  // wifiApStartin jälkeen
void webStats(WebStats& out);
uint32_t webTakePushMax();             // sarjaloki: jakson pisin push, nollaa
TaskHandle_t webPushTask();

}  // namespace hal
}  // namespace sbx
