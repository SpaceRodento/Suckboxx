// Suckboxx - ensimmäinen laiteohjelmisto (bring-up)
//
// Tarkoitus: lukea kolme HX710B-painesensoria ja näyttää lukemat kolmena
// palkkina + numerona puhelimen selaimessa laitteen omasta Wi-Fi AP:sta.
// Ei muuta. Kalibrointi (R4), käytösmatriisi (§8), rengaspuskuri (S5b) ja
// OTA on tietoisesti rajattu pois - ks. SUUNNITTELU.md.
//
// HUOM: HX710B on väliaikainen anturi. Lopullinen rauta käyttää analogista
// XGZP6847A:ta ADC1-pinneissä GPIO34/35/36 (docs/rauta.md §2.2). HX710B on
// digitaalinen (bittisarja), joten se ei istu noihin pinneihin sellaisenaan -
// DOUT-linjat käyttävät samoja GPIO-numeroita jatkuvuuden vuoksi (samat
// input-only-pinnit ovat turvallisia tähänkin), mutta SCK tarvitsee lisäksi
// kolme erillistä lähtöpinniä, joita lopullisessa suunnitelmassa ei ole.
//
// Pinnijako (protolevy/hyppylangat, ei sama kuin rauta.md:n lopullinen kytkentä):
//   HX710B #1 (SYL 1): SCK = GPIO25, DOUT = GPIO34
//   HX710B #2 (SYL 2): SCK = GPIO26, DOUT = GPIO35
//   HX710B #3 (SYL 3): SCK = GPIO27, DOUT = GPIO36
//   Tila-LED (sykkivä "elossa"-merkki): GPIO2
//   Kaikki HX710B-moduulit: VCC -> 3V3, GND -> GND (ESP32-devkitin oma kisko
//   käy tähän väliaikaiseen testiin - R6:n oma anturi-LDO koskee vasta
//   XGZP6847A-lopputoteutusta).

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include "hx710b.h"
#include "web_page.h"

namespace {

constexpr uint8_t CHANNEL_COUNT = 3;
constexpr uint8_t PIN_SCK[CHANNEL_COUNT] = {25, 26, 27};
constexpr uint8_t PIN_DOUT[CHANNEL_COUNT] = {34, 35, 36};
constexpr uint8_t PIN_LED = 2;
constexpr unsigned long LED_BLINK_MS = 500;

const char* const AP_SSID = "Suckboxx";
const char* const AP_PASSWORD = "alipaine123";  // vähintään 8 merkkiä (WPA2)

Hx710b sensors[CHANNEL_COUNT];
int32_t latestRaw[CHANNEL_COUNT] = {0, 0, 0};
int32_t zeroOffset[CHANNEL_COUNT] = {0, 0, 0};
unsigned long lastUpdateMs[CHANNEL_COUNT] = {0, 0, 0};
bool everRead[CHANNEL_COUNT] = {false, false, false};

WebServer server(80);

void handleRoot() {
  server.send_P(200, "text/html", WEB_PAGE_HTML);
}

void handleReadings() {
  unsigned long now = millis();
  char buf[384];
  snprintf(buf, sizeof(buf),
    "{\"uptime_ms\":%lu,\"ch\":["
    "{\"raw\":%ld,\"delta\":%ld,\"age_ms\":%lu},"
    "{\"raw\":%ld,\"delta\":%ld,\"age_ms\":%lu},"
    "{\"raw\":%ld,\"delta\":%ld,\"age_ms\":%lu}"
    "]}",
    now,
    static_cast<long>(latestRaw[0]), static_cast<long>(latestRaw[0] - zeroOffset[0]), now - lastUpdateMs[0],
    static_cast<long>(latestRaw[1]), static_cast<long>(latestRaw[1] - zeroOffset[1]), now - lastUpdateMs[1],
    static_cast<long>(latestRaw[2]), static_cast<long>(latestRaw[2] - zeroOffset[2]), now - lastUpdateMs[2]);
  server.send(200, "application/json", buf);
}

void handleZero() {
  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
    zeroOffset[i] = latestRaw[i];
  }
  server.send(200, "text/plain", "ok");
}

void handleNotFound() {
  server.send(404, "text/plain", "not found");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);

  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
    sensors[i].begin(PIN_SCK[i], PIN_DOUT[i]);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("[INFO] Wi-Fi AP kaynnissa, SSID=");
  Serial.print(AP_SSID);
  Serial.print(" IP=");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/readings", HTTP_GET, handleReadings);
  server.on("/api/zero", HTTP_POST, handleZero);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[INFO] Web-palvelin kaynnissa (portti 80)");
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
    if (sensors[i].isReady()) {
      int32_t value = sensors[i].readRaw();
      latestRaw[i] = value;
      lastUpdateMs[i] = now;
      if (!everRead[i]) {
        zeroOffset[i] = value;  // nollataan ensimmäiseen lukemaan, ei hyppyä käynnistyksessä
        everRead[i] = true;
      }
    }
  }

  static unsigned long lastBlinkMs = 0;
  if (now - lastBlinkMs >= LED_BLINK_MS) {
    lastBlinkMs = now;
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }
}
