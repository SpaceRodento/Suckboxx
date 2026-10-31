#pragma once
// Suckboxx core - käytösmatriisi (WP-4, docs/firmware.md §4.7, SUUNNITTELU.md §8).
//
// Kokoaa DSP:n kanavatilat ja rpm-arvion yhdeksi näyttötilaksi ja yhdeksi
// suomenkieliseksi ohjeeksi. Tämä on se kohta, jossa "väärä vihreä" joko
// syntyy tai estetään, joten prioriteetti on kiinteä (korkein ensin):
//   1. FaultMaster  rivi 12: master täyttää rivin 6 tai 7 ehdon
//   2. FaultSensor  rivi 7
//   3. FaultHose    rivi 6
//   4. NoSignal     rivit 1+8 (D10)
//   5. Unstable     rivi 2 + D12 (rpm < sync_rpm_min, vakausajan odotus)
//   6. RpmTooHigh   rivi 9
//   7. Synced / Near / OutOfSync  rivit 3 / 4a / 4b
// §8:n suunnittelusääntö: vikatilat voittavat aina säätötilat. Laite ei
// koskaan näytä vihreää, jos yksikin kanava on epäluotettava - väärä
// "synkronoitu" on pahempi kuin ei tulosta lainkaan.
//
// Aika tulee syötteestä (now_s), joten kone on deterministinen ja
// testattavissa ilman kelloa. Ei muistinvarausta, ei poikkeuksia; tekstit
// kirjoitetaan StatusResultin kiinteisiin puskureihin.

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "calibration.h"
#include "types.h"

namespace sbx {

struct StatusInput {
  ChannelState ch[kChannels];
  RpmEstimate rpm;
  bool zero_valid = false;   // Calibration::zero_valid
  bool gain_valid = false;   // Calibration::gain_valid
  float now_s = 0.0f;        // monotoninen aika
};

// Snapshot-JSONin "state"-avain (docs/firmware.md §5).
inline const char* syncStateKey(SyncState s) {
  switch (s) {
    case SyncState::NoSignal: return "no_signal";
    case SyncState::Unstable: return "unstable";
    case SyncState::RpmTooHigh: return "rpm_too_high";
    case SyncState::Synced: return "synced";
    case SyncState::Near: return "near";
    case SyncState::OutOfSync: return "out_of_sync";
    case SyncState::FaultSensor: return "fault_sensor";
    case SyncState::FaultHose: return "fault_hose";
    case SyncState::FaultMaster: return "fault_master";
  }
  return "no_signal";
}

namespace status_detail {

// Lyhentää NUL-päätetyn merkkijonon niin, ettei viimeinen UTF-8-merkki jää
// kesken. snprintf katkaisee tavurajalla, ja puolikas "ä" tai "–" näkyisi
// selaimessa korvausmerkkinä tai rikkoisi JSON-validoinnin.
inline void utf8TrimTail(char* s) {
  size_t len = std::strlen(s);
  size_t cont = 0;
  while (len > 0 && cont < 3 &&
         (static_cast<unsigned char>(s[len - 1]) & 0xC0u) == 0x80u) {
    --len;
    ++cont;
  }
  if (len == 0) {
    if (cont > 0) s[0] = '\0';   // pelkkiä jatkotavuja: ei kelvollista merkkiä
    return;
  }
  const unsigned char lead = static_cast<unsigned char>(s[len - 1]);
  size_t need = 0;
  if ((lead & 0xE0u) == 0xC0u) need = 1;
  else if ((lead & 0xF0u) == 0xE0u) need = 2;
  else if ((lead & 0xF8u) == 0xF0u) need = 3;
  if (lead >= 0x80u && cont < need) s[len - 1] = '\0';
}

// snprintf kiinteään puskuriin + UTF-8-turvallinen katkaisu. Tekstit on
// mitoitettu mahtumaan (ks. testit äärimmäisillä luvuilla), joten katkaisu on
// vain varmistus.
__attribute__((format(printf, 3, 4)))
inline void writeText(char* dst, size_t n, const char* fmt, ...) {
  if (dst == nullptr || n == 0) return;
  va_list ap;
  va_start(ap, fmt);
  const int r = std::vsnprintf(dst, n, fmt, ap);
  va_end(ap);
  if (r < 0) {
    dst[0] = '\0';
  } else if (static_cast<size_t>(r) >= n) {
    utf8TrimTail(dst);
  }
}

// Yksi desimaali desimaalipilkulla ("2,1", "-55,0"). |v| rajataan 999,9:ään,
// jolloin pisin tulos on "-999,9" ja tekstien pituus on rajattu
// rakenteellisesti. Ei-äärellinen -> "?". "-0,0" -> "0,0".
inline void fmt1(char (&out)[8], float v) {
  if (!std::isfinite(v)) {
    std::memcpy(out, "?", 2);
    return;
  }
  if (v > 999.9f) v = 999.9f;
  if (v < -999.9f) v = -999.9f;
  int r = std::snprintf(out, sizeof(out), "%.1f", static_cast<double>(v));
  if (r < 0) r = 0;
  for (int i = 0; out[i] != '\0'; ++i) {
    if (out[i] == '.') out[i] = ',';
  }
  if (std::strcmp(out, "-0,0") == 0) std::memcpy(out, "0,0", 4);
}

// Kokonaisluvuksi pyöristetty rpm, rajattu 0..99999 (5 numeroa riittää
// sync_rpm_max:n 12000 rpm:n konfiguraatiorajalle).
// Kaikki arvot samaan suuntaan yli rajan k (yhteismuotoinen siirtymä).
inline bool allBeyond(const float (&v)[kChannels], float k) {
  bool up = true;
  bool down = true;
  for (int i = 0; i < kChannels; ++i) {
    if (!(v[i] > k)) up = false;
    if (!(v[i] < -k)) down = false;
  }
  return up || down;
}

inline int rpmInt(float rpm) {
  if (!std::isfinite(rpm) || rpm < 0.0f) return 0;
  if (rpm > 99999.0f) return 99999;
  return static_cast<int>(rpm + 0.5f);
}

}  // namespace status_detail

class StatusMachine {
 public:
  StatusMachine() { configure(StatusParams()); }

