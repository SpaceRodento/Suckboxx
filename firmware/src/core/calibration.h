#pragma once
// Suckboxx core - mV -> kPa -muunnos sekä nolla- ja vahvistuskalibrointi
// (WP-3, R4, docs/firmware.md §4.3, docs/rauta.md §7).
//
// R4: kanavien KESKINÄINEN tarkkuus on koko tuote. Anturin oma ±2 kPa
// tarkoittaa, että kalibroimaton laite voisi näyttää 4 kPa eroa täysin
// synkronoidulla moottorilla. Kaksipistekalibrointi:
//   - nolla: kaikki kanavat ilmanpaineessa (moottori seis, rivi 1 / D10)
//     -> kanavakohtainen offset. Ajetaan automaattisesti joka käynnistyksellä.
//   - vahvistus: kaikki kolme letkua samassa alipaineessa T-haaralla
//     -> kanavakohtainen gain, joka saa kanavat lukemaan samaa. Huoltotoimi.
//
// Malli: P = gain * P_nominal + offset, jossa P_nominal tulee datasheetin
// siirtofunktiosta. Vahvistus on suhteellinen kanavien keskiarvoon - absoluuttista
// referenssiä laiturilla ei ole, eikä sitä tarvita (D3: vain erotus ratkaisee).

#include <cmath>
#include <cstdint>
#include <limits>

#include "types.h"

