#include <unity.h>
#include <stdint.h>
#include <string.h>

// Self-contained tests for MKS SERVO42C-MT UART protocol primitives.
// These mirror motor_hal_mks_uart.h logic without pulling in Arduino.h or
// Serial1 — pure protocol math (CRC, frame layout, byte order).
//
// If motor_hal_mks_uart.h changes the CRC or frame layout, update these too.

// ── CRC8 = sum of preceding bytes & 0xFF ────────────────────────────

static uint8_t crc8(const uint8_t* buf, uint8_t len) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < len; i++) sum += buf[i];
  return (uint8_t)(sum & 0xFF);
}

// ── Frame builders (downlink: [addr][func][data][crc]) ──────────────

static uint8_t build_read_encoder(uint8_t addr, uint8_t* out) {
  out[0] = addr;
  out[1] = 0x30;
  out[2] = crc8(out, 2);
  return 3;
}

static uint8_t build_enable(uint8_t addr, bool enable, uint8_t* out) {
  out[0] = addr;
  out[1] = 0xF3;
  out[2] = enable ? 0x01 : 0x00;
  out[3] = crc8(out, 3);
  return 4;
}

static uint8_t build_run_pulses(uint8_t addr, int32_t pulses, uint8_t speed, uint8_t* out) {
  bool reverse = (pulses < 0);
  uint32_t p = (uint32_t)(reverse ? -pulses : pulses);
  if (speed > 0x7F) speed = 0x7F;
  uint8_t val = (reverse ? 0x80 : 0x00) | (speed & 0x7F);
  out[0] = addr;
  out[1] = 0xFD;
  out[2] = val;
  out[3] = (uint8_t)((p >> 24) & 0xFF);
  out[4] = (uint8_t)((p >> 16) & 0xFF);
  out[5] = (uint8_t)((p >>  8) & 0xFF);
  out[6] = (uint8_t)( p        & 0xFF);
  out[7] = crc8(out, 7);
  return 8;
}

static uint8_t build_stop(uint8_t addr, uint8_t* out) {
  out[0] = addr;
  out[1] = 0xF7;
  out[2] = crc8(out, 2);
  return 3;
}

// ── Frame validator (uplink: [addr][data...][crc]) ──────────────────

static bool validate_frame(const uint8_t* buf, uint8_t len, uint8_t expectedAddr) {
  if (len < 3) return false;
  if (buf[0] != expectedAddr) return false;
  return crc8(buf, len - 1) == buf[len - 1];
}

// ── Encoder decoder (uplink 8 bytes: [addr][carry int32 BE][value u16 BE][crc]) ──

static int64_t decode_encoder(const uint8_t* buf) {
  int32_t carry =
    ((int32_t)buf[1] << 24) |
    ((int32_t)buf[2] << 16) |
    ((int32_t)buf[3] <<  8) |
    ((int32_t)buf[4]);
  uint16_t value = ((uint16_t)buf[5] << 8) | buf[6];
  return (int64_t)carry * (int64_t)0x4000 + (int64_t)value;
}

void setUp()    {}
void tearDown() {}

// ── CRC tests ────────────────────────────────────────────────────────

void test_crc_read_encoder_E0_30() {
  // Manuaalin esimerkki: E0 + 30 = 0x110 → CRC = 0x10
  uint8_t buf[2] = { 0xE0, 0x30 };
  TEST_ASSERT_EQUAL_UINT8(0x10, crc8(buf, 2));
}

void test_crc_overflow_byte_wrap() {
  // sum > 0xFF → wrap by & 0xFF
  uint8_t buf[4] = { 0xFF, 0xFF, 0x01, 0x02 };
  // sum = 0x201 → 0x01
  TEST_ASSERT_EQUAL_UINT8(0x01, crc8(buf, 4));
}

void test_crc_empty() {
  uint8_t buf[1] = { 0xE0 };
  TEST_ASSERT_EQUAL_UINT8(0xE0, crc8(buf, 1));
}

void test_crc_known_run_pulses() {
  // Manuaalin Part 6.4 esimerkki: E0 FD 02 00 00 0C 80 → ajaa 3200 pulssia fwd.
  // sum = 0xE0+0xFD+0x02+0x00+0x00+0x0C+0x80 = 619 → 619 & 0xFF = 0x6B.
  // (Manuaalissa painettu CRC 0x80 on typo — vertaa kaavaan.)
  uint8_t buf[7] = { 0xE0, 0xFD, 0x02, 0x00, 0x00, 0x0C, 0x80 };
  TEST_ASSERT_EQUAL_UINT8(0x6B, crc8(buf, 7));
}

