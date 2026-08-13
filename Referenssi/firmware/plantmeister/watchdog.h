/*=====================================================================
  watchdog.h - ESP32 hardware watchdog wrapper

  Inline header-only wrapper to allow unit-testing (UNIT_TEST define)
  and to support both ESP-IDF v4 and v5 esp_task_wdt APIs.

  See docs/arkisto/tehtavat/REL-3-watchdog-module.md for spec.
=====================================================================*/

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include "config.h"

#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
  #include <esp_task_wdt.h>
  #include <esp_system.h>
  // ROM-tason reset-syyn lukuun: kertoo USB-CDC chip-resetin (rst:0x15),
  // power-onin (rst:0x1) yms. erikseen, vaikka esp_reset_reason() palaisi
  // "Unknown". Tarvitaan T2/T3-erottelussa XIAO ESP32S3:lla.
  #if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_USB_MODE)
    #include <esp32s3/rom/rtc.h>
  #endif

  #ifndef ESP_IDF_VERSION_VAL
    #define ESP_IDF_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | patch)
  #endif
  #ifndef ESP_IDF_VERSION
    #define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(4, 4, 0)
  #endif
#endif

// ── Crash-reboot accounting (safe-mode input) ───────────────────────
// Stored in RTC noinit RAM: survives a warm reboot but not a power cycle.
// A magic word guards against garbage after power-on. Only crash resets
// (PANIC, WDT, brownout, CPU lockup) increment the count; clean reboots
// (power-on, RST button, SW restart, OTA reboot) reset it.
#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
  #define WATCHDOG_CRASH_MAGIC 0xC0DEFA11u
  RTC_NOINIT_ATTR static uint32_t s_crashMagic;
  RTC_NOINIT_ATTR static uint32_t s_crashCount;
  static const char* s_resetReasonShort = "n/a";

  // Compile-time guard: watchdog_isCrashReason() encodes these enum values as
  // raw ints (so it stays unit-testable without the esp headers). If the
  // toolchain's esp_reset_reason_t ever drifts, fail the build here.
  static_assert(ESP_RST_PANIC == 4 && ESP_RST_INT_WDT == 5 &&
                ESP_RST_TASK_WDT == 6 && ESP_RST_WDT == 7 &&
                ESP_RST_BROWNOUT == 9,
                "esp_reset_reason_t drifted — update watchdog_isCrashReason()");
#endif

// true if this reset reason indicates a crash (counts toward safe mode).
// Values mirror esp_reset_reason_t: 4=PANIC, 5=INT_WDT, 6=TASK_WDT, 7=WDT,
// 9=BROWNOUT, 15=CPU_LOCKUP. Takes a raw int so the classifier stays
// unit-testable on the native host without pulling in esp headers.
inline bool watchdog_isCrashReason(int reason) {
  switch (reason) {
    case 4:   // ESP_RST_PANIC
    case 5:   // ESP_RST_INT_WDT
    case 6:   // ESP_RST_TASK_WDT
    case 7:   // ESP_RST_WDT
    case 9:   // ESP_RST_BROWNOUT
    case 15:  // ESP_RST_CPU_LOCKUP
      return true;
    default:
      return false;
  }
}

#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
// Short name for an esp_reset_reason_t value. Shared by watchdog_logResetReason()
// and the /api/status boot_cause field so the mapping isn't duplicated.
inline const char* watchdog_resetReasonShortFor(int reason) {
  switch (reason) {
    case ESP_RST_POWERON:    return "Power-on";
    case ESP_RST_EXT:        return "External (RST)";
    case ESP_RST_SW:         return "SW restart";
    case ESP_RST_PANIC:      return "PANIC";
    case ESP_RST_INT_WDT:    return "INT_WDT";
    case ESP_RST_TASK_WDT:   return "TASK_WDT";
    case ESP_RST_WDT:        return "WDT";
    case ESP_RST_BROWNOUT:   return "Brownout";
    case ESP_RST_DEEPSLEEP:  return "Deep-sleep";
#ifdef ESP_RST_USB
    case ESP_RST_USB:        return "USB reset";
#endif
#ifdef ESP_RST_JTAG
    case ESP_RST_JTAG:       return "JTAG reset";
#endif
#ifdef ESP_RST_PWR_GLITCH
    case ESP_RST_PWR_GLITCH: return "Power glitch";
#endif
#ifdef ESP_RST_CPU_LOCKUP
    case ESP_RST_CPU_LOCKUP: return "CPU lockup";
#endif
    default:                 return "Unknown";
  }
}
#endif

inline void watchdog_init() {
#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
  #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t cfg = {
      .timeout_ms = (uint32_t)(WATCHDOG_TIMEOUT_S * 1000),
      .idle_core_mask = 0,
      .trigger_panic = true
    };
    esp_task_wdt_init(&cfg);
  #else
    esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);
  #endif
  esp_task_wdt_add(NULL);
  DEBUG_PRINTF("[INFO]  Watchdog: armed (%d s)\n", WATCHDOG_TIMEOUT_S);
