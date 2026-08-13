/*=====================================================================
  command_validation.h - Command/input validation helpers

  Shared validation for command paths (WiFi portal, LoRa command bridge)
  and strict numeric parsing to avoid permissive atoi/atof behavior.
=====================================================================*/

#ifndef COMMAND_VALIDATION_H
#define COMMAND_VALIDATION_H

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

inline bool cmd_equalsIgnoreCase(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

inline bool cmd_isEmpty(const char* s) {
  return !s || s[0] == '\0';
}

inline bool cmd_isEmptyOrOne(const char* s) {
  return cmd_isEmpty(s) || (s[0] == '1' && s[1] == '\0');
}

inline bool cmd_parseIntStrict(const char* s, int* out) {
  if (!s || !out || s[0] == '\0') return false;

  for (const char* p = s; *p; p++) {
    if (isspace((unsigned char)*p)) return false;
  }

  errno = 0;
  char* endPtr = NULL;
  long value = strtol(s, &endPtr, 10);
  if (errno != 0 || endPtr == s || *endPtr != '\0') return false;
  if (value < INT_MIN || value > INT_MAX) return false;

  *out = (int)value;
  return true;
}

inline bool cmd_parseFloatStrict(const char* s, float* out) {
  if (!s || !out || s[0] == '\0') return false;

  for (const char* p = s; *p; p++) {
    if (isspace((unsigned char)*p)) return false;
  }

  errno = 0;
  char* endPtr = NULL;
  float value = strtof(s, &endPtr);
  if (errno != 0 || endPtr == s || *endPtr != '\0') return false;
  if (!isfinite(value)) return false;

  *out = value;
  return true;
}

inline bool cmd_isPlantIdToken(const char* s) {
  if (!s || s[0] == '\0') return false;
  size_t len = strlen(s);
  if (len >= 24) return false;

  for (const char* p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (!(isalnum(c) || c == '_' || c == '-')) return false;
  }
  return true;
}

inline bool portal_validateCommand(const char* cmd, const char* value,
                                   char* error, size_t errorSize) {
  const char* val = value ? value : "";

  if (!cmd || cmd[0] == '\0') {
    snprintf(error, errorSize, "missing command");
    return false;
  }

  if (cmd_equalsIgnoreCase(cmd, "LIGHT") || cmd_equalsIgnoreCase(cmd, "light_toggle")) {
    if (cmd_isEmpty(val) || strcmp(val, "0") == 0 || strcmp(val, "1") == 0) return true;
    snprintf(error, errorSize, "LIGHT value must be 0, 1 or empty");
    return false;
  }

  if (cmd_equalsIgnoreCase(cmd, "WATER") || cmd_equalsIgnoreCase(cmd, "water")) {
    if (cmd_isEmpty(val)) return true;
    int ml = 0;
    if (!cmd_parseIntStrict(val, &ml) || ml <= 0 || ml > 1000) {
      snprintf(error, errorSize, "WATER value must be 1..1000 ml or empty");
      return false;
    }
    return true;
  }

  if (cmd_equalsIgnoreCase(cmd, "HEIGHT")) {
    int mm = 0;
    if (!cmd_parseIntStrict(val, &mm) || mm < 0 || mm > MOTOR_MAX_HEIGHT_MM) {
      snprintf(error, errorSize, "HEIGHT value must be 0..%d", MOTOR_MAX_HEIGHT_MM);
      return false;
    }
    return true;
  }

  if (cmd_equalsIgnoreCase(cmd, "MOTOR_SPEED") || cmd_equalsIgnoreCase(cmd, "MOTOR_ACC") ||
      cmd_equalsIgnoreCase(cmd, "MOTOR_DEC") || cmd_equalsIgnoreCase(cmd, "MOTOR_FDEC") ||
      cmd_equalsIgnoreCase(cmd, "MOTOR_FEND")) {
    float f = 0.0f;
    if (!cmd_parseFloatStrict(val, &f) || f <= 0.0f || f > 20000.0f) {
      snprintf(error, errorSize, "motor parameter must be >0 and <=20000");
      return false;
    }
    return true;
  }

  if (cmd_equalsIgnoreCase(cmd, "MOTOR_TEST")) {
    int profile = 0;
    if (!cmd_parseIntStrict(val, &profile) || profile < 1 || profile > 6) {
      snprintf(error, errorSize, "MOTOR_TEST profile must be 1..6");
      return false;
    }
    return true;
  }

  if (cmd_equalsIgnoreCase(cmd, "AIRPUMP") || cmd_equalsIgnoreCase(cmd, "air_pump")) {
    if (strcmp(val, "0") == 0 || strcmp(val, "1") == 0) return true;
    snprintf(error, errorSize, "AIRPUMP value must be 0 or 1");
    return false;
  }

  if (cmd_equalsIgnoreCase(cmd, "GROW_START") || cmd_equalsIgnoreCase(cmd, "grow_start")) {
    int method = 0;
    if (cmd_isEmpty(val)) return true;
    if (!cmd_parseIntStrict(val, &method) || method < 0 || method > 2) {
      snprintf(error, errorSize, "GROW_START value must be 0..2");
      return false;
    }
    return true;
  }

  // Askelindeksi: rajavalidointi listaa vasten tehdaan routerissa (lista
  // riippuu aktiivisesta kasvista) — tassa vain kelvollinen kokonaisluku.
  if (cmd_equalsIgnoreCase(cmd, "GROW_STEP_SET") ||
      cmd_equalsIgnoreCase(cmd, "grow_step_set")) {
    int idx = 0;
    if (!cmd_parseIntStrict(val, &idx) || idx < 0 || idx > 255) {
      snprintf(error, errorSize, "GROW_STEP_SET value must be 0..255");
      return false;
    }
    return true;
  }

  if (cmd_equalsIgnoreCase(cmd, "GROW_DEMO") || cmd_equalsIgnoreCase(cmd, "grow_demo")) {
    if (strcmp(val, "0") == 0 || strcmp(val, "1") == 0) return true;
    snprintf(error, errorSize, "GROW_DEMO value must be 0 or 1");
    return false;
  }

  // Huolto/katselmointi: aseta vaiheindeksi / vaihepaiva. Rajavalidointi
  // (phaseCount) tehdaan routerissa — tassa vain kelvollinen 0..255. Sama
  // valkolistasopimus kuin GROW_STEP_SET: handler tuntee komennon vasta kun
  // TAMA rivi paastaa sen /api/command-portista lapi (bugiluokka: EBB_FLOOD
  // 05/2026, FACTORY_RESET + GROW_STEP_* 07/2026).
  if (cmd_equalsIgnoreCase(cmd, "GROW_PHASE_SET") ||
      cmd_equalsIgnoreCase(cmd, "grow_phase_set") ||
      cmd_equalsIgnoreCase(cmd, "GROW_DAY_SET") ||
      cmd_equalsIgnoreCase(cmd, "grow_day_set")) {
    int n = 0;
    if (!cmd_parseIntStrict(val, &n) || n < 0 || n > 255) {
      snprintf(error, errorSize, "value must be 0..255");
      return false;
    }
    return true;
  }

  if (cmd_equalsIgnoreCase(cmd, "PLANT")) {
    if (cmd_isPlantIdToken(val)) return true;
    snprintf(error, errorSize, "PLANT id must be [a-zA-Z0-9_-], max 23 chars");
    return false;
  }

  // Tehdasreset vaatii tasmallisen vahvistuksen. Tarkistus on tassa eika vain
  // handlerissa, jotta lipsahtanut komento saa kunnollisen 400-virheen sen
  // sijaan etta se hyvaksyttaisiin ja hylattaisiin hiljaa syvemmalla. Ainoa
  // komento joka tuhoaa kayttajan dataa peruuttamattomasti — se ei saa olla
  // yhden kirjoitusvirheen paassa.
  if (cmd_equalsIgnoreCase(cmd, "FACTORY_RESET") ||
      cmd_equalsIgnoreCase(cmd, "factory_reset")) {
    if (strcmp(val, "CONFIRM") == 0) return true;
    snprintf(error, errorSize, "FACTORY_RESET requires value=CONFIRM");
    return false;
  }

  if (cmd_equalsIgnoreCase(cmd, "motor_up") || cmd_equalsIgnoreCase(cmd, "motor_down") ||
      cmd_equalsIgnoreCase(cmd, "motor_stop") || cmd_equalsIgnoreCase(cmd, "MOTOR_STOP") ||
      cmd_equalsIgnoreCase(cmd, "water_stop") || cmd_equalsIgnoreCase(cmd, "WATER_STOP") ||
      cmd_equalsIgnoreCase(cmd, "calibrate") || cmd_equalsIgnoreCase(cmd, "AP") ||
      cmd_equalsIgnoreCase(cmd, "sensor_read") || cmd_equalsIgnoreCase(cmd, "SENSOR_READ") ||
      cmd_equalsIgnoreCase(cmd, "REBOOT") || cmd_equalsIgnoreCase(cmd, "reboot") ||
      cmd_equalsIgnoreCase(cmd, "EBB_ACK") || cmd_equalsIgnoreCase(cmd, "ebb_ack") ||
      cmd_equalsIgnoreCase(cmd, "EBB_FLOOD") || cmd_equalsIgnoreCase(cmd, "ebb_flood") ||
      cmd_equalsIgnoreCase(cmd, "CLEAR_FAULT") || cmd_equalsIgnoreCase(cmd, "clear_fault") ||
      cmd_equalsIgnoreCase(cmd, "GROW_NEXT") || cmd_equalsIgnoreCase(cmd, "grow_next") ||
      cmd_equalsIgnoreCase(cmd, "GROW_DELAY") || cmd_equalsIgnoreCase(cmd, "grow_delay") ||
      cmd_equalsIgnoreCase(cmd, "GROW_STOP") || cmd_equalsIgnoreCase(cmd, "grow_stop") ||
      cmd_equalsIgnoreCase(cmd, "GROW_STEP_ACK") || cmd_equalsIgnoreCase(cmd, "grow_step_ack") ||
      cmd_equalsIgnoreCase(cmd, "GROW_STEP_SKIP") || cmd_equalsIgnoreCase(cmd, "grow_step_skip") ||
      cmd_equalsIgnoreCase(cmd, "GROW_STEP_BACK") || cmd_equalsIgnoreCase(cmd, "grow_step_back") ||
      cmd_equalsIgnoreCase(cmd, "MAINTENANCE_ON") || cmd_equalsIgnoreCase(cmd, "maintenance_on") ||
      cmd_equalsIgnoreCase(cmd, "MAINTENANCE_OFF") || cmd_equalsIgnoreCase(cmd, "maintenance_off")) {
    if (cmd_isEmptyOrOne(val)) return true;
    snprintf(error, errorSize, "command does not accept this value");
    return false;
  }

  snprintf(error, errorSize, "unknown command");
  return false;
}

#endif // COMMAND_VALIDATION_H
