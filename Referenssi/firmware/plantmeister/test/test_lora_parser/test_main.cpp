#include <unity.h>

#include "lora_parser.h"

void setUp() {}
void tearDown() {}

void test_parse_valid_key_value() {
  char key[16] = {0};
  char value[16] = {0};

  TEST_ASSERT_TRUE(lora_parseCommand("CMD:LIGHT=1", key, sizeof(key), value, sizeof(value)));
  TEST_ASSERT_EQUAL_STRING("LIGHT", key);
  TEST_ASSERT_EQUAL_STRING("1", value);
}

void test_parse_valid_key_only() {
  char key[16] = {0};
  char value[16] = {0};

  TEST_ASSERT_TRUE(lora_parseCommand("CMD:REBOOT", key, sizeof(key), value, sizeof(value)));
  TEST_ASSERT_EQUAL_STRING("REBOOT", key);
  TEST_ASSERT_EQUAL_STRING("", value);
}

void test_parse_rejects_missing_prefix() {
  char key[16] = {0};
  char value[16] = {0};

  TEST_ASSERT_FALSE(lora_parseCommand("LIGHT=1", key, sizeof(key), value, sizeof(value)));
}

void test_parse_rejects_empty_string() {
  char key[16] = {0};
  char value[16] = {0};

  TEST_ASSERT_FALSE(lora_parseCommand("", key, sizeof(key), value, sizeof(value)));
}

void test_parse_key_truncates_safely() {
  char key[6] = {0};
  char value[16] = {0};

  TEST_ASSERT_TRUE(lora_parseCommand("CMD:VERYLONGKEY=OK", key, sizeof(key), value, sizeof(value)));
  TEST_ASSERT_EQUAL_STRING("VERYL", key);
  TEST_ASSERT_EQUAL_STRING("OK", value);
}

void test_parse_value_truncates_safely() {
  char key[16] = {0};
  char value[6] = {0};

  TEST_ASSERT_TRUE(lora_parseCommand("CMD:LIGHT=123456789", key, sizeof(key), value, sizeof(value)));
  TEST_ASSERT_EQUAL_STRING("LIGHT", key);
  TEST_ASSERT_EQUAL_STRING("12345", value);
}

void test_parse_rejects_null_inputs() {
  char key[8] = {0};
  char value[8] = {0};

  TEST_ASSERT_FALSE(lora_parseCommand(NULL, key, sizeof(key), value, sizeof(value)));
  TEST_ASSERT_FALSE(lora_parseCommand("CMD:LIGHT=1", NULL, sizeof(key), value, sizeof(value)));
  TEST_ASSERT_FALSE(lora_parseCommand("CMD:LIGHT=1", key, sizeof(key), NULL, sizeof(value)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_valid_key_value);
  RUN_TEST(test_parse_valid_key_only);
  RUN_TEST(test_parse_rejects_missing_prefix);
  RUN_TEST(test_parse_rejects_empty_string);
  RUN_TEST(test_parse_key_truncates_safely);
  RUN_TEST(test_parse_value_truncates_safely);
  RUN_TEST(test_parse_rejects_null_inputs);
  return UNITY_END();
}
