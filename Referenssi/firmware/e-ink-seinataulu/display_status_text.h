/*=====================================================================
  display_status_text.h - "Toiminta"- ja "Vaihe"-rivien tekstinmuodostus

  Eristetty display_layout.h:sta samasta syysta kuin resolve_field.h: nama
  ovat puhdasta paatoslogiikkaa (DisplayData -> merkkijono, ei GxEPD-kutsuja),
  joten test/test_status_text/ voi ajaa ne natiivisti ilman naytto-riippuvuutta.
  Molemmat renderoijat (display_layout.h USER + coach_view.h COACH) ja niiden
  contentHash kayttavat naita, joten yksi totuus per rivi.

  formatActivity       -> "Toiminta:"-rivin sisalto (mita laite tekee nyt)
  eink_composeSysLine  -> "Vaihe/Valo/Vesi"-rivi (askel kaynnissa -> kehystys)

  Kastelutila (NFT vs. ebb&flow): ENABLE_EINK_FLOOD_STATUS (config.h). NFT-
  oletuksessa tulvakohtaiset tilat piilotetaan tasta rivilta kokonaan.
=====================================================================*/

#ifndef DISPLAY_STATUS_TEXT_H
#define DISPLAY_STATUS_TEXT_H

#include <stdio.h>
#include <string.h>

#include "display_data.h"
#include "layout_def.h"   // TXT_ACT_*/TXT_SYS_* + config.h (ENABLE_EINK_FLOOD_STATUS)
#include "text_wrap.h"    // TextMeasureFn

// Compose a human-readable "what is the device doing right now" string from the
// ebb&flow + circulation activity. This is the key live signal the wall display
// was missing — it turns the raw state into Finnish the grower understands.
//
// NFT-oletus (ENABLE_EINK_FLOOD_STATUS=false): tulvakohtaiset tilat (FLOOD/
// SOAK/DRAIN + "Seuraava tulva ~N min") jatetaan pois — laite ajaa NFT:ta,
// jossa tulvituksesta puhuminen on harhaanjohtavaa. Kierto ja vesivika nakyvat
// yha, koska ne eivat ole tulvitukseen sidottuja.
static void formatActivity(const DisplayData* d, char* out, size_t n) {
  if (!d->dataValid)      { snprintf(out, n, "-"); return; }
  if (d->ebbFault)        { snprintf(out, n, TXT_ACT_FAULT); return; }
  if (d->circulateActive) { snprintf(out, n, TXT_ACT_CIRCULATE); return; }
#if ENABLE_EINK_FLOOD_STATUS
  if (strcmp(d->ebbState, "FLOOD") == 0) { snprintf(out, n, TXT_ACT_FLOOD); return; }
  if (strcmp(d->ebbState, "SOAK") == 0)  { snprintf(out, n, TXT_ACT_SOAK); return; }
  if (strcmp(d->ebbState, "DRAIN") == 0) { snprintf(out, n, TXT_ACT_DRAIN); return; }
#endif
  // IDLE / ei aktiivista kasteluvaihetta
  if (!d->growActive) { snprintf(out, n, TXT_ACT_IDLE); return; }
#if ENABLE_EINK_FLOOD_STATUS
  if (d->ebbNextCycleSec >= 0) {
    int m = d->ebbNextCycleSec / 60;
    if (m <= 0) snprintf(out, n, TXT_ACT_FLOOD_SOON);
    else        snprintf(out, n, TXT_ACT_NEXT_FLOOD, m);
    return;
  }
  snprintf(out, n, TXT_ACT_RESTING);   // ebb&flow: kasvaa tulvien valissa
#else
  snprintf(out, n, TXT_ACT_GROWING);   // NFT: jatkuva, ei tulva-aikataulua nayteta
#endif
}

// ── Tilannenayton konteksti-rivit: pakkaa palat KAHDELLE riville ────────
//
// Miksi kaksi rivia: yhdelle riville (450 px, FreeSans9pt8b) mahtui viisi
// palaa vain kun yksikot lyhennettiin tai pudotettiin ("Valo 4.2" ilman
// umol). Se teki lukemasta tulkinnanvaraisen — kayttajan havainto 28.7.2026.
// Kahdella rivilla jokainen luku saa yksikkonsa takaisin.
//
// Pakkaustapa: taytetaan rivi 1 kunnes seuraava pala ei mahdu, loput riville 2.
// Palat EIVAT katkea kesken (yksikko ei saa jaada eri riville kuin lukunsa).
// Jos kaikki ei mahdu kahdellekaan riville, loput jatetaan pois — sama
// periaate kuin ennen: layout ei saa rikkoutua hiljaa uuden kentan myota.
//
// Palauttaa kaytettyjen rivien maaran (0-2). Puhdas logiikka (mittaus tulee
// callbackina), joten test_status_text ajaa taman ilman GxEPD2:ta.
static int status_packContextLines(const char* const* parts, int nParts,
                                   int16_t maxWidthPx,
                                   TextMeasureFn measure, void* measureCtx,
                                   char* line1, size_t n1,
                                   char* line2, size_t n2) {
  if (n1) line1[0] = '\0';
  if (n2) line2[0] = '\0';
  if (!parts || nParts <= 0 || !measure) return 0;

  char* dst[2] = { line1, line2 };
  size_t cap[2] = { n1, n2 };
  size_t len[2] = { 0, 0 };
  int row = 0;

  for (int i = 0; i < nParts; i++) {
    const char* p = parts[i];
    if (!p || !p[0]) continue;

    for (;;) {
      const char* sep = len[row] ? "  " : "";
      size_t need = len[row] + strlen(sep) + strlen(p);
      if (need < cap[row]) {
        // Mittaa ehdokas ennen kuin se jaa riville.
        char probe[160];
        if (need < sizeof(probe)) {
          snprintf(probe, sizeof(probe), "%s%s%s", dst[row], sep, p);
          if (measure(probe, (int)strlen(probe), measureCtx) <= maxWidthPx) {
            snprintf(dst[row] + len[row], cap[row] - len[row], "%s%s", sep, p);
            len[row] = strlen(dst[row]);
            break;
          }
        }
      }
      // Ei mahtunut: siirry seuraavalle riville ja yrita sama pala uudelleen.
      // Tyhjalle riville mahtumaton pala pudotetaan (muuten ikuinen silmukka).
      if (row >= 1) { i = nParts; break; }
      if (len[row] == 0) break;   // pala ei mahdu edes tyhjalle riville
      row++;
    }
  }
  return (len[1] > 0) ? 2 : (len[0] > 0 ? 1 : 0);
}

