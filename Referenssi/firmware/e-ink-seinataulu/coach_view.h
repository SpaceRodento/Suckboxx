/*=====================================================================
  coach_view.h - "Kasvivalmentaja" (COACH) - USER-profiili + advisory-banneri

  Design goal (CLAUDE.md / docs/ohjeet/eink_operointi.md § 11): kertoa
  paitsi mittarilukemat myos "onko tama hyva vai huono" ja "mita seuraavaksi".
  USER-profiili (display_layout.h) nayttaa raa'at PE-luvut korteilla muttei
  koskaan sano onko luku hyva vai huono.

  Ensimmainen versio kokeili 3x2-ruudukkoa + kapeaa identiteettipalkkia -
  hylatty rautatestissa (10.7.2026): fontit kutistuivat lukukelvottomiksi ja
  kasvigrafiikka ei mahtunut kapeaan palkkiin. Tama versio on tietoisesti
  paluu minimiin: SAMA geometria, SAMAT fontit, SAMA iso kasvigrafiikka ja
  SAMAT kortit kuin USER:ssa (display_layout.h) - ainoa lisays on yksi
  neuvontarivi (resolve_advisory.h), joka istuu korttiruudukon ja
  "Toiminta"-jakoviivan valiin jaavaan valmiiseen tyhjaan tilaan
  (layout_def.h: COACH_ADVISORY_TOP/H). Ei mitaan muuta uutta pinta-alaa,
  ei fonttikoon pienennysta.

  Piirtologiikka on siksi lahes identtinen display_render():n kanssa - se on
  tarkoituksellista toistoa (dev_view.h/dev_table_view.h noudattavat samaa
  "oma itsenainen renderoija per profiili" -kaytantoa), jotta COACH pysyy
  pikselintarkasti USER:n kaltaisena eika ajaudu siita eroon USER:a
  muokattaessa erikseen.
=====================================================================*/

#ifndef COACH_VIEW_H
#define COACH_VIEW_H

#include "config.h"
#include "data_fetch.h"
#include "layout_def.h"
#include "resolve_advisory.h"
#include "growth_animation.h"
#include "grow_step_box.h"    // jaettu askel-laatikko (KERTATEKO) — COACH + STATUS

#include <GxEPD2_BW.h>
#include "fonts/FreeSansBold9pt8b.h"
#include "fonts/FreeSansBold12pt8b.h"
#include "fonts/FreeSansBold18pt8b.h"
#include "fonts/FreeSansBold24pt8b.h"
#include "fonts/FreeSans9pt8b.h"

// The `display` global and resolveField()/formatActivity()/draw* helpers are
// declared in display_layout.h, included first by view_profiles.h (same
// translation unit via the .ino - header-only architecture). Reused as-is
// so COACH renders pixel-identically to USER wherever it doesn't add the
// advisory banner.
#if defined(ARDUINO_XIAO_ESP32S3)
extern GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT> display;
#else
extern GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display;
#endif

