/*=====================================================================
  config.h - E-ink Wall Display Configuration

  Waveshare 7.5" V2 (800x480) + ESP32 DevKit V1
  Connects to KasviAsema Gateway REST API via WiFi.
=====================================================================*/

#ifndef CONFIG_H
#define CONFIG_H

// ═══════════════════════════════════════════════════════════════════
// WIFI — credentials in secrets.h (gitignored)
// ═══════════════════════════════════════════════════════════════════

// Skipped in native unit test builds so a developer's local (gitignored)
// secrets.h never leaks into test assertions — always the committed
// secrets.h.example there, matching PLANTMEISTER_NATIVE_TEST's guard in
// firmware/plantmeister/config.h.
#if !defined(EINK_NATIVE_TEST) && __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.h.example"
#endif
// secrets file must define:
//   #define WIFI_SSID
//   #define WIFI_PASSWORD
//   #define PLANTMEISTER_HOST   ("plantmeister.local" or IP)

#define WIFI_CONNECT_TIMEOUT_MS   15000
#define WIFI_RETRY_DELAY_MS       5000

// ═══════════════════════════════════════════════════════════════════
// PLANTMEISTER API
//
// Data source is the PlantMeister XIAO firmware (firmware/plantmeister/),
// which serves a hierarchical /api/state JSON via ESPAsyncWebServer on
// port 80. mDNS hostname is set by WIFI_MDNS_HOSTNAME on the PM side.
// ═══════════════════════════════════════════════════════════════════

#define PLANTMEISTER_PORT         80
#define API_STATE_PATH            "/api/state"
#define API_TIMEOUT_MS            10000

// Keskitehon jarkevyysraja hetkelliseen tehoon nahden. INA228:n varausrekisteri
// ei nollaudu ESP32:n SW-resetissa, joten flashin jalkeen "varaus / uptime"
// yliarvioi rajusti (mitattu 28.7.2026: 47 W kun todellinen oli 0.68 W).
// Talla kertoimella ylittava keskiteho tulkitaan desynkiksi ja piilotetaan.
#define POWER_AVG_SANITY_RATIO    5.0f

// ═══════════════════════════════════════════════════════════════════
// DISPLAY — Waveshare 7.5" V2 (800x480)
// ═══════════════════════════════════════════════════════════════════

#define DISPLAY_WIDTH             800
#define DISPLAY_HEIGHT            480

// Optional custom bitmap overlay for right-side art area.
// true  = draw user bitmap from artwork/generated/eink_user_art_bitmap.h
// false = draw built-in ASCII plant art from plant_art.h
#define ENABLE_EINK_USER_ART      false

// ─── View profile selection ────────────────────────────────────────
// Pick which renderer the device uses. Compile-time only.
//   0 = EINK_VIEW_USER          (USER-layout ilman neuvontabanneria: display_layout.h)
//   1 = EINK_VIEW_DEV_DASHBOARD (tiivis diagnostiikka: dev_view.h)
//   2 = EINK_VIEW_RAW_TABLE     (raakadatataulukko: dev_table_view.h)
//   3 = EINK_VIEW_COACH         (kasvivalmentaja: coach_view.h). Rautatestattu
//       (COM5/e1001), sama layout/fontit kuin USER + neuvontabanneri.
//   4 = EINK_VIEW_STATUS        (tilannenaytto/SEURANTA: status_view.h). **Uusi
//       paanakyma (pe-ohjausmalli V1):** kortit yhdelle rivilla (tavoite vs.
//       nykyarvo) + valmennuspaneeli (HOIDAN/AUTA). Ks. eink_operointi.md § 11.
//
// Profile names are defined in view_profiles.h.
// Legacy flag ENABLE_EINK_DEV_VIEW (below) maps to DEV_DASHBOARD when
// EINK_VIEW_PROFILE is not explicitly set — left for back-compat.
#define EINK_VIEW_PROFILE         4   // 0=USER, 1=DEV_DASHBOARD, 2=RAW_TABLE, 3=COACH, 4=STATUS (uusi oletus)

// Legacy boolean — kept so old configs keep working. Prefer EINK_VIEW_PROFILE.
#define ENABLE_EINK_DEV_VIEW      false

// ─── Kastelutilan nakyvyys (NFT vs. ebb&flow) ──────────────────────
// false (oletus) = NFT: "Toiminta"-rivilta piilotetaan tulvitukseen liittyvat
//   tilat (Tulvitus / Liotus / Allas tyhjenee / "Seuraava tulva ~N min").
//   Kasvava laite nayttaa "Kasvatus kaynnissa"; kierto ja vesivika nakyvat
//   yha. true = ebb&flow: alkuperainen kaytos, tulvatila ja -laskuri esilla.
// Erillinen web-UI:n tulvakenttien piilotuksesta (PR #222): laite ajaa NFT:ta
// vaikka PM-firmwaressa ENABLE_EBB_FLOW olisi paalla ja se raportoisi tulvia.
#define ENABLE_EINK_FLOOD_STATUS  false

