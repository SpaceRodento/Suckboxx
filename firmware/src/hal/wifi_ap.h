#pragma once
// Suckboxx ESP32-liima - Wi-Fi-tukiasema (docs/firmware.md §4.9).
//
// SSID Suckboxx, WPA2 alipaine123, kiinteä kanava 1, enintään 4 asiakasta,
// WiFi.setSleep(false). Arvot ja kiinteä kanava bring-up-firmwaresta (b838911):
// automaattinen kanavavalinta näkyi puhelimessa AP:n katoamisena.

#include <cstdint>

namespace sbx {
namespace hal {

bool wifiApStart();          // setupista, ennen webStartia
uint32_t wifiApStations();   // yhdistetyt asemat (esp_wifi_ap_get_sta_list)
void wifiApIp(char* buf, unsigned n);  // "192.168.4.1"

}  // namespace hal
}  // namespace sbx
