// Unit tests for data_fetch_populateFromJson() (data_fetch_parse.h) — the
// /api/state JSON -> DisplayData mapping, tested without any real HTTP
// fetch. Keep the JSON fixtures here in sync with the PlantMeister side of
// the contract (firmware/plantmeister/wifi_portal_api_state.h) when adding
// a field.
#include <string.h>
#include <unity.h>

#include "data_fetch_parse.h"

void setUp() {}
void tearDown() {}

static bool parseInto(const char* json, DisplayData* out) {
  // Matches the 5120-byte capacity data_fetch.h uses for the real HTTP
  // response (see data_fetch.h's deserializeJson call) — plenty of margin
  // for these fixtures.
  StaticJsonDocument<5120> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    TEST_MESSAGE(err.c_str());
    return false;
  }
  data_fetch_populateFromJson(doc, out);
  return true;
}

// ── Full payload — every block present ───────────────────────────────

static const char* FULL_PAYLOAD =
  "{"
  "\"device\":{\"state\":\"IDLE\",\"prev_state\":\"BOOT\",\"fault_msg\":\"\","
             "\"entered_at\":100,\"time_in_state_ms\":5000},"
  "\"ux\":{\"led_color\":\"green\",\"led_pattern\":\"solid\","
         "\"message\":\"Kaikki hyvin\",\"action\":\"-\"},"
  "\"sensors\":{"
    "\"air_temp_c\":22.5,\"air_humidity\":55.0,\"water_temp_c\":21.0,\"tds_ppm\":640,"
    "\"plant_height_mm\":150,\"water_level_ok\":true,\"env_valid\":true,\"height_valid\":true,"
    "\"vpd_kpa\":1.1,\"vpd_valid\":true,\"leaf_temp_c\":23.0,\"leaf_ambient_c\":22.0,"
    "\"leaf_temp_valid\":true,\"ppfd\":300.0,\"ppfd_valid\":true,\"dli\":12.5,"
    "\"co2_ppm\":800,\"co2_valid\":true,\"battery_v\":4.1,\"battery_pct\":87"
  "},"
  "\"motor\":{\"current_mm\":120,\"target_mm\":150,\"moving\":true},"
  "\"actuators\":{\"lights_on\":true,\"air_pump_on\":false,\"pump_running\":true},"
  "\"growing\":{\"active\":true,\"phase\":1,\"elapsed_days\":7,\"plant_id\":\"basil\","
              "\"start_method\":\"seed\",\"plant_name\":\"Persilja\","
              "\"phase_name\":\"Juurtuminen\",\"phase_count\":4,"
              "\"targets\":["
                "{\"field\":\"vpd\",\"lo\":0.4,\"hi\":0.8,\"device_acts\":false},"
                "{\"field\":\"dli\",\"lo\":6.0,\"hi\":10.0,\"device_acts\":false},"
                "{\"field\":\"co2\",\"lo\":350.0,\"hi\":1500.0,\"device_acts\":false},"
                "{\"field\":\"leaf\",\"lo\":-1.0,\"hi\":26.0,\"device_acts\":false},"
                "{\"field\":\"ppfd\",\"lo\":100.0,\"hi\":200.0,\"device_acts\":false}"
              "]},"
  "\"ebb\":{\"state\":\"FLOOD\",\"flood_reason\":\"scheduled\",\"fault\":false,"
          "\"next_cycle_sec\":300,\"circulate_active\":true,\"circulate_enabled\":true,"
          "\"next_circulate_sec\":120},"
  "\"dev\":{\"device_state\":2,\"fault_bits\":0,\"free_heap\":123456,"
          "\"uptime_s\":3600,\"wifi_rssi\":-55}"
  "}";

void test_full_payload_device_block() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(FULL_PAYLOAD, &d));
  TEST_ASSERT_EQUAL_STRING("IDLE", d.deviceStateName);
  TEST_ASSERT_EQUAL_STRING("BOOT", d.devicePrevStateName);
  TEST_ASSERT_EQUAL_UINT32(100, d.deviceEnteredAt);
  TEST_ASSERT_EQUAL_UINT32(5000, d.deviceTimeInStateMs);
}

