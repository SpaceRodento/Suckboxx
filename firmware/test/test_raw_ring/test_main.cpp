// RawRing + CsvWriter (docs/firmware.md §4.5, S5b, R3).
//
// Rengas on ensimmäisen moottorikäynnin (M6) ainoa pysyvä tuotos: jos seq
// rekonstruoituu väärin tai CSV katkeaa palan rajalla, data on käyttökelvotonta
// eikä ajoa voi toistaa. Siksi testataan erityisesti 16-bittisen seq_lo:n ja
// 32-bittisen seq:n kierrot, aukot, jäädytys ja palakoon riippumattomuus.

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/raw_ring.h"

using sbx::Calibration;
using sbx::Config;
using sbx::CsvWriter;
using sbx::RawFrame;
using sbx::RawRing;
using sbx::RawSample;

void setUp() {}
void tearDown() {}

static RawFrame frame(uint32_t seq, uint16_t a, uint16_t b, uint16_t c) {
  RawFrame f;
  f.seq = seq;
  f.mv[0] = a;
  f.mv[1] = b;
  f.mv[2] = c;
  return f;
}

// mV-arvot johdetaan seq:stä, jotta jokainen näyte on tunnistettavissa.
static RawFrame frameFor(uint32_t seq) {
  return frame(seq, static_cast<uint16_t>(200 + seq % 2500), static_cast<uint16_t>(1325 + seq % 7),
               static_cast<uint16_t>(2700 - seq % 1000));
}

static void checkContents(const RawRing& r, const uint32_t* expect_seq, size_t n) {
  TEST_ASSERT_EQUAL_size_t(n, r.size());
  for (size_t i = 0; i < n; ++i) {
    RawSample s;
    TEST_ASSERT_TRUE(r.at(i, s));
    TEST_ASSERT_EQUAL_UINT32(expect_seq[i], r.seqOf(s));
    const RawFrame f = frameFor(expect_seq[i]);
    TEST_ASSERT_EQUAL_UINT16(f.mv[0], s.mv[0]);
    TEST_ASSERT_EQUAL_UINT16(f.mv[1], s.mv[1]);
    TEST_ASSERT_EQUAL_UINT16(f.mv[2], s.mv[2]);
  }
  RawSample s;
  TEST_ASSERT_FALSE(r.at(n, s));
}

// --- RawRing -------------------------------------------------------------------

void test_empty_ring() {
  RawSample mem[4];
  RawRing r(mem, 4);
  TEST_ASSERT_EQUAL_size_t(0, r.size());
  TEST_ASSERT_EQUAL_size_t(4, r.capacity());
  TEST_ASSERT_EQUAL_UINT32(0, r.newestSeq());
  TEST_ASSERT_EQUAL_UINT32(0, r.gaps());
  TEST_ASSERT_FALSE(r.frozen());
  RawSample s;
  TEST_ASSERT_FALSE(r.at(0, s));
}

void test_push_below_capacity_oldest_first() {
  RawSample mem[8];
  RawRing r(mem, 8);
  for (uint32_t q = 100; q < 105; ++q) r.push(frameFor(q));
  const uint32_t expect[] = {100, 101, 102, 103, 104};
  checkContents(r, expect, 5);
  TEST_ASSERT_EQUAL_UINT32(104, r.newestSeq());
  TEST_ASSERT_EQUAL_UINT32(0, r.gaps());
}

void test_wraparound_overwrites_oldest() {
  RawSample mem[5];
  RawRing r(mem, 5);
  for (uint32_t q = 0; q < 12; ++q) r.push(frameFor(q));
  const uint32_t expect[] = {7, 8, 9, 10, 11};
  checkContents(r, expect, 5);
  TEST_ASSERT_EQUAL_UINT32(0, r.gaps());
  // Täysi ESP32-kokoinen kierros: 8192 näytettä, 3 kierrosta.
  static RawSample big[8192];
  RawRing rb(big, 8192);
  for (uint32_t q = 0; q < 3u * 8192u + 17u; ++q) rb.push(frameFor(q));
  TEST_ASSERT_EQUAL_size_t(8192, rb.size());
  RawSample s;
  TEST_ASSERT_TRUE(rb.at(0, s));
  TEST_ASSERT_EQUAL_UINT32(3u * 8192u + 17u - 8192u, rb.seqOf(s));
  TEST_ASSERT_TRUE(rb.at(8191, s));
  TEST_ASSERT_EQUAL_UINT32(3u * 8192u + 16u, rb.seqOf(s));
}