  // Nollaa ajastimet: uusi master tai uudet kynnykset = uusi vakausjakso
  // (D7: nollataso vaihtui, vanha vakaus ei kerro uudesta viitteestä mitään).
  void configure(const StatusParams& p) {
    p_ = p;
    master_ = p.master < kChannels ? p.master : 0;
    reset();
  }

  void reset() {
    have_t_ = false;
    last_t_ = 0.0f;
    stable_run_ = false;
    stable_since_ = 0.0f;
    sat_run_ = false;
    sat_since_ = 0.0f;
    cm_count_ = 0;
    for (int i = 0; i < kChannels; ++i) {
      lost_run_[i] = false;
      lost_since_[i] = 0.0f;
      // Varmistus: ensimmäinen päivitys tyhjentäisi muistin joka tapauksessa,
      // koska pitoaika (kHoseHoldS) alkaa alusta (mutaatiokierros WP-9).
      hose_latched_[i] = false;
    }
    zero_run_ = false;
    zero_since_ = 0.0f;
    zero_armed_ = true;   // käynnistys moottori sammuksissa -> nollaus (rivi 1)
    settle_.reset();
    drift_n_ = 0;
    drift_head_ = 0;
    drift_primed_ = false;
  }

  const StatusParams& params() const { return p_; }
  int master() const { return master_; }

  // Nollauksen vakausportti (calibration.h): sama ikkuna automaattiselle
  // (rivi 1) ja käsin tehdylle nollaukselle (Processor::zeroNow).
  const LevelSettle& settle() const { return settle_; }
  // DSP:n nollaus (kalibroinnin muutos) aloittaa tason alusta, joten vanha
  // historia näyttäisi hyppynä. Ei koske muihin ajastimiin: reset() virittäisi
  // nollauksen uudelleen kesken saman NoSignal-jakson.
  void resetSettle() { settle_.reset(); }