void test_full_payload_ux_block() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(FULL_PAYLOAD, &d));
  TEST_ASSERT_TRUE(d.uxFieldsPresent);
  TEST_ASSERT_EQUAL_STRING("green", d.uxLedColor);
  TEST_ASSERT_EQUAL_STRING("Kaikki hyvin", d.uxMessage);
}

void test_full_payload_sensors_and_pe_metrics() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(FULL_PAYLOAD, &d));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 22.5f, d.airTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 55.0f, d.airHumidity);
  TEST_ASSERT_TRUE(d.envValid);
  TEST_ASSERT_TRUE(d.heightValid);
  TEST_ASSERT_EQUAL_INT(150, d.plantHeightMm);
  TEST_ASSERT_TRUE(d.vpdValid);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.1f, d.vpdKpa);
  TEST_ASSERT_TRUE(d.leafTempValid);
  TEST_ASSERT_TRUE(d.ppfdValid);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, d.dli);
  TEST_ASSERT_TRUE(d.co2Valid);
  TEST_ASSERT_EQUAL_INT(800, d.co2Ppm);
  TEST_ASSERT_EQUAL_INT(87, d.batteryPercent);
}

void test_full_payload_motor_and_actuators() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(FULL_PAYLOAD, &d));
  TEST_ASSERT_EQUAL_INT(120, d.motorHeightMm);
  TEST_ASSERT_EQUAL_INT(150, d.motorTargetMm);
  TEST_ASSERT_TRUE(d.motorMoving);
  TEST_ASSERT_TRUE(d.lightsOn);
  TEST_ASSERT_FALSE(d.airPumpOn);
  TEST_ASSERT_TRUE(d.pumpRunning);
}

void test_full_payload_growing_block() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(FULL_PAYLOAD, &d));
  TEST_ASSERT_TRUE(d.growFieldsPresent);
  TEST_ASSERT_TRUE(d.growActive);
  TEST_ASSERT_EQUAL_INT(1, d.growPhase);
  TEST_ASSERT_EQUAL_INT(7, d.growElapsedDays);
  TEST_ASSERT_EQUAL_STRING("basil", d.plantId);
  TEST_ASSERT_EQUAL_STRING("Persilja", d.plantName);
  TEST_ASSERT_EQUAL_STRING("Juurtuminen", d.phaseName);
  TEST_ASSERT_EQUAL_INT(4, d.phaseCount);
}

// ── growing.targets[] (M1, 11.8.2026) — PE comfort bands ────────────────

void test_full_payload_pe_targets_parsed() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(FULL_PAYLOAD, &d));
  TEST_ASSERT_EQUAL_UINT8(5, d.peTargetCount);
  TEST_ASSERT_EQUAL_STRING("vpd", d.peTargets[0].field);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, d.peTargets[0].lo);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, d.peTargets[0].hi);
  TEST_ASSERT_FALSE(d.peTargets[0].deviceActs);
  TEST_ASSERT_EQUAL_STRING("leaf", d.peTargets[3].field);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, PE_NO_LIMIT, d.peTargets[3].lo);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 26.0f, d.peTargets[3].hi);
}

// Old firmware (no "targets" key) or no active phase -> count stays 0, the
// consumer (status_targets.h) falls back to its own generic table.
void test_missing_pe_targets_leaves_count_zero() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(
      "{\"growing\":{\"active\":true,\"phase_name\":\"Kasvuvaihe\"}}", &d));
  TEST_ASSERT_EQUAL_UINT8(0, d.peTargetCount);
}

void test_full_payload_ebb_block() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(FULL_PAYLOAD, &d));
  TEST_ASSERT_EQUAL_STRING("FLOOD", d.ebbState);
  TEST_ASSERT_EQUAL_STRING("scheduled", d.ebbFloodReason);
  TEST_ASSERT_FALSE(d.ebbFault);
  TEST_ASSERT_EQUAL_INT(300, d.ebbNextCycleSec);
  TEST_ASSERT_TRUE(d.circulateActive);
  TEST_ASSERT_EQUAL_INT(120, d.ebbNextCirculateSec);
}