void test_seq_lo_16bit_wraparound() {
  // seq_lo kiertää 65535 -> 0 renkaan sisällä (65 s käytön jälkeen).
  RawSample mem[64];
  RawRing r(mem, 64);
  for (uint32_t q = 65500; q < 65550; ++q) r.push(frameFor(q));  // 65535 -> 65536 renkaan keskellä
  uint32_t expect[50];
  for (uint32_t i = 0; i < 50; ++i) expect[i] = 65500u + i;
  checkContents(r, expect, 50);
  TEST_ASSERT_EQUAL_UINT32(0, r.gaps());
  RawSample s;
  r.at(49, s);
  TEST_ASSERT_EQUAL_UINT16(13, s.seq_lo);  // tallennettu vain alin 16 bittiä (65549 & 0xFFFF)
  r.at(0, s);
  TEST_ASSERT_EQUAL_UINT16(65500, s.seq_lo);
  // Jatketaan kunnes rengas on kokonaan kierron yli ja vielä kiertää.
  for (uint32_t q = 65550; q < 65600; ++q) r.push(frameFor(q));
  uint32_t expect2[64];
  for (uint32_t i = 0; i < 64; ++i) expect2[i] = 65536u + i;
  checkContents(r, expect2, 64);
  // Staattinen rekonstruktio: newest 0x00010005, lo 0xFFFE -> 0x0000FFFE.
  TEST_ASSERT_EQUAL_UINT32(0x0000FFFEu, RawRing::expandSeq(0x00010005u, 0xFFFEu));
  TEST_ASSERT_EQUAL_UINT32(0x00010005u, RawRing::expandSeq(0x00010005u, 0x0005u));
  TEST_ASSERT_EQUAL_UINT32(0x00000006u, RawRing::expandSeq(0x00010005u, 0x0006u));  // 65535 taaksepäin
}

void test_seq_32bit_wraparound() {
  // 49,7 vuorokauden jälkeen 32-bittinen seq kiertää: ei aukkoa, ei tyhjennystä.
  RawSample mem[16];
  RawRing r(mem, 16);
  for (uint32_t k = 0; k < 16; ++k) r.push(frameFor(0xFFFFFFF8u + k));
  TEST_ASSERT_EQUAL_UINT32(0, r.gaps());
  TEST_ASSERT_EQUAL_size_t(16, r.size());
  uint32_t expect[16];
  for (uint32_t k = 0; k < 16; ++k) expect[k] = 0xFFFFFFF8u + k;
  checkContents(r, expect, 16);
  TEST_ASSERT_EQUAL_UINT32(7, r.newestSeq());
}

void test_gaps_counted_per_discontinuity() {
  RawSample mem[16];
  RawRing r(mem, 16);
  const uint32_t seqs[] = {1, 2, 3, 5, 6, 9, 10, 1000};  // kolme hyppyä, koosta riippumatta
  for (uint32_t q : seqs) r.push(frameFor(q));
  TEST_ASSERT_EQUAL_UINT32(3, r.gaps());
  checkContents(r, seqs, 8);  // aukot eivät pudota dataa
}

void test_backwards_seq_restarts_recording() {
  // Lähteen vaihto (sim -> ADC) aloittaa seq:n alusta: vanha data on eri
  // aikasarjaa eikä sen seq olisi rekonstruoitavissa.
  RawSample mem[16];
  RawRing r(mem, 16);
  for (uint32_t q = 100; q < 110; ++q) r.push(frameFor(q));
  for (uint32_t q = 0; q < 5; ++q) r.push(frameFor(q));
  const uint32_t expect[] = {0, 1, 2, 3, 4};
  checkContents(r, expect, 5);
  TEST_ASSERT_EQUAL_UINT32(1, r.gaps());
  // Sama seq kahdesti = myös uudelleenkäynnistys.
  r.push(frameFor(4));
  TEST_ASSERT_EQUAL_size_t(1, r.size());
  TEST_ASSERT_EQUAL_UINT32(2, r.gaps());
}