  // ~20 Hz (toimii 10-50 Hz:llä ja epätasaisella dt:llä).
  StatusResult update(const StatusInput& in) {
    using namespace status_detail;
    StatusResult r;

    // Ajastimet laskevat erotuksia, joten ajan on oltava monotoninen.
    // Taaksepäin hyppy = uusi alku; NaN = aika ei edennyt.
    float now = in.now_s;
    if (!std::isfinite(now)) now = have_t_ ? last_t_ : 0.0f;
    if (have_t_ && now < last_t_) reset();
    have_t_ = true;
    last_t_ = now;

    // Nollauksen vakausikkuna (rivi 1 ja Processor::zeroNow) kaikissa tiloissa.
    float levels[kChannels];
    for (int i = 0; i < kChannels; ++i) levels[i] = in.ch[i].level_kpa;
    settle_.add(levels, now);

    // Kanava pulssii vain, jos sen keskiarvo on myös käyttökelpoinen. NaN-
    // keskiarvo jäisi muuten spreadin min/max-vertailuista hiljaa pois ja
    // voisi päästää vihreän läpi.
    bool pulse[kChannels];
    int pulsing = 0;
    for (int i = 0; i < kChannels; ++i) {
      pulse[i] = in.ch[i].has_pulse && std::isfinite(in.ch[i].mean_kpa);
      if (pulse[i]) ++pulsing;
    }

    // --- kanavaluokitus --------------------------------------------------
    Cond cond[kChannels];
    for (int i = 0; i < kChannels; ++i) {
      const ChannelState& c = in.ch[i];
      const bool others = pulsing - (pulse[i] ? 1 : 0) > 0;
      // Pulssi puuttuu vain tältä kanavalta: ajastin sille, jäikö vika päälle.
      const bool lost = !c.sensor_fault && !pulse[i] && others;
      if (!lost) {
        lost_run_[i] = false;
      } else if (!lost_run_[i]) {
        lost_run_[i] = true;
        lost_since_[i] = now;
      }
      if (c.sensor_fault) {
        cond[i] = Cond::Sensor;                    // rivi 7 (DSP:n fault_hold_s)
      } else if (pulse[i]) {
        cond[i] = Cond::Ok;
      } else if (!others) {
        cond[i] = Cond::Idle;                      // rivit 1+8: kaikki hiljaa
      } else if (std::fabs(c.level_kpa) < hoseLimit(i) && now - lost_since_[i] >= kHoseHoldS) {
        // Rivi 6. Viive, koska käynnistyksessä ja sammutuksessa kanavat saavat
        // ja menettävät pulssin eri sykleillä: matalalla alipaineella (täysi
        // kaasu, -8 kPa) taso on jo hose_off_kpa:n alla, kun ensimmäinen
        // kanava menettää pulssin, ja VIKA välähti (sammutuksessa master
        // ensin -> FaultMaster). Oikealla irronneella letkulla taso-EMA
        // tarvitsee joka tapauksessa ~1,7 s, joten viive ei hidasta vikaa.
        // Poistuminen hystereesillä (hoseLimit, WP-9).
        cond[i] = Cond::HoseOff;
      } else if (now - lost_since_[i] >= p_.off_hold_s) {
        // Rivin 6 laajennus: ei pulssia, mutta taso ei ole nollassa. Tukossa
        // tai taittunut letku pitää alipaineen loukussa, ja kalibroimaton
        // anturi voi näyttää irti olevallakin letkulla yli hose_off_kpa:n
        // (±2 kPa, R4). Kun tila jatkuu off_hold_s, se on vika eikä
        // ohimenevä (ks. Lost).
        cond[i] = Cond::Blocked;
      } else {
        // Ohimenevä: letku juuri irrotettu (EMA-taso vielä laskussa) tai
        // kanavien pulssit alkavat/loppuvat eri syklillä käynnistyksessä
        // ja sammutuksessa. Ei vikaa heti, ettei VIKA välähdä turhaan.
        cond[i] = Cond::Lost;
      }
      // Hystereesin muisti: vain yhtäjaksoinen HoseOff pitää laajennetun rajan.
      hose_latched_[i] = cond[i] == Cond::HoseOff;
    }

    // --- tilariippumattomat liput ----------------------------------------
    // Rivi 10 koskee pulssin huippua. GN-anturi on ilmanpaineessa 2700 mV
    // eli aina >= sat_mv, joten ilman pulssia lippu varoittaisi
    // leikkautumisesta jokaisella sammutetulla moottorilla. DSP:n saturated
    // muistaa havainnon 1 s ja has_pulse viimeisen syklin 1 s, joten
    // käynnistyksessä ja sammutuksessa ne ovat päällekkäin alle 1 s
    // (ilmanpaine juuri ennen tai jälkeen pulssien). Aito leikkautuminen
    // toistuu joka syklillä, joten se jatkuu yli kSatHoldS:n.
    bool sat_now = false;
    for (int i = 0; i < kChannels; ++i) {
      if (in.ch[i].saturated && pulse[i]) sat_now = true;
    }
    if (!sat_now) {
      sat_run_ = false;
    } else if (!sat_run_) {
      sat_run_ = true;
      sat_since_ = now;
    }
    r.flag_saturation = sat_run_ && now - sat_since_ >= kSatHoldS;
    r.flag_uncal_zero = !in.zero_valid;   // D11
    r.flag_uncal_gain = !in.gain_valid;   // rivi 11
    r.flag_drift = updateDrift(in.rpm, now);   // rivi 5

    // --- alipaine-erot (D3) ---------------------------------------------
    // Lasketaan aina kun master on kelvollinen, kelvollisista kanavista;
    // muiden d = 0. Näyttääkö UI ne, kertovat show_bars/show_verdict.
    const int m = master_;
    if (cond[m] == Cond::Ok) {
      const float ref = in.ch[m].mean_kpa;
      float lo = 0.0f;
      float hi = 0.0f;
      float worst_abs = -1.0f;
      for (int i = 0; i < kChannels; ++i) {
        if (cond[i] != Cond::Ok || i == m) continue;
        // Positiivinen = kanava imee enemmän kuin master (läppä suljetumpi).
        const float d = ref - in.ch[i].mean_kpa;
        r.vac_delta_kpa[i] = d;
        if (d < lo) lo = d;
        if (d > hi) hi = d;
        if (std::fabs(d) > worst_abs) {
          worst_abs = std::fabs(d);
          r.worst_ch = static_cast<int8_t>(i);
        }
      }
      r.spread_kpa = hi - lo;   // masterin d = 0 on mukana (D2)
    }

    // --- tila prioriteettijärjestyksessä ---------------------------------
    int n_sensor = 0, n_hose = 0, n_blocked = 0, n_lost = 0;
    int first_sensor = -1, first_hose = -1, first_lost = -1;
    for (int i = 0; i < kChannels; ++i) {
      switch (cond[i]) {
        case Cond::Sensor:
          if (first_sensor < 0) first_sensor = i;
          ++n_sensor;
          break;
        case Cond::HoseOff:
        case Cond::Blocked:
          if (first_hose < 0) first_hose = i;
          if (cond[i] == Cond::HoseOff) ++n_hose; else ++n_blocked;
          break;
        case Cond::Lost:
          if (first_lost < 0) first_lost = i;
          ++n_lost;
          break;
        default:
          break;
      }
    }

    const RpmEstimate& e = in.rpm;
    // NaN-turvalliset vertailut: epäkelpo luku putoaa aina epävakaaksi.
    const bool cv_ok = e.valid && e.cv <= p_.unstable_cv;
    const bool rpm_low = !(e.rpm >= p_.sync_rpm_min);
    const bool rpm_high = e.rpm > p_.sync_rpm_max;
    const bool master_bad = cond[m] == Cond::Sensor || cond[m] == Cond::HoseOff ||
                            cond[m] == Cond::Blocked;

    // Vakausajastin (rivi 3, D12): kaikki kanavat pulssivat, ei vikoja, rpm
    // voimassa, hajonta kunnossa ja ikkunassa. Rivin 5 ajautuminen ei nollaa
    // sitä - yhteismuotoinen liike on jo poistettu erotuksesta (D3).
    const bool measurable = n_sensor == 0 && n_hose == 0 && n_blocked == 0 && n_lost == 0 &&
                            pulsing == kChannels && cv_ok && !rpm_low && !rpm_high;
    if (!measurable) {
      stable_run_ = false;
    } else if (!stable_run_) {
      stable_run_ = true;
      stable_since_ = now;
    }
    const float stable_for = stable_run_ ? now - stable_since_ : 0.0f;
    const bool stable = stable_run_ && stable_for >= p_.stable_hold_s;

    // Rivi 5, yhteismuotoinen alipaineen muutos (kaasu): kanavien
    // avg_cycles-keskiarvot päivittyvät eri hetkinä (sylinterit 240° välein),
    // joten X kPa:n muutos näkyy hetken spreadinä ~X / avg_cycles (20 kPa ->
    // 2,5 kPa = väärä säätöneuvo, vaikka mitään ei pidä kääntää). Taso-EMA
    // seuraa muutosta näytteittäin, joten kun se karkaa keskiarvosta kaikissa
    // kanavissa samaan suuntaan, keskiarvot ovat vielä kesken. Mitattu
    // 20 kPa:n askeleessa: 1,9 kPa jo 50 ms:ssa, kun spread on vasta 0,03.
    // Tasaisessa käynnissä ero <= 0,33 kPa (600-1500 rpm). Ruuvin säätö
    // liikuttaa yhtä kanavaa (tai kahta eri suuntiin), joten ehto ei täyty.
    // EMA ja liukuva keskiarvo lähestyvät uutta tasoa eri muotoisesti, ja
    // niiden ero käy nollassa juuri ennen kuin keskiarvot ovat perillä
    // (mitattu ~1 s:n kohdalla, spread yhä 1,7 kPa). Loppuosan kattaa
    // keskiarvojen oma muutos kCommonWindowS...2x sitten (mediaani), joka
    // kestää täsmälleen niin kauan kuin keskiarvot liikkuvat (taas kaikki
    // samaan suuntaan).
    // Vakausajastin ei nollaudu: tuomio palaa, kun keskiarvot ovat tasaantuneet.
    bool common_move = false;
    if (pulsing == kChannels) {
      float lag[kChannels];
      for (int i = 0; i < kChannels; ++i) lag[i] = in.ch[i].level_kpa - in.ch[i].mean_kpa;
      common_move = allBeyond(lag, kCommonMoveKpa);
      if (cm_count_ == 0 || now - cm_t_[1] >= kCommonWindowS) {
        cm_t_[0] = cm_t_[1];
        cm_t_[1] = now;
        for (int i = 0; i < kChannels; ++i) {
          cm_mean_[0][i] = cm_mean_[1][i];
          cm_mean_[1][i] = in.ch[i].mean_kpa;
        }
        if (cm_count_ < 2) ++cm_count_;
      }
      if (cm_count_ >= 2) {
        float moved[kChannels];
        for (int i = 0; i < kChannels; ++i) moved[i] = in.ch[i].mean_kpa - cm_mean_[0][i];
        if (allBeyond(moved, kCommonMoveKpa)) common_move = true;
      }
    } else {
      cm_count_ = 0;   // keskiarvot eivät ole vertailukelpoisia pulssikatkon yli
    }

    // Kanavakohtainen luokitus snapshotiin (ch[i].cond, WP-10). Sat vain, kun
    // rivin 10 varoitus on päällä (pitoaika) ja juuri tämä kanava leikkautuu.
    for (int i = 0; i < kChannels; ++i) {
      r.ch_cond[i] = publicCond(cond[i], r.flag_saturation && in.ch[i].saturated);
    }

    char a[8], b[8];   // fmt1-apupuskurit
    if (master_bad) {
      r.state = SyncState::FaultMaster;
      r.fault_ch = static_cast<int8_t>(m);
      // Erot masteriin ovat merkityksettömiä.
      clearDeltas(r);
      writeText(r.headline, sizeof(r.headline),
                "Master (SYL %d) ei kelpaa viitteeksi – valitse toinen", m + 1);
      if (cond[m] == Cond::Sensor) {
        writeText(r.advice, sizeof(r.advice), "SYL %d: anturivika (jännite %u mV)", m + 1,
                  static_cast<unsigned>(in.ch[m].last_mv));
      } else if (cond[m] == Cond::HoseOff) {
        writeText(r.advice, sizeof(r.advice), "SYL %d: ei alipainetta – tarkista letku", m + 1);
      } else {
        fmt1(a, in.ch[m].level_kpa);
        writeText(r.advice, sizeof(r.advice),
                  "SYL %d: ei pulssia (taso %s kPa) – tarkista letku", m + 1, a);
      }
    } else if (n_sensor > 0) {
      r.state = SyncState::FaultSensor;
      r.fault_ch = static_cast<int8_t>(first_sensor);
      char list[32];
      channelList(list, cond, Cond::Sensor, Cond::Sensor);
      writeText(r.headline, sizeof(r.headline), "VIKA: %s – anturivika", list);
      if (n_sensor == 1) {
        writeText(r.advice, sizeof(r.advice),
                  "Jännite %u mV – tarkista anturin johdot tai vaihda",
                  static_cast<unsigned>(in.ch[first_sensor].last_mv));
      } else {
        // Usea anturi yhtä aikaa viittaa yhteiseen syöttöön (R6:n LDO).
        writeText(r.advice, sizeof(r.advice),
                  "Useita anturivikoja – tarkista anturien käyttöjännite");
      }
    } else if (n_hose + n_blocked > 0) {
      r.state = SyncState::FaultHose;
      r.fault_ch = static_cast<int8_t>(first_hose);
      char list[32];
      channelList(list, cond, Cond::HoseOff, Cond::Blocked);
      const char* plural = n_hose + n_blocked > 1 ? "letkut" : "letku";
      if (n_blocked == 0) {
        writeText(r.headline, sizeof(r.headline), "VIKA: %s – ei alipainetta, tarkista %s", list,
                  plural);
        writeText(r.advice, sizeof(r.advice),
                  "Letku irti tai vuotaa – kytke ja tarkista liitokset");
      } else if (n_hose == 0) {
        writeText(r.headline, sizeof(r.headline), "VIKA: %s – ei pulssia, tarkista %s", list,
                  plural);
        fmt1(a, in.ch[first_hose].level_kpa);
        writeText(r.advice, sizeof(r.advice),
                  "Taso %s kPa ilman pulssia – letku irti, tukossa tai taittunut?", a);
      } else {
        writeText(r.headline, sizeof(r.headline), "VIKA: %s – tarkista letkut", list);
        writeText(r.advice, sizeof(r.advice),
                  "Letku irti, vuotaa tai tukossa – tarkista liitokset");
      }
    } else if (pulsing == 0) {
      r.state = SyncState::NoSignal;   // D10: sammuksissa ja kaikki letkut irti = sama tila
      clearDeltas(r);
      writeText(r.headline, sizeof(r.headline), "Ei alipainesignaalia");
      writeText(r.advice, sizeof(r.advice), "Moottori sammuksissa tai letkut irti");
    } else if (n_lost > 0 || !e.valid || !cv_ok || rpm_low) {
      r.state = SyncState::Unstable;
      writeText(r.headline, sizeof(r.headline), "Odota, käynti tasaantuu");
      if (n_lost > 0) {
        fmt1(a, in.ch[first_lost].level_kpa);
        writeText(r.advice, sizeof(r.advice), "SYL %d: ei pulssia (taso %s kPa)", first_lost + 1,
                  a);
      } else if (!e.valid) {
        writeText(r.advice, sizeof(r.advice), "Kierroslukua ei vielä tunnistettu");
      } else if (!cv_ok) {
        fmt1(a, e.cv * 100.0f);
        fmt1(b, p_.unstable_cv * 100.0f);
        writeText(r.advice, sizeof(r.advice), "Käynti epätasainen: hajonta %s %% (raja %s %%)", a,
                  b);
      } else {
        // floor/ceil: näytetty luku on aina rajan väärällä puolella
        // (499,9 rpm ei näy "500 rpm (raja 500)").
        writeText(r.advice, sizeof(r.advice), "Kierrosluku liian matala: %d rpm (raja %d)",
                  rpmInt(std::floor(e.rpm)), rpmInt(p_.sync_rpm_min));
      }
    } else if (rpm_high) {
      // Rivi 9: rpm näytetään, arvio piilotetaan. Ei vakausodotusta - piilotus
      // on turvallinen suunta.
      r.state = SyncState::RpmTooHigh;
      writeText(r.headline, sizeof(r.headline), "Säädä joutokäynnillä");
      writeText(r.advice, sizeof(r.advice), "Kierrosluku %d rpm (raja %d) – laske kaasua",
                rpmInt(std::ceil(e.rpm)), rpmInt(p_.sync_rpm_max));
    } else if (common_move) {
      r.state = SyncState::Unstable;
      writeText(r.headline, sizeof(r.headline), "Odota, käynti tasaantuu");
      writeText(r.advice, sizeof(r.advice), "Alipaine muuttuu kaikissa sylintereissä");
    } else if (!stable) {
      r.state = SyncState::Unstable;   // D12: vakaa >= stable_hold_s ennen tuomiota
      writeText(r.headline, sizeof(r.headline), "Odota, käynti tasaantuu");
      fmt1(a, stable_for);
      fmt1(b, p_.stable_hold_s);
      writeText(r.advice, sizeof(r.advice), "Vakautta odotetaan: %s / %s s", a, b);
    } else {
      verdict(r, in.zero_valid);
    }

    const bool verdict_state = r.state == SyncState::Synced || r.state == SyncState::Near ||
                               r.state == SyncState::OutOfSync;
    r.show_bars = verdict_state;
    r.show_verdict = verdict_state;

    // --- rivi 1: automaattinen nollaus -----------------------------------
    // Reunaliipattu: kerran per NoSignal-jakso, kun NoSignal on kestänyt
    // off_hold_s (rivi 1: ei pulssia >= 3 s) ja sillä hetkellä kaikki tasot
    // ovat nollan tuntumassa (off_kpa, D10) ja paikallaan (vakausportti
    // zero_settle_kpa / kZeroSettleWindowS, sama kuin käsin nollauksessa).
    // Vielä vuotava alipaine ei siis päädy offsetiksi. Uudelleenviritys vasta
    // kun pulssit palaavat.
    // WP-9: vakausehto kulkee off_hold_s:n rinnalla. WP-4:ssä off_hold_s
    // laskettiin vasta siitä, kun tasot olivat asettuneet 0,3 kPa:n kaistaan,
    // ja kaistan alku saattoi osua EMA:n hännälle: sammutuksesta -55 kPa:sta
    // nollaus 6,35-8,5 s (EngineSim, 8 siementä), nyt ~4,1 s (pulse_timeout_s
    // 1 s + off_hold_s 3 s, ja taso-EMA:n 1 s:n vaihtelu < 0,1 kPa 4,08 s:ssa).
    // NoSignal-ajastin alkaa alusta muissa tiloissa: nollaus vain NoSignalissa
    // (ei FaultSensor tms.: vikakanava ei näe luotettavasti ilmanpainetta,
    // vaikka sen taso olisi off_kpa:n sisällä; D10, rivi 7, §8).
    if (pulsing > 0) zero_armed_ = true;
    if (r.state != SyncState::NoSignal) {
      zero_run_ = false;
    } else if (!zero_run_) {
      zero_run_ = true;
      zero_since_ = now;
    }
    bool zero_ok = zero_run_ && now - zero_since_ >= p_.off_hold_s;
    for (int i = 0; i < kChannels && zero_ok; ++i) {
      zero_ok = std::fabs(in.ch[i].level_kpa) < p_.off_kpa &&
                settle_.range(i) < p_.zero_settle_kpa;
    }
    if (zero_ok && zero_armed_ && p_.auto_zero) {
      r.request_auto_zero = true;
      zero_armed_ = false;
    }
    return r;
  }

