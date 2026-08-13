#include <string.h>
#include <unity.h>

#include "resolve_field.h"

void setUp() {}
void tearDown() {}

static DisplayData makeBaseData() {
  DisplayData d = {};
  return d;
}

// ── VPD ──────────────────────────────────────────────────────────────

void test_vpd_valid() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = 1.23f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_VPD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("1.23", val);
  TEST_ASSERT_EQUAL_STRING("kPa", unit);
}

// Kaksi desimaalia, koska tavoiteikkuna on kapea (0.4 kPa). Yhdella
// desimaalilla 0.86 piirtyi "0.8":na tavoitteen "tav 0.4-0.8" viereen ja
// nayttoi silti poikkeamalta — nayttava luku ei saa olla ristiriidassa
// samalla ruudulla nakyvan tavoitteen kanssa (rautahavainto 27.7.2026).
void test_vpd_two_decimals_near_target_boundary() {
  DisplayData d = makeBaseData();
  d.vpdValid = true;
  d.vpdKpa = 0.8601f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_VPD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("0.86", val);
}

void test_vpd_invalid_returns_false() {
  DisplayData d = makeBaseData();
  d.vpdValid = false;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_VPD, &d, val, sizeof(val), unit, sizeof(unit)));
}

// ── Leaf delta / leaf temp ───────────────────────────────────────────

void test_leaf_delta_positive_sign() {
  DisplayData d = makeBaseData();
  d.leafTempValid = true;
  d.leafTempC = 24.0f;
  d.airTemp = 22.0f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_LEAF_DELTA, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("+2.0", val);
  TEST_ASSERT_EQUAL_STRING("C", unit);
}

void test_leaf_delta_negative_sign() {
  DisplayData d = makeBaseData();
  d.leafTempValid = true;
  d.leafTempC = 20.0f;
  d.airTemp = 22.0f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_LEAF_DELTA, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("-2.0", val);
}

void test_leaf_temp_invalid_returns_false() {
  DisplayData d = makeBaseData();
  d.leafTempValid = false;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_LEAF_TEMP, &d, val, sizeof(val), unit, sizeof(unit)));
}

// ── DLI — valid if ppfdValid OR an accumulated dli>0 (keeps showing today's
//    total after the PPFD sensor drops out mid-day) ─────────────────────

void test_dli_valid_via_ppfd_flag() {
  DisplayData d = makeBaseData();
  d.ppfdValid = true;
  d.dli = 0.0f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_DLI, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("0.0", val);
  TEST_ASSERT_EQUAL_STRING("mol", unit);
}

void test_dli_valid_via_accumulated_value() {
  DisplayData d = makeBaseData();
  d.ppfdValid = false;
  d.dli = 3.4f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_DLI, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("3.4", val);
}

void test_dli_invalid_when_both_false() {
  DisplayData d = makeBaseData();
  d.ppfdValid = false;
  d.dli = 0.0f;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_DLI, &d, val, sizeof(val), unit, sizeof(unit)));
}

// ── PPFD / CO2 — gated on their own *Valid flags ─────────────────────

void test_ppfd_valid() {
  DisplayData d = makeBaseData();
  d.ppfdValid = true;
  d.ppfd = 250.4f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("250", val);   // kasvatustaso -> desimaali on kohinaa
  TEST_ASSERT_EQUAL_STRING("umol", unit);
}

// Pimeassa (lamppu pois) koko lukema mahtuu desimaalien puolelle. "%.0f" antoi
// "0", mika nayttaa rikkinaiselta anturilta vaikka anturi toimii ja huone on
// vain pimea (rautahavainto 27.7.2026: ppfd 0.25, spektri valid, lights_on
// false). Alle 10 umol siis yhdella desimaalilla.
void test_ppfd_shows_decimal_when_dark() {
  DisplayData d = makeBaseData();
  d.ppfdValid = true;
  char val[16], unit[8];

  d.ppfd = 0.2539f;
  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("0.3", val);

  d.ppfd = 9.94f;                          // juuri kynnyksen alla
  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("9.9", val);

  d.ppfd = 10.4f;                          // kynnyksesta ylospain kokonaisluku
  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("10", val);
}