void test_long_jump_drops_unreconstructable_samples() {
  RawSample mem[1000];
  {
    RawRing r(mem, 1000);
    for (uint32_t q = 0; q < 100; ++q) r.push(frameFor(q));
    r.push(frameFor(70000));  // > 65535 eteenpäin: vanhat eivät enää yksilöidy
    const uint32_t expect[] = {70000};
    checkContents(r, expect, 1);
    TEST_ASSERT_EQUAL_UINT32(1, r.gaps());
  }
  {
    RawRing r(mem, 1000);
    for (uint32_t q = 0; q < 100; ++q) r.push(frameFor(q));
    for (uint32_t q = 60000; q <= 65600; ++q) r.push(frameFor(q));
    TEST_ASSERT_EQUAL_UINT32(1, r.gaps());
    // Rengas täynnä (1000): vanhat 0..99 ovat jo pyörineet pois kapasiteetin takia.
    RawSample s;
    r.at(0, s);
    TEST_ASSERT_EQUAL_UINT32(65600u - 999u, r.seqOf(s));
  }
  {
    // Iso rengas, jossa vanhat jäävät kapasiteetin puolesta: ne putoavat
    // vasta kun ikkuna (65535) ylittyy, ja jäljelle jäävät rekonstruoituvat oikein.
    static RawSample bigmem[8192];
    RawRing r(bigmem, 8192);
    for (uint32_t q = 0; q < 100; ++q) r.push(frameFor(q));
    for (uint32_t q = 60000; q < 60000 + 5601; ++q) r.push(frameFor(q));  // ...65600
    // Säilyvät vanhat: seq ≥ 65600 − 65535 = 65 -> 65..99 (35 kpl) + 5601 uutta.
    TEST_ASSERT_EQUAL_size_t(35u + 5601u, r.size());
    RawSample s;
    r.at(0, s);
    TEST_ASSERT_EQUAL_UINT32(65, r.seqOf(s));
    r.at(34, s);
    TEST_ASSERT_EQUAL_UINT32(99, r.seqOf(s));
    r.at(35, s);
    TEST_ASSERT_EQUAL_UINT32(60000, r.seqOf(s));
    TEST_ASSERT_EQUAL_UINT16(frameFor(60000).mv[0], s.mv[0]);
  }
}

void test_capacity_above_seq_window() {
  // PC-testeissä iso kapasiteetti: tehollinen koko rajautuu 65536:een,
  // koska 16-bittinen seq ei yksilöi pidempää ikkunaa.
  static RawSample mem[70000];
  RawRing r(mem, 70000);
  for (uint32_t q = 0; q < 70000; ++q) r.push(frameFor(q));
  TEST_ASSERT_EQUAL_size_t(65536, r.size());
  RawSample s;
  r.at(0, s);
  TEST_ASSERT_EQUAL_UINT32(70000u - 65536u, r.seqOf(s));
  r.at(65535, s);
  TEST_ASSERT_EQUAL_UINT32(69999, r.seqOf(s));
  TEST_ASSERT_EQUAL_UINT32(0, r.gaps());
}

void test_freeze_unfreeze() {
  RawSample mem[16];
  RawRing r(mem, 16);
  for (uint32_t q = 0; q < 10; ++q) r.push(frameFor(q));
  r.freeze();
  TEST_ASSERT_TRUE(r.frozen());
  for (uint32_t q = 10; q < 40; ++q) r.push(frameFor(q));  // CSV-latauksen aikana
  uint32_t expect[10];
  for (uint32_t i = 0; i < 10; ++i) expect[i] = i;
  checkContents(r, expect, 10);
  TEST_ASSERT_EQUAL_UINT32(9, r.newestSeq());
  r.unfreeze();
  TEST_ASSERT_FALSE(r.frozen());
  r.push(frameFor(40));
  TEST_ASSERT_EQUAL_size_t(11, r.size());
  // Tahallinen jäädytysaukko ei ole samplerin vika (diag.gaps).
  TEST_ASSERT_EQUAL_UINT32(0, r.gaps());
  r.push(frameFor(42));  // oikea pudonnut näyte lasketaan
  TEST_ASSERT_EQUAL_UINT32(1, r.gaps());
  // Jäädytys ilman pudotettuja näytteitä ei piilota seuraavaa oikeaa aukkoa.
  r.freeze();
  r.unfreeze();
  r.push(frameFor(44));
  TEST_ASSERT_EQUAL_UINT32(2, r.gaps());
}

void test_freeze_is_reference_counted() {
  // Kaksi rinnakkaista latausta (tuplanapautus "Tallenna", selaimen
  // uusintapyyntö): ensimmäisen vapautus ei saa vapauttaa toisen jäädytystä.
  RawSample mem[16];
  RawRing r(mem, 16);
  for (uint32_t q = 0; q < 10; ++q) r.push(frameFor(q));
  const uint32_t v0 = r.version();
  r.freeze();  // lataus A
  r.freeze();  // lataus B
  r.unfreeze();  // A valmis
  TEST_ASSERT_TRUE(r.frozen());
  r.push(frameFor(10));
  TEST_ASSERT_EQUAL_size_t(10, r.size());
  TEST_ASSERT_EQUAL_UINT32(9, r.newestSeq());
  TEST_ASSERT_EQUAL_UINT32(v0, r.version());  // jäädytettynä mikään ei muutu
  r.unfreeze();  // B valmis
  TEST_ASSERT_FALSE(r.frozen());
  // Ylimääräinen unfreeze (esim. katkenneen yhteyden siivous kahdesti) on
  // ei-op: laskuri ei mene "velaksi", joten seuraava jäädytys toimii heti.
  r.unfreeze();
  TEST_ASSERT_FALSE(r.frozen());
  r.freeze();
  TEST_ASSERT_TRUE(r.frozen());
  r.push(frameFor(11));
  TEST_ASSERT_EQUAL_size_t(10, r.size());
  r.unfreeze();
  TEST_ASSERT_FALSE(r.frozen());
  r.push(frameFor(12));
  TEST_ASSERT_EQUAL_size_t(11, r.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.gaps());  // jäädytyksen pudotukset eivät ole aukkoja
  TEST_ASSERT_TRUE(r.version() != v0);
}