  // Rivi 6: kanava ilman pulssia (muiden pulssiessa) näin kauan ennen
  // letkuvikaa. Käynnistyksessä ja sammutuksessa kanavien ero on enintään
  // muutama sykli (< 0,3 s joutokäynnillä).
  static constexpr float kHoseHoldS = 0.5f;
  // Rivi 10: leikkautuminen pulssin aikana näin kauan ennen varoitusta
  // (DSP:n saturated- ja has_pulse-muistit ovat 1 s, ks. update()).
  static constexpr float kSatHoldS = 1.2f;
  // Rivi 5: taso - keskiarvo (tai keskiarvon muutos) kaikissa kanavissa
  // samaan suuntaan yli tämän = yhteismuotoinen muutos (ks. update()).
  // ~3 x tasaisen käynnin suurin arvo.
  static constexpr float kCommonMoveKpa = 1.0f;
  // Keskiarvojen muutoksen vertailuväli: verrataan 1-2 x tämän vanhaan
  // tilaan, jotta jokainen kanava on ehtinyt päivittyä (600 rpm = 0,2 s/sykli).
  static constexpr float kCommonWindowS = 0.3f;

  // Rivi 5: rpm-historia harvennetaan niin, että drift_window_s jakautuu
  // vähintään kDriftSlots väliin päivitystaajuudesta riippumatta (10-50 Hz,
  // epätasainen dt). Kapasiteetti kattaa >= (kDriftCap-1)/kDriftSlots ≈ 1,47
  // ikkunaa, joten kohta now - drift_window_s on aina historiassa.
  static constexpr int kDriftSlots = 32;
  static constexpr int kDriftCap = 48;

