/*=====================================================================
  api_envelope.h - Common API response and field helper utilities

  Keeps endpoint responses and validation errors in a unified envelope:
  { ok, restartRequired, error?, field? }
=====================================================================*/

#ifndef API_ENVELOPE_H
#define API_ENVELOPE_H

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <stdio.h>
#include <string.h>

static void api_sendEnvelope(AsyncWebServerRequest* req, int status, bool ok,
                             bool restartRequired = false, const char* error = NULL,
                             const char* field = NULL) {
  StaticJsonDocument<256> out;
  out["ok"] = ok;
  out["restartRequired"] = restartRequired;
  if (error && error[0] != '\0') {
    out["error"] = error;
  }
  if (field && field[0] != '\0') {
    out["field"] = field;
  }

  char outBuf[256];
  serializeJson(out, outBuf, sizeof(outBuf));
  req->send(status, "application/json", outBuf);
}

static bool api_parseJsonBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                              StaticJsonDocument<512>& out, JsonObjectConst& dataOut) {
  out.clear();

  if (len == 0 || !data) {
    dataOut = out.to<JsonObject>();
    return true;
  }

  DeserializationError err = deserializeJson(out, data, len);
  if (err) {
    api_sendEnvelope(req, 400, false, false, "invalid JSON");
    return false;
  }

  if (!out.is<JsonObject>()) {
    api_sendEnvelope(req, 400, false, false, "JSON body must be object");
    return false;
  }

  dataOut = out.as<JsonObjectConst>();
  return true;
}

static bool api_parseJsonVariant(AsyncWebServerRequest* req, JsonVariant& json,
                                 JsonObjectConst& dataOut) {
  if (json.isNull()) {
    dataOut = JsonObjectConst();
    return true;
  }

  if (!json.is<JsonObject>()) {
    api_sendEnvelope(req, 400, false, false, "JSON body must be object");
    return false;
  }

  dataOut = json.as<JsonObjectConst>();
  return true;
}

static bool api_updateIntField(AsyncWebServerRequest* req, JsonObjectConst src,
                               const char* key, int minV, int maxV, int* target) {
  if (!src.containsKey(key)) return true;

  if (!src[key].is<int>()) {
    char msg[96];
    snprintf(msg, sizeof(msg), "%s must be integer", key);
    api_sendEnvelope(req, 400, false, false, msg, key);
    return false;
  }

  int value = src[key].as<int>();
  if (value < minV || value > maxV) {
    char msg[96];
    snprintf(msg, sizeof(msg), "%s must be %d..%d", key, minV, maxV);
    api_sendEnvelope(req, 400, false, false, msg, key);
    return false;
  }

  *target = value;
  return true;
}

static bool api_updateFloatField(AsyncWebServerRequest* req, JsonObjectConst src,
                                 const char* key, float minV, float maxV, float* target) {
  if (!src.containsKey(key)) return true;

  if (!src[key].is<float>() && !src[key].is<int>()) {
    char msg[96];
    snprintf(msg, sizeof(msg), "%s must be number", key);
    api_sendEnvelope(req, 400, false, false, msg, key);
    return false;
  }

  float value = src[key].as<float>();
  if (value < minV || value > maxV) {
    char msg[96];
    snprintf(msg, sizeof(msg), "%s must be %.3f..%.3f", key, minV, maxV);
    api_sendEnvelope(req, 400, false, false, msg, key);
    return false;
  }

  *target = value;
  return true;
}

static bool api_updateStringField(AsyncWebServerRequest* req, JsonObjectConst src,
                                  const char* key, char* target, size_t maxLen,
                                  bool allowEmpty) {
  if (!src.containsKey(key)) return true;

  if (!src[key].is<const char*>()) {
    char msg[96];
    snprintf(msg, sizeof(msg), "%s must be string", key);
    api_sendEnvelope(req, 400, false, false, msg, key);
    return false;
  }

  const char* value = src[key] | "";
  size_t len = strlen(value);

  if (!allowEmpty && len == 0) {
    char msg[96];
    snprintf(msg, sizeof(msg), "%s is required", key);
    api_sendEnvelope(req, 400, false, false, msg, key);
    return false;
  }

  if (len >= maxLen) {
    char msg[96];
    snprintf(msg, sizeof(msg), "%s max length is %u", key, (unsigned int)(maxLen - 1));
    api_sendEnvelope(req, 400, false, false, msg, key);
    return false;
  }

  strncpy(target, value, maxLen - 1);
  target[maxLen - 1] = '\0';
  return true;
}

// ── Salaisuudet: yksi merkkijono, yksi merkitys ─────────────────────
//
// GET /api/config palauttaa salaisuudet peitettyna, ja POST /api/config
// tulkitsee saman merkkijonon "ala muuta tata kenttaa" -pyynnoksi. Se
// toimii vain jos peittaja ja tunnistaja ovat tasmalleen samaa mielta
// siita mika se merkkijono on — siksi molemmat lukevat sen taalta.
//
// Ennen 16.7.2026 ne EIVAT olleet: api_maskSecret tuotti OSITTAISEN
// maskin ("sala*****23") mutta api_isMaskedSentinel tunnisti vain
// literaalin "********". Portaali taytti salasanakentan GET:n maskilla,
// ja tallennusnappi lahetti sen takaisin: sentinel ei tasmannyt, maski
// oli yli 8 merkkia joten validointi hyvaksyi sen, ja laite tallensi
// oman maskinsa salasanakseen. Kotiverkko katosi eika e-ink saanut enaa
// dataa. Kayttajan ei tarvinnut edes koskea kenttaan — riitti etta
// painoi Tallenna-nappia. Ks. [[project_maski_roundtrip_2026_07_16]].
//
// Osittaisesta maskista luovuttiin myos toisesta syysta: se paljasti
// kuusi merkkia salasanasta kenelle tahansa joka ehti GET /api/config
// avoimen AP:n kantamalla. "Onko arvo asetettu" kerrotaan erikseen
// bool-kentilla (wifi_password_set / admin_pin_set), joten osittaisen
// maskin ainoa hyoty oli jo katettu muualla.
#define API_SECRET_MASK "********"

// Peita salaisuus vastausta varten. Ei-tyhja -> API_SECRET_MASK
// (aina sama, riippumatta arvosta tai sen pituudesta). Tyhja -> tyhja,
// jotta asiakas erottaa "ei asetettu" tilasta "asetettu, ei kerrota".
static void api_maskSecret(const char* src, char* dst, size_t dstLen) {
  if (!dst || dstLen == 0) return;
  dst[0] = '\0';

  if (!src || src[0] == '\0') return;

  strncpy(dst, API_SECRET_MASK, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

// Onko asiakkaan lahettama arvo "ala muuta" -pyynto. Tosi tasmalleen
// silloin kun api_maskSecret olisi tuottanut saman merkkijonon.
//
// Sivuvaikutus jonka hyvaksymme: salasanaa "********" ei voi asettaa.
// Vaihtoehto olisi erillinen "muuta salasana" -lippu payloadissa, mika
// monimutkaistaisi jokaisen asiakkaan tavallista polkua yhden absurdin
// salasanan vuoksi. Dokumentoitu: docs/api/rest-api.md.
static bool api_isMaskedSentinel(const char* value) {
  return value && strcmp(value, API_SECRET_MASK) == 0;
}

#endif // API_ENVELOPE_H
