// S8: syötteiden robustisuus satunnaissyötteillä
// (docs/yoajo/paketit/S8-syotteiden-fuzzaus.md).
//
// Laite jäsentää verkosta tulevaa tekstiä ja PC-työkalu nauhoituksia. Tämä
// kokonaisuus syöttää niille deterministisiä pseudosatunnaisia syötteitä:
//   - POST /api/config -> Processor::applyConfigForm (processor.h, config.h)
//   - POST /api/master -> Processor::applyMasterForm (processor.h)
//   - POST /api/sim    -> simApplyForm (sim_api.h)
//   - nauhoitus-CSV    -> rec::parseCsv, headerConfig, headerCalibration ja
//                         replayRecording (test/support/recording.h)
// Laitteella ESPAsyncWebServer purkaa form-urlencoded-rungon, ja liima kokoaa
// FormPair-taulukon (hal/web.cpp:145 collectForm, enintään 64 paria), joten
// core näkee URL-dekoodatut C-merkkijonot: mitä tahansa tavuja NULiin asti.
//
// Syötteet: kelvolliset arvot (pilkku tai piste, välilyönnit, etumerkki,
// etunollat), alueen ulkopuoliset, NaN/inf, ylivuoto (1e99), heksa,
// ylipitkät (raja 32 tavua), rikkinäiset UTF-8-sekvenssit, NUL, tuntemattomat
// ja lähes oikeat avaimet, toistot, nullptr-avaimet ja -arvot sekä
// satunnaiset tavut. CSV:lle mutaatiot kelvollisista nauhoituksista
// (katkaisu, rivien poisto, toisto ja vaihto, seq-hypyt, otsakkeen arvot,
// sarakeotsake, CRLF, keskeytys, ylipitkät kentät) ja satunnaiset tavut.
//
// Muistivirheet: jokainen avain, arvo, FormPair-taulukko, virhepuskuri ja CSV
// on omassa tarkan kokoisessa keko-varauksessaan (CSV ilman NUL-päätettä),
// joten ASan huomaa yhdenkin tavun yli lukemisen tai kirjoittamisen;
// std::stringin kapasiteetti peittäisi sen. Kaatuminen tai ASan/UBSan-löydös
// on vika (paketin väite).
//
// Determinismi: kiinteät siemenet (kSeed*) ja oma splitmix64, ei <random>-
// jakaumia (niiden tulos riippuu standardikirjastosta). Tutkivaan ajoon:
//   SBX_FUZZ_SCALE=20     20-kertainen tapausmäärä
//   SBX_FUZZ_SEED=0x1234  XOR kaikkiin siemeniin
// Epäonnistuessa tulostuu tapauksen numero ja syöte (\xNN-muodossa).
//
// Väitteet ja päättäjät:
//   - Hylätty lomake ei muuta mitään (atomisuus): docs/firmware.md §4.4,
//     processor.h:254-258, sim_api.h:7-8 ja :201-205.
//   - Hyväksytyn jälkeen jokainen kenttä on alueellaan ja configValidate
//     hyväksyy konfiguraation: config.h:62-63 ja :406-409, processor.h:91-95.
//     Sim: arvo rajataan avaimen alueelle ja kokonaislukuavain pysyy
//     kokonaislukuna: sim_api.h:9-10 ja :110-111.
//   - Lomakkeessa mainitsemattomat kentät eivät muutu: processor.h:265
//     (kopio nykyisestä), sim_api.h:11-12.
//   - Yksikin hylättävä pari hylkää koko lomakkeen (§4.4, sim_api.h:7-8).
//     Hylättäviä: tuntematon avain (tarkka vertailu rekisteriin,
//     config.h:304-310, sim_api.h:77-83), nullptr-arvo (config.h:361), tyhjä
//     tai ≥ 32 tavua (config.h:368-369, sim_api.h:152-153), muu merkki kuin
//     numero, etumerkki, erotin tai e/E (config.h:381-388, sim_api.h:158-165),
//     roska luvun perässä (config.h:391), configissa ei-äärellinen tai alueen
//     ulkopuolinen (config.h:333-336), kokonaislukukenttään murtoluku
//     (config.h:335, sim_api.h:196).
//   - Kelvollinen arvo luetaan tarkasti (%.9g palautuu floatiksi bitilleen),
//     erottimena pilkku tai piste ja tyhjää ympärillä: config.h:347-357,
//     sim_api.h:140-143. Sim: ylivuoto rajataan alueen laitaan
//     (sim_api.h:141-143, :195). Parit kopioon järjestyksessä, joten
//     viimeinen esiintymä ratkaisee (processor.h:255).
//   - Virheteksti: NUL-päätetty puskurissa, ei tyhjä hylätessä (UI näyttää sen,
//     form.h:11), ei katkea kesken UTF-8-merkin, kun syöte on kelvollista
//     UTF-8:aa (form.h:11-13), ja sendOk-kääre (hal/web.cpp:132-140) tuottaa
//     kelvollista JSONia. n = 0 ei kirjoita mitään (form.h:60-61).
//   - /api/master: hyväksytään vain, kun viimeinen ch on tasan "0", "1" tai "2"
//     (processor.h:237-239); sama kanava uudelleen ei ole muutos
//     (processor.h:227).
//   - Hyväksytyllä konfiguraatiolla ja satunnaisilla raakakehyksillä
//     snapshot mahtuu kSnapshotJsonSize:en ja on kelvollista JSONia
//     (snapshot.h:13-18, :38-44). Hyväksytyt sim-parametrit mahtuvat
//     kSimJsonSize:en (sim_api.h:250-252), ja EngineSim ajaa niillä.
//   - CSV: false <=> err ei tyhjä (recording.h:184-188). Hyväksytyssä seq
//     kasvaa 1…65535 per rivi ja gaps/missing vastaavat hyppyjä
//     (recording.h:88-89, :184-188, firmware.md §6.1), rail_mv = 0
//     (recording.h:85). Otsakkeen konfiguraatio on alueellaan
//     (recording.h:357-359), toisto onnistuu tasan, kun otsake kelpaa
//     (recording.h:462-492), ja toiston kalibrointi on siivottu
//     (processor.h:107-112, :121-127).
//   - Generaattorin oma tarkistus (ehdotus): hyväksyttyjä ja hylättyjä
//     tapauksia on kumpiakin vähintään 5 %, jotta kumpikin polku tulee ajetuksi.

#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/calibration.h"
#include "core/config.h"
#include "core/engine_sim.h"
#include "core/form.h"
#include "core/json_writer.h"
#include "core/processor.h"
#include "core/sim_api.h"
#include "core/snapshot.h"
#include "json_validate.h"
#include "recording.h"

using sbx::Calibration;
using sbx::Config;
using sbx::EngineSim;
using sbx::FieldInfo;
using sbx::FieldType;
using sbx::FormPair;
using sbx::JsonWriter;
using sbx::Processor;
using sbx::RawFrame;
using sbx::RawSample;
using sbx::SimFault;
using sbx::SimParams;
using sbx::kChannels;
namespace rec = sbx::rec;

void setUp() {}
void tearDown() {}