// ── Frame builder tests ──────────────────────────────────────────────

void test_build_read_encoder() {
  uint8_t buf[8];
  uint8_t len = build_read_encoder(0xE0, buf);
  TEST_ASSERT_EQUAL_UINT8(3, len);
  TEST_ASSERT_EQUAL_UINT8(0xE0, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0x30, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0x10, buf[2]);
}

void test_build_enable_on() {
  uint8_t buf[8];
  uint8_t len = build_enable(0xE0, true, buf);
  TEST_ASSERT_EQUAL_UINT8(4, len);
  TEST_ASSERT_EQUAL_UINT8(0xE0, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0xF3, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0x01, buf[2]);
  // sum = 0xE0 + 0xF3 + 0x01 = 0x1D4 → 0xD4
  TEST_ASSERT_EQUAL_UINT8(0xD4, buf[3]);
}

void test_build_enable_off() {
  uint8_t buf[8];
  build_enable(0xE0, false, buf);
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[2]);
  // sum = 0xE0 + 0xF3 + 0x00 = 0x1D3 → 0xD3
  TEST_ASSERT_EQUAL_UINT8(0xD3, buf[3]);
}

void test_build_run_pulses_forward() {
  // Yksi kierros 1.8° MStep=16 = 3200 pulses, speed 2, forward.
  // Odotettu: E0 FD 02 00 00 0C 80 6B  (CRC = 619 & 0xFF, ks. test_crc_known_run_pulses)
  uint8_t buf[10];
  uint8_t len = build_run_pulses(0xE0, 3200, 2, buf);
  TEST_ASSERT_EQUAL_UINT8(8, len);
  TEST_ASSERT_EQUAL_UINT8(0xE0, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0xFD, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02, buf[2]);  // dir=fwd, speed=2
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[3]);  // pulses BE byte 3
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[4]);  // pulses BE byte 2
  TEST_ASSERT_EQUAL_UINT8(0x0C, buf[5]);  // pulses BE byte 1
  TEST_ASSERT_EQUAL_UINT8(0x80, buf[6]);  // pulses BE byte 0 (3200 = 0x0C80)
  TEST_ASSERT_EQUAL_UINT8(0x6B, buf[7]);  // crc
}

void test_build_run_pulses_reverse() {
  // Negatiivinen → bit7 (0x80) asetettu VAL-tavuun, pulses absoluuttisena.
  uint8_t buf[10];
  build_run_pulses(0xE0, -3200, 2, buf);
  TEST_ASSERT_EQUAL_UINT8(0x82, buf[2]);  // dir=rev (0x80) | speed=2
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[3]);
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[4]);
  TEST_ASSERT_EQUAL_UINT8(0x0C, buf[5]);
  TEST_ASSERT_EQUAL_UINT8(0x80, buf[6]);
}

void test_build_run_pulses_speed_clamp() {
  // speed > 0x7F → clamp ettei sotke dir-bittiä
  uint8_t buf[10];
  build_run_pulses(0xE0, 100, 0xFF, buf);
  TEST_ASSERT_EQUAL_UINT8(0x7F, buf[2]);  // fwd | speed=0x7F (ei 0xFF)
}

void test_build_stop() {
  uint8_t buf[8];
  uint8_t len = build_stop(0xE0, buf);
  TEST_ASSERT_EQUAL_UINT8(3, len);
  TEST_ASSERT_EQUAL_UINT8(0xF7, buf[1]);
  // sum = 0xE0 + 0xF7 = 0x1D7 → 0xD7
  TEST_ASSERT_EQUAL_UINT8(0xD7, buf[2]);
}

// ── Frame validator tests ────────────────────────────────────────────

void test_validate_frame_ok_3byte() {
  // F3-vastaus: addr + status + crc
  uint8_t buf[3] = { 0xE0, 0x01, 0xE1 };  // 0xE0 + 0x01 = 0xE1
  TEST_ASSERT_TRUE(validate_frame(buf, 3, 0xE0));
}

void test_validate_frame_bad_addr() {
  uint8_t buf[3] = { 0xE1, 0x01, 0xE2 };
  TEST_ASSERT_FALSE(validate_frame(buf, 3, 0xE0));
}

