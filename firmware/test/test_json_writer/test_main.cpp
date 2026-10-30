// JsonWriter (docs/firmware.md §4.6): pilkut, escapaus, liukuluvut, ylivuoto.
// Snapshot kulkee tämän läpi 15 kertaa sekunnissa (D9), joten rikkinäinen
// JSON = UI jähmettyy hiljaa (JSON.parse heittää). Siksi jokainen tuloste
// ajetaan myös tiukan validaattorin läpi.

#include <unity.h>

#include <cfloat>
#include <climits>
#include <clocale>
#include <cstdint>
#include <cstring>
#include <limits>

#include "core/json_writer.h"
#include "json_validate.h"

using sbx::JsonWriter;

void setUp() {}
void tearDown() {}

// --- rakenne ja pilkut -----------------------------------------------------

void test_empty_containers() {
  char buf[32];
  JsonWriter w(buf, sizeof(buf));
  w.beginObject().endObject();
  TEST_ASSERT_EQUAL_STRING("{}", buf);
  TEST_ASSERT_TRUE(w.ok());

  w.reset();
  w.beginArray().beginArray().endArray().beginObject().endObject().endArray();
  TEST_ASSERT_EQUAL_STRING("[[],{}]", buf);
  TEST_ASSERT_TRUE(w.ok());
  TEST_ASSERT_EQUAL_size_t(std::strlen(buf), w.length());
}

void test_commas_and_nesting_snapshot_shape() {
  char buf[256];
  JsonWriter w(buf, sizeof(buf));
  w.beginObject();
  w.key("v").value(1);
  w.key("state").value("near");
  w.key("rpm").null();
  w.key("flags").beginObject().key("sat").value(false).key("drift").value(true).endObject();
  w.key("tol").beginArray().value(1.5f, 1).value(3.0f, 1).endArray();
  w.key("ch").beginArray();
  for (int i = 0; i < 2; ++i) {
    w.beginObject().key("d").value(-0.25f * static_cast<float>(i), 2).key("mv").value(1325 + i).endObject();
  }
  w.endArray();
  w.endObject();
  TEST_ASSERT_TRUE(w.ok());
  TEST_ASSERT_FALSE(w.overflow());
  TEST_ASSERT_EQUAL_STRING(
      "{\"v\":1,\"state\":\"near\",\"rpm\":null,\"flags\":{\"sat\":false,\"drift\":true},"
      "\"tol\":[1.5,3.0],\"ch\":[{\"d\":0.00,\"mv\":1325},{\"d\":-0.25,\"mv\":1326}]}",
      buf);
  TEST_ASSERT_TRUE(jsoncheck::valid(buf));
}

// --- merkkijonot -----------------------------------------------------------

void test_string_escaping() {
  char buf[128];
  JsonWriter w(buf, sizeof(buf));
  w.value("a\"b\\c\nd\te\x01\x1f/");
  // " ja \ kenoviivalla, ohjausmerkit \u00XX, '/' ei tarvitse escapea.
  TEST_ASSERT_EQUAL_STRING("\"a\\\"b\\\\c\\u000ad\\u0009e\\u0001\\u001f/\"", buf);
  TEST_ASSERT_TRUE(jsoncheck::valid(buf));
}

void test_every_control_char_escaped() {
  static const char kHex[] = "0123456789abcdef";
  for (int c = 1; c < 0x20; ++c) {
    char in[2] = {static_cast<char>(c), '\0'};
    char buf[16];
    JsonWriter w(buf, sizeof(buf));
    w.value(in);
    char expect[16] = {'"', '\\', 'u', '0', '0', kHex[c >> 4], kHex[c & 15], '"', '\0'};
    TEST_ASSERT_EQUAL_STRING(expect, buf);
    TEST_ASSERT_TRUE(jsoncheck::valid(buf));
  }
}