void test_clear_keeps_gap_counter() {
  RawSample mem[8];
  RawRing r(mem, 8);
  r.push(frameFor(1));
  r.push(frameFor(3));
  TEST_ASSERT_EQUAL_UINT32(1, r.gaps());
  r.clear();
  TEST_ASSERT_EQUAL_size_t(0, r.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.gaps());
  r.push(frameFor(0));  // tyhjennyksen jälkeen ei verrata vanhaan
  TEST_ASSERT_EQUAL_UINT32(1, r.gaps());
  TEST_ASSERT_EQUAL_size_t(1, r.size());
}

void test_null_storage_is_harmless() {
  RawRing r(nullptr, 100);
  r.push(frameFor(1));
  r.push(frameFor(5));
  TEST_ASSERT_EQUAL_size_t(0, r.size());
  TEST_ASSERT_EQUAL_size_t(0, r.capacity());
  RawSample s;
  TEST_ASSERT_FALSE(r.at(0, s));
  RawSample one[1];
  RawRing r1(one, 1);
  for (uint32_t q = 0; q < 5; ++q) r1.push(frameFor(q));
  const uint32_t expect[] = {4};
  checkContents(r1, expect, 1);
}

// --- CsvWriter ---------------------------------------------------------------------

static size_t drain(CsvWriter& w, char* out, size_t out_cap, size_t chunk) {
  // Jokainen pala omaan täsmälleen max_len-kokoiseen keon puskuriin, jolloin
  // ASan kaataa testin yhdestäkin yli kirjoitetusta tavusta.
  size_t total = 0;
  bool last_short = false;
  for (;;) {
    char* piece = new char[chunk];
    const size_t n = w.fill(piece, chunk);
    if (n > 0) {
      TEST_ASSERT_FALSE_MESSAGE(last_short, "vajaa pala ennen loppua");
      TEST_ASSERT_LESS_OR_EQUAL(chunk, n);
      TEST_ASSERT_LESS_OR_EQUAL(out_cap, total + n);
      std::memcpy(out + total, piece, n);
      total += n;
      last_short = n < chunk;
    }
    delete[] piece;
    if (n == 0) break;
  }
  char dummy[4];
  TEST_ASSERT_EQUAL_size_t(0, w.fill(dummy, sizeof(dummy)));  // valmis pysyy valmiina
  TEST_ASSERT_TRUE(w.done());
  return total;
}

void test_csv_exact_small() {
  RawSample mem[8];
  RawRing r(mem, 8);
  // docs/rauta.md §1.2: 1325 mV = −55 kPa, 2700 = 0, 200 = −100, 825 = −75, 2500 = −8.
  r.push(frame(1000, 1325, 2700, 200));
  r.push(frame(1001, 1325, 1325, 1325));
  r.push(frame(1003, 2700, 825, 2500));  // yksi pudonnut näyte
  const Config cfg;
  const Calibration cal;
  CsvWriter w(r, cfg, cal);
  static char out[4096];
  const size_t n = drain(w, out, sizeof(out), 100000);
  out[n] = '\0';
  const char* expect =
      "# Suckboxx raakadata (S5b)\n"
      "# device=suckboxx\n"
      "# format=1\n"
      "# source=sim\n"
      "# fs_hz=1000\n"
      "# v_zero_mv=2700\n"
      "# mv_per_kpa=25\n"
      "# rail_comp=0\n"
      "# rail_nominal_mv=3300\n"
      "# zero_valid=0\n"
      "# gain_valid=0\n"
      "# offset_kpa=0,0,0\n"
      "# gain=1,1,1\n"
      "# samples=3\n"
      "# first_seq=1000\n"
      "# gaps=1\n"
      "# gaps_total=1\n"
      "# kpa=gain*(mv-v_zero_mv)/mv_per_kpa+offset (ei kiskokorjausta)\n"
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa\n"
      "1000,0,1325,2700,200,-55.000,0.000,-100.000\n"
      "1001,1,1325,1325,1325,-55.000,-55.000,-55.000\n"
      "1003,3,2700,825,2500,0.000,-75.000,-8.000\n";
  TEST_ASSERT_TRUE_MESSAGE(std::strcmp(expect, out) == 0, out);
}