namespace {

// Tapausmäärät (paketti: vähintään 10 000 per jäsennin, ajo alle 10 s) ja
// siemenet ("S8" = 0x53 0x38).
constexpr int kConfigCases = 12000;
constexpr int kMasterCases = 10000;
constexpr int kSimCases = 12000;
constexpr int kCsvCases = 10000;
constexpr uint64_t kSeedConfig = 0x5338000000000001ull;
constexpr uint64_t kSeedMaster = 0x5338000000000002ull;
constexpr uint64_t kSeedSim = 0x5338000000000003ull;
constexpr uint64_t kSeedCsv = 0x5338000000000004ull;

// --- satunnaisluvut ----------------------------------------------------------

class Rng {
 public:
  explicit Rng(uint64_t seed) : s_(seed) {}
  uint64_t next() {  // splitmix64
    uint64_t z = (s_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  uint32_t below(uint32_t n) { return n == 0 ? 0u : static_cast<uint32_t>(next() % n); }
  bool chance(uint32_t percent) { return below(100) < percent; }
  float unit() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }  // [0, 1)
  float range(float lo, float hi) {
    const float v = lo + (hi - lo) * unit();
    return v < lo ? lo : (v > hi ? hi : v);
  }
  char byte() { return static_cast<char>(below(256)); }

 private:
  uint64_t s_;
};

int fuzzScale() {
  const char* e = std::getenv("SBX_FUZZ_SCALE");
  const long v = e != nullptr ? std::strtol(e, nullptr, 10) : 1;
  return (v >= 1 && v <= 10000) ? static_cast<int>(v) : 1;
}

uint64_t fuzzSeed(uint64_t base) {
  const char* e = std::getenv("SBX_FUZZ_SEED");
  return e != nullptr ? base ^ std::strtoull(e, nullptr, 0) : base;
}

// --- merkkijonot ---------------------------------------------------------------

std::string sformat(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string sformat(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return std::string();
  return std::string(buf, static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1);
}

// Tulostettava muoto virheilmoitukseen: ei-ASCII ja ohjausmerkit \xNN:nä.
std::string escape(const std::string& s, size_t max_len) {
  std::string out;
  for (size_t i = 0; i < s.size() && i < max_len; ++i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    if (ch == '\\') {
      out += "\\\\";
    } else if (ch == '\n') {
      out += "\\n";
    } else if (ch == '\r') {
      out += "\\r";
    } else if (ch < 0x20 || ch >= 0x7F) {
      out += sformat("\\x%02X", static_cast<unsigned>(ch));
    } else {
      out += static_cast<char>(ch);
    }
  }
  if (s.size() > max_len) out += sformat("...(%lu tavua)", static_cast<unsigned long>(s.size()));
  return out;
}

// Jäsentimen näkemä C-merkkijono: kaikki ensimmäiseen NULiin asti.
std::string cstr(const std::string& s) { return s.substr(0, s.find('\0')); }

// Tiukka UTF-8 (ei ylipitkiä muotoja, ei surrogaatteja, enintään U+10FFFF).
bool validUtf8(const char* s) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  while (*p != 0) {
    const unsigned c0 = *p;
    if (c0 < 0x80) {
      ++p;
      continue;
    }
    int n = 0;
    uint32_t cp = 0;
    if ((c0 & 0xE0u) == 0xC0u) {
      n = 1;
      cp = c0 & 0x1Fu;
    } else if ((c0 & 0xF0u) == 0xE0u) {
      n = 2;
      cp = c0 & 0x0Fu;
    } else if ((c0 & 0xF8u) == 0xF0u) {
      n = 3;
      cp = c0 & 0x07u;
    } else {
      return false;
    }
    for (int i = 1; i <= n; ++i) {  // NUL ei ole jatkotavu, joten luku pysähtyy siihen
      if ((p[i] & 0xC0u) != 0x80u) return false;
      cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if ((n == 1 && cp < 0x80u) || (n == 2 && cp < 0x800u) || (n == 3 && cp < 0x10000u) ||
        cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
      return false;
    }
    p += n + 1;
  }
  return true;
}

bool sameBits(float a, float b) {
  uint32_t x = 0;
  uint32_t y = 0;
  std::memcpy(&x, &a, sizeof(x));
  std::memcpy(&y, &b, sizeof(y));
  return x == y;
}

// --- lomakkeet ---------------------------------------------------------------

enum class Expect : uint8_t { Ok, Reject, Unknown };

struct GenPair {
  bool key_null = false;
  bool value_null = false;
  std::string key;
  std::string value;
  int idx = -1;                      // rekisterin indeksi jäsentimen näkemällä avaimella
  Expect expect = Expect::Unknown;
  float v = 0.0f;                    // odotettu arvo, kun expect == Ok
};

struct GenForm {
  std::vector<GenPair> pairs;
  bool pass_null = false;  // pairs-osoitin nullptr
  int count = 0;           // kutsun count (tavallisesti pairs.size())
};

// FormPair-taulukko ja jokainen merkkijono omassa tarkan kokoisessa
// varauksessaan, jotta lukeminen count:n tai NULin yli on ASan-löydös.
class ExactForm {
 public:
  explicit ExactForm(const GenForm& g) : pairs_(new FormPair[g.pairs.size()]) {
    for (size_t i = 0; i < g.pairs.size(); ++i) {
      pairs_[i].key = g.pairs[i].key_null ? nullptr : copy(g.pairs[i].key);
      pairs_[i].value = g.pairs[i].value_null ? nullptr : copy(g.pairs[i].value);
    }
  }
  const FormPair* pairs() const { return pairs_.get(); }

 private:
  const char* copy(const std::string& s) {
    std::unique_ptr<char[]> p(new char[s.size() + 1]);
    if (!s.empty()) std::memcpy(p.get(), s.data(), s.size());
    p[s.size()] = '\0';
    store_.push_back(std::move(p));
    return store_.back().get();
  }
  std::unique_ptr<FormPair[]> pairs_;
  std::vector<std::unique_ptr<char[]>> store_;
};

// Virhepuskuri: liiman koko (kFormErrLen), nullptr + 0, 0 vahtitavulla tai
// satunnainen koko. Tarkka varaus täytettynä 'X':llä: kutsun on itse
// NUL-päätettävä ja pysyttävä puskurissa.
struct ErrBuf {
  std::unique_ptr<char[]> mem;
  char* ptr = nullptr;
  size_t len = 0;
  bool sentinel = false;  // ptr != nullptr ja len == 0: tavu ei saa muuttua
};

ErrBuf makeErr(Rng& r) {
  ErrBuf e;
  const uint32_t k = r.below(100);
  if (k < 70) {
    e.len = sbx::kFormErrLen;
  } else if (k < 80) {
    return e;
  } else if (k < 85) {
    e.mem.reset(new char[1]);
    e.mem[0] = 'Z';
    e.ptr = e.mem.get();
    e.sentinel = true;
    return e;
  } else {
    e.len = 1 + r.below(static_cast<uint32_t>(sbx::kFormErrLen) + 64u);
  }
  e.mem.reset(new char[e.len]);
  std::memset(e.mem.get(), 'X', e.len);
  e.ptr = e.mem.get();
  return e;
}

// hal/web.cpp:132-140 sendOk: {"ok":false,"err":teksti} 512 tavun puskuriin;
// jos ei mahdu, liima lähettää vakiotekstin. Liiman virhepuskurilla
// (kFormErrLen, web.cpp:177) teksti on enintään 191 tavua, ja ilman
// ohjausmerkkejä escapaus enintään kaksinkertaistaa sen (json_writer.h
// writeString), joten silloin oikea teksti mahtuu aina.
const char* checkSendOk(const char* text, bool glue_size) {
  char buf[512];
  JsonWriter w(buf, sizeof(buf));
  w.beginObject();
  w.key("ok").value(false);
  w.key("err").value(text);
  w.endObject();
  if (!w.ok()) {
    if (!glue_size) return nullptr;
    for (const char* p = text; *p != '\0'; ++p) {
      if (static_cast<unsigned char>(*p) < 0x20u) return nullptr;  // vakioteksti, sallittu
    }
    return "sendOk-vastaus ei mahdu 512 tavuun, vaikka virheteksti on liiman puskurin kokoinen";
  }
  return jsoncheck::valid(buf) ? nullptr : "sendOk-vastaus ei ole kelvollista JSONia";
}

// nullptr = kunnossa, muuten väitteen kuvaus.
const char* checkErr(const ErrBuf& e, bool accepted, bool inputs_utf8) {
  if (e.sentinel) return e.mem[0] == 'Z' ? nullptr : "n = 0 kirjoitti virhepuskuriin (form.h:60-61)";
  if (e.ptr == nullptr) return nullptr;
  if (std::memchr(e.ptr, '\0', e.len) == nullptr) return "virheteksti ei ole NUL-päätetty puskurissa";
  if (!accepted && e.len >= 2 && e.ptr[0] == '\0') return "hylkäys ilman virhetekstiä (form.h:11)";
  if (inputs_utf8 && !validUtf8(e.ptr)) return "virheteksti ei ole kelvollista UTF-8:aa (form.h:11-13)";
  return checkSendOk(e.ptr, e.len <= sbx::kFormErrLen);
}

bool formUtf8(const GenForm& g) {
  for (const GenPair& p : g.pairs) {
    if (!p.key_null && !validUtf8(p.key.c_str())) return false;
    if (!p.value_null && !validUtf8(p.value.c_str())) return false;
  }
  return true;
}

// --- epäonnistumisen raportti ----------------------------------------------------

struct Ctx {
  const char* target = "";
  uint64_t seed = 0;
  int index = 0;
  const GenForm* form = nullptr;
  const std::string* csv = nullptr;
};

void failCase(const Ctx& c, const char* what) {
  std::string msg = sformat("%s: tapaus %d, siemen 0x%llx", c.target, c.index,
                            static_cast<unsigned long long>(c.seed));
  if (c.form != nullptr) {
    msg += sformat(", pairs=%s count=%d:", c.form->pass_null ? "nullptr" : "taulukko", c.form->count);
    for (const GenPair& p : c.form->pairs) {
      msg += " [";
      msg += p.key_null ? std::string("(null)") : "\"" + escape(p.key, 64) + "\"";
      msg += "]=[";
      msg += p.value_null ? std::string("(null)") : "\"" + escape(p.value, 64) + "\"";
      msg += "]";
    }
  }
  if (c.csv != nullptr) msg += "\nsyöte: \"" + escape(*c.csv, 6000) + "\"";
  TEST_MESSAGE(msg.c_str());
  TEST_FAIL_MESSAGE(what);
}

#define FUZZ_CHECK(ctx, cond, what)      \
  do {                                   \
    if (!(cond)) failCase((ctx), (what)); \
  } while (0)

void reportCounts(const char* target, int cases, int accepted) {
  const std::string s = sformat("%s: %d tapausta, hyväksytty %d, hylätty %d", target, cases, accepted,
                                cases - accepted);
  TEST_MESSAGE(s.c_str());
}

// Generaattorin oma tarkistus (ehdotus): kumpikin polku vähintään 5 %:lla.
void requireBothPaths(int cases, int accepted) {
  TEST_ASSERT_GREATER_OR_EQUAL(cases / 20, accepted);
  TEST_ASSERT_GREATER_OR_EQUAL(cases / 20, cases - accepted);
}

// --- arvogeneraattorit -------------------------------------------------------------

// Arvotekstien luokat (päättäjät tiedoston otsakkeessa).
enum class Tok : uint8_t {
  Reject,       // kumpikin jäsennin hylkää aina
  PosOverflow,  // config hylkää (inf tai yli alueen), sim rajaa ylärajaan
  NegOverflow,  // config hylkää, sim rajaa alarajaan
  BoolTrue,     // config: bool-kenttä 1, muut hylkäävät; sim hylkää (kirjaimia)
  BoolFalse,
  Unknown,      // lopputulos riippuu kentästä (alue) - vain yleiset väitteet
};

struct Token {
  const char* text;
  size_t len;  // literaalissa voi olla NUL
  Tok cls;
};
#define SBX_TOK(s, cls) {s, sizeof(s) - 1, Tok::cls}

const Token kTokens[] = {
    SBX_TOK("", Reject),
    SBX_TOK(" ", Reject),
    SBX_TOK("\t", Reject),
    SBX_TOK("\r\n", Reject),   // lopun tyhjä trimmataan -> tyhjä
    SBX_TOK("\n1.5", Reject),  // alun rivinvaihtoa ei trimmata
    SBX_TOK("nan", Reject),
    SBX_TOK("NaN", Reject),
    SBX_TOK("-nan", Reject),
    SBX_TOK("inf", Reject),
    SBX_TOK("-inf", Reject),
    SBX_TOK("Infinity", Reject),
    SBX_TOK("INF", Reject),
    SBX_TOK("0x10", Reject),
    SBX_TOK("0X1p3", Reject),
    SBX_TOK("1,5,0", Reject),
    SBX_TOK("1.5.0", Reject),
    SBX_TOK("12abc", Reject),
    SBX_TOK("1e", Reject),
    SBX_TOK("e5", Reject),
    SBX_TOK(".", Reject),
    SBX_TOK(",", Reject),
    SBX_TOK("--1", Reject),
    SBX_TOK("+-1", Reject),
    SBX_TOK("1e+", Reject),
    SBX_TOK("1 5", Reject),
    SBX_TOK("1_000", Reject),
    SBX_TOK("\xD9\xA1", Reject),      // arabialais-intialainen 1
    SBX_TOK("\xEF\xBC\x91", Reject),  // täysleveä 1
    SBX_TOK("\xC3", Reject),          // katkennut 2-tavuinen
    SBX_TOK("\xE2\x82", Reject),      // katkennut 3-tavuinen
    SBX_TOK("\xF0\x9F\x98", Reject),  // katkennut 4-tavuinen
    SBX_TOK("\x80", Reject),          // irrallinen jatkotavu
    SBX_TOK("\xFF\xFE", Reject),
    SBX_TOK("\xC0\xAF", Reject),      // ylipitkä '/'
    SBX_TOK("\xED\xA0\x80", Reject),  // surrogaatti
    SBX_TOK("1\xC3", Reject),
    SBX_TOK("\xC3\xA4", Reject),      // "ä"
    SBX_TOK("yes", Reject),
    SBX_TOK("no", Reject),
    SBX_TOK("y", Reject),
    SBX_TOK("1e99", PosOverflow),
    SBX_TOK("1e39", PosOverflow),
    SBX_TOK("+1e400", PosOverflow),
    SBX_TOK("3.5e38", PosOverflow),
    SBX_TOK("3.4028235e38", PosOverflow),  // FLT_MAX: äärellinen, configille yli alueen
    SBX_TOK("-1e99", NegOverflow),
    SBX_TOK("-1e39", NegOverflow),
    SBX_TOK("-3.4028235e38", NegOverflow),
    SBX_TOK("true", BoolTrue),
    SBX_TOK("TRUE", BoolTrue),
    SBX_TOK("On", BoolTrue),
    SBX_TOK(" true\t", BoolTrue),
    SBX_TOK("false", BoolFalse),
    SBX_TOK("OFF", BoolFalse),
    SBX_TOK("off", BoolFalse),
    SBX_TOK("1e-50", Unknown),
    SBX_TOK("1e-45", Unknown),
    SBX_TOK("-0", Unknown),
    SBX_TOK("+0", Unknown),
    SBX_TOK("0e0", Unknown),
    SBX_TOK("7", Unknown),
    SBX_TOK("1.", Unknown),
    SBX_TOK(".5", Unknown),
    SBX_TOK("5e-1", Unknown),
    SBX_TOK("1.5\n", Unknown),
    SBX_TOK(" 1,5\t", Unknown),
    SBX_TOK("0.9999999999", Unknown),
    SBX_TOK("1.5\0junk", Unknown),  // jäsennin näkee "1.5"
};
#undef SBX_TOK
constexpr uint32_t kTokenCount = sizeof(kTokens) / sizeof(kTokens[0]);

std::string tokenText(const Token& t) { return std::string(t.text, t.len); }

// Avaimiksi: ei-ASCII ja rikkinäiset UTF-8-sekvenssit.
const char* const kOddKeys[] = {"\xD9\xA1", "\xEF\xBC\x91", "\xC3",         "\xE2\x82", "\xF0\x9F\x98", "\x80",
                                "\xFF\xFE", "\xC0\xAF",     "\xED\xA0\x80", "1\xC3",    "\xC3\xA4"};

std::string numText(float v) { return sformat("%.9g", static_cast<double>(v)); }

// Pelkkä tyhjä ympärille: config.h:362-367 trimmaa alusta ' ' ja '\t',
// lopusta lisäksi '\r' ja '\n' (sim_api.h:146-150 samoin).
std::string padSpace(Rng& r, const std::string& core) {
  static const char kLead[] = {' ', '\t'};
  static const char kTail[] = {' ', '\t', '\r', '\n'};
  std::string out;
  if (r.chance(15)) {
    for (uint32_t i = 0, n = 1 + r.below(3); i < n; ++i) out += kLead[r.below(2)];
  }
  out += core;
  if (r.chance(15)) {
    for (uint32_t i = 0, n = 1 + r.below(3); i < n; ++i) out += kTail[r.below(4)];
  }
  return out;
}

// Luvun koristelu: pilkku, +-merkki, etunollat ja tyhjä ympärillä. too_long =
// trimmattu teksti on vähintään 32 tavua (config.h:368-369, sim_api.h:152-153).
std::string decorate(Rng& r, const std::string& num, bool& too_long) {
  std::string core = num;
  if (r.chance(40)) {
    for (char& ch : core) {
      if (ch == '.') ch = ',';
    }
  }
  if (r.chance(15) && core[0] != '-') core.insert(0, "+");
  if (r.chance(10)) {
    const size_t at = (core[0] == '-' || core[0] == '+') ? 1u : 0u;
    core.insert(at, std::string(1 + r.below(3), '0'));
  }
  too_long = core.size() >= 32;
  return padSpace(r, core);
}

std::string intText(Rng& r, int k) {
  switch (r.below(5)) {
    case 0: return sformat("%d.0", k);
    case 1: return sformat("%d,00", k);
    case 2: return sformat("%de0", k);
    default: return sformat("%d", k);
  }
}

// Ylipitkä luku: L-1 nollaa ja "1" (L = 31 kelpaa vielä, 32 ei).
std::string longNumber(Rng& r, bool& too_long) {
  static const size_t kLens[] = {31, 32, 33, 64, 1000};
  const size_t len = kLens[r.below(5)];
  too_long = len >= 32;
  return padSpace(r, std::string(len - 1, '0') + "1");
}

std::string randomBytes(Rng& r, uint32_t max_len) {
  static const char kNumChars[] = "0123456789.,+-eE \t\r\n";
  std::string s;
  for (uint32_t i = 0, n = r.below(max_len + 1); i < n; ++i) {
    s += r.chance(50) ? kNumChars[r.below(sizeof(kNumChars) - 1)] : r.byte();
  }
  return s;
}

// Kelvollisen tekstin rikkominen: katkaisu, tuplaus, lisätty tai poistettu merkki.
std::string mutateText(Rng& r, std::string s) {
  static const char kIns[] = "0123456789.,+-eExX \t\xC3\xA4\x80";
  switch (r.below(4)) {
    case 0:
      s.resize(r.below(static_cast<uint32_t>(s.size()) + 1));
      break;
    case 1:
      s += s;
      break;
    case 2:
      s.insert(r.below(static_cast<uint32_t>(s.size()) + 1), 1, kIns[r.below(sizeof(kIns) - 1)]);
      break;
    default:
      if (!s.empty()) s.erase(r.below(static_cast<uint32_t>(s.size())), 1);
      break;
  }
  return s;
}

// --- avaingeneraattori ---------------------------------------------------------------

struct KeySet {
  std::vector<std::string> known;    // tämän reitin rekisteri (spesifikaatio)
  std::vector<std::string> foreign;  // toisen reitin avaimet (tuntemattomia täällä)
  int find(const std::string& k) const {
    for (size_t i = 0; i < known.size(); ++i) {
      if (known[i] == k) return static_cast<int>(i);
    }
    return -1;
  }
};

KeySet configKeys() {
  KeySet ks;
  for (int i = 0; i < sbx::kConfigFieldCount; ++i) ks.known.emplace_back(sbx::configField(i).key);
  for (int i = 0; i < sbx::kSimKeyCount; ++i) ks.foreign.emplace_back(sbx::simKey(i).key);
  return ks;
}

KeySet simKeys() {
  KeySet ks;
  for (int i = 0; i < sbx::kSimKeyCount; ++i) ks.known.emplace_back(sbx::simKey(i).key);
  for (int i = 0; i < sbx::kConfigFieldCount; ++i) ks.foreign.emplace_back(sbx::configField(i).key);
  return ks;
}

// Avain: enimmäkseen tunnettu, muuten lähes oikea (iso kirjain, tyhjää,
// katkaistu, jäsenpolku), toisen reitin, satunnainen, tyhjä, ylipitkä,
// rikkinäinen UTF-8, NUL perässä tai nullptr. idx jäsentimen näkemästä
// C-merkkijonosta.
void genKey(Rng& r, const KeySet& ks, GenPair& p) {
  const std::string& base = ks.known[r.below(static_cast<uint32_t>(ks.known.size()))];
  const uint32_t k = r.below(100);
  if (k < 75) {
    p.key = base;
  } else if (k < 79) {
    p.key = base;
    p.key[0] = static_cast<char>(p.key[0] - 'a' + 'A');
  } else if (k < 82) {
    p.key = r.chance(50) ? base + " " : " " + base;
  } else if (k < 84) {
    p.key = base.substr(0, base.size() - 1);
  } else if (k < 86) {
    p.key = "status." + base;
  } else if (k < 89) {
    p.key = ks.foreign[r.below(static_cast<uint32_t>(ks.foreign.size()))];
  } else if (k < 91) {
    p.key = base + std::string(1, '\0') + "x";  // jäsennin näkee tunnetun avaimen
  } else if (k < 93) {
    p.key = std::string();
  } else if (k < 94) {
    p.key = std::string(300 + r.below(200), 'a');
  } else if (k < 96) {
    p.key = kOddKeys[r.below(sizeof(kOddKeys) / sizeof(kOddKeys[0]))];
  } else if (k < 98) {
    p.key = randomBytes(r, 20);
  } else {
    p.key_null = true;
  }
  p.idx = p.key_null ? -1 : ks.find(cstr(p.key));
}

// --- /api/config ------------------------------------------------------------------

void genConfigValid(Rng& r, const FieldInfo& f, GenPair& p) {
  if (f.type == FieldType::Bool) {
    // config.h:349-350: true/false/1/0 kirjainkoosta riippumatta sekä on/off.
    static const char* const kTrue[] = {"1", "true", "TRUE", "True", "on", "ON", "1.0", "1e0"};
    static const char* const kFalse[] = {"0", "false", "False", "off", "OFF", "0,0", "-0"};
    const bool on = r.chance(50);
    p.value = padSpace(r, on ? kTrue[r.below(8)] : kFalse[r.below(7)]);
    p.expect = Expect::Ok;
    p.v = on ? 1.0f : 0.0f;
    return;
  }
  bool too_long = false;
  if (f.type == FieldType::Float) {
    p.v = r.range(f.min, f.max);
    p.value = decorate(r, numText(p.v), too_long);
  } else {
    const int lo = static_cast<int>(f.min);
    const int hi = static_cast<int>(f.max);
    const int k = lo + static_cast<int>(r.below(static_cast<uint32_t>(hi - lo + 1)));
    p.v = static_cast<float>(k);
    p.value = decorate(r, intText(r, k), too_long);
  }
  p.expect = too_long ? Expect::Reject : Expect::Ok;
}

void genConfigValue(Rng& r, const FieldInfo& f, GenPair& p) {
  const uint32_t k = r.below(100);
  bool too_long = false;
  if (k < 55) {
    genConfigValid(r, f, p);
  } else if (k < 67) {  // alueen ulkopuolella (config.h:336)
    if (f.type == FieldType::Bool) {
      static const char* const kOut[] = {"2", "-1", "1e1", "3"};
      p.value = kOut[r.below(4)];
    } else if (f.type == FieldType::Float) {
      const float span = f.max - f.min;
      float v = f.min;
      while (!(v < f.min || v > f.max)) {
        v = r.chance(50) ? f.min - (0.01f + r.unit() * span) : f.max + (0.01f + r.unit() * span);
      }
      p.value = decorate(r, numText(v), too_long);
    } else {
      const int lo = static_cast<int>(f.min);
      const int hi = static_cast<int>(f.max);
      const int v = r.chance(50) ? lo - 1 - static_cast<int>(r.below(10)) : hi + 1 + static_cast<int>(r.below(300));
      p.value = decorate(r, intText(r, v), too_long);
    }
    p.expect = Expect::Reject;
  } else if (k < 71 && f.type != FieldType::Float) {  // murtoluku kokonaislukukenttään (config.h:335)
    const int lo = static_cast<int>(f.min);
    const int hi = static_cast<int>(f.max);
    const int v = lo + static_cast<int>(r.below(static_cast<uint32_t>(hi - lo + 1)));
    p.value = padSpace(r, sformat(r.chance(50) ? "%d.5" : "%d,25", v));
    p.expect = Expect::Reject;
  } else if (k < 83) {
    const Token& t = kTokens[r.below(kTokenCount)];
    p.value = tokenText(t);
    switch (t.cls) {
      case Tok::Reject:
      case Tok::PosOverflow:
      case Tok::NegOverflow:
        p.expect = Expect::Reject;
        break;
      case Tok::BoolTrue:
      case Tok::BoolFalse:
        p.expect = f.type == FieldType::Bool ? Expect::Ok : Expect::Reject;
        p.v = t.cls == Tok::BoolTrue ? 1.0f : 0.0f;
        break;
      case Tok::Unknown:
        p.expect = Expect::Unknown;
        break;
    }
  } else if (k < 86) {
    p.value_null = true;
    p.expect = Expect::Reject;
  } else if (k < 89) {
    p.value = longNumber(r, too_long);
    p.expect = too_long ? Expect::Reject : Expect::Unknown;
  } else if (k < 95) {
    GenPair ok;
    genConfigValid(r, f, ok);
    p.value = mutateText(r, ok.value);
    p.expect = Expect::Unknown;
  } else {
    p.value = randomBytes(r, 40);
    p.expect = Expect::Unknown;
  }
}

GenForm genConfigForm(Rng& r, const KeySet& ks) {
  GenForm g;
  const bool clean = r.chance(45);
  if (clean) {
    // 1-3 kelvollista paria; joka neljännessä avaimet kolmen joukosta (toistoja).
    const uint32_t n = 1 + r.below(3);
    const bool repeat = r.chance(25);
    const uint32_t pool0 = r.below(static_cast<uint32_t>(ks.known.size()));
    for (uint32_t i = 0; i < n; ++i) {
      GenPair p;
      const uint32_t idx = repeat ? (pool0 + r.below(3)) % static_cast<uint32_t>(ks.known.size())
                                  : r.below(static_cast<uint32_t>(ks.known.size()));
      p.idx = static_cast<int>(idx);
      p.key = ks.known[idx];
      genConfigValid(r, sbx::configField(p.idx), p);
      g.pairs.push_back(std::move(p));
    }
  } else {
    const uint32_t n = r.chance(90) ? r.below(12) : r.below(71);
    for (uint32_t i = 0; i < n; ++i) {
      GenPair p;
      genKey(r, ks, p);
      if (p.idx >= 0) {
        genConfigValue(r, sbx::configField(p.idx), p);
      } else {
        p.value = r.chance(50) ? numText(r.range(0.0f, 100.0f)) : randomBytes(r, 12);
        p.expect = Expect::Reject;  // tuntematon avain
      }
      g.pairs.push_back(std::move(p));
    }
  }
  g.count = static_cast<int>(g.pairs.size());
  const uint32_t k = r.below(100);
  if (k < 3) {
    g.pass_null = true;
  } else if (k < 6) {
    g.count = r.chance(50) ? 0 : -1 - static_cast<int>(r.below(3));
  }
  return g;
}

struct CfgSnap {
  float v[sbx::kConfigFieldCount];
};

CfgSnap snapConfig(const Config& c) {
  CfgSnap s;
  for (int i = 0; i < sbx::kConfigFieldCount; ++i) s.v[i] = sbx::configGet(c, i);
  return s;
}

bool sameSnap(const CfgSnap& a, const CfgSnap& b, int skip = -1) {
  for (int i = 0; i < sbx::kConfigFieldCount; ++i) {
    if (i != skip && !sameBits(a.v[i], b.v[i])) return false;
  }
  return true;
}

// Satunnaisia raakakehyksiä nykyisellä konfiguraatiolla: ääripäät (0, 3300,
// 65535 mV), kisko 0 tai satunnainen, aukkoja seq:ssä. Sitten tick ja
// snapshot (snapshot.h:13-18, :38-44).
void exerciseProcessor(Processor& p, Rng& r, uint32_t& seq, float& now_s, char* snap_buf, const Ctx& c) {
  for (int i = 0; i < 64; ++i) {
    RawFrame f;
    seq += r.chance(3) ? 1u + r.below(200) : 1u;
    f.seq = seq;
    for (int ch = 0; ch < kChannels; ++ch) {
      const uint32_t k = r.below(10);
      const uint32_t mv = k == 0 ? 0u : k == 1 ? 65535u : k == 2 ? 3300u : k < 6 ? r.below(3301) : 1500u + r.below(1300);
      f.mv[ch] = static_cast<uint16_t>(mv);
    }
    f.rail_mv = r.chance(50) ? 0 : static_cast<uint16_t>(r.below(6000));
    p.push(f);
  }
  now_s += 0.064f;
  p.tick(now_s);
  sbx::SnapshotMeta m;
  m.t_ms = static_cast<uint32_t>(now_s * 1000.0f);
  m.fs_hz = 1000.0f;
  const size_t len = sbx::snapshotJson(p, m, snap_buf, sbx::kSnapshotJsonSize);
  FUZZ_CHECK(c, len > 0, "snapshot ei mahdu kSnapshotJsonSize:en (snapshot.h:38-44)");
  FUZZ_CHECK(c, jsoncheck::valid(snap_buf), "snapshot ei ole kelvollista JSONia (snapshot.h:13-18)");
}

void test_config_form_fuzz() {
  const KeySet ks = configKeys();
  const uint64_t seed = fuzzSeed(kSeedConfig);
  Rng r(seed);
  std::vector<RawSample> ring(sbx::rec::kRingCapacity);
  Processor p(ring.data(), ring.size());
  std::unique_ptr<char[]> snap_buf(new char[sbx::kSnapshotJsonSize]);
  std::unique_ptr<char[]> json_buf(new char[sbx::kConfigJsonSize]);
  uint32_t seq = 0;
  float now_s = 0.0f;
  const int cases = kConfigCases * fuzzScale();
  int accepted = 0;
  for (int n = 0; n < cases; ++n) {
    if (n % 1000 == 999) p.resetConfig();  // takaisin oletuksiin, ettei tila ajaudu
    (void)p.takeConfigDirty();
    const GenForm g = genConfigForm(r, ks);
    Ctx c;
    c.target = "applyConfigForm";
    c.seed = seed;
    c.index = n;
    c.form = &g;

    const Config before = p.config();
    const CfgSnap s0 = snapConfig(before);
    const int master0 = p.master();
    ErrBuf e = makeErr(r);
    const ExactForm xf(g);
    const bool ok = p.applyConfigForm(g.pass_null ? nullptr : xf.pairs(), g.count, e.ptr, e.len);
    const CfgSnap s1 = snapConfig(p.config());
    const bool dirty = p.takeConfigDirty();

    const char* why = checkErr(e, ok, formUtf8(g));
    FUZZ_CHECK(c, why == nullptr, why);

    bool any_reject = false;
    bool all_ok = !g.pairs.empty();
    for (const GenPair& gp : g.pairs) {
      if (gp.key_null || gp.idx < 0 || gp.expect == Expect::Reject) any_reject = true;
      if (gp.key_null || gp.idx < 0 || gp.expect != Expect::Ok) all_ok = false;
    }
    const bool no_pairs = g.pass_null || g.count <= 0;
    if (no_pairs) {
      FUZZ_CHECK(c, !ok, "tyhjä lomake hyväksyttiin (processor.h:261-263)");
    } else if (any_reject) {
      FUZZ_CHECK(c, !ok, "hylättävä pari, mutta lomake hyväksyttiin (§4.4: kaikki tai ei mitään)");
    }

    if (!ok) {
      FUZZ_CHECK(c, sameSnap(s0, s1), "hylätty lomake muutti konfiguraatiota (§4.4)");
      FUZZ_CHECK(c, p.master() == master0, "hylätty lomake muutti masteria (§4.4)");
      FUZZ_CHECK(c, !dirty, "hylätty lomake merkitsi konfiguraation muuttuneeksi (processor.h:254-258)");
    } else {
      ++accepted;
      FUZZ_CHECK(c, dirty, "hyväksytty lomake ei merkinnyt tallennusta (processor.h:254-258)");
      FUZZ_CHECK(c, sbx::configValidate(p.config(), nullptr, 0),
                 "hyväksytty konfiguraatio ei läpäise configValidatea (config.h:406-409)");
      for (int i = 0; i < sbx::kConfigFieldCount; ++i) {
        const FieldInfo& f = sbx::configField(i);
        FUZZ_CHECK(c, s1.v[i] >= f.min && s1.v[i] <= f.max, "hyväksytty arvo alueen ulkopuolella (config.h:62-63)");
        bool mentioned = false;
        for (const GenPair& gp : g.pairs) mentioned = mentioned || gp.idx == i;
        FUZZ_CHECK(c, mentioned || sameBits(s0.v[i], s1.v[i]),
                   "lomakkeessa mainitsematon kenttä muuttui (processor.h:265)");
      }
      FUZZ_CHECK(c, p.master() == static_cast<int>(p.config().status.master),
                 "StatusMachinen master ei vastaa konfiguraatiota");
      // Joka neljännellä hyväksytyllä (ajoaika): DSP, tick ja snapshot sekä
      // GET /api/config, joka mahtuu ja on JSONia (config.h:530-534).
      if (accepted % 4 == 0) {
        exerciseProcessor(p, r, seq, now_s, snap_buf.get(), c);
        const size_t jl = p.configJson(json_buf.get(), sbx::kConfigJsonSize);
        FUZZ_CHECK(c, jl > 0, "configJson ei mahdu kConfigJsonSize:en (config.h:530-534)");
        FUZZ_CHECK(c, jsoncheck::valid(json_buf.get()), "configJson ei ole kelvollista JSONia");
      }
    }

    // Täysi oraakkeli, kun jokainen pari on kelvollinen: odotettu konfiguraatio
    // = edellinen + parit järjestyksessä (processor.h:255), hyväksytään tasan
    // kun configValidate hyväksyy sen.
    if (all_ok && !no_pairs) {
      Config expect = before;
      for (const GenPair& gp : g.pairs) {
        FUZZ_CHECK(c, sbx::configSet(expect, gp.idx, gp.v) == sbx::SetResult::Ok,
                   "generaattori: kelvolliseksi tarkoitettu arvo hylkääntyi configSetissä");
      }
      const bool want = sbx::configValidate(expect, nullptr, 0);
      FUZZ_CHECK(c, ok == want, "hyväksyntä ei vastaa configValidatea odotetulle konfiguraatiolle (§4.4)");
      if (ok) {
        FUZZ_CHECK(c, sameSnap(snapConfig(expect), s1),
                   "kelvollinen arvo luettiin väärin (config.h:347-357; viimeinen esiintymä ratkaisee)");
      }
    }
  }
  reportCounts("applyConfigForm", cases, accepted);
  requireBothPaths(cases, accepted);
}

// --- /api/master ------------------------------------------------------------------

GenForm genMasterForm(Rng& r) {
  static const char* const kKeys[] = {"CH", "ch ", " ch", "c", "master", "channel", "", "ch\xC3"};
  static const char* const kBad[] = {"3",  "-1", "01", " 1", "1 ", "",  "1.0", "+1", "\xEF\xBC\x91",
                                     "1\xC3", "x", "\xC3", "9", "/", ":", "0x0", "10", "2\t"};
  GenForm g;
  const uint32_t n = r.below(6);
  for (uint32_t i = 0; i < n; ++i) {
    GenPair p;
    const uint32_t kk = r.below(100);
    if (kk < 70) {
      p.key = "ch";
    } else if (kk < 75) {
      p.key = std::string("ch\0x", 4);  // jäsennin näkee "ch"
    } else if (kk < 92) {
      p.key = kKeys[r.below(sizeof(kKeys) / sizeof(kKeys[0]))];
    } else if (kk < 96) {
      p.key = randomBytes(r, 6);
    } else {
      p.key_null = true;
    }
    const uint32_t kv = r.below(100);
    if (kv < 45) {
      p.value = std::string(1, static_cast<char>('0' + r.below(3)));
    } else if (kv < 50) {
      p.value = std::string(1, static_cast<char>('0' + r.below(3))) + std::string(1, '\0') + "9";
    } else if (kv < 80) {
      p.value = kBad[r.below(sizeof(kBad) / sizeof(kBad[0]))];
    } else if (kv < 90) {
      p.value = randomBytes(r, 4);
    } else {
      p.value_null = true;
    }
    g.pairs.push_back(std::move(p));
  }
  g.count = static_cast<int>(g.pairs.size());
  const uint32_t k = r.below(100);
  if (k < 3) {
    g.pass_null = true;
  } else if (k < 5) {
    g.count = r.chance(50) ? 0 : -1;
  }
  return g;
}

void test_master_form_fuzz() {
  const uint64_t seed = fuzzSeed(kSeedMaster);
  Rng r(seed);
  std::vector<RawSample> ring(sbx::rec::kRingCapacity);
  Processor p(ring.data(), ring.size());
  const int master_idx = sbx::configFindField("master");
  TEST_ASSERT_TRUE(master_idx >= 0);
  const int cases = kMasterCases * fuzzScale();
  int accepted = 0;
  for (int n = 0; n < cases; ++n) {
    if (r.chance(2)) p.resetConfig();
    (void)p.takeConfigDirty();
    const GenForm g = genMasterForm(r);
    Ctx c;
    c.target = "applyMasterForm";
    c.seed = seed;
    c.index = n;
    c.form = &g;

    const CfgSnap s0 = snapConfig(p.config());
    const int master0 = p.master();
    ErrBuf e = makeErr(r);
    const ExactForm xf(g);
    const bool ok = p.applyMasterForm(g.pass_null ? nullptr : xf.pairs(), g.count, e.ptr, e.len);
    const CfgSnap s1 = snapConfig(p.config());
    const bool dirty = p.takeConfigDirty();

    const char* why = checkErr(e, ok, formUtf8(g));
    FUZZ_CHECK(c, why == nullptr, why);

    // Oraakkeli (processor.h:237-239): viimeinen ch-kenttä, arvo tasan "0".."2".
    int want = -1;
    if (!g.pass_null) {
      for (int i = 0; i < g.count && i < static_cast<int>(g.pairs.size()); ++i) {
        const GenPair& gp = g.pairs[static_cast<size_t>(i)];
        if (gp.key_null || cstr(gp.key) != "ch") continue;
        const std::string v = gp.value_null ? std::string("?") : cstr(gp.value);
        want = (!gp.value_null && v.size() == 1 && v[0] >= '0' && v[0] <= '2') ? v[0] - '0' : -1;
      }
    }
    FUZZ_CHECK(c, ok == (want >= 0), "hyväksyntä ei vastaa sääntöä: viimeinen ch tasan 0, 1 tai 2");
    if (ok) {
      ++accepted;
      FUZZ_CHECK(c, p.master() == want && p.config().status.master == want, "master ei vaihtunut pyydettyyn");
      FUZZ_CHECK(c, dirty == (want != master0), "tallennusmerkintä ei vastaa muutosta (processor.h:227)");
      FUZZ_CHECK(c, sameSnap(s0, s1, master_idx), "/api/master muutti muuta kuin masteria");
    } else {
      FUZZ_CHECK(c, sameSnap(s0, s1) && p.master() == master0, "hylätty /api/master muutti tilaa");
      FUZZ_CHECK(c, !dirty, "hylätty /api/master merkitsi tallennuksen");
    }
  }
  reportCounts("applyMasterForm", cases, accepted);
  requireBothPaths(cases, accepted);
}

// --- /api/sim ------------------------------------------------------------------------

float clampKey(int i, float v) {
  const sbx::SimKeyInfo& info = sbx::simKey(i);
  return v < info.min ? info.min : (v > info.max ? info.max : v);
}

void genSimValid(Rng& r, int i, GenPair& p) {
  const sbx::SimKeyInfo& info = sbx::simKey(i);
  bool too_long = false;
  if (info.integer) {
    const int k = -3 + static_cast<int>(r.below(10));
    p.value = decorate(r, intText(r, k), too_long);
    p.v = clampKey(i, static_cast<float>(k));
  } else {
    const float span = info.max - info.min;
    const float v = r.range(info.min - span * 0.5f, info.max + span * 0.5f);
    p.value = decorate(r, numText(v), too_long);
    p.v = clampKey(i, v);  // rajaus, ei hylkäys (sim_api.h:9)
  }
  p.expect = too_long ? Expect::Reject : Expect::Ok;
}

void genSimValue(Rng& r, int i, GenPair& p) {
  const sbx::SimKeyInfo& info = sbx::simKey(i);
  const uint32_t k = r.below(100);
  bool too_long = false;
  if (k < 60) {
    genSimValid(r, i, p);
  } else if (k < 65 && info.integer) {  // murtoluku (sim_api.h:196)
    p.value = padSpace(r, sformat(r.chance(50) ? "%d.5" : "%d,25", static_cast<int>(r.below(4))));
    p.expect = Expect::Reject;
  } else if (k < 80) {
    const Token& t = kTokens[r.below(kTokenCount)];
    p.value = tokenText(t);
    switch (t.cls) {
      case Tok::PosOverflow:
        p.expect = Expect::Ok;
        p.v = info.max;  // sim_api.h:141-143, :195
        break;
      case Tok::NegOverflow:
        p.expect = Expect::Ok;
        p.v = info.min;
        break;
      case Tok::Reject:
      case Tok::BoolTrue:
      case Tok::BoolFalse:
        p.expect = Expect::Reject;
        break;
      case Tok::Unknown:
        p.expect = Expect::Unknown;
        break;
    }
  } else if (k < 83) {
    p.value_null = true;  // liima ei anna nullptr:ia, core käsittelee ""-arvona
    p.expect = Expect::Reject;
  } else if (k < 86) {
    p.value = longNumber(r, too_long);
    p.expect = too_long ? Expect::Reject : Expect::Unknown;
  } else if (k < 93) {
    GenPair ok;
    genSimValid(r, i, ok);
    p.value = mutateText(r, ok.value);
    p.expect = Expect::Unknown;
  } else {
    p.value = randomBytes(r, 40);
    p.expect = Expect::Unknown;
  }
}

GenForm genSimForm(Rng& r, const KeySet& ks) {
  GenForm g;
  const bool clean = r.chance(45);
  const uint32_t n = clean ? 1 + r.below(4) : (r.chance(90) ? r.below(12) : r.below(71));
  for (uint32_t j = 0; j < n; ++j) {
    GenPair p;
    if (clean) {
      p.idx = static_cast<int>(r.below(static_cast<uint32_t>(ks.known.size())));
      p.key = ks.known[static_cast<size_t>(p.idx)];
      genSimValid(r, p.idx, p);
    } else {
      genKey(r, ks, p);
      if (p.idx >= 0) {
        genSimValue(r, p.idx, p);
      } else {
        p.value = r.chance(50) ? numText(r.range(0.0f, 100.0f)) : randomBytes(r, 12);
        p.expect = Expect::Reject;
      }
    }
    g.pairs.push_back(std::move(p));
  }
  g.count = static_cast<int>(g.pairs.size());
  const uint32_t k = r.below(100);
  if (k < 3) {
    g.pass_null = true;
  } else if (k < 5) {
    g.count = r.chance(50) ? 0 : -1;
  }
  return g;
}

// Kaikki SimParamsin kentät bitilleen (myös ne, joilla ei ole /api/sim-avainta).
bool sameSim(const SimParams& a, const SimParams& b) {
  bool same = a.engine_on == b.engine_on && sameBits(a.rpm, b.rpm) && sameBits(a.rpm_jitter, b.rpm_jitter) &&
              sameBits(a.rpm_drift, b.rpm_drift) && sameBits(a.base_kpa, b.base_kpa) &&
              sameBits(a.depth_kpa, b.depth_kpa) && sameBits(a.intake_frac, b.intake_frac) &&
              sameBits(a.res_kpa, b.res_kpa) && sameBits(a.res_decay_s, b.res_decay_s) &&
              sameBits(a.xtalk, b.xtalk) && sameBits(a.peak_kpa, b.peak_kpa) &&
              sameBits(a.noise_mv, b.noise_mv) && a.seed == b.seed;
  for (int c = 0; c < kChannels; ++c) {
    same = same && sameBits(a.sync_err_kpa[c], b.sync_err_kpa[c]) && sameBits(a.res_hz[c], b.res_hz[c]) &&
           sameBits(a.sensor_offset_kpa[c], b.sensor_offset_kpa[c]) &&
           sameBits(a.sensor_gain_err[c], b.sensor_gain_err[c]) && a.fault[c] == b.fault[c];
  }
  return same;
}

// Kentät, joilla ei ole /api/sim-avainta, eivät muutu koskaan.
bool sameNonKeyFields(const SimParams& a, const SimParams& b) {
  bool same = sameBits(a.intake_frac, b.intake_frac) && sameBits(a.res_decay_s, b.res_decay_s) && a.seed == b.seed;
  for (int c = 0; c < kChannels; ++c) {
    same = same && sameBits(a.res_hz[c], b.res_hz[c]) && sameBits(a.sensor_offset_kpa[c], b.sensor_offset_kpa[c]) &&
           sameBits(a.sensor_gain_err[c], b.sensor_gain_err[c]);
  }
  return same;
}

void test_sim_form_fuzz() {
  const KeySet ks = simKeys();
  const uint64_t seed = fuzzSeed(kSeedSim);
  Rng r(seed);
  SimParams p;
  const int cases = kSimCases * fuzzScale();
  int accepted = 0;
  std::unique_ptr<char[]> json(new char[sbx::kSimJsonSize]);
  for (int n = 0; n < cases; ++n) {
    if (n % 1000 == 999) p = SimParams();
    const GenForm g = genSimForm(r, ks);
    Ctx c;
    c.target = "simApplyForm";
    c.seed = seed;
    c.index = n;
    c.form = &g;

    const SimParams before = p;
    const bool rpm_set0 = r.chance(50);
    bool rpm_set = rpm_set0;
    ErrBuf e = makeErr(r);
    const ExactForm xf(g);
    const bool ok = sbx::simApplyForm(p, g.pass_null ? nullptr : xf.pairs(), g.count, rpm_set, e.ptr, e.len);

    const char* why = checkErr(e, ok, formUtf8(g));
    FUZZ_CHECK(c, why == nullptr, why);

    const bool no_pairs = g.pass_null || g.count <= 0;
    bool any_reject = false;
    bool all_ok = true;
    bool has_rpm = false;
    bool mentioned[sbx::kSimKeyCount] = {};
    float want[sbx::kSimKeyCount] = {};
    if (!no_pairs) {
      for (const GenPair& gp : g.pairs) {
        if (gp.key_null || gp.idx < 0 || gp.expect == Expect::Reject) any_reject = true;
        if (gp.key_null || gp.idx < 0 || gp.expect != Expect::Ok) all_ok = false;
        if (gp.idx >= 0) {
          mentioned[gp.idx] = true;
          want[gp.idx] = gp.v;  // viimeinen esiintymä ratkaisee
          if (sbx::simKey(gp.idx).key == std::string("rpm")) has_rpm = true;
        }
      }
    }
    if (no_pairs) FUZZ_CHECK(c, ok, "tyhjä lomake hylättiin (sim_api.h:205)");
    if (any_reject) FUZZ_CHECK(c, !ok, "hylättävä pari, mutta lomake hyväksyttiin (sim_api.h:7-8)");
    if (all_ok) FUZZ_CHECK(c, ok, "kelvollinen lomake hylättiin (sim_api.h:9, :140-143)");

    if (!ok) {
      FUZZ_CHECK(c, sameSim(before, p), "hylätty lomake muutti parametreja (sim_api.h:201-203)");
      FUZZ_CHECK(c, rpm_set == rpm_set0, "hylätty lomake muutti rpm_setiä (sim_api.h:201-203)");
      continue;
    }
    ++accepted;
    FUZZ_CHECK(c, rpm_set == has_rpm, "rpm_set ei kerro rpm-avaimesta (sim_api.h:201-202)");
    FUZZ_CHECK(c, sameNonKeyFields(before, p), "avaimeton SimParams-kenttä muuttui");
    for (int i = 0; i < sbx::kSimKeyCount; ++i) {
      const sbx::SimKeyInfo& info = sbx::simKey(i);
      const float v = sbx::simGet(p, i);
      FUZZ_CHECK(c, std::isfinite(v) && v >= info.min && v <= info.max,
                 "hyväksytty arvo alueen ulkopuolella (sim_api.h:9)");
      FUZZ_CHECK(c, !info.integer || std::floor(v) == v, "kokonaislukuavain ei ole kokonaisluku (sim_api.h:10)");
      if (!mentioned[i]) {
        FUZZ_CHECK(c, sameBits(v, sbx::simGet(before, i)), "lomakkeessa mainitsematon avain muuttui (sim_api.h:11-12)");
      } else if (all_ok) {
        FUZZ_CHECK(c, v == want[i], "kelvollinen arvo luettiin tai rajattiin väärin (sim_api.h:9, :140-143)");
      }
    }
    // GET /api/sim mahtuu (sim_api.h:250-252) ja EngineSim ajaa parametreilla.
    const size_t jl = sbx::simToJson(p, json.get(), sbx::kSimJsonSize);
    FUZZ_CHECK(c, jl > 0, "simToJson ei mahdu kSimJsonSize:en (sim_api.h:250-252)");
    FUZZ_CHECK(c, jsoncheck::valid(json.get()), "simToJson ei ole kelvollista JSONia");
    if (accepted % 8 == 0) {
      EngineSim sim;
      sim.configure(p, sbx::SensorParams(), rpm_set);
      for (int k = 0; k < 48; ++k) (void)sim.next();
    }
  }
  reportCounts("simApplyForm", cases, accepted);
  requireBothPaths(cases, accepted);
}

// --- nauhoitus-CSV ---------------------------------------------------------------

std::vector<std::string> splitLines(const std::string& s, bool& final_nl) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos < s.size()) {
    const size_t eol = s.find('\n', pos);
    if (eol == std::string::npos) {
      out.push_back(s.substr(pos));
      final_nl = false;
      return out;
    }
    out.push_back(s.substr(pos, eol - pos));
    pos = eol + 1;
  }
  final_nl = true;
  return out;
}

std::string joinLines(const std::vector<std::string>& lines, bool final_nl) {
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    out += lines[i];
    if (i + 1 < lines.size() || final_nl) out += '\n';
  }
  return out;
}

