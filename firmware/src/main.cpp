// Suckboxx - XGZP6847A-arkkitehtuuri (virallinen nykyinen konfiguraatio)
//
// Tämän firmware-version peruslinja on siivottu pois vanhasta HX710B-
// bring-up -arkkitehtuurista. Vanha digitaalinen prototyyppi on hylätty ja
// uusi analoginen anturiverkko on käytössä. Kaikki toimiva kehitys perustuu
// nyt XGZP6847A -sensorien analogiseen ADC-reittiin, ei HX710B:n bititason
// protokollaan.
//
// Kannettava konfiguraatio (virallinen pinni kartta):
//   CH1 (SYL 1, pääkanava): GPIO34 (ADC1_CH6)
//   CH2 (SYL 2):            GPIO39 "VN" (ADC1_CH3)
//   CH3 (SYL 3):            GPIO32 (ADC1_CH4)
//   Tila-LED:              GPIO2
//
// Huomio: GPIO36 on devkitin silkkipainatuksessa "VP", ei "D36". Tämä oli
// prototyypin alussa virheellinen valinta, ja CH3 päätyi lopulta GPIO32:een.
// Tämä kanava on nyt virallinen käytössä oleva konfiguraatio.
//
// HUOM: GPIO25 EI KELPAA tähän - se on ADC2, joka ei toimi Wi-Fi AP:n kanssa.
// Anturien VDD/GND-kytkentä: ks. docs/rauta.md §6.6 (3V3_SENS-kisko,
// EI ESP32:n omaa 3V3-pinniä - ks. §3.3 miksi).

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include "web_page.h"

namespace {

constexpr uint8_t CHANNEL_COUNT = 3;
constexpr uint8_t PIN_ADC[CHANNEL_COUNT] = {34, 39, 32};
constexpr uint8_t PIN_LED = 2;
constexpr unsigned long LED_BLINK_MS = 500;

// Analogisessa ADC-reitissä ei ole digitaalisen HX710B:n kaltaista
// "näyte valmis" -lippua, joten kiinteä kyselyväli riittää. 20 Hz on
// reilusti UI:n tarpeisiin - tarkempi 1 kHz/desimointi-putki (rauta.md
// §3.2) on tietoisesti S5b:n asia.
constexpr unsigned long SAMPLE_INTERVAL_MS = 50;
// 200 ms -> 5 tulostusta/s, riittää näkemään puhalluksen nousun/laskun serial
// monitorissa ilman että konsoli tulvii.
constexpr unsigned long RAW_PRINT_INTERVAL_MS = 200;

const char* const AP_SSID = "Suckboxx";
const char* const AP_PASSWORD = "alipaine123";  // vähintään 8 merkkiä (WPA2)
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CLIENTS = 4;

int32_t latestRaw[CHANNEL_COUNT] = {0, 0, 0};
int32_t zeroOffset[CHANNEL_COUNT] = {0, 0, 0};
unsigned long lastUpdateMs[CHANNEL_COUNT] = {0, 0, 0};
bool everRead[CHANNEL_COUNT] = {false, false, false};

// Datasheetin mukaan anturin ulostulo pysyy aina välillä 0,2-2,7 V, eli
// 12-bittinen lukema ei normaalikäytössä koskaan ole lähellä 0 tai 4095.
// Jos on, ADC-linja on poikki tai oikosulussa eikä lukema ole käyttökelpoinen.
bool isRailed(int raw) { return raw <= 4 || raw >= 4090; }

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

  // 12-bittinen lukema ja 11 dB:n vaimennus kattaa n. 0,15-3,10 V lineaarisesti
  // (rauta.md §1.2) - anturin koko 0,2-2,7 V ulostuloalue mahtuu tähän.
  analogReadResolution(12);
  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
    analogSetPinAttenuation(PIN_ADC[i], ADC_11db);
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
      // ESP32:n ADC1:n tunnettu ominaisuus: peräkkäin luettujen kanavien
      // sample-and-hold-kondensaattori ei ehdi tyhjentyä täysin edellisestä
      // kanavasta ennen tätä, jolloin osa edellisen arvosta "vuotaa" tähän
      // lukemaan (korkeaimpedanssinen 10 kOhm-lähde pahentaa tätä). Hylätty
      // lämmittelyluku samasta kanavasta antaa S&H:n asettua ennen oikeaa
      // mittausta.
      analogRead(PIN_ADC[i]);
      int raw = analogRead(PIN_ADC[i]);
      if (isRailed(raw)) {
        continue;  // linja poikki tai oikosulussa - ei oikeaa dataa
      }
      latestRaw[i] = raw;
      lastUpdateMs[i] = now;
      if (!everRead[i]) {
        zeroOffset[i] = raw;  // nollataan ensimmäiseen lukemaan, ei hyppyä käynnistyksessä
        everRead[i] = true;
      }
    }
  }

  static unsigned long lastBlinkMs = 0;
  if (now - lastBlinkMs >= LED_BLINK_MS) {
    lastBlinkMs = now;
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }

  static unsigned long lastPrintMs = 0;
  if (now - lastPrintMs >= RAW_PRINT_INTERVAL_MS) {
    lastPrintMs = now;
    Serial.printf("%ld\t%ld\t%ld\n",
      static_cast<long>(latestRaw[0]),
      static_cast<long>(latestRaw[1]),
      static_cast<long>(latestRaw[2]));
  }

  // Antaa idle-taskin ja Wi-Fi-pinon ajoaikaa. Ilman tätä loop on busy-loop,
  // joka nälkiinnyttää muut taskit ja laukaisee task watchdogin (CPU1).
  delay(2);
}
