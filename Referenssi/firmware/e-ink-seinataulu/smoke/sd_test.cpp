/*=====================================================================
  sd_test.cpp - microSD-kortin rautakoe (reTerminal E1001)

  Miksi: M2 (SD-logger) kirjoittaa CSV-rivin joka /api/state-haulla, jotta
  historiaa kertyy ilman erillista tietokonetta. Sita ennen pitaa TIETAA
  eika arvata, etta kortti vastaa talla raudalla ja etta se vastaa myos
  sen jalkeen kun e-paperi on kayttanyt samaa SPI-vaylaa.
  Suunnitelma: docs/kehitys/Fable_kehityspolku.md § 4 + M2

  Six things this proves, in order of what fails first in practice:
    1. DET pin      - is a card physically in the slot (read BEFORE power)
    2. EN polarity   - the slot's power enable; without it the card is mute.
                       Polarity is NOT documented, so both levels are tried
                       and the working one is reported.
    3. Mount + type  - SD.begin over the shared SPI bus, card type and size
    4. Long filename - "sensors_2026-08-02.csv" is what M2 will use; FAT32
                       long-name support is not free on every SD stack
    5. Write+verify  - one row, flush and close per row, read back and
                       compare. Loses at most one row on power cut.
    6. Shared SPI    - the whole point: repeat 5 AFTER a full e-paper
                       refresh has driven the same SCK/MOSI lines

  Panel shows the verdict so the result is readable at the bench without a
  serial monitor. The loop keeps appending one row every 10 s and prints a
  running tally, so capture_serial.ps1 (which does NOT reset the device)
  sees evidence without a reset - see docs/ohjeet/eink_operointi.md § 3-4.

  Pins are local to this sketch on purpose: they move to config.h only
  after this test has passed, same convention as juice_test.cpp.
  Source for the SD pins: Seeed Wiki (reTerminal E1001), 2.8.2026.

    pio run -e sd_test -t upload
=====================================================================*/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <GxEPD2_BW.h>

// ── Pinnit ─────────────────────────────────────────────────────────
#define PIN_SD_CS       14
#define PIN_SD_EN       16      // slot power enable - polarity probed below
#define PIN_SD_DET      15      // card presence, active level probed below
#define PIN_SD_SCK       7      // shared with e-paper
#define PIN_SD_MISO      8
#define PIN_SD_MOSI      9      // shared with e-paper

#define PIN_EPD_CS      10
#define PIN_EPD_DC      11
#define PIN_EPD_RST     12
#define PIN_EPD_BUSY    13

// Conservative clock: this test answers "does it work at all", not "how
// fast". M2 can raise it once the wiring is proven.
#define SD_SPI_HZ       4000000UL

#define ROUNDS_PER_PHASE   3    // testaus.md: yksi onnistuminen voi olla onnea
#define LOOP_APPEND_MS 10000

// Short 8.3-safe name and the long name M2 will actually use.
static const char* PATH_SHORT = "/sd_smoke.csv";
static const char* PATH_LONG  = "/sensors_2026-08-02.csv";
static const char* CSV_HEADER = "uptime_ms,round,write_ms,heap_free";

GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT>
  display(GxEPD2_750_GDEY075T7(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// ── Tulokset ───────────────────────────────────────────────────────
struct SdSmokeResult {
  int      detLevel      = -1;
  int      enLevel       = -1;      // level that mounted the card
  bool     mounted       = false;
  uint8_t  cardType      = 0;
  uint64_t cardSizeMB    = 0;
  bool     longNameOk    = false;
  uint8_t  okBeforeEpd   = 0;       // successful write+verify rounds
  uint8_t  okAfterEpd    = 0;
  uint32_t maxWriteMs    = 0;
  char     failNote[64]  = "";
};

static SdSmokeResult g_res;
static uint32_t g_loopRounds = 0;
static uint32_t g_loopFails  = 0;
static uint32_t g_nextAppend = 0;

static bool allPassed() {
  return g_res.mounted && g_res.longNameOk &&
         g_res.okBeforeEpd == ROUNDS_PER_PHASE &&
         g_res.okAfterEpd  == ROUNDS_PER_PHASE &&
         g_loopFails == 0;
}

// ── SD ─────────────────────────────────────────────────────────────

// The e-paper shares SCK/MOSI. Its CS must be high before we clock the
// card, or the panel would latch our SD traffic as commands.
static void deselectEpd() {
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
}

static bool tryMount(int enLevel) {
  SD.end();
  pinMode(PIN_SD_EN, OUTPUT);
  digitalWrite(PIN_SD_EN, enLevel);
  delay(100);                                  // let the slot rail settle
  return SD.begin(PIN_SD_CS, SPI, SD_SPI_HZ);
}

static const char* cardTypeName(uint8_t t) {
  switch (t) {
    case CARD_NONE:  return "NONE";
    case CARD_MMC:   return "MMC";
    case CARD_SD:    return "SDSC";
    case CARD_SDHC:  return "SDHC";
    default:         return "TUNTEMATON";
  }
}

// Header is written only when the file does not exist yet - same rule the
// real logger needs, so it is worth exercising here.
static bool ensureHeader(const char* path) {
  if (SD.exists(path)) return true;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  f.println(CSV_HEADER);
  f.flush();
  f.close();
  return true;
}

// Append one row, flush+close it, then verify by reading the file back and
// comparing the LAST line. Reading back is the only honest proof that the
// bytes left the MCU; a successful write() return value is not.
static bool appendAndVerify(const char* path, uint32_t round, uint32_t* writeMsOut) {
  char row[96];
  snprintf(row, sizeof(row), "%lu,%lu,0,%lu",
           (unsigned long)millis(), (unsigned long)round,
           (unsigned long)ESP.getFreeHeap());

  uint32_t t0 = millis();
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    snprintf(g_res.failNote, sizeof(g_res.failNote), "open FILE_APPEND epaonnistui: %s", path);
    return false;
  }
  f.println(row);
  f.flush();
  f.close();
  uint32_t writeMs = millis() - t0;
  if (writeMsOut) *writeMsOut = writeMs;
  if (writeMs > g_res.maxWriteMs) g_res.maxWriteMs = writeMs;

  File r = SD.open(path, FILE_READ);
  if (!r) {
    snprintf(g_res.failNote, sizeof(g_res.failNote), "takaisinluku ei auennut: %s", path);
    return false;
  }
  char last[96] = "";
  while (r.available()) {
    String line = r.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      strncpy(last, line.c_str(), sizeof(last) - 1);
      last[sizeof(last) - 1] = '\0';
    }
  }
  r.close();

  bool match = (strcmp(last, row) == 0);
  if (!match) {
    snprintf(g_res.failNote, sizeof(g_res.failNote), "takaisinluku eroaa: '%s'", last);
  }
  return match;
}

// Printed from setup AND from every loop cycle. capture_serial.ps1 does not
// reset the device, so it opens the port well after setup has finished -
// a summary that exists only in setup would never be seen without a reset.
static void printSummary() {
  Serial.printf("YHTEENVETO: DET=%d  mount=%d  EN=%s  tyyppi=%s  %llu MB  "
                "pitka_nimi=%d  ennen=%u/%u  jalkeen=%u/%u  hitain_rivi=%lu ms  TULOS=%s\n",
                g_res.detLevel,
                (int)g_res.mounted,
                g_res.enLevel < 0 ? "-" : (g_res.enLevel ? "HIGH" : "LOW"),
                cardTypeName(g_res.cardType),
                (unsigned long long)g_res.cardSizeMB,
                (int)g_res.longNameOk,
                g_res.okBeforeEpd, ROUNDS_PER_PHASE,
                g_res.okAfterEpd, ROUNDS_PER_PHASE,
                (unsigned long)g_res.maxWriteMs,
                allPassed() ? "LAPI" : "EI LAPI");
  if (g_res.failNote[0]) Serial.printf("  huomio: %s\n", g_res.failNote);
}

