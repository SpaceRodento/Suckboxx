/*=====================================================================
  sd_logger.h - microSD CSV history logger (M2, reTerminal E1001)

  Header-only, architecture.md § 8 graceful-degradation shape:
    1. presence/EN/mount all live in sdlog_init(), which returns bool and
       sets g_sdReady - never assumed true.
    2. Every loop-side entry point (sdlog_appendRow) starts with
       "if (!g_sdReady) return" via its own internal check.
    3. Runtime degradation: write failures increment a counter: after
       SD_LOG_MAX_CONSECUTIVE_FAILS the card is marked not-ready again and
       a periodic remount is retried every SD_LOG_RETRY_EVERY_N_CALLS.

  Row/filename formatting is pure logic in sd_logger_format.h (unit tested
  in test/test_sd_logger/) - this file only adds the SD/SPI hardware calls
  around it, mirroring the data_fetch.h / data_fetch_parse.h split.

  SPI is SHARED with the e-paper (SD_SCK_PIN/SD_MOSI_PIN == PIN_EPD_CLK/
  PIN_EPD_DIN, config.h). The e-paper's own CS is pulled HIGH before any SD
  transfer so SD traffic is never latched as an EPD command - same pattern
  smoke/sd_test.cpp proved on the bench (PR #249, 2.8.2026). EN pin
  polarity was undocumented at that point, so both levels are still probed
  here exactly as that smoke test did.

  ALL calls in this file are no-ops when ENABLE_SD_LOGGING is false (the
  shipped default) - the production image is unaffected until a rauta-
  smoke run flips the flag.
=====================================================================*/

#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include "config.h"
#include "display_data.h"
#include "sd_logger_format.h"

#if ENABLE_SD_LOGGING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#endif

static bool     g_sdReady            = false;
static bool     g_sdEnLevelHigh      = true;   // toimiva EN-taso (probattu initissa)
static uint32_t g_sdConsecutiveFails = 0;
static uint32_t g_sdRetryCounter     = 0;

static bool sdlog_isReady() { return g_sdReady; }

#if ENABLE_SD_LOGGING

// EPD:n CS HIGH:iin ennen SD-liikennetta - sama kuvio kuin smoke/sd_test.cpp.
static void sdlog_deselectEpd() {
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
}

static bool sdlog_tryMount(int enLevel) {
  SD.end();
  pinMode(SD_EN_PIN, OUTPUT);
  digitalWrite(SD_EN_PIN, enLevel);
  delay(100);                                   // anna korttipaikan rail asettua
  return SD.begin(SD_CS_PIN, SPI, SD_LOG_SPI_HZ);
}

#endif // ENABLE_SD_LOGGING

// 1. DET (kortin lasnaolo raudasta) 2. EN-pinni + polariteetin haku
// 3. SD.begin(). Palauttaa g_sdReady:n; kutsuja (setup()) paattaa jatkosta.
static bool sdlog_init() {
#if !ENABLE_SD_LOGGING
  g_sdReady = false;
  return false;
#else
  pinMode(SD_DET_PIN, INPUT_PULLUP);
  delay(10);
  int detLevel = digitalRead(SD_DET_PIN);
  DEBUG_PRINTF("[INFO]  SD: DET pinni %d = %d (aktiivitaso vahvistamaton)\n",
              SD_DET_PIN, detLevel);

  sdlog_deselectEpd();
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);

  if (sdlog_tryMount(HIGH)) {
    g_sdEnLevelHigh = true;
  } else if (sdlog_tryMount(LOW)) {
    g_sdEnLevelHigh = false;
  } else {
    g_sdReady = false;
    DEBUG_PRINT("SD: mount epaonnistui molemmilla EN-tasoilla - advisory, ei kaadu");
    return false;
  }

  g_sdReady = true;
  g_sdConsecutiveFails = 0;
  g_sdRetryCounter = 0;
  DEBUG_PRINTF("[INFO]  SD: mount OK (EN=%s)\n", g_sdEnLevelHigh ? "HIGH" : "LOW");
  return true;
#endif
}

#if ENABLE_SD_LOGGING
// Kirjoittaa otsikon (meta + CSV-header) vain kun tiedosto ei viela ole
// olemassa - sama saanto kuin smoke/sd_test.cpp:ssa.
static bool sdlog_ensureHeader(const char* path) {
  if (SD.exists(path)) return true;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  char meta[48], header[256];
  sdlog_metaLine(meta, sizeof(meta));
  sdlog_headerLine(header, sizeof(header));
  f.println(meta);
  f.println(header);
  f.flush();
  f.close();
  return true;
}
#endif

// Kutsutaan siita kohdasta jossa /api/state on juuri parsittu onnistuneesti
// (data_fetch.h) - ratsastaa olemassa olevalla hakusyklilla, ei uutta
// pollausta. Avaa tiedoston, appendaa YHDEN rivin, flush+close SAMASSA
// kutsussa (virtakatko menettaa korkeintaan yhden rivin).
static void sdlog_appendRow(const DisplayData* d) {
#if !ENABLE_SD_LOGGING
  (void)d;
#else
  if (!g_sdReady) {
    // Periodinen uudelleenyritys - architecture.md § 8 runtime-degradation.
    if (++g_sdRetryCounter >= SD_LOG_RETRY_EVERY_N_CALLS) {
      g_sdRetryCounter = 0;
      DEBUG_PRINT("SD: yritetaan uudelleenmounttia (kortti oli poissa/vikatilassa)");
      if (sdlog_init()) DEBUG_PRINT("SD: re-detected, loggaus jatkuu");
    }
    return;
  }

  struct tm timeinfo;
  bool ntpValid = getLocalTime(&timeinfo, 200);   // lyhyt - ei saa hidastaa hakusykliä
  char iso[24] = "";
  char path[32];
  if (ntpValid) {
    snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    sdlog_buildFilename(path, sizeof(path), timeinfo.tm_year + 1900,
                        timeinfo.tm_mon + 1, timeinfo.tm_mday);
  } else {
    // Ei kalenteripaivaa tiedossa viela - yksi yhteinen tiedosto siihen asti
    // kun NTP synkkaantuu. ALA arvaa paivamaaraa (dokumentaatio.md); rivin
    // uptime_ms + time_source=uptime kertovat rehellisen totuuden.
    strncpy(path, "/sensors_pending_ntp.csv", sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
  }

  sdlog_deselectEpd();

  if (!sdlog_ensureHeader(path)) {
    g_sdConsecutiveFails++;
  } else {
    File f = SD.open(path, FILE_APPEND);
    if (!f) {
      g_sdConsecutiveFails++;
    } else {
      char row[256];
      sdlog_buildRow(d, ntpValid, iso, (uint32_t)millis(), row, sizeof(row));
      f.println(row);
      f.flush();
      f.close();
      g_sdConsecutiveFails = 0;
    }
  }

  if (g_sdConsecutiveFails >= SD_LOG_MAX_CONSECUTIVE_FAILS) {
    DEBUG_PRINT("SD: liikaa peräkkäisiä kirjoitusvirheitä - advisory, yritetaan uudelleen myöhemmin");
    g_sdReady = false;
    g_sdRetryCounter = 0;
  }
#endif
}

#endif // SD_LOGGER_H