// Advisory banner - one line, priority-ordered, drawn in the gap between the
// card grid and the "Toiminta" divider (layout_def.h COACH_ADVISORY_TOP/H).
// FreeSansBold12pt8b matches the "Toiminta" row's weight/size - no new font
// size introduced. WARN/FAULT get a slightly heavier (double) border since
// monochrome has no color to lean on for emphasis.
static void coach_drawAdvisory(const AdvisoryResult& adv) {
  int bx = DATA_X, by = COACH_ADVISORY_TOP, bw = DATA_W, bh = COACH_ADVISORY_H;
  display.drawRoundRect(bx, by, bw, bh, 6, GxEPD_BLACK);
  if (adv.level == ADVISORY_WARN || adv.level == ADVISORY_FAULT) {
    display.drawRoundRect(bx + 2, by + 2, bw - 4, bh - 4, 4, GxEPD_BLACK);
  }
  display.setFont(&FreeSansBold12pt8b);
  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(adv.message, 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(bx + (bw - (int)tw) / 2, by + (bh + (int)th) / 2);
  display.print(adv.message);
}

// ═══ Askel-laatikko (WIZARD: grow-steps) ════════════════════════════════
// Kun PM raportoi aktiivisen askeleen (growing.step_count > 0), 2x2-kortit
// ja advisory-banneri KORVATAAN yhdella isolla ohjelaatikolla. Piirto ja
// naikyvyyslogiikka on grow_step_box.h:ssa (jaettu STATUS-profiilin kanssa);
// COACH sijoittaa laatikon omaan datasarakkeeseensa (DATA_X/STEP_BOX_Y).

// ─── Main entry point - copy of display_render() (display_layout.h) plus
// one extra coach_drawAdvisory() call. Kept in sync manually; if USER's
// layout changes, mirror the change here too (small, deliberate duplication
// - see file header). ─────────────────────────────────────────────────────

static void coach_view_render(const DisplayData* data) {
  char buf[32];

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // ── Header: time + date (full width) ───────────────────────
    struct tm timeinfo;
    bool hasTime = getLocalTime(&timeinfo, 100);

    if (hasTime) {
      snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      display.setFont(&FreeSansBold24pt8b);
      display.setCursor(DATA_X, HEADER_Y);
      display.print(buf);

      const char* days[] = {"su","ma","ti","ke","to","pe","la"};
      snprintf(buf, sizeof(buf), "%s %d.%d.%d",
               days[timeinfo.tm_wday], timeinfo.tm_mday, timeinfo.tm_mon + 1,
               timeinfo.tm_year + 1900);
      drawRightIn(&FreeSansBold12pt8b, buf, DATA_X + DATA_W, HEADER_Y - 5);
    }

    display.drawLine(DATA_X, HEADER_Y + 10, DATA_X + DATA_W, HEADER_Y + 10, GxEPD_BLACK);
    display.drawLine(DIVIDER_X, HEADER_Y + 10, DIVIDER_X, FOOTER_Y - 20, GxEPD_BLACK);

    // ── Plant name (small caption above the bitmap) ───────────
    const char* plantName = TXT_SHOWROOM_PLANT;
    if (data->dataValid) {
      if (data->plantName[0] != '\0') plantName = data->plantName;
      else if (data->plantId[0] != '\0' && strcmp(data->plantId, "?") != 0) {
        plantName = data->plantId;
      }
    }
    drawCenteredIn(&FreeSansBold12pt8b, plantName, PLANT_X, PLANT_W, HEADER_Y);

    // ── Data-sarakkeen ylaosa: askel-laatikko TAI kortit+advisory ──
    // Aktiivinen askel korvaa mittarikortit ja advisoryn kokonaan (kayttajan
    // valinta 16.7.2026); kun sarja on valmis (step_count == 0), palataan
    // kortteihin itsestaan.
    if (grow_stepVisible(data)) {
      grow_drawStepBox(DATA_X, STEP_BOX_Y, DATA_W, STEP_BOX_H, data);
    } else {
      // ── Value cards (2x2 grid) — identical to USER (EINK_CARDS) ──
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

      // ── Advisory banner — the one thing COACH adds over USER ────
      if (data->dataValid) {
        AdvisoryResult adv = resolveAdvisory(data);
        coach_drawAdvisory(adv);
      }
    }

    // ── Status rows (right data column) ───────────────────────
    display.drawLine(DATA_X, STATUS_Y - 30, DATA_X + DATA_W, STATUS_Y - 30, GxEPD_BLACK);

    if (data->dataValid) {
      char actBuf[40];
      formatActivity(data, actBuf, sizeof(actBuf));
      display.setFont(&FreeSansBold12pt8b);
      display.setCursor(DATA_X, STATUS_Y);
      display.print(TXT_ACT_LABEL);
      display.print(actBuf);
    }

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
                         cval, cunit[0] ? " " : "", cunit);
        if (w < 0) break;
        pos += (size_t)w;
        if (pos >= sizeof(ctxBuf) - 1) break;
      }
      display.setCursor(DATA_X, CONTEXT_Y);
      display.print(ctxBuf);
    }

    if (data->dataValid) {
      display.setFont(&FreeSans9pt8b);
      char sysBuf[80];
      eink_composeSysLine(data, sysBuf, sizeof(sysBuf));
      display.setCursor(DATA_X, SYSTEM_Y);
      display.print(sysBuf);
    }

    // ── Footer (right data column) ────────────────────────────
    display.drawLine(DATA_X, FOOTER_Y - 20, DATA_X + DATA_W, FOOTER_Y - 20, GxEPD_BLACK);
    display.setFont(&FreeSans9pt8b);

    if (data->dataValid) {
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

    // ── Plant art (left column) — identical to USER ────────────
#if ENABLE_EINK_USER_ART
    drawUserArtOverlay(PLANT_X, HEADER_Y + 15, PLANT_W, FOOTER_Y - 25 - (HEADER_Y + 15));
#elif ENABLE_EINK_GROWTH_ANIMATION
    {
      GrowthFrameView gf = growth_selectFrame(data);
      if (gf.valid) {
        int areaY = HEADER_Y + 10;
        int areaH = DISPLAY_HEIGHT - 5 - areaY;
        int drawX = PLANT_X + (PLANT_W - gf.width) / 2;
        int drawY = areaY + (areaH - gf.height) / 2;
        if (drawX < PLANT_X) drawX = PLANT_X;
        if (drawY < areaY) drawY = areaY;
        display.drawBitmap(drawX, drawY, gf.bitmap, gf.width, gf.height, GxEPD_BLACK);
      } else {
        drawPlantArt(PLANT_X, HEADER_Y + 15, PLANT_W, FOOTER_Y - 25 - (HEADER_Y + 15),
                     data->plantId, data->motorHeightMm, data->plantHeightMm);
      }
    }
#else
    drawPlantArt(PLANT_X, HEADER_Y + 15, PLANT_W, FOOTER_Y - 25 - (HEADER_Y + 15),
                 data->plantId, data->motorHeightMm, data->plantHeightMm);
#endif

  } while (display.nextPage());

  display.hibernate();
  DEBUG_PRINT("E-ink: renderoidty (COACH)");
}