 private:
  enum class Cond : uint8_t { Ok, Idle, Sensor, HoseOff, Blocked, Lost };

  struct RpmPoint {
    float t;
    float rpm;
  };

  // Rivi 6, hystereesi (WP-9, jatko.md 7). Sisääntulo ennallaan: |taso| <
  // hose_off_kpa yhtäjaksoisesti kHoseHoldS. Kerran HoseOff-tilassa kanava
  // pysyy siinä, kunnes |taso| >= hose_off_kpa + hose_hyst_kpa (tai pulssi
  // palaa). Ilman tätä rajalla oleva taso (esim. irti oleva letku, jonka
  // anturin nolla on ryöminyt -2 kPa:iin, R5) vaihtoi tilaa HoseOff <-> Blocked
  // (otsikko "ei alipainetta" <-> "ei pulssia") ja off_hold_s:n sisällä
  // HoseOff <-> Lost (VIKA <-> "Odota"): EngineSimillä tasan rajalla 701
  // otsikon vaihtoa / 8 x 60 s (~1,5 Hz). Taso-EMA:n vaihteluväli 55 s:n aikana
  // 0,024 kPa (2 mV RMS) / 0,047 kPa (4 mV RMS, mitattu laitteella puhelin
  // yhdistettynä); oletus 0,2 kPa on > 4 x tämä ja kattaa ADC:n yksittäiset
  // ±90 mV:n piikit (EMA:ssa ~0,007 kPa / piikki). EHDOTUS, ei päätöslokissa.
  float hoseLimit(int i) const {
    return p_.hose_off_kpa + (hose_latched_[i] ? p_.hose_hyst_kpa : 0.0f);
  }

