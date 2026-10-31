// WP-8: nauhoitusten toisto ja golden-vektorit (docs/firmware.md §6.1,
// SUUNNITTELU.md §7 vaihe 4).
//
// 1. Golden-vektorit: jokainen firmware/test/data/recordings/<nimi>.csv
//    toistetaan Processorin läpi laitteen ajoituksella (test/support/recording.h)
//    ja tarkistetaan tiedostoa <nimi>.expect vasten. Uuden nauhoituksen
//    lisääminen = CSV + .expect samaan kansioon, ei koodimuutosta.
// 2. Toiston oikeellisuus: sama EngineSim-ajo suoraan Processoriin ja CSV:n
//    kautta (CsvWriter -> parseCsv -> replayRecording) antaa identtisen
//    tikkisarjan, myös pudonneilla näytteillä ja kalibroinnilla; toisto on
//    deterministinen.
// 3. Lukija: /api/raw.csv- ja tools/serial_record.py-muodot, keskeytynyt ja
//    katkennut lataus, rikkinäinen syöte.
//
// Odotustiedosto <nimi>.expect: rivi per väite "avain=arvo", '#' aloittaa
// kommentin (myös rivin lopussa). Jokaisen väitteen viereen sen päättäjä
// (D-numero, käytösmatriisin rivi, firmware.md:n sopimus tai olemassa oleva
// testi) tai merkintä "ehdotus". Tuntematon avain tai virheellinen arvo on
// virhe. Ajat sekunteina nauhoituksen alusta (tickin now_s), tilat ja liput
// snapshotin nimillä (docs/firmware.md §5), kanavat 0-pohjaisina (0 = SYL 1).
// "Ikkuna" = tikit, joilla t >= from_s.
//   from_s=<s>|lock          ikkunan alku (oletus 0); lock = ensimmäinen tuomiotila
//   final_state=<tila>       viimeisen tickin tila
//   allowed_states=<t,...>   ikkunan jokainen tick on jokin näistä
//   never_states=<t,...>     ei koskaan koko nauhoituksessa
//   lock_min_s=<s>           ensimmäinen tuomiotila (synced/near/out_of_sync) aikaisintaan
//   lock_max_s=<s>           ... viimeistään (vaatii tuomion)
//   rpm=<rpm>, rpm_tol=<rpm> voimassa olevien rpm-arvioiden keskiarvo ikkunassa (yhdessä)
//   spread_median_min, spread_median_max, spread_p95_max=<kPa>
//                            ikkunan tuomiotikkien (show_verdict) spread
//   worst_ch=<0..2>          ikkunan tuomiotikeissä pahin kanava
//   fault_ch=<-1..2>         ikkunan jokaisessa tickissä
//   headline=<teksti>        ikkunan jokaisessa tickissä täsmälleen
//   advice_prefix=<teksti>   ikkunan jokaisen tickin advice alkaa tällä
//   flags_never, flags_always=<sat,uncal_zero,uncal_gain,drift>   ikkunassa
//   no_cal=1                 toista kalibroimattomana (kuten replay --no-cal)
//   set.<avain>=<arvo>       konfiguraatio otsakkeen jälkeen (kuten replay --set)
//
// Datakansio löytyy __FILE__:n suhteen (run_native_tests.sh kääntää absoluuttisella
// polulla) tai työhakemistosta: firmware/ (pio test), test/test_replay/
// (run_native_tests.sh) tai repon juuri. SBX_RECORDINGS_DIR ohittaa haun.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/engine_sim.h"
#include "core/processor.h"
#include "core/raw_ring.h"
#include "recording.h"

using sbx::Calibration;
using sbx::Config;
using sbx::EngineSim;
using sbx::Processor;
using sbx::RawFrame;
using sbx::RawRing;
using sbx::RawSample;
using sbx::SimParams;
using sbx::SyncState;
using sbx::kChannels;
namespace rec = sbx::rec;
namespace fs = std::filesystem;