namespace sbx {

// Kiskokorjauksen järkevyysikkuna (R6). Anturi on ratiometrinen, joten oikea
// kiskon notkahdus korjataan skaalaamalla. Yli 20 %:n poikkeama ei kuitenkaan ole
// kiskon notkahdus vaan rikki mennyt kiskomonitori (GPIO39-jakaja irti tai
// oikosulussa) - silloin "korjaus" pilaisi kaikki kanavat, joten se jätetään pois.
constexpr float kRailPlausibleFrac = 0.20f;

// mV ADC-pinnissä -> nimellinen kPa (gauge). Datasheet: P = (V - V0) / k,
// 3,3 V GN: V0 = 2700 mV, k = 25 mV/kPa (docs/rauta.md §1.2).
// rail_comp: ulostulo skaalautuu syöttöjännitteen mukana, joten mitattu mV
// palautetaan nimelliskiskolle kertoimella rail_nominal / rail ennen muunnosta.
// 50 mV:n notkahdus 3,3 V:n kiskossa olisi muuten ~1,6–2 kPa virhe (R6).
// mv_per_kpa > 0 on configin alueen takaama (min 5).
inline float nominalKpa(uint16_t mv, uint16_t rail_mv, const SensorParams& sp) {
  float m = static_cast<float>(mv);
  if (sp.rail_comp && rail_mv > 0) {
    const float rail = static_cast<float>(rail_mv);
    if (std::fabs(rail - sp.rail_nominal_mv) <= kRailPlausibleFrac * sp.rail_nominal_mv) {
      m *= sp.rail_nominal_mv / rail;
    }
  }
  return (m - sp.v_zero_mv) / sp.mv_per_kpa;
}

// Nimellinen -> kalibroitu. Tuntematon kanava palauttaa nimellisen sellaisenaan.
inline float applyCal(float nominal_kpa, int ch, const Calibration& cal) {
  if (ch < 0 || ch >= kChannels) return nominal_kpa;
  return cal.gain[ch] * nominal_kpa + cal.offset_kpa[ch];
}

// NoZero ja NotSettled (WP-9) on lisätty sopimuksen (docs/firmware.md §4.3)
// loppuun, joten muiden arvojen numerot eivät muutu.
enum class CalResult : uint8_t {
  Ok,
  NotAtmosphere,
  NotStable,
  TooSmall,
  Mismatch,
  GainOutOfRange,
  NoZero,
  NotSettled,  // nollaus: taso liikkuu vielä (vuotava alipaine) tai historiaa < 1 s
};

// Vahvistuskalibroinnin hyväksymisrajat (docs/firmware.md §4.3).
//  - |referenssi| ≥ 15 kPa: gain-virhe näkyy suhteessa paineeseen. 15 kPa:ssa
//    0,5 kPa:n mittausepävarmuus on 3 % gainista, ja D11:n mukaan 1 % gain-ero
//    joutokäynnin −55 kPa:ssa on jo 0,55 kPa - pienempi paine ei kerro gainista
//    mitään luotettavaa. Käsipumpulla 40–60 kPa on helppo.
//  - sd < 0,5 kPa: kolmannes D2:n vihreästä rajasta. Suurempi hajonta 2 s:n
//    keskiarvon aikana = pumppu vuotaa tai painetta säädetään vielä.
//  - kanavat 15 %:n sisällä TOISISTAAN: suurimman ja pienimmän nollakorjatun
//    lukeman ero ≤ 15 % pienimmästä itseisarvosta. XGZP:n ±2 %FSS (= ±2 kPa
//    @ 100 kPa) ja ADC:n kanavaero jäävät 50 kPa:ssa reilusti alle 10 %:n.
//    Suurempi ero tarkoittaa, että letkut eivät ole samassa paineessa (vuoto,
//    letku irti T:stä). Pareittainen ehto eikä "kukin keskiarvosta": jälkimmäinen
//    päästäisi läpi yhden kanavan ~24 %:n päässä kahdesta muusta, ja vuotava
//    letku kalibroituisi muiden kanavien virheeksi (synkronoitu moottori näyttäisi
//    punaista, ja mekaanikko kääntäisi jo oikeaa ruuvia).
//  - gain 0,85…1,15: puolustava raja. Pareittaisen 15 %:n ehdon jälkeen
//    referenssi (keskiarvo) on aina 1,05…1,10 × pienin lukema, joten gain on
//    0,913…1,10 eikä tämä raja nykyisillä vakioilla voi laueta. Se pitää
//    tuloksen järkevänä, jos Mismatch-rajaa joskus löysätään, ja on sopimuksen
//    (§4.3) mukainen.
constexpr float kGainMinRefKpa = 15.0f;
constexpr float kGainMaxSdKpa = 0.5f;
constexpr float kGainMaxMismatch = 0.15f;
constexpr float kGainMin = 0.85f;
constexpr float kGainMax = 1.15f;

// Nollapiste: kanavat ilmanpaineessa -> offset niin että kalibroitu = 0.
// Säilyttää gainin. max_abs_kpa on se, mikä vielä voi olla ilmanpainetta
// (Processor antaa off_kpa = 5 kPa: anturin ±2 kPa + marginaali, D10).
// Hylkää, jos jokin kanava on selvästi alipaineessa - silloin moottori käy tai
// letkussa on alipainetta, ja nollaus siirtäisi kanavan väärään kohtaan.
inline CalResult computeZero(const float (&nominal_level)[kChannels], float max_abs_kpa,
                             Calibration& cal) {
  for (int i = 0; i < kChannels; ++i) {
    // !(<=) hylkää myös NaN:n.
    if (!(std::fabs(nominal_level[i]) <= max_abs_kpa)) return CalResult::NotAtmosphere;
  }
  for (int i = 0; i < kChannels; ++i) {
    cal.offset_kpa[i] = -cal.gain[i] * nominal_level[i];
  }
  cal.zero_valid = true;
  return CalResult::Ok;
}

// Nollauksen vakausportti (WP-9, jatko.md 8): ilmanpaine-ehdon lisäksi tason on
// pysyttävä paikallaan. Juuri sammutetun moottorin alipaine jää loukkuun ja
// purkautuu letkuista sekunteja, ja taso-EMA (level_tau_s) laskee sen perässä;
// |taso| < off_kpa täyttyy jo ~1,2 s sammutuksesta (-55 kPa, tau 0,5 s), jolloin
// käsin nollaus kirjasi vielä laskevan tason offsetiksi (mitattu EngineSimillä:
// 1,5 s:n kohdalla 2,8 kPa, 2,0 s:n kohdalla 1,0 kPa, vrt. D2:n 1,5 kPa).
// level_range[i] = kanavan tason vaihteluväli (max - min) viimeisen
// kZeroSettleWindowS:n aikana samalla asteikolla kuin max_range_kpa
// (Processor: kalibroitu taso, kuten StatusMachinen automaattisessa nollauksessa).
// Tarkistusjärjestys: NotAtmosphere, NotSettled - perustavin puute ensin.
// !(<) hylkää myös NaN:n ja äärettömän (ei historiaa).
inline CalResult computeZero(const float (&nominal_level)[kChannels],
                             const float (&level_range)[kChannels], float max_abs_kpa,
                             float max_range_kpa, Calibration& cal) {
  for (int i = 0; i < kChannels; ++i) {
    if (!(std::fabs(nominal_level[i]) <= max_abs_kpa)) return CalResult::NotAtmosphere;
  }
  for (int i = 0; i < kChannels; ++i) {
    if (!(level_range[i] < max_range_kpa)) return CalResult::NotSettled;
  }
  return computeZero(nominal_level, max_abs_kpa, cal);
}

// Vakausikkuna: 1 s. EHDOTUS (WP-9), ei päätöslokissa. Raja zero_settle_kpa
// (oletus 0,1 kPa, S5a) on vaihteluväli tämän ikkunan aikana, eli tason
// liike enintään ~0,1 kPa/s - sama kulmakerroin kuin WP-4:n kaistassa
// (0,3 kPa / 3 s). Lyhyempi ikkuna kuin off_hold_s, jotta vakausehto kulkee
// rivin 1 pitoajan (off_hold_s) rinnalla eikä sen perässä (jatko.md 8: nollaus
// 5,6-8 s sammutuksesta). Kohina ei laukaise porttia: taso-EMA:n 1 s:n
// vaihteluväli EngineSimillä (8 siementä x 56 s) enintään 0,023 kPa 2 mV:n,
// 0,039 kPa 4 mV:n (laitteella mitattu, puhelin yhdistettynä, jatko.md) ja
// 0,084 kPa 8 mV:n RMS-kohinalla (test_integration:
// test_wp9_settle_gate_ignores_adc_noise). Marginaali 4 mV:lla 2,5-kertainen.
constexpr float kZeroSettleWindowS = 1.0f;

// Kanavien tasojen vaihteluväli liukuvassa kZeroSettleWindowS:n ikkunassa
// (~20 Hz:n päivityksistä; 10-50 Hz ja epätasainen dt käyvät). Päivitykset
// kootaan lokeroihin, joiden alkujen väli on vähintään ikkuna / kSlots, joten
// jokainen näyte on mukana min/max:ssa nopeudesta riippumatta ja kapasiteetti
// kattaa >= (kCap - 1) / kSlots ≈ 1,47 ikkunaa. Ei muistinvarausta.
class LevelSettle {
 public:
  static constexpr int kSlots = 32;
  static constexpr int kCap = 48;

