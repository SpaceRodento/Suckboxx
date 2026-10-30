#pragma once
// Suckboxx ESP32-liima - diagnostiikan kokoaja (WP-7a).
//
// Kokoaa samplerin, käsittelytaskin, webin ja Wi-Fin tilan yhdeksi JSON-riviksi
// (GET /api/diag ja SSE-tapahtuma "diag") sekä sarjalokiin. WP-7b:ssä JSON
// korvautuu §5:n snapshotilla, mutta sarjaloki ja liiman mittarit jäävät.
//
// JSON (yksi rivi, avaimet lyhyitä, sivu diag_page.h lukee nämä):
//   v        skeemaversio (1)
//   t, up    millis() ja käyntiaika sekunteina
//   src      "sim" | "adc" | "hx710b"
//   win, n, seq        ikkunan numero, näytteet (~100) ja viimeinen seq
//   fs                 mitattu näytetaajuus, Hz (ikkunan aukottomista väleistä)
//   dev_us, lag_us     ikkunan suurin |väli - 1000 µs| ja suurin myöhästyminen tahdista
//   late, gaps         kumulatiiviset: myöhässä > kLateUs, seq-aukkoihin jääneet näytteet
//   q_drop, skipped    kumulatiiviset: jono täynnä, kiinnioton sijaan ohitetut jaksot
//   hx_bad             kumulatiivinen: HX710B-luku hylätty (DOUT ei vapautunut)
//   q_peak, q_len      ikkunan jonohuippu ja jonon koko
//   read_us            [ka, max] lähteen lukuaika ikkunassa
//   heap               [vapaa, pienin koskaan, suurin vapaa lohko] tavuina (lohko 1 s välein)
//   sta, sse           AP-asemat (1 s välein) ja SSE-asiakkaat
//   sse_tx             kumulatiiviset [kaikille, osalle, ei kenellekään]
//   push_us            edellisen SSE-lähetyskierroksen kesto (rakennus + send)
//   mv, rail           [min, ka, max] mV ikkunassa: kolme kanavaa ja kisko (rail_mv)

#include <cstddef>

namespace sbx {
namespace hal {

size_t diagJson(char* buf, size_t n);  // web.h:n JsonBuilder; säieturvallinen
void diagLogBoot();                    // setupin lopussa
void diagLogPeriodic();                // loopTaskista kLogPeriodMs välein

}  // namespace hal
}  // namespace sbx
