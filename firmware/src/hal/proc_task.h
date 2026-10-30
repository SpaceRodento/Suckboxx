#pragma once
// Suckboxx ESP32-liima - käsittelytaski (docs/firmware.md §4.9).
//
// Herää kProcPollMs välein ytimellä 1, tyhjentää samplerin jonon erissä ja laskee
//  - ajoitustilastot: mitattu näytetaajuus, jitter, myöhästyneet, seq-aukot,
//    jonon täyttöhuippu. Nämä jäävät WP-7b:hen (SnapshotMeta::fs_hz, diag).
//  - WP-7a: kanavakohtaiset mV-tilastot 100 ms:n ikkunoissa. WP-7b korvaa ne
//    Processor::push- ja tick-kutsuilla Processorin mutexin takana; kytkentä-
//    kohdat on merkitty proc_task.cpp:hen tunnisteella "7b:".
//
// Julkaistu tila luetaan mutexin takaa kopiona (web-käsittelijät ytimellä 0).

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

#include "core/types.h"

namespace sbx {
namespace hal {

constexpr int kRailIndex = kChannels;  // mv_*-taulukoissa kanavien jälkeen kisko (rail_mv)

// Viimeisin suljettu 100 ms:n ikkuna.
struct ProcWindow {
  uint32_t t_ms = 0;        // sulkemishetki (millis)
  uint32_t index = 0;       // suljettujen ikkunoiden määrä käynnistyksestä
  uint32_t seq = 0;         // ikkunan viimeinen seq
  uint16_t n = 0;           // ikkunan näytteet
  float fs_hz = 0.0f;       // peräkkäisten (aukottomien) näytteiden väleistä
  uint16_t dev_max_us = 0;  // suurin |väli - 1000 µs|
  uint16_t lag_max_us = 0;  // suurin myöhästyminen ihanteellisesta 1 ms:n tahdista
  uint16_t read_mean_us = 0;
  uint16_t read_max_us = 0;
  uint16_t q_peak = 0;      // jonon täyttöhuippu tyhjennysten alussa
  uint32_t late = 0;        // kumulatiivinen: näytteet, joiden lag > kLateUs
  uint32_t gaps = 0;        // kumulatiivinen: seq-aukkoihin jääneet näytteet
  // WP-7a: mV-tilastot, indeksit 0..2 = SYL 1..3, kRailIndex = kisko
  uint16_t mv_min[kChannels + 1] = {0, 0, 0, 0};
  uint16_t mv_mean[kChannels + 1] = {0, 0, 0, 0};
  uint16_t mv_max[kChannels + 1] = {0, 0, 0, 0};
};

// Kooste edellisestä procTakePeriod-kutsusta (sarjaloki 10 s välein).
struct ProcPeriod {
  uint32_t windows = 0;
  uint32_t n = 0;
  float fs_hz = 0.0f;
  uint16_t dev_max_us = 0;
  uint16_t lag_max_us = 0;
  uint16_t read_mean_us = 0;
  uint16_t read_max_us = 0;
  uint16_t q_peak = 0;
};

bool procStart();  // samplerStartin jälkeen
void procWindow(ProcWindow& out);
void procTakePeriod(ProcPeriod& out);  // kopioi ja nollaa jakson
TaskHandle_t procTask();

}  // namespace hal
}  // namespace sbx
