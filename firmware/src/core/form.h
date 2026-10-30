#pragma once
// Suckboxx core - web-lomakkeen avain=arvo-parit ja virhetekstit (WP-6,
// docs/firmware.md §4.8).
//
// Kaikki POST-reitit (§5: /api/config, /api/master, /api/sim) saavat
// form-urlencoded-parit. Liima (WP-7) kokoaa ne AsyncWebServerin
// parametreista tähän muotoon kerran, ja core tulkitsee ne. Näin jokaisen
// reitin säännöt (atomisuus, virheteksti) ovat native-testattavissa eivätkä
// jää liimaan.
//
// Virheteksti menee UI:hin sellaisenaan ({"ok":false,"err":"…"}), joten se ei
// saa katketa kesken UTF-8-merkin: puolikas "ä" näkyisi selaimessa
// korvausmerkkinä.

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace sbx {

// Yksi lomakkeen kenttä. Merkkijonot ovat kutsujan (AsyncWebParameter), ja
// niiden on elettävä kutsun ajan. URL-dekoodaus on jo tehty.
struct FormPair {
  const char* key;
  const char* value;
};

// Virhetekstin puskuri. Pisin core-teksti (configValidate, ~150 tavua) mahtuu
// katkeamatta; test_processor mittaa jokaisen hylkäyssyyn pituuden.
constexpr size_t kFormErrLen = 192;

namespace form_detail {

// Lyhentää NUL-päätetyn merkkijonon niin, ettei viimeinen UTF-8-merkki jää
// kesken (snprintf katkaisee tavurajalla).
inline void utf8TrimTail(char* s) {
  size_t len = std::strlen(s);
  size_t cont = 0;
  while (len > 0 && cont < 3 && (static_cast<unsigned char>(s[len - 1]) & 0xC0u) == 0x80u) {
    --len;
    ++cont;
  }
  if (len == 0) {
    if (cont > 0) s[0] = '\0';
    return;
  }
  const unsigned char lead = static_cast<unsigned char>(s[len - 1]);
  size_t need = 0;
  if ((lead & 0xE0u) == 0xC0u) {
    need = 1;
  } else if ((lead & 0xF0u) == 0xE0u) {
    need = 2;
  } else if ((lead & 0xF8u) == 0xF0u) {
    need = 3;
  }
  if (lead >= 0x80u && cont < need) s[len - 1] = '\0';
}

// snprintf kiinteään puskuriin + UTF-8-turvallinen katkaisu. dst = nullptr
// tai n = 0 -> ei-op, jolloin kutsuja voi jättää virhetekstin pyytämättä.
__attribute__((format(printf, 3, 4)))
inline void formatText(char* dst, size_t n, const char* fmt, ...) {
  if (dst == nullptr || n == 0) return;
  va_list ap;
  va_start(ap, fmt);
  const int r = std::vsnprintf(dst, n, fmt, ap);
  va_end(ap);
  if (r < 0) {
    dst[0] = '\0';
  } else if (static_cast<size_t>(r) >= n) {
    utf8TrimTail(dst);
  }
}

}  // namespace form_detail

}  // namespace sbx
