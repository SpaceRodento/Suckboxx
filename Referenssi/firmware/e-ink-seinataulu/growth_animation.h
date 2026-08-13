/*=====================================================================
  growth_animation.h - E-ink Plant Growth Animation Selector

  Selects frame by hybrid logic:
    - Guided grow phase/day as baseline signal
    - Sensor distance as refinement

  Fallback chain:
    1) species-specific animation
    2) generic animation
=====================================================================*/

#ifndef GROWTH_ANIMATION_H
#define GROWTH_ANIMATION_H

#include "config.h"
#include "data_fetch.h"
#include "artwork/generated/growth_anim_assets.h"
#include <ctype.h>

struct GrowthFrameView {
  const unsigned char* bitmap;
  uint16_t width;
  uint16_t height;
  uint8_t frameIndex;
  uint8_t frameCount;
  bool valid;
  bool fromGeneric;
};

static int growth_clampInt(int value, int minVal, int maxVal) {
  if (value < minVal) return minVal;
  if (value > maxVal) return maxVal;
  return value;
}

static bool growth_equalsIgnoreCase(const char* a, const char* b) {
  if (!a || !b) return false;

  while (*a && *b) {
    char ca = (char)tolower((unsigned char)*a);
    char cb = (char)tolower((unsigned char)*b);
    if (ca != cb) return false;
    a++;
    b++;
  }

  return (*a == '\0' && *b == '\0');
}

static const char* growth_normalizePlantId(const char* plantId) {
  if (!plantId || plantId[0] == '\0') return "generic";

  // Finnish + English aliases to keep runtime lookup stable.
  if (growth_equalsIgnoreCase(plantId, "basilika") ||
      growth_equalsIgnoreCase(plantId, "basil")) {
    return "basil";
  }
  if (growth_equalsIgnoreCase(plantId, "persilja") ||
      growth_equalsIgnoreCase(plantId, "parsley")) {
    return "parsley";
  }
  if (growth_equalsIgnoreCase(plantId, "tilli") ||
      growth_equalsIgnoreCase(plantId, "dill")) {
    return "dill";
  }
  if (growth_equalsIgnoreCase(plantId, "minttu") ||
      growth_equalsIgnoreCase(plantId, "mint")) {
    return "mint";
  }
  if (growth_equalsIgnoreCase(plantId, "ruohosipuli") ||
      growth_equalsIgnoreCase(plantId, "chives") ||
      growth_equalsIgnoreCase(plantId, "scallion") ||
      growth_equalsIgnoreCase(plantId, "spring_onion")) {
    return "chives";
  }
  if (growth_equalsIgnoreCase(plantId, "korianteri") ||
      growth_equalsIgnoreCase(plantId, "cilantro") ||
      growth_equalsIgnoreCase(plantId, "coriander")) {
    return "cilantro";
  }

  return plantId;
}

static const GrowthAnimAsset* growth_findAsset(const char* plantId) {
  const GrowthAnimAsset* generic = NULL;

  for (uint8_t i = 0; i < GROWTH_ANIM_ASSET_COUNT; i++) {
    const GrowthAnimAsset* a = &GROWTH_ANIM_ASSETS[i];
    if (strcmp(a->plantId, "generic") == 0) {
      generic = a;
    }
    if (plantId && strcmp(a->plantId, plantId) == 0) {
      return a;
    }
  }

  return generic;
}

static uint8_t growth_sensorFrame(const DisplayData* data, uint8_t frameCount) {
  if (!data || frameCount == 0) return 0;
  if (data->plantHeightMm <= 0) return 0;

  int minMm = EINK_GROWTH_SENSOR_MIN_MM;
  int maxMm = EINK_GROWTH_SENSOR_MAX_MM;
  if (maxMm <= minMm) return 0;

  // Smaller distance means taller plant.
  int distanceMm = growth_clampInt(data->plantHeightMm, minMm, maxMm);
  int numerator = (maxMm - distanceMm) * (frameCount - 1);
  int denominator = (maxMm - minMm);
  int frame = (numerator + (denominator / 2)) / denominator;
  return (uint8_t)growth_clampInt(frame, 0, frameCount - 1);
}

