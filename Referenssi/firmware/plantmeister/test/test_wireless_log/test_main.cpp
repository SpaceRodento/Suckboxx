#include <unity.h>

// ── Feature flags ─────────────────────────────────────────────────
// UNIT_TEST: pakottaa wireless_log.h:n transport-puolen (WiFiUDP) pois,
// jolloin wlog_write() vain puskuroi ring-bufferiin.
#define UNIT_TEST 1

// Pieni buffer jotta overflow on helppo testata.
#define WIRELESS_LOG_BUFFER_SIZE 16

// Pakota wireless log paalle riippumatta projektin oletuksesta.
#define ENABLE_WIRELESS_LOG true

#include "test_feature_flags.h"

#include "wireless_log.h"

void setUp()    { wlog_ring_reset(); }
void tearDown() {}

// ── Tests ─────────────────────────────────────────────────────────

void test_empty_ring_count_zero() {
  TEST_ASSERT_EQUAL_UINT16(0, wlog_ring_count());
  uint8_t dst[8] = {0};
  uint16_t n = wlog_ring_pop(dst, sizeof(dst));
  TEST_ASSERT_EQUAL_UINT16(0, n);
  TEST_ASSERT_EQUAL_UINT32(0, wlog_ring_dropped());
}

void test_push_pop_basic() {
  const char* msg = "hello";
  wlog_ring_pushBytes((const uint8_t*)msg, 5);
  TEST_ASSERT_EQUAL_UINT16(5, wlog_ring_count());

  uint8_t dst[8] = {0};
  uint16_t n = wlog_ring_pop(dst, sizeof(dst));
  TEST_ASSERT_EQUAL_UINT16(5, n);
  TEST_ASSERT_EQUAL_UINT16(0, wlog_ring_count());
  TEST_ASSERT_EQUAL_MEMORY("hello", dst, 5);
}

void test_overflow_drops_oldest() {
  // Pushaa 20 tavua: '0'..'9','A'..'J' (16-rajan yli)
  uint8_t src[20];
  for (int i = 0; i < 10; i++) src[i] = (uint8_t)('0' + i);
  for (int i = 0; i < 10; i++) src[10 + i] = (uint8_t)('A' + i);
  wlog_ring_pushBytes(src, 20);

  // Buffer taynna (16), 4 tavua dropattu (vanhimmat).
  TEST_ASSERT_EQUAL_UINT16(16, wlog_ring_count());
  TEST_ASSERT_EQUAL_UINT32(4, wlog_ring_dropped());

  uint8_t dst[32] = {0};
  uint16_t n = wlog_ring_pop(dst, sizeof(dst));
  TEST_ASSERT_EQUAL_UINT16(16, n);
  // Odotetaan viimeiset 16 tavua: '4'..'9','A'..'J'
  TEST_ASSERT_EQUAL_MEMORY("456789ABCDEFGHIJ", dst, 16);
}

void test_wraparound() {
  // Pushaa 10 tavua, pop 10 -> head/tail kierahtavat ennen seuraavaa kirjoitusta.
  uint8_t first[10];
  for (int i = 0; i < 10; i++) first[i] = (uint8_t)('a' + i);
  wlog_ring_pushBytes(first, 10);

  uint8_t drain[10] = {0};
  uint16_t n = wlog_ring_pop(drain, sizeof(drain));
  TEST_ASSERT_EQUAL_UINT16(10, n);
  TEST_ASSERT_EQUAL_UINT16(0, wlog_ring_count());

  // Pushaa toiset 10 - tail kayttaa wrap-aroundin (10 -> wrap -> 4).
  uint8_t second[10];
  for (int i = 0; i < 10; i++) second[i] = (uint8_t)('A' + i);
  wlog_ring_pushBytes(second, 10);

  TEST_ASSERT_EQUAL_UINT16(10, wlog_ring_count());
  TEST_ASSERT_EQUAL_UINT32(0, wlog_ring_dropped());

  uint8_t dst[10] = {0};
  uint16_t n2 = wlog_ring_pop(dst, sizeof(dst));
  TEST_ASSERT_EQUAL_UINT16(10, n2);
  TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHIJ", dst, 10);
}

void test_wlog_write_no_wifi_buffers() {
  wlog_write("test");
  TEST_ASSERT_EQUAL_UINT16(4, wlog_ring_count());

  uint8_t dst[8] = {0};
  uint16_t n = wlog_ring_pop(dst, sizeof(dst));
  TEST_ASSERT_EQUAL_UINT16(4, n);
  TEST_ASSERT_EQUAL_MEMORY("test", dst, 4);
}

void test_wlog_writeln_appends_newline() {
  wlog_writeln("ab");
  TEST_ASSERT_EQUAL_UINT16(3, wlog_ring_count());

  uint8_t dst[8] = {0};
  uint16_t n = wlog_ring_pop(dst, sizeof(dst));
  TEST_ASSERT_EQUAL_UINT16(3, n);
  TEST_ASSERT_EQUAL_UINT8('a',  dst[0]);
  TEST_ASSERT_EQUAL_UINT8('b',  dst[1]);
  TEST_ASSERT_EQUAL_UINT8('\n', dst[2]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_ring_count_zero);
  RUN_TEST(test_push_pop_basic);
  RUN_TEST(test_overflow_drops_oldest);
  RUN_TEST(test_wraparound);
  RUN_TEST(test_wlog_write_no_wifi_buffers);
  RUN_TEST(test_wlog_writeln_appends_newline);
  return UNITY_END();
}
