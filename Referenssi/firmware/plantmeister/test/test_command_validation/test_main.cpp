#include <unity.h>

#include "command_validation.h"

void setUp() {}
void tearDown() {}

void test_cmd_parseIntStrict_accepts_valid_number() {
  int v = 0;
  TEST_ASSERT_TRUE(cmd_parseIntStrict("123", &v));
  TEST_ASSERT_EQUAL_INT(123, v);
}

void test_cmd_parseIntStrict_rejects_non_numeric() {
  int v = 0;
  TEST_ASSERT_FALSE(cmd_parseIntStrict("12x", &v));
  TEST_ASSERT_FALSE(cmd_parseIntStrict("", &v));
}

void test_cmd_parseFloatStrict_accepts_valid_float() {
  float v = 0.0f;
  TEST_ASSERT_TRUE(cmd_parseFloatStrict("12.5", &v));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, v);
}

void test_cmd_parseFloatStrict_rejects_invalid() {
  float v = 0.0f;
  TEST_ASSERT_FALSE(cmd_parseFloatStrict("12.5x", &v));
  TEST_ASSERT_FALSE(cmd_parseFloatStrict("", &v));
}

void test_portal_validateCommand_accepts_height_range() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(portal_validateCommand("HEIGHT", "120", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("HEIGHT", "99999", err, sizeof(err)));
}