void test_utf8_passes_through() {
  // Status-tekstit ovat UTF-8-suomea, ja "–" (U+2013) on kolmitavuinen.
  const char* fi = "SYL 3: avaa läppää – imee 2,1 kPa enemmän";
  char buf[128];
  JsonWriter w(buf, sizeof(buf));
  w.beginObject().key("advice").value(fi).endObject();
  char expect[128];
  std::snprintf(expect, sizeof(expect), "{\"advice\":\"%s\"}", fi);
  TEST_ASSERT_EQUAL_STRING(expect, buf);
  TEST_ASSERT_TRUE(jsoncheck::valid(buf));
  // DEL (0x7f) ja tavu 0xff eivät ole ohjausmerkkejä JSONin mielessä.
  w.reset();
  w.value("\x7f\xff");
  TEST_ASSERT_EQUAL_STRING("\"\x7f\xff\"", buf);
}

void test_null_string_and_key_escaping() {
  char buf[64];
  JsonWriter w(buf, sizeof(buf));
  w.beginObject().key("q\"k").value(static_cast<const char*>(nullptr)).endObject();
  TEST_ASSERT_EQUAL_STRING("{\"q\\\"k\":null}", buf);
  TEST_ASSERT_TRUE(w.ok());
}

// --- luvut -----------------------------------------------------------------

void test_float_fixed_decimals() {
  char buf[128];
  JsonWriter w(buf, sizeof(buf));
  w.beginArray()
      .value(-55.123f, 2)
      .value(851.24f, 1)
      .value(2.6f, 0)
      .value(0.011f, 3)
      .value(1325.0f, 0)
      .value(-100.0f, 3)
      .endArray();
  TEST_ASSERT_EQUAL_STRING("[-55.12,851.2,3,0.011,1325,-100.000]", buf);
  TEST_ASSERT_TRUE(jsoncheck::valid(buf));
}

void test_nonfinite_becomes_null() {
  char buf[64];
  JsonWriter w(buf, sizeof(buf));
  const float inf = std::numeric_limits<float>::infinity();
  w.beginArray()
      .value(std::numeric_limits<float>::quiet_NaN(), 2)
      .value(inf, 1)
      .value(-inf, 1)
      .valueTrim(std::numeric_limits<float>::quiet_NaN(), 3)
      .endArray();
  TEST_ASSERT_EQUAL_STRING("[null,null,null,null]", buf);
  TEST_ASSERT_TRUE(w.ok());
}

void test_negative_zero_normalized() {
  // Pieni negatiivinen poikkeama (esim. vac_delta -0.004 kPa) ei saa näkyä "-0.00":na.
  char buf[64];
  JsonWriter w(buf, sizeof(buf));
  w.beginArray().value(-0.004f, 2).value(-0.0f, 1).value(-0.4f, 0).value(-0.6f, 0).endArray();
  TEST_ASSERT_EQUAL_STRING("[0.00,0.0,0,-1]", buf);
}

void test_decimals_clamped() {
  char out[64];
  // Negatiivinen -> 0, yli 9 -> 9 (floatin tarkkuuden raja).
  TEST_ASSERT_EQUAL_size_t(1, sbx::formatFixed(out, sizeof(out), 3.14159f, -2));
  TEST_ASSERT_EQUAL_STRING("3", out);
  const size_t n = sbx::formatFixed(out, sizeof(out), 1.0f, 20);
  TEST_ASSERT_EQUAL_STRING("1.000000000", out);
  TEST_ASSERT_EQUAL_size_t(11, n);
}

void test_value_trim() {
  char buf[128];
  JsonWriter w(buf, sizeof(buf));
  w.beginArray()
      .valueTrim(2700.0f, 2)
      .valueTrim(0.15f, 5)
      .valueTrim(1.5f, 4)
      .valueTrim(10.0f, 3)    // kokonaisosan nollat säilyvät
      .valueTrim(100.0f, 0)
      .valueTrim(-0.00001f, 4)
      .valueTrim(-2.50f, 3)
      .endArray();
  TEST_ASSERT_EQUAL_STRING("[2700,0.15,1.5,10,100,0,-2.5]", buf);
  TEST_ASSERT_TRUE(jsoncheck::valid(buf));
}

