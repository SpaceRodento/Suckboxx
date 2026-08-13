/*=====================================================================
  lora_payload.h - Shared LoRa report payload formatter

  Pure payload construction helper used by both LoRa backends:
    - RYLR890 (UART/AT)
    - SX1262 (RadioLib)
=====================================================================*/

#ifndef LORA_PAYLOAD_H
#define LORA_PAYLOAD_H

#include "config.h"
#include "structs.h"

#include <stdarg.h>
#include <stdio.h>

inline int lora_payloadAppend(char* payload, size_t payloadSize, int pos,
                              const char* fmt, ...) {
  if (!payload || payloadSize == 0 || pos < 0) return pos;

  size_t used = (size_t)pos;
  if (used >= payloadSize) return (int)(payloadSize - 1);

  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(payload + used, payloadSize - used, fmt, args);
  va_end(args);

  if (written < 0) return pos;
  if ((size_t)written >= payloadSize - used) return (int)(payloadSize - 1);
  return pos + written;
}

inline void lora_buildReportPayload(const SensorData* data, const SystemState* state,
                                    const DeviceConfig* config, char* payload,
                                    size_t payloadSize) {
  if (!payload || payloadSize == 0) return;
  payload[0] = '\0';

  if (!data || !state || !config) return;

  int pos = 0;
  pos = lora_payloadAppend(payload, payloadSize, pos, "KA:");

  if (data->envValid) {
    pos = lora_payloadAppend(payload, payloadSize, pos,
                             "T=%.1f,H=%.0f,P=%.0f,",
                             data->airTempC, data->airHumidity, data->airPressureHpa);
  }

  if (data->waterTempValid) {
    pos = lora_payloadAppend(payload, payloadSize, pos,
                             "WT=%.1f,", data->waterTempC);
  }

  if (data->tdsValid) {
    pos = lora_payloadAppend(payload, payloadSize, pos,
                             "TDS=%d,", data->tdsPpm);
  }

  if (data->heightValid) {
    pos = lora_payloadAppend(payload, payloadSize, pos,
                             "PH=%d,", data->plantHeightMm);
  }

  pos = lora_payloadAppend(payload, payloadSize, pos,
                           "MH=%d,", config->motorCurrentMm);

  if (config->currentPlantId[0] != '\0') {
    pos = lora_payloadAppend(payload, payloadSize, pos,
                             "PI=%s,", config->currentPlantId);
  }

#if ENABLE_FLOAT_SWITCH
  pos = lora_payloadAppend(payload, payloadSize, pos,
                           "WL=%d,", data->waterLevelOk ? 1 : 0);
#endif

  pos = lora_payloadAppend(payload, payloadSize, pos,
                           "L=%d,AP=%d,BAT=%.1f,BP=%d",
                           state->lightsOn ? 1 : 0,
                           state->airPumpOn ? 1 : 0,
                           data->batteryVoltage,
                           data->batteryPercent);

#if ENABLE_GUIDED_GROWING
  if (config->growActive) {
    pos = lora_payloadAppend(payload, payloadSize, pos,
                             ",GP=%u,GD=%u",
                             (unsigned int)config->growPhase,
                             (unsigned int)config->growElapsedDays);
  }
#endif

#if ENABLE_EBB_FLOW
  pos = lora_payloadAppend(payload, payloadSize, pos,
                           ",EBS=%d,EBF=%d",
                           state->ebbFlowState,
                           state->ebbFlowFaultCode);
#endif

  (void)pos;
}

#endif // LORA_PAYLOAD_H
