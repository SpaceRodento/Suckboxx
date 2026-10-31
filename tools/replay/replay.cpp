// Suckboxx - nauhoitusten toisto PC:llä (WP-8, docs/firmware.md §6.1).
//
// Ajaa raakanauhoituksen (laitteen /api/raw.csv tai tools/serial_record.py)
// täsmälleen saman Processor-koodin läpi kuin laite, laitteen ajoituksella
// (test/support/recording.h). Toinen tila kirjoittaa EngineSimin nauhoituksen
// samassa CSV-muodossa (raw_ring.h CsvWriter), jota myös tools/sim_fit.py (WP-12)
// käyttää.
//
//   replay <csv> [--set avain=arvo ...] [--jsonl out.jsonl] [--no-cal]
//                [--expect x.expect] [--from S|lock] [--poll-ms 4] [--tick-ms 50]
//   replay --sim [avain=arvo ...] --seconds N --out x.csv [--seed S]
//                [--set avain=arvo ...] [--zero-valid 0|1] [--gain-valid 0|1]
//
// Toisto:
//   --set        konfiguraatiokenttä (config.h, sama kuin POST /api/config),
//                otsakkeen anturiarvojen jälkeen. Toistettavissa.
//   --no-cal     aloita kalibroimattomana (offset 0, gain 1, zero_valid = 0).
//                Oletus: otsakkeen offset_kpa, gain, zero_valid ja gain_valid.
//   --jsonl      rivi per tick: {"t","state","rpm","rpm_cv","spread","d":[3],
//                "worst","flags":{"sat","uncal_zero","uncal_gain","drift"},
//                "fault_ch","headline","advice"}. rpm ja rpm_cv null, kun arvio
//                ei ole voimassa. t = tickin now_s nauhoituksen alusta.
//   --expect     tarkista odotustiedosto (muoto: test/test_replay/test_main.cpp);
//                sen no_cal ja set.* ovat voimassa, CLI:n --set lisätään perään.
//                Poistumiskoodi 1, jos jokin väite ei pidä.
//   --from       yhteenvedon ikkuna (tilastot tikeistä t >= S tai lukituksesta).
//   --poll-ms, --tick-ms
//                ajoitus (oletus laitteen 4 / 50 ms; 1 / 50 = test_integrationin
//                Bench).
//
// Simulaattori:
//   avain=arvo   /api/sim-avaimet (sim_api.h, docs/firmware.md §5): engine_on,
//                rpm, jitter, drift, base, err1..3, depth, res, xtalk, peak, noise,
//                fault1..3. Muut SimParams-kentät oletuksina. Rajaus kuten POST
//                /api/sim; tuntematon avain tai virheellinen arvo on virhe.
//   --seconds    nauhoituksen pituus, enintään 65,536 s (16-bittinen seq-ikkuna).
//   --seed       SimParams::seed (oletus 1). Sama seed ja avaimet = sama tiedosto.
//   --set        anturiparametrit (v_zero_mv, mv_per_kpa ...) simulaattorille ja
//                otsakkeeseen.
//   --zero-valid otsakkeen zero_valid (oletus 1). EngineSimissä ei ole
//                anturivirheitä /api/sim-avaimilla (sensor_offset_kpa = 0), joten
//                nollaus antaisi offsetin 0; otsake kertoo sen tehdyksi, jotta
//                toisto voi näyttää vihreää (D11). 0 = kuin laite ilman nollausta.
//   --gain-valid otsakkeen gain_valid (oletus 0: ei vahvistuskalibrointia, rivi 11).
// Otsakkeen loppuun (ennen sarakeriviä) lisätään sim_seed= ja sim= (GET
// /api/sim -muotoinen JSON), jotta nauhoituksen totuus on tiedostossa.
//
// Poistumiskoodi: 0 = OK, 1 = virhe tai rikottu odotus, 2 = käyttövirhe.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/json_writer.h"
#include "core/sim_api.h"
#include "recording.h"

