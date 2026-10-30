#pragma once
// Suckboxx core - pieni JSON-kirjoitin kiinteään puskuriin (WP-3, docs/firmware.md §4.6).
//
// Snapshot (SSE 15 Hz, D9), /api/config ja kalibrointivastaukset kirjoitetaan
// tällä. Puskuri on kutsujan, eikä kirjoitin varaa muistia eikä heitä
// poikkeuksia. Ylivuoto on tarttuva lippu: kun jokin ei mahdu, mitään ei enää
// kirjoiteta, joten puskurissa on aina eheä etuliite koko tulosteesta ja se on
// aina NUL-päätetty. Tällaista tulosta ei silti saa lähettää - kutsuja
// tarkistaa overflow()/ok() ennen lähetystä.
//
// Pilkut ja kaksoispisteet tulevat automaattisesti sisäkkäisyyspinon mukaan,
// joten snapshotin koodi on pelkkiä key()/value()-kutsuja ilman
// "onko tämä ensimmäinen kenttä" -kirjanpitoa.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sbx {

// Kiinteädesimaalinen muotoilu snprintf("%.*f"):llä kahdella korjauksella:
//  - desimaalierotin on aina '.', vaikka C-kirjaston LC_NUMERIC olisi
//    vaihdettu (JSON ja CSV vaativat pisteen; suomen locale antaisi pilkun).
//    %f ei koskaan tuota muita pilkkuja, joten korvaus on turvallinen.
//  - "-0.00" -> "0.00": pienen negatiivisen luvun pyöristys nollaksi ei saa
//    näkyä UI:ssa tai CSV:ssä miinusmerkkisenä nollana.
// trim = true poistaa desimaaliosan perässä olevat nollat ("1.500" -> "1.5",
// "2700.000" -> "2700"), jolloin konfiguraatio-JSON on luettavampi.
// Palauttaa kirjoitettujen merkkien määrän; 0 jos ei mahtunut (out ei
// silloin sisällä mitään käyttökelpoista). Ei-äärellinen arvo -> "nan".
inline size_t formatFixed(char* out, size_t n, float v, int decimals, bool trim = false) {
  if (out == nullptr || n == 0) return 0;
  if (!std::isfinite(v)) {
    if (n < 4) { out[0] = '\0'; return 0; }
    std::memcpy(out, "nan", 4);
    return 3;
  }
  // 9 desimaalia riittää kaikkeen floatin tarkkuuteen; enempi olisi kohinaa.
  if (decimals < 0) decimals = 0;
  if (decimals > 9) decimals = 9;
  const int r = std::snprintf(out, n, "%.*f", decimals, static_cast<double>(v));
  if (r < 0 || static_cast<size_t>(r) >= n) { out[0] = '\0'; return 0; }
  size_t len = static_cast<size_t>(r);
  bool all_zero = true;
  bool has_dot = false;
  for (size_t i = 0; i < len; ++i) {
    if (out[i] == ',') out[i] = '.';
    if (out[i] == '.') has_dot = true;
    if (out[i] >= '1' && out[i] <= '9') all_zero = false;
  }
  if (all_zero && out[0] == '-') {
    std::memmove(out, out + 1, len);  // siirtää myös NULin
    --len;
  }
  if (trim && has_dot) {
    while (out[len - 1] == '0') --len;   // pysähtyy viimeistään pisteeseen
    if (out[len - 1] == '.') --len;      // "2700." -> "2700"
    out[len] = '\0';
  }
  return len;
}

class JsonWriter {
 public:
  // Taso 0 on juuritaso, joten 31 sisäkkäistä säiliötä mahtuu 32-bittisiin
  // pinoihin. Snapshot käyttää neljää.
  static constexpr int kMaxDepth = 31;

  JsonWriter(char* buf, size_t cap) : buf_(buf), cap_(buf != nullptr ? cap : 0) { reset(); }

  // Aloittaa alusta samaan puskuriin (esim. SSE-viesti kerrallaan).
  void reset() {
    len_ = 0;
    depth_ = 0;
    comma_bits_ = 0;
    obj_bits_ = 0;
    after_key_ = false;
    overflow_ = false;
    misuse_ = false;
    if (cap_ > 0) buf_[0] = '\0';
  }

  JsonWriter& beginObject() { open('{', true); return *this; }
  JsonWriter& endObject() { close('}', true); return *this; }
  JsonWriter& beginArray() { open('[', false); return *this; }
  JsonWriter& endArray() { close(']', false); return *this; }

  JsonWriter& key(const char* k) {
    // Avain kuuluu vain objektin sisään, eikä kahta avainta saa olla peräkkäin.
    if (depth_ == 0 || !testBit(obj_bits_, depth_) || after_key_) misuse_ = true;
    separator();
    writeString(k != nullptr ? k : "");
    put(':');
    after_key_ = true;
    return *this;
  }

  // Liukuluku kiinteillä desimaaleilla. NaN/Inf eivät ole JSONia -> null,
  // jolloin UI näyttää "–" eikä koko viesti hylkäydy JSON.parse:ssa.
  JsonWriter& value(float v, int decimals) { return number(v, decimals, false); }
  // Kuten yllä, mutta desimaaliosan loppunollat pois ("1.5", "2700").
  JsonWriter& valueTrim(float v, int max_decimals) { return number(v, max_decimals, true); }