static uint8_t runWriteRounds(const char* label) {
  uint8_t ok = 0;
  for (uint8_t i = 1; i <= ROUNDS_PER_PHASE; i++) {
    uint32_t ms = 0;
    bool pass = appendAndVerify(PATH_SHORT, i, &ms);
    Serial.printf("  %s kierros %u/%u: %s (kirjoitus+flush %lu ms)\n",
                  label, i, ROUNDS_PER_PHASE, pass ? "OK" : "FAIL",
                  (unsigned long)ms);
    if (pass) ok++;
    delay(50);
  }
  return ok;
}

// ── Paneeli ────────────────────────────────────────────────────────
static void drawTextAt(int x, int y, uint8_t size, const char* s) {
  display.setTextSize(size);
  display.setCursor(x, y);
  display.print(s);
}

static void drawVerdict() {
  char line[120];
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    drawTextAt(40, 60, 4, "microSD - RAUTAKOE");
    drawTextAt(40, 110, 2, "docs/kehitys/Fable_kehityspolku.md M2");

    drawTextAt(40, 170, 5, allPassed() ? "LAPI" : "EI LAPI");

    int y = 250;
    snprintf(line, sizeof(line), "DET pinni %d = %d", PIN_SD_DET, g_res.detLevel);
    drawTextAt(40, y, 2, line); y += 34;

    snprintf(line, sizeof(line), "EN pinni %d: toimiva taso = %s",
             PIN_SD_EN, g_res.enLevel < 0 ? "EI KUMPIKAAN" : (g_res.enLevel ? "HIGH" : "LOW"));
    drawTextAt(40, y, 2, line); y += 34;

    snprintf(line, sizeof(line), "kortti: %s, %llu MB",
             cardTypeName(g_res.cardType), (unsigned long long)g_res.cardSizeMB);
    drawTextAt(40, y, 2, line); y += 34;

    snprintf(line, sizeof(line), "pitka tiedostonimi: %s", g_res.longNameOk ? "OK" : "FAIL");
    drawTextAt(40, y, 2, line); y += 34;

    snprintf(line, sizeof(line), "kirjoitus+luku: ennen e-paperia %u/%u, sen jalkeen %u/%u",
             g_res.okBeforeEpd, ROUNDS_PER_PHASE, g_res.okAfterEpd, ROUNDS_PER_PHASE);
    drawTextAt(40, y, 2, line); y += 34;

    snprintf(line, sizeof(line), "hitain rivi: %lu ms", (unsigned long)g_res.maxWriteMs);
    drawTextAt(40, y, 2, line); y += 34;

    if (g_res.failNote[0]) {
      drawTextAt(40, y, 2, g_res.failNote); y += 34;
    }

    drawTextAt(40, 450, 2, "loop lisaa rivin 10 s valein - sarjaloki kertoo tuloksen");
  } while (display.nextPage());
}