// Compose the "Vaihe/Valo/Vesi"-status line. Kun ohjattu askel on kaynnissa,
// vaihe kehystetaan aloitustavan mukaan ("Idatys kaynnissa" siemenesta,
// "Juurtuminen kaynnissa" pistokkaasta) EIKA "N pv" -paivalaskuria nayteta:
// paivalaskuri on harhaanjohtava kesken idatyksen/juurtumisen (kasvi on yha
// siemen kuvun alla). Muuten normaali vaiheen nimi + paivalaskuri.
//
// Vaihe (SEEDLING/ROOTING) riittaa erottamaan siemen- ja pistokasaloituksen:
// siemenaloitus kulkee SEEDLING-vaiheen ja pistokas ROOTING-vaiheen askelten
// kautta (grow_steps.h GROW_STEP_LISTS). start_method-kentta on /api/statessa
// numero, joten e-ink ei saa sita merkkijonona — vaihe on luotettava avain.
static void eink_composeSysLine(const DisplayData* d, char* out, size_t n) {
  const char* light = d->lightsOn ? TXT_ON : TXT_OFF;
  const char* water = d->waterLevelOk ? TXT_WATER_OK : TXT_WATER_LOW;

  if (!d->growActive) {
    snprintf(out, n, TXT_SYS_IDLE, light, water);
    return;
  }

  // Sama nakyvyysehto kuin askel-laatikolla (coach_stepVisible): lista on ja
  // teksti on olemassa. Ilman tata "N pv" katoaisi myos kun askelta ei nayteta.
  bool stepActive = (d->stepCount > 0) && (d->stepText[0] != '\0');
  if (stepActive && d->growPhase == EINK_PHASE_SEEDLING) {
    snprintf(out, n, TXT_SYS_STEP, TXT_PHASE_GERMINATING, light, water);
    return;
  }
  if (stepActive && d->growPhase == EINK_PHASE_ROOTING) {
    snprintf(out, n, TXT_SYS_STEP, TXT_PHASE_ROOTING, light, water);
    return;
  }

  const char* pn = d->phaseName[0] ? d->phaseName : "-";
  snprintf(out, n, TXT_SYS_GROWING, pn, d->growElapsedDays, light, water);
}

// ── STATUS-paneelin tilalause: kumpi teksti nakyy valmennuspaneelin ylimmalla
//    rivilla (status_view.h status_drawPanel). Puhdas paatos — kutsuja on jo
//    laskenut kriittisen turvahalytyksen (resolve_advisory.h resolveCriticalAlert,
//    joka pysyy resolve_advisory.h:ssa, jottei tama tiedosto opi riippuvuutta
//    siihen suuntaan) ja antaa vain sen tuloksen booleanina + viestina tanne.
//
//    Jarjestys: turvahalytys (vesi/FAULT/huolto) > nappivihje (askel kaytetty
//    loppuun, growing.button_next_phase) > vaiheohje (growAction) > oletus.
//    Nappivihja ohittaa vaiheohjeen tahallaan: kun ohjattu askelsarja on
//    kaytetty loppuun, edellinen vaiheohje ei enaa ole se mika kayttajan
//    pitaa tehda seuraavaksi — se on "paina nappia" (raudalla havaittu
//    3.8.2026, ks. display_data.h buttonNextPhase-kommentti).
enum StatusPanelSentenceKind {
  STATUS_SENTENCE_CRITICAL = 0,  // criticalMessage, jo Latin-1 (resolveCriticalAlert)
  STATUS_SENTENCE_BUTTON_HINT,   // TXT_STATUS_BUTTON_HINT, oma UTF-8-literaali
  STATUS_SENTENCE_GROW_ACTION,   // d->growAction, jo Latin-1 (PM:n growAction)
  STATUS_SENTENCE_DEFAULT,       // TXT_STATUS_SUB_DEFAULT, oma UTF-8-literaali
  STATUS_SENTENCE_WAITING,       // "Odotetaan dataa...", oma UTF-8-literaali
};

static StatusPanelSentenceKind status_selectPanelSentenceKind(const DisplayData* d,
                                                               bool criticalActive) {
  if (criticalActive) return STATUS_SENTENCE_CRITICAL;
  if (d->dataValid && d->buttonNextPhase) return STATUS_SENTENCE_BUTTON_HINT;
  if (d->dataValid && d->growActive && d->growAction[0]) return STATUS_SENTENCE_GROW_ACTION;
  return d->dataValid ? STATUS_SENTENCE_DEFAULT : STATUS_SENTENCE_WAITING;
}

#endif // DISPLAY_STATUS_TEXT_H