namespace {

// --- datakansio --------------------------------------------------------------

std::string recordingsDir() {
  std::vector<fs::path> cands;
  if (const char* env = std::getenv("SBX_RECORDINGS_DIR")) cands.emplace_back(env);
  cands.push_back(fs::path(__FILE__).parent_path().parent_path() / "data" / "recordings");
  cands.emplace_back("test/data/recordings");
  cands.emplace_back("../data/recordings");
  cands.emplace_back("firmware/test/data/recordings");
  for (const fs::path& c : cands) {
    std::error_code ec;
    if (fs::is_directory(c, ec)) return c.string();
  }
  return std::string();
}

std::vector<fs::path> listByExt(const std::string& dir, const char* ext) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (const fs::directory_entry& d : fs::directory_iterator(dir, ec)) {
    if (d.is_regular_file(ec) && d.path().extension() == ext) out.push_back(d.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool readText(const fs::path& p, std::string& out) {
  std::FILE* fp = std::fopen(p.string().c_str(), "rb");
  if (fp == nullptr) return false;
  char buf[4096];
  for (;;) {
    const size_t n = std::fread(buf, 1, sizeof(buf), fp);
    if (n == 0) break;
    out.append(buf, n);
  }
  std::fclose(fp);
  return true;
}

rec::Recording loadOrFail(const char* name) {
  const std::string dir = recordingsDir();
  TEST_ASSERT_TRUE_MESSAGE(!dir.empty(), "test/data/recordings ei löydy");
  rec::Recording r;
  std::string err;
  const std::string path = (fs::path(dir) / name).string();
  TEST_ASSERT_TRUE_MESSAGE(rec::loadCsv(path, r, err), (path + ": " + err).c_str());
  return r;
}

// --- tikkisarjojen vertailu ---------------------------------------------------

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// Ensimmäinen ero tekstinä; tyhjä = identtiset. Liukuluvut bitteinä.
std::string firstDiff(const std::vector<rec::TickRecord>& a, const std::vector<rec::TickRecord>& b) {
  if (a.size() != b.size()) {
    return rec::detail::format("tikkejä %lu vs %lu", static_cast<unsigned long>(a.size()),
                               static_cast<unsigned long>(b.size()));
  }
  for (size_t i = 0; i < a.size(); ++i) {
    const rec::TickRecord& x = a[i];
    const rec::TickRecord& y = b[i];
    bool same = sameBits(x.t, y.t) && x.st.state == y.st.state && x.st.fault_ch == y.st.fault_ch &&
                x.st.show_bars == y.st.show_bars && x.st.show_verdict == y.st.show_verdict &&
                sameBits(x.st.spread_kpa, y.st.spread_kpa) && x.st.worst_ch == y.st.worst_ch &&
                x.st.flag_saturation == y.st.flag_saturation &&
                x.st.flag_uncal_zero == y.st.flag_uncal_zero &&
                x.st.flag_uncal_gain == y.st.flag_uncal_gain && x.st.flag_drift == y.st.flag_drift &&
                x.st.request_auto_zero == y.st.request_auto_zero &&
                std::strcmp(x.st.headline, y.st.headline) == 0 &&
                std::strcmp(x.st.advice, y.st.advice) == 0 && x.rpm.valid == y.rpm.valid &&
                sameBits(x.rpm.rpm, y.rpm.rpm) && sameBits(x.rpm.sd_rpm, y.rpm.sd_rpm) &&
                sameBits(x.rpm.cv, y.rpm.cv);
    for (int c = 0; c < kChannels; ++c) {
      same = same && sameBits(x.st.vac_delta_kpa[c], y.st.vac_delta_kpa[c]);
    }
    if (!same) {
      return rec::detail::format("tick %lu (t=%.3f / %.3f): %s / %s, spread %.4f / %.4f",
                                 static_cast<unsigned long>(i), static_cast<double>(x.t),
                                 static_cast<double>(y.t), sbx::syncStateKey(x.st.state),
                                 sbx::syncStateKey(y.st.state), static_cast<double>(x.st.spread_kpa),
                                 static_cast<double>(y.st.spread_kpa));
    }
  }
  return std::string();
}

int countVerdictTicks(const std::vector<rec::TickRecord>& ticks) {
  int n = 0;
  for (const rec::TickRecord& t : ticks) n += rec::isVerdictState(t.st.state) ? 1 : 0;
  return n;
}

// --- suora ajo ilman CSV:tä ----------------------------------------------------

struct DirectRun {
  std::vector<rec::TickRecord> ticks;
  std::string csv;      // sama ajo laitteen /api/raw.csv -muodossa
  uint32_t gaps = 0;    // Processorin renkaan aukot
};

// EngineSim -> Processor kuten laitteella (src/hal/proc_task.cpp): kehys k on
// käsittelytaskin saatavilla hetkellä k + 1 ms, taski herää poll_ms:n välein
// ja tikkaa, kun edellisestä tickistä on >= 50 ms. drop(k) = jono pudotti
// kehyksen: se ei mene Processoriin eikä renkaaseen, ja seq hyppää (§4.9).
// Kirjoitettu erikseen recording.h:n feedFramesista, jotta vertailu testaa sen.
template <typename Drop>
DirectRun runDirect(const SimParams& sp, uint32_t n, const Config& cfg, const Calibration& cal,
                    uint32_t poll_ms, Drop drop) {
  DirectRun out;
  EngineSim sim;
  sim.configure(sp, cfg.sensor);
  std::vector<RawSample> pmem(rec::kRingCapacity);
  std::unique_ptr<Processor> p(new Processor(pmem.data(), pmem.size()));
  TEST_ASSERT_TRUE(p->configure(cfg));
  p->restoreCalibration(cal);
  std::vector<RawSample> rmem(n);
  RawRing ring(rmem.data(), rmem.size());
  uint32_t last_tick = 0;
  for (uint32_t k = 0; k < n; ++k) {
    const RawFrame f = sim.next();
    if (!drop(k)) {
      p->push(f);
      ring.push(f);
    }
    const uint32_t now_ms = k + 1u;
    if (now_ms % poll_ms == 0 && now_ms - last_tick >= 50u) {
      last_tick = now_ms;
      const float now = static_cast<float>(now_ms) / 1000.0f;
      p->tick(now);
      rec::TickRecord tr;
      tr.t = now;
      tr.st = p->status();
      tr.rpm = p->rpm();
      out.ticks.push_back(tr);
    }
  }
  out.csv = rec::ringToCsv(ring, cfg, cal);
  out.gaps = p->ring().gaps();
  return out;
}

rec::ReplayResult replayCsv(const std::string& csv, const rec::ReplayOptions& o) {
  rec::Recording r;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(rec::parseCsv(csv.data(), csv.size(), r, err), err.c_str());
  rec::ReplayResult res;
  TEST_ASSERT_TRUE_MESSAGE(rec::replayRecording(r, o, res, err), err.c_str());
  return res;
}

void assertSameCal(const Calibration& want, const Calibration& got) {
  for (int c = 0; c < kChannels; ++c) {
    TEST_ASSERT_TRUE(sameBits(want.offset_kpa[c], got.offset_kpa[c]));
    TEST_ASSERT_TRUE(sameBits(want.gain[c], got.gain[c]));
  }
  TEST_ASSERT_EQUAL_INT(want.zero_valid ? 1 : 0, got.zero_valid ? 1 : 0);
  TEST_ASSERT_EQUAL_INT(want.gain_valid ? 1 : 0, got.gain_valid ? 1 : 0);
}

Calibration zeroedCal() {
  Calibration c;
  c.zero_valid = true;
  return c;
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// 1. Golden-vektorit
// ---------------------------------------------------------------------------

// Hyväksyntä (iii): nauhoitus = CSV + .expect. Pari puuttuu -> testi kaatuu,
// jottei nauhoitus jää hiljaa tarkistamatta. Vähintään kolme synteettistä
// nauhoitusta (WP-8: synkassa, lähellä, letku irti), jotta väärä kansio ei
// mene läpi tyhjänä.
void test_every_recording_has_expectations() {
  const std::string dir = recordingsDir();
  TEST_ASSERT_TRUE_MESSAGE(!dir.empty(), "test/data/recordings ei löydy");
  const std::vector<fs::path> csvs = listByExt(dir, ".csv");
  const std::vector<fs::path> exps = listByExt(dir, ".expect");
  TEST_ASSERT_GREATER_OR_EQUAL(3, static_cast<long long>(csvs.size()));
  for (const fs::path& c : csvs) {
    fs::path e = c;
    e.replace_extension(".expect");
    TEST_ASSERT_TRUE_MESSAGE(fs::exists(e), ("odotustiedosto puuttuu: " + e.string()).c_str());
  }
  for (const fs::path& e : exps) {
    fs::path c = e;
    c.replace_extension(".csv");
    TEST_ASSERT_TRUE_MESSAGE(fs::exists(c), ("nauhoitus puuttuu: " + c.string()).c_str());
  }
}

// Jokainen nauhoitus odotuksiaan vasten (väitteiden päättäjät .expect-tiedostoissa).
// Kaikki nauhoitukset ajetaan, ja rikotut väitteet kootaan yhteen viestiin.
void test_recordings_match_expectations() {
  const std::string dir = recordingsDir();
  TEST_ASSERT_TRUE_MESSAGE(!dir.empty(), "test/data/recordings ei löydy");
  std::string report;
  int checked = 0;
  for (const fs::path& c : listByExt(dir, ".csv")) {
    fs::path e = c;
    e.replace_extension(".expect");
    const std::string name = c.filename().string();
    std::string text;
    std::string err;
    rec::Expect ex;
    rec::Recording r;
    rec::ReplayResult res;
    if (!readText(e, text)) {
      report += name + ": .expect ei lueta; ";
      continue;
    }
    if (!rec::parseExpect(text, ex, err)) {
      report += name + ": .expect: " + err + "; ";
      continue;
    }
    if (!rec::loadCsv(c.string(), r, err)) {
      report += name + ": " + err + "; ";
      continue;
    }
    if (!rec::replayRecording(r, ex.options, res, err)) {
      report += name + ": " + err + "; ";
      continue;
    }
    const rec::Summary s = rec::summarize(res.ticks);
    const std::string info = rec::detail::format(
        "%s: %lu tickiä, lukitus %.3f s, rpm %.1f, spread-mediaani %.3f kPa", name.c_str(),
        static_cast<unsigned long>(res.ticks.size()), static_cast<double>(s.first_lock_s),
        static_cast<double>(s.rpm_mean), static_cast<double>(s.spread_median));
    TEST_MESSAGE(info.c_str());
    for (const std::string& f : rec::checkExpect(ex, res.ticks)) report += name + ": " + f + "; ";
    ++checked;
  }
  TEST_ASSERT_TRUE_MESSAGE(report.empty(), report.c_str());
  TEST_ASSERT_GREATER_OR_EQUAL(3, checked);
}

// ---------------------------------------------------------------------------
// 2. Toiston oikeellisuus
// ---------------------------------------------------------------------------

// Hyväksyntä (i), test_integrationin ajoitus (Bench: tick joka 50. näyte,
// now = n / 1000): sama EngineSim-ajo suoraan ja CSV:n kautta -> identtinen
// tikkisarja. Todistaa, ettei CSV hukkaa mitään, mitä Processor käyttää (mV,
// seq, otsakkeen konfiguraatio ja kalibrointi).
void test_csv_path_identical_to_direct_bench_timing() {
  SimParams sp;
  sp.rpm = 900.0f;
  sp.sync_err_kpa[1] = 2.0f;
  sp.noise_mv = 4.0f;
  sp.seed = 11;
  const Config cfg;
  const Calibration cal = zeroedCal();
  const DirectRun d = runDirect(sp, 7000u, cfg, cal, 1u, [](uint32_t) { return false; });
  TEST_ASSERT_EQUAL_INT(140, static_cast<int>(d.ticks.size()));  // 7000 / 50
  TEST_ASSERT_GREATER_THAN(20, countVerdictTicks(d.ticks));      // vertailu ei ole triviaali
  rec::ReplayOptions o;
  o.timing.poll_ms = 1;
  o.timing.tick_ms = 50;
  const rec::ReplayResult r = replayCsv(d.csv, o);
  const std::string diff = firstDiff(d.ticks, r.ticks);
  TEST_ASSERT_TRUE_MESSAGE(diff.empty(), diff.c_str());
}

// Hyväksyntä (i), laitteen ajoitus (4 ms herätys, tick >= 50 ms -> 52 ms),
// pudonneet näytteet (jono täynnä, §4.9) ja kalibrointi, jonka otsake
// esittää tarkasti (4 ja 6 desimaalia, kahden potenssit).
void test_csv_path_identical_with_gaps_and_calibration() {
  SimParams sp;
  sp.rpm = 950.0f;
  sp.sync_err_kpa[2] = -1.0f;
  sp.noise_mv = 4.0f;
  sp.seed = 12;
  const Config cfg;
  Calibration cal;
  cal.offset_kpa[0] = 0.25f;
  cal.offset_kpa[1] = -0.5f;
  cal.offset_kpa[2] = 0.125f;
  cal.gain[0] = 1.015625f;
  cal.gain[1] = 0.984375f;
  cal.gain[2] = 1.0f;
  cal.zero_valid = true;
  cal.gain_valid = true;
  // Yksi 40 näytteen aukko ja yksittäisiä pudotuksia; ensimmäinen ja viimeinen
  // kehys mukana, jotta molempien polkujen kello alkaa ja päättyy samaan.
  auto drop = [](uint32_t k) { return (k >= 3000u && k < 3040u) || (k % 997u == 500u); };
  const DirectRun d = runDirect(sp, 7000u, cfg, cal, 4u, drop);
  TEST_ASSERT_EQUAL_INT(134, static_cast<int>(d.ticks.size()));  // 7000 / 52
  TEST_ASSERT_GREATER_THAN(20, countVerdictTicks(d.ticks));

  rec::Recording r;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(rec::parseCsv(d.csv.data(), d.csv.size(), r, err), err.c_str());
  // 40 näytteen aukko + 7 yksittäistä (500, 1497, 2494, 3491, 4488, 5485, 6482;
  // mikään ei osu aukkoon 3000-3039).
  TEST_ASSERT_EQUAL_INT(8, static_cast<int>(r.gaps));
  TEST_ASSERT_EQUAL_INT(47, static_cast<int>(r.missing));
  rec::ReplayResult res;
  TEST_ASSERT_TRUE_MESSAGE(rec::replayRecording(r, rec::ReplayOptions(), res, err), err.c_str());
  assertSameCal(cal, res.cal_start);              // otsake -> Processor ilman häviötä
  TEST_ASSERT_EQUAL_INT(static_cast<int>(d.gaps), static_cast<int>(res.ring_gaps));  // diag.gaps
  const std::string diff = firstDiff(d.ticks, res.ticks);
  TEST_ASSERT_TRUE_MESSAGE(diff.empty(), diff.c_str());
}

// Hyväksyntä (ii): sama nauhoitus kahdesti -> bitilleen sama tikkisarja.
void test_replay_is_deterministic() {
  const rec::Recording r = loadOrFail("sim_near_syl2.csv");
  rec::ReplayResult a;
  rec::ReplayResult b;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(rec::replayRecording(r, rec::ReplayOptions(), a, err), err.c_str());
  TEST_ASSERT_TRUE_MESSAGE(rec::replayRecording(r, rec::ReplayOptions(), b, err), err.c_str());
  TEST_ASSERT_GREATER_THAN(100, static_cast<long long>(a.ticks.size()));
  const std::string diff = firstDiff(a.ticks, b.ticks);
  TEST_ASSERT_TRUE_MESSAGE(diff.empty(), diff.c_str());
}

// Aukko kuten laitteella (proc_task.cpp, dsp.h): puuttuvia kehyksiä ei syötetä,
// mutta tickin kello etenee aukon yli 52 ms:n tahdissa, ja Processorin rengas
// laskee aukon (diag.gaps).
void test_gap_is_not_fed_but_clock_runs() {
  std::vector<RawFrame> frames;
  for (uint32_t s = 0; s < 2000u; ++s) {
    if (s >= 1000u && s < 1200u) continue;
    RawFrame f;
    f.seq = s;
    for (int c = 0; c < kChannels; ++c) f.mv[c] = 2700;
    frames.push_back(f);
  }
  std::vector<RawSample> mem(rec::kRingCapacity);
  std::unique_ptr<Processor> p(new Processor(mem.data(), mem.size()));
  std::vector<float> t;
  rec::feedFrames(*p, frames, rec::TickTiming(), [&](float now) { t.push_back(now); });
  TEST_ASSERT_EQUAL_INT(1800, static_cast<int>(p->ring().size()));  // vain oikeat kehykset
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(p->ring().gaps()));
  // Viimeinen kehys (seq 1999) viedään herätyksessä 2000 ms; tikit 52 ms:n
  // välein: 52, 104, ..., 1976 ms = 38 kpl, myös aukon 1000-1200 ms aikana.
  TEST_ASSERT_EQUAL_INT(38, static_cast<int>(t.size()));
  for (size_t i = 0; i < t.size(); ++i) {
    TEST_ASSERT_TRUE(sameBits(static_cast<float>(52u * (i + 1u)) / 1000.0f, t[i]));
  }
}

// D11: ilman tämän käynnistyksen nollausta ei koskaan vihreää, enintään
// keltainen + "kalibroimaton". Sama synkassa oleva nauhoitus kalibroimattomana
// (--no-cal): spread 0,7 kPa olisi vihreä (D2), mutta tila on near.
void test_no_cal_never_synced_D11() {
  const rec::Recording r = loadOrFail("sim_idle_synced.csv");
  rec::ReplayOptions o;
  o.no_cal = true;
  rec::ReplayResult res;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(rec::replayRecording(r, o, res, err), err.c_str());
  TEST_ASSERT_FALSE(res.cal_start.zero_valid);
  int verdicts = 0;
  for (const rec::TickRecord& t : res.ticks) {
    TEST_ASSERT_TRUE(t.st.state != SyncState::Synced);
    if (t.st.show_verdict) {
      ++verdicts;
      TEST_ASSERT_TRUE(t.st.flag_uncal_zero);
    }
  }
  TEST_ASSERT_GREATER_THAN(20, verdicts);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncState::Near), static_cast<int>(res.ticks.back().st.state));
}

// Otsakkeen konfiguraatio ja kalibrointi menevät Processoriin (WP-8:n
// tehtävä: toisto näkee saman kalibroinnin kuin laite). --set otsakkeen jälkeen;
// anturin siirtofunktion muutos mitätöi nollauksen kuten laitteella (D11,
// Processor::configure), toleranssin muutos ei.
void test_header_and_set_reach_processor() {
  SimParams sp;
  sp.seed = 5;
  Config cfg;
  cfg.sensor.v_zero_mv = 2690.0f;
  cfg.sensor.mv_per_kpa = 24.0f;
  Calibration cal;
  cal.offset_kpa[0] = 0.5f;
  cal.offset_kpa[1] = -0.25f;
  cal.offset_kpa[2] = 0.0625f;
  cal.zero_valid = true;
  std::string csv;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(rec::simToCsv(sp, cfg, cal, 300u, csv, err), err.c_str());

  const rec::ReplayResult a = replayCsv(csv, rec::ReplayOptions());
  TEST_ASSERT_EQUAL_FLOAT(2690.0f, a.cfg.sensor.v_zero_mv);
  TEST_ASSERT_EQUAL_FLOAT(24.0f, a.cfg.sensor.mv_per_kpa);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(sbx::SourceKind::Sim), static_cast<int>(a.cfg.source));
  assertSameCal(cal, a.cal_start);

