/*=====================================================================
  grow_step_box.h - Ohjatun askeleen iso ohjelaatikko (KERTATEKO)

  Kun PM raportoi aktiivisen askeleen (growing.step_count > 0), mittarikortit
  KORVATAAN yhdella isolla ohjelaatikolla (kayttajan valinta 16.7.2026:
  ohjeteksti saa koko mittarien tilan). Ohjeteksti tulee PM:lta (grow_steps.h)
  valmiiksi Latin-1:na; paikalliset otsikot ovat layout_def.h TXT_STEP_* (ASCII).

  Irrotettu coach_view.h:sta (19.7.2026) jaettavaksi COACH- ja STATUS-profiilien
  kesken: piirtaja on parametrinen laatikon suorakulmion (bx,by,bw,bh) suhteen,
  joten kumpikin profiili voi sijoittaa sen omaan datasarakkeeseensa. Yksi kopio
  poistaa `static`-funktioiden kaksoismaarittelyn samassa kaannosyksikossa.

  Puhdas piirto — logiikka (mika askel, ajastin) tulee DisplayDatasta.
=====================================================================*/

#ifndef GROW_STEP_BOX_H
#define GROW_STEP_BOX_H

#include "config.h"
#include "layout_def.h"          // TXT_STEP_* + STEP_* geometria (laatikon sisukset)
#include "display_layout.h"      // display-objekti, fontit, drawRightIn/drawCenteredIn
#include "text_wrap.h"           // rivitys

// GFX-fontin xAdvance-summa Latin-1-segmentille. Tasmallinen GFX-fonteille
// (ei kerningia) ja valttaa getTextBounds-kutsun vaatimat nollaterminoidut
// valikopiot rivityksen aikana.
static int16_t eink_measureLatin1(const char* s, int len, void* ctx) {
  const GFXfont* f = (const GFXfont*)ctx;
  uint8_t first = pgm_read_byte(&f->first);
  uint8_t last  = pgm_read_byte(&f->last);
  const GFXglyph* glyphs = (const GFXglyph*)pgm_read_ptr(&f->glyph);
  int16_t w = 0;
  for (int i = 0; i < len; i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < first || c > last) continue;
    w += (int16_t)pgm_read_byte(&glyphs[c - first].xAdvance);
  }
  return w;
}

// Ajastinrivi laatikon otsikkoon. Sama funktio tuottaa merkkijonon seka
// piirtoon etta contentHashiin -> naytto renderoituu vain kun NAYTETTAVA
// arvo muuttuu (10 min pykalat, ei joka sekunti eläva laskuri).
static void grow_stepTimerString(const DisplayData* d, char* buf, size_t size) {
  buf[0] = '\0';
  if (d->stepRemainingSec >= 0) {
    // TIMER-askel: laskee alas, pyoristys ylospain 10 min pykaliin.
    int mins = (int)((d->stepRemainingSec + 59) / 60);
    int g = STEP_TIMER_GRANULARITY_MIN;
    if (mins <= g) {
      snprintf(buf, size, TXT_STEP_TIMER_SOON, g);
    } else {
      snprintf(buf, size, TXT_STEP_TIMER_LEFT, ((mins + g - 1) / g) * g);
    }
  } else if (d->stepTimerMin > 0) {
    // BUTTON-askel jolla on tekstikynnys (idatys): kulunut aika ylospain.
    uint32_t hours = d->stepElapsedSec / 3600UL;
    if (hours >= 24) {
      snprintf(buf, size, TXT_STEP_ELAPSED_DAYS, (int)(hours / 24));
    } else if (hours >= 1) {
      snprintf(buf, size, TXT_STEP_ELAPSED_HOURS, (int)hours);
    }
    // Alle tunti: ei ajastintekstia ("kulunut 0 h" olisi kohinaa).
  }
}

static bool grow_stepVisible(const DisplayData* d) {
  return d->dataValid && d->stepCount > 0 && d->stepText[0] != '\0';
}

