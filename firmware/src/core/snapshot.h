#pragma once
// Suckboxx core - snapshot-JSON SSE-tapahtumaan "snap" (WP-6, docs/firmware.md §5).
//
// Viitetoteutus on tools/mock_server.py:n Device.snapshot(), jota web/index.html
// käyttää PC:llä: samat avaimet samassa järjestyksessä, samat tyypit ja sama
// null-käytäntö. UI ei siis voi erottaa laitetta mockista muuten kuin
// luvuista.
//   - ch[i].mean/min/p2p/rpm = null, kun kanava ei pulssi (types.h: voimassa
//     vain kun has_pulse); d, level ja mv ovat aina lukuja.
//   - rpm/rpm_sd/rpm_cv = null, kun RpmEstimate ei ole voimassa.
//   - NaN/Inf -> null (JsonWriter), joten yksi huono luku ei hylkää koko
//     viestiä selaimen JSON.parsessa.
// Desimaalit kuten mockissa (round(v, 2) / 1 / 4), loppunollat pois.
// Luvut rajataan ±99999:ään. Fysikaalinen alue on −100…+10 kPa ja 0…12000
// rpm, joten raja ei osu oikeaan arvoon, mutta viestin pituus on rajattu
// rakenteellisesti eikä riipu siitä, mitä rikkinäinen lähde syöttää.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "json_writer.h"
#include "processor.h"
#include "status.h"
#include "types.h"