namespace {

using sbx::rec::MetaEntry;

int usage(const char* why) {
  if (why != nullptr) std::fprintf(stderr, "replay: %s\n", why);
  std::fprintf(stderr,
               "käyttö:\n"
               "  replay <csv> [--set avain=arvo ...] [--jsonl out.jsonl] [--no-cal]\n"
               "               [--expect x.expect] [--from S|lock] [--poll-ms 4] [--tick-ms 50]\n"
               "  replay --sim [avain=arvo ...] --seconds N --out x.csv [--seed S]\n"
               "               [--set avain=arvo ...] [--zero-valid 0|1] [--gain-valid 0|1]\n");
  return 2;
}

bool splitPair(const std::string& s, MetaEntry& out) {
  const size_t eq = s.find('=');
  if (eq == std::string::npos || eq == 0) return false;
  out.key = s.substr(0, eq);
  out.value = s.substr(eq + 1);
  return true;
}

bool parseU32(const std::string& s, uint32_t& out) {
  uint64_t v = 0;
  if (!sbx::rec::detail::parseUint(s.data(), s.data() + s.size(), 0xFFFFFFFFull, v)) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

bool readFile(const std::string& path, std::string& out) {
  std::FILE* fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) return false;
  char buf[8192];
  for (;;) {
    const size_t n = std::fread(buf, 1, sizeof(buf), fp);
    if (n == 0) break;
    out.append(buf, n);
  }
  const bool ok = std::ferror(fp) == 0;
  std::fclose(fp);
  return ok;
}

bool writeFile(const std::string& path, const std::string& text) {
  std::FILE* fp = std::fopen(path.c_str(), "wb");  // "wb": LF myös Windowsilla
  if (fp == nullptr) return false;
  const bool ok = std::fwrite(text.data(), 1, text.size(), fp) == text.size();
  return std::fclose(fp) == 0 && ok;
}

size_t tickJson(const sbx::rec::TickRecord& t, char* buf, size_t n) {
  sbx::JsonWriter w(buf, n);
  w.beginObject();
  w.key("t").valueTrim(t.t, 3);
  w.key("state").value(sbx::syncStateKey(t.st.state));
  w.key("rpm");
  if (t.rpm.valid) {
    w.valueTrim(t.rpm.rpm, 1);
  } else {
    w.null();
  }
  w.key("rpm_cv");
  if (t.rpm.valid) {
    w.valueTrim(t.rpm.cv, 4);
  } else {
    w.null();
  }
  w.key("spread").valueTrim(t.st.spread_kpa, 3);
  w.key("d").beginArray();
  for (int c = 0; c < sbx::kChannels; ++c) w.valueTrim(t.st.vac_delta_kpa[c], 3);
  w.endArray();
  w.key("worst").value(static_cast<int>(t.st.worst_ch));
  w.key("flags").beginObject();
  w.key("sat").value(t.st.flag_saturation);
  w.key("uncal_zero").value(t.st.flag_uncal_zero);
  w.key("uncal_gain").value(t.st.flag_uncal_gain);
  w.key("drift").value(t.st.flag_drift);
  w.endObject();
  w.key("fault_ch").value(static_cast<int>(t.st.fault_ch));
  w.key("headline").value(t.st.headline);
  w.key("advice").value(t.st.advice);
  w.endObject();
  return w.ok() ? w.length() : 0;
}

void printCal(const char* what, const sbx::Calibration& c) {
  std::printf("%s: zero_valid=%d gain_valid=%d offset_kpa=%.4f,%.4f,%.4f gain=%.6f,%.6f,%.6f\n", what,
              c.zero_valid ? 1 : 0, c.gain_valid ? 1 : 0, static_cast<double>(c.offset_kpa[0]),
              static_cast<double>(c.offset_kpa[1]), static_cast<double>(c.offset_kpa[2]),
              static_cast<double>(c.gain[0]), static_cast<double>(c.gain[1]),
              static_cast<double>(c.gain[2]));
}

bool sameCal(const sbx::Calibration& a, const sbx::Calibration& b) {
  for (int c = 0; c < sbx::kChannels; ++c) {
    if (!(a.offset_kpa[c] == b.offset_kpa[c] && a.gain[c] == b.gain[c])) return false;
  }
  return a.zero_valid == b.zero_valid && a.gain_valid == b.gain_valid;
}

void printSummary(const sbx::rec::Summary& s) {
  std::printf("aika tiloittain (t >= %.3f s):\n", static_cast<double>(s.from_s));
  for (int i = 0; i < sbx::rec::kStateCount; ++i) {
    if (s.state_s[i] > 0.0f) {
      std::printf("  %-13s %8.3f s\n", sbx::syncStateKey(static_cast<sbx::SyncState>(i)),
                  static_cast<double>(s.state_s[i]));
    }
  }
  if (s.first_lock_s >= 0.0f) {
    std::printf("ensimmäinen lukitus: %.3f s (%s)\n", static_cast<double>(s.first_lock_s),
                sbx::syncStateKey(s.first_lock_state));
  } else {
    std::printf("ensimmäinen lukitus: ei (ei synced/near/out_of_sync)\n");
  }
  if (s.rpm_n > 0) {
    std::printf("rpm: %.1f ± %.1f (n=%d tickiä)\n", static_cast<double>(s.rpm_mean),
                static_cast<double>(s.rpm_sd), s.rpm_n);
  } else {
    std::printf("rpm: ei voimassa olevaa arviota\n");
  }
  if (s.spread_n > 0) {
    std::printf("spread (tuomiotikit): mediaani %.3f kPa, p95 %.3f kPa (n=%d)\n",
                static_cast<double>(s.spread_median), static_cast<double>(s.spread_p95), s.spread_n);
  } else {
    std::printf("spread: ei tuomiotikkejä\n");
  }
  if (s.faults.empty()) {
    std::printf("vikatilat: ei\n");
  } else {
    for (const sbx::rec::FaultSeen& f : s.faults) {
      std::printf("vikatila: %s fault_ch=%d, ensin %.3f s, yhteensä %.3f s\n", sbx::syncStateKey(f.state),
                  f.ch, static_cast<double>(f.first_s), static_cast<double>(f.total_s));
    }
  }
}

int runSim(const std::vector<std::string>& args) {
  sbx::SimParams sp;
  sbx::Config cfg;
  sbx::Calibration cal;
  cal.zero_valid = true;
  cal.gain_valid = false;
  float seconds = 0.0f;
  std::string out;
  bool want_pairs = false;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    const bool has_next = i + 1 < args.size();
    if (a == "--sim") {
      want_pairs = true;
    } else if (a == "--seconds" && has_next) {
      if (!sbx::rec::detail::parseFloat(args[++i], seconds) || !(seconds > 0.0f)) {
        return usage("--seconds: odotetaan positiivista lukua");
      }
      want_pairs = false;
    } else if (a == "--out" && has_next) {
      out = args[++i];
      want_pairs = false;
    } else if (a == "--seed" && has_next) {
      if (!parseU32(args[++i], sp.seed)) return usage("--seed: odotetaan kokonaislukua 0…4294967295");
      want_pairs = false;
    } else if (a == "--set" && has_next) {
      MetaEntry m;
      if (!splitPair(args[++i], m)) return usage("--set: odotetaan avain=arvo");
      const sbx::SetResult r = sbx::configSetText(cfg, m.key.c_str(), m.value.c_str());
      if (r != sbx::SetResult::Ok) {
        std::fprintf(stderr, "replay: --set %s=%s: %s\n", m.key.c_str(), m.value.c_str(),
                     sbx::setResultText(r));
        return 1;
      }
      want_pairs = false;
    } else if ((a == "--zero-valid" || a == "--gain-valid") && has_next) {
      bool v = false;
      if (!sbx::rec::detail::parseBit(args[++i].c_str(), v)) return usage("odotetaan 0 tai 1");
      (a == "--zero-valid" ? cal.zero_valid : cal.gain_valid) = v;
      want_pairs = false;
    } else if (want_pairs && a.compare(0, 2, "--") != 0) {
      MetaEntry m;
      if (!splitPair(a, m)) return usage("--sim: odotetaan avain=arvo");
      const sbx::SimSetResult r = sbx::simSetText(sp, m.key.c_str(), m.value.c_str());
      if (r == sbx::SimSetResult::UnknownKey) {
        std::fprintf(stderr, "replay: tuntematon simulaattoriparametri: %s\n", m.key.c_str());
        return 1;
      }
      if (r != sbx::SimSetResult::Ok) {
        std::fprintf(stderr, "replay: virheellinen arvo: %s=%s\n", m.key.c_str(), m.value.c_str());
        return 1;
      }
    } else {
      return usage(("tuntematon argumentti: " + a).c_str());
    }
  }
  if (out.empty() || !(seconds > 0.0f)) return usage("--sim vaatii --seconds ja --out");
  char why[192];
  if (!sbx::configValidate(cfg, why, sizeof(why))) {
    std::fprintf(stderr, "replay: %s\n", why);
    return 1;
  }
  const float n_f = std::round(seconds * static_cast<float>(sbx::kSampleRateHz));
  if (!(n_f >= 1.0f && n_f <= static_cast<float>(sbx::rec::kMaxCsvSamples))) {
    std::fprintf(stderr, "replay: --seconds enintään %.3f (16-bittinen seq-ikkuna, raw_ring.h)\n",
                 static_cast<double>(static_cast<float>(sbx::rec::kMaxCsvSamples) /
                                     static_cast<float>(sbx::kSampleRateHz)));
    return 1;
  }
  const uint32_t samples = static_cast<uint32_t>(n_f);
  std::string csv;
  std::string err;
  if (!sbx::rec::simToCsv(sp, cfg, cal, samples, csv, err)) {
    std::fprintf(stderr, "replay: %s\n", err.c_str());
    return 1;
  }
  // Totuus talteen: simulaattorin parametrit otsakkeen loppuun ennen sarakeriviä.
  char json[sbx::kSimJsonSize];
  if (sbx::simToJson(sp, json, sizeof(json)) == 0) {
    std::fprintf(stderr, "replay: simToJson ei mahtunut\n");
    return 1;
  }
  const size_t cols = csv.find("\nseq,");
  if (cols == std::string::npos) {
    std::fprintf(stderr, "replay: CsvWriterin sarakeriviä ei löytynyt\n");
    return 1;
  }
  char prov[sbx::kSimJsonSize + 96];
  std::snprintf(prov, sizeof(prov), "# generator=tools/replay --sim\n# sim_seed=%lu\n# sim=%s\n",
                static_cast<unsigned long>(sp.seed), json);
  csv.insert(cols + 1, prov);
  if (!writeFile(out, csv)) {
    std::fprintf(stderr, "replay: ei voi kirjoittaa: %s\n", out.c_str());
    return 1;
  }
  std::printf("kirjoitettu: %s (%lu näytettä, %lu tavua)\nsim_seed=%lu sim=%s\n", out.c_str(),
              static_cast<unsigned long>(samples), static_cast<unsigned long>(csv.size()),
              static_cast<unsigned long>(sp.seed), json);
  printCal("otsakkeen kalibrointi", cal);
  return 0;
}

