/*=====================================================================
  wifi_portal_auth.h - PIN login and session cookie helpers

  Included by wifi_portal.h after portal globals are declared.
=====================================================================*/

#ifndef WIFI_PORTAL_AUTH_H
#define WIFI_PORTAL_AUTH_H

#include <esp_system.h>

#include <stdio.h>
#include <string.h>

#define PORTAL_AUTH_MAX_SESSIONS   4
#define PORTAL_AUTH_SESSION_HEX    32
#define PORTAL_AUTH_MAX_FAILS      5
#define PORTAL_AUTH_LOCKOUT_MS     60000UL
#define PORTAL_AUTH_COOKIE_NAME    "pm_sess"

struct PortalAuthSession {
  bool active;
  char sessionId[PORTAL_AUTH_SESSION_HEX + 1];
  unsigned long expiresAtMs;
};

static PortalAuthSession g_portalAuthSessions[PORTAL_AUTH_MAX_SESSIONS];
static uint8_t g_portalAuthFailedAttempts = 0;
static unsigned long g_portalAuthLockoutUntilMs = 0;

static bool portal_authTimeReached(unsigned long now, unsigned long atMs) {
  return ((long)(now - atMs) >= 0);
}

static bool portal_authIsRequired() {
#if WIFI_PORTAL_REQUIRE_PIN
  return g_portalConfig && g_portalConfig->adminPin[0] != '\0';
#else
  return false;
#endif
}

static void portal_authPruneExpiredSessions() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < PORTAL_AUTH_MAX_SESSIONS; i++) {
    if (!g_portalAuthSessions[i].active) continue;
    if (portal_authTimeReached(now, g_portalAuthSessions[i].expiresAtMs)) {
      g_portalAuthSessions[i].active = false;
      g_portalAuthSessions[i].sessionId[0] = '\0';
      g_portalAuthSessions[i].expiresAtMs = 0;
    }
  }
}

static void portal_authClearSessions() {
  for (uint8_t i = 0; i < PORTAL_AUTH_MAX_SESSIONS; i++) {
    g_portalAuthSessions[i].active = false;
    g_portalAuthSessions[i].sessionId[0] = '\0';
    g_portalAuthSessions[i].expiresAtMs = 0;
  }
  g_portalAuthFailedAttempts = 0;
  g_portalAuthLockoutUntilMs = 0;
}

static bool portal_authIsLockedOut() {
  if (g_portalAuthLockoutUntilMs == 0) return false;
  unsigned long now = millis();
  if (portal_authTimeReached(now, g_portalAuthLockoutUntilMs)) {
    g_portalAuthLockoutUntilMs = 0;
    return false;
  }
  return true;
}

static void portal_authGenerateSessionId(char* out, size_t outLen) {
  if (!out || outLen < (PORTAL_AUTH_SESSION_HEX + 1)) {
    if (out && outLen > 0) out[0] = '\0';
    return;
  }

  static const char* HEX_DIGITS = "0123456789abcdef";
  for (uint8_t i = 0; i < (PORTAL_AUTH_SESSION_HEX / 2); i++) {
    uint8_t b = (uint8_t)(esp_random() & 0xFF);
    out[i * 2] = HEX_DIGITS[(b >> 4) & 0x0F];
    out[i * 2 + 1] = HEX_DIGITS[b & 0x0F];
  }
  out[PORTAL_AUTH_SESSION_HEX] = '\0';
}

static bool portal_authExtractSessionIdFromCookie(const String& cookieHeader,
                                                  char* out, size_t outLen) {
  if (!out || outLen == 0) return false;
  out[0] = '\0';

  const char* key = PORTAL_AUTH_COOKIE_NAME "=";
  const char* cookie = cookieHeader.c_str();
  const char* pos = strstr(cookie, key);
  if (!pos) return false;

  pos += strlen(key);
  size_t i = 0;
  while (pos[i] != '\0' && pos[i] != ';' && pos[i] != ' ' && i < (outLen - 1)) {
    out[i] = pos[i];
    i++;
  }
  out[i] = '\0';

  return i > 0;
}

static bool portal_authHasValidSessionId(const char* sessionId) {
  if (!sessionId || sessionId[0] == '\0') return false;

  portal_authPruneExpiredSessions();

  for (uint8_t i = 0; i < PORTAL_AUTH_MAX_SESSIONS; i++) {
    if (!g_portalAuthSessions[i].active) continue;
    if (strcmp(g_portalAuthSessions[i].sessionId, sessionId) == 0) {
      g_portalAuthSessions[i].expiresAtMs = millis() + WIFI_PORTAL_SESSION_MS;
      return true;
    }
  }

  return false;
}

static void portal_authStoreSession(const char* sessionId) {
  if (!sessionId || sessionId[0] == '\0') return;

  portal_authPruneExpiredSessions();

  int slot = -1;
  for (uint8_t i = 0; i < PORTAL_AUTH_MAX_SESSIONS; i++) {
    if (!g_portalAuthSessions[i].active) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    unsigned long oldest = g_portalAuthSessions[0].expiresAtMs;
    slot = 0;
    for (uint8_t i = 1; i < PORTAL_AUTH_MAX_SESSIONS; i++) {
      if (g_portalAuthSessions[i].expiresAtMs < oldest) {
        oldest = g_portalAuthSessions[i].expiresAtMs;
        slot = i;
      }
    }
  }

  g_portalAuthSessions[slot].active = true;
  strncpy(g_portalAuthSessions[slot].sessionId, sessionId,
          sizeof(g_portalAuthSessions[slot].sessionId) - 1);
  g_portalAuthSessions[slot].sessionId[sizeof(g_portalAuthSessions[slot].sessionId) - 1] = '\0';
  g_portalAuthSessions[slot].expiresAtMs = millis() + WIFI_PORTAL_SESSION_MS;
}

