#pragma once
// Suckboxx ESP32-liima - raakadatan jatkuva striimaus sarjaporttiin.
//
// Wi-Fin /api/raw.csv antaa vain renkaan viimeiset ~8 s (§4.5). USB-kaapelilla
// laite voi lähettää jokaisen 1 kHz:n näytteen niin pitkään kuin halutaan,
// esimerkiksi penkkitestin tai moottoriajon ajan. PC:n puoli:
// tools/serial_record.py, joka kirjoittaa saman CSV-muodon kuin /api/raw.csv
// (tools/cycle_profile.py lukee molempia).
//
// Protokolla (rivipohjainen, 115200 baud):
//   PC:    "stream <sekunnit> [baud]\n"          esim. "stream 60 921600"
//   laite: "# stream ok <sekunnit> <baud>\n"      115200:lla, sitten baud vaihtuu
//          "# <avain>=<arvo>" -otsakkeet (anturi, kalibrointi)
//          "<seq>,<mv1>,<mv2>,<mv3>\n" jokaisesta näytteestä
//          "# end samples=<n> dropped=<n>\n"     ja baud takaisin 115200:aan
// 115200 baudia ei riitä 1 kHz:n riveille (~20 kt/s), joten virta ajetaan
// oletuksena 921600:lla (USB-UART-sillat CP2102/CH340 tukevat sitä).
//
// Näytteet tulevat käsittelytaskista lukitsemattomaan SPSC-renkaaseen (1 s),
// josta loopTask tulostaa ne. Täysi rengas pudottaa näytteen, ja pudotus
// näkyy seq-aukkona ja end-rivin dropped-laskurissa - näytteenotto ei koskaan odota.

#include "core/types.h"

namespace sbx {
namespace hal {

void streamPush(const RawFrame& f);  // käsittelytaskista jokaiselle kehykselle; ei estä
bool streamActive();
void streamPoll();                   // loopTaskista ~10 ms välein: komennot ja tulostus

}  // namespace hal
}  // namespace sbx
