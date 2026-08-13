/*=====================================================================
  test_state_filter - /api/state-inkluusiofiltterin eheys (data_fetch_parse.h)

  Vartioi 19.7.2026 loydettya bugia: filter-dokumentti oli <512> ja YLIVUOTI
  (35 slottia ei mahdu), jolloin MYOHEMMAT sensori-avaimet (vpd_valid, co2_valid,
  dli, ppfd_valid, ...) putosivat filtterista -> deserialize pudotti ne ->
  DisplayData-liput false -> KAIKKI mittarikortit "--" (osui COACH + STATUS).
  Palvelin oli koko ajan validi; vika oli pelkka filtterin koko.

  Nama testit olisivat napanneet sen: filtterin ei saa ylivuotaa eika pudottaa
  yhtaan avainta tuotantokoossa (<2048>).
=====================================================================*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unity.h>
#include <ArduinoJson.h>

#include "data_fetch_parse.h"

void setUp() {}
void tearDown() {}

// Tuotantokoko ei saa ylivuotaa (muuten avaimia putoaa hiljaa -> "--").
void test_filter_2048_does_not_overflow() {
  StaticJsonDocument<2048> filter;
  data_buildStateFilter(filter);
  TEST_ASSERT_FALSE_MESSAGE(filter.overflowed(),
    "state-filtteri ylivuotaa <2048>:ssa -> sensoreita putoaa -> naytto '--'. Kasvata data_fetch.h:n kokoa.");
}

// Jokaisen sensori-avaimen on sailyttava filtterissa.
void test_filter_keeps_all_sensor_keys() {
  StaticJsonDocument<2048> filter;
  data_buildStateFilter(filter);
  JsonObject fs = filter["sensors"];
  for (int i = 0; i < kStateSensorKeyCount; i++) {
    char msg[80];
    snprintf(msg, sizeof(msg), "sensori-avain putosi filtterista: %s", kStateSensorKeys[i]);
    TEST_ASSERT_TRUE_MESSAGE(fs.containsKey(kStateSensorKeys[i]), msg);
  }
}

// Regressiovartija: vanha <512> DROPPAA PE-avaimia (dokumentoi juurisyyn).
// Jos joku palauttaa koon 512:een, tama muistuttaa miksi se rikkoo naytn.
void test_old_512_filter_drops_pe_keys() {
  StaticJsonDocument<512> filter;
  data_buildStateFilter(filter);
  JsonObject fs = filter["sensors"];
  bool allPresent = fs.containsKey("vpd_valid") && fs.containsKey("co2_valid") &&
                    fs.containsKey("dli") && fs.containsKey("ppfd_valid");
  TEST_ASSERT_FALSE_MESSAGE(allPresent,
    "512 riittikin — juurisyyn ehto muuttunut, tarkista tuotantokoko uudelleen");
}

// Parse oikeankaltaisen payloadin filtterin lapi -> validiteettiliput sailyvat.
void test_parse_through_filter_keeps_valid_flags() {
  const char* payload =
    "{\"device\":{\"state\":\"IDLE\"},"
    "\"sensors\":{\"air_temp_c\":23.2,\"air_humidity\":47,\"env_valid\":true,"
    "\"vpd_kpa\":1.48,\"vpd_valid\":true,\"co2_ppm\":447,\"co2_valid\":true,"
    "\"ppfd\":1500,\"ppfd_valid\":true,\"dli\":21.6,"
    "\"leaf_temp_c\":0,\"leaf_temp_valid\":false},"
    "\"growing\":{\"active\":true,\"phase_name\":\"Kasvuvaihe\"}}";
  StaticJsonDocument<2048> filter;
  data_buildStateFilter(filter);
  StaticJsonDocument<5120> doc;
  DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  TEST_ASSERT_EQUAL_STRING("Ok", err.c_str());

  DisplayData d = {};
  data_fetch_populateFromJson(doc, &d);
  TEST_ASSERT_TRUE_MESSAGE(d.vpdValid, "vpd_valid putosi filtterin lapi");
  TEST_ASSERT_TRUE_MESSAGE(d.co2Valid, "co2_valid putosi filtterin lapi");
  TEST_ASSERT_TRUE_MESSAGE(d.ppfdValid, "ppfd_valid putosi filtterin lapi");
  TEST_ASSERT_TRUE_MESSAGE(d.envValid, "env_valid putosi filtterin lapi");
  TEST_ASSERT_FALSE(d.leafTempValid);   // MLX rikki -> odotettu false
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.48f, d.vpdKpa);
  TEST_ASSERT_EQUAL_INT(447, d.co2Ppm);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_filter_2048_does_not_overflow);
  RUN_TEST(test_filter_keeps_all_sensor_keys);
  RUN_TEST(test_old_512_filter_drops_pe_keys);
  RUN_TEST(test_parse_through_filter_keeps_valid_flags);
  return UNITY_END();
}
