/*=====================================================================
  juice_test.cpp - Pelituntuman rautakoe

  Suunnitelma ja hyvaksymiskriteerit:
  docs/kehitys/pelillistaminen-tietopohja.md § 4.5

  Mittaa kolme asiaa jotka ratkaisevat onko pelillistaminen tallä raudalla
  mahdollista:
    1. nappi -> aani                (tavoite < 20 ms)
    2. nappi -> palkki liikkunut    (tavoite < 600 ms, osittaispaivitys)
    3. ei koko ruudun vilkkua       (silmamaarainen havainto)

  Napit:
    GPIO3 (vihrea)      "tein taman" -> aani + yksi lohko palkkiin
    GPIO5 (valk. vasen) kierrata hit-stop-viive 0 -> 150 -> 400 ms
    GPIO4 (valk. oikea) EI KAYTOSSA - Seeed varaa sen heratykseen unesta

  Kaikki pinnit ovat paikallisia tarkoituksella: smoke-sketsi ei riipu
  tuotantokonfiguraatiosta eika tuo secrets.h-riippuvuutta. EPD-pinnit ovat
  sama nelikko kuin config.h:n ARDUINO_XIAO_ESP32S3-haara; SPI CLK/DIN
  tulevat XIAO:n oletus-SPI:sta kuten tuotannossakin.

  Nappi- ja summeripinnit ovat Seeedin ESPHome-dokumentaatiosta EIVATKA
  vahvistettuja tallä raudalla. Boot-loki tulostaa nappien lepotilan: jos se
  ei ole 1, pinni on vaara tai nappi ei ole pull-upissa. Vasta rautatestin
  jalkeen ne siirretaan config.h:hon.
=====================================================================*/

#include <Arduino.h>
#include <GxEPD2_BW.h>

// ── Pinnit ─────────────────────────────────────────────────────────
#define PIN_EPD_CS      10
#define PIN_EPD_DC      11
#define PIN_EPD_RST     12
#define PIN_EPD_BUSY    13

#define PIN_BTN_GREEN    3
#define PIN_BTN_LEFT     5
#define PIN_BUZZER      45
#define PIN_LED          6      // invertoitu: LOW = palaa

#define BUZZER_CH        0      // LEDC-kanava (core 2.0.x kanava-API)

// ── Palkin geometria ───────────────────────────────────────────────
// x ja leveys 8:n monikertoja: UC8179 rajaa osittaisikkunan 8 px:n
// tarkkuuteen, ja valmiiksi kohdistettu ikkuna estaa yllattavat
// pyoristykset lohkorajoilla.
#define BAR_X           80
#define BAR_Y          200
#define BAR_W          640
#define BAR_H           64
#define BAR_BLOCKS      20
#define BLOCK_W        (BAR_W / BAR_BLOCKS)   // 32 px
#define BAR_GAP          3                    // lohkojen valirako

// Osittaisikkuna kattaa palkin JA sen alla olevat lukemat yhdella
// paivityksella - kaksi erillista ikkunaa maksaisi 2 x 450 ms.
#define WIN_Y          (BAR_Y - 8)
#define WIN_H          (BAR_H + 108)

// ── Annettu edistyminen (endowed progress, § 4.3 G4) ───────────────
// Kausi ei ala nollasta: kaksi lohkoa on jo taynna.
#define START_BLOCKS     2

GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT>
  display(GxEPD2_750_GDEY075T7(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

static const uint16_t HITSTOP_MS[] = { 0, 150, 400 };   // § 4.3 G6
static uint8_t  g_hitstopIdx = 1;                       // oletus 150 ms
static uint8_t  g_blocks     = START_BLOCKS;
static uint32_t g_lastSoundUs = 0;
static uint32_t g_lastDrawMs  = 0;
static uint16_t g_presses     = 0;

// ── Aani ───────────────────────────────────────────────────────────
// Nouseva savelkulku = onnistui, laskeva = ei nyt (§ 4.3 G1).
static void buzzOff() { ledcWriteTone(BUZZER_CH, 0); }

static void buzzNote(uint16_t hz, uint16_t ms) {
  ledcWriteTone(BUZZER_CH, hz);
  delay(ms);
  buzzOff();
}

// Kolikon kilahdus: kaksi nopeaa nousevaa savelta (E6 -> B6).
static void soundCoin() {
  buzzNote(1319, 60);
  buzzNote(1976, 90);
}

// Palkki taynna: nouseva neljan savelen kuvio.
static void soundFanfare() {
  const uint16_t notes[] = { 1047, 1319, 1568, 2093 };
  for (uint8_t i = 0; i < 4; i++) buzzNote(notes[i], 90);
}

// Asetusmuutos: yksi neutraali napsu, ei palkintoaani.
static void soundBlip() { buzzNote(880, 40); }

// ── Piirto ─────────────────────────────────────────────────────────
static void drawTextAt(int x, int y, uint8_t size, const char* s) {
  display.setTextSize(size);
  display.setCursor(x, y);
  display.print(s);
}

// Palkin lohkot + lukemat. Kutsutaan sekä taysvirkistyksesta etta
// osittaispaivityksesta - sama piirtokoodi, eri ikkuna.
static void drawBarAndReadouts() {
  display.drawRect(BAR_X - 4, BAR_Y - 4, BAR_W + 8, BAR_H + 8, GxEPD_BLACK);

  for (uint8_t i = 0; i < BAR_BLOCKS; i++) {
    int bx = BAR_X + i * BLOCK_W;
    int bw = BLOCK_W - BAR_GAP;
    if (i < g_blocks) display.fillRect(bx, BAR_Y, bw, BAR_H, GxEPD_BLACK);
    else              display.drawRect(bx, BAR_Y, bw, BAR_H, GxEPD_BLACK);
  }

  char line[80];
  snprintf(line, sizeof(line), "%d / %d", g_blocks, BAR_BLOCKS);
  drawTextAt(BAR_X, BAR_Y + BAR_H + 24, 3, line);

  snprintf(line, sizeof(line), "hit-stop %d ms   aani %lu us   piirto %lu ms",
           HITSTOP_MS[g_hitstopIdx],
           (unsigned long)g_lastSoundUs,
           (unsigned long)g_lastDrawMs);
  drawTextAt(BAR_X, BAR_Y + BAR_H + 64, 2, line);
}

// Koko ruutu: kehys + ohjeet. Kalliin taysvirkistyksen (1200 ms) paikka.
static void drawFrameFull() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    drawTextAt(BAR_X, 60, 4, "PELITUNTUMAN KOE");
    drawTextAt(BAR_X, 110, 2, "vihrea nappi = tein taman   vasen valkoinen = hit-stop");

    drawBarAndReadouts();

    drawTextAt(BAR_X, 430, 2, "tavoite: aani < 20 ms, piirto < 600 ms, ei koko ruudun vilkkua");
  } while (display.nextPage());
}

// Vain palkki + lukemat. Tama on se 450 ms jonka takia koko koe tehdaan.
static uint32_t drawBarPartial() {
  uint32_t t0 = millis();
  display.setPartialWindow(BAR_X - 8, WIN_Y, BAR_W + 16, WIN_H);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    drawBarAndReadouts();
  } while (display.nextPage());
  return millis() - t0;
}

// ── Napit ──────────────────────────────────────────────────────────
static bool waitRelease(uint8_t pin) {
  uint32_t t0 = millis();
  while (digitalRead(pin) == LOW) {
    if (millis() - t0 > 3000) return false;   // jumissa: alä lukitu
    delay(5);
  }
  delay(30);                                   // debounce vapautuksen jalkeen
  return true;
}