  static void clearDeltas(StatusResult& r) {
    for (int i = 0; i < kChannels; ++i) r.vac_delta_kpa[i] = 0.0f;
    r.spread_kpa = 0.0f;
    r.worst_ch = -1;
  }

  // Sisäinen luokitus -> ChannelCond (snapshotin ch[i].cond). Ei default-
  // haaraa: uusi Cond ilman vastinetta on -Wswitch-varoitus (-Werror).
  static ChannelCond publicCond(Cond c, bool sat) {
    switch (c) {
      case Cond::Ok: return sat ? ChannelCond::Sat : ChannelCond::Ok;
      case Cond::Idle: return ChannelCond::Idle;
      case Cond::Sensor: return ChannelCond::Sensor;
      case Cond::HoseOff: return ChannelCond::Hose;
      case Cond::Blocked: return ChannelCond::Blocked;
      case Cond::Lost: return ChannelCond::NoPulse;
    }
    return ChannelCond::Sensor;   // ei saavuteta; tuntematon ei ole koskaan "ok"
  }

  // "SYL 2", "SYL 2 ja SYL 3", "SYL 1, SYL 2 ja SYL 3" (pisin 21 tavua).
  static void channelList(char (&out)[32], const Cond (&cond)[kChannels], Cond c1, Cond c2) {
    int idx[kChannels];
    int n = 0;
    for (int i = 0; i < kChannels; ++i) {
      if (cond[i] == c1 || cond[i] == c2) idx[n++] = i;
    }
    // Numerot merkkeinä (%c): GCC ei näe, että idx < kChannels, ja varoittaisi
    // %d:n mahdollisesta katkeamisesta (-Wformat-truncation).
    char d[kChannels];
    for (int i = 0; i < n; ++i) d[i] = static_cast<char>('1' + idx[i]);
    out[0] = '\0';
    if (n == 1) {
      std::snprintf(out, sizeof(out), "SYL %c", d[0]);
    } else if (n == 2) {
      std::snprintf(out, sizeof(out), "SYL %c ja SYL %c", d[0], d[1]);
    } else if (n == 3) {
      std::snprintf(out, sizeof(out), "SYL %c, SYL %c ja SYL %c", d[0], d[1], d[2]);
    }
  }

