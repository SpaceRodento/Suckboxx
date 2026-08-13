#include <unity.h>

#include "api_contract_validation.h"

void setUp() {}
void tearDown() {}

void test_validate_grow_action_accepts_known_actions() {
  char err[64] = {0};
  TEST_ASSERT_TRUE(api_validateGrowAction("start", err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateGrowAction("next", err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateGrowAction("delay", err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateGrowAction("stop", err, sizeof(err)));
}

void test_validate_grow_action_rejects_unknown() {
  char err[64] = {0};
  TEST_ASSERT_FALSE(api_validateGrowAction("invalid", err, sizeof(err)));
}

void test_validate_grow_start_method_bounds() {
  char err[64] = {0};
  TEST_ASSERT_TRUE(api_validateGrowStartMethod(0, err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateGrowStartMethod(2, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateGrowStartMethod(-1, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateGrowStartMethod(3, err, sizeof(err)));
}

void test_validate_history_request_accepts_valid_field_and_max() {
  char err[64] = {0};
  char field[32] = {0};
  TEST_ASSERT_TRUE(api_validateHistoryRequest("air_temp", 10, field, sizeof(field), err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("air_temp", field);
}

void test_validate_history_request_defaults_empty_field() {
  char err[64] = {0};
  char field[32] = {0};
  TEST_ASSERT_TRUE(api_validateHistoryRequest("", 50, field, sizeof(field), err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("air_temp", field);
}

void test_validate_history_request_rejects_invalid_field() {
  char err[64] = {0};
  char field[32] = {0};
  TEST_ASSERT_FALSE(api_validateHistoryRequest("unknown", 50, field, sizeof(field), err, sizeof(err)));
}

void test_validate_history_request_rejects_invalid_max() {
  char err[64] = {0};
  char field[32] = {0};
  TEST_ASSERT_FALSE(api_validateHistoryRequest("air_temp", 0, field, sizeof(field), err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateHistoryRequest("air_temp", API_HISTORY_MAX_ENTRIES + 1, field, sizeof(field), err, sizeof(err)));
}

void test_validate_config_lora_bounds() {
  char err[64] = {0};
  TEST_ASSERT_TRUE(api_validateConfigLoraAddress(1, err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateConfigLoraAddress(255, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateConfigLoraAddress(0, err, sizeof(err)));

  TEST_ASSERT_TRUE(api_validateConfigLoraNetworkId(0, err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateConfigLoraNetworkId(255, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateConfigLoraNetworkId(-1, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateConfigLoraNetworkId(256, err, sizeof(err)));

  TEST_ASSERT_TRUE(api_validateConfigLoraTarget(0, err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateConfigLoraTarget(255, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateConfigLoraTarget(-1, err, sizeof(err)));
}

void test_validate_config_light_on_hour_bounds() {
  char err[64] = {0};
  TEST_ASSERT_TRUE(api_validateConfigLightOnHour(0, err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateConfigLightOnHour(23, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateConfigLightOnHour(-1, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateConfigLightOnHour(24, err, sizeof(err)));
}

void test_validate_config_wifi_lengths() {
  char err[64] = {0};
  TEST_ASSERT_TRUE(api_validateConfigWifiSsid("", err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateConfigWifiSsid("PlantNet", err, sizeof(err)));

  char longSsid[40];
  memset(longSsid, 'A', sizeof(longSsid));
  longSsid[39] = '\0';
  TEST_ASSERT_FALSE(api_validateConfigWifiSsid(longSsid, err, sizeof(err)));

  TEST_ASSERT_TRUE(api_validateConfigWifiPassword("", err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateConfigWifiPassword("1234567", err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateConfigWifiPassword("12345678", err, sizeof(err)));

  char longPass[80];
  memset(longPass, 'B', sizeof(longPass));
  longPass[79] = '\0';
  TEST_ASSERT_FALSE(api_validateConfigWifiPassword(longPass, err, sizeof(err)));
}

void test_validate_admin_pin_rules() {
  char err[64] = {0};

  TEST_ASSERT_TRUE(api_validateAdminPin("", true, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateAdminPin("", false, err, sizeof(err)));

  TEST_ASSERT_TRUE(api_validateAdminPin("1234", false, err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateAdminPin("12345678", false, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateAdminPin("123", false, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateAdminPin("123456789", false, err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateAdminPin("12a4", false, err, sizeof(err)));
}

void test_validate_config_plant_id_token() {
  char err[64] = {0};
  TEST_ASSERT_TRUE(api_validateConfigPlantId("basil", err, sizeof(err)));
  TEST_ASSERT_TRUE(api_validateConfigPlantId("mint_2", err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateConfigPlantId("", err, sizeof(err)));
  TEST_ASSERT_FALSE(api_validateConfigPlantId("bad id", err, sizeof(err)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_validate_grow_action_accepts_known_actions);
  RUN_TEST(test_validate_grow_action_rejects_unknown);
  RUN_TEST(test_validate_grow_start_method_bounds);
  RUN_TEST(test_validate_history_request_accepts_valid_field_and_max);
  RUN_TEST(test_validate_history_request_defaults_empty_field);
  RUN_TEST(test_validate_history_request_rejects_invalid_field);
  RUN_TEST(test_validate_history_request_rejects_invalid_max);
  RUN_TEST(test_validate_config_lora_bounds);
  RUN_TEST(test_validate_config_light_on_hour_bounds);
  RUN_TEST(test_validate_config_wifi_lengths);
  RUN_TEST(test_validate_admin_pin_rules);
  RUN_TEST(test_validate_config_plant_id_token);
  return UNITY_END();
}
