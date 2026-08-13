// smoke_wifi.h - Minimal WiFi + OTA + UDP-log helper for hw_smoke sketches.
//
// Sketsit voivat liittyä WiFiin, julkaista lokirivit UDP-broadcastina
// (yhteensopiva scripts/wireless_log_listener.py:n kanssa) ja vastaanottaa
// ElegantOTA-päivityksiä /update-osoitteessa. Eliminoi USB-flashin tarpeen
// ensimmäisen flashin jälkeen.
//
// Käyttö (per sketsi):
//
//   #include <Arduino.h>
//   #include "v1_pins.h"
//   #include "smoke_wifi.h"
//
//   void setup() {
//       smoke_log_begin("light_dead_man");  // hoitaa Serial.begin + WiFi-yhteyden
//       pinMode(PM_PIN_MOSFET_LIGHT, OUTPUT);
//   }
//
//   void loop() {
//       smoke_log_loop();                   // pakollinen: hoitaa OTA + WiFi-yhteyden
//       // ... sketsin oma logiikka ...
//       smoke_logf("button=%d\n", digitalRead(PM_PIN_BTN_USER));
//   }
//
// Vaatii secrets.h:n firmware/plantmeister/-kansiosta (samat makrot kuin
// pää-firmwaressa: WIFI_STA_SSID_DEFAULT, WIFI_STA_PASSWORD_DEFAULT,
// OTA_USERNAME, OTA_PASSWORD). hw_smoke/platformio.ini lisää sen include-pathiin.

#ifndef SMOKE_WIFI_H
#define SMOKE_WIFI_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <esp_ota_ops.h>

// Salaisuudet pää-firmwaren secrets.h:sta. Jos puuttuu, fallback-arvot
// joilla ei pääse mihinkään verkkoon — secrets.h on käytännössä pakollinen.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

#ifndef WIFI_STA_SSID_DEFAULT
  #define WIFI_STA_SSID_DEFAULT "PlantMeister-Setup"
#endif
#ifndef WIFI_STA_PASSWORD_DEFAULT
  #define WIFI_STA_PASSWORD_DEFAULT ""
#endif
#ifndef OTA_USERNAME
  #define OTA_USERNAME "admin"
#endif
#ifndef OTA_PASSWORD
  #define OTA_PASSWORD "admin"
#endif

#ifndef SMOKE_LOG_UDP_PORT
  #define SMOKE_LOG_UDP_PORT 14533
#endif

#ifndef SMOKE_WIFI_TIMEOUT_MS
  #define SMOKE_WIFI_TIMEOUT_MS 20000
#endif

// ── State ────────────────────────────────────────────────────────────
static WiFiUDP    g_smokeUdp;
static AsyncWebServer g_smokeWebServer(80);
static char       g_smokeSketchName[24] = "unnamed";
static bool       g_smokeWifiUp = false;
static unsigned long g_smokeLastBeacon = 0;

// ── Internal helpers ─────────────────────────────────────────────────

static void smoke_broadcast(const char* line, size_t len) {
    if (!g_smokeWifiUp) return;
    // Lähetä broadcast-osoitteeseen, sama portti kuin pää-firmware
    IPAddress bcast = WiFi.localIP();
    bcast[3] = 255;  // x.x.x.255
    g_smokeUdp.beginPacket(bcast, SMOKE_LOG_UDP_PORT);
    g_smokeUdp.write((const uint8_t*)line, len);
    g_smokeUdp.endPacket();
}

// ── Public API ───────────────────────────────────────────────────────

// Lähettää lokin sekä USB-Serialiin (jos kytketty) että UDP-broadcastina.
// printf-tyylinen. Pitää tehdä rivinvaihto itse jos haluat selkeät rivit.
inline void smoke_logf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
    Serial.write((const uint8_t*)buf, n);
    smoke_broadcast(buf, n);
}

inline void smoke_log(const char* line) {
    Serial.println(line);
    char buf[260];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n > 0) smoke_broadcast(buf, (size_t)n);
}

// Aja kerran setup():ssa. sketchName näkyy boot-bannerissa.
// Tekee: Serial.begin, WiFi.STA, ElegantOTA, UDP.begin.
// Ei blokkaa ikuisesti — jos WiFi ei nouse SMOKE_WIFI_TIMEOUT_MS sisään,
// jatkaa ilman WiFiä (sketsi toimii silti USB-Serial-lokin kanssa).
inline void smoke_log_begin(const char* sketchName) {
    Serial.begin(115200);
    delay(300);
    strncpy(g_smokeSketchName, sketchName, sizeof(g_smokeSketchName) - 1);
    g_smokeSketchName[sizeof(g_smokeSketchName) - 1] = '\0';

    Serial.println();
    Serial.printf("[smoke] sketch=%s build=%s %s\n", g_smokeSketchName, __DATE__, __TIME__);
    Serial.printf("[smoke] WiFi STA -> %s\n", WIFI_STA_SSID_DEFAULT);

    // Confirm this firmware as bootable so ESP32 won't roll back to the previous
    // OTA partition. Without this, an OTA-flashed smoke sketch reboots into the
    // main firmware as soon as the bootloader's pending-verify timer fires.
    {
        const esp_partition_t* running = esp_ota_get_running_partition();
        if (running) {
            esp_ota_img_states_t state;
            if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
                state == ESP_OTA_IMG_PENDING_VERIFY) {
                esp_ota_mark_app_valid_cancel_rollback();
                Serial.println("[smoke] OTA: boot confirmed, rollback cancelled");
            }
        }
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID_DEFAULT, WIFI_STA_PASSWORD_DEFAULT);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < SMOKE_WIFI_TIMEOUT_MS) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[smoke] WiFi FAILED — jatkaen ilman WiFiä (vain USB-loki)");
        g_smokeWifiUp = false;
        return;
    }

    g_smokeWifiUp = true;
    IPAddress ip = WiFi.localIP();
    Serial.printf("[smoke] WiFi OK IP=%u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);

    // UDP broadcast valmiiksi
    g_smokeUdp.begin(SMOKE_LOG_UDP_PORT);

    // ElegantOTA /update-osoitteessa (Basic Auth)
    g_smokeWebServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        char body[200];
        snprintf(body, sizeof(body),
                 "<h3>smoke=%s</h3><p>OTA: <a href=\"/update\">/update</a></p>",
                 g_smokeSketchName);
        req->send(200, "text/html", body);
    });
    ElegantOTA.begin(&g_smokeWebServer, OTA_USERNAME, OTA_PASSWORD);
    g_smokeWebServer.begin();

    smoke_logf("[smoke] OTA enabled: http://%u.%u.%u.%u/update\n",
               ip[0], ip[1], ip[2], ip[3]);
}

// Aja joka loop()-iteraatiossa. Päivittää OTA:n ja lähettää periodisen
// beacon-rivin (5 s välein) — listener näkee että laite on elossa.
inline void smoke_log_loop() {
    if (g_smokeWifiUp) {
        ElegantOTA.loop();
        unsigned long now = millis();
        if (now - g_smokeLastBeacon >= 5000) {
            g_smokeLastBeacon = now;
            smoke_logf("[smoke] alive sketch=%s uptime=%lus\n",
                       g_smokeSketchName, now / 1000);
        }
    }
}

inline bool smoke_wifi_isUp() { return g_smokeWifiUp; }

#endif // SMOKE_WIFI_H
