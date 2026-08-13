/*=====================================================================
  E-ink Seinataulu — KasviAsema Wall Display

  ESP32-S3 (reTerminal E1001) + GDEY075T7 e-ink

  Kaksi ajomallia (config.h ENABLE_DEEP_SLEEP):
    DEEP SLEEP (tuotanto):  setup() yhdistaa, hakee, renderoi, nukkuu;
                            herays ajaa setup():n uudelleen.
    LIVE (dev / USB):       setup() yhdistaa kerran + tekee ensimmaisen
                            syklin; loop() hakee + renderoi
                            UPDATE_INTERVAL_SEC valein WiFi PAALLA — ei
                            ESP.restart():ia, ei disconnectia joka
                            syklissa. Yhteys pysyy vakaana ja viimeisin
                            hyva data sailyy lyhyen katkon yli.

  Libraries required:
    - GxEPD2
    - ArduinoJson v6
    - Adafruit GFX (+ Adafruit BusIO)
=====================================================================*/

#include "config.h"
#include "data_fetch.h"
// view_profiles.h transitively includes display_layout.h, dev_view.h,
// and dev_table_view.h — all renderers stay available for the dispatcher.
#include "view_profiles.h"

// Renderoi nykyinen g_displayData oikealla profiililla. Logaa onko data
// tuoretta, vanhaa (fetch failasi mutta viimeisin hyva on muistissa) vai
// kokonaan puuttuvaa (renderer fallbackaa showroom-naytolle).
static void renderCurrent(bool fetched) {
  DEBUG_PRINTF("[INFO]  View profile: %s\n", view_profile_name());
  if (!fetched && !g_displayData.dataValid) {
    DEBUG_PRINT("Ei dataa - renderer paattaa fallbackista");
  } else if (!fetched && g_displayData.dataValid) {
    DEBUG_PRINT("Naytetaan viimeisin hyva data (fetch failasi)");
  }
  view_profile_render(&g_displayData);
}

#if ENABLE_EINK_RENDER_ON_CHANGE
// Render-on-change state — RTC_DATA_ATTR survives deep sleep, and (since the
// live loop no longer calls ESP.restart) also persists across the live loop.
RTC_DATA_ATTR uint32_t g_lastRenderedHash = 0;
RTC_DATA_ATTR bool     g_hasRenderedOnce  = false;
RTC_DATA_ATTR uint16_t g_renderSkipCount  = 0;

// Heartbeat: force a render at least every N cycles even if content is
// unchanged (keeps the clock fresh + gives the panel a periodic refresh).
static const uint16_t RENDER_FORCE_AFTER =
  (EINK_RENDER_MAX_IDLE_SEC / UPDATE_INTERVAL_SEC) > 0
    ? (uint16_t)(EINK_RENDER_MAX_IDLE_SEC / UPDATE_INTERVAL_SEC) : 1;
#endif

// Render only when the visible content changed, or when the heartbeat forces
// it. Falls back to "always render" when the optimization is disabled.
static void renderIfChanged(bool fetched) {
#if ENABLE_EINK_RENDER_ON_CHANGE
  uint32_t h = view_profile_contentHash(&g_displayData);
  bool changed = (h != g_lastRenderedHash);
  bool force   = !g_hasRenderedOnce || (g_renderSkipCount >= RENDER_FORCE_AFTER);
  if (changed || force) {
    renderCurrent(fetched);
    g_lastRenderedHash = h;
    g_hasRenderedOnce  = true;
    g_renderSkipCount  = 0;
  } else {
    g_renderSkipCount++;
    DEBUG_PRINTF("[INFO]  Sisalto ennallaan — render ohitettu (%u/%u)\n",
                 (unsigned)g_renderSkipCount, (unsigned)RENDER_FORCE_AFTER);
  }
#else
  renderCurrent(fetched);
#endif
}

#if !ENABLE_DEEP_SLEEP
// Yksi live-sykli: (re)connect tarvittaessa, hae data, renderoi. WiFi
// jatetaan paalle syklien valiin — vain oikea pudotus laukaisee
// uudelleenyhdistyksen, ei joka sykli.
static void updateCycleLive() {
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINT("WiFi pudonnut — yhdistetaan uudelleen...");
    if (!wifi_connect()) {
      DEBUG_PRINT("WiFi reconnect epaonnistui — yritetaan seuraavalla syklilla");
      renderIfChanged(false);
      return;
    }
    WiFi.setSleep(false);
    time_sync();
  }
  bool fetched = data_fetch();
  renderIfChanged(fetched);
}
#endif

void setup() {
  DEBUG_SERIAL.begin(DEBUG_BAUD);
  delay(200);

  g_bootCount++;
  DEBUG_PRINTF("\n[INFO]  E-ink seinataulu — boot #%d\n", g_bootCount);
  // Versio-identiteetti heti bootissa: kertoo suoraan mika commit ja env
  // paneelissa ajaa (vrt. paayksikon /api/status fw_git/fw_env).
  DEBUG_PRINTF("[INFO]  FW git: %s (env %s)\n", FW_GIT_SHA, FW_PIO_ENV);

  // ── Initialize display ──
  display_init();

  // M2 SD-historia: no-op kun ENABLE_SD_LOGGING on false (oletus, config.h).
  // Kortin puute/vika on advisory - ei estä bootia eikä piirtoa.
  if (sdlog_init()) {
    DEBUG_PRINT("SD-loggeri: kortti mountattu, kirjaa /api/state-hakusykliin");
  }

#if ENABLE_DEEP_SLEEP
  // ── Tuotanto: yksi sykli, sitten deep sleep ──
  if (!wifi_connect()) {
    DEBUG_PRINT("WiFi epaonnistui");
    display_renderError("WiFi: ei yhteytta");
    wifi_disconnect();
    enter_sleep();
    return;
  }
  time_sync();
  bool fetched = data_fetch();
  wifi_disconnect();          // saasta virtaa ennen renderointia + unta
  renderIfChanged(fetched);
  enter_sleep();              // herays ajaa setup():n uudelleen
#else
  // ── Dev / live: yhdista kerran + ensimmainen sykli; loop() jatkaa ──
  bool connected = wifi_connect();
  if (connected) {
    WiFi.setSleep(false);     // pida STA responsiivisena jatkuvassa yhteydessa
    time_sync();
  } else {
    DEBUG_PRINT("WiFi epaonnistui — loop yrittaa reconnectia");
  }
  bool fetched = connected ? data_fetch() : false;
  renderIfChanged(fetched);   // fallback nakyy jos ei yhteytta/dataa
#endif
}

#if ENABLE_DEEP_SLEEP
void enter_sleep() {
  DEBUG_PRINTF("[INFO]  Deep sleep: %d sekuntia\n", UPDATE_INTERVAL_SEC);
  DEBUG_SERIAL.flush();

  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
  esp_deep_sleep_start();
}
#endif

void loop() {
#if ENABLE_DEEP_SLEEP
  // Never reached — deep sleep restarts from setup().
#else
  // Live-tila: hae + renderoi UPDATE_INTERVAL_SEC valein, WiFi paalla.
  // Ei ESP.restart():ia — yhteys ja viimeisin hyva data sailyvat.
  static unsigned long lastUpdate = millis();
  if (millis() - lastUpdate >= (unsigned long)UPDATE_INTERVAL_SEC * 1000UL) {
    lastUpdate = millis();
    updateCycleLive();
  }
  delay(100);
#endif
}