bool isDataLine(const std::string& l) { return !l.empty() && l[0] >= '0' && l[0] <= '9'; }

// Pohjat: laitteen /api/raw.csv (CsvWriter, recording.h simToCsv) kahdella
// kalibroinnilla, serial_record.py-muoto (CRLF, %g-luvut, recorded=,
// stream_end=), raaka sarjavirta aukkoineen ja keskeytynyt lataus.
std::vector<std::string> csvCorpus() {
  std::vector<std::string> out;
  std::string csv;
  std::string err;
  SimParams sp;
  Config cfg;
  Calibration cal;
  cal.zero_valid = true;
  TEST_ASSERT_TRUE(rec::simToCsv(sp, cfg, cal, 100, csv, err));
  out.push_back(csv);

  sp.fault[1] = SimFault::SensorHigh;
  sp.rpm = 2400.0f;
  cal.offset_kpa[0] = 0.5f;
  cal.offset_kpa[1] = -0.25f;
  cal.gain[0] = 1.05f;
  cal.gain[1] = 0.95f;
  cal.gain_valid = true;
  TEST_ASSERT_TRUE(rec::simToCsv(sp, cfg, cal, 250, csv, err));
  out.push_back(csv);
  // Keskeytynyt lataus: CsvWriter lopettaa "# aborted="-riviin (raw_ring.h:249).
  bool nl = true;
  std::vector<std::string> lines = splitLines(csv, nl);
  lines.resize(lines.size() / 2);
  lines.push_back("# aborted=ring_changed rows=110/250");
  out.push_back(joinLines(lines, true));

  // serial_record.py (tools/serial_record.py:115-138), Windowsilla CRLF.
  EngineSim sim;
  sim.configure(SimParams(), sbx::SensorParams());
  std::string s =
      "# Suckboxx raakadata (sarjavirta, tools/serial_record.py)\r\n# device=suckboxx\r\n# format=1\r\n"
      "# source=?\r\n# fs_hz=1000\r\n# v_zero_mv=2700\r\n# mv_per_kpa=25\r\n# zero_valid=0\r\n"
      "# gain_valid=0\r\n# offset_kpa=0,0,0\r\n# gain=1,1,1\r\n# samples=80\r\n# gaps=0\r\n"
      "# recorded=2026-10-31T12:00:00\r\n# stream_end=end samples=80\r\n"
      "# kpa=gain*(mv-v_zero_mv)/mv_per_kpa+offset (ei kiskokorjausta)\r\n"
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa\r\n";
  for (int i = 0; i < 80; ++i) {
    const RawFrame f = sim.next();
    s += sformat("%lu,%lu,%u,%u,%u,%.3f,%.3f,%.3f\r\n", static_cast<unsigned long>(f.seq),
                 static_cast<unsigned long>(f.seq), static_cast<unsigned>(f.mv[0]), static_cast<unsigned>(f.mv[1]),
                 static_cast<unsigned>(f.mv[2]), static_cast<double>((f.mv[0] - 2700.0f) / 25.0f),
                 static_cast<double>((f.mv[1] - 2700.0f) / 25.0f), static_cast<double>((f.mv[2] - 2700.0f) / 25.0f));
  }
  out.push_back(s);

  // Laitteen raaka sarjavirta: vain pakolliset sarakkeet, aukkoja ja seq:n
  // kierto 2^32:n yli (modulo kuten RawRing, recording.h:281).
  s = "seq,ch1_mv,ch2_mv,ch3_mv\n";
  uint32_t seq = 0xFFFFFF80u;
  for (int i = 0; i < 120; ++i) {
    const RawFrame f = sim.next();
    seq += (i % 37 == 36) ? 5u : 1u;
    s += sformat("%lu,%u,%u,%u\n", static_cast<unsigned long>(seq), static_cast<unsigned>(f.mv[0]),
                 static_cast<unsigned>(f.mv[1]), static_cast<unsigned>(f.mv[2]));
  }
  out.push_back(s);
  return out;
}

