#pragma once
// Suckboxx ESP32-liima - 1 kHz näytteenotto (docs/firmware.md §4.9, WP-7a).
//
// Oma FreeRTOS-taski ytimellä 1 herää vTaskDelayUntililla 1 ms välein
// (CONFIG_FREERTOS_HZ = 1000, tarkistetaan käännöksessä), lukee lähteen ja
// laittaa kehyksen jonoon. Jono on ainoa kanava käsittelytaskiin: täysi jono
// pudottaa kehyksen, ja pudotus näkyy vastaanottajalle seq-aukkona (diag.gaps).
//
// seq on samplerin oma jaksolaskuri kaikille lähteille: seq = 1 ms:n jaksojen
// määrä käynnistyksestä. Jos taski on jäänyt yli kCatchUpMaxUs jälkeen (esim.
// flash-kirjoitus pysäytti molemmat ytimet), väliin jääneitä jaksoja ei ajeta
// peräkkäin, vaan seq hyppää: aukko kertoo totuuden, kasattu purske ei.
//
// Ei pitkiä keskeytyskieltoja eikä busy-waitia (§4.9, b838911). Pisimmät
// keskeytyskiellot ytimellä 1: ADC-muunnos adc1_get_raw:n kriittisessä osassa
// (yksi kanava kerrallaan) ja HX710B:n 24-bittinen luku (~65 µs, enintään yksi
// per jakso).

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdint>

#include "core/engine_sim.h"
#include "core/types.h"

namespace sbx {
namespace hal {

// Jonon alkio: kehys + liiman ajoitustieto. Käsittelytaski laskee näistä
// mitatun näytetaajuuden ja jitterin (7b: SnapshotMeta::fs_hz).
struct SampleMsg {
  RawFrame frame;
  uint32_t t_us = 0;     // lukuhetki, esp_timer_get_time():n alin 32 bittiä (kiertää 71 min)
  uint16_t lag_us = 0;   // myöhästyminen ihanteellisesta 1 ms:n tahdista (kyllästyy)
  uint16_t read_us = 0;  // lähteen lukuun kulunut aika (CPU-budjetti)
};

// Kumulatiiviset laskurit käynnistyksestä. Vain sampler-taski kirjoittaa.
struct SamplerCounters {
  uint32_t queue_drops = 0;  // jono täynnä -> kehys pudotettu (seq-aukko)
  uint32_t skipped = 0;      // jaksot, jotka ohitettiin kiinnioton sijaan (seq-aukko)
  uint32_t hx_bad = 0;       // HX710B: DOUT ei vapautunut lukemisen jälkeen (hx710b.h)
};

// ADC-lähteen karakterisointi käynnistyslokiin (vain SourceKind::Adc).
struct AdcInfo {
  bool valid = false;
  int cal_type = -1;          // esp_adc_cal_value_t: 0 eFuse Vref, 1 eFuse TP, 2 oletus-Vref
  uint32_t vref_mv = 0;
  uint32_t coeff_a = 0;
  uint32_t coeff_b = 0;
  uint32_t raw0_mv = 0;       // mitä esp_adc_cal antaisi raw 0:lle (lattia)
  uint32_t raw4095_mv = 0;    // ... ja raw 4095:lle (katto)
};

// Alustaa lähteen kutsujan taskissa ja käynnistää sampler-taskin. Kerran.
bool samplerStart(SourceKind src);
QueueHandle_t samplerQueue();
SourceKind samplerSource();
TaskHandle_t samplerTask();
void samplerCounters(SamplerCounters& out);
const AdcInfo& samplerAdcInfo();  // voimassa samplerStartin jälkeen

// "sim" | "adc" | "hx710b" (SnapshotMeta::src)
const char* sourceName(SourceKind s);

// Simulaattorin parametrit (7b: POST /api/sim). Säieturvallinen mistä tahansa
// taskista; tulee voimaan seuraavalla jaksolla. Sampler ei koskaan odota lukkoa:
// jos asettaja pitää sitä, muutos siirtyy yhdellä jaksolla.
void samplerSetSim(const SimParams& p, const SensorParams& s);
void samplerGetSim(SimParams& out);  // viimeisin asetettu (myös vielä soveltamaton)

}  // namespace hal
}  // namespace sbx