static void onGreenPress() {
  // AANI ENSIN, ennen debounceria ja ennen mitaan muuta: se on koko
  // arkkitehtuurin ydin (§ 4.2). Latenssi mitataan tahan ensimmaiseen
  // ledcWriteTone-kutsuun.
  uint32_t t0 = micros();
  ledcWriteTone(BUZZER_CH, 1319);
  g_lastSoundUs = micros() - t0;

  digitalWrite(PIN_LED, LOW);                  // invertoitu: palaa
  delay(60);
  buzzNote(1976, 90);                          // kilahduksen toinen savel
  digitalWrite(PIN_LED, HIGH);

  g_presses++;
  if (g_blocks < BAR_BLOCKS) g_blocks++;

  delay(HITSTOP_MS[g_hitstopIdx]);             // § 4.3 G6

  if (g_blocks >= BAR_BLOCKS) {
    // Palkki taynna: seremonia saa taysvirkistyksen (§ 4.3 G9), joka
    // samalla siivoaa osittaispaivitysten haamukuvan (§ 4.4).
    soundFanfare();
    g_lastDrawMs = 0;
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);
      drawTextAt(BAR_X, 220, 6, "TEHTY!");
      drawTextAt(BAR_X, 300, 2, "kausi alkaa uudelleen kahdesta lohkosta");
    } while (display.nextPage());
    delay(1500);
    g_blocks = START_BLOCKS;
    drawFrameFull();
    Serial.printf("[%u] PALKKI TAYNNA -> taysvirkistys + nollaus %d/%d\n",
                  g_presses, g_blocks, BAR_BLOCKS);
  } else {
    g_lastDrawMs = drawBarPartial();
    Serial.printf("[%u] aani %lu us | hit-stop %u ms | osittaispiirto %lu ms | %d/%d\n",
                  g_presses,
                  (unsigned long)g_lastSoundUs,
                  HITSTOP_MS[g_hitstopIdx],
                  (unsigned long)g_lastDrawMs,
                  g_blocks, BAR_BLOCKS);
  }

  waitRelease(PIN_BTN_GREEN);
}

static void onLeftPress() {
  g_hitstopIdx = (g_hitstopIdx + 1) % 3;
  soundBlip();
  g_lastDrawMs = drawBarPartial();
  Serial.printf("hit-stop -> %u ms (osittaispiirto %lu ms)\n",
                HITSTOP_MS[g_hitstopIdx], (unsigned long)g_lastDrawMs);
  waitRelease(PIN_BTN_LEFT);
}

// ── Setup / loop ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== juice_test: pelituntuman rautakoe ===");
  Serial.println("docs/kehitys/pelillistaminen-tietopohja.md § 4.5");

  pinMode(PIN_BTN_GREEN, INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT,  INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);                 // invertoitu: sammuksissa

  ledcSetup(BUZZER_CH, 2000, 10);
  ledcAttachPin(PIN_BUZZER, BUZZER_CH);
  buzzOff();

  // Pinnien maatotuus ennen kuin mitaan tulkitaan: lepotilan pitaa olla 1.
  Serial.printf("nappien lepotila: GPIO%d=%d  GPIO%d=%d  (odotus 1 = pull-up, ei painettu)\n",
                PIN_BTN_GREEN, digitalRead(PIN_BTN_GREEN),
                PIN_BTN_LEFT,  digitalRead(PIN_BTN_LEFT));

  display.init(115200, true, 2, false);
  display.setRotation(0);
  Serial.printf("naytto: %dx%d, hasFastPartialUpdate=%d, osittais %d ms, tays %d ms\n",
                display.width(), display.height(),
                (int)GxEPD2_750_GDEY075T7::hasFastPartialUpdate,
                (int)GxEPD2_750_GDEY075T7::partial_refresh_time,
                (int)GxEPD2_750_GDEY075T7::full_refresh_time);

  drawFrameFull();
  soundBlip();
  Serial.println("valmis. paina vihreaa nappia.");
}

void loop() {
  if (digitalRead(PIN_BTN_GREEN) == LOW) onGreenPress();
  if (digitalRead(PIN_BTN_LEFT)  == LOW) onLeftPress();
  delay(5);
}
