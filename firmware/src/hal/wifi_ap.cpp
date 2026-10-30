// Suckboxx ESP32-liima - Wi-Fi-tukiasema. Ks. wifi_ap.h.

#include "hal/wifi_ap.h"

#include <WiFi.h>

#include <cstdio>

#include "hal/hal_config.h"

namespace sbx {
namespace hal {

bool wifiApStart() {
  // Ei NVS-kirjoitusta: persistent(false) -> esp_wifi_set_storage(WIFI_STORAGE_RAM)
  // ennen kuin mode() alustaa Wi-Fin (WiFiGeneric.cpp:693-694). Muuten softAP()
  // kirjoittaa asetukset flashiin aina kun ne poikkeavat tallennetuista
  // (WiFiAP.cpp:166-167), ja flash-kirjoitus pysäyttää molemmat ytimet.
  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_AP)) return false;
  // Bring-up-firmwaren arvo (§4.9). Huom: AP-tilassa tämä vain tallentaa
  // asetuksen - esp_wifi_set_ps kutsutaan vain, jos STA on päällä
  // (WiFiGeneric.cpp:1377-1390, 1046) - mutta pitää modem sleepin pois, jos
  // tilaksi joskus vaihdetaan AP+STA.
  WiFi.setSleep(false);
  return WiFi.softAP(kApSsid, kApPass, kApChannel, 0 /* ei piilotettu */, kApMaxClients);
}

uint32_t wifiApStations() { return static_cast<uint32_t>(WiFi.softAPgetStationNum()); }

void wifiApIp(char* buf, unsigned n) {
  const IPAddress ip = WiFi.softAPIP();
  snprintf(buf, n, "%u.%u.%u.%u", static_cast<unsigned>(ip[0]), static_cast<unsigned>(ip[1]),
           static_cast<unsigned>(ip[2]), static_cast<unsigned>(ip[3]));
}

}  // namespace hal
}  // namespace sbx
