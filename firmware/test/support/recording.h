#pragma once
// Suckboxx - raakanauhoituksen luku ja toisto Processorin läpi (WP-8,
// docs/firmware.md §6.1, SUUNNITTELU.md §7 vaihe 4).
//
// Vain PC:lle: native-testit (test_replay) ja tools/replay. Ei ESP32:lle, joten
// std::string, std::vector ja <cstdio> ovat sallittuja (core ei käytä niitä, §1).
//
// Kaksi CSV-muotoa, sama sisältö (sarakkeet tunnistetaan nimestä):
//   - laitteen GET /api/raw.csv (raw_ring.h CsvWriter): otsakkeessa myös
//     first_seq, rail_comp ja rail_nominal_mv; t_ms = seq - first_seq; jos
//     rengas muuttui kesken latauksen, viimeinen rivi on
//     "# aborted=ring_changed rows=K/N" ja sitä seuraava data puuttuu.
//   - tools/serial_record.py: seq alkaa nollasta, t_ms = seq, otsakkeen luvut
//     %g-muodossa, lisäksi recorded= ja stream_end=. Windowsilla rivinvaihdot
//     ovat CRLF (Path.write_text), joten '\r' riviltä ohitetaan.
// Pakolliset sarakkeet ovat seq, ch1_mv, ch2_mv ja ch3_mv, joten myös laitteen
// raaka sarjavirta (seq,ch1_mv,ch2_mv,ch3_mv) kelpaa. Toisto käyttää vain
// mV-sarakkeita: kPa-sarakkeet ovat laitteen laskemia lataushetken
// kalibroinnilla eivätkä sisällä kiskokorjausta. Kiskojännitettä ei tallenneta,
// joten kehysten rail_mv = 0 ("ei mitattu"), kuten protolevyn ADC-lähteellä ja
// simulaattorilla (sampler.cpp kAdcHasRail = false, engine_sim.h).
//
// Otsakkeesta konfiguraatioon ja kalibrointiin (headerConfig, headerCalibration):
// v_zero_mv, mv_per_kpa, rail_comp, rail_nominal_mv, source, offset_kpa, gain,
// zero_valid ja gain_valid. Otsake kuvaa lataushetken (CsvWriterin konstruktori)
// tai sarjavirran alun tilan: jos nollaus tehtiin kesken nauhoitetun ikkunan,
// alkuosa toistuu jo uudella offsetilla. Offset tallentuu 4 ja gain 6
// desimaalilla, joten toiston kPa voi poiketa laitteen luvusta < 1e-4 kPa.
//
// Ajoitus kuten laitteella (src/hal/proc_task.cpp, src/hal/hal_config.h):
//   - sbx_proc herää kProcPollMs = 4 ms välein (vTaskDelay), vie jonossa olevat
//     kehykset Processor::pushiin ja kutsuu tick(now) samassa herätyksessä, jos
//     edellisestä tickistä on kulunut vähintään kTickMs = 50 ms. last_tick =
//     now (ei += 50), joten tikit tulevat 52 ms:n välein (19,2 Hz).
//   - seq laskee 1 ms:n jaksoja myös pudotusten yli: sampler hyppää ohitettujen
//     jaksojen yli, ja täysi jono pudottaa kehyksen, joka näkyy aukkona
//     (docs/firmware.md §4.9). seq on siis laitteen kello: herätyshetkellä T
//     (ms nauhoituksen alusta) on viety kaikki kehykset, joilla seq - seq0 < T.
//   - Aukko ei syötä mitään: DSP laskee aikaa näytteinä eikä käytä seq:iä
//     (dsp.h), mutta tickin kello etenee aukon yli. Processorin rengas laskee
//     aukot (diag.gaps) samoin kuin laitteella.
//   - Toisto alkaa kylmästä Processorista nauhoituksen ensimmäisestä
//     näytteestä, ja now_s = T / 1000 nauhoituksen alusta (laitteella
//     käynnistyksestä; StatusMachine käyttää vain erotuksia).
// Ero laitteeseen: pitkän pysähdyksen (flash) aikana laitteen käsittelytaski
// seisoo myös ja tikkaa kerran perään; toisto tikkaa aukon läpi 52 ms:n välein.
// TickTiming{1, 50} on test_integrationin Bench-ajoitus (tick joka 50. näyte).

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
#include "core/processor.h"
#include "core/raw_ring.h"
#include "core/status.h"
#include "core/types.h"

namespace sbx {
namespace rec {

// Laitteen renkaan koko (proc_task.cpp kRingCapacity). Ei vaikuta käsittelyyn.
constexpr size_t kRingCapacity = 8192;
// CsvWriter kirjoittaa renkaasta, jonka vanhin ja uusin ovat 16-bittisen
// seq-ikkunan sisällä (raw_ring.h), joten yksi kirjoitus on enintään 65536 näytettä.
constexpr uint32_t kMaxCsvSamples = RawRing::kSeqWindow + 1u;

struct MetaEntry {
  std::string key;
  std::string value;
};

struct Recording {
  std::vector<MetaEntry> meta;     // otsakkeen avain=arvo-rivit järjestyksessä
  std::vector<RawFrame> frames;    // rail_mv = 0 (ei tallennettu)
  bool aborted = false;            // "# aborted=": rengas muuttui kesken latauksen
  bool truncated = false;          // viimeinen rivi ilman rivinvaihtoa (katkennut lataus)
  uint32_t gaps = 0;               // seq-epäjatkuvuudet (hyppy = yksi aukko, kuten RawRing::gaps)
  uint32_t missing = 0;            // puuttuvat näytteet yhteensä
  std::vector<std::string> warnings;

