/*=====================================================================
  test_text_wrap - Latin-1-rivitys (text_wrap.h)

  Mittarina vakioleveys 10 px/merkki -> leveydet ovat merkkimaaria ja
  odotukset voi laskea paassa. Suite lukitsee kolme ominaisuutta joiden
  hajoaminen olisi ruma nahda vasta paneelilla:
    1. "\n\n" tuottaa kappalevalin (tyhja rivi sailyy)
    2. rivia pidempi sana katkaistaan kovasti EIKA jaa ikuiseen silmukkaan
    3. rivit ovat indekseja alkuperaiseen tekstiin (Latin-1-tavut, ei kopioita)
=====================================================================*/

#include <unity.h>
#include <string.h>

#include "text_wrap.h"

// 10 px per merkki — leveydet luetaan suoraan merkkimaarina.
static int16_t measure10(const char* /*s*/, int len, void* /*ctx*/) {
  return (int16_t)(len * 10);
}

static TextLine g_lines[32];

void setUp(void)    { memset(g_lines, 0, sizeof(g_lines)); }
void tearDown(void) {}

// Apuri: kopioi rivi nollaterminoituna vertailua varten.
static const char* lineStr(const char* text, const TextLine& L) {
  static char buf[128];
  memcpy(buf, text + L.start, L.len);
  buf[L.len] = '\0';
  return buf;
}

// ── Perusrivitys ─────────────────────────────────────────────────────

void test_short_text_is_single_line(void) {
  const char* t = "Liota kuutiot";
  int n = text_wrap(t, 200, measure10, NULL, g_lines, 32);   // 20 merkkia/rivi
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("Liota kuutiot", lineStr(t, g_lines[0]));
}

void test_wraps_at_word_boundary(void) {
  const char* t = "yksi kaksi kolme nelja";
  int n = text_wrap(t, 100, measure10, NULL, g_lines, 32);   // 10 merkkia/rivi
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL_STRING("yksi kaksi", lineStr(t, g_lines[0]));
  TEST_ASSERT_EQUAL_STRING("kolme",      lineStr(t, g_lines[1]));
  TEST_ASSERT_EQUAL_STRING("nelja",      lineStr(t, g_lines[2]));
}

void test_empty_and_null_are_safe(void) {
  TEST_ASSERT_EQUAL_INT(0, text_wrap("",   100, measure10, NULL, g_lines, 32));
  TEST_ASSERT_EQUAL_INT(0, text_wrap(NULL, 100, measure10, NULL, g_lines, 32));
  TEST_ASSERT_EQUAL_INT(0, text_wrap("x",  0,   measure10, NULL, g_lines, 32));
}

// ── Kovat rivinvaihdot ja kappalevali ────────────────────────────────

void test_single_newline_breaks_line_without_empty_line(void) {
  const char* t = "abc\ndef";
  int n = text_wrap(t, 200, measure10, NULL, g_lines, 32);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_STRING("abc", lineStr(t, g_lines[0]));
  TEST_ASSERT_EQUAL_STRING("def", lineStr(t, g_lines[1]));
}

void test_double_newline_preserves_paragraph_gap(void) {
  // PM:n askeltekstit erottavat kappaleet "\n\n":lla — tyhjan rivin on
  // sailyttava tai pitka ohje muuttuu yhdeksi mossoksi.
  const char* t = "kappale yksi\n\nkappale kaksi";
  int n = text_wrap(t, 200, measure10, NULL, g_lines, 32);
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL_STRING("kappale yksi",  lineStr(t, g_lines[0]));
  TEST_ASSERT_EQUAL_UINT16(0, g_lines[1].len);   // tyhja rivi
  TEST_ASSERT_EQUAL_STRING("kappale kaksi", lineStr(t, g_lines[2]));
}

// ── Ylipitka sana: kova katkaisu, ei ikuista silmukkaa ───────────────

