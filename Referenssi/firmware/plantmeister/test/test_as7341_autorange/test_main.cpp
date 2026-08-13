#include <unity.h>

#include "test_feature_flags.h"
#include "as7341_autorange.h"

// Tuotantoasetukset: ATIME 49, ASTEP 999 -> katto 50 000 counts.
static const uint16_t ATIME = 49;
static const uint16_t ASTEP = 999;

void setUp() {}
void tearDown() {}

// ════════════════════════════════════════════════════════════════════
// ADC-katto
// ════════════════════════════════════════════════════════════════════

// Katto on (ATIME+1)*(ASTEP+1), EI rekisterin 65535 leveys. Tama ero on
// koko ongelman ydin: 50 000 countia vastaa ~98 umol paivanvalossa, mika on
// alle taimivaiheen 150 umol tavoitteen.
void test_adc_ceiling_from_timing() {
  TEST_ASSERT_EQUAL_UINT32(50000UL, as7341_adcCeiling(ATIME, ASTEP));
}

// Rekisteri on 16-bittinen: pitka integraatio ei voi nostaa kattoa yli 65535.
void test_adc_ceiling_capped_at_register_width() {
  TEST_ASSERT_EQUAL_UINT32(65535UL, as7341_adcCeiling(255, 999));
}

// ════════════════════════════════════════════════════════════════════
// Gain-kertoimet
// ════════════════════════════════════════════════════════════════════

// Indeksi 0 on poikkeus (0.5x); muut ovat 2^(idx-1). Vaara kaava tassa
// skaalaisi PPFD:n suoraan pieleen, koska light_calc.h normalisoi tallа.
void test_gain_multiplier_table() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, as7341_gainMultiplier(0));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, as7341_gainMultiplier(1));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 16.0f, as7341_gainMultiplier(5));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 512.0f, as7341_gainMultiplier(10));
}

// ════════════════════════════════════════════════════════════════════
// Kirkkaimman PAR-kanavan valinta
// ════════════════════════════════════════════════════════════════════

// Vain 8 PAR-kanavaa ohjaavat aluetta. Clear/NIR eivat ole PPFD-summassa,
// joten niiden antaa ohjata gainia uhraisi PAR-resoluution kayttamattomalle
// kanavalle.
void test_max_par_channel_picks_largest() {
  const uint16_t ch[8] = { 7149, 4626, 8333, 7628, 38710, 20742, 9628, 9913 };
  TEST_ASSERT_EQUAL_UINT16(38710, as7341_maxParChannel(ch));
}

void test_max_par_channel_null_safe() {
  TEST_ASSERT_EQUAL_UINT16(0, as7341_maxParChannel(0));
}

// ════════════════════════════════════════════════════════════════════
// Alueen valinta
// ════════════════════════════════════════════════════════════════════

// Mitattu tilanne 29.7.2026 parvekkeella: f5 = 39 655 / 50 000 = 79 %.
// Alle 85 %:n kynnyksen -> ei vielä muutosta, mutta lahella.
void test_no_change_in_normal_range() {
  TEST_ASSERT_EQUAL_UINT8(5, as7341_autorangeNextGain(5, 39655, 50000));
}

// Yli 85 % -> yksi askel alas (16x -> 8x).
void test_steps_down_when_near_ceiling() {
  TEST_ASSERT_EQUAL_UINT8(4, as7341_autorangeNextGain(5, 43000, 50000));
}

// Alle 6 % -> yksi askel ylos, jotta hamarassa sailyy resoluutio.
void test_steps_up_when_very_dim() {
  TEST_ASSERT_EQUAL_UINT8(6, as7341_autorangeNextGain(5, 1200, 50000));
}

// Hyvin hamara valo vaatii USEAN askeleen, yhden per naytte. Se on tahallista:
// iso hyppy tekisi valinaytteista vaikeasti tulkittavia DLI-integraalissa.
// 1200 = 2.4 % -> 4.8 % -> 9.6 %, eli kaksi askelta ennen vakautumista.
void test_very_dim_converges_over_multiple_samples() {
  uint8_t idx = as7341_autorangeNextGain(5, 1200, 50000);
  TEST_ASSERT_EQUAL_UINT8(6, idx);
  idx = as7341_autorangeNextGain(idx, 2400, 50000);
  TEST_ASSERT_EQUAL_UINT8(7, idx);
  TEST_ASSERT_EQUAL_UINT8(7, as7341_autorangeNextGain(idx, 4800, 50000));
}