// ─── Content hash — render-on-change, same discipline as USER ───────────
static uint32_t coach_view_contentHash(const DisplayData* d) {
  if (!grow_stepVisible(d)) {
    uint32_t h = display_contentHash(d);
    if (!d->dataValid) return h;
    AdvisoryResult adv = resolveAdvisory(d);
    h = fnv1a_u32(h, (uint32_t)adv.level);
    h = fnv1a_str(h, adv.message);
    return h;
  }

  // Askel nakyvissa: kortit ja advisory EIVAT ole ruudulla, joten niiden
  // arvot eivat saa aiheuttaa renderia — elava VPD polttaisi paneelin
  // refresh-budjettia 30 s valein koko liotuksen ajan ilman nakyvaa
  // muutosta. Hash kootaan vain nakyvista osista + askelkentista, ja
  // ajastimesta hashataan NAYTETTAVA merkkijono (10 min pykalat), ei
  // raakasekunteja.
  uint32_t h = 2166136261u;
  h = fnv1a_str(h, "STEPBOX");
  h = fnv1a_str(h, d->plantName[0] ? d->plantName : d->plantId);
  h = fnv1a_str(h, d->plantId);
  for (int i = 0; i < EINK_CONTEXT_COUNT; i++) {
    char cval[16], cunit[8];
    if (!resolveField(EINK_CONTEXT[i].field, d, cval, sizeof(cval), cunit, sizeof(cunit)))
      continue;
    h = fnv1a_str(h, cval);
    h = fnv1a_str(h, cunit);
  }
  char act[40];
  formatActivity(d, act, sizeof(act));
  h = fnv1a_str(h, act);
  // Composed status line captures growActive + phaseName + day count + the
  // guided-step framing (idatys/juurtuminen kaynnissa) in one hash input.
  char sys[80];
  eink_composeSysLine(d, sys, sizeof(sys));
  h = fnv1a_str(h, sys);
  h = fnv1a_u32(h, (uint32_t)d->growPhase);
  h = fnv1a_u32(h, (uint32_t)d->motorHeightMm);

  h = grow_stepBoxHash(h, d);
  return h;
}

#endif // COACH_VIEW_H