void test_validate_frame_bad_crc() {
  uint8_t buf[3] = { 0xE0, 0x01, 0xFF };  // crc väärä
  TEST_ASSERT_FALSE(validate_frame(buf, 3, 0xE0));
}

void test_validate_frame_too_short() {
  uint8_t buf[2] = { 0xE0, 0x01 };
  TEST_ASSERT_FALSE(validate_frame(buf, 2, 0xE0));
}

// ── Encoder decoder tests ────────────────────────────────────────────

void test_decode_encoder_zero() {
  uint8_t buf[8] = { 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  // CRC ei tarvitse olla validi tässä testissä — testaa vain dekoodaus
  TEST_ASSERT_EQUAL_INT64(0, decode_encoder(buf));
}

void test_decode_encoder_value_only() {
  // carry=0, value=0x4000 (yksi kierros) → 0x4000
  uint8_t buf[8] = { 0xE0, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x20 };
  TEST_ASSERT_EQUAL_INT64(0x4000, decode_encoder(buf));
}

void test_decode_encoder_carry_one() {
  // carry=1, value=0 → 0x4000 (täsmälleen yksi kierros)
  uint8_t buf[8] = { 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };
  TEST_ASSERT_EQUAL_INT64(0x4000, decode_encoder(buf));
}

void test_decode_encoder_carry_plus_value() {
  // carry=2, value=0x1234 → 2*0x4000 + 0x1234 = 0x9234
  uint8_t buf[8] = { 0xE0, 0x00, 0x00, 0x00, 0x02, 0x12, 0x34, 0x00 };
  TEST_ASSERT_EQUAL_INT64(0x9234, decode_encoder(buf));
}

void test_decode_encoder_negative_carry() {
  // carry=-1 (0xFFFFFFFF), value=0 → -0x4000
  uint8_t buf[8] = { 0xE0, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00 };
  TEST_ASSERT_EQUAL_INT64(-0x4000, decode_encoder(buf));
}

// ── MKSU_STEPS_PER_REV math (sanity) ─────────────────────────────────
// MKSU_STEPS_PER_REV = MOTOR_STEPS_PER_REV * MKS_UART_MICROSTEPS = 200 * 16 = 3200
// MOTOR_LEAD_MM = 8 → 1 mm = 400 steps (sama kuin TMC2208 16us)

void test_mksu_mm_to_steps_one_mm() {
  const long STEPS_PER_REV = 200;
  const long MICROSTEPS = 16;
  const long LEAD_MM = 8;
  long steps = 1 * STEPS_PER_REV * MICROSTEPS / LEAD_MM;
  TEST_ASSERT_EQUAL_INT32(400, steps);
}

void test_mksu_mm_to_steps_one_rev() {
  // Yksi kierros pituussuunnassa = 8 mm = 3200 steps
  const long STEPS_PER_REV = 200;
  const long MICROSTEPS = 16;
  const long LEAD_MM = 8;
  long steps = 8 * STEPS_PER_REV * MICROSTEPS / LEAD_MM;
  TEST_ASSERT_EQUAL_INT32(3200, steps);
}

// ── main ─────────────────────────────────────────────────────────────

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc_read_encoder_E0_30);
  RUN_TEST(test_crc_overflow_byte_wrap);
  RUN_TEST(test_crc_empty);
  RUN_TEST(test_crc_known_run_pulses);
  RUN_TEST(test_build_read_encoder);
  RUN_TEST(test_build_enable_on);
  RUN_TEST(test_build_enable_off);
  RUN_TEST(test_build_run_pulses_forward);
  RUN_TEST(test_build_run_pulses_reverse);
  RUN_TEST(test_build_run_pulses_speed_clamp);
  RUN_TEST(test_build_stop);
  RUN_TEST(test_validate_frame_ok_3byte);
  RUN_TEST(test_validate_frame_bad_addr);
  RUN_TEST(test_validate_frame_bad_crc);
  RUN_TEST(test_validate_frame_too_short);
  RUN_TEST(test_decode_encoder_zero);
  RUN_TEST(test_decode_encoder_value_only);
  RUN_TEST(test_decode_encoder_carry_one);
  RUN_TEST(test_decode_encoder_carry_plus_value);
  RUN_TEST(test_decode_encoder_negative_carry);
  RUN_TEST(test_mksu_mm_to_steps_one_mm);
  RUN_TEST(test_mksu_mm_to_steps_one_rev);
  return UNITY_END();
}
