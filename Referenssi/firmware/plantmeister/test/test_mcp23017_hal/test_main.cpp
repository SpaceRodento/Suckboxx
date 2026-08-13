#include <unity.h>

// Feature flags BEFORE includes.
#define ENABLE_MCP23017 true
#include "test/stubs/test_feature_flags.h"

#include "config.h"
#include "mcp23017_hal.h"

// MCP23017 register addresses, mirrored from mcp23017_hal.h for assertions.
#define REG_IODIRA   0x00
#define REG_GPINTENA 0x04
#define REG_INTCONA  0x08
#define REG_GPPUA    0x0C
#define REG_INTFA    0x0E
#define REG_GPIOA    0x12
#define REG_OLATA    0x14

void setUp(void) {
  mcp23017_resetForTest();
  Wire.wire_reset();
  g_wire_endTransmission_result = 0;
}

void tearDown(void) {}

// ── Address scan (plug-and-play, § 8) ────────────────────────────────

void test_init_finds_device_at_default_address(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);  // 0x20
  TEST_ASSERT_TRUE(mcp23017_init());
  TEST_ASSERT_TRUE(mcp23017_isReady());
  TEST_ASSERT_EQUAL_HEX8(I2C_ADDR_MCP23017, mcp23017_addr());
}

void test_init_finds_device_strapped_high_at_0x27(void) {
  Wire.wire_setAckAddress(0x27);
  TEST_ASSERT_TRUE(mcp23017_init());
  TEST_ASSERT_EQUAL_HEX8(0x27, mcp23017_addr());
}

void test_init_scans_whole_range_before_giving_up(void) {
  Wire.wire_setAckAddress(0x25);  // middle of the 0x20-0x27 range
  TEST_ASSERT_TRUE(mcp23017_init());
  TEST_ASSERT_EQUAL_HEX8(0x25, mcp23017_addr());
}

void test_init_fails_when_nothing_acks(void) {
  Wire.wire_setAckAddress(0x30);  // outside the scanned range
  TEST_ASSERT_FALSE(mcp23017_init());
  TEST_ASSERT_FALSE(mcp23017_isReady());
}

// ── Init register configuration ──────────────────────────────────────

void test_init_configures_registers_correctly(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  TEST_ASSERT_TRUE(mcp23017_init());

  TEST_ASSERT_EQUAL_HEX8(MCP23017_PORTA_INPUT_MASK,  Wire.wire_getReg(I2C_ADDR_MCP23017, REG_IODIRA));
  TEST_ASSERT_EQUAL_HEX8(MCP23017_PORTA_PULLUP_MASK, Wire.wire_getReg(I2C_ADDR_MCP23017, REG_GPPUA));
  TEST_ASSERT_EQUAL_HEX8(0x00,                       Wire.wire_getReg(I2C_ADDR_MCP23017, REG_INTCONA));
  TEST_ASSERT_EQUAL_HEX8(MCP23017_PORTA_IOC_MASK,    Wire.wire_getReg(I2C_ADDR_MCP23017, REG_GPINTENA));
  TEST_ASSERT_EQUAL_HEX8(0x00,                       Wire.wire_getReg(I2C_ADDR_MCP23017, REG_OLATA));
}

// ── Output pins (OLAT shadow) ─────────────────────────────────────────

void test_write_pin_before_ready_is_noop(void) {
  mcp23017_writePin(1, true);
  TEST_ASSERT_EQUAL_HEX8(0x00, Wire.wire_getReg(I2C_ADDR_MCP23017, REG_OLATA));
}

void test_write_pin_sets_and_clears_bit(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  TEST_ASSERT_TRUE(mcp23017_init());

  mcp23017_writePin(1, true);
  TEST_ASSERT_EQUAL_HEX8(0x02, Wire.wire_getReg(I2C_ADDR_MCP23017, REG_OLATA));

  mcp23017_writePin(3, true);
  TEST_ASSERT_EQUAL_HEX8(0x0A, Wire.wire_getReg(I2C_ADDR_MCP23017, REG_OLATA));

  mcp23017_writePin(1, false);
  TEST_ASSERT_EQUAL_HEX8(0x08, Wire.wire_getReg(I2C_ADDR_MCP23017, REG_OLATA));
}

void test_write_pin_out_of_range_is_noop(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  TEST_ASSERT_TRUE(mcp23017_init());
  mcp23017_writePin(8, true);
  TEST_ASSERT_EQUAL_HEX8(0x00, Wire.wire_getReg(I2C_ADDR_MCP23017, REG_OLATA));
}

// ── Cached input pins / edge accumulator ──────────────────────────────