void test_full_payload_dev_block() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(FULL_PAYLOAD, &d));
  TEST_ASSERT_TRUE(d.devFieldsPresent);
  TEST_ASSERT_EQUAL_INT(2, d.devDeviceState);
  TEST_ASSERT_EQUAL_UINT8(0, d.devFaultBits);
  TEST_ASSERT_EQUAL_UINT32(123456, d.devFreeHeap);
  TEST_ASSERT_EQUAL_UINT32(3600, d.devUptimeS);
  TEST_ASSERT_EQUAL_INT(-55, d.devWifiRssi);
}

// ── Empty payload — every optional block absent, defaults must be sane ──

void test_empty_payload_defaults() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto("{}", &d));
  TEST_ASSERT_FALSE(d.uxFieldsPresent);
  TEST_ASSERT_FALSE(d.growFieldsPresent);
  TEST_ASSERT_FALSE(d.devFieldsPresent);
  // waterLevelOk defaults to true (fail-safe: don't show a false water
  // alarm just because the gateway payload omitted the field).
  TEST_ASSERT_TRUE(d.waterLevelOk);
  TEST_ASSERT_EQUAL_STRING("IDLE", d.ebbState);
  TEST_ASSERT_EQUAL_INT(-1, d.ebbNextCycleSec);
  TEST_ASSERT_EQUAL_INT(-1, d.ebbNextCirculateSec);
  TEST_ASSERT_EQUAL_STRING("?", d.plantId);
  TEST_ASSERT_EQUAL_INT(-1, d.devDeviceState);
  TEST_ASSERT_EQUAL_UINT8(0, d.devFaultBits);
}

// ── device_state/fault_bits fallback to device.state_num/faults when the
//    dev block itself is absent (older PM firmware without dev block) ────

void test_dev_block_falls_back_to_device_fields() {
  DisplayData d = {};
  const char* json =
    "{\"device\":{\"state_num\":4,\"faults\":7}}";
  TEST_ASSERT_TRUE(parseInto(json, &d));
  TEST_ASSERT_FALSE(d.devFieldsPresent);  // "dev" block itself is absent
  TEST_ASSERT_EQUAL_INT(4, d.devDeviceState);
  TEST_ASSERT_EQUAL_UINT8(7, d.devFaultBits);
}

// ── String truncation — plant_name longer than the 32-byte buffer must
//    not overflow and must stay null-terminated ─────────────────────────

void test_plant_name_truncates_safely() {
  DisplayData d = {};
  const char* json =
    "{\"growing\":{\"plant_name\":"
    "\"ThisPlantNameIsDefinitelyLongerThanThirtyTwoBytes\"}}";
  TEST_ASSERT_TRUE(parseInto(json, &d));
  // strncpy fills up to (bufsize - 1) chars, then the code null-terminates
  // the last byte explicitly — so a too-long source truncates to exactly
  // sizeof(plantName)-1 visible chars, never overflowing.
  TEST_ASSERT_EQUAL_UINT(sizeof(d.plantName) - 1, strlen(d.plantName));
  TEST_ASSERT_EQUAL_CHAR('\0', d.plantName[sizeof(d.plantName) - 1]);
}


// ── Sticky-validiteetti (kayttajan havainto 28.7.2026) ────────────────
// Laite pudottaa xxx_validin heti kun yksi anturiluku epaonnistuu. Naytolla
// se nakyi arvon KATOAMISENA joka palautui itsestaan — vilkkuva "--" on
// hairitsevampi kuin muutaman minuutin vanha luku.

static const char* kGoodVpd =
  "{\"sensors\":{\"vpd_kpa\":1.24,\"vpd_valid\":true,\"co2_ppm\":500,"
  "\"co2_valid\":true,\"air_temp_c\":24.0,\"air_humidity\":58.0,"
  "\"env_valid\":true}}";
static const char* kMissedVpd =
  "{\"sensors\":{\"vpd_kpa\":0.0,\"vpd_valid\":false,\"co2_ppm\":500,"
  "\"co2_valid\":true,\"air_temp_c\":24.0,\"air_humidity\":58.0,"
  "\"env_valid\":true}}";