// ── Setup / loop ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== sd_test: microSD-rautakoe (reTerminal E1001) ===");
  Serial.printf("pinnit: CS=%d EN=%d DET=%d SCK=%d MISO=%d MOSI=%d @ %lu Hz\n",
                PIN_SD_CS, PIN_SD_EN, PIN_SD_DET, PIN_SD_SCK, PIN_SD_MISO,
                PIN_SD_MOSI, (unsigned long)SD_SPI_HZ);

  deselectEpd();

  // -- 1. DET before power: is a card physically there --------------
  pinMode(PIN_SD_DET, INPUT_PULLUP);
  delay(10);
  g_res.detLevel = digitalRead(PIN_SD_DET);
  Serial.printf("1. DET pinni %d = %d  (aktiivitaso vahvistamaton - tulkitse "
                "vertaamalla kortti paikallaan / poissa)\n",
                PIN_SD_DET, g_res.detLevel);

  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, -1);

  // -- 2. EN polarity: undocumented, so probe both ------------------
  Serial.println("2. EN-polariteetin haku (HIGH ensin, sitten LOW)");
  if (tryMount(HIGH)) {
    g_res.enLevel = HIGH;
  } else {
    Serial.println("   HIGH ei mountannut - kokeillaan LOW");
    if (tryMount(LOW)) g_res.enLevel = LOW;
  }
  g_res.mounted = (g_res.enLevel >= 0);
  Serial.printf("   tulos: %s%s\n",
                g_res.mounted ? "mount OK, toimiva EN-taso = " : "MOUNT EPAONNISTUI molemmilla tasoilla",
                g_res.mounted ? (g_res.enLevel ? "HIGH" : "LOW") : "");

  if (!g_res.mounted) {
    snprintf(g_res.failNote, sizeof(g_res.failNote), "SD.begin epaonnistui molemmilla EN-tasoilla");
    Serial.println("   -> kortti, kortinlukija tai pinnikartta on pielessa. Loput vaiheet ohitetaan.");
  } else {
    // -- 3. Card identity -------------------------------------------
    g_res.cardType   = SD.cardType();
    g_res.cardSizeMB = SD.cardSize() / (1024ULL * 1024ULL);
    Serial.printf("3. kortti: tyyppi %s, koko %llu MB, kaytossa %llu MB\n",
                  cardTypeName(g_res.cardType),
                  (unsigned long long)g_res.cardSizeMB,
                  (unsigned long long)(SD.usedBytes() / (1024ULL * 1024ULL)));
    if (g_res.cardSizeMB > 32768ULL) {
      Serial.println("   VAROITUS: yli 32 GB - exFAT ei ole taattu, formatoi FAT32");
    }

    // -- 4. Long filename (what M2 will use) ------------------------
    g_res.longNameOk = ensureHeader(PATH_LONG) && SD.exists(PATH_LONG);
    Serial.printf("4. pitka tiedostonimi %s: %s\n",
                  PATH_LONG, g_res.longNameOk ? "OK" : "FAIL");

    // -- 5. Write + read back, BEFORE the panel touches the bus -----
    Serial.println("5. kirjoitus+takaisinluku ENNEN e-paperin SPI-kayttoa");
    if (!ensureHeader(PATH_SHORT)) {
      Serial.println("   otsikkorivin kirjoitus epaonnistui");
    }
    g_res.okBeforeEpd = runWriteRounds("ennen");
  }

  // -- 6. Panel takes the bus, then the card must still answer ------
  Serial.println("6. e-paperin init + taysvirkistys (sama SCK/MOSI)");
  display.init(115200, true, 2, false);
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    drawTextAt(40, 200, 4, "microSD-koe kesken...");
  } while (display.nextPage());
  Serial.println("   taysvirkistys tehty");

  if (g_res.mounted) {
    Serial.println("   kirjoitus+takaisinluku e-paperin JALKEEN");
    deselectEpd();
    g_res.okAfterEpd = runWriteRounds("jalkeen");
  }

  Serial.println("--- YHTEENVETO ---");
  printSummary();

  drawVerdict();
  Serial.println("valmis. loop lisaa yhden rivin 10 s valein (kestotodiste).");
  g_nextAppend = millis();
}

void loop() {
  if (!g_res.mounted) {
    delay(5000);
    Serial.println("SD ei mountattu - ei kestotestia. Tarkista kortti ja kytkenta.");
    printSummary();
    return;
  }

  if ((int32_t)(millis() - g_nextAppend) < 0) {
    delay(50);
    return;
  }
  g_nextAppend = millis() + LOOP_APPEND_MS;

  g_loopRounds++;
  uint32_t ms = 0;
  bool pass = appendAndVerify(PATH_SHORT, 1000 + g_loopRounds, &ms);
  if (!pass) g_loopFails++;

  Serial.printf("kesto: kierros %lu, %s, %lu ms, virheita %lu, heap %lu\n",
                (unsigned long)g_loopRounds, pass ? "OK" : "FAIL",
                (unsigned long)ms, (unsigned long)g_loopFails,
                (unsigned long)ESP.getFreeHeap());
  printSummary();
}
