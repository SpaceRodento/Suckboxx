/*=====================================================================
  test_utf8_latin1 - UTF-8 -> Latin-1 -muunnos (utf8_latin1.h)

  Miksi tama suite on olemassa: skandien saaminen e-inkille on kaksi eri
  ongelmaa, ja fonttien laajentaminen 0x20-0xFF:aan ratkaisee vain
  glyyfipuolen. Ilman TATA muunnosta "ä" (UTF-8: 0xC3 0xA4) piirtyisi
  kahtena glyyfina -> "Ã¤", ja fontit nayttaisivat rikkinaisilta vaikka
  ne olisivat tayysin kunnossa. Suite lukitsee sen ettei nain kay.
=====================================================================*/

#include <unity.h>
#include <string.h>

#include "utf8_latin1.h"

static char buf[128];

void setUp(void) { memset(buf, 0xAA, sizeof(buf)); }
void tearDown(void) {}

// ── Perustapaukset ───────────────────────────────────────────────────

void test_ascii_passes_through_unchanged(void) {
  size_t n = utf8ToLatin1("Liota kuutiot", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Liota kuutiot", buf);
  TEST_ASSERT_EQUAL_size_t(13, n);
}

// Suiten ydin: juuri tama on se joka epaonnistuisi ilman muunnosta.
void test_scandinavian_letters_become_single_latin1_bytes(void) {
  // "äöå ÄÖÅ" UTF-8:na -> jokainen kirjain kaksi tavua.
  utf8ToLatin1("\xC3\xA4\xC3\xB6\xC3\xA5 \xC3\x84\xC3\x96\xC3\x85", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT8(0xE4, (uint8_t)buf[0]);  // ä
  TEST_ASSERT_EQUAL_UINT8(0xF6, (uint8_t)buf[1]);  // ö
  TEST_ASSERT_EQUAL_UINT8(0xE5, (uint8_t)buf[2]);  // å
  TEST_ASSERT_EQUAL_UINT8(0x20, (uint8_t)buf[3]);  // valilyonti
  TEST_ASSERT_EQUAL_UINT8(0xC4, (uint8_t)buf[4]);  // Ä
  TEST_ASSERT_EQUAL_UINT8(0xD6, (uint8_t)buf[5]);  // Ö
  TEST_ASSERT_EQUAL_UINT8(0xC5, (uint8_t)buf[6]);  // Å
  TEST_ASSERT_EQUAL_UINT8(0x00, (uint8_t)buf[7]);
}

// Oikea ohjeteksti grow_steps.h:sta — tama on se jonka kayttaja lukee.
void test_real_guidance_text_converts_to_readable_finnish(void) {
  utf8ToLatin1("S\xC3\xA4\xC3\xA4" "d\xC3\xA4 vesi", buf, sizeof(buf));
  // "Säädä vesi" -> S ä ä d ä ' ' v e s i
  TEST_ASSERT_EQUAL_UINT8('S',  (uint8_t)buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0xE4, (uint8_t)buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0xE4, (uint8_t)buf[2]);
  TEST_ASSERT_EQUAL_UINT8('d',  (uint8_t)buf[3]);
  TEST_ASSERT_EQUAL_UINT8(0xE4, (uint8_t)buf[4]);
  TEST_ASSERT_EQUAL_STRING_LEN(" vesi", buf + 5, 5);
}

// Aste-merkki on Latin-1:ssa (0xB0) ja fontissa -> "20-25 °C" toimii.
void test_degree_sign_survives(void) {
  utf8ToLatin1("20-25 \xC2\xB0" "C", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT8(0xB0, (uint8_t)buf[6]);
  TEST_ASSERT_EQUAL_UINT8('C',  (uint8_t)buf[7]);
}

// ── Latin-1:sta puuttuvat merkit ─────────────────────────────────────

// En-dash EI ole Latin-1:ssa. Hiljainen katoaminen keskella lausetta on
// pahempi kuin '-', joten se korvataan eika pudoteta.
void test_en_dash_falls_back_to_ascii_hyphen(void) {
  utf8ToLatin1("5\xE2\x80\x93" "10 vrk", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("5-10 vrk", buf);
}

void test_typographic_quotes_fall_back_to_ascii(void) {
  utf8ToLatin1("\xE2\x80\x9C" "hei" "\xE2\x80\x9D", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("\"hei\"", buf);
}

// Sitova valilyonti (U+00A0) ON Latin-1:ssa -> menee lapi 0xA0:na, EI muunnu
// tavalliseksi valilyonniksi. Se on oikein: fontissa 0xA0:lla on identtinen
// metriikka valilyonnin kanssa (1x1, xAdvance 7) joten se piirtyy
// valilyontina, mutta rivitys ei katkaise siina. Juuri sita se tarkoittaa.
void test_non_breaking_space_stays_latin1_not_plain_space(void) {
  utf8ToLatin1("a\xC2\xA0" "b", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT8('a',  (uint8_t)buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0xA0, (uint8_t)buf[1]);
  TEST_ASSERT_EQUAL_UINT8('b',  (uint8_t)buf[2]);
  TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[3]);
}

// Merkki jolle ei ole vastinetta jatetaan pois — mutta muu teksti sailyy.
void test_unmappable_char_is_dropped_without_eating_the_rest(void) {
  utf8ToLatin1("a\xE2\x98\x85" "b", buf, sizeof(buf));   // U+2605 BLACK STAR
  TEST_ASSERT_EQUAL_STRING("ab", buf);
}

// 4-tavuinen (emoji) ei mahdu fonttiin; se ei saa sotkea loppua.
void test_four_byte_emoji_is_dropped_cleanly(void) {
  utf8ToLatin1("a\xF0\x9F\x8C\xB1" "b", buf, sizeof(buf));  // U+1F331 seedling
  TEST_ASSERT_EQUAL_STRING("ab", buf);
}

// ── Turva: puskurit ja roskadata ─────────────────────────────────────

void test_output_is_always_terminated_and_never_overflows(void) {
  char small[5];
  memset(small, 0xAA, sizeof(small));
  size_t n = utf8ToLatin1("aaaaaaaaaa", small, sizeof(small));
  TEST_ASSERT_EQUAL_size_t(4, n);
  TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)small[4]);
  TEST_ASSERT_EQUAL_STRING("aaaa", small);
}

// Monitavuinen merkki ei saa jaada puoliksi puskurin rajalle.
void test_multibyte_char_never_truncated_mid_sequence(void) {
  char small[3];
  utf8ToLatin1("\xC3\xA4\xC3\xB6", small, sizeof(small));   // "äö" -> tilaa 2:lle
  TEST_ASSERT_EQUAL_UINT8(0xE4, (uint8_t)small[0]);
  TEST_ASSERT_EQUAL_UINT8(0xF6, (uint8_t)small[1]);
  TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)small[2]);
}

// Katkennut UTF-8 (esim. leikattu HTTP-vaste) ei saa kaataa eika loopata.
void test_truncated_utf8_does_not_hang_or_crash(void) {
  utf8ToLatin1("ab\xC3", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("ab", buf);
}

void test_lone_continuation_byte_is_skipped(void) {
  utf8ToLatin1("a\xA4" "b", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("ab", buf);
}

void test_null_input_yields_empty_string(void) {
  utf8ToLatin1(NULL, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_zero_size_output_does_not_write(void) {
  char c = 0x7F;
  TEST_ASSERT_EQUAL_size_t(0, utf8ToLatin1("abc", &c, 0));
  TEST_ASSERT_EQUAL_UINT8(0x7F, (uint8_t)c);   // koskematon
}

// Muunnos ei koskaan kasvata pituutta -> sama puskuri sisaan ja ulos on ok.
// Tata kaytetaan data_fetch_parse.h:ssa, joten sopimus on lukittava.
void test_in_place_conversion_is_safe(void) {
  char s[32];
  strcpy(s, "S\xC3\xA4\xC3\xA4" "d\xC3\xA4");
  utf8ToLatin1(s, s, sizeof(s));
  TEST_ASSERT_EQUAL_UINT8('S',  (uint8_t)s[0]);
  TEST_ASSERT_EQUAL_UINT8(0xE4, (uint8_t)s[1]);
  TEST_ASSERT_EQUAL_UINT8(0xE4, (uint8_t)s[2]);
  TEST_ASSERT_EQUAL_UINT8('d',  (uint8_t)s[3]);
  TEST_ASSERT_EQUAL_UINT8(0xE4, (uint8_t)s[4]);
  TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)s[5]);
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_ascii_passes_through_unchanged);
  RUN_TEST(test_scandinavian_letters_become_single_latin1_bytes);
  RUN_TEST(test_real_guidance_text_converts_to_readable_finnish);
  RUN_TEST(test_degree_sign_survives);

  RUN_TEST(test_en_dash_falls_back_to_ascii_hyphen);
  RUN_TEST(test_typographic_quotes_fall_back_to_ascii);
  RUN_TEST(test_non_breaking_space_stays_latin1_not_plain_space);
  RUN_TEST(test_unmappable_char_is_dropped_without_eating_the_rest);
  RUN_TEST(test_four_byte_emoji_is_dropped_cleanly);

  RUN_TEST(test_output_is_always_terminated_and_never_overflows);
  RUN_TEST(test_multibyte_char_never_truncated_mid_sequence);
  RUN_TEST(test_truncated_utf8_does_not_hang_or_crash);
  RUN_TEST(test_lone_continuation_byte_is_skipped);
  RUN_TEST(test_null_input_yields_empty_string);
  RUN_TEST(test_zero_size_output_does_not_write);
  RUN_TEST(test_in_place_conversion_is_safe);

  return UNITY_END();
}