  void reset() {
    n_ = 0;
    head_ = 0;
  }

  // now monotoninen; kutsuja nollaa taaksepäin hypyssä (StatusMachine::update).
  void add(const float (&level)[kChannels], float now) {
    const float spacing = kZeroSettleWindowS / static_cast<float>(kSlots);
    const int last = (head_ - 1 + kCap) % kCap;
    if (n_ == 0 || now - slot_[last].t >= spacing) {
      Slot& s = slot_[head_];
      s.t = now;
      s.bad = false;
      for (int i = 0; i < kChannels; ++i) {
        s.lo[i] = level[i];
        s.hi[i] = level[i];
        if (!std::isfinite(level[i])) s.bad = true;
      }
      head_ = (head_ + 1) % kCap;
      if (n_ < kCap) ++n_;
    } else {
      Slot& s = slot_[last];
      for (int i = 0; i < kChannels; ++i) {
        if (!std::isfinite(level[i])) s.bad = true;
        if (level[i] < s.lo[i]) s.lo[i] = level[i];
        if (level[i] > s.hi[i]) s.hi[i] = level[i];
      }
    }
    now_ = now;
  }

  // Historiaa koko ikkunan verran: vanhin lokero alkoi viimeistään ikkunan alussa.
  bool full() const {
    if (n_ == 0) return false;
    const int oldest = (head_ - n_ + kCap) % kCap;
    return now_ - slot_[oldest].t >= kZeroSettleWindowS;
  }

  // max - min lokeroista, jotka alkoivat ikkunan sisällä (viimeisin add).
  // Ääretön, jos historiaa ei ole koko ikkunaa tai jokin taso oli NaN: vakautta
  // ei ole osoitettu.
  float range(int ch) const {
    const float inf = std::numeric_limits<float>::infinity();
    if (ch < 0 || ch >= kChannels || !full()) return inf;
    float lo = inf;
    float hi = -inf;
    for (int k = 0; k < n_; ++k) {
      const Slot& s = slot_[(head_ - 1 - k + kCap) % kCap];
      if (now_ - s.t > kZeroSettleWindowS) break;
      if (s.bad) return inf;
      if (s.lo[ch] < lo) lo = s.lo[ch];
      if (s.hi[ch] > hi) hi = s.hi[ch];
    }
    return hi - lo;
  }

