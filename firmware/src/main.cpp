// Suckboxx - ESP32-liima, WP-7b (docs/firmware.md §4.9)
//
//   sbx_sample (ydin 1, 1 kHz) --jono--> sbx_proc (ydin 1): Processor push + tick 20 Hz
//                                                   | (Processorin mutex)
//   async_tcp (ydin 0): GET /, /diag, §5:n /api-reitit <---+
//   sbx_push  (ydin 0): SSE /events "snap" 15 Hz, "diag" 5 Hz
//   loopTask  (ydin 1): NVS-tallennus dirty-lipuista + sarjaloki 10 s välein
//
// Bring-up-firmwaren opetus (b838911): ei pitkiä keskeytyskieltoja eikä
// busy-waitia - tässä tiedostossa ei ole kumpaakaan, ja loop() vain odottaa.
//
// Lähde: Config::source NVS:stä. Jos NVS:ssä ei ole konfiguraatiota, käännöksen oletus:
//   pio run -e esp32dev       simulaattori (D14)
//   pio run -e esp32dev_adc   ADC1 GPIO34/35/32 (protolevy, ei kiskoa)
//   pio run -e esp32dev_hx    HX710B-penkkitesti
// Lähteen vaihto UI:sta tallentuu NVS:ään ja käynnistää laitteen uudelleen.

#include <Arduino.h>

#include "core/types.h"
#include "hal/diag.h"
#include "hal/hal_config.h"
#include "hal/nvs_store.h"
#include "hal/proc_task.h"
#include "hal/sampler.h"
#include "hal/web.h"
#include "hal/wifi_ap.h"

namespace {

void fail(const char* what) {
  // Jatketaan silti: loki ja muut osat kertovat enemmän kuin pysähtynyt laite.
  Serial.printf("[VIRHE] %s epaonnistui\n", what);
}

// Pysyvät muutokset NVS:ään dirty-lipuista (§4.9). Kopio mutexin alla,
// kirjoitus ilman sitä: flash-kirjoitus kestää, eikä käsittelytaski saa odottaa.
// Lähteen vaihto tulee voimaan uudelleenkäynnistyksellä (sampler alustaa
// lähteensä kerran).
void saveDirty() {
  using namespace sbx;
  hal::procLock();
  Processor& p = hal::processor();
  const bool cfg_dirty = p.takeConfigDirty();
  const bool cal_dirty = p.takeCalDirty();
  const Config cfg = p.config();
  const Calibration cal = p.calibration();
  hal::procUnlock();
  if (cfg_dirty) hal::nvsSaveConfig(cfg);
  if (cal_dirty) hal::nvsSaveCal(cal);
  if (cfg_dirty || cal_dirty) {
    Serial.printf("[INFO] NVS tallennettu (%s%s)\n", cfg_dirty ? "asetukset " : "",
                  cal_dirty ? "kalibrointi" : "");
  }
  if (cfg.source != hal::samplerSource()) {
    Serial.printf("[INFO] lahde vaihtui -> %s, kaynnistetaan uudelleen\n", hal::sourceName(cfg.source));
    delay(200);
    ESP.restart();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  using namespace sbx;

  Config cfg;
  cfg.source = static_cast<SourceKind>(SBX_SAMPLE_SOURCE);
  const bool from_nvs = hal::nvsLoadConfig(cfg);
  Calibration cal;
  hal::nvsLoadCal(cal);
  Serial.printf("[INFO] asetukset: %s, lahde=%s, gain %s\n", from_nvs ? "NVS" : "oletukset",
                hal::sourceName(cfg.source), cal.gain_valid ? "kalibroitu" : "ei");

  // Näytteenotto ensin, jotta myös Wi-Fin käynnistyksen häiriöt näkyvät
  // laskureissa (flash-luvut PHY-kalibroinnille pysäyttävät ytimet hetkeksi).
  if (!hal::samplerStart(cfg.source)) fail("sampler");
  if (!hal::procStart(cfg, cal)) fail("kasittelytaski");
  if (!hal::wifiApStart()) fail("Wi-Fi AP");
  if (!hal::webStart(hal::diagJson)) fail("web");
  hal::diagLogBoot();
}

void loop() {
  // loopTask (ydin 1, prio 1) vain kirjoittaa sarjalokin. vTaskDelayUntil estää
  // vain tämän taskin; kaikki muu työ on omissa taskeissaan.
  static TickType_t wake = xTaskGetTickCount();
  vTaskDelayUntil(&wake, pdMS_TO_TICKS(sbx::hal::kLogPeriodMs));
  saveDirty();
  sbx::hal::diagLogPeriodic();
}
