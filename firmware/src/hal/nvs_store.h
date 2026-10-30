#pragma once
// Suckboxx ESP32-liima - pysyvä tallennus NVS:ään (docs/firmware.md §4.9).
//
// Preferences, nimiavaruus "sbx". Konfiguraatio kenttä kerrallaan
// kenttärekisterin kautta (§4.4: avaimet ≤ 15 merkkiä), joten uusi kenttä ei
// vaadi muutoksia tänne. Kalibroinnista vain gainit ja gain_valid (D11:
// nollaus ei ole pysyvä). Skeemaversion vaihtuessa tallennettu konfiguraatio
// ohitetaan ja oletukset jäävät voimaan.
//
// Flash-kirjoitus pysäyttää molemmat ytimet hetkeksi, ja se näkyy samplerin
// seq-aukkoina. Siksi tallennus tehdään dirty-lipuista viiveellä (loopTask),
// ei jokaisesta muutoksesta erikseen.

#include "core/calibration.h"
#include "core/config.h"

namespace sbx {
namespace hal {

// true = NVS:ssä oli tämän skeemaversion konfiguraatio (cfg luettu siitä).
bool nvsLoadConfig(Config& cfg);
void nvsLoadCal(Calibration& cal);
void nvsSaveConfig(const Config& cfg);
void nvsSaveCal(const Calibration& cal);

}  // namespace hal
}  // namespace sbx