void test_csv_uses_calibration_and_sensor_params() {
  RawSample mem[4];
  RawRing r(mem, 4);
  r.push(frame(7, 1325, 2700, 200));
  Config cfg;
  cfg.sensor.mv_per_kpa = 24.0f;  // 5 V -anturi jakajalla
  cfg.source = sbx::SourceKind::Adc;
  Calibration cal;
  cal.gain[0] = 1.1f;
  cal.gain[1] = 0.9f;
  cal.offset_kpa[0] = -2.0f;
  cal.offset_kpa[1] = 1.0f;
  cal.offset_kpa[2] = 0.5f;
  cal.zero_valid = true;
  cal.gain_valid = true;
  CsvWriter w(r, cfg, cal);
  static char out[4096];
  const size_t n = drain(w, out, sizeof(out), 64);
  out[n] = '\0';
  TEST_ASSERT_NOT_NULL(std::strstr(out, "# source=adc\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "# mv_per_kpa=24\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "# zero_valid=1\n# gain_valid=1\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "# offset_kpa=-2,1,0.5\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "# gain=1.1,0.9,1\n"));
  // 1,1·(1325−2700)/24 − 2 = −65,0208; 0,9·0 + 1 = 1; (200−2700)/24 + 0,5 = −103,6667
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\n7,0,1325,2700,200,-65.021,1.000,-103.667\n"));
  // Asetusten muutos latauksen aikana ei sotke jo luotua kirjoitinta (kopio).
  cfg.sensor.mv_per_kpa = 25.0f;
  cal.gain[0] = 1.0f;
  CsvWriter w2(r, Config(), Calibration());
  static char out2[4096];
  const size_t n2 = drain(w2, out2, sizeof(out2), 7);
  out2[n2] = '\0';
  TEST_ASSERT_NOT_NULL(std::strstr(out2, "\n7,0,1325,2700,200,-55.000,0.000,-100.000\n"));
}

void test_csv_empty_ring() {
  RawSample mem[4];
  RawRing r(mem, 4);
  CsvWriter w(r, Config(), Calibration());
  static char out[4096];
  const size_t n = drain(w, out, sizeof(out), 1);
  out[n] = '\0';
  TEST_ASSERT_NOT_NULL(std::strstr(out, "# samples=0\n# first_seq=0\n"));
  const char* tail = "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa\n";
  TEST_ASSERT_EQUAL_STRING(tail, out + n - std::strlen(tail));
}

// Täysi ESP32-rengas: 8192 näytettä, seq_lo kiertää, yksi aukko ikkunassa ja
// yksi jo pois pyörinyt aukko.
static RawSample g_big[8192];
static char g_ref[1 << 20];
static char g_out[1 << 20];

static void fillBigRing(RawRing& r) {
  r.push(frameFor(1));
  r.push(frameFor(5));  // aukko, joka pyörii pois ikkunasta
  uint32_t q = 60000;
  for (int k = 0; k < 9000; ++k) {
    if (k == 7000) q += 3;  // aukko, joka jää ikkunaan
    r.push(frameFor(q++));
  }
}

void test_csv_chunk_size_independent() {
  RawRing r(g_big, 8192);
  fillBigRing(r);
  r.freeze();
  const Config cfg;
  Calibration cal;
  cal.gain[2] = 1.0123f;
  cal.offset_kpa[1] = -0.37f;
  CsvWriter ref(r, cfg, cal);
  const size_t ref_len = drain(ref, g_ref, sizeof(g_ref), 100000);
  TEST_ASSERT_GREATER_THAN(8192u * 30u, ref_len);
  const size_t chunks[] = {1, 7, 64, 1400, 100000};
  for (size_t ch : chunks) {
    CsvWriter w(r, cfg, cal);
    const size_t n = drain(w, g_out, sizeof(g_out), ch);
    TEST_ASSERT_EQUAL_size_t(ref_len, n);
    TEST_ASSERT_TRUE_MESSAGE(std::memcmp(g_ref, g_out, n) == 0, "palakoko muutti tulostetta");
  }
  r.unfreeze();
}

void test_csv_parses_back_to_ring_contents() {
  RawRing r(g_big, 8192);
  fillBigRing(r);
  r.freeze();
  const Config cfg;
  Calibration cal;
  cal.gain[0] = 0.97f;
  cal.offset_kpa[0] = 1.25f;
  CsvWriter w(r, cfg, cal);
  const size_t len = drain(w, g_out, sizeof(g_out) - 1, 1400);
  g_out[len] = '\0';

  TEST_ASSERT_NOT_NULL(std::strstr(g_out, "# samples=8192\n"));
  // Kertyvät: 1 -> 5, 5 -> 60000 ja +3-hyppy; ikkunassa vain viimeinen.
  TEST_ASSERT_NOT_NULL(std::strstr(g_out, "# gaps=1\n# gaps_total=3\n"));
  RawSample first{};
  TEST_ASSERT_TRUE(r.at(0, first));
  const uint32_t first_seq = r.seqOf(first);
  char fs[48];
  std::snprintf(fs, sizeof(fs), "# first_seq=%lu\n", static_cast<unsigned long>(first_seq));
  TEST_ASSERT_NOT_NULL(std::strstr(g_out, fs));
  // 2000 näytettä aukon jälkeen (67003…69002) + 6192 ennen (60808…66999):
  // ikkuna ylittää seq_lo:n kierron (65535 -> 0) keskeltä.
  TEST_ASSERT_EQUAL_UINT32(60808, first_seq);
  TEST_ASSERT_EQUAL_UINT32(69002, r.newestSeq());

  // Rivi kerrallaan: '#'-rivit ensin, sitten otsake, sitten täsmälleen 8192 riviä.
  char* p = g_out;
  int meta = 0;
  while (*p == '#') {
    p = std::strchr(p, '\n') + 1;
    ++meta;
  }
  TEST_ASSERT_GREATER_THAN(10, meta);
  const char* hdr = "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa\n";
  TEST_ASSERT_EQUAL_INT(0, std::strncmp(p, hdr, std::strlen(hdr)));
  p += std::strlen(hdr);
  size_t rows = 0;
  uint32_t prev_seq = 0;
  while (*p != '\0') {
    char* eol = std::strchr(p, '\n');
    TEST_ASSERT_NOT_NULL(eol);
    *eol = '\0';
    int commas = 0;
    for (const char* c = p; *c != '\0'; ++c) commas += (*c == ',');
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, commas, p);  // desimaalipilkku rikkoisi sarakkeet
    unsigned long seq = 0, t = 0;
    unsigned m0 = 0, m1 = 0, m2 = 0;
    float k[3] = {0.0f, 0.0f, 0.0f};
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, std::sscanf(p, "%lu,%lu,%u,%u,%u,%f,%f,%f", &seq, &t, &m0, &m1, &m2,
                                                 &k[0], &k[1], &k[2]), p);
    RawSample s;
    TEST_ASSERT_TRUE(r.at(rows, s));
    TEST_ASSERT_EQUAL_UINT32(r.seqOf(s), seq);
    TEST_ASSERT_EQUAL_UINT32(seq - first_seq, t);  // 1 kHz -> t_ms = seq-ero
    if (rows > 0) TEST_ASSERT_GREATER_THAN(prev_seq, seq);
    prev_seq = static_cast<uint32_t>(seq);
    const unsigned mv[3] = {m0, m1, m2};
    for (int c = 0; c < 3; ++c) {
      TEST_ASSERT_EQUAL_UINT16(s.mv[c], mv[c]);
      const float truth = sbx::applyCal(sbx::nominalKpa(s.mv[c], 0, cfg.sensor), c, cal);
      TEST_ASSERT_FLOAT_WITHIN(0.00051f, truth, k[c]);
    }
    ++rows;
    p = eol + 1;
  }
  TEST_ASSERT_EQUAL_size_t(8192, rows);
  r.unfreeze();
}

