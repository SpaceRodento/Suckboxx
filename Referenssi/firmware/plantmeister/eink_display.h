/*=====================================================================
  eink_display.h - E-Ink software baseline (hardware-independent)

  This header provides a lightweight runtime data model for future
  e-ink rendering and a stub update pipeline that can be unit-tested
  without display hardware.
=====================================================================*/

#ifndef EINK_DISPLAY_H
#define EINK_DISPLAY_H

#include "config.h"
#include "structs.h"

#include <string.h>

struct EinkFrameData {
  float airTempC;
  float airHumidity;
  float waterTempC;
  int tdsPpm;
  int plantHeightMm;
  int motorHeightMm;
  int batteryPercent;

  bool lightsOn;
  bool airPumpOn;
  bool waterLevelOk;

  bool growActive;
  uint8_t growPhase;
  uint16_t growElapsedDays;

  char plantId[24];
  char plantName[32];
  char phaseLabel[16];
  char phaseGuidance[96];
};

struct EinkBackendOps {
  bool (*init)();
  bool (*updateFrame)(const EinkFrameData* frame);
};

namespace eink_detail {

inline const EinkBackendOps*& backendRef() {
  static const EinkBackendOps* backend = NULL;
  return backend;
}

inline EinkFrameData* lastFrameRef() {
  static EinkFrameData frame;
  return &frame;
}

inline bool& hasLastFrameRef() {
  static bool hasFrame = false;
  return hasFrame;
}

}  // namespace eink_detail

inline void eink_setBackend(const EinkBackendOps* backend) {
  eink_detail::backendRef() = backend;
}

inline const EinkBackendOps* eink_getBackend() {
  return eink_detail::backendRef();
}

inline bool eink_hasBackend() {
  const EinkBackendOps* backend = eink_getBackend();
  return backend && (backend->init || backend->updateFrame);
}

inline void eink_clearLastFrame() {
  memset(eink_detail::lastFrameRef(), 0, sizeof(EinkFrameData));
  eink_detail::hasLastFrameRef() = false;
}

inline bool eink_getLastFrame(EinkFrameData* out) {
  if (!out || !eink_detail::hasLastFrameRef()) return false;
  *out = *eink_detail::lastFrameRef();
  return true;
}

inline void eink_copyText(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

inline void eink_buildFrameData(const SensorData* sensors,
                                const SystemState* state,
                                const DeviceConfig* config,
                                const PlantConfig* plant,
                                EinkFrameData* out) {
  if (!out) return;

  memset(out, 0, sizeof(*out));
  out->waterLevelOk = true;

  if (sensors) {
    out->airTempC = sensors->airTempC;
    out->airHumidity = sensors->airHumidity;
    out->waterTempC = sensors->waterTempC;
    out->tdsPpm = sensors->tdsPpm;
    out->plantHeightMm = sensors->plantHeightMm;
    out->batteryPercent = sensors->batteryPercent;
    out->waterLevelOk = sensors->waterLevelOk;
  }

  if (state) {
    out->lightsOn = state->lightsOn;
    out->airPumpOn = state->airPumpOn;
  }

  if (config) {
    out->motorHeightMm = config->motorCurrentMm;
    if (config->lightsForceOn) out->lightsOn = true;
    if (config->lightsForceOff) out->lightsOn = false;
    out->growActive = config->growActive;
    out->growPhase = config->growPhase;
    out->growElapsedDays = config->growElapsedDays;
    eink_copyText(out->plantId, sizeof(out->plantId), config->currentPlantId);
  }

  if (plant) {
    eink_copyText(out->plantName, sizeof(out->plantName), plant->name);
    if (out->plantId[0] == '\0') {
      eink_copyText(out->plantId, sizeof(out->plantId), plant->id);
    }

    if (config && config->growActive && plant->phaseCount > 0) {
      uint8_t phase = config->growPhase;
      if (phase >= plant->phaseCount) {
        phase = (uint8_t)(plant->phaseCount - 1);
      }
      out->growPhase = phase;

      const GrowPhaseParams& gp = plant->phases[phase];
      eink_copyText(out->phaseLabel, sizeof(out->phaseLabel), gp.label);
      if (gp.guidance[0] != '\0') {
        eink_copyText(out->phaseGuidance, sizeof(out->phaseGuidance), gp.guidance);
      } else {
        eink_copyText(out->phaseGuidance, sizeof(out->phaseGuidance), gp.label);
      }
    }
  }
}

inline bool eink_shouldUpdate(unsigned long nowMs, unsigned long* lastUpdateMs) {
  if (!lastUpdateMs) return false;

  if (*lastUpdateMs == 0 || (nowMs - *lastUpdateMs) >= EINK_UPDATE_INTERVAL_MS) {
    *lastUpdateMs = nowMs;
    return true;
  }
  return false;
}

inline bool eink_init() {
  if (!ENABLE_EINK_DISPLAY) return false;

  const EinkBackendOps* backend = eink_getBackend();
  if (backend && backend->init) {
    return backend->init();
  }
  return true;
}

inline bool eink_updateFrame(const EinkFrameData* frame) {
  if (!ENABLE_EINK_DISPLAY || !frame) return false;

  *eink_detail::lastFrameRef() = *frame;
  eink_detail::hasLastFrameRef() = true;

  const EinkBackendOps* backend = eink_getBackend();
  if (backend && backend->updateFrame) {
    return backend->updateFrame(frame);
  }
  return true;
}

inline bool eink_updateFromRuntime(const SensorData* sensors,
                                   const SystemState* state,
                                   const DeviceConfig* config,
                                   const PlantConfig* plant) {
  if (!ENABLE_EINK_DISPLAY) return false;

  EinkFrameData frame;
  eink_buildFrameData(sensors, state, config, plant, &frame);
  return eink_updateFrame(&frame);
}

#endif // EINK_DISPLAY_H
