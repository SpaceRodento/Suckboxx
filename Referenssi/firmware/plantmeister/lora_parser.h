/*=====================================================================
  lora_parser.h - Pure LoRa command parser helpers

  Isolated from lora_comm.h so parser logic can be unit-tested in native
  environment without UART/radio dependencies.
=====================================================================*/

#ifndef LORA_PARSER_H
#define LORA_PARSER_H

#include <string.h>

// Parse a command string: "CMD:KEY=VALUE" or "CMD:KEY"
// Returns true if valid command, fills key and value.
inline bool lora_parseCommand(const char* cmd, char* key, int keySize,
                              char* value, int valueSize) {
  if (!cmd || !key || !value || keySize <= 0 || valueSize <= 0) return false;
  if (strncmp(cmd, "CMD:", 4) != 0) return false;

  const char* p = cmd + 4;
  const char* eq = strchr(p, '=');

  if (eq) {
    int kLen = (int)(eq - p);
    if (kLen >= keySize) kLen = keySize - 1;
    strncpy(key, p, kLen);
    key[kLen] = '\0';

    strncpy(value, eq + 1, valueSize - 1);
    value[valueSize - 1] = '\0';
  } else {
    strncpy(key, p, keySize - 1);
    key[keySize - 1] = '\0';
    value[0] = '\0';
  }

  return true;
}

#endif // LORA_PARSER_H
