#pragma once
// Suckboxx core - yhteiset tyypit ja parametrit (rajapintasopimus).
//
// Kaikki src/core/ -kansion koodi on puhdasta C++17:aa ilman Arduino- tai
// ESP-IDF-riippuvuuksia, jotta se kääntyy sekä ESP32:lle (GCC 8.4) että PC:lle
// native-testejä varten. Tämä tiedosto on moduulien välinen sopimus: muutos
// tänne on arkkitehtuurimuutos, ks. docs/firmware.md.
//
// Yksiköt: paine aina kPa gauge (negatiivinen = alipaine), jännite mV
// ADC-pinnissä, aika näytenumeroina (seq) tai sekunteina floatina.
// Liukuluvut ovat float, ei double: ESP32:n FPU on yksitarkkuuksinen.

#include <cstdint>

namespace sbx {

constexpr int kChannels = 3;
constexpr uint32_t kSampleRateHz = 1000;  // per kanava, docs/rauta.md §3.2
constexpr float kSamplePeriodS = 1.0f / static_cast<float>(kSampleRateHz);

// Yksi näytehetki kaikista kanavista. Lähde (ADC, simulaattori, HX710B) tuottaa
// millivoltteja ADC-pinnissä - muunnos kPa:ksi tehdään vasta kalibroinnissa,
// jotta simulaattori kulkee saman muunnos- ja vikatunnistuspolun kuin rauta.
struct RawFrame {
  uint32_t seq = 0;                // juokseva näytenumero; aukko = pudonnut näyte
  uint16_t mv[kChannels] = {0, 0, 0};
  uint16_t rail_mv = 0;            // anturikiskon jännite (R6), 0 = ei mitattu
};

// ---------------------------------------------------------------------------
// Parametrit. Kaikki ovat ajonaikaisesti säädettäviä (S5a) config.h:n
// rekisterin kautta. Oletusarvot ovat SUUNNITTELU.md:n lähtöarvoja.
// ---------------------------------------------------------------------------

// Anturin siirtofunktio ADC-pinnissä: P_kPa = (mv - v_zero_mv) / mv_per_kpa.
// 3,3 V XGZP6847A100KPGN33: 2700 mV, 25 mV/kPa. 5 V -versio 20k/30k-jakajalla:
// 4500*0,6 = 2700 mV, 40*0,6 = 24 mV/kPa (docs/rauta.md §1.6).
struct SensorParams {
  float v_zero_mv = 2700.0f;
  float mv_per_kpa = 25.0f;
  uint16_t fault_lo_mv = 100;    // käytösmatriisi rivi 7
  uint16_t fault_hi_mv = 3150;   // rivi 7
  float fault_hold_s = 1.0f;     // rivi 7: yhtäjaksoisesti näin kauan
  uint16_t sat_mv = 2680;        // rivi 10: GN-anturi leikkaa 2700 mV:iin
  bool rail_comp = false;        // ratiometrinen korjaus rail_mv:llä (R6)
  float rail_nominal_mv = 3300.0f;
};

struct DspParams {
  float lp_cutoff_hz = 40.0f;    // resonanssin (90-215 Hz) poisto ennen syklintunnistusta
  float level_tau_s = 0.5f;      // EMA-taso, käytössä myös ilman pulssia
  float hyst_frac = 0.3f;        // syklirajan hystereesi suhteessa huippu-huippuun
  float hyst_min_kpa = 0.3f;     // hystereesin alaraja
  uint8_t avg_cycles = 8;        // montako sykliä keskiarvoistetaan näyttöön
  float rpm_min = 300.0f;        // hyväksyttävä jaksonaika-alue syklintunnistukselle
  float rpm_max = 6000.0f;
  float pulse_timeout_s = 1.0f;  // näin kauan ilman sykliä -> has_pulse = false
  uint8_t rpm_hist = 16;         // jaksoja RPM-hajonnan laskentaan
};

struct StatusParams {
  uint8_t master = 0;            // D7, oletus SYL 1
  float tol_green_kpa = 1.5f;    // D2
  float tol_yellow_kpa = 3.0f;   // D2
  float off_kpa = 5.0f;          // rivit 1/8: kaikki |taso| alle tämän (kalibroimatonkin, D10)
  float off_hold_s = 3.0f;       // rivi 1: ei pulssia näin kauan
  float hose_off_kpa = 2.0f;     // rivi 6: kanavan |taso| alle tämän muiden pulssiessa
  float unstable_cv = 0.15f;     // rivi 2: rpm-hajonta (sd/ka) yli tämän
  float stable_hold_s = 3.0f;    // rivi 3: vakaa näin kauan ennen tuomiota
  float sync_rpm_min = 500.0f;   // D12: synkronointiarvio tällä välillä
  float sync_rpm_max = 3000.0f;  // rivi 9
  float drift_rpm = 50.0f;       // rivi 5: muutos yli tämän ...
  float drift_window_s = 2.0f;   // ... tämän ikkunan aikana
  bool auto_zero = true;         // rivi 1: automaattinen nollakalibrointi
};

enum class SourceKind : uint8_t { Sim = 0, Adc = 1, Hx710b = 2 };

// ---------------------------------------------------------------------------
// Kalibrointi (R4). Sovelletaan: P = gain * P_nominal + offset.
// ---------------------------------------------------------------------------
struct Calibration {
  float offset_kpa[kChannels] = {0.0f, 0.0f, 0.0f};
  float gain[kChannels] = {1.0f, 1.0f, 1.0f};
  bool zero_valid = false;       // nollapiste otettu tämän käynnistyksen aikana (D11)
  bool gain_valid = false;       // vahvistuskalibrointi tehty (pysyvä, NVS)
};

// ---------------------------------------------------------------------------
// DSP:n tuottama tila (dsp.h). Luetaan status-logiikkaan ja snapshotiin.
// ---------------------------------------------------------------------------
struct ChannelState {
  float level_kpa = 0.0f;        // EMA (level_tau_s), aina voimassa - myös ilman pulssia
  bool has_pulse = false;        // sykli havaittu viimeisen pulse_timeout_s aikana
  uint32_t cycles = 0;           // havaittujen syklien kokonaismäärä
  float mean_kpa = 0.0f;         // D1: syklin keskiarvo, avg_cycles syklin yli; voimassa kun has_pulse
  float min_kpa = 0.0f;          // diagnostiikka (D1), sama keskiarvoistus
  float p2p_kpa = 0.0f;          // diagnostiikka (D1)
  float rpm = 0.0f;              // tämän kanavan jaksosta, 0 jos ei pulssia
  float last_kpa = 0.0f;         // viimeisin kalibroitu näyte
  uint16_t last_mv = 0;
  bool sensor_fault = false;     // rivi 7
  bool saturated = false;        // rivi 10: sat_mv ylitetty viimeisen 1 s aikana
};

struct RpmEstimate {
  bool valid = false;            // vähintään yksi kanava pulssii ja jaksoja riittävästi
  float rpm = 0.0f;              // kanavien mediaani
  float sd_rpm = 0.0f;           // sykli-sykliltä hajonta (keskihajonta)
  float cv = 0.0f;               // sd_rpm / rpm
};

// ---------------------------------------------------------------------------
// Status (status.h) - käytösmatriisi SUUNNITTELU.md §8.
// ---------------------------------------------------------------------------
enum class SyncState : uint8_t {
  NoSignal = 0,     // rivit 1 + 8 (D10): ei pulssia, kaikki ~0
  Unstable,         // rivi 2 (myös rpm < sync_rpm_min ja vakausajan odotus)
  RpmTooHigh,       // rivi 9
  Synced,           // rivi 3
  Near,             // rivi 4a
  OutOfSync,        // rivi 4b
  FaultSensor,      // rivi 7
  FaultHose,        // rivi 6
  FaultMaster,      // rivi 12
};

constexpr int kHeadlineLen = 80;   // UTF-8 tavuina, sis. NUL
constexpr int kAdviceLen = 112;

struct StatusResult {
  SyncState state = SyncState::NoSignal;
  int8_t fault_ch = -1;            // vikakanava (0..2) tai -1
  bool show_bars = false;          // rivi 2: palkit piiloon
  bool show_verdict = false;       // vihreä/keltainen/punainen tuomio näkyvissä
  // Alipaine-ero masteriin: d = mean_master - mean_ch. Positiivinen = kanava
  // imee enemmän kuin master (läppä suljetumpi) -> "avaa". Master = 0.
  float vac_delta_kpa[kChannels] = {0.0f, 0.0f, 0.0f};
  float spread_kpa = 0.0f;         // max(d) - min(d)
  int8_t worst_ch = -1;            // suurin |d| ei-master-kanavista
  bool flag_saturation = false;    // rivi 10
  bool flag_uncal_zero = false;    // D11: nollaus puuttuu -> ei vihreää
  bool flag_uncal_gain = false;    // rivi 11: vahvistuskalibrointi puuttuu
  bool flag_drift = false;         // rivi 5
  bool request_auto_zero = false;  // rivi 1: Processor ajaa nollauksen
  char headline[kHeadlineLen] = {0};
  char advice[kAdviceLen] = {0};
};

}  // namespace sbx