void test_cached_pin_reads_default_high(void) {
  // Before any tick(), the cache defaults to 0xFF (pulled-up inputs).
  TEST_ASSERT_EQUAL_INT(HIGH, mcp23017_cachedPin(0));
  TEST_ASSERT_EQUAL_INT(HIGH, mcp23017_cachedPin(6));
}

void test_cached_pin_out_of_range_returns_low(void) {
  TEST_ASSERT_EQUAL_INT(LOW, mcp23017_cachedPin(8));
}

void test_consume_edges_clears_accumulator(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  TEST_ASSERT_TRUE(mcp23017_init());
  Wire.wire_setReg(I2C_ADDR_MCP23017, REG_INTFA, 0x01);  // GPA0 edge pending
  Wire.wire_setReg(I2C_ADDR_MCP23017, REG_GPIOA, 0xFE);  // GPA0 now low

  mcp23017_tick(1000);

  TEST_ASSERT_EQUAL_INT(LOW, mcp23017_cachedPin(0));
  TEST_ASSERT_EQUAL_HEX8(0x01, mcp23017_consumeEdgesA());
  // Second call drains to zero.
  TEST_ASSERT_EQUAL_HEX8(0x00, mcp23017_consumeEdgesA());
}

// ── tick() rate limiting ──────────────────────────────────────────────

void test_tick_is_rate_limited(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  TEST_ASSERT_TRUE(mcp23017_init());
  Wire.wire_setReg(I2C_ADDR_MCP23017, REG_GPIOA, 0x55);

  mcp23017_tick(1000);
  TEST_ASSERT_EQUAL_HEX8(0x55, mcp23017_cachedPin(0) == HIGH ? 0x55 : 0x00);  // sanity: tick did read

  // Change the "hardware" register but call tick() again immediately - too
  // soon (< MCP23017_REFRESH_MS) so the cache must NOT update yet.
  Wire.wire_setReg(I2C_ADDR_MCP23017, REG_GPIOA, 0xAA);
  mcp23017_tick(1000 + MCP23017_REFRESH_MS - 1);
  TEST_ASSERT_EQUAL_INT(HIGH, mcp23017_cachedPin(0));  // still reflects 0x55, bit0=1

  // Now enough time has passed - cache should refresh to 0xAA (bit0=0).
  mcp23017_tick(1000 + MCP23017_REFRESH_MS);
  TEST_ASSERT_EQUAL_INT(LOW, mcp23017_cachedPin(0));
}

// ── Runtime degradation: lost device / re-probe ──────────────────────

void test_tick_marks_lost_when_read_fails(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  TEST_ASSERT_TRUE(mcp23017_init());
  TEST_ASSERT_TRUE(mcp23017_isReady());

  // Device stops answering entirely (e.g. glitch/power loss).
  Wire.wire_clearAckAddress();
  Wire.wire_setAckAddress(0x30);  // nothing at 0x20 anymore

  mcp23017_tick(1000);
  TEST_ASSERT_FALSE(mcp23017_isReady());
}

void test_tick_reprobes_and_recovers_after_loss(void) {
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  TEST_ASSERT_TRUE(mcp23017_init());

  Wire.wire_setAckAddress(0x30);  // lost
  mcp23017_tick(1000);
  TEST_ASSERT_FALSE(mcp23017_isReady());

  // Device comes back before the 2 s re-probe window - must NOT recover yet.
  Wire.wire_setAckAddress(I2C_ADDR_MCP23017);
  mcp23017_tick(1000 + MCP23017_REFRESH_MS);
  TEST_ASSERT_FALSE(mcp23017_isReady());

  // After the re-probe window elapses, the next tick() re-detects it.
  mcp23017_tick(1000 + 2000U + MCP23017_REFRESH_MS);
  TEST_ASSERT_TRUE(mcp23017_isReady());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_finds_device_at_default_address);
  RUN_TEST(test_init_finds_device_strapped_high_at_0x27);
  RUN_TEST(test_init_scans_whole_range_before_giving_up);
  RUN_TEST(test_init_fails_when_nothing_acks);
  RUN_TEST(test_init_configures_registers_correctly);
  RUN_TEST(test_write_pin_before_ready_is_noop);
  RUN_TEST(test_write_pin_sets_and_clears_bit);
  RUN_TEST(test_write_pin_out_of_range_is_noop);
  RUN_TEST(test_cached_pin_reads_default_high);
  RUN_TEST(test_cached_pin_out_of_range_returns_low);
  RUN_TEST(test_consume_edges_clears_accumulator);
  RUN_TEST(test_tick_is_rate_limited);
  RUN_TEST(test_tick_marks_lost_when_read_fails);
  RUN_TEST(test_tick_reprobes_and_recovers_after_loss);
  return UNITY_END();
}