  // Rivit 3 / 4a / 4b (D2) ja D11.
  void verdict(StatusResult& r, bool zero_valid) const {
    using namespace status_detail;
    char a[8], b[8];
    const float s = r.spread_kpa;
    // NaN-spread ei täytä kumpaakaan ehtoa -> OutOfSync, ei koskaan vihreä.
    if (s <= p_.tol_green_kpa) {
      if (zero_valid) {
        r.state = SyncState::Synced;
        writeText(r.headline, sizeof(r.headline), "Synkronoitu");
        fmt1(a, s);
        fmt1(b, p_.tol_green_kpa);
        writeText(r.advice, sizeof(r.advice), "Valmis: ero %s kPa (raja %s kPa)", a, b);
      } else {
        // D11: ilman tämän käynnistyksen nollausta ero voi olla pelkkää
        // anturin offsetia (±2 kPa, R4) -> enintään keltainen, ja ohje
        // kertoo miten nollaus saadaan.
        r.state = SyncState::Near;
        writeText(r.headline, sizeof(r.headline), "Lähellä – kalibroimaton");
        if (p_.auto_zero) {
          writeText(r.advice, sizeof(r.advice),
                    "Nollaus puuttuu – sammuta moottori hetkeksi, laite nollaa itse");
        } else {
          writeText(r.advice, sizeof(r.advice), "Nollaus puuttuu – irrota letkut ja tee nollaus");
        }
      }
      return;
    }
    r.state = s <= p_.tol_yellow_kpa ? SyncState::Near : SyncState::OutOfSync;
    const char* head = r.state == SyncState::Near ? "Lähellä" : "Ei synkronissa";
    writeText(r.headline, sizeof(r.headline), "%s%s", head, zero_valid ? "" : " – kalibroimaton");
    const int w = r.worst_ch;
    if (w < 0 || w >= kChannels) {
      fmt1(a, s);
      writeText(r.advice, sizeof(r.advice), "Ero %s kPa", a);
      return;
    }
    // Säätösuunta (D3, docs/firmware.md §4.7): d > 0 = imee enemmän kuin
    // master = läppä suljetumpi -> avaa. Ruuvin kiertosuunta riippuu
    // moottorista (D6), joten sitä ei kerrota.
    const float d = r.vac_delta_kpa[w];
    const bool more = d > 0.0f;
    fmt1(a, std::fabs(d));
    writeText(r.advice, sizeof(r.advice), "SYL %d: %s läppää – imee %s kPa %s kuin SYL %d", w + 1,
              more ? "avaa" : "sulje", a, more ? "enemmän" : "vähemmän", master_ + 1);
    // Sama neuvo rakenteisena (snapshotin act, WP-10): UI merkitsee kortin
    // tästä eikä jäsennä tekstiä. Ainoa paikka, jossa act asetetaan.
    r.act_ch = static_cast<int8_t>(w);
    r.act_open = more;
  }

