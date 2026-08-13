/*=====================================================================
  ota_manager.h - HTTP OTA firmware update via ElegantOTA

  Shares the wifi_portal AsyncWebServer instance — no separate port,
  no separate auth stack. /update endpoint is added to the portal.

  Build flag (set in platformio.ini, applies to all envs):
    -DELEGANTOTA_USE_ASYNC_WEBSERVER=1

  Feature flags (config.h / secrets.h):
    ENABLE_OTA          on/off (default true; set in config.h)
    OTA_USERNAME        HTTP Digest auth user (set in secrets.h)
    OTA_PASSWORD        empty string disables auth (dev mode)
    FIRMWARE_VERSION    shown in OTA UI

  Note: ElegantOTA v3 uses HTTP Digest authentication (not Basic).
  Clients must use --digest with curl, or HTTPDigestAuth with requests.

  Lifecycle:
    setup()  → portal_init() → ota_begin() (called from wifi_portal)
    loop()   → portal_update() → ota_loop()
             → ota_confirmTick()        // cancel rollback after healthy runtime
=====================================================================*/

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "config.h"

#if ENABLE_OTA && ENABLE_WIFI_PORTAL

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <esp_ota_ops.h>
#include "reboot_request.h"

static bool g_otaInProgress = false;

// CRITICAL for rollback: Arduino-ESP32's core auto-confirms a freshly-OTA'd app
// in initArduino() (cores/esp32/esp32-hal-misc.c) BEFORE setup() runs — it calls
// esp_ota_mark_app_valid_cancel_rollback() if the running image is PENDING_VERIFY.
// That defeats deferred rollback entirely: by the time our code runs the image is
// already ESP_OTA_IMG_VALID, so ota_confirmBoot() is a no-op and a bad update that
// reboots early is NOT rolled back. Overriding this weak hook to return true tells
// the core to leave verification to us, so the new image stays PENDING_VERIFY until
// ota_confirmTick() confirms it after BOOT_HEALTHY_RUNTIME_MS (or the bootloader
// rolls it back if it reboots first). Verified on hardware 2026-06-11: without this
// override, rollback never engages despite CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE.
extern "C" bool verifyRollbackLater() { return true; }

// Forward declaration: the onStart lambda inside ota_begin() calls
// ota_confirmBoot(), whose body is defined further down.
inline void ota_confirmBoot();

inline void ota_begin(AsyncWebServer* server) {
  if (!server) return;

  // Log the running image's OTA state once per boot. PENDING_VERIFY(1) means the
  // verifyRollbackLater override engaged and ota_confirmTick() must confirm within
  // BOOT_HEALTHY_RUNTIME_MS or the next reboot rolls back; VALID(2) = confirmed.
  {
    esp_ota_img_states_t st;
    const esp_partition_t* run = esp_ota_get_running_partition();
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK) {
      DEBUG_PRINTF("[INFO]  OTA: boot image state=%d (1=PENDING_VERIFY 2=VALID)\n", (int)st);
    }
  }

  ElegantOTA.begin(server);

  // We own the post-update reboot. ElegantOTA's built-in auto-reboot calls
  // ESP.restart() directly from ElegantOTA.loop() WITHOUT a clean WiFi/AsyncTCP
  // shutdown — on XIAO that leaves the radio in a zombie state where STA never
  // reconnects (the exact failure reboot_request.h was created to fix, PR
  // #102/#108). Disable it and route through reboot_request_tick() instead, the
  // same deferred clean-shutdown path used by the remote REBOOT command and the
  // physical button. onEnd (postUpdateCallback) fires unconditionally at upload
  // completion, so scheduling here works regardless of auto-reboot.
  ElegantOTA.setAutoReboot(false);

  if (strlen(OTA_PASSWORD) > 0) {
    ElegantOTA.setAuth(OTA_USERNAME, OTA_PASSWORD);
    DEBUG_PRINTF("[INFO]  OTA: enabled at /update (Digest auth, user: %s)\n", OTA_USERNAME);
  } else {
    DEBUG_INFO(F("[WARN]  OTA: enabled at /update (NO AUTH — dev mode)"));
  }

  ElegantOTA.onStart([]() {
    g_otaInProgress = true;
    // D2: the user is replacing the running image. Confirm the current image
    // first — otherwise a still-PENDING_VERIFY running image plus a freshly
    // flashed image leaves the bootloader in an ambiguous rollback state.
    ota_confirmBoot();
    DEBUG_INFO(F("[INFO]  OTA: update started"));
  });

  ElegantOTA.onEnd([](bool success) {
    g_otaInProgress = false;
    if (success) {
      // Defer to loop()'s reboot_request_tick() for a clean WiFi shutdown.
      // 2000 ms matches ElegantOTA's old internal delay so the HTTP 200 "OK"
      // flushes to ota_upload.py before the radio goes down.
      DEBUG_INFO(F("[INFO]  OTA: update OK — scheduling clean reboot (2s)"));
      reboot_request_schedule(2000);
    } else {
      DEBUG_ERROR(F("OTA: update FAILED"));
    }
  });
}

