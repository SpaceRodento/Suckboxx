// sensor_driver_as7341.h - AS7341 driver wrapper (11-channel spectral)
//
// Phase 0: raw channel readout into SensorData.lightSpectrum.
// PPFD/DLI calculation lives in light_calc.h (Phase 1, not yet present).
// Graceful degradation: presence-check before init; cache-pattern read
// so calls between hardware sample cycles return last good values.

#ifndef SENSOR_DRIVER_AS7341_H
#define SENSOR_DRIVER_AS7341_H

#include "sensor_driver.h"
#include "config.h"

#if HW_AS7341
#include "as7341_autorange.h"
#include <Adafruit_AS7341.h>

static Adafruit_AS7341 g_as7341;
static bool g_as7341Ready = false;

// Cache: AS7341 read is slow (two SMUX cycles, ~333 ms total at the default
// ATIME/ASTEP). We poll every cycle but only update the SensorData when a
// fresh sample is ready. Between samples the last known good values are kept
// (cache expiry = 30 s).
static LightSpectrum g_as7341LastSpectrum = {};
static uint32_t      g_as7341LastSampleMs = 0;
static bool          g_as7341CacheFilled  = false;
#ifndef AS7341_CACHE_MAX_MS
#define AS7341_CACHE_MAX_MS 30000UL
#endif

// Integration timing. ATIME/ASTEP set the per-cycle integration time:
//   t_integ = (ATIME+1) * (ASTEP+1) * 2.78 us
// readAllChannels() runs TWO SMUX cycles (low + high channels), so the
// blocking read ~= 2 * t_integ + I2C overhead. It MUST stay under
// SENSOR_READ_BUDGET_MS (500 ms) or the orchestrator drops the sensor after
// 3 overruns. Old defaults ATIME=100/ASTEP=999 -> ~281 ms/cycle -> ~617 ms
// read -> permanent "Anturi hidas, pudotettu: AS7341" advisory + ppfd/vpd
// flapping (root-caused 8.7.2026 from UDP log "luku kesti 617ms"). New
// defaults ATIME=49/ASTEP=999 -> ~139 ms/cycle -> ~333 ms read, ~167 ms
// under budget. PPFD is integration-time normalized (light_calc.h) so a
// shorter integration does NOT change PPFD — only the raw counts, which stay
// well above the noise floor at grow-light levels (256x gain).
//
// NB: the PM_ prefix is mandatory. The bare names AS7341_ATIME / AS7341_ASTEP
// COLLIDE with Adafruit_AS7341.h register-address #defines (AS7341_ATIME=0x81).
// A guard `#ifndef AS7341_ATIME` silently keeps the library's 0x81 (=129) and
// setATIME(129) makes the read SLOWER, not faster — the exact trap hit
// 8.7.2026 (intended 49 → got 129 → 780 ms read). Keep these project-local.
#ifndef PM_AS7341_ATIME
#define PM_AS7341_ATIME 49
#endif
#ifndef PM_AS7341_ASTEP
#define PM_AS7341_ASTEP 999
#endif
// True per-cycle integration time (ms) derived from ATIME/ASTEP. Reported in
// LightSpectrum.integrationMs and used by light_calc.h to normalize PPFD —
// must reflect the ACTUAL timing, not a hardcoded guess. 2.78 us/step:
//   (49+1)*(999+1)*2.78us = 139 ms.
#define PM_AS7341_INTEGRATION_MS \
  ((uint16_t)(((uint32_t)(PM_AS7341_ATIME + 1) * (PM_AS7341_ASTEP + 1) * 278UL) / 100000UL))

// Sensor gain. 256x default is tuned for indoor grow-light brightness; full
// outdoor sun saturates channels at this gain (ADC ceiling here is
// (ATIME+1)*(ASTEP+1) = 50000 counts, not the 65535 register width) —
// confirmed on the balcony 21.7.2026, f5_555nm/f6_590nm pinned at exactly
// 50000 in direct sun. Override with e.g. -DAS7341_GAIN=AS7341_GAIN_16X for
// outdoor/full-sun deployments. Changing this REQUIRES re-running the PPFD
// calibration wizard (WIZARD: ppfd-cal, /api/calib/ppfd/save) — raw counts
// scale with gain, so an old calibration factor becomes wrong by roughly
// the gain ratio.
#ifndef AS7341_GAIN
#define AS7341_GAIN AS7341_GAIN_256X
#endif
// Actual multiplier for AS7341_GAIN (enum index -> 0.5 * 2^index; valid for
// index >= 1, i.e. every gain option except the unused 0.5x). Reported in
// LightSpectrum.gain so /api/state reflects the gain actually configured,
// not a hardcoded guess.
#define PM_AS7341_GAIN_MULTIPLIER ((uint16_t)(1 << ((int)AS7341_GAIN - 1)))

