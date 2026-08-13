/*=====================================================================
  dev_view.h - Developer diagnostics dashboard for the e-ink wall display

  Activated by ENABLE_EINK_DEV_VIEW in config.h. Renders a dense,
  monospace-style diagnostics layout instead of the user-facing UI.
  Designed for hardware bring-up and remote field debugging — fields are
  laid out so a developer can read the device state from across the room.

  Layout (800x480, monochrome):
    ┌─────────────────────────────────────────────────────────────────┐
    │ DEV VIEW                              <plant_id>  boot #N       │
    ├─────────────────────────────────────────────────────────────────┤
    │ STATE  PHASE  FAULTS  GROW   LIGHTS  PUMP        (big row)      │
    │  3      VEG    0x00   ACTV   ON      OK                         │
    ├──────────────────────────────────┬──────────────────────────────┤
    │ Sensors                          │ System                       │
    │  Air     22.4 C  54%             │  Uptime    13h 04m           │
    │  Water   19.1 C                  │  Heap      214 kB            │
    │  TDS     820 ppm                 │  WiFi      -64 dBm           │
    │  Height  142 mm                  │  LoRa RSSI -91 dBm           │
    │  Motor    20 mm                  │  Sched     1234 ticks        │
    │  Battery  87%  4.05 V            │  Last upd  2 min ago         │
    ├──────────────────────────────────┴──────────────────────────────┤
    │ Last intent: START_GROWING  src=BUTTON                          │
    │ Fault bits:  ...                                                │
    └─────────────────────────────────────────────────────────────────┘

  Implementation note: GxEPD2 + AdafruitGFX has no true monospace bundled,
  so we use FreeSans (9/12pt) and align values with fixed pixel columns.
=====================================================================*/

#ifndef DEV_VIEW_H
#define DEV_VIEW_H

#include "config.h"
#include "data_fetch.h"

#include <GxEPD2_BW.h>
#include "fonts/FreeSans9pt8b.h"
#include "fonts/FreeSansBold9pt8b.h"
#include "fonts/FreeSansBold12pt8b.h"
#include "fonts/FreeSansBold18pt8b.h"

// The `display` global is declared in display_layout.h.
#if defined(ARDUINO_XIAO_ESP32S3)
extern GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT> display;
#else
extern GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display;
#endif

// ─── Helpers ──────────────────────────────────────────────────────────

// Map numeric DeviceState (from gateway) to short label. Mirrors
// firmware/plantmeister/device_state.h enum order (DEVICE_BOOTING=0..).
static const char* dev_view_stateLabel(int s) {
  switch (s) {
    case 0:  return "BOOT";
    case 1:  return "SELF";    // SELF_TEST
    case 2:  return "IDLE";
    case 3:  return "WAIT";    // AWAITING_USER
    case 4:  return "GROW";    // GROWING
    case 5:  return "PAUS";    // PAUSED
    case 6:  return "FAULT";
    case 7:  return "SHDN";    // SHUTDOWN
    case 8:  return "TEST";    // HARDWARE_TEST
    default: return "?";
  }
}

// Format uptime seconds as "13h 04m" or "12m 03s" if under one hour.
static void dev_view_formatUptime(uint32_t s, char* out, size_t outLen) {
  if (s >= 3600) {
    uint32_t h = s / 3600;
    uint32_t m = (s % 3600) / 60;
    snprintf(out, outLen, "%uh %02um", (unsigned)h, (unsigned)m);
  } else {
    uint32_t m = s / 60;
    uint32_t sec = s % 60;
    snprintf(out, outLen, "%um %02us", (unsigned)m, (unsigned)sec);
  }
}

// Print a "label  value" pair at (x, y). Right-edge of `value` column
// fixed at `valueRightX` for column alignment. Returns next-row Y.
static int dev_view_kv(int x, int y, int valueRightX,
                       const char* label, const char* value) {
  display.setFont(&FreeSans9pt8b);
  display.setCursor(x, y);
  display.print(label);

  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(value, 0, 0, &bx, &by, &bw, &bh);
  display.setFont(&FreeSansBold9pt8b);
  display.setCursor(valueRightX - bw, y);
  display.print(value);
  return y + 22;
}

// ─── Main entry point ─────────────────────────────────────────────────

