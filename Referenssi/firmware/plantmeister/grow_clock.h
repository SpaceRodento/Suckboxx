/*=====================================================================
  grow_clock.h - Grow-clock persistence (LittleFS I/O + integration)

  Persists GrowClockData to PATH_GROW_CLOCK so an active grow survives
  power cycles without an RTC or internet (NTP is never required).
  Pure math lives in grow_clock_math.h (natively tested in
  test_grow_clock). Wiring:

    setup():  growclock_restore()  — after config_load + anchor init
    loop:     growclock_tick()     — from the grow scheduler task
              (DEVICE_GROWING only, so IDLE never writes flash)

  Restore error = worst case one save interval of grow-day progress
  lost; without this module a power cut lost the whole day (and the
  photoperiod position) every time.
=====================================================================*/

#ifndef GROW_CLOCK_H
#define GROW_CLOCK_H

#include "config.h"
#include "structs.h"
#include "grow_clock_math.h"
#include "grow_step_fsm.h"   // askelkello: aktiivisen askeleen tyyppi -> tallennustahti
#include <ArduinoJson.h>
#include <LittleFS.h>

static unsigned long g_growclockLastSaveMs = 0;
static uint32_t g_growclockLastSavedDayElapsedMs = 0;
static uint32_t g_growclockLastSavedStepElapsedMs = 0;
static uint8_t  g_growclockLastSavedStepIndex = 0;

// ── LittleFS I/O ────────────────────────────────────────────────

bool growclock_load(GrowClockData* out) {
  if (!LittleFS.exists(PATH_GROW_CLOCK)) return false;
  File f = LittleFS.open(PATH_GROW_CLOCK, "r");
  if (!f) return false;

  // 384 (oli 256): DLI-kentat mahtuvat varmasti. Ylivuoto ei kaataisi vaan
  // pudottaisi kenttia hiljaa validista JSONista (PR #157 -oppi) — siksi
  // marginaali eika minimi.
  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    DEBUG_WARNF("GrowClock: JSON parse error: %s\n", err.c_str());
    return false;
  }

  out->dayElapsedMs   = growclock_clampDayElapsed(doc["day_elapsed_ms"] | 0UL);
  out->lightElapsedMs = growclock_wrapLightElapsed(doc["light_elapsed_ms"] | 0UL);
  // Vanha tiedosto ilman step-kenttia: 0/0 — tagi ei todennakoisesti tasmaa
  // nykyiseen indeksiin, jolloin askelkello alkaa nollasta (mainostettu
  // vikatila, ei virhe). Ei clampia: iso elapsed on BUTTON-askeleella
  // vaaraton ("kulunut n vrk") ja TIMER-askeleella vain paattaa askeleen.
  out->stepElapsedMs  = doc["step_elapsed_ms"] | 0UL;
  out->stepIndex      = (uint8_t)(doc["step_index"] | 0);
  // Vanha tiedosto ilman DLI-kenttia: 0/0 = "kertyma alkaa nollasta", sama
  // kuin ennen tata ominaisuutta. Ei siis vaadi skeemaversion nostoa.
  out->dliMol         = growclock_sanitizeDli(doc["dli_mol"] | 0.0f);
  out->dliElapsedMs   = growclock_clampDayElapsed(doc["dli_elapsed_ms"] | 0UL);
  return true;
}

bool growclock_save(const GrowClockData* d) {
  StaticJsonDocument<384> doc;
  doc["day_elapsed_ms"]   = d->dayElapsedMs;
  doc["light_elapsed_ms"] = d->lightElapsedMs;
  doc["step_elapsed_ms"]  = d->stepElapsedMs;
  doc["step_index"]       = d->stepIndex;
  doc["dli_mol"]          = d->dliMol;
  doc["dli_elapsed_ms"]   = d->dliElapsedMs;

  File f = LittleFS.open(PATH_GROW_CLOCK, "w");
  if (!f) {
    DEBUG_ERROR(F("GrowClock: failed to write growclock.json"));
    return false;
  }
  serializeJson(doc, f);
  f.close();
  return true;
}

// ── Boot restore ────────────────────────────────────────────────

