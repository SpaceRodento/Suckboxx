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

// HX710B tuottaa kiinteät 10 näytettä/s, joten 20 Hz kysely riittää reilusti.
// Tämä katto on tärkeä: ilman sitä loop pyörittää readRaw():ta niin tiheään,
// että noInterrupts()-jaksot syövät suurimman osan CPU-ajasta ja Wi-Fi AP:n
// beaconit myöhästyvät -> verkko katoaa puhelimen listalta.
constexpr unsigned long SAMPLE_INTERVAL_MS = 50;
constexpr unsigned long DIAG_INTERVAL_MS = 5000;

const char* const AP_SSID = "Suckboxx";
const char* const AP_PASSWORD = "alipaine123";  // vähintään 8 merkkiä (WPA2)
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CLIENTS = 4;

Hx710b sensors[CHANNEL_COUNT];
int32_t latestRaw[CHANNEL_COUNT] = {0, 0, 0};
int32_t zeroOffset[CHANNEL_COUNT] = {0, 0, 0};
unsigned long lastUpdateMs[CHANNEL_COUNT] = {0, 0, 0};
bool everRead[CHANNEL_COUNT] = {false, false, false};
uint32_t badReads[CHANNEL_COUNT] = {0, 0, 0};

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
  WiFi.setSleep(false);  // AP ei saa mennä modem sleepiin - katkoo yhteyksiä
  // Kiinteä kanava ja asiakasraja: automaattivalinta vaihtaa kanavaa lennossa,
  // mikä näkyy puhelimessa AP:n katoamisena ja uudelleenilmestymisenä.
  bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CLIENTS);
  if (!apOk) {
    Serial.println("[VIRHE] softAP-kaynnistys epaonnistui");
  }
  Serial.print("[INFO] Wi-Fi AP kaynnissa, SSID=");
  Serial.print(AP_SSID);
  Serial.print(" kanava=");
  Serial.print(AP_CHANNEL);
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

  static unsigned long lastSampleMs = 0;
  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
      if (!sensors[i].isReady()) {
        continue;
      }
      int32_t value = 0;
      if (!sensors[i].readRaw(value)) {
        badReads[i]++;  // kelluva tai jumissa oleva DOUT - ei oikeaa dataa
        continue;
      }
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

  static unsigned long lastDiagMs = 0;
  if (now - lastDiagMs >= DIAG_INTERVAL_MS) {
    lastDiagMs = now;
    Serial.printf(
      "[DIAG] up=%lus asiakkaita=%d heap=%u ika_ms=%lu/%lu/%lu virhelukuja=%lu/%lu/%lu\n",
      now / 1000, WiFi.softAPgetStationNum(), ESP.getFreeHeap(),
      now - lastUpdateMs[0], now - lastUpdateMs[1], now - lastUpdateMs[2],
      static_cast<unsigned long>(badReads[0]),
      static_cast<unsigned long>(badReads[1]),
      static_cast<unsigned long>(badReads[2]));
  }

  // Antaa idle-taskin ja Wi-Fi-pinon ajoaikaa. Ilman tätä loop on busy-loop,
  // joka nälkiinnyttää muut taskit ja laukaisee task watchdogin (CPU1).
  delay(2);
}