  // Kokonaisluvut: kaikki perustyypit omina ylikuormituksinaan, jotta
  // uint32_t/size_t/int64_t eivät ole monitulkintaisia kummallakaan alustalla
  // (ESP32:lla uint32_t voi olla unsigned int tai unsigned long). int8/16 ja
  // uint8/16 promotoituvat int:ksi. Liukulukua ei voi kirjoittaa vahingossa
  // ilman desimaalimäärää: value(1.5f) ei käänny.
  JsonWriter& value(int v) { return integer("%d", v); }
  JsonWriter& value(unsigned v) { return integer("%u", v); }
  JsonWriter& value(long v) { return integer("%ld", v); }
  JsonWriter& value(unsigned long v) { return integer("%lu", v); }
  JsonWriter& value(long long v) { return integer("%lld", v); }
  JsonWriter& value(unsigned long long v) { return integer("%llu", v); }

  JsonWriter& value(bool b) {
    beforeValue();
    append(b ? "true" : "false");
    return *this;
  }

  // Merkkijono escapattuna. nullptr -> null.
  JsonWriter& value(const char* s) {
    if (s == nullptr) return null();
    beforeValue();
    writeString(s);
    return *this;
  }

  JsonWriter& null() {
    beforeValue();
    append("null");
    return *this;
  }

  bool overflow() const { return overflow_; }
  // Valmis lähetettäväksi: mahtui, rakenne suljettu eikä API:a käytetty väärin.
  bool ok() const { return !overflow_ && !misuse_ && depth_ == 0 && !after_key_ && len_ > 0; }
  size_t length() const { return len_; }
  const char* c_str() const { return cap_ > 0 ? buf_ : ""; }

 private:
  static bool testBit(uint32_t bits, int d) { return ((bits >> d) & 1u) != 0; }
  static void setBit(uint32_t& bits, int d, bool on) {
    if (on) {
      bits |= (1u << d);
    } else {
      bits &= ~(1u << d);
    }
  }

  void open(char c, bool is_object) {
    // Pino täynnä: ei kirjoiteta mitään (ei edes erotinta), rakenne on jo rikki.
    if (depth_ >= kMaxDepth) {
      misuse_ = true;
      return;
    }
    beforeValue();
    put(c);
    ++depth_;
    setBit(comma_bits_, depth_, false);
    setBit(obj_bits_, depth_, is_object);
  }

  void close(char c, bool is_object) {
    if (depth_ == 0 || testBit(obj_bits_, depth_) != is_object || after_key_) {
      misuse_ = true;
      return;
    }
    put(c);
    --depth_;
  }

  // Ennen arvoa: avaimen jälkeen ei erotinta (kaksoispiste tuli jo), muuten
  // pilkku jos tällä tasolla on jo jotain. Objektissa arvo ilman avainta on virhe.
  void beforeValue() {
    if (after_key_) {
      after_key_ = false;
      return;
    }
    if (depth_ > 0 && testBit(obj_bits_, depth_)) misuse_ = true;
    if (depth_ == 0 && testBit(comma_bits_, 0)) misuse_ = true;  // vain yksi juuriarvo
    separator();
  }

  void separator() {
    if (testBit(comma_bits_, depth_)) put(',');
    setBit(comma_bits_, depth_, true);
  }

  JsonWriter& number(float v, int decimals, bool trim) {
    if (!std::isfinite(v)) return null();
    char tmp[64];  // 3.4e38 + piste + 9 desimaalia = 50 merkkiä
    const size_t m = formatFixed(tmp, sizeof(tmp), v, decimals, trim);
    beforeValue();
    append(tmp, m);
    return *this;
  }

  template <typename T>
  JsonWriter& integer(const char* fmt, T v) {
    char tmp[24];  // 20 numeroa + etumerkki
    const int r = std::snprintf(tmp, sizeof(tmp), fmt, v);
    beforeValue();
    append(tmp, r > 0 ? static_cast<size_t>(r) : 0);
    return *this;
  }

  // RFC 8259: lainausmerkki, kenoviiva ja ohjausmerkit < 0x20 escapataan.
  // Ohjausmerkit muodossa \u00XX (yksi sääntö kaikille). UTF-8-tavut >= 0x80
  // menevät sellaisenaan - suomen ä/ö ja "–" ovat UI:n teksteissä.
  void writeString(const char* s) {
    static const char kHex[] = "0123456789abcdef";
    put('"');
    for (const char* p = s; *p != '\0' && !overflow_; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      if (c == '"' || c == '\\') {
        const char esc[2] = {'\\', static_cast<char>(c)};
        append(esc, 2);
      } else if (c < 0x20) {
        const char esc[6] = {'\\', 'u', '0', '0', kHex[c >> 4], kHex[c & 0x0f]};
        append(esc, 6);
      } else {
        put(static_cast<char>(c));
      }
    }
    put('"');
  }

  void put(char c) { append(&c, 1); }
  void append(const char* s) { append(s, std::strlen(s)); }

  // Kaikki kirjoitus kulkee tätä kautta: joko koko pala mahtuu NULin kanssa
  // tai mitään ei kirjoiteta ja ylivuoto jää päälle.
  void append(const char* s, size_t m) {
    if (overflow_) return;
    if (cap_ == 0 || m >= cap_ - len_) {
      overflow_ = true;
      return;
    }
    std::memcpy(buf_ + len_, s, m);
    len_ += m;
    buf_[len_] = '\0';
  }

  char* buf_;
  size_t cap_;
  size_t len_ = 0;
  int depth_ = 0;
  uint32_t comma_bits_ = 0;  // bitti d: tasolla d on jo arvo -> seuraava tarvitsee pilkun
  uint32_t obj_bits_ = 0;    // bitti d: taso d on objekti (muuten taulukko)
  bool after_key_ = false;
  bool overflow_ = false;
  bool misuse_ = false;
};

}  // namespace sbx
