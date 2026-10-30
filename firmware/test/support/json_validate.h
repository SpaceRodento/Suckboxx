#pragma once
// Testiapuri: tiukka RFC 8259 -validaattori (rekursiivinen laskeutuminen).
// Käytetään test_json_writer- ja test_config-testeissä, jotta "JSON on
// kelvollinen" tarkoittaa samaa kuin selaimen JSON.parse eikä pelkkää
// sulkujen laskemista. Ei osa firmwarea.

#include <cstring>

namespace jsoncheck {

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isHex(char c) {
  return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
inline void ws(const char*& p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
}

inline bool string(const char*& p) {
  if (*p != '"') return false;
  ++p;
  while (*p != '"') {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20) return false;  // myös NUL: merkkijono loppui kesken
    if (c == '\\') {
      ++p;
      switch (*p) {
        case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
          ++p;
          break;
        case 'u':
          ++p;
          for (int i = 0; i < 4; ++i, ++p) {
            if (!isHex(*p)) return false;
          }
          break;
        default:
          return false;
      }
    } else {
      ++p;
    }
  }
  ++p;
  return true;
}

inline bool number(const char*& p) {
  if (*p == '-') ++p;
  if (*p == '0') {
    ++p;
  } else if (*p >= '1' && *p <= '9') {
    while (isDigit(*p)) ++p;
  } else {
    return false;
  }
  if (*p == '.') {
    ++p;
    if (!isDigit(*p)) return false;
    while (isDigit(*p)) ++p;
  }
  if (*p == 'e' || *p == 'E') {
    ++p;
    if (*p == '+' || *p == '-') ++p;
    if (!isDigit(*p)) return false;
    while (isDigit(*p)) ++p;
  }
  return true;
}

inline bool literal(const char*& p, const char* lit) {
  const size_t n = std::strlen(lit);
  if (std::strncmp(p, lit, n) != 0) return false;
  p += n;
  return true;
}

inline bool value(const char*& p, int depth) {
  if (depth > 64) return false;
  ws(p);
  switch (*p) {
    case '{':
      ++p;
      ws(p);
      if (*p == '}') { ++p; return true; }
      for (;;) {
        ws(p);
        if (!string(p)) return false;
        ws(p);
        if (*p != ':') return false;
        ++p;
        if (!value(p, depth + 1)) return false;
        ws(p);
        if (*p == ',') { ++p; continue; }
        if (*p == '}') { ++p; return true; }
        return false;
      }
    case '[':
      ++p;
      ws(p);
      if (*p == ']') { ++p; return true; }
      for (;;) {
        if (!value(p, depth + 1)) return false;
        ws(p);
        if (*p == ',') { ++p; continue; }
        if (*p == ']') { ++p; return true; }
        return false;
      }
    case '"': return string(p);
    case 't': return literal(p, "true");
    case 'f': return literal(p, "false");
    case 'n': return literal(p, "null");
    default: return number(p);
  }
}

// Koko merkkijono on täsmälleen yksi JSON-arvo.
inline bool valid(const char* s) {
  if (s == nullptr) return false;
  const char* p = s;
  if (!value(p, 0)) return false;
  ws(p);
  return *p == '\0';
}

}  // namespace jsoncheck
