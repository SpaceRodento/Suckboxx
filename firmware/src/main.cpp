// Suckboxx - ESP32-liima, WP-7a: alustatestifirmware (docs/firmware.md §4.9)
//
// Laitteen riskialttein osa ennen Processoria: 1 kHz:n näytteenotto Wi-Fin
// rinnalla, tukiasema, web-palvelin ja SSE. Ketju:
//
//   sbx_sample (ydin 1, 1 kHz) --jono--> sbx_proc (ydin 1) --> diag-tila (mutex)
//                                                               |
//   async_tcp (ydin 0): GET /, GET /api/diag <------------------+
//   sbx_push  (ydin 0): SSE /events "diag" 15 Hz <--------------+
//
// WP-7b kytkee käsittelytaskiin Processorin (§4.8), NVS-konfiguraation ja §5:n
// reitit. Näytteenottoon (hal/sampler.*) ja Wi-Fiin (hal/wifi_ap.*) ei tarvitse
// koskea. Bring-up-firmwaren opetus (b838911): ei pitkiä keskeytyskieltoja eikä
// busy-waitia - tässä tiedostossa ei ole kumpaakaan, ja loop() vain odottaa.
//
// Lähde valitaan väliaikaisesti käännösaikana (7b: Config::source NVS:stä):
//   pio run -e esp32dev       simulaattori (oletus, D14)
//   pio run -e esp32dev_adc   ADC1 GPIO34/35/36 + kisko GPIO39
//   pio run -e esp32dev_hx    HX710B-penkkitesti

#include <Arduino.h>

#include "core/types.h"
#include "hal/diag.h"
#include "hal/hal_config.h"
#include "hal/proc_task.h"
#include "hal/sampler.h"
#include "hal/web.h"
#include "hal/wifi_ap.h"

#ifndef SBX_SAMPLE_SOURCE
#define SBX_SAMPLE_SOURCE 0
#endif
static_assert(SBX_SAMPLE_SOURCE >= 0 && SBX_SAMPLE_SOURCE <= 2,
              "SBX_SAMPLE_SOURCE: 0 = sim, 1 = ADC, 2 = HX710B (SourceKind)");

namespace {

void fail(const char* what) {
  // Jatketaan silti: loki ja muut osat kertovat enemmän kuin pysähtynyt laite.
  Serial.printf("[VIRHE] %s epaonnistui\n", what);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  using namespace sbx;

  // Näytteenotto ensin, jotta myös Wi-Fin käynnistyksen häiriöt näkyvät
  // laskureissa (flash-luvut PHY-kalibroinnille pysäyttävät ytimet hetkeksi).
  if (!hal::samplerStart(static_cast<SourceKind>(SBX_SAMPLE_SOURCE))) fail("sampler");
  if (!hal::procStart()) fail("kasittelytaski");
  if (!hal::wifiApStart()) fail("Wi-Fi AP");
  if (!hal::webStart(hal::diagJson)) fail("web");
  hal::diagLogBoot();
}

void loop() {
  // loopTask (ydin 1, prio 1) vain kirjoittaa sarjalokin. vTaskDelayUntil estää
  // vain tämän taskin; kaikki muu työ on omissa taskeissaan.
  static TickType_t wake = xTaskGetTickCount();
  vTaskDelayUntil(&wake, pdMS_TO_TICKS(sbx::hal::kLogPeriodMs));
  sbx::hal::diagLogPeriodic();
}