struct HeaderMut {
  const char* key;
  std::vector<const char*> values;
};

const std::vector<HeaderMut>& headerMuts() {
  static const std::vector<HeaderMut> kMuts = {
      {"fs_hz", {"1000", "1000.0", "1e3", " 1000 ", "999.9", "nan", "inf", "", "0", "-1000", "1000x", "0x3e8"}},
      {"format", {"1", " 1 ", "1.0", "2", "", "x", "\xC3"}},
      {"samples", {"150", "0", "99999999999", "abc", "-1", "", "4294967295", "4294967296"}},
      {"offset_kpa",
       {"0,0,0", "nan,0,0", "inf,-inf,0", "1e39,0,0", "0,0", "0,0,0,0", ",,", "1,2,", "0.5, -0.25 ,0", "0x1p3,0,0",
        "1e-45,0,0", "-0,0,0", "3.4e38,-3.4e38,0"}},
      {"gain",
       {"1,1,1", "0,0,0", "-1,1,1", "1.1,0.9,1", "1.2,1,1", "inf,1,1", "nan,nan,nan", "1,1", "0.85,1.15,1",
        "0.8499999,1,1", "1e-45,1,1"}},
      {"zero_valid", {"0", "1", "2", "", "true", " 1 ", "1\xC3"}},
      {"gain_valid", {"0", "1", "2", "", "true", " 1 "}},
      {"v_zero_mv", {"2700", "1000", "3300", "999", "1e99", "0", "nan", "2700,5", "1,5e3", "-0", "\xC3"}},
      {"mv_per_kpa", {"25", "5", "50", "0", "-25", "nan", "4.99", "12,5"}},
      {"rail_comp", {"0", "1", "on", "true", "2", ""}},
      {"rail_nominal_mv", {"3300", "2500", "5500", "5501", "nan", "5000"}},
      {"source", {"sim", "adc", "hx710b", "?", "", "SIM", "\xC3\xA4"}},
      {"aborted", {"ring_changed rows=10/150", ""}},
  };
  return kMuts;
}

