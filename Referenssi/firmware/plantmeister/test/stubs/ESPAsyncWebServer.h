#pragma once
// Minimal ESPAsyncWebServer stub for native unit tests.
//
// Production code only needs to call `req->send(status, type, body)` through
// `api_sendEnvelope`. In tests we want to verify what api_sendEnvelope
// produced (status, body, field) without dragging in WiFi/AsyncTCP. The
// helper functions in api_envelope.h take an `AsyncWebServerRequest*` so we
// expose a tiny class that records the last send() call.
//
// Tests pass a pointer to a TestAsyncRequest instance everywhere a real
// AsyncWebServerRequest* would be expected. JsonVariant is not needed here
// — helpers operate on JsonObjectConst from ArduinoJson directly.

#include <cstdio>
#include <cstring>

class AsyncWebServerRequest {
public:
  int last_status = 0;
  char last_type[32] = "";
  char last_body[512] = "";
  int send_count = 0;

  void send(int status, const char* type, const char* body) {
    last_status = status;
    if (type) {
      strncpy(last_type, type, sizeof(last_type) - 1);
      last_type[sizeof(last_type) - 1] = '\0';
    } else {
      last_type[0] = '\0';
    }
    if (body) {
      strncpy(last_body, body, sizeof(last_body) - 1);
      last_body[sizeof(last_body) - 1] = '\0';
    } else {
      last_body[0] = '\0';
    }
    send_count++;
  }

  void test_reset() {
    last_status = 0;
    last_type[0] = '\0';
    last_body[0] = '\0';
    send_count = 0;
  }
};
