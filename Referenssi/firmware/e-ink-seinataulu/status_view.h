/*=====================================================================
  status_view.h - Tilannenaytto (SEURANTA) — pe-ohjausmalli.md V1

  Arjen paanakyma: "mita kasvi tarvitsee nyt". Anturikortit puoleen tilaan
  yhdelle riville (tavoite vs. nykyarvo), ja niiden alle iso rajattu
  VALMENNUSPANEELI:
    - lihavoitu paavaihe + edistyminen (paiva X / Y)
    - tilalause (PM:n growing.action, "mita tapahtuu")
    - HOIDAN/AUTA-rivit: mika mitta on tavoitteen ulkona, ja korjaako laite
      sen itse (HOIDAN, 1. persoona) vai pitaako kayttajan toimia (AUTA).

  Geometria: layout_def.h STATUS_* (data-sarake ~50 px leveampi kuin USER).
  Tavoitemalli + luokittelu + valmennusviestit: status_targets.h (testattu).
  Aktiivinen askel (KERTATEKO): grow_step_box.h (jaettu COACH:n kanssa).

  ── a/o piirtoraja ───────────────────────────────────────────────────
  DisplayData-merkkijonot (phaseName, growAction, plantName) ovat JO Latin-1
  (data_fetch_parse.h muunsi ne) → printataan RAAKANA (display.print /
  drawRightIn). OMAT UTF-8-literaalit (valmennusviestit status_targets.h:sta,
  paikallistekstit) muunnetaan utf8ToLatin1:lla piirtorajalla → status_u8*().
  Kaksoismuunnos korruptoisi jo-Latin-1:n, siksi raja on tiukka.
=====================================================================*/

#ifndef STATUS_VIEW_H
#define STATUS_VIEW_H

#include "config.h"
#include "data_fetch.h"
#include "layout_def.h"
#include "status_targets.h"
#include "resolve_advisory.h"  // resolveCriticalAlert(): jaettu turva-/tilahalytys (FAULT/vesi/huolto)
#include "dli_expectation.h"   // DLI-arvio kellonajan mukaan (status_evalTargetTimeAware)
#include "grow_step_box.h"     // display, fontit, drawRightIn, resolveField, fnv1a, askel-laatikko
#include "display_status_text.h"  // status_packContextLines (konteksti 2 rivia)
#include "growth_animation.h"
#include "utf8_latin1.h"

// ── OMA UTF-8-literaali (a/o) → Latin-1-muunnos ennen printtia ──────────
static void status_u8At(int x, int y, const GFXfont* font, const char* u8) {
  char b[192];
  utf8ToLatin1(u8, b, sizeof(b));
  display.setFont(font);
  display.setCursor(x, y);
  display.print(b);
}