  rec::ReplayOptions no_cal;
  no_cal.no_cal = true;
  assertSameCal(Calibration(), replayCsv(csv, no_cal).cal_start);

  rec::ReplayOptions tol;
  tol.set.push_back(rec::MetaEntry{"tol_green_kpa", "1.0"});
  const rec::ReplayResult b = replayCsv(csv, tol);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, b.cfg.status.tol_green_kpa);
  TEST_ASSERT_TRUE(b.cal_start.zero_valid);

  rec::ReplayOptions vz;
  vz.set.push_back(rec::MetaEntry{"v_zero_mv", "2700"});
  const rec::ReplayResult c = replayCsv(csv, vz);
  TEST_ASSERT_EQUAL_FLOAT(2700.0f, c.cfg.sensor.v_zero_mv);
  TEST_ASSERT_FALSE(c.cal_start.zero_valid);
}

// ---------------------------------------------------------------------------
// 3. Lukija (muodot: raw_ring.h CsvWriter, tools/serial_record.py write_csv;
// virheiden käsittely: docs/firmware.md §6.1, WP-8:n ehdotus)
// ---------------------------------------------------------------------------

// serial_record.py:n muoto: %g-luvut, t_ms = seq, recorded= ja stream_end=,
// ei first_seq:iä eikä kiskokenttiä, CRLF (Path.write_text Windowsilla).
void test_reader_serial_record_format() {
  const char* text =
      "# Suckboxx raakadata (sarjavirta, tools/serial_record.py)\r\n"
      "# device=suckboxx\r\n# format=1\r\n# source=adc\r\n# fs_hz=1000\r\n"
      "# v_zero_mv=2700\r\n# mv_per_kpa=25\r\n# zero_valid=1\r\n# gain_valid=0\r\n"
      "# offset_kpa=0.12,-0.08,0\r\n# gain=1,1,1\r\n# samples=5\r\n# gaps=1\r\n"
      "# recorded=2026-10-31T12:00:00\r\n# stream_end=end samples=5 dropped=0\r\n"
      "# kpa=gain*(mv-v_zero_mv)/mv_per_kpa+offset (ei kiskokorjausta)\r\n"
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa\r\n"
      "0,0,2695,2697,2700,-0.080,-0.200,0.000\r\n"
      "1,1,2696,2697,2701,-0.040,-0.200,0.040\r\n"
      "2,2,2694,2698,2699,-0.120,-0.160,-0.040\r\n"
      "4,4,2695,2697,2700,-0.080,-0.200,0.000\r\n"
      "5,5,2693,2696,2700,-0.160,-0.240,0.000\r\n";
  rec::Recording r;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(rec::parseCsv(text, std::strlen(text), r, err), err.c_str());
  TEST_ASSERT_EQUAL_INT(5, static_cast<int>(r.frames.size()));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(r.gaps));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(r.missing));
  TEST_ASSERT_EQUAL_UINT32(4u, r.frames[3].seq);
  TEST_ASSERT_EQUAL_INT(2694, r.frames[2].mv[0]);
  TEST_ASSERT_EQUAL_INT(2699, r.frames[2].mv[2]);
  TEST_ASSERT_EQUAL_INT(0, r.frames[2].rail_mv);
  TEST_ASSERT_FALSE(r.aborted);
  TEST_ASSERT_FALSE(r.truncated);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(r.warnings.size()));
  TEST_ASSERT_EQUAL_STRING("end samples=5 dropped=0", r.find("stream_end"));
  Config cfg;
  TEST_ASSERT_TRUE_MESSAGE(rec::headerConfig(r, cfg, err), err.c_str());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(sbx::SourceKind::Adc), static_cast<int>(cfg.source));
  Calibration cal;
  TEST_ASSERT_TRUE_MESSAGE(rec::headerCalibration(r, cal, err), err.c_str());
  TEST_ASSERT_EQUAL_FLOAT(0.12f, cal.offset_kpa[0]);
  TEST_ASSERT_EQUAL_FLOAT(-0.08f, cal.offset_kpa[1]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, cal.offset_kpa[2]);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, cal.gain[1]);
  TEST_ASSERT_TRUE(cal.zero_valid);
  TEST_ASSERT_FALSE(cal.gain_valid);
}