// ─── COACH-profiilin neuvontarivin kynnysarvot ──────────────────────
// VPD/PPFD FALLBACK-arvot (M1, 11.8.2026) — eivät enää ensisijainen lähde.
// resolve_advisory.h lukee ensin PM:n /api/state:n julkaiseman per-vaihe-
// kaistan (status_targetsForData(), GrowPhaseParams structs.h:ssa) ja
// putoaa naihin yleispateviin kirjallisuusarvoihin vain jos laite ei
// julkaissut mitaan (vanha firmware ilman M1-kenttaa, tai ei aktiivista
// kasvatusvaihetta). Ei ohjaukseen kaytettya arvoa kummassakaan
// tapauksessa — vain karkea "onko tama ihan pielessa" -halytys.
#define COACH_VPD_COMFORT_MIN_KPA      0.4f    // alle → liian kostea
#define COACH_VPD_COMFORT_MAX_KPA      1.6f    // yli → liian kuiva kasville
#define COACH_PPFD_LIGHTS_ON_MIN_UMOL  100.0f  // valot päällä mutta PPFD alle → heikko valo

// Yläraja lisätty 29.7.2026 rautahavainnon jälkeen: kasvit siirrettiin
// parvekkeelle ja anturi mittasi 795 µmol taimivaiheessa (tavoite 100–200).
// Alarajalle oli sääntö, ylärajalle ei — eli näyttö ei sanonut mitään juuri
// siinä tilanteessa jossa liika valo on todellinen riski. Kynnys on karkea
// yleisraja lehtivihanneksille: sen yli mennään vain suorassa auringossa tai
// aivan lampun alla, ja molemmissa varjostus on oikea toimenpide.
#define COACH_PPFD_MAX_UMOL            600.0f  // yli → liikaa valoa, varjosta

// Ilman lämpötilan yläraja. Erillinen VPD:stä tarkoituksella: kuumuus ja kuiva
// ilma esiintyvät yhdessä (43,7 °C / 23,7 % RH / VPD 6,9 kPa mitattiin
// 29.7.2026), mutta niiden korjaus on eri — kosteuden lisäys ei auta jos
// perussyy on paahde. Kuumuus siksi VPD:n YLÄPUOLELLA prioriteetissa.
#define COACH_AIR_TEMP_MAX_C           32.0f   // yli → kuumuusstressi

// Growth animation renderer (species-specific with generic fallback).
// Active only when ENABLE_EINK_USER_ART is false.
#define ENABLE_EINK_GROWTH_ANIMATION true
#define EINK_GROWTH_FRAME_COUNT      6

// Hybrid frame selection tuning: phase/day drives baseline, sensor refines it.
#define EINK_GROWTH_PHASE_COUNT_HINT 4
#define EINK_GROWTH_SENSOR_MIN_MM    20
#define EINK_GROWTH_SENSOR_MAX_MM    300

// ─── Render optimization ────────────────────────────────────────────
// Render only when the VISIBLE content changes (saves the e-ink panel's
// limited full-refresh budget and, in deep sleep, returns to sleep faster
// when nothing changed). The clock and "edellinen X min" footer are NOT
// part of the change signal — instead a heartbeat forces a render at least
// every EINK_RENDER_MAX_IDLE_SEC so the clock stays reasonably fresh and the
// panel gets a periodic refresh (ghosting guard).
// Set ENABLE_EINK_RENDER_ON_CHANGE false to restore "render every cycle".
#define ENABLE_EINK_RENDER_ON_CHANGE  true
#define EINK_RENDER_MAX_IDLE_SEC      1800    // 30 min heartbeat

// SPI pins
#if defined(ARDUINO_XIAO_ESP32S3)
  // reTerminal E1001 (XIAO ESP32-S3 onboard SPI)
  #define PIN_EPD_CLK               7
  #define PIN_EPD_DIN               9
  #define PIN_EPD_CS                10
  #define PIN_EPD_DC                11
  #define PIN_EPD_RST               12
  #define PIN_EPD_BUSY              13
#else
  // ESP32 DevKit V1 + Waveshare 7.5" V2 (VSPI)
  #define PIN_EPD_BUSY              4
  #define PIN_EPD_RST               16
  #define PIN_EPD_DC                17
  #define PIN_EPD_CS                5
  #define PIN_EPD_CLK               18
  #define PIN_EPD_DIN               23
#endif

// ═══════════════════════════════════════════════════════════════════
// SD-LOGGER (M2 — historia ilman tietokonetta, docs/kehitys/Fable_kehityspolku.md)
//
// Oletus false: nykyinen tuotantobuild ei muutu ennen kuin rauta-smoke
// (kortti + flash) on ajettu. Pinnit reTerminal E1001:lle (Seeed Wiki,
// vahvistettu smoke/sd_test.cpp -rautakoeella, PR #249, 2.8.2026).
// SCK/MOSI ovat JAETUT e-paperin kanssa (PIN_EPD_CLK/PIN_EPD_DIN ylla) —
// sd_logger.h nostaa EPD:n CS:n HIGH:iin ennen SD-liikennetta, ks. sen
// deselectEpd()-vastine. EN-pinnin polariteetti oli dokumentoimaton
// 2.8.2026 — sd_logger.h koettelee molemmat tasot samaan tapaan kuin
// smoke/sd_test.cpp.
// ═══════════════════════════════════════════════════════════════════
#define ENABLE_SD_LOGGING         false