// Täyttää renkaan kuten 10 s käytön jälkeen: seq 100000…109999.
static void fillRingAt100k(RawRing& r) {
  for (uint32_t q = 100000; q < 110000; ++q) r.push(frameFor(q));
}

void test_csv_two_concurrent_downloads() {
  // Oikein paritettu liima: kumpikin lataus jäädyttää ja vapauttaa omansa.
  // B:n tulos on tavulleen sama kuin A:n, vaikka A vapautti ja sampler jatkoi.
  RawRing r(g_big, 8192);
  fillRingAt100k(r);
  const Config cfg;
  const Calibration cal;
  r.freeze();
  CsvWriter a(r, cfg, cal);
  r.freeze();
  CsvWriter b(r, cfg, cal);
  const size_t n1 = b.fill(g_out, 1400);
  TEST_ASSERT_EQUAL_size_t(1400, n1);
  const size_t ref_len = drain(a, g_ref, sizeof(g_ref), 1400);
  r.unfreeze();  // A valmis
  for (uint32_t q = 110000; q < 113000; ++q) r.push(frameFor(q));  // sampler jatkaa
  const size_t n2 = drain(b, g_out + n1, sizeof(g_out) - n1, 1400);
  TEST_ASSERT_FALSE(b.aborted());
  TEST_ASSERT_FALSE(a.aborted());
  TEST_ASSERT_EQUAL_size_t(ref_len, n1 + n2);
  TEST_ASSERT_TRUE_MESSAGE(std::memcmp(g_ref, g_out, ref_len) == 0, "B poikkesi A:sta");
  TEST_ASSERT_EQUAL_UINT32(109999, r.newestSeq());
  r.unfreeze();  // B valmis
  r.push(frameFor(113000));
  TEST_ASSERT_EQUAL_UINT32(113000, r.newestSeq());
}