// Flicker detection (detectFlickerHz) blocks ~1 s → blows the 500 ms sensor
// budget → the scheduler drops the AS7341 after 3 consecutive overruns
// (intermittent spectrum/DLI gaps, observed 3.7.2026). It is informational
// only (exposed as /api/state flicker_hz, NOT used by PPFD/DLI/VPD), so it is
// OFF by default. Enable for diagnostics only (accepts the budget overrun).
#ifndef ENABLE_AS7341_FLICKER
#define ENABLE_AS7341_FLICKER false
#endif

// Kirjaston gain-enumia ei ole nimetty natiivistubissa (test/stubs/
// Adafruit_AS7341.h: anonyymi enum + setGain(int)), joten tyyppinimea
// as7341_gain_t ei voi kirjoittaa nakyviin — se kaantyy laitteella mutta ei
// natiivitestissa. decltype poimii tyypin vakiosta ja toimii molemmilla.
typedef decltype(AS7341_GAIN_0_5X) As7341GainEnum;

// Runtime-gain (indeksi, ei kerroin). Alustetaan AS7341_GAIN:sta; autorange
// siirtaa tata yhden askeleen kerrallaan. AS7341_GAIN on siis LAHTOARVO eika
// enaa lukittu asetus, kun ENABLE_AS7341_AUTORANGE on paalla.
static uint8_t g_as7341GainIdx = (uint8_t)AS7341_GAIN;

// Automaattinen mittausalueen valinta. Kiintea gain ei voi kattaa seka
// kasvilamppua (~120 umol) etta suoraa aurinkoa (~2000 umol): 16x saturoituu
// jo 98 umol:iin, mika on ALLE taimivaiheen 150 umol tavoitteen — laite ei
// voisi koskaan raportoida riittavaa valoa ulkona (mitattu 29.7.2026).
// Kalibrointikerroin sailyy silti voimassa, koska light_calc.h normalisoi
// gainin pois (LIGHTCALC_REFERENCE_GAIN).
#ifndef ENABLE_AS7341_AUTORANGE
#define ENABLE_AS7341_AUTORANGE true
#endif

static bool as7341_isReady(void) { return g_as7341Ready; }

#ifdef PLANTMEISTER_NATIVE_TEST
static void as7341_resetForTest(void) {
  g_as7341Ready = false;
  g_as7341LastSampleMs = 0;
  g_as7341CacheFilled = false;
}
#endif

static bool as7341_init(void) {
  if (!isI2CDevicePresent(I2C_ADDR_AS7341)) {
    g_as7341Ready = false;
    return false;
  }
  if (g_as7341Ready) return true;

  if (!g_as7341.begin(I2C_ADDR_AS7341, &Wire)) {
    DEBUG_WARN(F("AS7341: begin() failed despite I2C presence"));
    g_as7341Ready = false;
    return false;
  }

  // Integration time + gain: PM_AS7341_ATIME/ASTEP and AS7341_GAIN, both
  // file-scope (see top of file for the budget/saturation rationale).
  g_as7341.setATIME(PM_AS7341_ATIME);
  g_as7341.setASTEP(PM_AS7341_ASTEP);
  g_as7341GainIdx = (uint8_t)AS7341_GAIN;
  g_as7341.setGain((As7341GainEnum)g_as7341GainIdx);

  g_as7341Ready = true;
  g_as7341LastSampleMs = 0;
  g_as7341CacheFilled = false;
  return true;
}

static SensorProbeResult as7341_probe(void) {
  Wire.beginTransmission(I2C_ADDR_AS7341);
  int res = Wire.endTransmission();
  return (res == 0) ? SENSOR_PROBE_OK : SENSOR_PROBE_ABSENT;
}