// Call after config_load() and the default anchor init. Back-dates the grow
// timer anchors from the persisted accumulators so the day counter and the
// photoperiod continue where the previous power cycle left them. No-op when
// no grow is active or nothing was persisted.
// outLoaded (valinnainen) saa palautetut arvot, jotta kutsuja voi palauttaa
// myos DLI-kertyman ilman toista tiedostolukua. Jaa nollatuksi jos mitaan ei
// ollut persistoituna — silloin DLI alkaa nollasta kuten ennenkin.
void growclock_restore(SystemState* state, const DeviceConfig* cfg,
                       GrowClockData* outLoaded = 0) {
  if (!cfg->growActive) return;

  GrowClockData d;
  if (!growclock_load(&d)) {
    DEBUG_INFO(F("GrowClock: no persisted clock, anchors start from boot"));
    return;
  }
  if (outLoaded) *outLoaded = d;

  unsigned long now = millis();
  state->growDayStartMs    = growclock_restoredAnchor(now, d.dayElapsedMs);
  state->lightCycleStartMs = growclock_restoredAnchor(now, d.lightElapsedMs);
  g_growclockLastSavedDayElapsedMs = d.dayElapsedMs;

  // Askelkello: elapsed hyvaksytaan vain jos epookkitagi tasmaa config.jsonin
  // nykyiseen indeksiin — muuten 0 (kello alkaa alusta, ei VAARAN askeleen
  // vanhaa kertymaa). Ks. grow_clock_math.h growclock_stepElapsedFor.
  uint32_t stepElapsed = growclock_stepElapsedFor(d.stepElapsedMs, d.stepIndex,
                                                  cfg->growStepIndex);
  state->growStepStartMs = growclock_restoredAnchor(now, stepElapsed);
  g_growclockLastSavedStepElapsedMs = stepElapsed;
  g_growclockLastSavedStepIndex     = cfg->growStepIndex;
  if (d.stepElapsedMs > 0 && stepElapsed == 0) {
    DEBUG_PRINTF("[WARN]  GrowClock: step clock discarded (tag %u != index %u)\n",
                 (unsigned)d.stepIndex, (unsigned)cfg->growStepIndex);
  }

  DEBUG_PRINTF("[INFO]  GrowClock: restored day +%lu min, light cycle +%lu min, "
               "step %u +%lu min, DLI %.2f mol +%lu min\n",
               (unsigned long)(d.dayElapsedMs / 60000UL),
               (unsigned long)(d.lightElapsedMs / 60000UL),
               (unsigned)cfg->growStepIndex,
               (unsigned long)(stepElapsed / 60000UL),
               (double)d.dliMol,
               (unsigned long)(d.dliElapsedMs / 60000UL));
}

// ── Periodic persistence ────────────────────────────────────────

// Call from the grow scheduler loop task (runs only in DEVICE_GROWING).
// Saves on the coarse interval, plus immediately after a day tick / grow
// restart (rollover detection in growclock_shouldSave) and after a step
// advance/reset (epoch-tag change, growclock_stepForcesSave). While a
// countdown (TIMER) step runs, the interval tightens to 60 s so a power
// cut cannot swallow a whole 30 min soak (grow_clock_math.h).
// dliMol/dliElapsedMs tulevat sensor_managerilta (DLI-tilan omistaja) — ne
// kulkevat parametreina eivatka includena, koska riippuvuussuunta on ylhaalta
// alas: grow_clock.h on persistointikerros eika saa tuntea sensorikerrosta.
void growclock_tick(const SystemState* state, const DeviceConfig* cfg,
                    const PlantConfig* plant,
                    float dliMol, uint32_t dliElapsedMs) {
  if (!cfg->growActive) return;

  unsigned long now = millis();
  uint32_t dayElapsedMs   = growclock_clampDayElapsed(
                              (uint32_t)(now - state->growDayStartMs));
  uint32_t lightElapsedMs = growclock_wrapLightElapsed(
                              (uint32_t)(now - state->lightCycleStartMs));
  uint32_t stepElapsedMs  = (uint32_t)(now - state->growStepStartMs);

  bool timerStepRunning = false;
  uint8_t stepCount = 0;
  const GrowStep* steps = growstep_stepsForConfig(plant, cfg, &stepCount);
  if (steps && cfg->growStepIndex < stepCount) {
    timerStepRunning =
        (steps[cfg->growStepIndex].advanceOn == STEP_ADVANCE_TIMER);
  } else {
    stepElapsedMs = 0;   // ei aktiivista askelta — ala persistoi roikkuvaa kelloa
  }

  unsigned long interval = growclock_saveIntervalMs(GROW_CLOCK_SAVE_INTERVAL_MS,
                                                    timerStepRunning);
  bool due = growclock_shouldSave(now, g_growclockLastSaveMs, interval,
                                  dayElapsedMs, g_growclockLastSavedDayElapsedMs) ||
             growclock_stepForcesSave(cfg->growStepIndex,
                                      g_growclockLastSavedStepIndex,
                                      stepElapsedMs,
                                      g_growclockLastSavedStepElapsedMs);
  if (!due) return;

  GrowClockData d = { dayElapsedMs, lightElapsedMs, stepElapsedMs,
                      cfg->growStepIndex,
                      growclock_sanitizeDli(dliMol),
                      growclock_clampDayElapsed(dliElapsedMs) };
  if (growclock_save(&d)) {
    g_growclockLastSaveMs = now;
    g_growclockLastSavedDayElapsedMs  = dayElapsedMs;
    g_growclockLastSavedStepElapsedMs = stepElapsedMs;
    g_growclockLastSavedStepIndex     = cfg->growStepIndex;
  }
}

#endif // GROW_CLOCK_H