#endif
}

inline void watchdog_feed() {
#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
  esp_task_wdt_reset();
#endif
}

// ROM-tason reset-syyn selkokielinen kuvaus. ROM-loader printtaa bootlogiin
// "rst:0x.. (NAME)" mutta ESP-IDF:n esp_reset_reason() summaa ne karkeampiin
// luokkiin (USB-CDC chip-reset -> "Unknown" XIAO ESP32S3:lla). Boot-cause-
// tunniste auttaa T2 (RST-nappi) vs T3 (power cycle) -erottelussa.
inline const char* watchdog_romResetReasonName(int raw) {
  switch (raw) {
    case 0:  return "NO_MEAN";
    case 1:  return "POWERON_RESET";
    case 3:  return "RTC_SW_SYS_RESET";
    case 5:  return "DEEPSLEEP_RESET";
    case 7:  return "TG0WDT_SYS_RESET";
    case 8:  return "TG1WDT_SYS_RESET";
    case 9:  return "RTCWDT_SYS_RESET";
    case 10: return "INTRUSION_RESET";
    case 11: return "TG0WDT_CPU_RESET";
    case 12: return "RTC_SW_CPU_RESET";
    case 13: return "RTCWDT_CPU_RESET";
    case 15: return "RTCWDT_BROWN_OUT_RESET";
    case 16: return "RTCWDT_RTC_RESET";
    case 17: return "TG1WDT_CPU_RESET";
    case 18: return "SUPER_WDT_RESET";
    case 19: return "GLITCH_RTC_RESET";
    case 20: return "EFUSE_RESET";
    case 21: return "USB_UART_CHIP_RESET";
    case 22: return "USB_JTAG_CHIP_RESET";
    case 23: return "POWER_GLITCH_RESET";
    default: return "UNKNOWN_RAW";
  }
}

inline void watchdog_logResetReason() {
#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
  esp_reset_reason_t reason = esp_reset_reason();
  const char* name = watchdog_resetReasonShortFor((int)reason);
  // Warn on crashes; ESP_RST_UNKNOWN also warns (can mask an unclean reset).
  bool warn = watchdog_isCrashReason((int)reason) || reason == ESP_RST_UNKNOWN;
  if (warn) {
    DEBUG_PRINTF("[WARN]  Reset reason: %s (%d)\n", name, (int)reason);
  } else {
    DEBUG_PRINTF("[INFO]  Reset reason: %s (%d)\n", name, (int)reason);
  }

  // ROM-tason syy lisätietona — erottaa USB-CDC chip-resetin (0x15) muista
  // resetin "Unknown" -tapauksista.
  #if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_USB_MODE)
  int romRaw = (int)rtc_get_reset_reason(0);
  DEBUG_PRINTF("[INFO]  Boot-cause: rst:0x%02x (%s)\n",
               romRaw, watchdog_romResetReasonName(romRaw));
  #endif
#endif
}

// ── Crash-reboot accounting API ─────────────────────────────────────
// Call once in setup() right after watchdog_init(): classifies the reset,
// updates the RTC crash count, and caches the reset-reason short name.
inline void watchdog_bootAccounting() {
#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
  esp_reset_reason_t reason = esp_reset_reason();
  s_resetReasonShort = watchdog_resetReasonShortFor((int)reason);
  if (s_crashMagic != WATCHDOG_CRASH_MAGIC) {
    // First boot after power-on (RTC RAM is garbage) — start a fresh chain.
    s_crashMagic = WATCHDOG_CRASH_MAGIC;
    s_crashCount = 0;
  } else if (watchdog_isCrashReason((int)reason)) {
    s_crashCount++;
  } else {
    s_crashCount = 0;
  }
  if (s_crashCount > 0) {
    DEBUG_PRINTF("[WARN]  Crash-reboot count: %u (last reset: %s)\n",
                 (unsigned)s_crashCount, s_resetReasonShort);
  }
#endif
}

inline uint32_t watchdog_crashCount() {
#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
  return s_crashCount;
#else
  return 0;
#endif
}

// Clear the crash chain. Called once the boot proves healthy (BOOT_HEALTHY_
// RUNTIME_MS of loop uptime) so a one-off crash does not accumulate toward
// safe mode across unrelated reboots.
inline void watchdog_clearCrashCount() {
#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
  s_crashCount = 0;
  s_crashMagic = WATCHDOG_CRASH_MAGIC;
#endif
}

inline const char* watchdog_resetReasonShort() {
#if ENABLE_WATCHDOG && !defined(UNIT_TEST)
  return s_resetReasonShort;
#else
  return "n/a";
#endif
}

#endif // WATCHDOG_H