  // Palauttaa flag_driftin: |y(now) - y(now - drift_window_s)| > drift_rpm,
  // jossa y on rpm-arvio suodatettuna aikavakiolla drift_tau_s (0 = y = rpm).
  // Menneisyyden y interpoloidaan lineaarisesti kahden tallennetun pisteen
  // välistä, joten tulos ei riipu siitä, osuuko jokin päivitys tarkalleen
  // ikkunan reunaan. Ennen kuin historiaa on koko ikkunan verran, lippu on
  // alhaalla (ei tietoa != ajautuu).
  //
  // Suodatus (WP-9, jatko.md 7): DSP:n rpm on avg_cycles (8) syklin
  // keskiarvo, joten sen kohina kasvaa kierrosluvun mukana (8 sykliä on
  // 3000 rpm:llä vain 0,32 s). Suodattamaton vertailu liputti EngineSimillä
  // (8 siementä x 60 s, osuus tickeistä): 1500 rpm / jitter 2-3 % 0,21 / 2,4 %,
  // 3000 rpm / 1-3 % 0,10 / 8,9 / 24,6 %, ja 3000 rpm:llä myös 20 rpm / 2 s
  // (8/8 siementä) - rivin 5 raja on > 50 rpm / 2 s, ja D12 sallii arvion
  // 3000 rpm:iin asti. Tasaisella rampilla suodatettu y on rpm - kulmakerroin
  // * tau, joten ikkunan erotus on tarkalleen sama kuin ilman suodatusta
  // (raja ei siirry). tau = 0,5 s: 1500 rpm 0 / 0,12 %, 3000 rpm 0 / 0,07 /
  // 1,7 %, 20 rpm / 2 s ei kertaakaan; 850 rpm:llä 0 % kuten ennenkin (5 %:n
  // jitterillä 1,8 % -> 0,2 %). Hinta: 60 rpm / 2 s tunnistuu 850 rpm:llä
  // keskimäärin 3,07 s:ssa (ennen 2,49 s), 3000 rpm:llä 2,38 s (1,71 s, mutta
  // liputettuna vain 75 % ajasta, nyt 94 %). EHDOTUS.
  // Diskretointi olettaa rpm:n lineaariseksi päivitysten välillä (first-order
  // hold), jolloin ramppi suodattuu tarkasti myös epätasaisella dt:llä.
  bool updateDrift(const RpmEstimate& e, float now) {
    const float win = p_.drift_window_s;
    if (!e.valid || !std::isfinite(e.rpm) || !(win > 0.0f)) {
      drift_n_ = 0;
      drift_head_ = 0;
      drift_primed_ = false;
      return false;
    }
    const float x = e.rpm;
    const float tau = p_.drift_tau_s;
    if (!drift_primed_ || !(tau > 0.0f)) {
      drift_y_ = x;
      drift_x_ = x;
      drift_tx_ = now;
      drift_primed_ = true;
    } else if (now > drift_tx_) {
      const float dt = now - drift_tx_;
      const float om = -std::expm1(-dt / tau);  // 1 - e^(-dt/tau) tarkasti pienelläkin dt:llä
      drift_y_ = x - (x - drift_x_) * (tau / dt) * om + (drift_y_ - drift_x_) * (1.0f - om);
      drift_x_ = x;
      drift_tx_ = now;
    }
    const float y = drift_y_;
    bool drift = false;
    const float target = now - win;
    int newer = -1;
    for (int k = 0; k < drift_n_; ++k) {
      const int idx = (drift_head_ - 1 - k + kDriftCap) % kDriftCap;
      const RpmPoint& pa = drift_[idx];
      if (pa.t <= target) {
        float then = pa.rpm;
        if (newer >= 0) {
          const RpmPoint& pb = drift_[newer];
          const float span = pb.t - pa.t;
          if (span > 0.0f) then = pa.rpm + (pb.rpm - pa.rpm) * ((target - pa.t) / span);
        }
        drift = std::fabs(y - then) > p_.drift_rpm;
        break;
      }
      newer = idx;
    }
    const float spacing = win / static_cast<float>(kDriftSlots);
    const int last = (drift_head_ - 1 + kDriftCap) % kDriftCap;
    if (drift_n_ == 0 || now - drift_[last].t >= spacing) {
      drift_[drift_head_].t = now;
      drift_[drift_head_].rpm = y;
      drift_head_ = (drift_head_ + 1) % kDriftCap;
      if (drift_n_ < kDriftCap) ++drift_n_;
    }
    return drift;
  }

  StatusParams p_;
  int master_ = 0;

  bool have_t_ = false;
  float last_t_ = 0.0f;

  bool stable_run_ = false;
  float stable_since_ = 0.0f;
  bool sat_run_ = false;     // rivi 10: leikkautuminen pulssin aikana jatkuu
  float sat_since_ = 0.0f;
  // Rivi 5: keskiarvot kahdelta viimeisimmältä kCommonWindowS-rajalta, [0] vanhempi.
  int cm_count_ = 0;
  float cm_t_[2] = {0.0f, 0.0f};
  float cm_mean_[2][kChannels] = {};

  bool lost_run_[kChannels] = {false, false, false};
  float lost_since_[kChannels] = {0.0f, 0.0f, 0.0f};
  bool hose_latched_[kChannels] = {false, false, false};   // rivi 6: HoseOff edellisellä kierroksella

  bool zero_run_ = false;     // NoSignal jatkunut zero_since_:sta
  bool zero_armed_ = true;
  float zero_since_ = 0.0f;
  LevelSettle settle_;

  RpmPoint drift_[kDriftCap] = {};   // rpm = suodatettu y
  int drift_n_ = 0;
  int drift_head_ = 0;
  bool drift_primed_ = false;  // suodattimen tila: y, edellinen rpm ja sen hetki
  float drift_y_ = 0.0f;
  float drift_x_ = 0.0f;
  float drift_tx_ = 0.0f;
};

}  // namespace sbx
