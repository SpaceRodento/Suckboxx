/*=====================================================================
  dev_table_view.h - Raw-data table profile for the e-ink seinataulu

  Activated by EINK_VIEW_PROFILE=EINK_VIEW_RAW_TABLE in config.h.
  Renders every field that data_fetch parses from PM /api/state — the
  full sensor set (incl. PE metrics VPD/CO2/PPFD/DLI/leaf), power/uptime,
  device/actuator state, and the e-ink side of the link (boot, latency).

  Missing sensors show "--" (validity is embedded in the value), so a
  partially-wired rig reads at a glance which sensors are live.

  Power note: PlantMeister /api/state exposes only battery_v / battery_pct
  (default/USB-derived), NOT true power draw (mA/W). Real consumption
  metering needs an INA219/shunt (V2). Uptime is PM device uptime.

  Layout (800x480), 3 columns of label|value pairs, tuned to FIT in 480 px:
    Header:    title + profile + boot# + last_upd
    [SENSORS]  air_t air_rh vpd | co2 ppfd dli | leaf_t leaf_dt water_t | height tds water_lvl
    [POWER]    batt_v batt_% uptime
    [DEVICE]   state prev t_in_state | faults fault_msg
    [ACT]      lights pump ebb | motor target moving
    [GROW]     active plant phase/day
    [LINK]     http dur fails | pm_ip mdns last_err

  Rendered with FreeSans9pt (label) + FreeSansBold9pt (value) for density.
=====================================================================*/

#ifndef DEV_TABLE_VIEW_H
#define DEV_TABLE_VIEW_H

#include "config.h"
#include "data_fetch.h"

#include <GxEPD2_BW.h>
#include "fonts/FreeSans9pt8b.h"
#include "fonts/FreeSansBold9pt8b.h"
#include "fonts/FreeSansBold12pt8b.h"

// The `display` global is declared in display_layout.h.
#if defined(ARDUINO_XIAO_ESP32S3)
extern GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT> display;
#else
extern GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display;
#endif

// ─── Layout constants ─────────────────────────────────────────────────
// 800 / 3 = ~266 px per column. Label takes left ~100 px, value the rest.
// Row height and section spacing tuned so all 7 sections FIT in 480 px.
#define DTV_COL_W      260
#define DTV_COL_GAP    10
#define DTV_ROW_H      16
#define DTV_SEC_GAP    2     // extra gap after a section's last row
#define DTV_LABEL_W    98    // pixels reserved for label inside a column
#define DTV_PAD_L      10
#define DTV_PAD_T      8

// ─── Helpers ──────────────────────────────────────────────────────────

// Compute X for column index 0..2.
static int dtv_colX(int col) {
  return DTV_PAD_L + col * (DTV_COL_W + DTV_COL_GAP);
}

// Draw "label value" at given column slot and row baseline Y.
static void dtv_kv(int col, int y, const char* label, const char* value) {
  int x = dtv_colX(col);
  display.setFont(&FreeSans9pt8b);
  display.setCursor(x, y);
  display.print(label);
  display.setFont(&FreeSansBold9pt8b);
  display.setCursor(x + DTV_LABEL_W, y);
  display.print(value ? value : "");
}

// Section header: horizontal line + bold label. Returns baseline for the
// caller to advance from (caller adds DTV_ROW_H before the first data row).
static int dtv_section(int y, const char* title) {
  display.drawLine(DTV_PAD_L, y, DISPLAY_WIDTH - DTV_PAD_L, y, GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt8b);
  display.setCursor(DTV_PAD_L, y + 13);
  display.print(title);
  return y + 14;
}

// ─── Main entry point ─────────────────────────────────────────────────