// /api/raw.csv, jonka rengas muuttui kesken latauksen (§4.5): CsvWriter
// päättää riviin "# aborted=ring_changed rows=K/N". Lukija ottaa K ehjää riviä.
void test_reader_device_aborted_export() {
  std::vector<RawSample> mem(64);
  RawRing ring(mem.data(), mem.size());
  EngineSim sim;
  for (int i = 0; i < 40; ++i) ring.push(sim.next());
  sbx::CsvWriter w(ring, Config(), Calibration());
  std::string csv;
  char buf[256];
  size_t n = 0;
  while (csv.size() < 1200u && (n = w.fill(buf, sizeof(buf))) > 0) csv.append(buf, n);
  ring.push(sim.next());  // rengas ei ollut jäädytetty
  while ((n = w.fill(buf, sizeof(buf))) > 0) csv.append(buf, n);
  TEST_ASSERT_TRUE(w.aborted());

  rec::Recording r;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(rec::parseCsv(csv.data(), csv.size(), r, err), err.c_str());
  TEST_ASSERT_TRUE(r.aborted);
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(r.frames.size()));
  TEST_ASSERT_LESS_THAN(40, static_cast<long long>(r.frames.size()));
  const std::string want = rec::detail::format("ring_changed rows=%lu/40",
                                               static_cast<unsigned long>(r.frames.size()));
  TEST_ASSERT_EQUAL_STRING(want.c_str(), r.find("aborted"));
  TEST_ASSERT_EQUAL_UINT32(0u, r.frames.front().seq);
}