const char* const kNastyFields[] = {
    "",           "-1",         "+1",          "65535",       "65536",     "4294967295", "4294967296",
    "18446744073709551616",    "1.5",         "1e3",         " 7 ",       "\t7",        "nan",
    "inf",        "0x10",       "\xC3",        "\xE2\x82",    "\xC3\xA4",  "\x80",       "7\r",
    "00000000000000000000000000000000000000001",                          "#",          "=",
};
constexpr uint32_t kNastyFieldCount = sizeof(kNastyFields) / sizeof(kNastyFields[0]);

bool parseU32(const std::string& s, uint32_t& out) {
  if (s.empty() || s.size() > 10) return false;
  uint64_t v = 0;
  for (char ch : s) {
    if (ch < '0' || ch > '9') return false;
    v = v * 10u + static_cast<uint64_t>(ch - '0');
  }
  if (v > 0xFFFFFFFFull) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

// Rivin kenttä i (pilkuilla erotettu) korvataan.
void replaceField(std::string& line, uint32_t i, const std::string& with) {
  size_t b = 0;
  for (uint32_t k = 0; k < i; ++k) {
    const size_t comma = line.find(',', b);
    if (comma == std::string::npos) return;
    b = comma + 1;
  }
  size_t e = line.find(',', b);
  if (e == std::string::npos) e = line.size();
  line.replace(b, e - b, with);
}

// Rivitason mutaatio (lajit 4-15).
void mutateLines(Rng& r, uint32_t kind, std::vector<std::string>& lines, bool& final_nl) {
  if (lines.empty()) {
    lines.push_back("seq,ch1_mv,ch2_mv,ch3_mv");
    final_nl = true;
  }
  const uint32_t nl = static_cast<uint32_t>(lines.size());
  const uint32_t at = r.below(nl);
  std::string& line = lines[at];
  const uint32_t nf = 1 + static_cast<uint32_t>(std::count(line.begin(), line.end(), ','));
  switch (kind) {
    case 4:  // rivin poisto
      lines.erase(lines.begin() + at);
      break;
    case 5: {  // rivin toisto
      const std::string dup = line;
      lines.insert(lines.begin() + at, 1 + r.below(3), dup);
      break;
    }
    case 6:  // kahden rivin vaihto
      std::swap(line, lines[r.below(nl)]);
      break;
    case 7:  // kenttä korvataan ilkeällä
      replaceField(line, r.below(nf), kNastyFields[r.below(kNastyFieldCount)]);
      break;
    case 8:  // ylipitkä numerokenttä
      replaceField(line, r.below(nf), std::string(20 + r.below(400), static_cast<char>('0' + r.below(10))));
      break;
    case 9: {  // seq-hyppy edellisestä datarivistä
      uint32_t prev = 0;
      bool have_prev = false;
      for (uint32_t i = 0; i < nl; ++i) {
        if (!isDataLine(lines[i])) continue;
        uint32_t v = 0;
        const std::string first = lines[i].substr(0, lines[i].find(','));
        if (have_prev && i >= at && parseU32(first, v)) {
          static const uint32_t kDeltas[] = {0u, 0xFFFFFFFFu, 2u, 1000u, 65535u, 65536u, 0x7FFFFFFFu, 0x80000000u};
          replaceField(lines[i], 0, std::to_string(prev + kDeltas[r.below(8)]));
          break;
        }
        if (parseU32(first, v)) {
          prev = v;
          have_prev = true;
        }
      }
      break;
    }
    case 10: {  // otsakkeen arvo (ensimmäinen esiintymä ratkaisee, recording.h:92)
      const HeaderMut& h = headerMuts()[r.below(static_cast<uint32_t>(headerMuts().size()))];
      const std::string prefix = std::string("# ") + h.key + "=";
      const std::string text = prefix + h.values[r.below(static_cast<uint32_t>(h.values.size()))];
      bool replaced = false;
      for (std::string& l : lines) {
        if (l.compare(0, prefix.size(), prefix) == 0) {
          l = text;
          replaced = true;
          break;
        }
      }
      if (!replaced) lines.insert(lines.begin() + r.below(nl + 1), text);
      break;
    }
    case 11: {  // sarakeotsake: järjestys, puuttuva, tuplattu, kirjainkoko, tyhjää
      for (std::string& l : lines) {
        if (l.empty() || l[0] == '#') continue;
        bool dummy = true;
        std::string tmp = l;
        std::replace(tmp.begin(), tmp.end(), ',', '\n');
        std::vector<std::string> cols = splitLines(tmp, dummy);
        if (cols.empty()) break;
        const uint32_t c0 = r.below(static_cast<uint32_t>(cols.size()));
        switch (r.below(5)) {
          case 0: std::swap(cols[c0], cols[r.below(static_cast<uint32_t>(cols.size()))]); break;
          case 1: cols.erase(cols.begin() + c0); break;
          case 2: cols.push_back(cols[c0]); break;
          case 3: if (!cols[c0].empty()) cols[c0][0] = static_cast<char>(cols[c0][0] & ~0x20); break;
          default: cols[c0] = " " + cols[c0] + "\t"; break;
        }
        l.clear();
        for (size_t i = 0; i < cols.size(); ++i) l += (i ? "," : "") + cols[i];
        break;
      }
      break;
    }
    case 12:  // keskeytys kesken
      lines.insert(lines.begin() + at, sformat("# aborted=ring_changed rows=%lu/%lu", static_cast<unsigned long>(at),
                                               static_cast<unsigned long>(nl)));
      break;
    case 13:  // viimeinen rivinvaihto pois (katkennut lataus)
      final_nl = false;
      break;
    case 14:  // kenttiä lisää tai vähemmän
      if (r.chance(50)) {
        line += ",1";
      } else {
        const size_t comma = line.rfind(',');
        if (comma != std::string::npos) line.resize(comma);
      }
      break;
    default:  // pelkkä '\r' tai tyhjä rivi väliin
      lines.insert(lines.begin() + at, r.chance(50) ? std::string("\r") : std::string());
      break;
  }
}

// Tavutason mutaatio (lajit 0-3).
void mutateBytes(Rng& r, uint32_t kind, std::string& s) {
  if (kind == 0) {  // katkaisu
    s.resize(r.below(static_cast<uint32_t>(s.size()) + 1));
  } else if (kind == 1) {  // tavun vaihto
    if (!s.empty()) s[r.below(static_cast<uint32_t>(s.size()))] = r.byte();
  } else if (kind == 2) {  // satunnaisia tavuja väliin
    static const char kBias[] = {'\0', '\r', '\n', ',', '#', '=', '-', '0', '9', '\x80', '\xC3', '\xFF', ' '};
    std::string ins;
    for (uint32_t i = 0, n = 1 + r.below(16); i < n; ++i) {
      ins += r.chance(60) ? kBias[r.below(sizeof(kBias))] : r.byte();
    }
    s.insert(r.below(static_cast<uint32_t>(s.size()) + 1), ins);
  } else {  // CRLF <-> LF
    std::string t;
    t.reserve(s.size() + s.size() / 16);
    const bool has_cr = s.find('\r') != std::string::npos;
    for (char ch : s) {
      if (has_cr && ch == '\r') continue;
      if (!has_cr && ch == '\n') t += '\r';
      t += ch;
    }
    s.swap(t);
  }
}

// 1-4 mutaatiota: riviltä tehtävät yhdellä jaolla ja kokoamisella, tavutason
// sen jälkeen (ajoaika: pohjat ovat satoja rivejä).
void mutateCsv(Rng& r, std::string& s) {
  const uint32_t ops = 1 + r.below(4);
  uint32_t kinds[4] = {0, 0, 0, 0};
  bool any_line_op = false;
  for (uint32_t op = 0; op < ops; ++op) {
    kinds[op] = r.below(16);
    if (kinds[op] >= 4) any_line_op = true;
  }
  if (any_line_op) {
    bool final_nl = true;
    std::vector<std::string> lines = splitLines(s, final_nl);
    for (uint32_t op = 0; op < ops; ++op) {
      if (kinds[op] >= 4) mutateLines(r, kinds[op], lines, final_nl);
    }
    s = joinLines(lines, final_nl);
  }
  for (uint32_t op = 0; op < ops; ++op) {
    if (kinds[op] < 4) mutateBytes(r, kinds[op], s);
  }
}

// Satunnainen mutta rakenteellinen: pakolliset sarakkeet satunnaisessa
// järjestyksessä ja satunnaisia datarivejä, jotta myös hyväksyvä polku
// saa satunnaisia arvoja.
std::string structuredRandomCsv(Rng& r) {
  std::vector<std::string> cols = {"seq", "ch1_mv", "ch2_mv", "ch3_mv"};
  for (uint32_t i = 0, n = r.below(3); i < n; ++i) cols.push_back(sformat("x%u", static_cast<unsigned>(i)));
  for (size_t i = cols.size(); i > 1; --i) std::swap(cols[i - 1], cols[r.below(static_cast<uint32_t>(i))]);
  std::string s;
  if (r.chance(50)) s += "# fs_hz=1000\n";
  for (size_t i = 0; i < cols.size(); ++i) s += (i ? "," : "") + cols[i];
  s += '\n';
  uint32_t seq = r.chance(10) ? 0xFFFFFFF0u : r.below(100000);
  for (uint32_t row = 0, n = r.below(60); row < n; ++row) {
    seq += r.chance(90) ? 1u : 1u + r.below(70000);
    for (size_t i = 0; i < cols.size(); ++i) {
      if (i) s += ',';
      if (cols[i] == "seq") {
        s += std::to_string(seq);
      } else if (r.chance(97)) {
        s += std::to_string(r.chance(80) ? r.below(3301) : r.below(65536));
      } else {
        s += kNastyFields[r.below(kNastyFieldCount)];
      }
    }
    s += '\n';
  }
  return s;
}

void test_csv_reader_fuzz() {
  const uint64_t seed = fuzzSeed(kSeedCsv);
  Rng r(seed);
  const std::vector<std::string> corpus = csvCorpus();
  // Pohjat kelpaavat sellaisenaan (recording.h:8-17: kaikki kolme muotoa).
  for (const std::string& base : corpus) {
    rec::Recording rr;
    std::string err;
    const bool ok = rec::parseCsv(base.data(), base.size(), rr, err);
    TEST_ASSERT_TRUE_MESSAGE(ok, err.c_str());
  }
  const int cases = kCsvCases * fuzzScale();
  int accepted = 0;
  int replayed = 0;
  for (int n = 0; n < cases; ++n) {
    std::string input;
    const uint32_t k = r.below(100);
    if (k < 6) {
      for (uint32_t i = 0, len = r.below(600); i < len; ++i) input += r.byte();
    } else if (k < 14) {
      input = structuredRandomCsv(r);
    } else {
      input = corpus[r.below(static_cast<uint32_t>(corpus.size()))];
      mutateCsv(r, input);
    }
    Ctx c;
    c.target = "parseCsv";
    c.seed = seed;
    c.index = n;
    c.csv = &input;

    // Tarkka varaus ilman NUL-päätettä: lukeminen len:n yli on ASan-löydös.
    std::unique_ptr<char[]> buf(new char[input.size()]);
    if (!input.empty()) std::memcpy(buf.get(), input.data(), input.size());
    rec::Recording rr;
    std::string err;
    const bool ok = rec::parseCsv(buf.get(), input.size(), rr, err);
    FUZZ_CHECK(c, ok == err.empty(), "parseCsv: false <=> err ei tyhjä (recording.h:184-188)");
    if (!ok) continue;
    ++accepted;

    FUZZ_CHECK(c, !rr.frames.empty(), "hyväksytyssä nauhoituksessa ei näytteitä (recording.h:306-308)");
    uint32_t gaps = 0;
    uint32_t missing = 0;
    for (size_t i = 0; i < rr.frames.size(); ++i) {
      FUZZ_CHECK(c, rr.frames[i].rail_mv == 0, "rail_mv ei ole 0 (recording.h:85)");
      if (i == 0) continue;
      const uint32_t d = rr.frames[i].seq - rr.frames[i - 1].seq;
      FUZZ_CHECK(c, d >= 1 && d <= sbx::RawRing::kSeqWindow,
                 "seq ei kasva 1…65535 per rivi (recording.h:184-188, firmware.md §6.1)");
      if (d != 1) {
        ++gaps;
        missing += d - 1u;
      }
    }
    FUZZ_CHECK(c, rr.gaps == gaps && rr.missing == missing, "gaps/missing ei vastaa hyppyjä (recording.h:88-89)");

    Config hdr;
    std::string err_cfg;
    const bool hc = rec::headerConfig(rr, hdr, err_cfg);
    FUZZ_CHECK(c, hc == err_cfg.empty(), "headerConfig: false <=> err ei tyhjä");
    if (hc) {
      for (int i = 0; i < sbx::kConfigFieldCount; ++i) {
        const FieldInfo& f = sbx::configField(i);
        const float v = sbx::configGet(hdr, i);
        FUZZ_CHECK(c, v >= f.min && v <= f.max, "otsakkeen arvo alueen ulkopuolella (recording.h:357-359)");
      }
    }
    Calibration cal;
    std::string err_cal;
    const bool hk = rec::headerCalibration(rr, cal, err_cal);
    FUZZ_CHECK(c, hk == err_cal.empty(), "headerCalibration: false <=> err ei tyhjä");

    // Toisto vain lyhyille (ajoaika): 2 s = ~38 tickiä.
    if (rr.durationS() > 2.0f) continue;
    rec::ReplayOptions o;
    rec::ReplayResult res;
    std::string err_rep;
    const bool rp = rec::replayRecording(rr, o, res, err_rep);
    FUZZ_CHECK(c, rp == err_rep.empty(), "replayRecording: false <=> err ei tyhjä");
    FUZZ_CHECK(c, rp == (hc && sbx::configValidate(hdr, nullptr, 0) && hk),
               "toisto onnistuu tasan kun otsake kelpaa (recording.h:462-492)");
    if (!rp) continue;
    ++replayed;
    const Calibration& cs = res.cal_start;
    for (int ch = 0; ch < kChannels; ++ch) {
      FUZZ_CHECK(c, std::isfinite(cs.offset_kpa[ch]), "toiston offset ei äärellinen (processor.h:107-112)");
      FUZZ_CHECK(c, cs.gain_valid ? (cs.gain[ch] >= sbx::kGainMin && cs.gain[ch] <= sbx::kGainMax) : cs.gain[ch] == 1.0f,
                 "toiston gain ei siivottu (processor.h:107-112)");
    }
    for (size_t i = 1; i < res.ticks.size(); ++i) {
      FUZZ_CHECK(c, res.ticks[i].t > res.ticks[i - 1].t, "tickien aika ei kasva (recording.h:28-33)");
    }
  }
  const std::string s = sformat("parseCsv: %d tapausta, hyväksytty %d, toistettu %d", cases, accepted, replayed);
  TEST_MESSAGE(s.c_str());
  requireBothPaths(cases, accepted);
  TEST_ASSERT_GREATER_OR_EQUAL(cases / 100, replayed);  // ehdotus: toistopolkukin ajetaan
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_config_form_fuzz);
  RUN_TEST(test_master_form_fuzz);
  RUN_TEST(test_sim_form_fuzz);
  RUN_TEST(test_csv_reader_fuzz);
  return UNITY_END();
}
