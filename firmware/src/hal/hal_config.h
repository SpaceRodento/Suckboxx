#pragma once
// Suckboxx ESP32-liima - laitteen vakiot yhdessä paikassa (docs/firmware.md §4.9).
//
// Taskit ja ytimet (WP-7a). Ydin 1 näytteenotolle ja laskennalle, ydin 0 verkolle:
//
//   ydin | taski        | prio | pino  | tehtävä
//   -----+--------------+------+-------+-------------------------------------------
//     1  | sbx_sample   |  20  | 4096  | 1 kHz vTaskDelayUntil, lähde -> jono
//     1  | arduino_events| 19  |       | Arduino-ydin (WiFiGeneric.cpp:578), harvoin
//     1  | sbx_proc     |   5  | 6144  | jonon tyhjennys 4 ms välein, 100 ms ikkunat
//     1  | loopTask     |   1  | 8192  | Arduino loop(): sarjaloki 10 s välein
//     0  | wifi         |  23  |       | ESP-IDF (CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE_0)
//     0  | tcpip        |  18  |       | lwIP (CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0)
//     0  | sbx_push     |  11  | 6144  | SSE-lähetys 15 Hz
//     0  | async_tcp    |  10  | 16384 | web-käsittelijät (CONFIG_ASYNC_TCP_RUNNING_CORE=0)
//
// Näytteenotto on ytimen 1 korkein sovellusprioriteetti (yli arduino_events:n 19),
// joten mikään liiman oma työ ei myöhästytä sitä. sbx_push on ytimellä 0 heti
// async_tcp:n yläpuolella: perustelu web.cpp:ssä (AsyncEventSourcen säieturva).
// Task watchdog (5 s) vahtii IDLE0:aa ja async_tcp:tä (sdkconfig
// CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y, CPU1 ei; AsyncTCP.cpp:331-337),
// joten ytimen 0 taskit eivät saa pyöriä estämättä eivätkä käsittelijät jäädä odottamaan.

#include <cstdint>

#include "core/types.h"

// Lähteen käännösaikainen oletus (platformio.ini: esp32dev_adc, esp32dev_hx).
// Käytössä, kun NVS:ssä ei ole konfiguraatiota, ja asetusten nollauksessa.
#ifndef SBX_SAMPLE_SOURCE
#define SBX_SAMPLE_SOURCE 0
#endif
static_assert(SBX_SAMPLE_SOURCE >= 0 && SBX_SAMPLE_SOURCE <= 2,
              "SBX_SAMPLE_SOURCE: 0 = sim, 1 = ADC, 2 = HX710B (SourceKind)");

namespace sbx {
namespace hal {

// --- Pinnit (docs/rauta.md §2.2) -------------------------------------------
// ADC-kanavat ovat sampler.cpp:ssä ADC1-kanavanumeroina (protolevy GPIO34/35/32,
// ei kiskoa; suunnitelma GPIO34/35/36 + kisko GPIO39). Kaikki ADC1:ssä, koska
// ADC2 ei toimi Wi-Fin kanssa (D8).
// HX710B-penkkitesti (D14) bring-up-kytkennällä: DOUT samoissa input-only-
// pinneissä, SCK erillisissä lähtöpinneissä.
constexpr uint8_t kPinHxSck[kChannels] = {25, 26, 27};
constexpr uint8_t kPinHxDout[kChannels] = {34, 35, 36};
constexpr uint8_t kPinLed = 2;  // devkitin oma LED: vilkkuu 1 Hz, kun käsittelytaski elää

// --- Näytteenotto ------------------------------------------------------------
constexpr uint32_t kSamplePeriodUs = 1000000u / kSampleRateHz;  // 1000
constexpr int kSamplerCore = 1;
constexpr uint32_t kSamplerPrio = 20;
constexpr uint32_t kSamplerStack = 4096;  // tavuina (ESP-IDF)
// Jono samplerilta käsittelyyn: 128 kehystä = 128 ms puskuria (2,5 kt). Riittää
// käsittelytaskin pisimpään odotukseen (7b: Processor-mutexi, snapshot) ja
// flash-operaation pysäytykseen (CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS = 10).
constexpr uint32_t kQueueLen = 128;
// "Myöhässä" = näyte otettu yli neljänneksen näytevälistä ihanteellisen 1 ms:n
// tahdin jälkeen. 112 Hz:n resonanssilla 250 µs on ~0,5 kPa:n hetkellinen virhe,
// joka keskiarvoistuu syklin yli pois - raja erottaa häiriöt tavallisesta värinästä.
constexpr uint32_t kLateUs = 250;
// Kiinniottoraja: jos taski on jäljessä enemmän, puuttuvia jaksoja ei ajeta
// peräkkäin (kasattu purske väärillä aikaleimoilla), vaan seq hyppää yli.
constexpr uint32_t kCatchUpMaxUs = 3000;
// HX710B tuottaa 10 SPS; luku korkeintaan näin usein per anturi (b838911).
constexpr uint32_t kHxMinIntervalUs = 90000;

// --- Käsittelytaski (7a: diagnostiikka, 7b: Processor) ----------------------
constexpr int kProcCore = 1;
constexpr uint32_t kProcPrio = 5;
constexpr uint32_t kProcStack = 6144;
constexpr uint32_t kProcPollMs = 4;   // jono tyhjennetään erissä, 7b: yksi mutexi per erä
constexpr uint32_t kWindowMs = 100;   // diagnostiikkaikkuna
constexpr uint32_t kLedToggleWindows = 5;  // 5 x 100 ms -> 1 Hz vilkku

// --- Wi-Fi AP (§4.9, bring-up-firmwaren arvot) ------------------------------
constexpr char kApSsid[] = "Suckboxx";
constexpr char kApPass[] = "alipaine123";  // WPA2 vaatii vähintään 8 merkkiä
constexpr uint8_t kApChannel = 1;
constexpr uint8_t kApMaxClients = 4;

// --- Web -------------------------------------------------------------------
constexpr uint16_t kHttpPort = 80;
constexpr uint32_t kPushHz = 15;          // D9: SSE 15 Hz
constexpr uint32_t kPushStack = 6144;
constexpr uint32_t kJsonBufLen = 1024;    // diag-JSON ~600 tavua
constexpr uint32_t kSseRetryMs = 2000;    // selaimen uudelleenyhdistysväli (SSE retry:)

// --- Sarjaloki ---------------------------------------------------------------
constexpr uint32_t kLogPeriodMs = 10000;
constexpr uint32_t kSlowRefreshMs = 1000;  // heapin suurin lohko ja AP-asemat (diag.cpp)

}  // namespace hal
}  // namespace sbx
