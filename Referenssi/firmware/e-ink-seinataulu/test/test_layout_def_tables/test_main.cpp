// Integrity checks for the editable layout tables in layout_def.h
// (EINK_CARDS / EINK_CONTEXT / STATUS_CARDS / STATUS_CONTEXT). This file is
// the one growers/devs edit to change what shows on the cards and the context
// line (see docs/ohjeet/eink_operointi.md § 10) — these tests catch a bad
// FieldId, an empty card label, and above all a text literal written in the
// WRONG SCANDIC CONVENTION for its draw path (see below).
//
// <stdint.h> must be included before layout_def.h: in the real firmware
// uint8_t/uint16_t come from the ambient Arduino.h the .ino pulls in first;
// a native test has no .ino wrapper, so it must provide that itself.
#include <stdint.h>
#include <stdio.h>
#include <unity.h>

#include "layout_def.h"
#include "utf8_latin1.h"   // the conversion the STATUS draw path applies

void setUp() {}
void tearDown() {}

// ── FieldId range ────────────────────────────────────────────────────
// Highest valid FieldId is FIELD_ENERGY_WH (last entry in the enum, see
// layout_def.h). FIELD_NONE (0) is the "unset" sentinel — a configured
// card/context row using it is a mistake, not a legitimate field.
// NOTE: when you append a FieldId to the enum, bump this upper bound to the
// new last entry, or a card/context row using it will read as "out of range".
static bool isValidConfiguredField(uint8_t field) {
  return field > FIELD_NONE && field <= FIELD_ENERGY_WH;
}

void test_cards_have_valid_field_ids() {
  for (int i = 0; i < EINK_CARD_COUNT; i++) {
    char msg[64];
    snprintf(msg, sizeof(msg), "EINK_CARDS[%d].field out of range", i);
    TEST_ASSERT_TRUE_MESSAGE(isValidConfiguredField(EINK_CARDS[i].field), msg);
  }
}

void test_context_have_valid_field_ids() {
  for (int i = 0; i < EINK_CONTEXT_COUNT; i++) {
    char msg[64];
    snprintf(msg, sizeof(msg), "EINK_CONTEXT[%d].field out of range", i);
    TEST_ASSERT_TRUE_MESSAGE(isValidConfiguredField(EINK_CONTEXT[i].field), msg);
  }
}

// ── Card labels — always shown as the card's subtitle, must not be empty ──
// Context row labels are a separate matter: layout_def.h documents empty
// label as a valid, intentional choice ("tyhja label = ei etuliitetta" —
// empty label = no prefix), so only a non-NULL pointer is checked there.

void test_card_labels_not_empty() {
  for (int i = 0; i < EINK_CARD_COUNT; i++) {
    char msg[64];
    snprintf(msg, sizeof(msg), "EINK_CARDS[%d].label is empty", i);
    TEST_ASSERT_NOT_NULL(EINK_CARDS[i].label);
    TEST_ASSERT_TRUE_MESSAGE(EINK_CARDS[i].label[0] != '\0', msg);
  }
}

void test_context_labels_not_null() {
  for (int i = 0; i < EINK_CONTEXT_COUNT; i++) {
    TEST_ASSERT_NOT_NULL(EINK_CONTEXT[i].label);
  }
}

// ── ASCII-only labels ────────────────────────────────────────────────
// Not because the font lacks a/o (it has them, 0x20-0xFF, hardware-proven
// 21.7.2026) but because these short labels are drawn on BOTH paths in
// different views, so ASCII is the one form that is always safe. Longer
// prose is where scandics belong.

static bool isAsciiOnly(const char* s) {
  if (!s) return true;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7E) return false;
  }
  return true;
}

void test_card_labels_are_ascii_only() {
  for (int i = 0; i < EINK_CARD_COUNT; i++) {
    char msg[64];
    snprintf(msg, sizeof(msg), "EINK_CARDS[%d].label has a non-ASCII byte (scandic?)", i);
    TEST_ASSERT_TRUE_MESSAGE(isAsciiOnly(EINK_CARDS[i].label), msg);
  }
}

void test_context_labels_are_ascii_only() {
  for (int i = 0; i < EINK_CONTEXT_COUNT; i++) {
    char msg[64];
    snprintf(msg, sizeof(msg), "EINK_CONTEXT[%d].label has a non-ASCII byte (scandic?)", i);
    TEST_ASSERT_TRUE_MESSAGE(isAsciiOnly(EINK_CONTEXT[i].label), msg);
  }
}

// ── Table shape sanity ────────────────────────────────────────────────
// display_layout.h's card loop is `i < EINK_CARD_COUNT && i < 4` (2x2 grid):
// catch an accidental resize here rather than silently leaving grid cells
// blank or truncating extra cards.