 private:
  struct Slot {
    float t;
    float lo[kChannels];
    float hi[kChannels];
    bool bad;
  };
  Slot slot_[kCap] = {};
  int n_ = 0;
  int head_ = 0;
  float now_ = 0.0f;
};

// Vahvistus: kaikki kanavat samassa alipaineessa (T-haara) -> gain niin, että
// kanavat täsmäävät toisiinsa. Referenssi = kanavien nollakorjattujen
// nimellislukemien keskiarvo, jolloin kalibrointi ei siirrä yhteistä
// skaalaa (sen tarkkuus on datasheetin varassa, eikä sillä ole väliä, R4).
// Säilyttää nollapisteen: ilmanpaine kuvautuu edelleen nollaan.
// Hylätty kalibrointi ei muuta cal:ia.
//
// Vaatii tämän käynnistyksen nollauksen (cal.zero_valid, D11). Ilman sitä
// anturien omat offsetit (±2 kPa, R4) menisivät gainiin: −50 kPa:ssa 2 kPa on
// 4 % gainia, ja gain_valid tallentuu NVS:ään pysyvästi. Seuraavan nollauksen
// jälkeen synkronoitu moottori näyttäisi −55 kPa:ssa >2 kPa eroa eikä koskaan
// vihreää. Tarkistusjärjestys: NoZero, TooSmall, NotStable, Mismatch,
// GainOutOfRange - perustavin puute ensin.
//
// Laskenta: kanavan nollapiste nimellisasteikolla z = -offset / gain, ja
// nollakorjattu lukema c = n - z. Kalibroitu lukema on gain * c, joten uusi
// gain = ref / c ja uusi offset = -gain * z.
inline CalResult computeGain(const float (&nominal_mean)[kChannels],
                             const float (&nominal_sd)[kChannels], Calibration& cal) {
  if (!cal.zero_valid) return CalResult::NoZero;
  float z[kChannels];
  float c[kChannels];
  float ref = 0.0f;
  for (int i = 0; i < kChannels; ++i) {
    // Rikkinäinen (NVS:stä ladattu) gain ei saa tuottaa nollalla jakoa.
    const float g = (cal.gain[i] > 0.0f && std::isfinite(cal.gain[i])) ? cal.gain[i] : 1.0f;
    z[i] = -cal.offset_kpa[i] / g;
    c[i] = nominal_mean[i] - z[i];
    ref += c[i];
  }
  ref /= static_cast<float>(kChannels);

  if (!(std::fabs(ref) >= kGainMinRefKpa)) return CalResult::TooSmall;
  for (int i = 0; i < kChannels; ++i) {
    if (!(nominal_sd[i] < kGainMaxSdKpa)) return CalResult::NotStable;
  }
  // Pareittainen ehto: max − min ≤ 15 % · min |c|. Eri etumerkit tai nolla
  // jossain kanavassa (letku irti T:stä) hylkääntyvät tässä aina, koska silloin
  // ero ≥ |c_i| > 15 % · min |c|.
  float c_lo = c[0];
  float c_hi = c[0];
  float mag_min = std::fabs(c[0]);
  for (int i = 1; i < kChannels; ++i) {
    c_lo = c[i] < c_lo ? c[i] : c_lo;
    c_hi = c[i] > c_hi ? c[i] : c_hi;
    const float mag = std::fabs(c[i]);
    mag_min = mag < mag_min ? mag : mag_min;
  }
  if (!(c_hi - c_lo <= kGainMaxMismatch * mag_min)) return CalResult::Mismatch;
  float g_new[kChannels];
  for (int i = 0; i < kChannels; ++i) {
    g_new[i] = ref / c[i];  // c[i] ≠ 0: Mismatch-ehto hylkää nollan (ks. yllä)
    if (!(g_new[i] >= kGainMin && g_new[i] <= kGainMax)) return CalResult::GainOutOfRange;
  }
  for (int i = 0; i < kChannels; ++i) {
    cal.gain[i] = g_new[i];
    cal.offset_kpa[i] = -g_new[i] * z[i];
  }
  cal.gain_valid = true;
  return CalResult::Ok;
}

// Suomeksi UI:hin: mitä tapahtui ja mitä käyttäjän pitää tarkistaa.
inline const char* calResultText(CalResult r) {
  switch (r) {
    case CalResult::Ok:
      return "Kalibrointi onnistui";
    // Ei pelkkää "alipainetta": myös väärä v_zero_mv tai ylös leikkaava anturi
    // näyttää ilmanpaineessa ±5 kPa:n ulkopuolelle, eikä moottorin sammutus auta.
    case CalResult::NotAtmosphere:
      return "Nollaus hylätty: kanava ei näytä ilmanpainetta. Sammuta moottori ja päästä "
             "alipaine letkuista; jos se ei auta, tarkista v_zero_mv ja mv_per_kpa";
    case CalResult::NotStable:
      return "Paine vaihteli mittauksen aikana. Tarkista ettei pumppu tai T-haara vuoda ja "
             "pidä alipaine tasaisena";
    case CalResult::TooSmall:
      return "Alipaine liian pieni. Pumppaa vähintään 15 kPa (mieluiten 40–60 kPa) ja "
             "yritä uudelleen";
    case CalResult::Mismatch:
      return "Kanavat näkevät eri paineen. Tarkista että kaikki kolme letkua ovat samassa "
             "T-haarassa eikä mikään vuoda";
    case CalResult::GainOutOfRange:
      return "Anturin herkkyys poikkeaa yli 15 %. Tarkista anturityyppi (GN/GPN, 3,3 V / 5 V) "
             "ja anturiasetukset v_zero_mv ja mv_per_kpa";
    case CalResult::NoZero:
      return "Nollaus puuttuu. Päästä alipaine pois (letkut ilmanpaineeseen), tee vaihe 1 "
             "Nollaa ja toista sitten vahvistuskalibrointi";
    // Ei "vika": tavallisin syy on juuri sammutettu moottori, ja odotus riittää.
    case CalResult::NotSettled:
      return "Nollaus hylätty: lukemat liikkuvat vielä (alipaine purkautuu letkuista). "
             "Odota, kunnes lukemat pysyvät paikallaan, ja yritä uudelleen";
  }
  return "Tuntematon kalibrointitulos";
}

// Keskiarvo + keskihajonta -kerääjä vahvistuskalibrointia varten (2 s × 1 kHz).
// Welfordin algoritmi: suora summa ja neliösumma floatina kumoaisi itsensä
// (−50 kPa:n neliö 2000 kertaa = 5e6, floatin tarkkuus siinä ~0,5), Welford
// kerää vain poikkeamia keskiarvosta.
class CalCapture {
 public:
  void reset() {
    n_ = 0;
    for (int i = 0; i < kChannels; ++i) {
      mean_[i] = 0.0f;
      m2_[i] = 0.0f;
    }
  }

  void add(const float (&nominal)[kChannels]) {
    ++n_;
    const float inv_n = 1.0f / static_cast<float>(n_);
    for (int i = 0; i < kChannels; ++i) {
      const float delta = nominal[i] - mean_[i];
      mean_[i] += delta * inv_n;
      m2_[i] += delta * (nominal[i] - mean_[i]);
    }
  }

  uint32_t count() const { return n_; }

  // Otoskeskihajonta (n − 1). Alle kahdella näytteellä vakautta ei voi
  // osoittaa -> sd = ääretön, jolloin computeGain antaa NotStable.
  void result(float (&mean)[kChannels], float (&sd)[kChannels]) const {
    for (int i = 0; i < kChannels; ++i) {
      mean[i] = mean_[i];
      if (n_ < 2) {
        sd[i] = std::numeric_limits<float>::infinity();
      } else {
        const float var = m2_[i] / static_cast<float>(n_ - 1);
        sd[i] = std::sqrt(var > 0.0f ? var : 0.0f);
      }
    }
  }

 private:
  uint32_t n_ = 0;
  float mean_[kChannels] = {0.0f, 0.0f, 0.0f};
  float m2_[kChannels] = {0.0f, 0.0f, 0.0f};
};

}  // namespace sbx
