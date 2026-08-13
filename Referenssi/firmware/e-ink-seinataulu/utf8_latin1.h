/*=====================================================================
  utf8_latin1.h - UTF-8 -> Latin-1 -muunnos e-inkin piirtoa varten

  ── Miksi tama on olemassa ───────────────────────────────────────────
  Skandien saaminen naytolle on KAKSI eri ongelmaa, ja fonttien
  laajentaminen ratkaisee vain toisen:

    1. Glyyfit. Adafruitin FreeSans-fontit kattavat 0x20-0x7E. Ratkaistu
       generoimalla 0x20-0xFF (scripts/gen_eink_fonts.sh, fonts/*8b.h).

    2. ENKOODAUS — tama tiedosto. Lahdekoodi ja /api/state-JSON ovat
       UTF-8:aa, jossa 'a' on KAKSI tavua (0xC3 0xA4). Adafruit_GFX:n
       print() piirtaa tavu kerrallaan, joten se piirtaisi glyyfit 0xC3
       ja 0xA4 -> "Ã¤". Fontit eivat auta lainkaan ilman muunnosta.

  GFX-fontti indeksoidaan tavulla, joten sen merkkiavaruus ON Latin-1
  (0x00-0xFF). Muunnos on siis suora: UTF-8-koodipiste <= 0xFF -> yksi
  tavu; kaikki muu ei mahdu fonttiin lainkaan.

  ── Mika EI mahdu Latin-1:een ────────────────────────────────────────
  Typografiset merkit: en-dash (U+2013), lainausmerkit (U+201C/201D),
  ajatusviiva (U+2014), ellipsi (U+2026). Nama korvataan lahimmalla
  ASCII-vastineella eika pudoteta, koska hiljainen katoaminen keskella
  lausetta on pahempi kuin '-' vaarassa paikassa. Kirjoita tekstit
  suoraan ASCII-viivalla — talloin korvaus ei koskaan laukea.

  Aste-merkki ° (U+00B0 -> 0xB0) ja skandit (a o A O a) ovat Latin-1:ssa
  ja menevat lapi sellaisenaan.

  Puhdas funktio ilman Arduino-riippuvuuksia -> natiivitestattava
  (test/test_utf8_latin1).
=====================================================================*/

#ifndef UTF8_LATIN1_H
#define UTF8_LATIN1_H

#include <stddef.h>
#include <stdint.h>

// Korvaa Latin-1:sta puuttuvan koodipisteen ASCII-vastineella.
// Palauttaa 0 jos vastinetta ei ole (merkki jatetaan pois).
static inline char utf8_latin1_fallback(uint32_t cp) {
  switch (cp) {
    case 0x2010:            // hyphen
    case 0x2011:            // non-breaking hyphen
    case 0x2012:            // figure dash
    case 0x2013:            // en dash
    case 0x2014:            // em dash
    case 0x2015: return '-';
    case 0x2018:            // ' left single quote
    case 0x2019: return '\'';  // ' right single quote / heittomerkki
    case 0x201C:            // " left double quote
    case 0x201D: return '"';
    case 0x2026: return '.';   // ellipsi -> yksi piste (kolmea ei mahduteta)
    default:     return 0;
  }
}
// HUOM sitova valilyonti (U+00A0): sita EI ole talla listalla eika pida olla.
// Se on Latin-1:ssa (0xA0) ja menee suoraan lapi. Fontissa 0xA0:lla on
// identtinen metriikka valilyonnin kanssa (1x1, xAdvance 7), joten se
// piirtyy valilyontina — ja rivitys, joka katkaisee vain 0x20:ssa, jattaa
// sen katkaisematta. Se on tasmalleen mita sitova valilyonti tarkoittaa.

// Muunna UTF-8 -> Latin-1. Kirjoittaa aina nollatermioidun merkkijonon.
// Palauttaa kirjoitettujen tavujen maaran (ilman nollaa).
//
// out saa olla sama kuin in (in-place) — muunnos ei koskaan kasvata pituutta:
// UTF-8 koodaa kaikki Latin-1-merkit 1-2 tavulla ja me tuotamme aina 1.
inline size_t utf8ToLatin1(const char* in, char* out, size_t outSize) {
  if (!out || outSize == 0) return 0;
  if (!in) { out[0] = '\0'; return 0; }

  size_t w = 0;
  const uint8_t* p = (const uint8_t*)in;

  while (*p && w + 1 < outSize) {
    uint32_t cp;
    uint8_t  c = *p;

    if (c < 0x80) {                       // ASCII
      cp = c; p += 1;
    } else if ((c & 0xE0) == 0xC0) {      // 2 tavua: U+0080..U+07FF
      if ((p[1] & 0xC0) != 0x80) { p += 1; continue; }   // katkennut -> ohita
      cp = ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else if ((c & 0xF0) == 0xE0) {      // 3 tavua: U+0800..U+FFFF
      if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) { p += 1; continue; }
      cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
    } else if ((c & 0xF8) == 0xF0) {      // 4 tavua: U+10000+ (emoji yms.)
      if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) {
        p += 1; continue;
      }
      cp = 0x10000;                       // ei koskaan mahdu -> fallback-polku
      p += 4;
    } else {                              // vale-jatkotavu tai roska
      p += 1; continue;
    }

    if (cp <= 0xFF) {
      out[w++] = (char)cp;                // suoraan Latin-1:ksi
    } else {
      char sub = utf8_latin1_fallback(cp);
      if (sub) out[w++] = sub;            // ei sijaista -> merkki jatetaan pois
    }
  }

  out[w] = '\0';
  return w;
}

#endif // UTF8_LATIN1_H