static void portal_authInvalidateSessionFromRequest(AsyncWebServerRequest* req) {
  if (!req || !req->hasHeader("Cookie")) return;

  char sessionId[PORTAL_AUTH_SESSION_HEX + 1];
  if (!portal_authExtractSessionIdFromCookie(req->getHeader("Cookie")->value(),
                                             sessionId, sizeof(sessionId))) {
    return;
  }

  for (uint8_t i = 0; i < PORTAL_AUTH_MAX_SESSIONS; i++) {
    if (!g_portalAuthSessions[i].active) continue;
    if (strcmp(g_portalAuthSessions[i].sessionId, sessionId) == 0) {
      g_portalAuthSessions[i].active = false;
      g_portalAuthSessions[i].sessionId[0] = '\0';
      g_portalAuthSessions[i].expiresAtMs = 0;
      return;
    }
  }
}

static bool portal_isAuthorized(AsyncWebServerRequest* req) {
  if (!portal_authIsRequired()) return true;
  if (!req || !req->hasHeader("Cookie")) return false;

  char sessionId[PORTAL_AUTH_SESSION_HEX + 1];
  if (!portal_authExtractSessionIdFromCookie(req->getHeader("Cookie")->value(),
                                             sessionId, sizeof(sessionId))) {
    return false;
  }

  return portal_authHasValidSessionId(sessionId);
}

static bool portal_requireAuthorized(AsyncWebServerRequest* req) {
  if (portal_isAuthorized(req)) return true;
  api_sendEnvelope(req, 401, false, false, "unauthorized");
  return false;
}

static void portal_authSendEnvelopeWithCookie(AsyncWebServerRequest* req, int status,
                                              bool ok, const char* error,
                                              const char* field,
                                              const char* cookieHeader) {
  StaticJsonDocument<192> out;
  out["ok"] = ok;
  out["restartRequired"] = false;
  if (error && error[0] != '\0') out["error"] = error;
  if (field && field[0] != '\0') out["field"] = field;

  char outBuf[192];
  serializeJson(out, outBuf, sizeof(outBuf));

  AsyncWebServerResponse* response = req->beginResponse(status, "application/json", outBuf);
  if (cookieHeader && cookieHeader[0] != '\0') {
    response->addHeader("Set-Cookie", cookieHeader);
  }
  req->send(response);
}

static void portal_authHandleLogin(AsyncWebServerRequest* req, JsonObjectConst payload) {
  if (!g_portalConfig) {
    api_sendEnvelope(req, 500, false, false, "no config");
    return;
  }

  if (!portal_authIsRequired()) {
    api_sendEnvelope(req, 400, false, false, "auth disabled");
    return;
  }

  const char* pin = payload["pin"] | "";
  char err[96];
  if (!api_validateAdminPin(pin, false, err, sizeof(err))) {
    api_sendEnvelope(req, 400, false, false, err, "pin");
    return;
  }

  if (portal_authIsLockedOut()) {
    api_sendEnvelope(req, 429, false, false, "locked", "pin");
    return;
  }

  if (strcmp(pin, g_portalConfig->adminPin) != 0) {
    g_portalAuthFailedAttempts++;
    if (g_portalAuthFailedAttempts >= PORTAL_AUTH_MAX_FAILS) {
      g_portalAuthFailedAttempts = 0;
      g_portalAuthLockoutUntilMs = millis() + PORTAL_AUTH_LOCKOUT_MS;
      api_sendEnvelope(req, 429, false, false, "locked", "pin");
      return;
    }
    api_sendEnvelope(req, 401, false, false, "unauthorized", "pin");
    return;
  }

  g_portalAuthFailedAttempts = 0;
  g_portalAuthLockoutUntilMs = 0;

  char sessionId[PORTAL_AUTH_SESSION_HEX + 1];
  portal_authGenerateSessionId(sessionId, sizeof(sessionId));
  portal_authStoreSession(sessionId);

  char cookie[160];
  snprintf(cookie, sizeof(cookie),
           PORTAL_AUTH_COOKIE_NAME "=%s; Path=/; Max-Age=%lu; HttpOnly; SameSite=Lax",
           sessionId,
           (unsigned long)(WIFI_PORTAL_SESSION_MS / 1000UL));

  portal_authSendEnvelopeWithCookie(req, 200, true, NULL, NULL, cookie);
}

static void portal_authHandleLogout(AsyncWebServerRequest* req) {
  portal_authInvalidateSessionFromRequest(req);
  portal_authSendEnvelopeWithCookie(req, 200, true, NULL, NULL,
                                    PORTAL_AUTH_COOKIE_NAME "=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
}

#endif // WIFI_PORTAL_AUTH_H
