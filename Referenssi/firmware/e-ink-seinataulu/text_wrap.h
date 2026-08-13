/*=====================================================================
  text_wrap.h - Latin-1-tekstin rivitys annettuun pikselileveyteen

  Puhdas moduuli (ei Adafruit-riippuvuutta): leveys mitataan kutsujan
  antamalla callbackilla, joten sama rivitys testataan natiivisti
  vakiolevyisella mittarilla (test/test_text_wrap/) ja ajetaan laitteella
  GFX-fontin xAdvance-summalla (coach_view.h eink_measureLatin1).

  Sopimus:
    - Syote on LATIN-1-TAVUJA, ei UTF-8:aa. Muunnos on tehty kerran
      parse-vaiheessa (utf8_latin1.h) — yksi tavu == yksi merkki, joten
      indeksit eivat voi katkaista monitavusekvenssia.
    - '\n' on kova rivinvaihto. "\n\n" tuottaa tyhjan rivin (kappalevali
      sailyy — PM:n askeltekstit kayttavat sita tarkoituksella).
    - Rivia pidempi sana katkaistaan kovasti merkkirajalta. Vahintaan
      yksi merkki etenee aina -> silmukka EI voi jaada ikuiseksi vaikka
      maxWidthPx olisi pienempi kuin yhden merkin leveys.
    - Palautus on indeksipareja alkuperaiseen tekstiin (ei kopioita, ei
      allokaatioita). maxLines tayttyessa loput jaa pois — kutsuja voi
      verrata paluuarvoa maxLines:iin ja vaihtaa pienempaan fonttiin.
=====================================================================*/

#ifndef TEXT_WRAP_H
#define TEXT_WRAP_H

#include <stdint.h>

// Palauttaa tekstisegmentin [s, s+len) leveyden pikseleina. ctx on
// kutsujan oma (esim. GFXfont-osoitin). len on tavuja, ei koskaan
// negatiivinen.
typedef int16_t (*TextMeasureFn)(const char* s, int len, void* ctx);

struct TextLine {
  uint16_t start;   // rivin alkuindeksi alkuperaisessa tekstissa
  uint16_t len;     // rivin pituus tavuina (voi olla 0 = tyhja rivi)
};

// Rivita text -> lines. Palauttaa rivien kokonaismaaran (voi olla >
// maxLines; silloin vain maxLines ensimmaista on taulukossa ja kutsuja
// tietaa etta teksti ei mahdu).
inline int text_wrap(const char* text, int16_t maxWidthPx,
                     TextMeasureFn measure, void* ctx,
                     TextLine* lines, int maxLines) {
  if (!text || !measure || maxWidthPx <= 0) return 0;

  int lineCount = 0;
  uint16_t pos = 0;

  // Yksi iteraatio = yksi tuotettu rivi. pos etenee joka kierroksella
  // vahintaan yhden (merkki tai '\n') -> paattyy aina.
  while (text[pos] != '\0') {
    uint16_t lineStart = pos;
    uint16_t lineEnd   = pos;    // eksklusiivinen: viimeinen mahtunut
    uint16_t scan      = pos;

    // Kova rivinvaihto heti? -> tyhja rivi (kappalevali).
    if (text[pos] == '\n') {
      if (lineCount < maxLines) { lines[lineCount].start = pos; lines[lineCount].len = 0; }
      lineCount++;
      pos++;
      continue;
    }

    // Keraa sanoja kunnes leveys ylittyy, '\n' tulee vastaan tai teksti
    // loppuu. "Sana" = valilyonteihin/\n:aan paattyva jakso.
    while (text[scan] != '\0' && text[scan] != '\n') {
      // Ohita sanan edelta valilyonnit (paitsi rivin alussa: ei sisennysta).
      uint16_t wordStart = scan;
      while (text[scan] != '\0' && text[scan] != '\n' && text[scan] != ' ') scan++;
      uint16_t wordEnd = scan;   // eksklusiivinen

      if (wordEnd > wordStart) {
        int16_t w = measure(text + lineStart, (int)(wordEnd - lineStart), ctx);
        if (w <= maxWidthPx) {
          lineEnd = wordEnd;     // sana mahtuu talle riville
        } else if (lineEnd == lineStart) {
          // Ensimmainenkaan sana ei mahdu -> kova katkaisu: pisin
          // merkkimaara joka mahtuu, kuitenkin vahintaan 1 (eteneminen).
          uint16_t cut = wordStart + 1;
          while (cut < wordEnd &&
                 measure(text + lineStart, (int)(cut + 1 - lineStart), ctx) <= maxWidthPx) {
            cut++;
          }
          lineEnd = cut;
          break;
        } else {
          break;                 // sana seuraavalle riville
        }
      }

      // Syo sanan jalkeiset valilyonnit (ne eivat aloita seuraavaa rivia).
      while (text[scan] == ' ') scan++;
    }

    if (lineEnd == lineStart) {
      // Rivi jai tyhjaksi (esim. pelkkia valilyonteja ennen '\n':aa) —
      // hyppaa niiden yli etenemisen takaamiseksi.
      while (text[pos] == ' ') pos++;
      if (text[pos] == '\0') break;
      continue;
    }

    if (lineCount < maxLines) {
      lines[lineCount].start = lineStart;
      lines[lineCount].len   = (uint16_t)(lineEnd - lineStart);
    }
    lineCount++;

    // Seuraava rivi alkaa mahtuneen osan jalkeen; valilyonnit ja yksi
    // mahdollinen '\n' ohitetaan (sanavalin rivinvaihto ei tuota tyhjaa
    // rivia — vain eksplisiittinen "\n\n" tuottaa).
    pos = lineEnd;
    while (text[pos] == ' ') pos++;
    if (text[pos] == '\n') pos++;
  }

  return lineCount;
}

#endif // TEXT_WRAP_H