void test_float_extremes_fit() {
  // Pahin äärellinen float + 9 desimaalia mahtuu kirjoittimen 64 merkin apupuskuriin.
  char out[64];
  const size_t n = sbx::formatFixed(out, sizeof(out), -FLT_MAX, 9);
  TEST_ASSERT_GREATER_THAN(40, n);
  TEST_ASSERT_EQUAL_size_t(std::strlen(out), n);
  char buf[128];
  JsonWriter w(buf, sizeof(buf));
  w.value(-FLT_MAX, 9);
  TEST_ASSERT_TRUE(w.ok());
  TEST_ASSERT_TRUE(jsoncheck::valid(buf));
  // Liian pieni kohde: 0 eikä yli kirjoitusta.
  // Osoitin ja koko volatile-muuttujien kautta: -O2/-O3:lla GCC muuten
  // inlinaa kutsun ja varoittaa (-Wformat-truncation -> -Werror) juuri siitä
  // katkaisusta, jota tässä tahallaan testataan.
  char small[4] = {'x', 'x', 'x', 'x'};
  char* volatile small_p = small;
  volatile size_t small_n = 3;
  const size_t r = sbx::formatFixed(small_p, small_n, 123.5f, 1);
  TEST_ASSERT_EQUAL_size_t(0, r);
  TEST_ASSERT_EQUAL_INT('\0', small[0]);
  TEST_ASSERT_EQUAL_INT('x', small[3]);
}

void test_integers_all_types() {
  char buf[256];
  JsonWriter w(buf, sizeof(buf));
  w.beginArray()
      .value(INT_MIN)
      .value(static_cast<uint32_t>(UINT32_MAX))
      .value(static_cast<int64_t>(INT64_MIN))
      .value(static_cast<uint64_t>(UINT64_MAX))
      .value(static_cast<uint8_t>(255))
      .value(static_cast<int8_t>(-128))
      .value(static_cast<uint16_t>(65535))
      .value(static_cast<size_t>(8192))
      .value(-1L)
      .endArray();
  TEST_ASSERT_EQUAL_STRING(
      "[-2147483648,4294967295,-9223372036854775808,18446744073709551615,255,-128,65535,8192,-1]",
      buf);
  TEST_ASSERT_TRUE(jsoncheck::valid(buf));
}

// --- ylivuoto --------------------------------------------------------------

static void writeReferenceDoc(JsonWriter& w) {
  w.beginObject();
  w.key("head").value("Lähellä");
  w.key("esc").value("a\"\n");
  w.key("n").value(-55.125f, 3);
  w.key("arr").beginArray().value(1).value(true).null().value(12345.5f, 1).endArray();
  w.key("o").beginObject().key("x").value(0u).endObject();
  w.endObject();
}

void test_overflow_every_capacity() {
  char ref[256];
  JsonWriter rw(ref, sizeof(ref));
  writeReferenceDoc(rw);
  TEST_ASSERT_TRUE(rw.ok());
  const size_t full = rw.length();
  TEST_ASSERT_TRUE(jsoncheck::valid(ref));

  const size_t kGuard = 16;
  for (size_t cap = 0; cap <= full + 2; ++cap) {
    char mem[512];
    std::memset(mem, 0xA5, sizeof(mem));
    JsonWriter w(mem, cap);
    writeReferenceDoc(w);
    // Koskaan ei kirjoiteta kapasiteetin ohi.
    for (size_t g = cap; g < cap + kGuard; ++g) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(0xA5, static_cast<unsigned char>(mem[g]), "kirjoitti kapasiteetin ohi");
    }
    if (cap > full) {
      TEST_ASSERT_FALSE(w.overflow());
      TEST_ASSERT_TRUE(w.ok());
      // strcmp eikä EQUAL_STRING: shimin 512 tavun viestipuskuri laukaisee
      // -Wformat-truncation, kun kääntäjä näkee näin ison taulukon.
      TEST_ASSERT_TRUE_MESSAGE(std::strcmp(ref, mem) == 0, "ehjä tuloste kun mahtuu");
    } else {
      TEST_ASSERT_TRUE_MESSAGE(w.overflow(), "puskuri liian pieni -> overflow");
      TEST_ASSERT_FALSE(w.ok());
      if (cap > 0) {
        // Aina NUL-päätetty ja eheä etuliite koko tulosteesta.
        const size_t len = std::strlen(mem);
        TEST_ASSERT_LESS_THAN(cap, len);
        TEST_ASSERT_EQUAL_size_t(w.length(), len);
        TEST_ASSERT_EQUAL_INT(0, std::strncmp(ref, mem, len));
      }
    }
  }
}

