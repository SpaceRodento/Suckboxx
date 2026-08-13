/*=====================================================================
  api_contract_validation.h - API contract validators

  Testable validation helpers for REST endpoint inputs.
=====================================================================*/

#ifndef API_CONTRACT_VALIDATION_H
#define API_CONTRACT_VALIDATION_H

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define API_HISTORY_MAX_ENTRIES 500

inline bool api_validateGrowAction(const char* action, char* error, size_t errorSize) {
  if (!action || action[0] == '\0') {
    snprintf(error, errorSize, "missing action");
    return false;
  }

  if (strcmp(action, "start") == 0 || strcmp(action, "next") == 0 ||
      strcmp(action, "delay") == 0 || strcmp(action, "stop") == 0) {
    return true;
  }

  snprintf(error, errorSize, "invalid action");
  return false;
}

inline bool api_validateGrowStartMethod(int startMethod, char* error, size_t errorSize) {
  if (startMethod < 0 || startMethod > 2) {
    snprintf(error, errorSize, "start_method must be 0..2");
    return false;
  }
  return true;
}

inline bool api_validateHistoryRequest(const char* field, int maxEntries,
                                       char* outField, size_t outFieldSize,
                                       char* error, size_t errorSize) {
  const char* selectedField = (field && field[0] != '\0') ? field : "air_temp";
  const char* allowedFields[] = {
    "air_temp", "air_humidity", "plant_height_mm",
    "battery_voltage", "battery_percent", "lora_rssi"
  };

  bool fieldAllowed = false;
  for (size_t i = 0; i < sizeof(allowedFields) / sizeof(allowedFields[0]); i++) {
    if (strcmp(selectedField, allowedFields[i]) == 0) {
      fieldAllowed = true;
      break;
    }
  }

  if (!fieldAllowed) {
    snprintf(error, errorSize, "invalid field");
    return false;
  }

  if (maxEntries < 1 || maxEntries > API_HISTORY_MAX_ENTRIES) {
    snprintf(error, errorSize, "max must be 1..%d", API_HISTORY_MAX_ENTRIES);
    return false;
  }

  if (outField && outFieldSize > 0) {
    strncpy(outField, selectedField, outFieldSize - 1);
    outField[outFieldSize - 1] = '\0';
  }

  return true;
}

inline bool api_validateConfigLoraAddress(int addr, char* error, size_t errorSize) {
  if (addr < 1 || addr > 255) {
    snprintf(error, errorSize, "lora_address must be 1..255");
    return false;
  }
  return true;
}

inline bool api_validateConfigLoraNetworkId(int networkId, char* error, size_t errorSize) {
  if (networkId < 0 || networkId > 255) {
    snprintf(error, errorSize, "lora_network_id must be 0..255");
    return false;
  }
  return true;
}

inline bool api_validateConfigLoraTarget(int target, char* error, size_t errorSize) {
  if (target < 0 || target > 255) {
    snprintf(error, errorSize, "lora_target must be 0..255");
    return false;
  }
  return true;
}

inline bool api_validateConfigLightOnHour(int hour, char* error, size_t errorSize) {
  if (hour < 0 || hour > 23) {
    snprintf(error, errorSize, "light_on_hour must be 0..23");
    return false;
  }
  return true;
}

inline bool api_validateConfigWifiSsid(const char* ssid, char* error, size_t errorSize) {
  if (!ssid) {
    snprintf(error, errorSize, "wifi_ssid must be a string");
    return false;
  }
  if (strlen(ssid) > 31) {
    snprintf(error, errorSize, "wifi_ssid max length is 31");
    return false;
  }
  return true;
}

inline bool api_validateConfigWifiPassword(const char* password, char* error, size_t errorSize) {
  if (!password) {
    snprintf(error, errorSize, "wifi_password must be a string");
    return false;
  }

  size_t len = strlen(password);
  if (len > 63) {
    snprintf(error, errorSize, "wifi_password max length is 63");
    return false;
  }
  if (len > 0 && len < 8) {
    snprintf(error, errorSize, "wifi_password must be empty or at least 8 chars");
    return false;
  }
  return true;
}

inline bool api_validateConfigPlantId(const char* plantId, char* error, size_t errorSize) {
  if (!plantId || plantId[0] == '\0') {
    snprintf(error, errorSize, "plant_id is required");
    return false;
  }
  if (strlen(plantId) > 23) {
    snprintf(error, errorSize, "plant_id max length is 23");
    return false;
  }
  for (const char* p = plantId; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (!(isalnum(c) || c == '_' || c == '-')) {
      snprintf(error, errorSize, "plant_id must use [a-zA-Z0-9_-]");
      return false;
    }
  }
  return true;
}

inline bool api_validateAdminPin(const char* pin, bool allowEmpty,
                                 char* error, size_t errorSize) {
  if (!pin) {
    snprintf(error, errorSize, "pin must be a string");
    return false;
  }

  size_t len = strlen(pin);
  if (len == 0) {
    if (allowEmpty) return true;
    snprintf(error, errorSize, "pin is required");
    return false;
  }

  if (len < 4 || len > 8) {
    snprintf(error, errorSize, "pin must be 4..8 digits");
    return false;
  }

  for (const char* p = pin; *p; p++) {
    if (!isdigit((unsigned char)*p)) {
      snprintf(error, errorSize, "pin must contain digits only");
      return false;
    }
  }

  return true;
}

#endif // API_CONTRACT_VALIDATION_H