void test_card_count_is_four() {
  TEST_ASSERT_EQUAL_INT(4, EINK_CARD_COUNT);
}

void test_context_count_is_positive() {
  TEST_ASSERT_TRUE(EINK_CONTEXT_COUNT > 0);
}

// ═══ STATUS-profiilin taulukot (status_view.h) ═══════════════════════

void test_status_cards_valid_and_four() {
  TEST_ASSERT_EQUAL_INT(4, STATUS_CARD_COUNT);   // STATUS_CARD_W mitoitettu neljalle
  for (int i = 0; i < STATUS_CARD_COUNT; i++) {
    TEST_ASSERT_TRUE(isValidConfiguredField(STATUS_CARDS[i].field));
    TEST_ASSERT_NOT_NULL(STATUS_CARDS[i].label);
    TEST_ASSERT_TRUE(STATUS_CARDS[i].label[0] != '\0');
    TEST_ASSERT_TRUE(isAsciiOnly(STATUS_CARDS[i].label));
  }
}

void test_status_context_valid() {
  TEST_ASSERT_TRUE(STATUS_CONTEXT_COUNT > 0);
  for (int i = 0; i < STATUS_CONTEXT_COUNT; i++) {
    char msg[72];
    snprintf(msg, sizeof(msg), "STATUS_CONTEXT[%d].field out of range", i);
    TEST_ASSERT_TRUE_MESSAGE(isValidConfiguredField(STATUS_CONTEXT[i].field), msg);
    TEST_ASSERT_NOT_NULL(STATUS_CONTEXT[i].label);
    TEST_ASSERT_TRUE(isAsciiOnly(STATUS_CONTEXT[i].label));
  }
}

// unitOverride on lisatty EinkContextSpec:iin JALKIKATEEN, ja koko turvallisuus
// nojaa siihen etta vanhat 2-jaseniset alustukset arvoalustavat sen nullptriksi
// (= "kayta resolveField():n omaa yksikkoa"). Jos tama oletus joskus pettaa,
// USER/COACH-rivin yksikot katoaisivat hiljaa — siksi se testataan.
void test_eink_context_unit_override_defaults_to_null() {
  for (int i = 0; i < EINK_CONTEXT_COUNT; i++) {
    char msg[80];
    snprintf(msg, sizeof(msg), "EINK_CONTEXT[%d].unitOverride ei ole nullptr", i);
    TEST_ASSERT_NULL_MESSAGE(EINK_CONTEXT[i].unitOverride, msg);
  }
}

// ═══ Skandikonventio — kaksi polkua, kaksi kirjoitustapaa ════════════
//
// layout_def.h:ssa on kaksi keskenaan yhteensopimatonta konventiota, ja oikea
// valinta riippuu PIIRTOKUTSUSTA eika osiosta:
//
//   raaka display.print()  -> \xE4-tavu (valmis Latin-1, ei muunnosta)
//   status_u8*() (STATUS)  -> UTF-8 "a" (muunnetaan piirtorajalla)
//
// Vaara valinta ei ole kaannosvirhe eika nay preview'ssa: se on HILJAINEN
// merkin katoaminen laitteella (utf8ToLatin1 tulkitsee yksinaisen \xE4:n
// katkenneeksi 3-tavusekvenssiksi ja pudottaa sen). Tama kaatoi tekstit
// kahdesti — 19.7.2026 (STATUS kirjoitettiin ASCII:ksi) ja 21.7.2026 (vain
// § 2 korjattiin, § 6 jai) — joten konventio testataan mekaanisesti.

// Validi UTF-8? Yksinainen \xE4 EI ole -> tama erottaa konventiot toisistaan.
static bool isValidUtf8(const char* s) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    if (*p < 0x80) { p++; continue; }
    int need;
    if      ((*p & 0xE0) == 0xC0) need = 1;
    else if ((*p & 0xF0) == 0xE0) need = 2;   // 0xE4 osuu tahan -> jatkotavut eivat tasmaa
    else if ((*p & 0xF8) == 0xF0) need = 3;
    else return false;
    for (int i = 1; i <= need; i++) if ((p[i] & 0xC0) != 0x80) return false;
    p += need + 1;
  }
  return true;
}

// 0xC3 = UTF-8:n skandi-lead-tavu. Raakapolulla se piirtyisi "A"-merkkina.
static bool hasUtf8LeadByte(const char* s) {
  for (const unsigned char* p = (const unsigned char*)s; *p; p++)
    if (*p == 0xC3) return true;
  return false;
}