  // Otsakkeen arvo tai nullptr. Ensimmäinen esiintymä ratkaisee.
  const char* find(const char* key) const {
    for (const MetaEntry& m : meta) {
      if (m.key == key) return m.value.c_str();
    }
    return nullptr;
  }
  // Kesto seq:n mukaan aukot mukaan lukien, sekunteina.
  float durationS() const {
    if (frames.empty()) return 0.0f;
    return static_cast<float>(frames.back().seq - frames.front().seq + 1u) /
           static_cast<float>(kSampleRateHz);
  }
};

namespace detail {

inline std::string trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

inline std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline std::string format(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return std::string();
  return std::string(buf, static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1);
}

// Ei-negatiivinen kokonaisluku: vain numeroita (ei etumerkkiä, ei tyhjää), <= max.
inline bool parseUint(const char* b, const char* e, uint64_t max, uint64_t& out) {
  while (b < e && (*b == ' ' || *b == '\t')) ++b;
  while (e > b && (e[-1] == ' ' || e[-1] == '\t')) --e;
  if (b == e) return false;
  uint64_t v = 0;
  for (const char* p = b; p < e; ++p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10u + static_cast<uint64_t>(*p - '0');
    if (v > max) return false;
  }
  out = v;
  return true;
}

// Liukuluku C-muodossa (piste). Koko tekstin on oltava luku. Työkalu ja testit
// eivät kutsu setlocalea, joten strtof lukee pisteen.
inline bool parseFloat(const std::string& s, float& out) {
  const std::string t = trim(s);
  if (t.empty()) return false;
  char* end = nullptr;
  const float v = std::strtof(t.c_str(), &end);
  if (end == t.c_str() || *end != '\0') return false;
  out = v;
  return true;
}

// "a,b,c" -> kolme lukua (offset_kpa, gain).
inline bool parseTriple(const std::string& s, float (&out)[kChannels]) {
  size_t pos = 0;
  for (int c = 0; c < kChannels; ++c) {
    const size_t comma = s.find(',', pos);
    const bool last = c == kChannels - 1;
    if (last != (comma == std::string::npos)) return false;
    const std::string part = s.substr(pos, last ? std::string::npos : comma - pos);
    if (!parseFloat(part, out[c])) return false;
    pos = comma + 1;
  }
  return true;
}

inline bool parseBit(const char* text, bool& out) {
  const std::string t = trim(text != nullptr ? text : "");
  if (t == "0") {
    out = false;
    return true;
  }
  if (t == "1") {
    out = true;
    return true;
  }
  return false;
}

}  // namespace detail

// CSV muistista. false + err, jos tiedosto ei ole käyttökelpoinen: sarake
// puuttuu, datarivi on rikki (muualla kuin viimeisenä), seq ei kasva, seq hyppää
// yli 16-bittisen ikkunan, fs_hz ei ole 1000 tai format ei ole 1. Katkennut
// viimeinen rivi (ei rivinvaihtoa) ohitetaan varoituksella; "# aborted=" lopettaa
// lukemisen (CsvWriter ei kirjoita sen jälkeen dataa).
inline bool parseCsv(const char* text, size_t len, Recording& out, std::string& err) {
  using detail::format;
  out = Recording();
  err.clear();
  static const char* const kCols[] = {"seq", "ch1_mv", "ch2_mv", "ch3_mv"};
  constexpr int kNeed = 1 + kChannels;
  int col[kNeed] = {-1, -1, -1, -1};
  size_t ncols = 0;
  bool have_columns = false;
  bool have_prev = false;
  uint32_t prev = 0;
  std::vector<std::pair<const char*, const char*>> f;
  size_t pos = 0;
  int line_no = 0;
  while (text != nullptr && pos < len) {
    size_t eol = pos;
    while (eol < len && text[eol] != '\n') ++eol;
    const bool has_newline = eol < len;
    const char* b = text + pos;
    const char* e = text + eol;
    pos = has_newline ? eol + 1 : eol;
    ++line_no;
    if (e > b && e[-1] == '\r') --e;
    if (b == e) continue;

    if (*b == '#') {
      const std::string body = detail::trim(std::string(b + 1, e));
      const size_t eq = body.find('=');
      if (eq == std::string::npos) continue;  // vapaa kommentti
      MetaEntry m;
      m.key = detail::trim(body.substr(0, eq));
      m.value = detail::trim(body.substr(eq + 1));
      if (m.key == "aborted") {
        out.aborted = true;
        out.warnings.push_back("lataus keskeytyi: " + m.value + " (vain ehjä alkuosa toistetaan)");
        out.meta.push_back(m);
        break;
      }
      if (!have_columns) out.meta.push_back(m);
      continue;
    }

    f.clear();
    const char* s = b;
    for (const char* p = b;; ++p) {
      if (p == e || *p == ',') {
        f.emplace_back(s, p);
        if (p == e) break;
        s = p + 1;
      }
    }

    if (!have_columns) {
      for (size_t i = 0; i < f.size(); ++i) {
        const std::string name = detail::trim(std::string(f[i].first, f[i].second));
        for (int c = 0; c < kNeed; ++c) {
          if (name == kCols[c] && col[c] < 0) col[c] = static_cast<int>(i);
        }
      }
      for (int c = 0; c < kNeed; ++c) {
        if (col[c] < 0) {
          err = format("rivi %d: sarake puuttuu: %s", line_no, kCols[c]);
          return false;
        }
      }
      ncols = f.size();
      have_columns = true;
      continue;
    }

    uint64_t v[kNeed] = {0, 0, 0, 0};
    bool ok = f.size() == ncols;
    for (int c = 0; ok && c < kNeed; ++c) {
      const auto& fld = f[static_cast<size_t>(col[c])];
      ok = detail::parseUint(fld.first, fld.second, c == 0 ? 0xFFFFFFFFull : 0xFFFFull, v[c]);
    }
    if (!has_newline) {
      // Ehjä tiedosto päättyy aina rivinvaihtoon (CsvWriter, serial_record.py):
      // ilman sitä viimeinen rivi on voinut katketa luvun keskeltä.
      out.truncated = true;
      out.warnings.push_back(format("rivi %d: viimeinen rivi ilman rivinvaihtoa ohitettu", line_no));
      break;
    }
    if (!ok) {
      err = format("rivi %d: virheellinen datarivi", line_no);
      return false;
    }
    RawFrame fr;
    fr.seq = static_cast<uint32_t>(v[0]);
    for (int c = 0; c < kChannels; ++c) fr.mv[c] = static_cast<uint16_t>(v[1 + c]);
    fr.rail_mv = 0;
    if (have_prev) {
      const uint32_t d = fr.seq - prev;  // modulo 2^32 kuten RawRing
      if (d == 0 || d > 0x7FFFFFFFu) {
        err = format("rivi %d: seq ei kasva (%lu -> %lu)", line_no, static_cast<unsigned long>(prev),
                     static_cast<unsigned long>(fr.seq));
        return false;
      }
      if (d > RawRing::kSeqWindow) {
        err = format("rivi %d: seq-hyppy %lu näytettä (enintään %lu)", line_no,
                     static_cast<unsigned long>(d), static_cast<unsigned long>(RawRing::kSeqWindow));
        return false;
      }
      if (d != 1) {
        ++out.gaps;
        out.missing += d - 1u;
      }
    }
    prev = fr.seq;
    have_prev = true;
    out.frames.push_back(fr);
  }

  if (!have_columns) {
    err = "sarakeotsake puuttuu";
    return false;
  }
  if (out.frames.empty()) {
    err = "ei näytteitä";
    return false;
  }
  if (const char* fs = out.find("fs_hz")) {
    float hz = 0.0f;
    if (!detail::parseFloat(fs, hz) || hz != static_cast<float>(kSampleRateHz)) {
      err = format("fs_hz=%s: toisto olettaa %lu Hz (types.h kSampleRateHz)", fs,
                   static_cast<unsigned long>(kSampleRateHz));
      return false;
    }
  }
  if (const char* fm = out.find("format")) {
    if (detail::trim(fm) != "1") {
      err = format("format=%s: tunnetaan vain muoto 1 (raw_ring.h CsvWriter::kFormatVersion)", fm);
      return false;
    }
  }
  if (const char* n = out.find("samples")) {
    uint64_t want = 0;
    if (!out.aborted && detail::parseUint(n, n + std::strlen(n), 0xFFFFFFFFull, want) &&
        want != out.frames.size()) {
      out.warnings.push_back(format("otsake samples=%lu, rivejä %lu", static_cast<unsigned long>(want),
                                    static_cast<unsigned long>(out.frames.size())));
    }
  }
  return true;
}

inline bool loadCsv(const std::string& path, Recording& out, std::string& err) {
  std::FILE* fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    err = "ei voi avata: " + path;
    return false;
  }
  std::string text;
  char buf[65536];
  for (;;) {
    const size_t n = std::fread(buf, 1, sizeof(buf), fp);
    if (n == 0) break;
    text.append(buf, n);
  }
  const bool read_err = std::ferror(fp) != 0;
  std::fclose(fp);
  if (read_err) {
    err = "lukuvirhe: " + path;
    return false;
  }
  return parseCsv(text.data(), text.size(), out, err);
}

// Otsakkeen anturi- ja lähdetiedot konfiguraatioon configSetTextillä (sama
// alue- ja muototarkistus kuin POST /api/config). Otsakkeen avaimet ovat samat
// kuin kenttärekisterin (config.h). Puuttuva avain jättää oletuksen.
inline bool headerConfig(const Recording& r, Config& cfg, std::string& err) {
  static const char* const kKeys[] = {"v_zero_mv", "mv_per_kpa", "rail_comp", "rail_nominal_mv"};
  for (const char* key : kKeys) {
    const char* v = r.find(key);
    if (v == nullptr) continue;
    const SetResult res = configSetText(cfg, key, v);
    if (res != SetResult::Ok) {
      err = detail::format("otsake %s=%s: %s", key, v, setResultText(res));
      return false;
    }
  }
  if (const char* s = r.find("source")) {
    // serial_record.py kirjoittaa "?", jos laite ei kertonut lähdettä: oletus jää.
    const char* code = std::strcmp(s, "sim") == 0      ? "0"
                       : std::strcmp(s, "adc") == 0    ? "1"
                       : std::strcmp(s, "hx710b") == 0 ? "2"
                                                       : nullptr;
    if (code != nullptr) configSetText(cfg, "source", code);
  }
  return true;
}

// Otsakkeen kalibrointi. Puuttuva avain jättää oletuksen (offset 0, gain 1,
// ei voimassa), kuten laite ilman nollausta ja vahvistusta.
inline bool headerCalibration(const Recording& r, Calibration& cal, std::string& err) {
  cal = Calibration();
  if (const char* v = r.find("offset_kpa")) {
    if (!detail::parseTriple(v, cal.offset_kpa)) {
      err = detail::format("otsake offset_kpa=%s: odotetaan kolmea lukua", v);
      return false;
    }
  }
  if (const char* v = r.find("gain")) {
    if (!detail::parseTriple(v, cal.gain)) {
      err = detail::format("otsake gain=%s: odotetaan kolmea lukua", v);
      return false;
    }
  }
  if (const char* v = r.find("zero_valid")) {
    if (!detail::parseBit(v, cal.zero_valid)) {
      err = detail::format("otsake zero_valid=%s: odotetaan 0 tai 1", v);
      return false;
    }
  }
  if (const char* v = r.find("gain_valid")) {
    if (!detail::parseBit(v, cal.gain_valid)) {
      err = detail::format("otsake gain_valid=%s: odotetaan 0 tai 1", v);
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Toisto
// ---------------------------------------------------------------------------

struct TickTiming {
  uint32_t poll_ms = 4;   // hal_config.h kProcPollMs
  uint32_t tick_ms = 50;  // proc_task.cpp kTickMs
};

struct TickRecord {
  float t = 0.0f;         // now_s, jolla tick kutsuttiin
  StatusResult st;
  RpmEstimate rpm;
};

// Syöttää kehykset Processoriin laitteen ajoituksella (ks. otsake) ja kutsuu
// on_tick(now_s) jokaisen tickin jälkeen.
template <typename OnTick>
void feedFrames(Processor& p, const std::vector<RawFrame>& frames, const TickTiming& tm,
                OnTick&& on_tick) {
  if (frames.empty() || tm.poll_ms == 0) return;
  const uint32_t seq0 = frames.front().seq;
  size_t k = 0;
  uint64_t last_tick = 0;
  for (uint64_t t = tm.poll_ms; k < frames.size(); t += tm.poll_ms) {
    while (k < frames.size() && static_cast<uint64_t>(frames[k].seq - seq0) < t) p.push(frames[k++]);
    if (t - last_tick >= tm.tick_ms) {
      last_tick = t;
      const float now = static_cast<float>(t) / 1000.0f;
      p.tick(now);
      on_tick(now);
    }
  }
}

struct ReplayOptions {
  bool no_cal = false;            // aloita kalibroimattomana (offset 0, gain 1, ei voimassa)
  std::vector<MetaEntry> set;     // --set: konfiguraatio otsakkeen jälkeen
  TickTiming timing;
};

struct ReplayResult {
  Config cfg;
  Calibration cal_start;          // voimassa toiston alussa (restoreCalibrationin jälkeen)
  Calibration cal_end;            // lopussa (automaattinen nollaus voi muuttaa)
  uint32_t ring_gaps = 0;         // Processorin renkaan aukot (snapshotin diag.gaps)
  std::vector<TickRecord> ticks;
};

// Konfiguraatio otsakkeesta + --set, kalibrointi otsakkeesta (tai ei, no_cal),
// sitten kehykset laitteen ajoituksella. Sama Processor-koodi kuin laitteella.
inline bool replayRecording(const Recording& rec, const ReplayOptions& o, ReplayResult& out,
                            std::string& err) {
  err.clear();
  out = ReplayResult();
  if (o.timing.poll_ms == 0 || o.timing.tick_ms == 0) {
    err = "poll_ms ja tick_ms > 0";
    return false;
  }
  Config hdr;
  if (!headerConfig(rec, hdr, err)) return false;
  char why[192];
  if (!configValidate(hdr, why, sizeof(why))) {
    err = std::string("otsake: ") + why;
    return false;
  }
  Config cfg = hdr;
  for (const MetaEntry& s : o.set) {
    const SetResult r = configSetText(cfg, s.key.c_str(), s.value.c_str());
    if (r != SetResult::Ok) {
      err = detail::format("%s=%s: %s", s.key.c_str(), s.value.c_str(), setResultText(r));
      return false;
    }
  }
  if (!configValidate(cfg, why, sizeof(why))) {
    err = why;
    return false;
  }
  Calibration cal;
  if (!o.no_cal && !headerCalibration(rec, cal, err)) return false;

  // Ensin laitteen konfiguraatio ja sen kalibrointi, sitten --set. Jos --set
  // muuttaa anturin siirtofunktiota tai lähdettä, Processor::configure
  // mitätöi nollauksen itse (D11), kuten laitteella.
  std::vector<RawSample> mem(kRingCapacity);
  std::unique_ptr<Processor> p(new Processor(mem.data(), mem.size()));
  if (!p->configure(hdr)) {
    err = "otsakkeen konfiguraatio hylättiin";
    return false;
  }
  p->restoreCalibration(cal);
  if (!p->configure(cfg)) {
    err = "konfiguraatio hylättiin";
    return false;
  }
  out.cfg = cfg;
  out.cal_start = p->calibration();
  out.ticks.reserve(static_cast<size_t>(rec.durationS() * 20.0f) + 16u);
  Processor& proc = *p;
  feedFrames(proc, rec.frames, o.timing, [&](float now) {
    TickRecord tr;
    tr.t = now;
    tr.st = proc.status();
    tr.rpm = proc.rpm();
    out.ticks.push_back(tr);
  });
  out.cal_end = proc.calibration();
  out.ring_gaps = proc.ring().gaps();
  return true;
}

// Rengas CSV:ksi täsmälleen kuten GET /api/raw.csv (CsvWriter).
inline std::string ringToCsv(const RawRing& ring, const Config& cfg, const Calibration& cal) {
  CsvWriter w(ring, cfg, cal);
  std::string out;
  char buf[4096];
  for (;;) {
    const size_t n = w.fill(buf, sizeof(buf));
    if (n == 0) break;
    out.append(buf, n);
  }
  return out;
}

// EngineSimin nauhoitus samassa muodossa kuin laitteen /api/raw.csv: tuore
// simulaattori (seed sp.seed, seq alkaa nollasta) cfg.sensorilla -> RawRing ->
// CsvWriter. Otsakkeen kalibrointi on cal (CSV:n kPa-sarakkeet lasketaan sillä).
inline bool simToCsv(const SimParams& sp, const Config& cfg, const Calibration& cal, uint32_t samples,
                     std::string& out, std::string& err) {
  if (samples == 0 || samples > kMaxCsvSamples) {
    err = detail::format("näytteitä 1…%lu (16-bittinen seq-ikkuna, raw_ring.h)",
                         static_cast<unsigned long>(kMaxCsvSamples));
    return false;
  }
  EngineSim sim;
  sim.configure(sp, cfg.sensor);
  std::vector<RawSample> mem(samples);
  RawRing ring(mem.data(), mem.size());
  for (uint32_t i = 0; i < samples; ++i) ring.push(sim.next());
  out = ringToCsv(ring, cfg, cal);
  return true;
}

// ---------------------------------------------------------------------------
// Yhteenveto
// ---------------------------------------------------------------------------

constexpr int kStateCount = static_cast<int>(SyncState::FaultMaster) + 1;

inline bool isVerdictState(SyncState s) {
  return s == SyncState::Synced || s == SyncState::Near || s == SyncState::OutOfSync;
}
inline bool isFaultState(SyncState s) {
  return s == SyncState::FaultSensor || s == SyncState::FaultHose || s == SyncState::FaultMaster;
}
inline bool parseState(const std::string& name, SyncState& out) {
  for (int i = 0; i < kStateCount; ++i) {
    const SyncState s = static_cast<SyncState>(i);
    if (name == syncStateKey(s)) {
      out = s;
      return true;
    }
  }
  return false;
}

// Liput snapshotin nimillä (§5 flags).
inline bool flagByName(const StatusResult& r, const std::string& name, bool& out) {
  if (name == "sat") out = r.flag_saturation;
  else if (name == "uncal_zero") out = r.flag_uncal_zero;
  else if (name == "uncal_gain") out = r.flag_uncal_gain;
  else if (name == "drift") out = r.flag_drift;
  else return false;
  return true;
}

struct FaultSeen {
  SyncState state = SyncState::FaultHose;
  int ch = -1;
  float first_s = 0.0f;
  float total_s = 0.0f;
};

// Ensimmäinen tuomiotila (synced, near, out_of_sync) nauhoituksen alusta; -1 = ei.
inline float firstLock(const std::vector<TickRecord>& ticks, SyncState* state = nullptr) {
  for (const TickRecord& t : ticks) {
    if (isVerdictState(t.st.state)) {
      if (state != nullptr) *state = t.st.state;
      return t.t;
    }
  }
  return -1.0f;
}

struct Summary {
  int ticks = 0;                      // ikkunan tikit
  float from_s = 0.0f;                // ikkunan alku
  float end_s = 0.0f;                 // viimeinen tick
  float state_s[kStateCount] = {};    // aika tiloittain: tickiä edeltävä väli tickin tilalle
  float first_lock_s = -1.0f;         // nauhoituksen alusta, ikkunasta riippumatta
  SyncState first_lock_state = SyncState::NoSignal;
  int rpm_n = 0;                      // tikit, joilla rpm voimassa
  float rpm_mean = 0.0f;
  float rpm_sd = 0.0f;                // keskihajonta tikkien yli (populaatio)
  int spread_n = 0;                   // tuomiotikit (show_verdict)
  float spread_median = 0.0f;
  float spread_p95 = 0.0f;            // lähin järjestysluku: ceil(0,95 n):s pienin
  std::vector<FaultSeen> faults;      // vikatilat ja -kanavat ilmestymisjärjestyksessä
};

// Tilastot tikeistä, joilla t >= from_s.
inline Summary summarize(const std::vector<TickRecord>& ticks, float from_s = 0.0f) {
  Summary s;
  s.from_s = from_s;
  s.first_lock_s = firstLock(ticks, &s.first_lock_state);
  float prev_t = 0.0f;
  float mean = 0.0f;
  float m2 = 0.0f;
  std::vector<float> spreads;
  for (const TickRecord& t : ticks) {
    const float dt = t.t - prev_t;
    prev_t = t.t;
    if (t.t < from_s) continue;
    ++s.ticks;
    s.end_s = t.t;
    s.state_s[static_cast<int>(t.st.state)] += dt;
    if (t.rpm.valid) {
      ++s.rpm_n;
      const float d = t.rpm.rpm - mean;
      mean += d / static_cast<float>(s.rpm_n);
      m2 += d * (t.rpm.rpm - mean);
    }
    if (t.st.show_verdict) spreads.push_back(t.st.spread_kpa);
    if (isFaultState(t.st.state)) {
      FaultSeen* f = nullptr;
      for (FaultSeen& g : s.faults) {
        if (g.state == t.st.state && g.ch == t.st.fault_ch) f = &g;
      }
      if (f == nullptr) {
        FaultSeen g;
        g.state = t.st.state;
        g.ch = t.st.fault_ch;
        g.first_s = t.t;
        s.faults.push_back(g);
        f = &s.faults.back();
      }
      f->total_s += dt;
    }
  }
  if (s.rpm_n > 0) {
    s.rpm_mean = mean;
    s.rpm_sd = std::sqrt(m2 / static_cast<float>(s.rpm_n));
  }
  s.spread_n = static_cast<int>(spreads.size());
  if (!spreads.empty()) {
    std::sort(spreads.begin(), spreads.end());
    const size_t n = spreads.size();
    s.spread_median = n % 2 == 1 ? spreads[n / 2] : 0.5f * (spreads[n / 2 - 1] + spreads[n / 2]);
    s.spread_p95 = spreads[(95u * n + 99u) / 100u - 1u];
  }
  return s;
}

// ---------------------------------------------------------------------------
// Odotustiedosto (.expect), muoto: test/test_replay/test_main.cpp:n otsake ja
// docs/firmware.md §6.1.
// ---------------------------------------------------------------------------

struct Expect {
  bool from_lock = false;             // from_s=lock: ikkuna alkaa ensimmäisestä tuomiosta
  float from_s = 0.0f;
  bool has_final = false;
  SyncState final_state = SyncState::NoSignal;
  std::vector<SyncState> allowed;     // ikkunassa vain nämä (tyhjä = ei tarkisteta)
  std::vector<SyncState> never;       // ei koskaan koko nauhoituksessa
  bool has_lock_min = false;
  float lock_min_s = 0.0f;
  bool has_lock_max = false;
  float lock_max_s = 0.0f;
  bool has_rpm = false;
  float rpm = 0.0f;
  bool has_rpm_tol = false;
  float rpm_tol = 0.0f;
  bool has_spread_median_min = false;
  float spread_median_min = 0.0f;
  bool has_spread_median_max = false;
  float spread_median_max = 0.0f;
  bool has_spread_p95_max = false;
  float spread_p95_max = 0.0f;
  int worst_ch = -2;                  // -2 = ei tarkisteta
  int fault_ch = -2;
  std::vector<std::string> flags_never;
  std::vector<std::string> flags_always;
  bool has_headline = false;
  std::string headline;               // ikkunan jokaisessa tickissä täsmälleen
  bool has_advice_prefix = false;
  std::string advice_prefix;          // ikkunan jokaisen tickin advice alkaa tällä
  ReplayOptions options;
};

namespace detail {

inline bool splitList(const std::string& v, std::vector<std::string>& out) {
  out.clear();
  size_t pos = 0;
  for (;;) {
    const size_t comma = v.find(',', pos);
    const size_t len = comma == std::string::npos ? std::string::npos : comma - pos;
    const std::string item = trim(v.substr(pos, len));
    if (item.empty()) return false;
    out.push_back(item);
    if (comma == std::string::npos) return true;
    pos = comma + 1;
  }
}

inline bool parseStates(const std::string& v, std::vector<SyncState>& out) {
  std::vector<std::string> names;
  if (!splitList(v, names)) return false;
  out.clear();
  for (const std::string& n : names) {
    SyncState s;
    if (!parseState(n, s)) return false;
    out.push_back(s);
  }
  return true;
}

inline bool parseFlags(const std::string& v, std::vector<std::string>& out) {
  if (!splitList(v, out)) return false;
  const StatusResult probe;
  for (const std::string& n : out) {
    bool b = false;
    if (!flagByName(probe, n, b)) return false;
  }
  return true;
}

inline bool parseInt(const std::string& v, int lo, int hi, int& out) {
  const std::string t = trim(v);
  char* end = nullptr;
  const long x = std::strtol(t.c_str(), &end, 10);
  if (t.empty() || *end != '\0' || x < lo || x > hi) return false;
  out = static_cast<int>(x);
  return true;
}

}  // namespace detail

// Rivit "avain=arvo"; '#' aloittaa kommentin (myös rivin lopussa). Tuntematon
// avain tai virheellinen arvo on virhe, jotta kirjoitusvirhe ei ohita väitettä.
inline bool parseExpect(const std::string& text, Expect& e, std::string& err) {
  using detail::format;
  e = Expect();
  err.clear();
  size_t pos = 0;
  int line_no = 0;
  while (pos <= text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    ++line_no;
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    line = detail::trim(line);
    if (line.empty()) continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      err = format("rivi %d: odotetaan avain=arvo", line_no);
      return false;
    }
    const std::string key = detail::trim(line.substr(0, eq));
    const std::string val = detail::trim(line.substr(eq + 1));
    bool ok = true;
    if (key == "from_s") {
      if (val == "lock") {
        e.from_lock = true;
      } else {
        ok = detail::parseFloat(val, e.from_s) && e.from_s >= 0.0f;
      }
    } else if (key == "final_state") {
      ok = parseState(val, e.final_state);
      e.has_final = ok;
    } else if (key == "allowed_states") {
      ok = detail::parseStates(val, e.allowed);
    } else if (key == "never_states") {
      ok = detail::parseStates(val, e.never);
    } else if (key == "lock_min_s") {
      ok = e.has_lock_min = detail::parseFloat(val, e.lock_min_s);
    } else if (key == "lock_max_s") {
      ok = e.has_lock_max = detail::parseFloat(val, e.lock_max_s);
    } else if (key == "rpm") {
      ok = e.has_rpm = detail::parseFloat(val, e.rpm);
    } else if (key == "rpm_tol") {
      ok = e.has_rpm_tol = detail::parseFloat(val, e.rpm_tol) && e.rpm_tol >= 0.0f;
    } else if (key == "spread_median_min") {
      ok = e.has_spread_median_min = detail::parseFloat(val, e.spread_median_min);
    } else if (key == "spread_median_max") {
      ok = e.has_spread_median_max = detail::parseFloat(val, e.spread_median_max);
    } else if (key == "spread_p95_max") {
      ok = e.has_spread_p95_max = detail::parseFloat(val, e.spread_p95_max);
    } else if (key == "worst_ch") {
      ok = detail::parseInt(val, 0, kChannels - 1, e.worst_ch);
    } else if (key == "fault_ch") {
      ok = detail::parseInt(val, -1, kChannels - 1, e.fault_ch);
    } else if (key == "flags_never") {
      ok = detail::parseFlags(val, e.flags_never);
    } else if (key == "flags_always") {
      ok = detail::parseFlags(val, e.flags_always);
    } else if (key == "headline") {
      e.headline = val;
      ok = e.has_headline = !val.empty();
    } else if (key == "advice_prefix") {
      e.advice_prefix = val;
      ok = e.has_advice_prefix = !val.empty();
    } else if (key == "no_cal") {
      ok = detail::parseBit(val.c_str(), e.options.no_cal);
    } else if (key.compare(0, 4, "set.") == 0 && key.size() > 4) {
      MetaEntry m;
      m.key = key.substr(4);
      m.value = val;
      ok = configFindField(m.key.c_str()) >= 0;
      e.options.set.push_back(m);
    } else {
      err = format("rivi %d: tuntematon avain: %s", line_no, key.c_str());
      return false;
    }
    if (!ok) {
      err = format("rivi %d: virheellinen arvo: %s=%s", line_no, key.c_str(), val.c_str());
      return false;
    }
  }
  if (e.has_rpm != e.has_rpm_tol) {
    err = "rpm ja rpm_tol annetaan yhdessä";
    return false;
  }
  return true;
}

// Väitteet tikkisarjaa vasten. Palauttaa rikotut väitteet tekstinä (tyhjä = OK).
inline std::vector<std::string> checkExpect(const Expect& e, const std::vector<TickRecord>& ticks) {
  using detail::format;
  std::vector<std::string> fails;
  if (ticks.empty()) {
    fails.push_back("ei yhtään tickiä");
    return fails;
  }
  const float lock = firstLock(ticks);
  if (e.has_lock_min && lock >= 0.0f && lock < e.lock_min_s) {
    fails.push_back(format("lukitus %.3f s < lock_min_s %.3f", static_cast<double>(lock),
                           static_cast<double>(e.lock_min_s)));
  }
  if (e.has_lock_max && (lock < 0.0f || lock > e.lock_max_s)) {
    fails.push_back(lock < 0.0f ? std::string("ei lukitusta (lock_max_s)")
                                : format("lukitus %.3f s > lock_max_s %.3f", static_cast<double>(lock),
                                         static_cast<double>(e.lock_max_s)));
  }
  for (const TickRecord& t : ticks) {
    if (std::find(e.never.begin(), e.never.end(), t.st.state) != e.never.end()) {
      fails.push_back(format("t=%.3f: tila %s (never_states)", static_cast<double>(t.t),
                             syncStateKey(t.st.state)));
      break;
    }
  }
  if (e.has_final && ticks.back().st.state != e.final_state) {
    fails.push_back(format("lopputila %s, odotettu %s", syncStateKey(ticks.back().st.state),
                           syncStateKey(e.final_state)));
  }

  float from = e.from_s;
  if (e.from_lock) {
    if (lock < 0.0f) {
      fails.push_back("from_s=lock, mutta tuomiota ei tullut");
      return fails;
    }
    from = lock;
  }
  const Summary s = summarize(ticks, from);
  if (s.ticks == 0) {
    fails.push_back(format("ei tikkejä ajasta %.3f s alkaen", static_cast<double>(from)));
    return fails;
  }
  bool allowed_done = false;
  bool worst_done = false;
  bool fault_done = false;
  bool head_done = false;
  bool advice_done = false;
  for (const TickRecord& t : ticks) {
    if (t.t < from) continue;
    if (!head_done && e.has_headline && e.headline != t.st.headline) {
      fails.push_back(format("t=%.3f: headline \"%s\", odotettu \"%s\"", static_cast<double>(t.t),
                             t.st.headline, e.headline.c_str()));
      head_done = true;
    }
    if (!advice_done && e.has_advice_prefix &&
        std::strncmp(t.st.advice, e.advice_prefix.c_str(), e.advice_prefix.size()) != 0) {
      fails.push_back(format("t=%.3f: advice \"%s\" ei ala \"%s\"", static_cast<double>(t.t), t.st.advice,
                             e.advice_prefix.c_str()));
      advice_done = true;
    }
    if (!allowed_done && !e.allowed.empty() &&
        std::find(e.allowed.begin(), e.allowed.end(), t.st.state) == e.allowed.end()) {
      fails.push_back(format("t=%.3f: tila %s ei ole allowed_states", static_cast<double>(t.t),
                             syncStateKey(t.st.state)));
      allowed_done = true;
    }
    if (!worst_done && e.worst_ch >= 0 && t.st.show_verdict && t.st.worst_ch != e.worst_ch) {
      fails.push_back(format("t=%.3f: worst_ch %d, odotettu %d", static_cast<double>(t.t), t.st.worst_ch,
                             e.worst_ch));
      worst_done = true;
    }
    if (!fault_done && e.fault_ch >= -1 && t.st.fault_ch != e.fault_ch) {
      fails.push_back(format("t=%.3f: fault_ch %d, odotettu %d", static_cast<double>(t.t), t.st.fault_ch,
                             e.fault_ch));
      fault_done = true;
    }
  }
  for (const std::string& name : e.flags_never) {
    for (const TickRecord& t : ticks) {
      bool on = false;
      if (t.t >= from && flagByName(t.st, name, on) && on) {
        fails.push_back(format("t=%.3f: lippu %s päällä (flags_never)", static_cast<double>(t.t),
                               name.c_str()));
        break;
      }
    }
  }
  for (const std::string& name : e.flags_always) {
    for (const TickRecord& t : ticks) {
      bool on = true;
      if (t.t >= from && flagByName(t.st, name, on) && !on) {
        fails.push_back(format("t=%.3f: lippu %s pois (flags_always)", static_cast<double>(t.t),
                               name.c_str()));
        break;
      }
    }
  }
  if (e.has_rpm) {
    if (s.rpm_n == 0) {
      fails.push_back("rpm: ei voimassa olevaa arviota ikkunassa");
    } else if (std::fabs(s.rpm_mean - e.rpm) > e.rpm_tol) {
      fails.push_back(format("rpm %.1f, odotettu %.1f ± %.1f", static_cast<double>(s.rpm_mean),
                             static_cast<double>(e.rpm), static_cast<double>(e.rpm_tol)));
    }
  }
  const bool want_spread = e.has_spread_median_min || e.has_spread_median_max || e.has_spread_p95_max;
  if (want_spread && s.spread_n == 0) {
    fails.push_back("spread: ei tuomiotikkejä ikkunassa");
  } else if (want_spread) {
    if (e.has_spread_median_min && s.spread_median < e.spread_median_min) {
      fails.push_back(format("spread-mediaani %.3f < %.3f", static_cast<double>(s.spread_median),
                             static_cast<double>(e.spread_median_min)));
    }
    if (e.has_spread_median_max && s.spread_median > e.spread_median_max) {
      fails.push_back(format("spread-mediaani %.3f > %.3f", static_cast<double>(s.spread_median),
                             static_cast<double>(e.spread_median_max)));
    }
    if (e.has_spread_p95_max && s.spread_p95 > e.spread_p95_max) {
      fails.push_back(format("spread p95 %.3f > %.3f", static_cast<double>(s.spread_p95),
                             static_cast<double>(e.spread_p95_max)));
    }
  }
  return fails;
}

}  // namespace rec
}  // namespace sbx