#define SD_CS_PIN                 14
#define SD_EN_PIN                 16   // korttipaikan virransyoton enable - ilman tata kortti ei vastaa
#define SD_DET_PIN                15   // kortin fyysinen lasnaolo (luetaan ENNEN mounttiyritysta)
#define SD_SCK_PIN                7    // jaettu PIN_EPD_CLK:n kanssa
#define SD_MOSI_PIN               9    // jaettu PIN_EPD_DIN:n kanssa
#define SD_MISO_PIN               8
#define SD_LOG_SPI_HZ             4000000UL   // konservatiivinen — nostettavissa kun rauta on todennettu

// CSV-skeeman versio kirjoitetaan jokaisen uuden tiedoston ensimmaiselle
// riville. Kasvata kun sarakkeisto muuttuu, jotta replay (M3) osaa erottaa
// vanhat ja uudet tiedostot toisistaan.
#define SD_LOG_SCHEMA_VERSION     1

// Montako perakkaista kirjoitusvirhetta siedetaan ennen kuin g_sdReady
// pudotetaan (advisory) — yksi glitch ei saa katkaista loggausta samalla
// tavalla kuin sensoreiden sticky-validiteetti (data_fetch_parse.h).
#define SD_LOG_MAX_CONSECUTIVE_FAILS  3

// Montako appendRow()-kutsua odotetaan ennen kuin puuttuva/pudonnut kortti
// yritetaan mountata uudelleen (architecture.md § 8 runtime-degradation).
// Karkea laskuri riittaa — uudelleenyritys ei ole aikakriittinen.
#define SD_LOG_RETRY_EVERY_N_CALLS    20

// ═══════════════════════════════════════════════════════════════════
// TIMING
// ═══════════════════════════════════════════════════════════════════

// Päivitysväli — dev iteroi nopeasti, tuotanto säästää e-ink-paneelin
// full-refresh-budjettia (rajallinen elinikä) ja akkua. Aktiivinen
// UPDATE_INTERVAL_SEC johdetaan ENABLE_DEEP_SLEEP:stä DEEP SLEEP -osiossa.
#define UPDATE_INTERVAL_DEV_SEC   30      // dev (USB, ei deep sleep): nopea iterointi
#define UPDATE_INTERVAL_PROD_SEC  600     // tuotanto (deep sleep): 10 min
#define NTP_SERVER                "pool.ntp.org"
#define NTP_GMT_OFFSET_SEC        7200  // UTC+2 (Finland winter)
#define NTP_DST_OFFSET_SEC        3600  // +1h daylight saving

// ═══════════════════════════════════════════════════════════════════
// DEEP SLEEP
// ═══════════════════════════════════════════════════════════════════

#define ENABLE_DEEP_SLEEP         false   // dev: pidetään XIAO/USB-CDC aktiivisena

// Johda aktiivinen päivitysväli ajomallista (määriteltävä ENABLE_DEEP_SLEEP:n
// jälkeen): live-loop = nopea dev-väli, deep sleep = harva tuotantoväli.
#if ENABLE_DEEP_SLEEP
  #define UPDATE_INTERVAL_SEC     UPDATE_INTERVAL_PROD_SEC
#else
  #define UPDATE_INTERVAL_SEC     UPDATE_INTERVAL_DEV_SEC
#endif
#define SLEEP_DURATION_US         (UPDATE_INTERVAL_SEC * 1000000ULL)

// ═══════════════════════════════════════════════════════════════════
// DEBUG
// ═══════════════════════════════════════════════════════════════════

#define DEBUG_SERIAL              Serial
#define DEBUG_BAUD                115200
#define ENABLE_DEBUG              true

#if ENABLE_DEBUG
  #define DEBUG_PRINT(msg) { DEBUG_SERIAL.print(F("[INFO]  ")); DEBUG_SERIAL.println(msg); }
  #define DEBUG_PRINTF(fmt, ...) { DEBUG_SERIAL.printf(fmt, ##__VA_ARGS__); }
#else
  #define DEBUG_PRINT(msg)
  #define DEBUG_PRINTF(fmt, ...)
#endif

// ═══════════════════════════════════════════════════════════════════
// VERSIO-IDENTITEETTI
// ═══════════════════════════════════════════════════════════════════
// scripts/pio_git_version.py (jaettu plantmeisterin kanssa) injektoi nama
// build-flageina; fallbackit kattavat natiivitestit. Tulostetaan boot-lokiin,
// jotta "onko e-ink vanhalla firmwarella" selviaa capture_serial-lokista
// arvailematta (16.7.2026: PR #209 puuttui flashista, tunti hukkaan).
#ifndef FW_GIT_SHA
  #define FW_GIT_SHA              "unknown"
#endif
#ifndef FW_PIO_ENV
  #define FW_PIO_ENV              "unknown"
#endif

#endif // CONFIG_H