static uint8_t growth_phaseFrame(const DisplayData* data, uint8_t frameCount) {
  if (!data || frameCount == 0) return 0;
  if (!data->growFieldsPresent || !data->growActive) return 0;

  int phaseCount = EINK_GROWTH_PHASE_COUNT_HINT;
  if (phaseCount < 1) phaseCount = 1;

  int phase = growth_clampInt(data->growPhase, 0, phaseCount - 1);
  int baseFrame = 0;

  if (phaseCount > 1) {
    // Floor (ei ylospain-pyoristysta): 4 vaihetta / 6 framea -> {0,1,3,5}.
    // Aiempi +(phaseCount-1)/2 -bias mappasi SEEDLING(1)->frame 2 (liian iso
    // kasvi kesken idatyksen); floor mappaa SEEDLING->1 (taimi). Idatyksen
    // alussa korkeus~0 -> sensori-blend vetaa framen 0:aan (itu). 19.7.2026.
    baseFrame = (phase * (frameCount - 1)) / (phaseCount - 1);
  }

  // Day-based progression inside current phase.
  int dayBonus = (data->growElapsedDays >= 7) ? 1 : 0;
  int frame = baseFrame + dayBonus;

  return (uint8_t)growth_clampInt(frame, 0, frameCount - 1);
}

static uint8_t growth_pickFrameIndex(const DisplayData* data, uint8_t frameCount) {
  if (frameCount == 0) return 0;

  bool hasPhaseSignal = data && data->growFieldsPresent && data->growActive;
  bool hasSensorSignal = data && data->plantHeightMm > 0;

  uint8_t phaseFrame = growth_phaseFrame(data, frameCount);
  uint8_t sensorFrame = growth_sensorFrame(data, frameCount);

  if (hasPhaseSignal && hasSensorSignal) {
    // Hybrid mode: prioritize guided grow phase/day, refine with sensor.
    int blended = (phaseFrame * 2 + sensorFrame) / 3;
    return (uint8_t)growth_clampInt(blended, 0, frameCount - 1);
  }

  if (hasPhaseSignal) return phaseFrame;
  if (hasSensorSignal) return sensorFrame;

  // Last fallback: very slow movement by elapsed grow days if available.
  if (data && data->growElapsedDays > 0) {
    int dayFrame = data->growElapsedDays / 10;
    return (uint8_t)growth_clampInt(dayFrame, 0, frameCount - 1);
  }

  return 0;
}

static GrowthFrameView growth_selectFrame(const DisplayData* data) {
  GrowthFrameView out = {};
  out.valid = false;

  // Treat empty or placeholder ("?") plantId as "no plant configured".
  bool plantIdMissing = !data ||
                        data->plantId[0] == '\0' ||
                        strcmp(data->plantId, "?") == 0;

  // When no grow session is active and the sensor reports no plant, fall
  // back to basil as the showroom default. Keeps the screen visually full.
  bool noLiveData = !data ||
                    (!data->growActive &&
                     data->plantHeightMm <= 0 &&
                     plantIdMissing);

  const char* rawPlantId;
  if (noLiveData) {
    rawPlantId = "basil";
  } else {
    rawPlantId = (!plantIdMissing) ? data->plantId : "generic";
  }
  const char* plantId = growth_normalizePlantId(rawPlantId);

  const GrowthAnimAsset* primary = growth_findAsset(plantId);
  if (!primary || primary->frameCount == 0 || !primary->frames) {
    return out;
  }

  // Onko meilla mitaan kasvattamiseen liittyvaa signaalia?
  bool hasGrowSignal = data &&
                       ((data->growFieldsPresent && data->growActive) ||
                        data->plantHeightMm > 0 ||
                        data->growElapsedDays > 0);

  uint8_t frameIdx;
  if (!hasGrowSignal) {
    // Ei kasvatussignaalia -> nayta tuuhea Bushial Fullness (stage_04)
    // sen sijaan etta nayttaisi pikkutaimea (stage_00). Tama on
    // "showroom"-default: kuva taysittaa vasenta saraketta.
    frameIdx = (primary->frameCount >= 5) ? 4 : (primary->frameCount - 1);
  } else {
    frameIdx = growth_pickFrameIndex(data, primary->frameCount);
  }
  frameIdx = (uint8_t)growth_clampInt(frameIdx, 0, primary->frameCount - 1);

  const unsigned char* frame = primary->frames[frameIdx];
  if (!frame) return out;

  out.bitmap = frame;
  out.width = primary->width;
  out.height = primary->height;
  out.frameIndex = frameIdx;
  out.frameCount = primary->frameCount;
  out.valid = true;
  out.fromGeneric = (strcmp(primary->plantId, "generic") == 0);

  return out;
}

#endif // GROWTH_ANIMATION_H