int runReplay(const std::vector<std::string>& args) {
  std::string csv_path;
  std::string jsonl_path;
  std::string expect_path;
  sbx::rec::ReplayOptions opt;
  bool cli_no_cal = false;
  std::vector<MetaEntry> cli_set;
  bool from_lock = false;
  float from_s = 0.0f;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    const bool has_next = i + 1 < args.size();
    if (a == "--set" && has_next) {
      MetaEntry m;
      if (!splitPair(args[++i], m)) return usage("--set: odotetaan avain=arvo");
      cli_set.push_back(m);
    } else if (a == "--jsonl" && has_next) {
      jsonl_path = args[++i];
    } else if (a == "--expect" && has_next) {
      expect_path = args[++i];
    } else if (a == "--no-cal") {
      cli_no_cal = true;
    } else if (a == "--from" && has_next) {
      const std::string& v = args[++i];
      if (v == "lock") {
        from_lock = true;
      } else if (!sbx::rec::detail::parseFloat(v, from_s) || from_s < 0.0f) {
        return usage("--from: odotetaan sekunteja tai lock");
      }
    } else if ((a == "--poll-ms" || a == "--tick-ms") && has_next) {
      uint32_t v = 0;
      if (!parseU32(args[++i], v) || v == 0 || v > 1000) return usage("--poll-ms/--tick-ms: 1…1000");
      (a == "--poll-ms" ? opt.timing.poll_ms : opt.timing.tick_ms) = v;
    } else if (a.compare(0, 2, "--") != 0 && csv_path.empty()) {
      csv_path = a;
    } else {
      return usage(("tuntematon argumentti: " + a).c_str());
    }
  }
  if (csv_path.empty()) return usage("anna CSV-tiedosto");

  sbx::rec::Expect expect;
  std::string err;
  if (!expect_path.empty()) {
    std::string text;
    if (!readFile(expect_path, text)) {
      std::fprintf(stderr, "replay: ei voi lukea: %s\n", expect_path.c_str());
      return 1;
    }
    if (!sbx::rec::parseExpect(text, expect, err)) {
      std::fprintf(stderr, "replay: %s: %s\n", expect_path.c_str(), err.c_str());
      return 1;
    }
    opt.no_cal = expect.options.no_cal;
    opt.set = expect.options.set;
  }
  if (cli_no_cal) opt.no_cal = true;
  opt.set.insert(opt.set.end(), cli_set.begin(), cli_set.end());

  sbx::rec::Recording rec;
  if (!sbx::rec::loadCsv(csv_path, rec, err)) {
    std::fprintf(stderr, "replay: %s: %s\n", csv_path.c_str(), err.c_str());
    return 1;
  }
  const char* src = rec.find("source");
  std::printf("nauhoitus: %s\n  source=%s, %lu näytettä, %.3f s, aukkoja %lu (puuttuu %lu näytettä)\n",
              csv_path.c_str(), src != nullptr ? src : "?", static_cast<unsigned long>(rec.frames.size()),
              static_cast<double>(rec.durationS()), static_cast<unsigned long>(rec.gaps),
              static_cast<unsigned long>(rec.missing));
  for (const std::string& w : rec.warnings) std::printf("  varoitus: %s\n", w.c_str());
  if (const char* rc = rec.find("rail_comp")) {
    if (std::strcmp(rc, "1") == 0) {
      std::printf("  varoitus: rail_comp=1, mutta CSV:ssä ei ole kiskojännitettä "
                  "(toisto ilman korjausta)\n");
    }
  }

  sbx::rec::ReplayResult res;
  if (!sbx::rec::replayRecording(rec, opt, res, err)) {
    std::fprintf(stderr, "replay: %s\n", err.c_str());
    return 1;
  }
  printCal(opt.no_cal ? "kalibrointi (--no-cal)" : "kalibrointi otsakkeesta", res.cal_start);
  if (!sameCal(res.cal_start, res.cal_end)) {
    printCal("kalibrointi lopussa (automaattinen nollaus)", res.cal_end);
  }
  for (const MetaEntry& m : opt.set) std::printf("set: %s=%s\n", m.key.c_str(), m.value.c_str());
  std::printf("ajoitus: poll %lu ms, tick %lu ms -> %lu tickiä, renkaan aukot %lu\n",
              static_cast<unsigned long>(opt.timing.poll_ms), static_cast<unsigned long>(opt.timing.tick_ms),
              static_cast<unsigned long>(res.ticks.size()), static_cast<unsigned long>(res.ring_gaps));

  if (!jsonl_path.empty()) {
    std::FILE* fp = std::fopen(jsonl_path.c_str(), "wb");
    if (fp == nullptr) {
      std::fprintf(stderr, "replay: ei voi kirjoittaa: %s\n", jsonl_path.c_str());
      return 1;
    }
    char line[1024];
    bool ok = true;
    for (const sbx::rec::TickRecord& t : res.ticks) {
      const size_t n = tickJson(t, line, sizeof(line) - 1);
      if (n == 0) {
        ok = false;
        break;
      }
      line[n] = '\n';
      if (std::fwrite(line, 1, n + 1, fp) != n + 1) {
        ok = false;
        break;
      }
    }
    if (std::fclose(fp) != 0 || !ok) {
      std::fprintf(stderr, "replay: JSONL-kirjoitus epäonnistui: %s\n", jsonl_path.c_str());
      return 1;
    }
  }

  float from = from_s;
  if (from_lock) {
    from = sbx::rec::firstLock(res.ticks);
    if (from < 0.0f) from = 0.0f;
  }
  printSummary(sbx::rec::summarize(res.ticks, from));

  if (!expect_path.empty()) {
    const std::vector<std::string> fails = sbx::rec::checkExpect(expect, res.ticks);
    if (fails.empty()) {
      std::printf("odotukset (%s): OK\n", expect_path.c_str());
    } else {
      std::printf("odotukset (%s): %lu rikottu\n", expect_path.c_str(),
                  static_cast<unsigned long>(fails.size()));
      for (const std::string& f : fails) std::printf("  - %s\n", f.c_str());
      return 1;
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args;
  bool sim = false;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
    if (args.back() == "--sim") sim = true;
    if (args.back() == "-h" || args.back() == "--help") {
      usage(nullptr);
      return 0;
    }
  }
  if (args.empty()) return usage(nullptr);
  return sim ? runSim(args) : runReplay(args);
}