// Anturin oma lukema erillaan latvan arvosta. Naiden EROTUS on ainoa asia joka
// paljastaa vaarin asetetun geometriakertoimen ilman CLI:ta, joten kentat eivat
// saa nayttaa samaa lukua kun kerroin != 1.0.
void test_ppfd_sensor_separate_from_canopy() {
  DisplayData d = makeBaseData();
  d.ppfdValid = true;
  d.ppfd       = 213.0f;   // latva (geometria mukana)
  d.ppfdSensor = 34.1f;    // anturi kauempana lampusta
  char val[16], unit[8];

  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD_SENSOR, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("34", val);
  TEST_ASSERT_EQUAL_STRING("umol", unit);

  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("213", val);
}

// Sama tarkkuussaanto kuin FIELD_PPFD:lla, jotta rinnakkain luetut arvot ovat
// vertailukelpoisia (pimeassa molemmat desimaalilla).
void test_ppfd_sensor_follows_same_precision_rule() {
  DisplayData d = makeBaseData();
  d.ppfdValid = true;
  char val[16], unit[8];

  d.ppfdSensor = 0.41f;
  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD_SENSOR, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("0.4", val);

  d.ppfdSensor = 34.1f;
  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD_SENSOR, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("34", val);
}

// Invalidi spektri piilottaa molemmat — ei nollaa naytolle.
void test_ppfd_sensor_hidden_when_invalid() {
  DisplayData d = makeBaseData();
  d.ppfdValid = false;
  d.ppfdSensor = 34.1f;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_PPFD_SENSOR, &d, val, sizeof(val), unit, sizeof(unit)));
}

// ── Keskiteho: desynkkautunut varauslaskuri piilotetaan ──────────────
// INA228:n varausrekisteri ei nollaudu ESP32:n SW-resetissa, joten flashin
// jalkeen "varaus / uptime" yliarvioi rajusti. Vaara luku on pahempi kuin
// puuttuva, joten se piilotetaan (kayttajan havainto 28.7.2026: 47 W).