void test_overlong_word_is_hard_cut(void) {
  const char* t = "abcdefghijklmnop";                        // 16 merkkia
  int n = text_wrap(t, 100, measure10, NULL, g_lines, 32);   // 10/rivi
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_STRING("abcdefghij", lineStr(t, g_lines[0]));
  TEST_ASSERT_EQUAL_STRING("klmnop",     lineStr(t, g_lines[1]));
}

void test_width_below_one_char_still_terminates(void) {
  // Patologinen leveys: yksikin merkki ei "mahdu" — silti vahintaan yksi
  // merkki per rivi, eika silmukka jaa ikuiseksi.
  const char* t = "abc";
  int n = text_wrap(t, 5, measure10, NULL, g_lines, 32);
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL_STRING("a", lineStr(t, g_lines[0]));
  TEST_ASSERT_EQUAL_STRING("c", lineStr(t, g_lines[2]));
}

// ── maxLines-ylitys: paluuarvo kertoo todellisen tarpeen ─────────────

void test_line_count_exceeding_max_is_reported(void) {
  const char* t = "yksi kaksi kolme nelja viisi kuusi";
  int n = text_wrap(t, 60, measure10, NULL, g_lines, 3);     // 6 merkkia/rivi
  TEST_ASSERT_GREATER_THAN_INT(3, n);   // kutsuja: "ei mahdu -> pienempi fontti"
  // Vain 3 ensimmaista on kirjoitettu taulukkoon:
  TEST_ASSERT_EQUAL_STRING("yksi",  lineStr(t, g_lines[0]));
  TEST_ASSERT_EQUAL_STRING("kaksi", lineStr(t, g_lines[1]));
  TEST_ASSERT_EQUAL_STRING("kolme", lineStr(t, g_lines[2]));
}

// ── Latin-1-tavut kulkevat lapi koskemattomina ───────────────────────

void test_latin1_bytes_measure_as_single_chars(void) {
  // "Sää" Latin-1:na: S=0x53, ä=0xE4 — 3 tavua = 3 merkkia.
  const char t[] = { 'S', (char)0xE4, (char)0xE4, ' ', 'o', 'n', '\0' };
  int n = text_wrap(t, 60, measure10, NULL, g_lines, 32);    // 6 merkkia/rivi
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_UINT16(6, g_lines[0].len);
}

// ── Todellinen askelteksti mahtuu laatikkoon 9pt:lla ─────────────────

void test_realistic_step_text_fits_box_budget(void) {
  // Kylvoteksti (grow_steps.h TXT_STEP_SOW -mittaluokka, ~250 merkkia).
  // 9pt: ~38 merkkia/rivi, laatikossa tilaa ~10 riville. Talla mittarilla
  // 380 px / 10 px = 38 merkkia/rivi — todellinen budjetti-savutesti.
  const char* t =
      "Valuta ylimaarainen vesi pois. Kuution pitaa olla kostea, ei lilluva - "
      "liika vesi tukahduttaa idun hapenpuutteeseen. Ala purista kuutiota, "
      "se rikkoo ilmarakenteen.\n\n"
      "Pudota sitten 1-2 siementa kuution reikaan, noin 0,5 cm syvyyteen, "
      "ja peita kevyesti kivivillan murulla.";
  int n = text_wrap(t, 380, measure10, NULL, g_lines, 12);
  TEST_ASSERT_GREATER_THAN_INT(4, n);
  TEST_ASSERT_LESS_OR_EQUAL_INT(10, n);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_short_text_is_single_line);
  RUN_TEST(test_wraps_at_word_boundary);
  RUN_TEST(test_empty_and_null_are_safe);
  RUN_TEST(test_single_newline_breaks_line_without_empty_line);
  RUN_TEST(test_double_newline_preserves_paragraph_gap);
  RUN_TEST(test_overlong_word_is_hard_cut);
  RUN_TEST(test_width_below_one_char_still_terminates);
  RUN_TEST(test_line_count_exceeding_max_is_reported);
  RUN_TEST(test_latin1_bytes_measure_as_single_chars);
  RUN_TEST(test_realistic_step_text_fits_box_budget);
  return UNITY_END();
}