void test_portal_validateCommand_accepts_water_empty_or_range() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(portal_validateCommand("WATER", "", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("WATER", "50", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("WATER", "-1", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("WATER", "abc", err, sizeof(err)));
}

void test_portal_validateCommand_rejects_bad_airpump() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(portal_validateCommand("AIRPUMP", "0", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("air_pump", "1", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("AIRPUMP", "2", err, sizeof(err)));
}

void test_portal_validateCommand_rejects_unknown_command() {
  char err[96] = {0};
  TEST_ASSERT_FALSE(portal_validateCommand("WHATEVER", "1", err, sizeof(err)));
}

// Regression: EBB_FLOOD is handled by command_handler but was missing from
// the validation whitelist, so /api/command rejected it with "unknown command".
// The portal "Aja tulva nyt" button sends value "1"; empty must also pass.
void test_portal_validateCommand_accepts_ebb_flood() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(portal_validateCommand("EBB_FLOOD", "1", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("EBB_FLOOD", "", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("ebb_flood", "1", err, sizeof(err)));
}

void test_portal_validateCommand_validates_plant_id_token() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(portal_validateCommand("PLANT", "basil", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("PLANT", "mint_2", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("PLANT", "bad id", err, sizeof(err)));
}

// Sama luokka bugi kuin EBB_FLOOD yllä: command_handler tunsi FACTORY_RESET:in
// mutta valkolista ei, joten /api/command hylkasi sen "unknown command":lla
// (todettu 15.7.2026 ajamalla oikeaa laitetta vasten). Handlerin lisays ILMAN
// valkolistariviä on hiljainen ei-toiminto — nama testit sitovat ne yhteen.
void test_portal_validateCommand_accepts_factory_reset_with_confirm() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(portal_validateCommand("FACTORY_RESET", "CONFIRM", err, sizeof(err)));
}

// Ainoa komento joka tuhoaa kayttajan datan peruuttamattomasti — se ei saa
// lauetakaan ilman tasmallista vahvistusta.
void test_portal_validateCommand_rejects_factory_reset_without_confirm() {
  char err[96] = {0};
  TEST_ASSERT_FALSE(portal_validateCommand("FACTORY_RESET", "", err, sizeof(err)));
}

void test_portal_validateCommand_rejects_factory_reset_wrong_token() {
  char err[96] = {0};
  TEST_ASSERT_FALSE(portal_validateCommand("FACTORY_RESET", "confirm", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("FACTORY_RESET", "yes", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("FACTORY_RESET", "1", err, sizeof(err)));
}

// Kolmas kerta samaa bugiluokkaa (EBB_FLOOD 05/2026, FACTORY_RESET 15.7.2026,
// GROW_STEP_* 16.7.2026): handler tunsi komennot mutta valkolista ei, joten
// /api/command hylkasi ne "unknown command":lla — todettu taas oikeaa laitetta
// vasten OTA:n jalkeen. Uusi komento EI ole valmis ennen valkolistarivia.
void test_portal_validateCommand_accepts_grow_step_commands() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(portal_validateCommand("GROW_STEP_ACK", "", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("GROW_STEP_SKIP", "", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("GROW_STEP_BACK", "", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("GROW_STEP_SET", "2", err, sizeof(err)));
}

void test_portal_validateCommand_grow_step_set_requires_index() {
  char err[96] = {0};
  TEST_ASSERT_FALSE(portal_validateCommand("GROW_STEP_SET", "", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("GROW_STEP_SET", "abc", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("GROW_STEP_SET", "-1", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("GROW_STEP_SET", "256", err, sizeof(err)));
}

// Sama valkolistasopimus kuin GROW_STEP_SET: huoltoreitin GROW_PHASE_SET /
// GROW_DAY_SET on valmis vasta kun /api/command paastaa ne lapi. Ilman tata
// riviä portaalin Huolto-saatonapit saisivat 400 "unknown command".
void test_portal_validateCommand_accepts_grow_phase_and_day_set() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(portal_validateCommand("GROW_PHASE_SET", "0", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("grow_phase_set", "2", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("GROW_DAY_SET", "0", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("grow_day_set", "12", err, sizeof(err)));
}

void test_portal_validateCommand_grow_phase_and_day_set_reject_bad_value() {
  char err[96] = {0};
  TEST_ASSERT_FALSE(portal_validateCommand("GROW_PHASE_SET", "", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("GROW_PHASE_SET", "abc", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("GROW_DAY_SET", "-1", err, sizeof(err)));
  TEST_ASSERT_FALSE(portal_validateCommand("GROW_DAY_SET", "256", err, sizeof(err)));
}

// Neljas kerta samaa bugiluokkaa (EBB_FLOOD 05/2026, FACTORY_RESET 15.7.2026,
// GROW_STEP_* 16.7.2026): command_handler.h tunsi MAINTENANCE_ON/OFF mutta
// valkolista ei, joten pm.py maintenance on/off sai 400 "unknown command"
// oikeaa laitetta vasten (todettu 23.7.2026 - laite jaanyt huoltotilaan ~10 h
// koska etapoisto ei toiminut, ainoa keino oli fyysinen nappi-pitkapainallus).
void test_portal_validateCommand_accepts_maintenance_on_off() {
  char err[96] = {0};
  TEST_ASSERT_TRUE(portal_validateCommand("MAINTENANCE_ON", "", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("maintenance_on", "", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("MAINTENANCE_OFF", "", err, sizeof(err)));
  TEST_ASSERT_TRUE(portal_validateCommand("maintenance_off", "", err, sizeof(err)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cmd_parseIntStrict_accepts_valid_number);
  RUN_TEST(test_cmd_parseIntStrict_rejects_non_numeric);
  RUN_TEST(test_cmd_parseFloatStrict_accepts_valid_float);
  RUN_TEST(test_cmd_parseFloatStrict_rejects_invalid);
  RUN_TEST(test_portal_validateCommand_accepts_height_range);
  RUN_TEST(test_portal_validateCommand_accepts_water_empty_or_range);
  RUN_TEST(test_portal_validateCommand_rejects_bad_airpump);
  RUN_TEST(test_portal_validateCommand_rejects_unknown_command);
  RUN_TEST(test_portal_validateCommand_accepts_ebb_flood);
  RUN_TEST(test_portal_validateCommand_validates_plant_id_token);
  RUN_TEST(test_portal_validateCommand_accepts_factory_reset_with_confirm);
  RUN_TEST(test_portal_validateCommand_rejects_factory_reset_without_confirm);
  RUN_TEST(test_portal_validateCommand_accepts_grow_step_commands);
  RUN_TEST(test_portal_validateCommand_grow_step_set_requires_index);
  RUN_TEST(test_portal_validateCommand_accepts_grow_phase_and_day_set);
  RUN_TEST(test_portal_validateCommand_grow_phase_and_day_set_reject_bad_value);
  RUN_TEST(test_portal_validateCommand_rejects_factory_reset_wrong_token);
  RUN_TEST(test_portal_validateCommand_accepts_maintenance_on_off);
  return UNITY_END();
}