// Piirra iso ohjelaatikko annettuun suorakulmioon (bx,by,bw,bh).
static void grow_drawStepBox(int bx, int by, int bw, int bh, const DisplayData* d) {
  display.drawRoundRect(bx, by, bw, bh, 8, GxEPD_BLACK);
  if (d->stepAwaiting) {
    // Tuplareunus = "vaatii reagointia" — sama korostuskeino kuin advisoryn
    // WARN/FAULT-tasoilla (mustavalkoisella ei ole varia nojattavaksi).
    display.drawRoundRect(bx + 2, by + 2, bw - 4, bh - 4, 6, GxEPD_BLACK);
  }

  // ── Otsikkorivi: "Askel 1/3" + ajastin oikealla ──
  char hdr[20];
  snprintf(hdr, sizeof(hdr), TXT_STEP_HEADER, d->stepIndex + 1, d->stepCount);
  int hdrBaseline = by + STEP_BOX_PAD + 20;
  display.setFont(&FreeSansBold12pt8b);
  display.setCursor(bx + STEP_BOX_PAD, hdrBaseline);
  display.print(hdr);
  char timer[32];
  grow_stepTimerString(d, timer, sizeof(timer));
  if (timer[0]) drawRightIn(&FreeSansBold12pt8b, timer, bx + bw - STEP_BOX_PAD, hdrBaseline);
  display.drawLine(bx + STEP_BOX_PAD, by + STEP_HEADER_H + 8,
                   bx + bw - STEP_BOX_PAD, by + STEP_HEADER_H + 8, GxEPD_BLACK);

  // ── Ohjeteksti: rivitys + automaattinen fonttipudotus ──
  // Ensin 12pt bold (luettavuus etaalta); jos ei mahdu, 9pt (mahtuu ~2x
  // enemman). Jos ei sittenkaan, loppu leikkautuu — pitka teksti kuuluu
  // kirjoittaa lyhyemmaksi grow_steps.h:ssa, ja eink_preview.py --step
  // nayttaa ongelman ennen flashia.
  const int bodyTop    = by + STEP_HEADER_H + 8 + 6;
  const int bodyBottom = by + bh - STEP_FOOTER_H - 6;
  const int textW      = bw - 2 * STEP_BOX_PAD;

  const GFXfont* bodyFont = &FreeSansBold12pt8b;
  int lineH = 26 + STEP_LINE_GAP;
  TextLine lines[16];
  int capacity = (bodyBottom - bodyTop) / lineH;
  if (capacity > 16) capacity = 16;
  int n = text_wrap(d->stepText, (int16_t)textW, eink_measureLatin1,
                    (void*)bodyFont, lines, 16);
  if (n > capacity) {
    bodyFont = &FreeSans9pt8b;
    lineH = 19 + STEP_LINE_GAP;
    capacity = (bodyBottom - bodyTop) / lineH;
    if (capacity > 16) capacity = 16;
    n = text_wrap(d->stepText, (int16_t)textW, eink_measureLatin1,
                  (void*)bodyFont, lines, 16);
  }
  if (n > capacity) n = capacity;   // leikkaus: ei koskaan footerin paalle

  display.setFont(bodyFont);
  char lineBuf[96];
  int yy = bodyTop + lineH - STEP_LINE_GAP;   // ensimmaisen rivin baseline
  for (int i = 0; i < n; i++) {
    if (lines[i].len > 0) {
      size_t cp = lines[i].len < sizeof(lineBuf) - 1 ? lines[i].len : sizeof(lineBuf) - 1;
      memcpy(lineBuf, d->stepText + lines[i].start, cp);
      lineBuf[cp] = '\0';
      display.setCursor(bx + STEP_BOX_PAD, yy);
      display.print(lineBuf);
    }
    yy += lineH;
  }

  // ── Alarivin kehote ──
  const char* hint = d->stepAckAllowed ? TXT_STEP_ACK_HINT : TXT_STEP_TIMER_HINT;
  drawCenteredIn(d->stepAckAllowed ? &FreeSansBold9pt8b : &FreeSans9pt8b,
                 hint, bx, bw, by + bh - STEP_BOX_PAD);
}

// Hash-panos askel-laatikolle — sama sisalto kuin piirto (10 min ajastin-
// pykalat, ei raakasekunteja) -> naytto ei paivity turhaan.
static uint32_t grow_stepBoxHash(uint32_t h, const DisplayData* d) {
  h = fnv1a_u32(h, (uint32_t)d->stepIndex);
  h = fnv1a_u32(h, (uint32_t)d->stepCount);
  h = fnv1a_u32(h, d->stepLate ? 1u : 2u);
  h = fnv1a_u32(h, d->stepAwaiting ? 1u : 2u);
  h = fnv1a_u32(h, d->stepAckAllowed ? 1u : 2u);
  h = fnv1a_str(h, d->stepText);
  char timer[32];
  grow_stepTimerString(d, timer, sizeof(timer));
  h = fnv1a_str(h, timer);
  return h;
}

#endif // GROW_STEP_BOX_H