// Katkennut lataus ilman aborted-riviä (yhteys poikki): ehjä tiedosto päättyy
// aina rivinvaihtoon, joten viimeinen rivi ilman sitä ohitetaan varoituksella.
void test_reader_drops_truncated_last_line() {
  std::string csv;
  std::string err;
  TEST_ASSERT_TRUE(rec::simToCsv(SimParams(), Config(), Calibration(), 10u, csv, err));
  csv.resize(csv.size() - 9u);  // viimeinen rivi poikki kPa-sarakkeiden kohdalta
  rec::Recording r;
  TEST_ASSERT_TRUE_MESSAGE(rec::parseCsv(csv.data(), csv.size(), r, err), err.c_str());
  TEST_ASSERT_TRUE(r.truncated);
  TEST_ASSERT_EQUAL_INT(9, static_cast<int>(r.frames.size()));
  TEST_ASSERT_GREATER_THAN(0, static_cast<long long>(r.warnings.size()));
}

// Rikkinäinen syöte hylätään eikä toistu hiljaa väärin.
void test_reader_rejects_broken_input() {
  const char* const kHead = "# fs_hz=1000\nseq,t_ms,ch1_mv,ch2_mv,ch3_mv\n";
  const char* const kBad[] = {
      "seq,ch1_mv,ch2_mv\n0,1,2\n",                              // sarake puuttuu
      "0,0,2700,2700,2700\n",                                     // ei sarakeotsaketta
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv\n",                          // ei näytteitä
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv\n5,5,1,1,1\n5,5,1,1,1\n",    // seq ei kasva
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv\n5,5,1,1,1\n4,4,1,1,1\n",    // seq taaksepäin
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv\n0,0,1,1,1\n70000,0,1,1,1\n",  // hyppy > 16 bittiä
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv\n0,0,1,x,1\n1,1,1,1,1\n",    // roskaa
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv\n0,0,1,65536,1\n1,1,1,1,1\n",  // mV > uint16
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv\n0,0,1,-1,1\n1,1,1,1,1\n",   // negatiivinen
      "seq,t_ms,ch1_mv,ch2_mv,ch3_mv\n0,0,1,1\n1,1,1,1,1\n",      // sarakkeita liian vähän
      "# fs_hz=500\nseq,t_ms,ch1_mv,ch2_mv,ch3_mv\n0,0,1,1,1\n",  // väärä näytetaajuus
      "# format=2\nseq,t_ms,ch1_mv,ch2_mv,ch3_mv\n0,0,1,1,1\n",   // tuntematon muoto
  };
  for (const char* bad : kBad) {
    rec::Recording r;
    std::string err;
    TEST_ASSERT_FALSE_MESSAGE(rec::parseCsv(bad, std::strlen(bad), r, err), bad);
    TEST_ASSERT_TRUE_MESSAGE(!err.empty(), bad);
  }
  // Vertailukohta: sama otsake ehjällä datalla kelpaa.
  const std::string ok = std::string(kHead) + "0,0,1,1,1\n1,1,1,1,1\n";
  rec::Recording r;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(rec::parseCsv(ok.data(), ok.size(), r, err), err.c_str());
  // Otsakkeen kalibrointiarvon on oltava kolme lukua.
  const std::string bad_cal = "# offset_kpa=0.1,0.2\n" + ok;
  TEST_ASSERT_TRUE(rec::parseCsv(bad_cal.data(), bad_cal.size(), r, err));
  Calibration cal;
  TEST_ASSERT_FALSE(rec::headerCalibration(r, cal, err));
}