void test_avg_power_hidden_when_charge_counter_desynced() {
  DisplayData d = makeBaseData();
  d.powerValid = true;
  d.powerChargeMah = 3376.0f;     // kertynyt tunteja ennen reboottia
  d.devUptimeS = 3076;            // 0.855 h bootista
  d.powerCurrentMa = 56.9f;       // todellinen kulutus 0.68 W
  char val[16], unit[8];
  // Naiva kaava antaisi 47 W eli 69x hetkellisen -> piilota.
  TEST_ASSERT_FALSE(resolveField(FIELD_AVG_POWER_W, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_avg_power_shown_when_counters_agree() {
  DisplayData d = makeBaseData();
  d.powerValid = true;
  d.devUptimeS = 3600;            // 1 h
  d.powerCurrentMa = 56.9f;       // 0.68 W hetkellinen
  d.powerChargeMah = 56.9f;       // 1 h samalla virralla -> 0.68 W keskiteho
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_AVG_POWER_W, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("0.7", val);
  TEST_ASSERT_EQUAL_STRING("W", unit);
}

// Hetkellinen 0 (lamppu pois, mittaus nollassa) ei saa laukaista vahtia —
// muuten keskiteho katoaisi aina kun kuorma on hetkellisesti pois.
void test_avg_power_sanity_check_skipped_when_instant_zero() {
  DisplayData d = makeBaseData();
  d.powerValid = true;
  d.devUptimeS = 3600;
  d.powerCurrentMa = 0.0f;
  d.powerChargeMah = 50.0f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_AVG_POWER_W, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_co2_invalid_returns_false() {
  DisplayData d = makeBaseData();
  d.co2Valid = false;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_CO2, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_co2_valid() {
  DisplayData d = makeBaseData();
  d.co2Valid = true;
  d.co2Ppm = 812;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_CO2, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("812", val);
  TEST_ASSERT_EQUAL_STRING("ppm", unit);
}

// ── Air temp / RH — gated on envValid ─────────────────────────────────

void test_air_temp_and_rh_require_env_valid() {
  DisplayData d = makeBaseData();
  d.envValid = false;
  d.airTemp = 21.0f;
  d.airHumidity = 55.0f;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_AIR_TEMP, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_FALSE(resolveField(FIELD_AIR_RH, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_air_rh_valid() {
  DisplayData d = makeBaseData();
  d.envValid = true;
  d.airHumidity = 55.0f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_AIR_RH, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("55", val);
  TEST_ASSERT_EQUAL_STRING("%", unit);
}

// ── Water temp — plausibility range (0.5, 60), not a *Valid flag; guards a
//    disconnected/zero sensor reading (see resolve_field.h FIELD_WATER_TEMP) ──

void test_water_temp_valid_in_range() {
  DisplayData d = makeBaseData();
  d.waterTemp = 21.5f;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_WATER_TEMP, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("21.5", val);
  TEST_ASSERT_EQUAL_STRING("C", unit);
}

void test_water_temp_zero_is_invalid() {
  DisplayData d = makeBaseData();
  d.waterTemp = 0.0f;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_WATER_TEMP, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_water_temp_above_60_is_invalid() {
  DisplayData d = makeBaseData();
  d.waterTemp = 61.0f;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_WATER_TEMP, &d, val, sizeof(val), unit, sizeof(unit)));
}

// ── Height — gated on heightValid ─────────────────────────────────────

void test_height_invalid_returns_false() {
  DisplayData d = makeBaseData();
  d.heightValid = false;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_HEIGHT, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_height_valid() {
  DisplayData d = makeBaseData();
  d.heightValid = true;
  d.plantHeightMm = 145;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_HEIGHT, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("145", val);
  TEST_ASSERT_EQUAL_STRING("mm", unit);
}

// ── Fields that are always present (no *Valid gate) ─────────────────────

void test_water_level_ok_text() {
  DisplayData d = makeBaseData();
  d.waterLevelOk = true;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_WATER_LEVEL, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING(TXT_WATER_OK, val);
}

void test_water_level_low_text() {
  DisplayData d = makeBaseData();
  d.waterLevelOk = false;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_WATER_LEVEL, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING(TXT_WATER_LOW, val);
}

void test_tds_always_present() {
  DisplayData d = makeBaseData();
  d.tdsPpm = 640;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_TDS, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("640", val);
  TEST_ASSERT_EQUAL_STRING("ppm", unit);
}

void test_battery_always_present() {
  DisplayData d = makeBaseData();
  d.batteryPercent = 87;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_BATTERY, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("87", val);
  TEST_ASSERT_EQUAL_STRING("%", unit);
}

void test_lights_on_text() {
  DisplayData d = makeBaseData();
  d.lightsOn = true;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_LIGHTS, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING(TXT_ON, val);
}

void test_pump_off_text() {
  DisplayData d = makeBaseData();
  d.pumpRunning = false;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_PUMP, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING(TXT_OFF, val);
}

// ── Ebb state / next flood ───────────────────────────────────────────

void test_ebb_state_passthrough() {
  DisplayData d = makeBaseData();
  strncpy(d.ebbState, "FLOOD", sizeof(d.ebbState) - 1);
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_EBB_STATE, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("FLOOD", val);
}

void test_next_flood_scheduled() {
  DisplayData d = makeBaseData();
  d.ebbNextCycleSec = 180; // 3 min
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_NEXT_FLOOD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("3", val);
  TEST_ASSERT_EQUAL_STRING("min", unit);
}

void test_next_flood_unscheduled_is_invalid() {
  DisplayData d = makeBaseData();
  d.ebbNextCycleSec = -1;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_NEXT_FLOOD, &d, val, sizeof(val), unit, sizeof(unit)));
}

// ── Plant name / phase name / grow days (grow-session gated) ────────────

void test_plant_name_present() {
  DisplayData d = makeBaseData();
  strncpy(d.plantName, "Persilja", sizeof(d.plantName) - 1);
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_PLANT_NAME, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("Persilja", val);
}

void test_plant_name_missing_is_invalid() {
  DisplayData d = makeBaseData();
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_PLANT_NAME, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_phase_name_requires_grow_active() {
  DisplayData d = makeBaseData();
  d.growActive = false;
  strncpy(d.phaseName, "Juurtuminen", sizeof(d.phaseName) - 1);
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_PHASE_NAME, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_phase_name_requires_non_empty() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.phaseName[0] = '\0';
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_PHASE_NAME, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_phase_name_active_and_present() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  strncpy(d.phaseName, "Juurtuminen", sizeof(d.phaseName) - 1);
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_PHASE_NAME, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("Juurtuminen", val);
}

void test_grow_days_requires_active() {
  DisplayData d = makeBaseData();
  d.growActive = false;
  d.growElapsedDays = 5;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_GROW_DAYS, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_grow_days_active() {
  DisplayData d = makeBaseData();
  d.growActive = true;
  d.growElapsedDays = 5;
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_GROW_DAYS, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("5", val);
  TEST_ASSERT_EQUAL_STRING("pv", unit);
}

// ── Uptime — always present, whole-minute truncation ─────────────────

void test_uptime_converts_seconds_to_minutes() {
  DisplayData d = makeBaseData();
  d.devUptimeS = 185; // truncates to 3 whole minutes
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_UPTIME, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("3", val);
  TEST_ASSERT_EQUAL_STRING("min", unit);
}

// ── Virrankulutus (INA228, 12 V vakio) — powerValid-gated ─────────────

void test_power_w_valid() {
  DisplayData d = makeBaseData();
  d.powerValid = true;
  d.powerCurrentMa = 50.0f;   // 12 V * 50 mA = 0.6 W
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_POWER_W, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("0.6", val);
  TEST_ASSERT_EQUAL_STRING("W", unit);
}

void test_power_w_invalid_returns_false() {
  DisplayData d = makeBaseData();
  d.powerValid = false;
  d.powerCurrentMa = 50.0f;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_POWER_W, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_avg_power_w_energy_over_time() {
  DisplayData d = makeBaseData();
  d.powerValid = true;
  d.powerChargeMah = 100.0f;  // energy = 12 V * 0.1 Ah = 1.2 Wh
  d.devUptimeS = 7200;        // 2 h  -> avg = 1.2 Wh / 2 h = 0.6 W
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_AVG_POWER_W, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("0.6", val);
  TEST_ASSERT_EQUAL_STRING("W", unit);
}

void test_avg_power_w_zero_uptime_is_invalid() {
  DisplayData d = makeBaseData();
  d.powerValid = true;
  d.powerChargeMah = 100.0f;
  d.devUptimeS = 0;           // guard against divide-by-zero right after boot
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_AVG_POWER_W, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_energy_wh_integral() {
  DisplayData d = makeBaseData();
  d.powerValid = true;
  d.powerChargeMah = 100.0f;  // 12 V * 0.1 Ah = 1.2 Wh
  char val[16], unit[8];
  TEST_ASSERT_TRUE(resolveField(FIELD_ENERGY_WH, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("1.20", val);
  TEST_ASSERT_EQUAL_STRING("Wh", unit);
}

void test_energy_wh_invalid_returns_false() {
  DisplayData d = makeBaseData();
  d.powerValid = false;
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_ENERGY_WH, &d, val, sizeof(val), unit, sizeof(unit)));
}

// ── Unknown / sentinel FieldId ────────────────────────────────────────

void test_unknown_field_returns_false() {
  DisplayData d = makeBaseData();
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(255, &d, val, sizeof(val), unit, sizeof(unit)));
}

void test_field_none_returns_false() {
  DisplayData d = makeBaseData();
  char val[16], unit[8];
  TEST_ASSERT_FALSE(resolveField(FIELD_NONE, &d, val, sizeof(val), unit, sizeof(unit)));
}


// ════════════════════════════════════════════════════════════════════
// Saturoitunut PPFD (mittausalue taynna)
// ════════════════════════════════════════════════════════════════════

// Katossa oleva lukema merkitaan ">"-etuliitteella. Ilman sita naytto
// esittaa alarajan faktana ja tasanne luetaan "valo lakkasi kirkastumasta".
void test_ppfd_saturated_gets_prefix() {
  DisplayData d = {};
  d.ppfdValid = true; d.ppfd = 96.8f; d.ppfdSaturated = true;
  char val[24], unit[12];
  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING(">97", val);
  TEST_ASSERT_EQUAL_STRING("umol", unit);
}

// Normaali lukema pysyy ennallaan — etuliite ei saa vuotaa.
void test_ppfd_unsaturated_has_no_prefix() {
  DisplayData d = {};
  d.ppfdValid = true; d.ppfd = 78.0f; d.ppfdSaturated = false;
  char val[24], unit[12];
  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING("78", val);
}

// Alle 10 umol sailyttaa desimaalin myos etuliitteen kanssa.
void test_ppfd_saturated_keeps_decimal_when_dim() {
  DisplayData d = {};
  d.ppfdValid = true; d.ppfd = 5.5f; d.ppfdSaturated = true;
  char val[24], unit[12];
  TEST_ASSERT_TRUE(resolveField(FIELD_PPFD, &d, val, sizeof(val), unit, sizeof(unit)));
  TEST_ASSERT_EQUAL_STRING(">5.5", val);
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_vpd_valid);
  RUN_TEST(test_vpd_two_decimals_near_target_boundary);
  RUN_TEST(test_vpd_invalid_returns_false);

  RUN_TEST(test_leaf_delta_positive_sign);
  RUN_TEST(test_leaf_delta_negative_sign);
  RUN_TEST(test_leaf_temp_invalid_returns_false);

  RUN_TEST(test_dli_valid_via_ppfd_flag);
  RUN_TEST(test_dli_valid_via_accumulated_value);
  RUN_TEST(test_dli_invalid_when_both_false);

  RUN_TEST(test_ppfd_valid);
  RUN_TEST(test_ppfd_shows_decimal_when_dark);
  RUN_TEST(test_ppfd_sensor_separate_from_canopy);
  RUN_TEST(test_ppfd_sensor_follows_same_precision_rule);
  RUN_TEST(test_ppfd_sensor_hidden_when_invalid);
  RUN_TEST(test_avg_power_hidden_when_charge_counter_desynced);
  RUN_TEST(test_avg_power_shown_when_counters_agree);
  RUN_TEST(test_avg_power_sanity_check_skipped_when_instant_zero);
  RUN_TEST(test_co2_invalid_returns_false);
  RUN_TEST(test_co2_valid);

  RUN_TEST(test_air_temp_and_rh_require_env_valid);
  RUN_TEST(test_air_rh_valid);

  RUN_TEST(test_water_temp_valid_in_range);
  RUN_TEST(test_water_temp_zero_is_invalid);
  RUN_TEST(test_water_temp_above_60_is_invalid);

  RUN_TEST(test_height_invalid_returns_false);
  RUN_TEST(test_height_valid);

  RUN_TEST(test_water_level_ok_text);
  RUN_TEST(test_water_level_low_text);
  RUN_TEST(test_tds_always_present);
  RUN_TEST(test_battery_always_present);
  RUN_TEST(test_lights_on_text);
  RUN_TEST(test_pump_off_text);

  RUN_TEST(test_ebb_state_passthrough);
  RUN_TEST(test_next_flood_scheduled);
  RUN_TEST(test_next_flood_unscheduled_is_invalid);

  RUN_TEST(test_plant_name_present);
  RUN_TEST(test_plant_name_missing_is_invalid);
  RUN_TEST(test_phase_name_requires_grow_active);
  RUN_TEST(test_phase_name_requires_non_empty);
  RUN_TEST(test_phase_name_active_and_present);
  RUN_TEST(test_grow_days_requires_active);
  RUN_TEST(test_grow_days_active);

  RUN_TEST(test_uptime_converts_seconds_to_minutes);

  RUN_TEST(test_power_w_valid);
  RUN_TEST(test_power_w_invalid_returns_false);
  RUN_TEST(test_avg_power_w_energy_over_time);
  RUN_TEST(test_avg_power_w_zero_uptime_is_invalid);
  RUN_TEST(test_energy_wh_integral);
  RUN_TEST(test_energy_wh_invalid_returns_false);

  RUN_TEST(test_unknown_field_returns_false);
  RUN_TEST(test_field_none_returns_false);

  RUN_TEST(test_ppfd_saturated_gets_prefix);
  RUN_TEST(test_ppfd_unsaturated_has_no_prefix);
  RUN_TEST(test_ppfd_saturated_keeps_decimal_when_dim);
  return UNITY_END();
}