// STATUS-polku (status_u8At/Right/Centered/WrapPrint) -> pakko olla UTF-8.
void test_status_texts_are_valid_utf8() {
  const char* utf8Path[] = {
    TXT_STATUS_ALL_OK, TXT_STATUS_SUB_DEFAULT,
    TXT_STATUS_PROGRESS, TXT_STATUS_PROGRESS_OPEN,
    TXT_STATUS_TARGET_PREF, TXT_STATUS_TARGET_MAXPREF,
  };
  for (int i = 0; i < (int)(sizeof(utf8Path) / sizeof(utf8Path[0])); i++) {
    char msg[120];
    snprintf(msg, sizeof(msg),
             "STATUS-teksti ei ole validia UTF-8:aa (\\xE4-tavu? merkki katoaa piirrossa): %s",
             utf8Path[i]);
    TEST_ASSERT_TRUE_MESSAGE(isValidUtf8(utf8Path[i]), msg);
  }
}

// Positiivinen puoli: skandit ovat OIKEASTI mukana ja muuntuvat Latin-1:ksi.
// Pelkka "on validia UTF-8" menisi lapi myos ASCII-taantumalla ("paiva").
void test_status_progress_carries_scandics_through_conversion() {
  char out[48];
  utf8ToLatin1(TXT_STATUS_PROGRESS, out, sizeof(out));
  bool hasAe = false;
  for (const unsigned char* p = (const unsigned char*)out; *p; p++) if (*p == 0xE4) hasAe = true;
  TEST_ASSERT_TRUE_MESSAGE(hasAe, "TXT_STATUS_PROGRESS: 'paiva' ilman aakkosia?");

  utf8ToLatin1(TXT_STATUS_ALL_OK, out, sizeof(out));
  hasAe = false;
  for (const unsigned char* p = (const unsigned char*)out; *p; p++) if (*p == 0xE4) hasAe = true;
  TEST_ASSERT_TRUE_MESSAGE(hasAe, "TXT_STATUS_ALL_OK: 'toimenpiteita' ilman aakkosia?");
}

// Raakapolku (display.print) -> pakko olla \xE4-tavuina, EI UTF-8:aa.
// TXT_UPTIME_PREFIX on § 2:ssa mutta sita kaytetaan MYOS STATUS-naytolla
// (status_view.h footer) raakana — juuri se ansa jonka takia tama testi on.
void test_raw_path_texts_are_not_utf8() {
  const char* rawPath[] = {
    TXT_UPTIME_PREFIX, TXT_WORDMARK, TXT_WAITING, TXT_SHOWROOM_PLANT,
    TXT_ACT_CIRCULATE, TXT_ACT_FLOOD, TXT_ACT_SOAK, TXT_ACT_RESTING,
    TXT_ACT_GROWING, TXT_PHASE_GERMINATING, TXT_PHASE_ROOTING,
    TXT_ADV_WATER_LOW, TXT_ADV_VPD_HIGH,
    TXT_STEP_TIMER_LEFT,
    TXT_STATUS_TAG_DEVICE, TXT_STATUS_TAG_USER,
  };
  for (int i = 0; i < (int)(sizeof(rawPath) / sizeof(rawPath[0])); i++) {
    char msg[120];
    snprintf(msg, sizeof(msg),
             "Raakapolun teksti on UTF-8:aa (0xC3) - kirjoita \\xE4-tavuina: %s",
             rawPath[i]);
    TEST_ASSERT_FALSE_MESSAGE(hasUtf8LeadByte(rawPath[i]), msg);
  }
}

// Ja etta raakapolulla skandit todella OVAT mukana (ei ASCII-taantumaa).
void test_raw_path_uptime_prefix_has_latin1_scandics() {
  bool hasAe = false;
  for (const unsigned char* p = (const unsigned char*)TXT_UPTIME_PREFIX; *p; p++)
    if (*p == 0xE4) hasAe = true;
  TEST_ASSERT_TRUE_MESSAGE(hasAe, "TXT_UPTIME_PREFIX: 'Kaynnissa' ilman aakkosia?");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cards_have_valid_field_ids);
  RUN_TEST(test_context_have_valid_field_ids);
  RUN_TEST(test_card_labels_not_empty);
  RUN_TEST(test_context_labels_not_null);
  RUN_TEST(test_card_labels_are_ascii_only);
  RUN_TEST(test_context_labels_are_ascii_only);
  RUN_TEST(test_card_count_is_four);
  RUN_TEST(test_context_count_is_positive);
  RUN_TEST(test_status_cards_valid_and_four);
  RUN_TEST(test_status_context_valid);
  RUN_TEST(test_eink_context_unit_override_defaults_to_null);
  RUN_TEST(test_status_texts_are_valid_utf8);
  RUN_TEST(test_status_progress_carries_scandics_through_conversion);
  RUN_TEST(test_raw_path_texts_are_not_utf8);
  RUN_TEST(test_raw_path_uptime_prefix_has_latin1_scandics);
  return UNITY_END();
}