static void status_u8Right(int right, int y, const GFXfont* font, const char* u8) {
  char b[192];
  utf8ToLatin1(u8, b, sizeof(b));
  display.setFont(font);
  int16_t x1, y1; uint16_t tw, th;
  display.getTextBounds(b, 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(right - (int)tw, y);
  display.print(b);
}

static void status_u8Centered(int x, int w, int y, const GFXfont* font, const char* u8) {
  char b[192];
  utf8ToLatin1(u8, b, sizeof(b));
  display.setFont(font);
  int16_t x1, y1; uint16_t tw, th;
  display.getTextBounds(b, 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(x + (w - (int)tw) / 2, y);
  display.print(b);
}

// Rivita + printtaa OMA UTF-8-teksti. Palauttaa seuraavan y:n.
static int status_u8WrapPrint(int x, int y, int maxW, const GFXfont* font,
                              const char* u8, int maxLines) {
  char b[192];
  utf8ToLatin1(u8, b, sizeof(b));
  display.setFont(font);
  TextLine lines[8];
  int n = text_wrap(b, (int16_t)maxW, eink_measureLatin1, (void*)font, lines, 8);
  if (n > maxLines) n = maxLines;
  const int lineH = 20;
  char lb[128];
  int yy = y;
  for (int i = 0; i < n; i++) {
    if (lines[i].len > 0) {
      size_t cp = lines[i].len < sizeof(lb) - 1 ? lines[i].len : sizeof(lb) - 1;
      memcpy(lb, b + lines[i].start, cp);
      lb[cp] = '\0';
      display.setCursor(x, yy);
      display.print(lb);
    }
    yy += lineH;
  }
  return yy;
}

// Rivita + printtaa JO LATIN-1 -muotoinen teksti (API-data: growAction).
// EI muunnosta — status_u8WrapPrint tekisi toisen muunnoksen ja korruptoisi
// jo-muunnetun tavun. Palauttaa seuraavan vapaan y:n (viimeinen rivi + lineH).
//
// Miksi rivitys tarvitaan: setTextWrap on false (display_layout.h), joten pitka
// lause EI katkea paneelin reunaan vaan valuu sen yli ja leikkautuu vasta ruudun
// reunaan. PM:n tilalause on <= 44 merkkia (grow_guidance.h), ja esim.
// "Taimi vahvistuu valossa, juuret 10-14 vrk" mittaa FreeSansBold12pt8b:lla
// ~470 px kun paneelin sisaleveys on 418 px -> ylivuoto oli pysyva.
static int status_rawWrapPrint(int x, int y, int maxW, const GFXfont* font,
                               const char* latin1, int maxLines, int lineH) {
  display.setFont(font);
  TextLine lines[4];
  int n = text_wrap(latin1, (int16_t)maxW, eink_measureLatin1, (void*)font, lines, 4);
  if (n > maxLines) n = maxLines;
  char lb[128];
  int yy = y;
  for (int i = 0; i < n; i++) {
    if (lines[i].len > 0) {
      size_t cp = lines[i].len < sizeof(lb) - 1 ? lines[i].len : sizeof(lb) - 1;
      memcpy(lb, latin1 + lines[i].start, cp);
      lb[cp] = '\0';
      display.setCursor(x, yy);
      display.print(lb);
    }
    yy += lineH;
  }
  // Tyhja teksti (n == 0): ALA lue lines[0]:aa (alustamaton) — varaa vain
  // yhden rivin vali, jotta kutsujan seuraava elementti ei hyppaa ylos.
  if (n < 1) yy = y + lineH;
  return yy;
}

// Etsi kentan tavoite vaiheen taulusta (NULL jos ei ole).
static const PeTarget* status_findTarget(uint8_t field, const PeTarget* t, int n) {
  for (int i = 0; i < n; i++) if (t[i].field == field) return &t[i];
  return nullptr;
}

// ── Yksi kompakti kortti: KOLME rivia — label / arvo / yksikko. Tavoitepoikkeama
//    naytetaan tuplareunuksella (tavoitealue itse on paneelin valmennusrivilla,
//    ei kortissa — kayttajan pyynto 19.7.2026: kortti vahemman ruuhkainen).
static void status_drawCard(int x, int y, int w, int h, uint8_t field, const char* label,
                            const DisplayData* d, const PeTarget* targets, int nTargets) {
  char cval[16], cunit[8];
  bool ok = d->dataValid && resolveField(field, d, cval, sizeof(cval), cunit, sizeof(cunit));
  if (!ok) { snprintf(cval, sizeof(cval), "--"); cunit[0] = '\0'; }

  const PeTarget* t = status_findTarget(field, targets, nTargets);
  PeStatus st = PE_NODATA;
  if (ok && t) { float v; if (status_fieldValue(field, d, &v)) st = status_classify(v, t); }

  display.drawRoundRect(x, y, w, h, 6, GxEPD_BLACK);
  if (st == PE_LOW || st == PE_HIGH) display.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 4, GxEPD_BLACK);

  // Rivi 1: label (ylareuna)
  status_u8Centered(x, w, y + 18, &FreeSans9pt8b, label);

  // Rivi 2: arvo (iso, keskella)
  display.setFont(&FreeSansBold18pt8b);
  int16_t x1, y1; uint16_t vw, vh;
  display.getTextBounds(cval, 0, 0, &x1, &y1, &vw, &vh);
  display.setCursor(x + (w - (int)vw) / 2, y + h / 2 + 8);
  display.print(cval);

  // Rivi 3: yksikko (alareuna) — korvaa entisen tavoiterivin
  if (cunit[0]) status_u8Centered(x, w, y + h - 10, &FreeSans9pt8b, cunit);
}

// ── Valmennuspaneeli: paavaihe + edistyminen + tilalause + HOIDAN/AUTA-rivit.
static void status_drawPanel(int x, int y, int w, int h, const DisplayData* d,
                             const PeTarget* targets, int nTargets) {
  const int pad = STATUS_PANEL_PAD;
  display.drawRoundRect(x, y, w, h, 8, GxEPD_BLACK);

  // Paavaihe (lihavoitu, versaalein) — phaseName on JO Latin-1 → raaka print.
  char phaseU[20];
  size_t pi = 0;
  for (; d->phaseName[pi] && pi < sizeof(phaseU) - 1; pi++) {
    char c = d->phaseName[pi];
    phaseU[pi] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  if (pi == 0) { phaseU[pi++] = '-'; }
  phaseU[pi] = '\0';
  display.setFont(&FreeSansBold18pt8b);
  display.setCursor(x + pad, y + pad + 20);
  display.print(phaseU);

  // Edistyminen oikealle (oma literaali) — paiva X / Y, tai vain X jos Y auki.
  if (d->growActive) {
    char prog[24];
    int days = d->growElapsedDays;
    if (d->growDaysLeft >= 0) snprintf(prog, sizeof(prog), TXT_STATUS_PROGRESS, days, days + d->growDaysLeft);
    else                      snprintf(prog, sizeof(prog), TXT_STATUS_PROGRESS_OPEN, days);
    status_u8Right(x + w - pad, y + pad + 18, &FreeSansBold12pt8b, prog);
  }
  display.drawLine(x + pad, y + pad + 34, x + w - pad, y + pad + 34, GxEPD_BLACK);

  // Tilalause. Valinta on ulkoistettu status_selectPanelSentenceKind():lle
  // (display_status_text.h) — puhdas paatos, testattu test_status_text:ssa.
  // Prioriteetti: kriittinen turva-/tilahalytys (FAULT/vesi/huolto, resolve_
  // advisory.h) > nappivihje (askel kaytetty loppuun, growing.button_next_phase)
  // > vaiheohje (growAction) > oletus. Nain STATUS-profiili nostaa vian/
  // vesipulan yhta prominentisti kuin COACH:n neuvontabanneri (aiemmin STATUS
  // ei nayttanyt niita lainkaan — vain vaiheen ja PE-poikkeamat), ja kertoo
  // heti kun ohjattu askelsarja on kaytetty loppuun etta nappi toimii nyt
  // (raudalla havaittu 3.8.2026: laite hyvaksyi napin, paneeli ei kertonut).
  //
  // Kaikki haarat rivittavat: pitka lause ei katkea paneelin reunaan
  // (setTextWrap false) vaan valuisi ruudun yli. CRITICAL/GROW_ACTION ovat
  // JO Latin-1 (PM lahettaa ne valmiiksi muunnettuna) → status_rawWrapPrint
  // ilman toista muunnosta. BUTTON_HINT/DEFAULT/WAITING ovat e-inkin omia
  // UTF-8-literaaleja → status_u8WrapPrint muuntaa piirtorajalla.
  const int sentY  = y + pad + 58;
  const int sentW  = w - 2 * pad;
  const int lineH  = 22;
  int sentNextY;
  AdvisoryResult crit = resolveCriticalAlert(d);
  StatusPanelSentenceKind sentenceKind =
      status_selectPanelSentenceKind(d, crit.level != ADVISORY_OK);
  switch (sentenceKind) {
    case STATUS_SENTENCE_CRITICAL:
      sentNextY = status_rawWrapPrint(x + pad, sentY, sentW, &FreeSansBold12pt8b,
                                      crit.message, 2, lineH);
      // WARN/FAULT saa korostuksen (alleviivaus viimeisen rivin alle), koska
      // mustavalkoruudulla ei ole varia joka erottaisi halytyksen tilalauseesta.
      if (crit.level == ADVISORY_WARN || crit.level == ADVISORY_FAULT) {
        const int underY = sentNextY - lineH + 5;
        display.drawLine(x + pad, underY, x + w - pad, underY, GxEPD_BLACK);
      }
      break;
    case STATUS_SENTENCE_BUTTON_HINT:
      sentNextY = status_u8WrapPrint(x + pad, sentY, sentW, &FreeSansBold12pt8b,
                                     TXT_STATUS_BUTTON_HINT, 2);
      break;
    case STATUS_SENTENCE_GROW_ACTION:
      sentNextY = status_rawWrapPrint(x + pad, sentY, sentW, &FreeSansBold12pt8b,
                                      d->growAction, 2, lineH);
      break;
    case STATUS_SENTENCE_DEFAULT:
      status_u8At(x + pad, sentY, &FreeSansBold12pt8b, TXT_STATUS_SUB_DEFAULT);
      sentNextY = sentY + lineH;
      break;
    case STATUS_SENTENCE_WAITING:
    default:
      status_u8At(x + pad, sentY, &FreeSansBold12pt8b, "Odotetaan dataa...");
      sentNextY = sentY + lineH;
      break;
  }

  if (!d->dataValid) return;

  // DLI lasketaan etukateen: PPFD-rivi piilotetaan jos DLI nayttaa jo saman
  // suunnan poikkeaman (status_ppfdRedundantWithDli, status_targets.h) - muuten
  // "valo matala" toistuisi kahtena rivina samasta oireesta. DLI voittaa koska
  // se on se yksikko joka nakyy korttirivilla.
  PeStatus dliStatusPreview = PE_NODATA;
  {
    const PeTarget* dliT = status_findTarget(FIELD_DLI, targets, nTargets);
    if (dliT) dliStatusPreview = status_evalTargetTimeAware(dliT, d, nullptr);
  }
  // Sama etukateislaskenta PPFD:lle: tarvitaan DLI-rivin ristiriitavartijaan
  // (status_dliContradictsPpfd), joka voi osua ENNEN kuin PPFD-rivi on
  // kasitelty silmukassa.
  PeStatus ppfdStatusPreview = PE_NODATA;
  {
    const PeTarget* ppfdT = status_findTarget(FIELD_PPFD, targets, nTargets);
    if (ppfdT) ppfdStatusPreview = status_evalTargetTimeAware(ppfdT, d, nullptr);
  }

  // Valmennusrivit: kaydaan vaiheen tavoitteet, poikkeamat riveiksi.
  // Aloitus seuraa tilalauseen todellista korkeutta (1 tai 2 rivia).
  int yy = sentNextY + 8;
  const int bottom = y + h - pad;
  int shown = 0;
  for (int i = 0; i < nTargets && yy < bottom - 18; i++) {
    float v;
    PeStatus st = status_evalTargetTimeAware(&targets[i], d, &v);
    if (st == PE_NODATA || st == PE_OK) continue;
    if (targets[i].field == FIELD_PPFD &&
        status_ppfdRedundantWithDli(st, dliStatusPreview)) continue;
    if (targets[i].field == FIELD_DLI &&
        status_dliContradictsPpfd(st, ppfdStatusPreview)) continue;
    const char* title; const char* body;
    status_coachMessage(targets[i].field, st, &title, &body);
    if (!title[0]) continue;

    // Badge (HOIDAN/AUTA) + otsikko
    const char* tag = targets[i].deviceActs ? TXT_STATUS_TAG_DEVICE : TXT_STATUS_TAG_USER;
    display.setFont(&FreeSansBold9pt8b);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(tag, 0, 0, &x1, &y1, &tw, &th);
    int badgeW = (int)tw + 12;
    display.drawRoundRect(x + pad, yy - 13, badgeW, 20, 4, GxEPD_BLACK);
    display.setCursor(x + pad + 6, yy + 1);
    display.print(tag);
    status_u8At(x + pad + badgeW + 8, yy + 1, &FreeSansBold9pt8b, title);

    // Tavoiteikkuna oikeaan reunaan samalle riville. Ilman tata otsikko
    // ("Ilma kuiva") ei kerro MIHIN pyritaan, eika kortin lukemaa voi verrata
    // mihinkaan — kayttajan havainto 27.7.2026. Otsikko on lihavoitu eika
    // rivity, joten piirretaan vain jos vahintaan 8 px jaa valiin.
    char tgt[24];
    status_formatTarget(&targets[i], tgt, sizeof(tgt));
    if (tgt[0]) {
      char titleL1[64];
      utf8ToLatin1(title, titleL1, sizeof(titleL1));   // mittaa piirretty muoto
      display.setFont(&FreeSansBold9pt8b);
      uint16_t titleW, titleH;
      display.getTextBounds(titleL1, 0, 0, &x1, &y1, &titleW, &titleH);
      display.setFont(&FreeSans9pt8b);
      uint16_t tgtW, tgtH;
      display.getTextBounds(tgt, 0, 0, &x1, &y1, &tgtW, &tgtH);
      int titleEnd = x + pad + badgeW + 8 + (int)titleW;
      if (titleEnd + 8 <= x + w - pad - (int)tgtW) {
        status_u8Right(x + w - pad, yy + 1, &FreeSans9pt8b, tgt);
      }
    }

    // Ohjeteksti (oma UTF-8, rivitys)
    yy += 20;
    yy = status_u8WrapPrint(x + pad + 8, yy + 2, w - 2 * pad - 8, &FreeSans9pt8b, body, 3) + 6;
    shown++;
  }
  if (shown == 0) {
    status_u8At(x + pad, sentNextY + 8, &FreeSans9pt8b, TXT_STATUS_ALL_OK);
  }
}

// ═══ Renderoija ═════════════════════════════════════════════════════════
static void status_view_render(const DisplayData* data) {
  char buf[32];
  int count = 0;
  const PeTarget* targets = status_targetsForData(data, &count);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // ── Header: kello + paivamaara ──
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
      snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      display.setFont(&FreeSansBold24pt8b);
      display.setCursor(STATUS_DATA_X, STATUS_HEADER_Y);
      display.print(buf);
      const char* days[] = {"su", "ma", "ti", "ke", "to", "pe", "la"};
      snprintf(buf, sizeof(buf), "%s %d.%d.%d", days[timeinfo.tm_wday],
               timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      drawRightIn(&FreeSansBold12pt8b, buf, STATUS_DATA_X + STATUS_DATA_W, STATUS_HEADER_Y - 5);
    }
    display.drawLine(STATUS_DATA_X, STATUS_HEADER_Y + 10,
                     STATUS_DATA_X + STATUS_DATA_W, STATUS_HEADER_Y + 10, GxEPD_BLACK);
    display.drawLine(STATUS_DIVIDER_X, STATUS_HEADER_Y + 10,
                     STATUS_DIVIDER_X, STATUS_FOOTER_Y - 20, GxEPD_BLACK);

    // ── Kasvinimi (vasen otsikko) ──
    const char* plantName = TXT_SHOWROOM_PLANT;
    if (data->dataValid) {
      if (data->plantName[0] != '\0') plantName = data->plantName;
      else if (data->plantId[0] != '\0' && strcmp(data->plantId, "?") != 0) plantName = data->plantId;
    }
    drawCenteredIn(&FreeSansBold12pt8b, plantName, STATUS_PLANT_X, STATUS_PLANT_W, STATUS_HEADER_Y);

    // ── Data-sarake: askel-laatikko TAI kortit+paneeli ──
    if (grow_stepVisible(data)) {
      grow_drawStepBox(STATUS_DATA_X, STATUS_STEP_BOX_Y, STATUS_DATA_W, STATUS_STEP_BOX_H, data);
    } else {
      for (int i = 0; i < STATUS_CARD_COUNT && i < 4; i++) {
        int cx = STATUS_DATA_X + i * (STATUS_CARD_W + STATUS_CARD_GAP);
        status_drawCard(cx, STATUS_CARDS_Y, STATUS_CARD_W, STATUS_CARD_H,
                        STATUS_CARDS[i].field, STATUS_CARDS[i].label, data, targets, count);
      }
      status_drawPanel(STATUS_DATA_X, STATUS_PANEL_Y, STATUS_DATA_W, STATUS_PANEL_H,
                       data, targets, count);
    }

    // ── Konteksti-rivit, KAKSI (lampo/RH/valo/korkeus/teho) — RAAKA (ASCII) ──
    //    Sisalto: STATUS_CONTEXT (layout_def.h § 6), oma taulukko jotta
    //    USER/COACH:n EINK_CONTEXT ei muutu. Kahdella rivilla jokaiselle
    //    luvulle mahtuu yksikko; pakkaaja mittaa ja pudottaa mika ei mahdu.
    if (data->dataValid) {
      display.setFont(&FreeSans9pt8b);
      // Rakenna palat ("Kosteus 59 %"), pakkaa ne kahdelle riville. Pakkaus on
      // puhdasta logiikkaa display_status_text.h:ssa (test_status_text ajaa sen
      // natiivisti); tama pää vastaa vain palojen muotoilusta ja piirrosta.
      char partBuf[STATUS_CONTEXT_COUNT][40];
      const char* parts[STATUS_CONTEXT_COUNT];
      int nParts = 0;
      for (int i = 0; i < STATUS_CONTEXT_COUNT; i++) {
        char cval[16], cunit[8];
        if (!resolveField(STATUS_CONTEXT[i].field, data, cval, sizeof(cval), cunit, sizeof(cunit))) continue;
        const char* lbl  = STATUS_CONTEXT[i].label;
        const char* unit = STATUS_CONTEXT[i].unitOverride ? STATUS_CONTEXT[i].unitOverride : cunit;
        snprintf(partBuf[nParts], sizeof(partBuf[0]), "%s%s%s%s%s",
                 lbl[0] ? lbl : "", lbl[0] ? " " : "",
                 cval, unit[0] ? " " : "", unit);
        parts[nParts] = partBuf[nParts];
        nParts++;
      }

      char ctxLine1[140], ctxLine2[140];
      int nLines = status_packContextLines(parts, nParts, (int16_t)STATUS_DATA_W,
                                           eink_measureLatin1, (void*)&FreeSans9pt8b,
                                           ctxLine1, sizeof(ctxLine1),
                                           ctxLine2, sizeof(ctxLine2));
      if (nLines >= 1) {
        display.setCursor(STATUS_DATA_X, STATUS_CONTEXT_Y);
        display.print(ctxLine1);
      }
      if (nLines >= 2) {
        display.setCursor(STATUS_DATA_X, STATUS_CONTEXT_Y + STATUS_CONTEXT_LINE_H);
        display.print(ctxLine2);
      }
    }

    // ── Footer: kayntiaika + leima ──
    display.drawLine(STATUS_DATA_X, STATUS_FOOTER_Y - 20,
                     STATUS_DATA_X + STATUS_DATA_W, STATUS_FOOTER_Y - 20, GxEPD_BLACK);
    display.setFont(&FreeSans9pt8b);
    if (data->dataValid) {
      uint32_t up = data->devUptimeS;
      if (up >= 86400UL) snprintf(buf, sizeof(buf), TXT_UPTIME_PREFIX "%lupv %luh",
                                  (unsigned long)(up / 86400UL), (unsigned long)((up % 86400UL) / 3600UL));
      else if (up >= 3600UL) snprintf(buf, sizeof(buf), TXT_UPTIME_PREFIX "%luh %lumin",
                                      (unsigned long)(up / 3600UL), (unsigned long)((up % 3600UL) / 60UL));
      else snprintf(buf, sizeof(buf), TXT_UPTIME_PREFIX "%lu min", (unsigned long)(up / 60UL));
    } else {
      snprintf(buf, sizeof(buf), TXT_WAITING);
    }
    display.setCursor(STATUS_DATA_X, STATUS_FOOTER_Y);
    display.print(buf);
    // M2: SD-loggerin advisory korvaa leiman kun kortti puuttuu/vikaantui.
    // ENABLE_SD_LOGGING=false (oletus) -> tama lohko ei koskaan aja, leima
    // pysyy aina TXT_WORDMARK:ina, sama kuin ennen M2:ta.
#if ENABLE_SD_LOGGING
    if (!sdlog_isReady()) {
      drawRightIn(&FreeSans9pt8b, TXT_SD_ADVISORY, STATUS_DATA_X + STATUS_DATA_W, STATUS_FOOTER_Y);
    } else
#endif
    {
      drawRightIn(&FreeSans9pt8b, TXT_WORDMARK, STATUS_DATA_X + STATUS_DATA_W, STATUS_FOOTER_Y);
    }

    // ── Kasvigrafiikka (vasen sarake) ──
#if ENABLE_EINK_GROWTH_ANIMATION
    {
      GrowthFrameView gf = growth_selectFrame(data);
      if (gf.valid) {
        int areaY = STATUS_HEADER_Y + 10;
        int areaH = DISPLAY_HEIGHT - 5 - areaY;
        int drawX = STATUS_PLANT_X + (STATUS_PLANT_W - gf.width) / 2;
        int drawY = areaY + (areaH - gf.height) / 2;
        if (drawX < STATUS_PLANT_X) drawX = STATUS_PLANT_X;
        if (drawY < areaY) drawY = areaY;
        display.drawBitmap(drawX, drawY, gf.bitmap, gf.width, gf.height, GxEPD_BLACK);
      } else {
        drawPlantArt(STATUS_PLANT_X, STATUS_HEADER_Y + 15, STATUS_PLANT_W,
                     STATUS_FOOTER_Y - 25 - (STATUS_HEADER_Y + 15),
                     data->plantId, data->motorHeightMm, data->plantHeightMm);
      }
    }
#else
    drawPlantArt(STATUS_PLANT_X, STATUS_HEADER_Y + 15, STATUS_PLANT_W,
                 STATUS_FOOTER_Y - 25 - (STATUS_HEADER_Y + 15),
                 data->plantId, data->motorHeightMm, data->plantHeightMm);
#endif

  } while (display.nextPage());

  display.hibernate();
  DEBUG_PRINT("E-ink: renderoidty (STATUS)");
}

// ═══ Content hash — render-on-change ════════════════════════════════════
static uint32_t status_view_contentHash(const DisplayData* d) {
  uint32_t h = 2166136261u;
  h = fnv1a_u32(h, d->dataValid ? 1u : 0u);
  h = fnv1a_str(h, "STATUS");
  if (!d->dataValid) return fnv1a_str(h, "WAIT");

  h = fnv1a_str(h, d->plantName[0] ? d->plantName : d->plantId);

  if (grow_stepVisible(d)) {
    h = fnv1a_str(h, "STEPBOX");
    h = fnv1a_u32(h, (uint32_t)d->growPhase);
    h = grow_stepBoxHash(h, d);
    return h;
  }

  int count = 0;
  const PeTarget* targets = status_targetsForData(d, &count);

  // Kortit: arvo + validiteetti + tavoitepoikkeaman tila (reunus muuttuu).
  for (int i = 0; i < STATUS_CARD_COUNT && i < 4; i++) {
    char cval[16], cunit[8];
    bool ok = resolveField(STATUS_CARDS[i].field, d, cval, sizeof(cval), cunit, sizeof(cunit));
    h = fnv1a_str(h, ok ? cval : "--");
    const PeTarget* t = status_findTarget(STATUS_CARDS[i].field, targets, count);
    PeStatus st = PE_NODATA;
    if (ok && t) { float v; if (status_fieldValue(STATUS_CARDS[i].field, d, &v)) st = status_classify(v, t); }
    h = fnv1a_u32(h, (uint32_t)st);
  }
  // Paneeli: vaihe + edistyminen + tilalause + jokaisen mitan tila.
  h = fnv1a_str(h, d->phaseName);
  h = fnv1a_u32(h, (uint32_t)d->growElapsedDays);
  h = fnv1a_u32(h, (uint32_t)d->growDaysLeft);
  h = fnv1a_str(h, d->growAction);
  // button_next_phase mukaan: sama bugiluokka kuin PR #252:n SD-advisory —
  // ilman tata rivia lippu voisi vaihtua tilalauseen (CRITICAL/GROW_ACTION
  // tekstin) pysyessa samana, jolloin render-on-change ei piirtaisi uutta
  // nappivihjetta lainkaan.
  h = fnv1a_u32(h, d->buttonNextPhase ? 1u : 0u);
  // Kriittinen halytys mukaan: aiemmin STATUS-hash EI kattanut deviceStateName/
  // waterLevelOk/ebbFault:ia, joten FAULT-/vesipula-siirtyma ei laukaissut
  // renderia (render-on-change olisi jattanyt vian nakymatta paneelille).
  AdvisoryResult critHash = resolveCriticalAlert(d);
  h = fnv1a_u32(h, (uint32_t)critHash.level);
  h = fnv1a_str(h, critHash.message);
  for (int i = 0; i < count; i++) {
    PeStatus st = status_evalTargetTimeAware(&targets[i], d, nullptr);
    h = fnv1a_u32(h, (uint32_t)st);
  }
  // Konteksti-rivi (STATUS_CONTEXT, sama taulukko kuin piirrossa)
  for (int i = 0; i < STATUS_CONTEXT_COUNT; i++) {
    char cval[16], cunit[8];
    if (!resolveField(STATUS_CONTEXT[i].field, d, cval, sizeof(cval), cunit, sizeof(cunit))) continue;
    h = fnv1a_str(h, cval);
  }
  h = fnv1a_u32(h, (uint32_t)d->growPhase);
  h = fnv1a_u32(h, (uint32_t)d->motorHeightMm);
#if ENABLE_SD_LOGGING
  // Footer vaihtaa TXT_WORDMARK <-> TXT_SD_ADVISORY sdlog_isReady():n
  // mukaan - sen pitaa olla hashissa, tai render-on-change piilottaisi
  // siirtymän samalla tavalla kuin critHash-kommentti yllä kuvaa.
  h = fnv1a_u32(h, sdlog_isReady() ? 1u : 0u);
#endif
  return h;
}

#endif // STATUS_VIEW_H