void test_overflow_is_sticky() {
  char buf[8];
  JsonWriter w(buf, sizeof(buf));
  w.beginArray().value("pitka merkkijono");
  TEST_ASSERT_TRUE(w.overflow());
  const size_t len = w.length();
  // Myöhempi lyhyt kirjoitus ei saa ilmestyä rikkinäisen keskelle.
  w.value(1).endArray();
  TEST_ASSERT_TRUE(w.overflow());
  TEST_ASSERT_EQUAL_size_t(len, w.length());
  TEST_ASSERT_EQUAL_STRING("[\"pitka", buf);  // merkkijonosta mahtui alku, 7 + NUL
  // Luku on atominen: joko koko luku tai ei mitään.
  char nb[6];
  JsonWriter wn(nb, sizeof(nb));
  wn.beginArray().value(123456);
  TEST_ASSERT_TRUE(wn.overflow());
  TEST_ASSERT_EQUAL_STRING("[", nb);
}

void test_null_buffer_never_crashes() {
  JsonWriter w(nullptr, 100);  // kapasiteetti ohitetaan kun puskuria ei ole
  w.beginObject().key("a").value(1).endObject();
  TEST_ASSERT_TRUE(w.overflow());
  TEST_ASSERT_FALSE(w.ok());
  TEST_ASSERT_EQUAL_size_t(0, w.length());
  TEST_ASSERT_EQUAL_STRING("", w.c_str());
}

// --- väärinkäyttö ----------------------------------------------------------

void test_misuse_detected() {
  char buf[64];
  {
    JsonWriter w(buf, sizeof(buf));
    w.endObject();  // juuritasolla ei ole suljettavaa
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    JsonWriter w(buf, sizeof(buf));
    w.beginObject().value(1).endObject();  // arvo objektissa ilman avainta
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    JsonWriter w(buf, sizeof(buf));
    w.beginArray().key("k").value(1).endArray();  // avain taulukossa
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    JsonWriter w(buf, sizeof(buf));
    w.beginObject().endArray();  // väärä sulkija
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    JsonWriter w(buf, sizeof(buf));
    w.beginObject().key("k").endObject();  // avain ilman arvoa
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    JsonWriter w(buf, sizeof(buf));
    w.beginObject().key("a").key("b").value(1).endObject();  // kaksi avainta peräkkäin
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    JsonWriter w(buf, sizeof(buf));
    w.beginObject();  // sulkematta
    TEST_ASSERT_FALSE(w.ok());
    TEST_ASSERT_FALSE(w.overflow());
  }
  {
    JsonWriter w(buf, sizeof(buf));
    w.value(1).value(2);  // kaksi juuriarvoa
    TEST_ASSERT_FALSE(w.ok());
  }
  {
    JsonWriter w(buf, sizeof(buf));
    TEST_ASSERT_FALSE(w.ok());  // tyhjä tuloste ei ole JSONia
  }
}

void test_max_depth() {
  char buf[256];
  JsonWriter w(buf, sizeof(buf));
  for (int i = 0; i < JsonWriter::kMaxDepth; ++i) w.beginArray();
  for (int i = 0; i < JsonWriter::kMaxDepth; ++i) w.endArray();
  TEST_ASSERT_TRUE(w.ok());
  TEST_ASSERT_TRUE(jsoncheck::valid(buf));
  TEST_ASSERT_EQUAL_size_t(2u * JsonWriter::kMaxDepth, w.length());

  w.reset();
  for (int i = 0; i < JsonWriter::kMaxDepth + 3; ++i) w.beginArray();  // ei bittisiirtoa yli 31:n (UBSan)
  TEST_ASSERT_FALSE(w.ok());
  TEST_ASSERT_EQUAL_size_t(static_cast<size_t>(JsonWriter::kMaxDepth), w.length());
}

