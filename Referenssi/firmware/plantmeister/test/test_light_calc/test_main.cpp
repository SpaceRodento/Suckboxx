/*=====================================================================
  test_light_calc — Unity tests for light_calc.h PPFD estimation.

  Tests cover: NULL/zero-guard, single-channel weighting, calibration
  factor scaling, and integration-time normalization.
=====================================================================*/

#include <unity.h>

// Feature flag overrides — must come before test_feature_flags.h.
// light_calc.h depends only on structs.h and config.h math; no hardware needed.
#define ENABLE_SENSORS       false
#define ENABLE_DEVICE_STATE  false
#define HW_AS7341            false
// Disable all sensor sub-features so config.h guard does not fire.
#define ENABLE_TDS_SENSOR    false
#define ENABLE_WATER_TEMP    false
#define ENABLE_FLOAT_SWITCH  false
#define ENABLE_HEIGHT_SENSOR false
#define ENABLE_ENV_SENSOR    false
#define ENABLE_BATTERY_MONITOR false

#include "test_feature_flags.h"

#include "structs.h"
#include "light_calc.h"

void setUp() {}
void tearDown() {}

// NULL spec must return 0.0f without crashing.
void test_ppfd_null_spec() {
  float result = lightCalc_estimatePPFD(NULL, 1.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, result);
}

// integrationMs == 0 must return 0.0f (guard against divide-by-zero).
void test_ppfd_zero_integration() {
  LightSpectrum spec = {};
  spec.f8_680nm = 1000;
  spec.integrationMs = 0;

  float result = lightCalc_estimatePPFD(&spec, 1.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, result);
}

// Only F8 (680 nm, weight=1.00) active, integrationMs=100 (reference point).
// Expected: 1000 * 1.00 / (100/100) * 1.0 = 1000.0f
void test_ppfd_single_channel_680nm() {
  LightSpectrum spec = {};
  spec.f8_680nm = 1000;
  spec.integrationMs = 100;

  float result = lightCalc_estimatePPFD(&spec, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, result);
}

// With calibFactor=2.0f the result must be exactly 2x the uncalibrated value.
void test_ppfd_calibration_factor() {
  LightSpectrum spec = {};
  spec.f8_680nm = 1000;
  spec.integrationMs = 100;

  float uncal = lightCalc_estimatePPFD(&spec, 1.0f);
  float cal   = lightCalc_estimatePPFD(&spec, 2.0f);

  TEST_ASSERT_FLOAT_WITHIN(0.01f, uncal * 2.0f, cal);
}

// Doubling integrationMs should yield the same normalized result.
void test_ppfd_integration_normalization() {
  LightSpectrum spec100 = {};
  spec100.f8_680nm = 1000;
  spec100.integrationMs = 100;

  LightSpectrum spec200 = {};
  spec200.f8_680nm = 1000;
  spec200.integrationMs = 200;

  float result100 = lightCalc_estimatePPFD(&spec100, 1.0f);
  float result200 = lightCalc_estimatePPFD(&spec200, 1.0f);

  // spec200 raw counts are the same but integration is 2x longer,
  // so normalized value should be half of spec100.
  // The two readings represent the same photon rate only if counts scale
  // proportionally — here they don't, so result200 == result100/2.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, result100 / 2.0f, result200);
}


// ════════════════════════════════════════════════════════════════════
// Gain-normalisointi (autorange-tuki)
// ════════════════════════════════════════════════════════════════════

// KRIITTINEN: 16x on referenssi, joten kentalla asetettu kalibrointikerroin
// sailyy voimassa sellaisenaan. Jos tama testi hajoaa, jokaisen laitteen
// PPFD-asteikko on vaarin sen suhteen mika se oli ennen muutosta.
void test_gain_16x_is_identity() {
  LightSpectrum spec = {};
  spec.f8_680nm = 1000;
  spec.integrationMs = 100;
  spec.gain = 16;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, lightCalc_estimatePPFD(&spec, 1.0f));
}

// Gain puolittuu -> countit puolittuvat samasta valosta -> normalisoinnin
// jalkeen PPFD on sama. Tama on koko autorangen edellytys: mittausalueen
// vaihto ei saa nakya lukemassa.
void test_ppfd_invariant_across_gain_step() {
  LightSpectrum hi = {};
  hi.f8_680nm = 40000; hi.integrationMs = 100; hi.gain = 16;

  LightSpectrum lo = {};
  lo.f8_680nm = 20000; lo.integrationMs = 100; lo.gain = 8;   // sama valo, puoli gain

  TEST_ASSERT_FLOAT_WITHIN(0.01f,
                           lightCalc_estimatePPFD(&hi, 1.0f),
                           lightCalc_estimatePPFD(&lo, 1.0f));
}

// Sama koko mielekkaan alueen yli: countit skaalautuvat gainin mukana -> PPFD
// pysyy samana. Lahtoarvo 8000 @16x jakautuu tasan jokaiselle gainille, joten
// kvantisointivirhetta ei ole ja toleranssi voi olla tiukka.
//
// Alue on 1x..64x eika 0.5x..512x TAHALLAAN: 8000 @16x vastaa 32000 counttia
// 64x:lla, ja seuraava askel ylittaisi ADC-katon 50 000 — sellaista naytetta ei
// voi fyysisesti syntya, joten sen testaaminen vaittaisi jotain mahdotonta.
void test_ppfd_invariant_across_full_gain_range() {
  const float referenceCounts = 8000.0f;   // @16x
  const uint16_t gains[] = { 1, 2, 4, 8, 16, 32, 64 };

  LightSpectrum base = {};
  base.f8_680nm = (uint16_t)referenceCounts;
  base.integrationMs = 100;
  base.gain = 16;
  const float expected = lightCalc_estimatePPFD(&base, 1.0f);

  for (unsigned i = 0; i < sizeof(gains) / sizeof(gains[0]); i++) {
    LightSpectrum s = {};
    s.integrationMs = 100;
    s.gain = gains[i];
    s.f8_680nm = (uint16_t)(referenceCounts * (float)gains[i] / 16.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, lightCalc_estimatePPFD(&s, 1.0f));
  }
}

// gain == 0 tarkoittaa "tuntematon" (vanha naytte tai nollattu struct):
// silloin normalisointi ohitetaan, mika vastaa vanhaa kaytosta. Ei saa
// aiheuttaa nollajakoa eika nollata lukemaa.
void test_gain_zero_falls_back_to_legacy_behaviour() {
  LightSpectrum spec = {};
  spec.f8_680nm = 1000;
  spec.integrationMs = 100;
  spec.gain = 0;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, lightCalc_estimatePPFD(&spec, 1.0f));
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_ppfd_null_spec);
  RUN_TEST(test_ppfd_zero_integration);
  RUN_TEST(test_ppfd_single_channel_680nm);
  RUN_TEST(test_ppfd_calibration_factor);
  RUN_TEST(test_ppfd_integration_normalization);
  RUN_TEST(test_gain_16x_is_identity);
  RUN_TEST(test_ppfd_invariant_across_gain_step);
  RUN_TEST(test_ppfd_invariant_across_full_gain_range);
  RUN_TEST(test_gain_zero_falls_back_to_legacy_behaviour);
  return UNITY_END();
}