static void dev_view_render(const DisplayData* d) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    // ── Top header ─────────────────────────────────────────────────
    display.setFont(&FreeSansBold18pt8b);
    display.setCursor(20, 32);
    display.print("DEV VIEW");

    display.setFont(&FreeSans9pt8b);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%s   boot #%d",
             d->plantId[0] ? d->plantId : "(no id)", g_bootCount);
    display.setCursor(450, 32);
    display.print(hdr);

    display.drawLine(20, 42, 780, 42, GxEPD_BLACK);

    // ── Big state row ──────────────────────────────────────────────
    char st[8];
    snprintf(st, sizeof(st), "%d/%s",
             d->devDeviceState,
             dev_view_stateLabel(d->devDeviceState));
    display.setFont(&FreeSansBold12pt8b);
    int y = 75;
    display.setCursor(20,  y); display.print("STATE");
    display.setCursor(180, y); display.print("FAULTS");
    display.setCursor(340, y); display.print("GROW");
    display.setCursor(480, y); display.print("LIGHT");
    display.setCursor(620, y); display.print("WATER");

    display.setFont(&FreeSansBold18pt8b);
    y = 110;
    display.setCursor(20,  y); display.print(st);
    char faults[12];
    snprintf(faults, sizeof(faults), "0x%02X", d->devFaultBits);
    display.setCursor(180, y); display.print(faults);
    display.setCursor(340, y); display.print(d->growActive ? "ACTV" : "off");
    display.setCursor(480, y); display.print(d->lightsOn   ? "ON"   : "off");
    display.setCursor(620, y); display.print(d->waterLevelOk ? "OK" : "LOW");

    display.drawLine(20, 130, 780, 130, GxEPD_BLACK);

    // ── Two-column body ────────────────────────────────────────────
    // Left column: sensor data.
    int leftX = 30;
    int leftVal = 360;
    y = 165;

    display.setFont(&FreeSansBold12pt8b);
    display.setCursor(leftX, y);
    display.print("Sensors");
    y += 28;

    char buf[40];
    snprintf(buf, sizeof(buf), "%.1f C  %.0f%%", d->airTemp, d->airHumidity);
    y = dev_view_kv(leftX, y, leftVal, "Air",     buf);

    snprintf(buf, sizeof(buf), "%.1f C", d->waterTemp);
    y = dev_view_kv(leftX, y, leftVal, "Water",   buf);

    snprintf(buf, sizeof(buf), "%d ppm", d->tdsPpm);
    y = dev_view_kv(leftX, y, leftVal, "TDS",     buf);

    snprintf(buf, sizeof(buf), "%d mm", d->plantHeightMm);
    y = dev_view_kv(leftX, y, leftVal, "Height",  buf);

    snprintf(buf, sizeof(buf), "%d mm", d->motorHeightMm);
    y = dev_view_kv(leftX, y, leftVal, "Motor",   buf);

    snprintf(buf, sizeof(buf), "%d%% (%.2f V)",
             d->batteryPercent, d->batteryVoltage);
    y = dev_view_kv(leftX, y, leftVal, "Battery", buf);

    // Right column: system / link diagnostics.
    int rightX = 400;
    int rightVal = 770;
    int yr = 165;

    display.setFont(&FreeSansBold12pt8b);
    display.setCursor(rightX, yr);
    display.print("System");
    yr += 28;

    char upt[24];
    dev_view_formatUptime(d->devUptimeS, upt, sizeof(upt));
    yr = dev_view_kv(rightX, yr, rightVal, "Uptime",   upt);

    snprintf(buf, sizeof(buf), "%lu kB", (unsigned long)(d->devFreeHeap / 1024));
    yr = dev_view_kv(rightX, yr, rightVal, "Heap",     buf);

    snprintf(buf, sizeof(buf), "%d dBm", d->devWifiRssi);
    yr = dev_view_kv(rightX, yr, rightVal, "WiFi",     buf);

    snprintf(buf, sizeof(buf), "%d dBm", d->loraRssi);
    yr = dev_view_kv(rightX, yr, rightVal, "LoRa",     buf);

    snprintf(buf, sizeof(buf), "%d", d->devSchedulerTicks);
    yr = dev_view_kv(rightX, yr, rightVal, "Sched",    buf);

    int mins = data_minutesSinceUpdate();
    if (mins < 0) snprintf(buf, sizeof(buf), "n/a");
    else          snprintf(buf, sizeof(buf), "%d min", mins);
    yr = dev_view_kv(rightX, yr, rightVal, "Last upd", buf);

    // ── Bottom strip: last intent + footer ─────────────────────────
    display.drawLine(20, 380, 780, 380, GxEPD_BLACK);

    display.setFont(&FreeSans9pt8b);
    char intent[48];
    if (d->devLastIntent[0]) {
      snprintf(intent, sizeof(intent), "Last intent: %s  src=%s",
               d->devLastIntent,
               d->devLastIntentSrc[0] ? d->devLastIntentSrc : "?");
    } else {
      snprintf(intent, sizeof(intent), "Last intent: (none reported)");
    }
    display.setCursor(20, 405);
    display.print(intent);

    if (!d->devFieldsPresent) {
      display.setCursor(20, 430);
      display.print("Gateway is not reporting dev fields - upgrade /api/current.");
    } else if (d->fetchFailCount > 0) {
      char fc[40];
      snprintf(fc, sizeof(fc), "Recent fetch failures: %d", d->fetchFailCount);
      display.setCursor(20, 430);
      display.print(fc);
    }

    display.setCursor(20, 465);
    display.print("ENABLE_EINK_DEV_VIEW=true  (recompile to disable)");
  } while (display.nextPage());
}

#endif // DEV_VIEW_H
