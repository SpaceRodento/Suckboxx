#include <unity.h>

#include "test/stubs/test_feature_flags.h"
#include "test/stubs/ESPAsyncWebServer.h"

#include <ArduinoJson.h>
#include "api_envelope.h"

void setUp(void) {}
void tearDown(void) {}

// Helper: parse JSON literal into a JsonObjectConst usable by api_updateFloatField
static bool parsePayload(StaticJsonDocument<256>& doc, const char* json, JsonObjectConst& out) {
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;
  out = doc.as<JsonObjectConst>();
  return true;
}

void test_float_field_accepts_int() {
  StaticJsonDocument<256> doc;
  JsonObjectConst payload;
  TEST_ASSERT_TRUE(parsePayload(doc, "{\"x\": 5}", payload));

  AsyncWebServerRequest req;
  float target = 999.0f;

  bool result = api_updateFloatField(&req, payload, "x", 0, 10, &target);

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, target);
  TEST_ASSERT_EQUAL_INT(0, req.send_count);
}

void test_float_field_accepts_float() {
  StaticJsonDocument<256> doc;
  JsonObjectConst payload;
  TEST_ASSERT_TRUE(parsePayload(doc, "{\"x\": 5.5}", payload));

  AsyncWebServerRequest req;
  float target = 999.0f;

  bool result = api_updateFloatField(&req, payload, "x", 0, 10, &target);

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL_FLOAT(5.5f, target);
  TEST_ASSERT_EQUAL_INT(0, req.send_count);
}

void test_float_field_rejects_string() {
  StaticJsonDocument<256> doc;
  JsonObjectConst payload;
  TEST_ASSERT_TRUE(parsePayload(doc, "{\"x\": \"not a number\"}", payload));

  AsyncWebServerRequest req;
  float target = 999.0f;

  bool result = api_updateFloatField(&req, payload, "x", 0, 10, &target);

  TEST_ASSERT_FALSE(result);
  TEST_ASSERT_EQUAL_FLOAT(999.0f, target);
  TEST_ASSERT_EQUAL_INT(400, req.last_status);
  TEST_ASSERT_EQUAL_INT(1, req.send_count);
  TEST_ASSERT_NOT_NULL(strstr(req.last_body, "must be number"));
}

void test_float_field_rejects_below_min() {
  StaticJsonDocument<256> doc;
  JsonObjectConst payload;
  TEST_ASSERT_TRUE(parsePayload(doc, "{\"x\": -1}", payload));

  AsyncWebServerRequest req;
  float target = 999.0f;

  bool result = api_updateFloatField(&req, payload, "x", 0, 10, &target);

  TEST_ASSERT_FALSE(result);
  TEST_ASSERT_EQUAL_INT(400, req.last_status);
  TEST_ASSERT_NOT_NULL(strstr(req.last_body, "must be"));
}

void test_float_field_rejects_above_max() {
  StaticJsonDocument<256> doc;
  JsonObjectConst payload;
  TEST_ASSERT_TRUE(parsePayload(doc, "{\"x\": 11}", payload));

  AsyncWebServerRequest req;
  float target = 999.0f;

  bool result = api_updateFloatField(&req, payload, "x", 0, 10, &target);

  TEST_ASSERT_FALSE(result);
  TEST_ASSERT_EQUAL_INT(400, req.last_status);
  TEST_ASSERT_NOT_NULL(strstr(req.last_body, "must be"));
}

void test_float_field_missing_key_ok() {
  StaticJsonDocument<256> doc;
  JsonObjectConst payload;
  TEST_ASSERT_TRUE(parsePayload(doc, "{\"other\": 5}", payload));

  AsyncWebServerRequest req;
  float target = 999.0f;

  bool result = api_updateFloatField(&req, payload, "x", 0, 10, &target);

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL_FLOAT(999.0f, target);
  TEST_ASSERT_EQUAL_INT(0, req.send_count);
}

void test_float_field_accepts_exact_boundaries() {
  // Test lower boundary
  {
    StaticJsonDocument<256> doc;
    JsonObjectConst payload;
    TEST_ASSERT_TRUE(parsePayload(doc, "{\"x\": 0}", payload));

    AsyncWebServerRequest req;
    float target = 999.0f;

    bool result = api_updateFloatField(&req, payload, "x", 0, 10, &target);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, target);
    TEST_ASSERT_EQUAL_INT(0, req.send_count);
  }

  // Test upper boundary
  {
    StaticJsonDocument<256> doc;
    JsonObjectConst payload;
    TEST_ASSERT_TRUE(parsePayload(doc, "{\"y\": 10}", payload));

    AsyncWebServerRequest req;
    float target = 999.0f;

    bool result = api_updateFloatField(&req, payload, "y", 0, 10, &target);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, target);
    TEST_ASSERT_EQUAL_INT(0, req.send_count);
  }
}