// Yksi askel on 2x, joten alas-askel 85 %:sta laskeutuu ~42 %:iin ja
// ylos-askel 6 %:sta nousee ~12 %:iin — molemmat kaukana kynnyksista.
// Tama on se ominaisuus joka estaa gainia varahtelemasta edestakaisin.
void test_no_oscillation_after_step() {
  // Alas: 43 000 @16x -> 8x, sama valo antaa nyt puolet counteja.
  uint8_t idx = as7341_autorangeNextGain(5, 43000, 50000);
  TEST_ASSERT_EQUAL_UINT8(4, idx);
  TEST_ASSERT_EQUAL_UINT8(4, as7341_autorangeNextGain(idx, 21500, 50000));

  // Ylos: 2900 (5.8 %, juuri kynnyksen alla) @16x -> 32x. Sama valo antaa nyt
  // 5800 countia = 11.6 %, joka on kynnysten valissa -> ei uutta askelta.
  idx = as7341_autorangeNextGain(5, 2900, 50000);
  TEST_ASSERT_EQUAL_UINT8(6, idx);
  TEST_ASSERT_EQUAL_UINT8(6, as7341_autorangeNextGain(idx, 5800, 50000));
}

// Reunat eivat saa karata alueen ulkopuolelle.
void test_clamps_at_range_ends() {
  TEST_ASSERT_EQUAL_UINT8(AS7341_GAIN_IDX_MIN,
      as7341_autorangeNextGain(AS7341_GAIN_IDX_MIN, 50000, 50000));
  TEST_ASSERT_EQUAL_UINT8(AS7341_GAIN_IDX_MAX,
      as7341_autorangeNextGain(AS7341_GAIN_IDX_MAX, 0, 50000));
}

// Nollakatto (jarjeton ajoitus) ei saa aiheuttaa nollajakoa eika muutosta.
void test_zero_ceiling_is_noop() {
  TEST_ASSERT_EQUAL_UINT8(5, as7341_autorangeNextGain(5, 1000, 0));
}

// ════════════════════════════════════════════════════════════════════
// Saturaation tunnistus
// ════════════════════════════════════════════════════════════════════

// Katon lyonyt naytte on ALARAJA eika mittaus — kuluttajan on tiedettava se.
void test_saturation_detected_at_ceiling() {
  TEST_ASSERT_TRUE(as7341_isSaturated(50000, 50000));
  TEST_ASSERT_FALSE(as7341_isSaturated(49999, 50000));
}

// Mitattu 21.7.2026: gain 256x, f5/f6 juuttuivat TASAN 50 000:een auringossa.
// Se on saturaatio, ei "valo lakkasi kirkastumasta".
void test_saturation_matches_observed_pinning() {
  TEST_ASSERT_TRUE(as7341_isSaturated(50000, as7341_adcCeiling(ATIME, ASTEP)));
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_adc_ceiling_from_timing);
  RUN_TEST(test_adc_ceiling_capped_at_register_width);
  RUN_TEST(test_gain_multiplier_table);
  RUN_TEST(test_max_par_channel_picks_largest);
  RUN_TEST(test_max_par_channel_null_safe);
  RUN_TEST(test_no_change_in_normal_range);
  RUN_TEST(test_steps_down_when_near_ceiling);
  RUN_TEST(test_steps_up_when_very_dim);
  RUN_TEST(test_very_dim_converges_over_multiple_samples);
  RUN_TEST(test_no_oscillation_after_step);
  RUN_TEST(test_clamps_at_range_ends);
  RUN_TEST(test_zero_ceiling_is_noop);
  RUN_TEST(test_saturation_detected_at_ceiling);
  RUN_TEST(test_saturation_matches_observed_pinning);
  return UNITY_END();
}