void test_sticky_keeps_last_good_value_on_single_miss() {
  DisplayData d = {};
  parseInto(kGoodVpd, &d);
  TEST_ASSERT_TRUE(d.vpdValid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.24f, d.vpdKpa);

  parseInto(kMissedVpd, &d);          // yksi hukattu naute
  TEST_ASSERT_TRUE_MESSAGE(d.vpdValid, "yksi hukattu naute ei saa tyhjentaa korttia");
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.24f, d.vpdKpa);
}

void test_sticky_gives_up_after_grace_period() {
  DisplayData d = {};
  parseInto(kGoodVpd, &d);
  // Armonaika on EINK_STICKY_MAX_MISSES perakkaista hukkaa.
  for (int i = 0; i < EINK_STICKY_MAX_MISSES; i++) parseInto(kMissedVpd, &d);
  TEST_ASSERT_TRUE(d.vpdValid);       // viela armonajan sisalla
  parseInto(kMissedVpd, &d);          // yksi yli -> pysyva vika, nayta "--"
  TEST_ASSERT_FALSE_MESSAGE(d.vpdValid, "pysyva anturivika pitaa nakya, ei piiloutua");
}

void test_sticky_resets_counter_when_value_returns() {
  DisplayData d = {};
  parseInto(kGoodVpd, &d);
  parseInto(kMissedVpd, &d);
  parseInto(kGoodVpd, &d);            // arvo palasi -> laskuri nollaan
  TEST_ASSERT_EQUAL_UINT8(0, d.missVpd);
  // Uusi armonaika on taysimittainen.
  for (int i = 0; i < EINK_STICKY_MAX_MISSES; i++) parseInto(kMissedVpd, &d);
  TEST_ASSERT_TRUE(d.vpdValid);
}

// Ensimmainen haku ei saa keksia arvoja tyhjasta — sticky vaatii etta dataa
// oli ennen (dataValid), muuten kaynnistyva laite nayttaisi nollia.
void test_sticky_does_not_invent_values_on_first_fetch() {
  DisplayData d = {};
  parseInto(kMissedVpd, &d);
  TEST_ASSERT_FALSE(d.vpdValid);
}

// ── growing.button_next_phase (added 10.8.2026) ─────────────────────────
// Missing field must default to false (a panel updated ahead of the main
// unit must never claim the button does something it does not), and both
// explicit true/false must round-trip through parsing.

void test_button_next_phase_missing_defaults_false() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto("{\"growing\":{\"active\":true}}", &d));
  TEST_ASSERT_FALSE(d.buttonNextPhase);
}

void test_button_next_phase_true_parses() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(
      "{\"growing\":{\"active\":true,\"button_next_phase\":true}}", &d));
  TEST_ASSERT_TRUE(d.buttonNextPhase);
}

void test_button_next_phase_false_parses() {
  DisplayData d = {};
  TEST_ASSERT_TRUE(parseInto(
      "{\"growing\":{\"active\":true,\"button_next_phase\":false}}", &d));
  TEST_ASSERT_FALSE(d.buttonNextPhase);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_full_payload_device_block);
  RUN_TEST(test_full_payload_ux_block);
  RUN_TEST(test_full_payload_sensors_and_pe_metrics);
  RUN_TEST(test_full_payload_motor_and_actuators);
  RUN_TEST(test_full_payload_growing_block);
  RUN_TEST(test_full_payload_pe_targets_parsed);
  RUN_TEST(test_missing_pe_targets_leaves_count_zero);
  RUN_TEST(test_full_payload_ebb_block);
  RUN_TEST(test_full_payload_dev_block);
  RUN_TEST(test_empty_payload_defaults);
  RUN_TEST(test_dev_block_falls_back_to_device_fields);
  RUN_TEST(test_plant_name_truncates_safely);
  RUN_TEST(test_sticky_keeps_last_good_value_on_single_miss);
  RUN_TEST(test_sticky_gives_up_after_grace_period);
  RUN_TEST(test_sticky_resets_counter_when_value_returns);
  RUN_TEST(test_sticky_does_not_invent_values_on_first_fetch);
  RUN_TEST(test_button_next_phase_missing_defaults_false);
  RUN_TEST(test_button_next_phase_true_parses);
  RUN_TEST(test_button_next_phase_false_parses);
  return UNITY_END();
}