void test_float_field_negative_range() {
  StaticJsonDocument<256> doc;
  JsonObjectConst payload;
  TEST_ASSERT_TRUE(parsePayload(doc, "{\"x\": -50.5}", payload));

  AsyncWebServerRequest req;
  float target = 999.0f;

  bool result = api_updateFloatField(&req, payload, "x", -100, 100, &target);

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL_FLOAT(-50.5f, target);
  TEST_ASSERT_EQUAL_INT(0, req.send_count);
}

// ── Salaisuuksien peitto (api_maskSecret <-> api_isMaskedSentinel) ──
//
// Nama kaksi funktiota muodostavat parin: toinen peittaa salaisuuden
// vastaukseen, toinen tunnistaa saman merkkijonon paluupostissa "ala
// muuta" -pyynnoksi. Paria ei ollut testattu lainkaan, ja ne ehtivat
// erkaantua toisistaan — osittainen maski ("sala*****23") ei tasmannyt
// literaaliin "********", joten laite tallensi oman maskinsa
// salasanakseen ja pudotti itsensa kotiverkosta (16.7.2026).
//
// Tarkein testi alla on test_masked_secret_is_always_recognised: se
// lukitsee nimenomaan sen invariantin joka petti. Yksittaisten
// merkkijonojen tarkistus ei olisi loytanyt vikaa — vika oli SUHTEESSA.

void test_mask_hides_the_secret_entirely() {
  char dst[40];
  api_maskSecret("akuankka", dst, sizeof(dst));

  // Mikaan osa salaisuudesta ei saa nakya: ei alkua, ei loppua.
  TEST_ASSERT_EQUAL_STRING("********", dst);
  TEST_ASSERT_NULL(strstr(dst, "aku"));
  TEST_ASSERT_NULL(strstr(dst, "kka"));
}

void test_mask_is_same_for_different_secrets() {
  char a[40], b[40];
  api_maskSecret("lyhyt123", a, sizeof(a));
  api_maskSecret("huomattavasti-pidempi-salasana-2026", b, sizeof(b));

  // Maski ei saa paljastaa edes salaisuuden pituutta.
  TEST_ASSERT_EQUAL_STRING(a, b);
}

// TAMA on regressiovahti. Jos joku muuttaa api_maskSecret:in muotoa
// muuttamatta api_isMaskedSentinel:ia (tai painvastoin), tama kaatuu.
void test_masked_secret_is_always_recognised() {
  const char* secrets[] = {
    "a", "ab", "abc", "12345678", "akuankka",
    "pitka-salasana-jossa-on-63-merkkia-korkeintaan-ja-erikoismerkkeja!",
    "********-mutta-pidempi",
  };

  for (size_t i = 0; i < sizeof(secrets) / sizeof(secrets[0]); i++) {
    char masked[40];
    api_maskSecret(secrets[i], masked, sizeof(masked));
    TEST_ASSERT_TRUE_MESSAGE(api_isMaskedSentinel(masked),
        "api_maskSecret tuotti arvon jota api_isMaskedSentinel ei tunnista "
        "-> paluuposti tallentaisi maskin salaisuudeksi");
  }
}

void test_empty_secret_masks_to_empty_and_is_not_sentinel() {
  char dst[40];
  api_maskSecret("", dst, sizeof(dst));
  TEST_ASSERT_EQUAL_STRING("", dst);

  // Tyhja EI ole "ala muuta" vaan "ei asetettu" / "tyhjenna" — muuten
  // salasanaa ei voisi koskaan poistaa.
  TEST_ASSERT_FALSE(api_isMaskedSentinel(""));
}

void test_null_is_not_sentinel() {
  TEST_ASSERT_FALSE(api_isMaskedSentinel(NULL));
}

void test_real_secret_is_not_mistaken_for_sentinel() {
  // Aito arvo ei saa menna lapi "ala muuta" -pyyntona, muuten
  // kayttaja ei voisi vaihtaa salasanaansa.
  TEST_ASSERT_FALSE(api_isMaskedSentinel("akuankka"));
  TEST_ASSERT_FALSE(api_isMaskedSentinel("*******"));   // 7 tahtea
  TEST_ASSERT_FALSE(api_isMaskedSentinel("*********")); // 9 tahtea
  TEST_ASSERT_FALSE(api_isMaskedSentinel("aku*****ka"));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_float_field_accepts_int);
  RUN_TEST(test_float_field_accepts_float);
  RUN_TEST(test_float_field_rejects_string);
  RUN_TEST(test_float_field_rejects_below_min);
  RUN_TEST(test_float_field_rejects_above_max);
  RUN_TEST(test_float_field_missing_key_ok);
  RUN_TEST(test_float_field_accepts_exact_boundaries);
  RUN_TEST(test_float_field_negative_range);
  RUN_TEST(test_mask_hides_the_secret_entirely);
  RUN_TEST(test_mask_is_same_for_different_secrets);
  RUN_TEST(test_masked_secret_is_always_recognised);
  RUN_TEST(test_empty_secret_masks_to_empty_and_is_not_sentinel);
  RUN_TEST(test_null_is_not_sentinel);
  RUN_TEST(test_real_secret_is_not_mistaken_for_sentinel);
  return UNITY_END();
}