// Laskee rivit ja tarkistaa, että seq kasvaa ja t_ms pysyy ikkunan sisällä.
static size_t checkRowsSane(const char* csv, size_t len, uint32_t max_t_ms) {
  const char* hdr = "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa\n";
  const char* p = std::strstr(csv, hdr);
  TEST_ASSERT_NOT_NULL(p);
  p += std::strlen(hdr);
  const char* end = csv + len;
  size_t rows = 0;
  unsigned long prev = 0;
  while (p < end && *p != '#') {
    unsigned long seq = 0, t = 0;
    TEST_ASSERT_EQUAL_INT(2, std::sscanf(p, "%lu,%lu,", &seq, &t));
    if (rows > 0) TEST_ASSERT_TRUE_MESSAGE(seq > prev, "seq hyppäsi taaksepäin");
    TEST_ASSERT_TRUE_MESSAGE(t <= max_t_ms, "t_ms ikkunan ulkopuolella");
    prev = seq;
    ++rows;
    p = std::strchr(p, '\n');
    TEST_ASSERT_NOT_NULL(p);
    ++p;
  }
  return rows;
}

void test_csv_stops_when_ring_changes_mid_export() {
  // Liimavirhe (arvostelun skenaario): kaksi latausta, mutta vain yksi
  // jäädytys, joten A:n unfreeze() vapauttaa renkaan B:n alta ja sampler
  // kirjoittaa 3000 näytettä päälle. B ei saa tuottaa yhtään väärää riviä
  // (ennen korjausta: seq 109999 -> 44464, t_ms 4 240 622 ms 8,2 s:n
  // ikkunassa), vaan lopettaa ja merkitsee katkon.
  RawRing r(g_big, 8192);
  fillRingAt100k(r);
  const Config cfg;
  const Calibration cal;
  r.freeze();
  CsvWriter a(r, cfg, cal);
  CsvWriter b(r, cfg, cal);
  const size_t n1 = b.fill(g_out, 1400);
  const size_t ref_len = drain(a, g_ref, sizeof(g_ref) - 1, 1400);
  g_ref[ref_len] = '\0';
  TEST_ASSERT_FALSE(a.aborted());
  r.unfreeze();
  for (uint32_t q = 110000; q < 113000; ++q) r.push(frameFor(q));
  const size_t n2 = drain(b, g_out + n1, sizeof(g_out) - n1 - 1, 1400);
  const size_t len = n1 + n2;
  g_out[len] = '\0';
  TEST_ASSERT_TRUE(b.aborted());
  TEST_ASSERT_LESS_THAN(ref_len, len);
  // Katkon merkki on viimeinen rivi, ja kaikki sitä ennen on A:n ehjän
  // tulosteen alkuosa (rivirajaan asti).
  const char* mark = std::strstr(g_out, "# aborted=ring_changed rows=");
  TEST_ASSERT_NOT_NULL(mark);
  const size_t prefix = static_cast<size_t>(mark - g_out);
  TEST_ASSERT_EQUAL_INT('\n', g_out[prefix - 1]);
  TEST_ASSERT_TRUE_MESSAGE(std::memcmp(g_ref, g_out, prefix) == 0, "katkennut vienti ei ole A:n alkuosa");
  TEST_ASSERT_TRUE_MESSAGE(std::strchr(mark, '\n') + 1 == g_out + len, "merkin jälkeen tulostetta");
  const size_t rows = checkRowsSane(g_out, len, 8191);
  TEST_ASSERT_GREATER_THAN(0, rows);
  char expect_mark[64];
  std::snprintf(expect_mark, sizeof(expect_mark), "# aborted=ring_changed rows=%lu/8192\n",
                static_cast<unsigned long>(rows));
  TEST_ASSERT_EQUAL_STRING(expect_mark, mark);
}