void test_reset_reuses_buffer() {
  char buf[32];
  JsonWriter w(buf, sizeof(buf));
  w.beginObject().key("a").value(1).endObject();
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", buf);
  w.reset();
  TEST_ASSERT_EQUAL_STRING("", buf);
  TEST_ASSERT_EQUAL_size_t(0, w.length());
  w.beginArray().value(2).endArray();
  TEST_ASSERT_EQUAL_STRING("[2]", buf);
  TEST_ASSERT_TRUE(w.ok());
}

void test_decimal_point_independent_of_locale() {
  // ESP32:n newlibissä locale on aina "C". PC:llä kokeillaan pilkkulokaalia,
  // jos järjestelmässä sellainen on; muuten testi kertoo ohittavansa.
  const char* candidates[] = {"fi_FI.UTF-8", "fi_FI.utf8", "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8"};
  const char* got = nullptr;
  for (const char* loc : candidates) {
    if (std::setlocale(LC_NUMERIC, loc) != nullptr) {
      got = loc;
      break;
    }
  }
  if (got == nullptr) {
    TEST_MESSAGE("ei pilkkulokaalia asennettuna - ohitetaan lokaalitarkistus");
  } else {
    char buf[32];
    JsonWriter w(buf, sizeof(buf));
    w.beginArray().value(1.5f, 2).valueTrim(-2.25f, 3).endArray();
    std::setlocale(LC_NUMERIC, "C");
    TEST_ASSERT_EQUAL_STRING("[1.50,-2.25]", buf);
  }
  std::setlocale(LC_NUMERIC, "C");
}

// Validaattorin oma järkevyys: se hylkää rikkinäisen JSONin, joten
// valid() == true yllä tarkoittaa jotain.
void test_validator_rejects_broken_json() {
  const char* bad[] = {"", "{", "{\"a\":}", "[1,]", "[01]", "\"\x01\"", "nul", "{\"a\" 1}",
                       "[1 2]", "-", "1.", "\"\\x\"", "{}{}", "[-0.00,]"};
  for (const char* b : bad) TEST_ASSERT_FALSE_MESSAGE(jsoncheck::valid(b), b);
  const char* good[] = {"{}", "[]", "0", "-0.5e-3", "\"\\u00e4\"", "{\"a\":[1,{\"b\":null}]}"};
  for (const char* g : good) TEST_ASSERT_TRUE_MESSAGE(jsoncheck::valid(g), g);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_containers);
  RUN_TEST(test_commas_and_nesting_snapshot_shape);
  RUN_TEST(test_string_escaping);
  RUN_TEST(test_every_control_char_escaped);
  RUN_TEST(test_utf8_passes_through);
  RUN_TEST(test_null_string_and_key_escaping);
  RUN_TEST(test_float_fixed_decimals);
  RUN_TEST(test_nonfinite_becomes_null);
  RUN_TEST(test_negative_zero_normalized);
  RUN_TEST(test_decimals_clamped);
  RUN_TEST(test_value_trim);
  RUN_TEST(test_float_extremes_fit);
  RUN_TEST(test_integers_all_types);
  RUN_TEST(test_overflow_every_capacity);
  RUN_TEST(test_overflow_is_sticky);
  RUN_TEST(test_null_buffer_never_crashes);
  RUN_TEST(test_misuse_detected);
  RUN_TEST(test_max_depth);
  RUN_TEST(test_reset_reuses_buffer);
  RUN_TEST(test_decimal_point_independent_of_locale);
  RUN_TEST(test_validator_rejects_broken_json);
  return UNITY_END();
}