namespace sbx {

struct SnapshotMeta {
  uint32_t t_ms = 0;          // millis()
  const char* src = nullptr;  // "sim" | "adc" | "hx710b"; nullptr -> config().source
  float fs_hz = 0.0f;         // mitattu näytetaajuus (liima laskee)
  uint32_t heap = 0;          // vapaa heap (native: 0)
};

// Pahin tapaus on 1781 tavua, mitattu test_snapshot:ssa: kaikki luvut
// leveimmillään (±99998.1172), pisimmät tekstit (headline 79, advice 111,
// calResultText 149 tavua), 16 aaltoriviä ja kymmennumeroiset kokonaisluvut.
// Testi kaatuu, jos vara putoaa alle 128 tavun. Tavallinen viesti
// joutokäynnillä on noin 0,9 kt (4 aaltoriviä 15 Hz:llä).
constexpr size_t kSnapshotJsonSize = 2048;

// Snapshotin sisältö yhdeltä hetkeltä. snapshotJson kokoaa tämän
// Processorista; test_snapshot rakentaa pahimman tapauksen suoraan.
struct SnapshotView {
  const StatusResult* status = nullptr;
  const ChannelState* ch[kChannels] = {nullptr, nullptr, nullptr};
  RpmEstimate rpm;
  int master = 0;
  const Config* cfg = nullptr;
  const Calibration* cal = nullptr;
  const CalStatus* cal_status = nullptr;
  uint32_t gaps = 0;
  bool frozen = false;
  const float (*rows)[kChannels] = nullptr;
  int nrows = 0;
};

namespace snapshot_detail {

constexpr float kNumLimit = 99999.0f;

inline void num(JsonWriter& w, float v, int decimals) {
  if (!std::isfinite(v)) {
    w.null();
    return;
  }
  if (v > kNumLimit) v = kNumLimit;
  if (v < -kNumLimit) v = -kNumLimit;
  w.valueTrim(v, decimals);
}

// Arvo vain kun voimassa (mock: "… if has_pulse else None").
inline void numIf(JsonWriter& w, bool valid, float v, int decimals) {
  if (valid) {
    num(w, v, decimals);
  } else {
    w.null();
  }
}

inline const char* sourceKey(SourceKind s) {
  switch (s) {
    case SourceKind::Sim: return "sim";
    case SourceKind::Adc: return "adc";
    case SourceKind::Hx710b: return "hx710b";
  }
  return "sim";
}

}  // namespace snapshot_detail

// Kirjoittaa näkymän §5:n muodossa yhdelle riville (ei rivinvaihtoja, SSE:n
// data-kenttä). Puuttuva osa (nullptr) -> 0, eli ei koskaan puolikasta viestiä.
inline size_t snapshotWrite(const SnapshotView& v, const SnapshotMeta& m, char* buf, size_t n) {
  using snapshot_detail::num;
  using snapshot_detail::numIf;
  if (v.status == nullptr || v.cfg == nullptr || v.cal == nullptr || v.cal_status == nullptr) {
    return 0;
  }
  for (int i = 0; i < kChannels; ++i) {
    if (v.ch[i] == nullptr) return 0;
  }
  const StatusResult& s = *v.status;
  const RpmEstimate& e = v.rpm;

  JsonWriter w(buf, n);
  w.beginObject();
  w.key("v").value(1);
  w.key("t").value(static_cast<unsigned long>(m.t_ms));
  w.key("src").value(m.src != nullptr ? m.src : snapshot_detail::sourceKey(v.cfg->source));
  w.key("state").value(syncStateKey(s.state));
  w.key("fault_ch").value(static_cast<int>(s.fault_ch));
  w.key("bars").value(s.show_bars);
  w.key("verdict").value(s.show_verdict);
  w.key("head").value(s.headline);
  w.key("advice").value(s.advice);
  w.key("flags").beginObject();
  w.key("sat").value(s.flag_saturation);
  w.key("uncal_zero").value(s.flag_uncal_zero);
  w.key("uncal_gain").value(s.flag_uncal_gain);
  w.key("drift").value(s.flag_drift);
  w.endObject();
  w.key("master").value(v.master);
  w.key("tol").beginArray();
  num(w, v.cfg->status.tol_green_kpa, 3);
  num(w, v.cfg->status.tol_yellow_kpa, 3);
  w.endArray();
  w.key("rpm");
  numIf(w, e.valid, e.rpm, 1);
  w.key("rpm_sd");
  numIf(w, e.valid, e.sd_rpm, 1);
  w.key("rpm_cv");
  numIf(w, e.valid, e.cv, 4);
  w.key("spread");
  num(w, s.spread_kpa, 2);
  w.key("worst").value(static_cast<int>(s.worst_ch));

  w.key("ch").beginArray();
  for (int i = 0; i < kChannels; ++i) {
    const ChannelState& c = *v.ch[i];
    w.beginObject();
    w.key("d");
    num(w, s.vac_delta_kpa[i], 2);
    w.key("mean");
    numIf(w, c.has_pulse, c.mean_kpa, 2);
    w.key("level");
    num(w, c.level_kpa, 2);
    w.key("min");
    numIf(w, c.has_pulse, c.min_kpa, 2);
    w.key("p2p");
    numIf(w, c.has_pulse, c.p2p_kpa, 2);
    w.key("rpm");
    numIf(w, c.has_pulse, c.rpm, 1);
    w.key("mv").value(static_cast<unsigned>(c.last_mv));
    w.key("pulse").value(c.has_pulse);
    w.key("fault").value(c.sensor_fault);
    w.key("sat").value(c.saturated);
    w.endObject();
  }
  w.endArray();

  w.key("wave").beginObject();
  w.key("fs");
  num(w, Processor::kWaveFsHz, 1);
  w.key("s").beginArray();
  for (int r = 0; v.rows != nullptr && r < v.nrows; ++r) {
    w.beginArray();
    for (int c = 0; c < kChannels; ++c) num(w, v.rows[r][c], 2);
    w.endArray();
  }
  w.endArray();
  w.endObject();

  w.key("cal").beginObject();
  w.key("busy").value(v.cal_status->busy);
  w.key("zero").value(v.cal->zero_valid);
  w.key("gain").value(v.cal->gain_valid);
  w.key("msg").value(v.cal_status->msg);
  w.endObject();

  w.key("diag").beginObject();
  w.key("fs");
  num(w, m.fs_hz, 1);
  w.key("gaps").value(static_cast<unsigned long>(v.gaps));
  w.key("heap").value(static_cast<unsigned long>(m.heap));
  w.key("frozen").value(v.frozen);
  w.endObject();
  w.endObject();
  return w.ok() ? w.length() : 0;
}

// §4.8: §5:n snapshot Processorin viimeisimmästä tickistä. Tyhjentää
// aaltojonon (takeWave). Palauttaa pituuden ilman NULia, 0 = ei mahtunut
// (silloin jonon rivit on jo kulutettu; kSnapshotJsonSize riittää aina).
inline size_t snapshotJson(Processor& p, const SnapshotMeta& m, char* buf, size_t n) {
  float rows[Processor::kWaveRows][kChannels];
  SnapshotView v;
  v.nrows = p.takeWave(rows, Processor::kWaveRows);
  v.rows = rows;
  v.status = &p.status();
  for (int i = 0; i < kChannels; ++i) v.ch[i] = &p.channel(i);
  v.rpm = p.rpm();
  v.master = p.master();
  v.cfg = &p.config();
  v.cal = &p.calibration();
  v.cal_status = &p.calStatus();
  v.gaps = p.ring().gaps();
  v.frozen = p.ring().frozen();
  return snapshotWrite(v, m, buf, n);
}

}  // namespace sbx