static void dev_table_view_render(const DisplayData* d) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    char buf[48];

    // ── Header ─────────────────────────────────────────────────────────
    display.setFont(&FreeSansBold12pt8b);
    display.setCursor(DTV_PAD_L, DTV_PAD_T + 14);
    display.print("RAW STATE  -  e-ink seinataulu");

    display.setFont(&FreeSans9pt8b);
    {
      int mins = data_minutesSinceUpdate();
      if (mins < 0) snprintf(buf, sizeof(buf), "RAW_TABLE  boot #%d  upd n/a", g_bootCount);
      else          snprintf(buf, sizeof(buf), "RAW_TABLE  boot #%d  upd %dmin", g_bootCount, mins);
    }
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
    display.setCursor(DISPLAY_WIDTH - DTV_PAD_L - bw, DTV_PAD_T + 14);
    display.print(buf);

    int y = DTV_PAD_T + 20;

    // ── SENSORS (full set, incl. PE metrics; "--" = sensor absent) ──────
    y = dtv_section(y, "SENSORS");
    y += DTV_ROW_H;

    // Row 1: air temp / RH / VPD
    if (d->envValid) snprintf(buf, sizeof(buf), "%.1f C", d->airTemp);
    else             snprintf(buf, sizeof(buf), "--");
    dtv_kv(0, y, "air_t", buf);
    if (d->envValid) snprintf(buf, sizeof(buf), "%.0f %%", d->airHumidity);
    else             snprintf(buf, sizeof(buf), "--");
    dtv_kv(1, y, "air_rh", buf);
    if (d->vpdValid) snprintf(buf, sizeof(buf), "%.2f kPa", d->vpdKpa);
    else             snprintf(buf, sizeof(buf), "--");
    dtv_kv(2, y, "vpd", buf);
    y += DTV_ROW_H;

    // Row 2: CO2 / PPFD / DLI
    if (d->co2Valid) snprintf(buf, sizeof(buf), "%d ppm", d->co2Ppm);
    else             snprintf(buf, sizeof(buf), "--");
    dtv_kv(0, y, "co2", buf);
    if (d->ppfdValid) snprintf(buf, sizeof(buf), "%.0f umol", d->ppfd);
    else              snprintf(buf, sizeof(buf), "--");
    dtv_kv(1, y, "ppfd", buf);
    if (d->ppfdValid || d->dli > 0.0f) snprintf(buf, sizeof(buf), "%.1f mol", d->dli);
    else                               snprintf(buf, sizeof(buf), "--");
    dtv_kv(2, y, "dli", buf);
    y += DTV_ROW_H;

    // Row 3: leaf temp / leaf-air delta / water temp
    if (d->leafTempValid) snprintf(buf, sizeof(buf), "%.1f C", d->leafTempC);
    else                  snprintf(buf, sizeof(buf), "--");
    dtv_kv(0, y, "leaf_t", buf);
    if (d->leafTempValid) snprintf(buf, sizeof(buf), "%+.1f C", d->leafTempC - d->airTemp);
    else                  snprintf(buf, sizeof(buf), "--");
    dtv_kv(1, y, "leaf_dt", buf);
    if (d->waterTemp > 0.5f && d->waterTemp < 60.0f) snprintf(buf, sizeof(buf), "%.1f C", d->waterTemp);
    else                                             snprintf(buf, sizeof(buf), "--");
    dtv_kv(2, y, "water_t", buf);
    y += DTV_ROW_H;

    // Row 4: plant height / TDS / water level
    if (d->heightValid) snprintf(buf, sizeof(buf), "%d mm", d->plantHeightMm);
    else                snprintf(buf, sizeof(buf), "--");
    dtv_kv(0, y, "height", buf);
    if (d->tdsPpm > 0) snprintf(buf, sizeof(buf), "%d ppm", d->tdsPpm);
    else               snprintf(buf, sizeof(buf), "--");
    dtv_kv(1, y, "tds", buf);
    snprintf(buf, sizeof(buf), "%s", d->waterLevelOk ? "OK" : "MATALA");
    dtv_kv(2, y, "water_lvl", buf);
    y += DTV_ROW_H + DTV_SEC_GAP;

    // ── POWER (battery + uptime; true mA/W needs INA219, not in contract) ─
    y = dtv_section(y, "POWER / UPTIME");
    y += DTV_ROW_H;
    snprintf(buf, sizeof(buf), "%.2f V", d->batteryVoltage);
    dtv_kv(0, y, "batt_v", buf);
    snprintf(buf, sizeof(buf), "%d %%", d->batteryPercent);
    dtv_kv(1, y, "batt_pct", buf);
    {
      uint32_t s = d->devUptimeS;
      if (s >= 86400UL) snprintf(buf, sizeof(buf), "%lud %luh",
                                 (unsigned long)(s/86400UL), (unsigned long)((s%86400UL)/3600UL));
      else if (s >= 3600UL) snprintf(buf, sizeof(buf), "%luh %02lum",
                                     (unsigned long)(s/3600UL), (unsigned long)((s%3600UL)/60UL));
      else snprintf(buf, sizeof(buf), "%lum %02lus",
                    (unsigned long)(s/60UL), (unsigned long)(s%60UL));
    }
    dtv_kv(2, y, "uptime", buf);
    y += DTV_ROW_H + DTV_SEC_GAP;

    // ── DEVICE ──────────────────────────────────────────────────────────
    y = dtv_section(y, "DEVICE");
    y += DTV_ROW_H;
    snprintf(buf, sizeof(buf), "%s", d->deviceStateName[0] ? d->deviceStateName : "-");
    dtv_kv(0, y, "state", buf);
    snprintf(buf, sizeof(buf), "%s", d->devicePrevStateName[0] ? d->devicePrevStateName : "-");
    dtv_kv(1, y, "prev", buf);
    snprintf(buf, sizeof(buf), "%lus", (unsigned long)(d->deviceTimeInStateMs / 1000UL));
    dtv_kv(2, y, "t_in_state", buf);
    y += DTV_ROW_H;
    snprintf(buf, sizeof(buf), "0x%02X", d->devFaultBits);
    dtv_kv(0, y, "faults", buf);
    snprintf(buf, sizeof(buf), "%s", d->deviceFaultMsg[0] ? d->deviceFaultMsg : "-");
    dtv_kv(1, y, "fault_msg", buf);
    y += DTV_ROW_H + DTV_SEC_GAP;

    // ── ACTUATORS + MOTOR (what the device is doing) ────────────────────
    y = dtv_section(y, "ACTUATORS / MOTOR");
    y += DTV_ROW_H;
    snprintf(buf, sizeof(buf), "%s", d->lightsOn ? "ON" : "OFF");
    dtv_kv(0, y, "lights", buf);
    snprintf(buf, sizeof(buf), "%s", d->pumpRunning ? "ON" : "OFF");
    dtv_kv(1, y, "pump", buf);
    snprintf(buf, sizeof(buf), "%s", d->ebbState[0] ? d->ebbState : "-");
    dtv_kv(2, y, "ebb", buf);
    y += DTV_ROW_H;
    snprintf(buf, sizeof(buf), "%d mm", d->motorHeightMm);
    dtv_kv(0, y, "motor_mm", buf);
    snprintf(buf, sizeof(buf), "%d mm", d->motorTargetMm);
    dtv_kv(1, y, "target_mm", buf);
    snprintf(buf, sizeof(buf), "%s", d->motorMoving ? "true" : "false");
    dtv_kv(2, y, "moving", buf);
    y += DTV_ROW_H + DTV_SEC_GAP;

    // ── GROWING ─────────────────────────────────────────────────────────
    y = dtv_section(y, "GROWING");
    y += DTV_ROW_H;
    snprintf(buf, sizeof(buf), "%s", d->growActive ? "true" : "false");
    dtv_kv(0, y, "active", buf);
    snprintf(buf, sizeof(buf), "%s", d->plantName[0] ? d->plantName : d->plantId);
    dtv_kv(1, y, "plant", buf);
    snprintf(buf, sizeof(buf), "ph%d / %dpv", d->growPhase, d->growElapsedDays);
    dtv_kv(2, y, "phase/day", buf);
    y += DTV_ROW_H + DTV_SEC_GAP;

    // ── LINK (e-ink ↔ PM fetch diagnostics) ─────────────────────────────
    y = dtv_section(y, "LINK (e-ink)");
    y += DTV_ROW_H;
    snprintf(buf, sizeof(buf), "%d", d->lastHttpCode);
    dtv_kv(0, y, "http", buf);
    snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)d->lastFetchDurationMs);
    dtv_kv(1, y, "fetch_dur", buf);
    snprintf(buf, sizeof(buf), "%d", d->fetchFailCount);
    dtv_kv(2, y, "fails", buf);
    y += DTV_ROW_H;
    snprintf(buf, sizeof(buf), "%s", d->lastResolvedIp[0] ? d->lastResolvedIp : "-");
    dtv_kv(0, y, "pm_ip", buf);
    snprintf(buf, sizeof(buf), "%s", d->lastMdnsResolved ? "true" : "false");
    dtv_kv(1, y, "mdns", buf);
    snprintf(buf, sizeof(buf), "%s", d->lastErrorMsg[0] ? d->lastErrorMsg : "-");
    dtv_kv(2, y, "last_err", buf);

  } while (display.nextPage());
}

#endif // DEV_TABLE_VIEW_H
