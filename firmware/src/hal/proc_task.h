#pragma once
// Suckboxx ESP32-liima - käsittelytaski (docs/firmware.md §4.9).
//
// Herää kProcPollMs välein ytimellä 1, tyhjentää samplerin jonon erissä ja laskee
//  - ajoitustilastot: mitattu näytetaajuus, jitter, myöhästyneet, seq-aukot,
//    jonon täyttöhuippu. Nämä jäävät WP-7b:hen (SnapshotMeta::fs_hz, diag).
//  - kanavakohtaiset mV-tilastot 100 ms:n ikkunoissa (diagnostiikkasivu /diag)
//  - WP-7b: Processor::push jokaiselle kehykselle ja tick 20 Hz:llä, kaikki
//    Processorin mutexin alla (yksi lukitus per erä).
//
// Julkaistu tila luetaan mutexin takaa kopiona (web-käsittelijät ytimellä 0).
// Processoria käyttävät muut taskit (web, NVS-tallennus) lukitsevat procLock()illa
// lyhyesti: käsittelytaski odottaa sillä välin, ja sampler-jonossa on 128 ms varaa.

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

#include "core/calibration.h"
#include "core/config.h"
#include "core/processor.h"
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

// samplerStartin jälkeen. cfg ja cal NVS:stä (setCalibration: zero_valid = false, D11).
bool procStart(const Config& cfg, const Calibration& cal);
void procWindow(ProcWindow& out);

// Processor mutexin takana (§4.8). Lukitse, käytä, vapauta - ei odotuksia välissä.
Processor& processor();
void procLock();
void procUnlock();
void procTakePeriod(ProcPeriod& out);  // kopioi ja nollaa jakson
TaskHandle_t procTask();

}  // namespace hal
}  // namespace sbx