inline void ota_loop() {
  ElegantOTA.loop();
}

inline bool ota_isActive() {
  return g_otaInProgress;
}

// Onko ajossa oleva image viela vahvistamatta (= seuraava reboot rollbackaa)?
//
// Rollback on tarkoituksellinen turvaverkko: jos uusi firmware ei kaynnisty
// terveena, reboot palauttaa vanhan. Mutta se tekee jokaisesta ennen
// BOOT_HEALTHY_RUNTIME_MS:aa tapahtuvasta rebootista hiljaisen downgraden, ja
// se yllattaa kutsujan joka rebootaa jostain AIVAN muusta syysta.
//
// Todettu 15.7.2026: FACTORY_RESET rebootaa 500 ms:ssa -> ~10 s vanha OTA
// rullautui takaisin -> laite ajoi vanhaa firmwarea ja regeneroi plants.json:n
// sen VANHOISTA sisaanrakennetuista teksteista. Nayttaa silta etta reset
// "unohti" korjauksen, vaikka syy oli firmware-versio.
//
// Kutsuja voi kysya taman ja kieltaytya sen sijaan etta aiheuttaisi vahingossa
// downgraden. REBOOT itse EI kysy: siella rollback on nimenomaan haluttu.
inline bool ota_isPendingVerify() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return false;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return false;
  return state == ESP_OTA_IMG_PENDING_VERIFY;
}

// Sekunteja jaljella kunnes ota_confirmTick() vahvistaa imagen. 0 = vahvistettu
// tai vahvistus on jo myohassa. Kutsujalle joka haluaa kertoa "odota N s".
inline uint32_t ota_secondsUntilConfirm() {
  if (!ota_isPendingVerify()) return 0;
  unsigned long now = millis();
  if (now >= BOOT_HEALTHY_RUNTIME_MS) return 0;
  return (uint32_t)((BOOT_HEALTHY_RUNTIME_MS - now + 999UL) / 1000UL);
}

// Cancel pending rollback once the new firmware is confirmed bootable.
// Callers: ota_confirmTick() after healthy runtime, and onStart (D2).
// No-op unless the running partition is in PENDING_VERIFY.
inline void ota_confirmBoot() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;
  if (state != ESP_OTA_IMG_PENDING_VERIFY) return;
  // NOTE: use DEBUG_PRINTF here, not DEBUG_INFO — DEBUG_INFO is serial-only (no
  // WLOG), which made a working confirm look broken in the UDP log (2026-06-11).
  esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err == ESP_OK) {
    DEBUG_PRINTF("[INFO]  OTA: boot confirmed - rollback cancelled\n");
  } else {
    DEBUG_PRINTF("[ERROR] OTA: confirm FAILED err=%d - image still PENDING_VERIFY\n", (int)err);
  }
}

// Called every loop(). Cancels the pending rollback once the firmware has
// proven itself: BOOT_HEALTHY_RUNTIME_MS of loop uptime (= setup completed
// and at least one full watchdog window survived). Deliberately NOT gated
// on DEVICE_FAULT: an environmental blocking fault (water overflow) is a
// valid state and must not cause a surprise downgrade on the next reboot.
inline void ota_confirmTick() {
  static bool s_confirmed = false;
  if (s_confirmed) return;
  if (millis() < BOOT_HEALTHY_RUNTIME_MS) return;
  ota_confirmBoot();
  s_confirmed = true;
}

#else  // !ENABLE_OTA — no-op stubs

class AsyncWebServer;
inline void ota_begin(AsyncWebServer*) {}
inline void ota_loop() {}
inline bool ota_isActive() { return false; }
inline void ota_confirmBoot() {}
inline void ota_confirmTick() {}
inline bool ota_isPendingVerify() { return false; }
inline uint32_t ota_secondsUntilConfirm() { return 0; }

#endif // ENABLE_OTA && ENABLE_WIFI_PORTAL

#endif // OTA_MANAGER_H
