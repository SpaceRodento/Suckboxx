/*=====================================================================
  display_layout.h - E-ink Display Rendering

  Waveshare 7.5" V2 (800x480), black and white.
  Uses GxEPD2 + Adafruit GFX for rendering.

  Layout: plant art (left, < 1/2) + sensor data (right)

  Libraries required:
    - GxEPD2
    - Adafruit GFX
=====================================================================*/

#ifndef DISPLAY_LAYOUT_H
#define DISPLAY_LAYOUT_H

#include "config.h"
#include "data_fetch.h"
#include "layout_def.h"        // geometria + tekstit + kortti/konteksti-taulut (EDITOI TÄTÄ)
#include "growth_animation.h"
#include "display_status_text.h" // formatActivity + eink_composeSysLine (puhdas, testattava)

#include <GxEPD2_BW.h>
#include "fonts/FreeSansBold9pt8b.h"
#include "fonts/FreeSansBold12pt8b.h"
#include "fonts/FreeSansBold18pt8b.h"
#include "fonts/FreeSansBold24pt8b.h"
#include "fonts/FreeSans9pt8b.h"

// Display instance
#if defined(ARDUINO_XIAO_ESP32S3)
  // reTerminal E1001 — GDEY075T7 (UC8179 controller)
  GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT>
    display(GxEPD2_750_GDEY075T7(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
#else
  // ESP32 DevKit V1 — Waveshare 7.5" V2 (GxEPD2_750_T7)
  GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT>
    display(GxEPD2_750_T7(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
#endif

// Headers below use the global `display` object declared above.
#include "plant_art.h"
#include "eink_user_art.h"

// LAYOUT CONSTANTS (geometria + tekstit + kortti/konteksti-taulut) on siirretty
// editoitavaksi dataksi → layout_def.h. Säädä etäisyyksiä ja tekstejä sieltä.

// ═══════════════════════════════════════════════════════════════════
// DRAWING HELPERS
// ═══════════════════════════════════════════════════════════════════

static void display_init() {
  display.init(115200, true, 2, false);   // initial=true, reset_duration=2
  display.setRotation(0);                  // Landscape
  display.setTextColor(GxEPD_BLACK);
  display.setTextWrap(false);
}

// Draw centered text within a horizontal region
static void drawCenteredIn(const GFXfont* font, const char* text,
                           int x, int w, int y) {
  display.setFont(font);
  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(x + (w - tw) / 2, y);
  display.print(text);
}

// Draw centered text full width (header)
static void drawCentered(const GFXfont* font, const char* text, int y) {
  drawCenteredIn(font, text, 0, DISPLAY_WIDTH, y);
}

// Draw right-aligned text in region
static void drawRightIn(const GFXfont* font, const char* text,
                        int regionRight, int y) {
  display.setFont(font);
  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(regionRight - tw, y);
  display.print(text);
}

// Draw a value card with large number and small label
static void drawCard(int x, int y, int w, int h,
                     const char* value, const char* unit, const char* label) {
  display.drawRoundRect(x, y, w, h, 8, GxEPD_BLACK);

  // Value (large, centered in card)
  display.setFont(&FreeSansBold24pt8b);
  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(value, 0, 0, &x1, &y1, &tw, &th);
  int valX = x + (w - tw) / 2;
  display.setCursor(valX, y + 50);
  display.print(value);

  // Unit (smaller, next to value)
  display.setFont(&FreeSansBold12pt8b);
  display.setCursor(valX + tw + 4, y + 45);
  display.print(unit);

  // Label (bottom of card)
  display.setFont(&FreeSans9pt8b);
  display.getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(x + (w - tw) / 2, y + h - 12);
  display.print(label);
}

// Draw a status row with label + value
static void drawStatusRow(int x, int y, const char* label, const char* value,
                          bool highlight = false) {
  display.setFont(&FreeSansBold12pt8b);
  display.setCursor(x, y);
  display.print(label);
  display.print(": ");

  if (highlight) {
    int16_t cx = display.getCursorX();
    display.fillCircle(cx + 5, y - 5, 5, GxEPD_BLACK);
    display.setCursor(cx + 16, y);
  }
  display.print(value);
}

// formatActivity() + eink_composeSysLine() ovat display_status_text.h:ssa
// (puhdas, testattava) — molemmat profiilit kayttavat samaa logiikkaa.

// ═══════════════════════════════════════════════════════════════════
// resolveField — extracted to resolve_field.h so it can be unit-tested
// without GxEPD2 (test/test_resolve_field/). See that header for the
// "add a case here" note when a new FieldId is added.
// ═══════════════════════════════════════════════════════════════════

#include "resolve_field.h"

// ═══════════════════════════════════════════════════════════════════
// CONTENT HASH — change signal for render-on-change optimization
//
// Hash ONLY what display_render() actually draws, rounded to the visible
// precision. Deliberately excludes the header clock and the "edellinen X
// min" footer: those tick every minute and must not force a full refresh
// (the .ino heartbeat keeps them fresh instead). Keep this in sync with
// display_render above if the visible fields change.
// ═══════════════════════════════════════════════════════════════════

static inline uint32_t fnv1a_u32(uint32_t h, uint32_t v) {
  const uint8_t* p = (const uint8_t*)&v;
  for (int i = 0; i < 4; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}

static inline uint32_t fnv1a_str(uint32_t h, const char* s) {
  while (*s) { h ^= (uint8_t)(*s++); h *= 16777619u; }
  return h;
}

static uint32_t display_contentHash(const DisplayData* d) {
  uint32_t h = 2166136261u;
  h = fnv1a_u32(h, d->dataValid ? 1u : 0u);
  if (!d->dataValid) {
    // Showroom/fallback layout: plant-name caption + idle art, no live data.
    return fnv1a_str(h, "FALLBACK");
  }
  // Plant name caption + art identity
  h = fnv1a_str(h, d->plantName[0] ? d->plantName : d->plantId);
  h = fnv1a_str(h, d->plantId);
  // PE value cards — hash each card's rendered value + validity. Driven by
  // EINK_CARDS (layout_def.h) so the change signal auto-tracks whatever fields
  // are configured: rounding to the shown precision happens inside resolveField.
  for (int i = 0; i < EINK_CARD_COUNT && i < 4; i++) {
    char cval[16], cunit[8];
    bool ok = resolveField(EINK_CARDS[i].field, d, cval, sizeof(cval), cunit, sizeof(cunit));
    h = fnv1a_u32(h, ok ? 1u : 0u);
    h = fnv1a_str(h, ok ? cval : "--");
  }
  // Context line — hash each rendered piece (skips invalid like the renderer).
  for (int i = 0; i < EINK_CONTEXT_COUNT; i++) {
    char cval[16], cunit[8];
    if (!resolveField(EINK_CONTEXT[i].field, d, cval, sizeof(cval), cunit, sizeof(cunit)))
      continue;
    h = fnv1a_str(h, cval);
    h = fnv1a_str(h, cunit);
  }
  // Toiminta row — hash the rendered string (already minute-quantized)
  char act[40];
  formatActivity(d, act, sizeof(act));
  h = fnv1a_str(h, act);
  // Phase / lights / water row — hash the composed line so the guided-step
  // framing (idatys/juurtuminen kaynnissa vs. "N pv") is part of the change
  // signal, not just the raw phaseName/day count.
  char sys[80];
  eink_composeSysLine(d, sys, sizeof(sys));
  h = fnv1a_str(h, sys);
  // Growth frame selection inputs (left art column)
  h = fnv1a_u32(h, (uint32_t)d->growPhase);
  h = fnv1a_u32(h, (uint32_t)d->motorHeightMm);
  return h;
}

// ═══════════════════════════════════════════════════════════════════
// MAIN RENDER
// ═══════════════════════════════════════════════════════════════════

void display_render(const DisplayData* data) {
  char buf[32];

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // ── Header: time + date (full width) ───────────────────────
    struct tm timeinfo;
    bool hasTime = getLocalTime(&timeinfo, 100);

    if (hasTime) {
      snprintf(buf, sizeof(buf), "%02d:%02d",
               timeinfo.tm_hour, timeinfo.tm_min);
      display.setFont(&FreeSansBold24pt8b);
      display.setCursor(DATA_X, HEADER_Y);
      display.print(buf);

      const char* days[] = {"su","ma","ti","ke","to","pe","la"};
      snprintf(buf, sizeof(buf), "%s %d.%d.%d",
               days[timeinfo.tm_wday],
               timeinfo.tm_mday, timeinfo.tm_mon + 1,
               timeinfo.tm_year + 1900);
      drawRightIn(&FreeSansBold12pt8b, buf, DATA_X + DATA_W, HEADER_Y - 5);
    }

    // Header separator (data column only)
    display.drawLine(DATA_X, HEADER_Y + 10,
                     DATA_X + DATA_W, HEADER_Y + 10, GxEPD_BLACK);

    // ── Vertical divider ──────────────────────────────────────
    display.drawLine(DIVIDER_X, HEADER_Y + 10,
                     DIVIDER_X, FOOTER_Y - 20, GxEPD_BLACK);

    // ── Plant name (small caption above the bitmap) ───────────
    // Showroom-default: kun dataa ei viela ole, nayta "Basilika" otsikko
    // ja basilika-kuva idle-tilassa, ei tyhjaa nayttoa.
    // Use the display name PlantMeister now provides (covers salaatti/testikasvi
    // too); fall back to the id, then the showroom-default "Basilika".
    const char* plantName = TXT_SHOWROOM_PLANT;
    if (data->dataValid) {
      if (data->plantName[0] != '\0') plantName = data->plantName;
      else if (data->plantId[0] != '\0' && strcmp(data->plantId, "?") != 0) {
        plantName = data->plantId;
      }
    }
    drawCenteredIn(&FreeSansBold12pt8b, plantName,
                   PLANT_X, PLANT_W, HEADER_Y);

    // ── Value cards (2x2 grid) — data-driven from EINK_CARDS (layout_def.h).
    //    Vaihda kortin mittari/otsikko sieltä; arvon muotoilu = resolveField().
    int cardStartX = DATA_X + (DATA_W - (2 * CARD_W + CARD_GAP)) / 2;
    for (int i = 0; i < EINK_CARD_COUNT && i < 4; i++) {
      int row = i / 2, col = i % 2;
      int cx = cardStartX + col * (CARD_W + CARD_GAP);
      int cy = CARDS_Y + row * (CARD_H + CARD_ROW_GAP);
      char cval[16], cunit[8];
      bool ok = data->dataValid &&
                resolveField(EINK_CARDS[i].field, data, cval, sizeof(cval), cunit, sizeof(cunit));
      if (!ok) { snprintf(cval, sizeof(cval), "--"); cunit[0] = '\0'; }
      drawCard(cx, cy, CARD_W, CARD_H, cval, cunit, EINK_CARDS[i].label);
    }

    // ── Status rows (right data column) ───────────────────────
    // Divider sits well above the Toiminta baseline (bold12 ascends ~17 px).
    display.drawLine(DATA_X, STATUS_Y - 30,
                     DATA_X + DATA_W, STATUS_Y - 30, GxEPD_BLACK);

    if (data->dataValid) {
      // ── Toiminta: what the device is doing right now (the key live signal) ──
      char actBuf[40];
      formatActivity(data, actBuf, sizeof(actBuf));
      display.setFont(&FreeSansBold12pt8b);
      display.setCursor(DATA_X, STATUS_Y);
      display.print(TXT_ACT_LABEL);
      display.print(actBuf);
    }

    // ── Context line — data-driven from EINK_CONTEXT (layout_def.h). One line of
    //    "label value unit" pieces; invalid fields are skipped automatically. ──
    if (data->dataValid) {
      display.setFont(&FreeSans9pt8b);
      char ctxBuf[120];
      size_t pos = 0;
      ctxBuf[0] = '\0';
      for (int i = 0; i < EINK_CONTEXT_COUNT; i++) {
        char cval[16], cunit[8];
        if (!resolveField(EINK_CONTEXT[i].field, data, cval, sizeof(cval), cunit, sizeof(cunit)))
          continue;
        const char* lbl = EINK_CONTEXT[i].label;
        int w = snprintf(ctxBuf + pos, sizeof(ctxBuf) - pos, "%s%s%s%s%s%s",
                         pos ? "   " : "",
                         lbl[0] ? lbl : "", lbl[0] ? " " : "",
                         cval,
                         cunit[0] ? " " : "", cunit);
        if (w < 0) break;
        pos += (size_t)w;
        if (pos >= sizeof(ctxBuf) - 1) break;
      }
      display.setCursor(DATA_X, CONTEXT_Y);
      display.print(ctxBuf);
    }

    // ── Phase name + lights + water (one composed line) ────────
    if (data->dataValid) {
      display.setFont(&FreeSans9pt8b);
      char sysBuf[80];
      eink_composeSysLine(data, sysBuf, sizeof(sysBuf));
      display.setCursor(DATA_X, SYSTEM_Y);
      display.print(sysBuf);
    }

    // ── Footer (right data column) ────────────────────────────
    display.drawLine(DATA_X, FOOTER_Y - 20,
                     DATA_X + DATA_W, FOOTER_Y - 20, GxEPD_BLACK);

    display.setFont(&FreeSans9pt8b);

    if (data->dataValid) {
      // PlantMeister-laitteen käyntiaika (dev.uptime_s). Vaihtelee hitaasti
      // -> ei hashata (footer päivittyy heartbeatilla, ei pakota refreshiä).
      uint32_t up = data->devUptimeS;
      if (up >= 86400UL) {
        snprintf(buf, sizeof(buf), TXT_UPTIME_PREFIX "%lupv %luh",
                 (unsigned long)(up / 86400UL), (unsigned long)((up % 86400UL) / 3600UL));
      } else if (up >= 3600UL) {
        snprintf(buf, sizeof(buf), TXT_UPTIME_PREFIX "%luh %lumin",
                 (unsigned long)(up / 3600UL), (unsigned long)((up % 3600UL) / 60UL));
      } else {
        snprintf(buf, sizeof(buf), TXT_UPTIME_PREFIX "%lu min", (unsigned long)(up / 60UL));
      }
    } else {
      snprintf(buf, sizeof(buf), TXT_WAITING);
    }

    display.setCursor(DATA_X, FOOTER_Y);
    display.print(buf);

    drawRightIn(&FreeSans9pt8b, TXT_WORDMARK, DATA_X + DATA_W, FOOTER_Y);

    // ── Plant art (left column) ───────────────────────────────
    // growth_selectFrame osaa palauttaa showroom-default basilikan myos
    // silloin kun data->dataValid == false. Alue ei jaa koskaan tyhjaksi.
#if ENABLE_EINK_USER_ART
    drawUserArtOverlay(PLANT_X, HEADER_Y + 15, PLANT_W,
                       FOOTER_Y - 25 - (HEADER_Y + 15));
#elif ENABLE_EINK_GROWTH_ANIMATION
    {
      GrowthFrameView gf = growth_selectFrame(data);
      if (gf.valid) {
        // Vasen sarake on omanaan ja voi kayttaa lahes koko korkeuden
        // otsikon jalkeen naytoon asti (480). Oikealla puolella on omat
        // rivinsa (STATUS_Y/SYSTEM_Y/FOOTER_Y), ne eivat tormaa tahan.
        int areaY = HEADER_Y + 10;
        int areaH = DISPLAY_HEIGHT - 5 - areaY;
        int drawX = PLANT_X + (PLANT_W - gf.width) / 2;
        int drawY = areaY + (areaH - gf.height) / 2;
        if (drawX < PLANT_X) drawX = PLANT_X;
        if (drawY < areaY) drawY = areaY;

        display.drawBitmap(drawX, drawY,
                           gf.bitmap,
                           gf.width,
                           gf.height,
                           GxEPD_BLACK);
      } else {
        drawPlantArt(PLANT_X, HEADER_Y + 15, PLANT_W,
                     FOOTER_Y - 25 - (HEADER_Y + 15),
                     data->plantId,
                     data->motorHeightMm,
                     data->plantHeightMm);
      }
    }
#else
    drawPlantArt(PLANT_X, HEADER_Y + 15, PLANT_W,
                 FOOTER_Y - 25 - (HEADER_Y + 15),
                 data->plantId,
                 data->motorHeightMm,
                 data->plantHeightMm);
#endif

  } while (display.nextPage());

  display.hibernate();
  DEBUG_PRINT("E-ink: renderoidty");
}

// ═══════════════════════════════════════════════════════════════════
// ERROR SCREEN
// ═══════════════════════════════════════════════════════════════════

void display_renderError(const char* message) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    drawCentered(&FreeSansBold18pt8b, "PlantMeister", 180);
    drawCentered(&FreeSansBold12pt8b, message, 230);

    if (g_displayData.dataValid) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Viimeisin: %.1f C, %.0f%%",
               g_displayData.airTemp, g_displayData.airHumidity);
      drawCentered(&FreeSans9pt8b, buf, 280);
    }

    drawCentered(&FreeSans9pt8b, "Yritetaan uudelleen...", 320);

  } while (display.nextPage());
  display.hibernate();
}

#endif // DISPLAY_LAYOUT_H