// Odotustiedoston kirjoitusvirhe ei saa ohittaa väitettä hiljaa.
void test_expect_rejects_unknown_keys_and_values() {
  rec::Expect e;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(rec::parseExpect("from_s=lock  # kommentti\nfinal_state=near\n"
                                            "allowed_states=near,synced\nrpm=900\nrpm_tol=9\n"
                                            "set.tol_green_kpa=1.0\nno_cal=1\n",
                                            e, err),
                           err.c_str());
  TEST_ASSERT_TRUE(e.from_lock);
  TEST_ASSERT_TRUE(e.options.no_cal);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(e.options.set.size()));
  TEST_ASSERT_EQUAL_INT(2, static_cast<int>(e.allowed.size()));
  const char* const kBad[] = {
      "final_stat=near\n",        // tuntematon avain
      "final_state=green\n",      // tuntematon tila
      "rpm=900\n",                // rpm ilman toleranssia
      "flags_never=saturation\n", // tuntematon lippu
      "worst_ch=3\n",             // kanava 0..2
      "set.tol_gren_kpa=1\n",     // tuntematon konfiguraatioavain
      "lock_max_s=kuusi\n",       // ei luku
      "pelkkä rivi\n",            // ei avain=arvo
  };
  for (const char* bad : kBad) {
    TEST_ASSERT_FALSE_MESSAGE(rec::parseExpect(bad, e, err), bad);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_recording_has_expectations);
  RUN_TEST(test_recordings_match_expectations);
  RUN_TEST(test_csv_path_identical_to_direct_bench_timing);
  RUN_TEST(test_csv_path_identical_with_gaps_and_calibration);
  RUN_TEST(test_replay_is_deterministic);
  RUN_TEST(test_gap_is_not_fed_but_clock_runs);
  RUN_TEST(test_no_cal_never_synced_D11);
  RUN_TEST(test_header_and_set_reach_processor);
  RUN_TEST(test_reader_serial_record_format);
  RUN_TEST(test_reader_device_aborted_export);
  RUN_TEST(test_reader_drops_truncated_last_line);
  RUN_TEST(test_reader_rejects_broken_input);
  RUN_TEST(test_expect_rejects_unknown_keys_and_values);
  return UNITY_END();
}