static void as7341_read(SensorData* out) {
  if (!g_as7341Ready) return;

  // readAllChannels blokkaa luku-syklin ajaksi (~333 ms, kaksi SMUX-sykliä).
  // Adafruit-kirjaston
  // odottaa AS7341:n ASTAT-bitin: jos data ei ole valmis, palauttaa false.
  uint16_t readings[12];
  if (!g_as7341.readAllChannels(readings)) {
    // Data not ready this tick; serve cached values if recent enough.
    if (!g_as7341CacheFilled) return;
    uint32_t now = (uint32_t)millis();
    if ((now - g_as7341LastSampleMs) >= AS7341_CACHE_MAX_MS) {
      // Cache stale — drop validity.
      return;
    }
    out->lightSpectrum = g_as7341LastSpectrum;
    out->spectrumValid = true;
    return;
  }

  // readings[] ordering per Adafruit:
  //   [0]=F1 415, [1]=F2 445, [2]=F3 480, [3]=F4 515,
  //   [4]=F5 555, [5]=F6 590, [6]=F7 630, [7]=F8 680,
  //   [8]=clear,  [9]=NIR
  LightSpectrum s = {};
  s.f1_415nm = readings[0];
  s.f2_445nm = readings[1];
  s.f3_480nm = readings[2];
  s.f4_515nm = readings[3];
  s.f5_555nm = readings[4];
  s.f6_590nm = readings[5];
  s.f7_630nm = readings[6];
  s.f8_680nm = readings[7];
  s.clear    = readings[8];
  s.nir      = readings[9];
#if ENABLE_AS7341_FLICKER
  s.flickerHz       = (uint16_t)g_as7341.detectFlickerHz();  // ~1 s — budget overrun!
#else
  s.flickerHz       = 0;     // not measured — keeps read under the 500 ms budget
#endif
  s.integrationMs   = PM_AS7341_INTEGRATION_MS;   // johdettu ATIME/ASTEP:sta (~139 ms)
  // Gain kirjataan silla arvolla jolla TAMA naytte otettiin — ei sillä johon
  // autorange kohta vaihtaa. light_calc.h normalisoi kertoimen pois taman
  // kentan perusteella, joten vaara arvo tassa skaalaisi PPFD:n suoraan
  // pieleen gain-askeleen verran.
  s.gain            = (uint16_t)as7341_gainMultiplier(g_as7341GainIdx);

#if ENABLE_AS7341_AUTORANGE
  {
    const uint16_t par8[8] = { s.f1_415nm, s.f2_445nm, s.f3_480nm, s.f4_515nm,
                               s.f5_555nm, s.f6_590nm, s.f7_630nm, s.f8_680nm };
    const uint16_t maxPar  = as7341_maxParChannel(par8);
    const uint32_t ceiling = as7341_adcCeiling(PM_AS7341_ATIME, PM_AS7341_ASTEP);

    s.saturated = as7341_isSaturated(maxPar, ceiling);
    if (s.saturated) {
      DEBUG_WARNF("AS7341: saturated at gain %ux - PPFD is a floor, not a reading\n",
                  (unsigned)s.gain);
    }

    const uint8_t nextIdx = as7341_autorangeNextGain(g_as7341GainIdx, maxPar, ceiling);
    if (nextIdx != g_as7341GainIdx) {
      DEBUG_PRINTF("[INFO]  AS7341: autorange gain %ux -> %ux (max channel %u/%lu)\n",
                   (unsigned)as7341_gainMultiplier(g_as7341GainIdx),
                   (unsigned)as7341_gainMultiplier(nextIdx),
                   (unsigned)maxPar, (unsigned long)ceiling);
      g_as7341GainIdx = nextIdx;
      g_as7341.setGain((As7341GainEnum)g_as7341GainIdx);
    }
  }
#endif

  g_as7341LastSpectrum = s;
  g_as7341LastSampleMs = (uint32_t)millis();
  g_as7341CacheFilled = true;

  out->lightSpectrum = s;
  out->spectrumValid = true;
}

static const SensorDriver g_as7341Driver = {
  .name = "AS7341",
  .readyFlag = &g_as7341Ready,
  .reprobeIntervalMs = SENSOR_REPROBE_I2C_MS,
  .init = as7341_init,
  .probe = as7341_probe,
  .read = as7341_read
};

#endif // HW_AS7341

#endif // SENSOR_DRIVER_AS7341_H