void test_csv_detects_unfrozen_changes_small() {
  // Jäädytys unohtui kokonaan tai rengas tyhjennettiin (lähteen vaihto)
  // kesken latauksen: rivejä ei tule muuttuneesta renkaasta.
  RawSample mem[4];
  RawRing r(mem, 4);
  for (uint32_t q = 1000; q < 1004; ++q) r.push(frameFor(q));
  static char out[4096];
  {
    CsvWriter w(r, Config(), Calibration());  // ei muutoksia -> ehjä, vaikkei jäädytetty
    const size_t n = drain(w, out, sizeof(out) - 1, 7);
    out[n] = '\0';
    TEST_ASSERT_FALSE(w.aborted());
    TEST_ASSERT_EQUAL_size_t(4, checkRowsSane(out, n, 3));
    TEST_ASSERT_NULL(std::strstr(out, "aborted"));
  }
  {
    CsvWriter w(r, Config(), Calibration());
    char head[16];
    TEST_ASSERT_EQUAL_size_t(sizeof(head), w.fill(head, sizeof(head)));
    r.push(frameFor(1004));  // sampler ehti väliin
    const size_t n = drain(w, out, sizeof(out) - 1, 64);
    out[n] = '\0';
    TEST_ASSERT_TRUE(w.aborted());
    const char* tail = "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa\n"
                       "# aborted=ring_changed rows=0/4\n";
    TEST_ASSERT_EQUAL_STRING(tail, out + n - std::strlen(tail));
  }
  {
    // Luetaan tavu kerrallaan sarakeotsakkeen yli ja keskelle ensimmäistä
    // datariviä, sitten lähde vaihtuu (clear).
    CsvWriter w(r, Config(), Calibration());
    size_t n = 0;
    char c = 0;
    out[0] = '\0';
    while (std::strstr(out, "ch3_kpa\n10") == nullptr) {  // rengas nyt 1001…1004
      TEST_ASSERT_EQUAL_size_t(1, w.fill(&c, 1));
      out[n++] = c;
      out[n] = '\0';
    }
    r.clear();
    n += drain(w, out + n, sizeof(out) - n - 1, 1);
    out[n] = '\0';
    TEST_ASSERT_TRUE(w.aborted());
    // Kesken ollut rivi (muotoiltu ennen muutosta) viedään loppuun, sitten katko.
    const char* tail = "\n# aborted=ring_changed rows=1/4\n";
    TEST_ASSERT_EQUAL_STRING(tail, out + n - std::strlen(tail));
    TEST_ASSERT_EQUAL_size_t(1, checkRowsSane(out, n, 3));
  }
}

void test_csv_null_buffer() {
  RawSample mem[2];
  RawRing r(mem, 2);
  r.push(frameFor(1));
  CsvWriter w(r, Config(), Calibration());
  TEST_ASSERT_EQUAL_size_t(0, w.fill(nullptr, 100));
  TEST_ASSERT_FALSE(w.done());
  char buf[4096];
  const size_t n = w.fill(buf, sizeof(buf));  // shimin vertailumakrot arvioivat argumentin kahdesti
  TEST_ASSERT_GREATER_THAN(100, n);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_ring);
  RUN_TEST(test_push_below_capacity_oldest_first);
  RUN_TEST(test_wraparound_overwrites_oldest);
  RUN_TEST(test_seq_lo_16bit_wraparound);
  RUN_TEST(test_seq_32bit_wraparound);
  RUN_TEST(test_gaps_counted_per_discontinuity);
  RUN_TEST(test_backwards_seq_restarts_recording);
  RUN_TEST(test_long_jump_drops_unreconstructable_samples);
  RUN_TEST(test_capacity_above_seq_window);
  RUN_TEST(test_freeze_unfreeze);
  RUN_TEST(test_freeze_is_reference_counted);
  RUN_TEST(test_clear_keeps_gap_counter);
  RUN_TEST(test_null_storage_is_harmless);
  RUN_TEST(test_csv_exact_small);
  RUN_TEST(test_csv_uses_calibration_and_sensor_params);
  RUN_TEST(test_csv_empty_ring);
  RUN_TEST(test_csv_chunk_size_independent);
  RUN_TEST(test_csv_parses_back_to_ring_contents);
  RUN_TEST(test_csv_two_concurrent_downloads);
  RUN_TEST(test_csv_stops_when_ring_changes_mid_export);
  RUN_TEST(test_csv_detects_unfrozen_changes_small);
  RUN_TEST(test_csv_null_buffer);
  return UNITY_END();
}
