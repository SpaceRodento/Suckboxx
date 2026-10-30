#pragma once
// Suckboxx core - signaalinkäsittely (WP-2, docs/firmware.md §4.2).
//
// Muuntaa 1 kHz:n kalibroidut kPa-näytteet kanavittain D1-luvuiksi (syklin
// keskiarvo, minimi, huippu-huippu) ja kierrosluvuksi. Koko käytösmatriisi (§8)
// nojaa näihin lukuihin, joten tämän moduulin virhe näkyy suoraan vääränä
// säätöohjeena.
//
// Ketju per kanava:
//
//   kPa ──┬──► EMA ─────────────────────────────────► level_kpa (aina voimassa)
//         ├──► 4. kert. Butterworth-LP ──► suhteellinen Schmitt ──► syklirajat
//         └──► suodattamaton trapetsi-integraali rajalta rajalle ──► D1-keskiarvo
//   mV ───────► vika- (rivi 7) ja saturaatiomonitori (rivi 10)
//
// Tärkein suunnitteluperiaate: suodatettua signaalia käytetään VAIN siihen,
// *missä* syklirajat ovat. Itse keskiarvo lasketaan suodattamattomasta
// signaalista täsmälleen yhden syklin yli, jolloin letkuresonanssi ja kohina
// keskiarvoistuvat pois ilman suodattimen viivettä (D1). Jaksollisella
// signaalilla suodatetun ja suodattamattoman syklikeskiarvon ero olisi nolla
// (DC-vahvistus 1), mutta tason muuttuessa (kaasu, rivi 5) suodatettu
// keskiarvo jäisi ryhmäviiveen verran (~10 ms @ 40 Hz) jälkeen.
//
// Aika on näytenumeroina (1 näyte = 1 ms, kSampleRateHz). RawFrame.seq:iä ei
// käytetä aikana, koska pudonneita näytteitä ei voi kuitenkaan interpoloida
// IIR-suotimeen jälkikäteen - aukot näkyvät raw_ringin diagnostiikassa.
//
// Mitatut rajat (testit test_dsp, oletusparametrit):
//  - Lukitus (has_pulse) käyvään signaaliin: <= 3,5 sykliä + suotimen
//    asettuminen 37,5 ms, eli 600 rpm ~0,7 s, 850 rpm ~0,5 s, 3000 rpm ~0,17 s
//    (~4,3 sykliä, koska 37,5 ms on siellä ~1 sykli).
//  - Pienin havaittava imupulssi (puolisini, intake_frac 0,25): suodatettu
//    huippu-huippu >= ~0,5 kPa (hyst_min_kpa-lattia). Alle ~3000 rpm se on
//    sama kuin raakapulssin syvyys (~1 kPa); 4500 rpm:ssä ~2 kPa ja 6000
//    rpm:ssä ~4 kPa syvyys, koska 40 Hz:n alipäästö vaimentaa perustaajuutta
//    (50 Hz -> 0,38x). Ristivuoto xtalk vaimentaa suurilla kierroksilla
//    perustaajuutta lisää (1 - xtalk), joten 6000 rpm, xtalk 0,3 vaatii ~6 kPa.

#include <cmath>
#include <cstdint>

#include "types.h"

namespace sbx {

// ---------------------------------------------------------------------------
// 2. kertaluvun IIR-osa, Direct Form II transposed. DF2T on floatilla
// kaskadimuodoista tarkin (tilat pysyvät signaalin suuruusluokassa) ja vaatii
// vain kaksi tilamuuttujaa.
// ---------------------------------------------------------------------------
class Biquad {
 public:
  // Bilineaarimuunnos esivääristyksellä (prewarp), jotta -3 dB osuu täsmälleen
  // fc:hen myös kun fc/fs ei ole pieni (40 Hz / 1 kHz siirtyisi ilman sitä ~1 %).
  void setLowpass(float fc_hz, float fs_hz, float q) {
    const float k = std::tan(kPi * fc_hz / fs_hz);
    const float k2 = k * k;
    const float norm = 1.0f / (1.0f + k / q + k2);
    b0_ = k2 * norm;
    b1_ = 2.0f * b0_;
    b2_ = b0_;
    a1_ = 2.0f * (k2 - 1.0f) * norm;
    a2_ = (1.0f - k / q + k2) * norm;
    s1_ = 0.0f;
    s2_ = 0.0f;
  }

  // Tilat vakiotulon x lepotilaan. Ilman tätä suodin lähtisi nollasta, ja
  // -55 kPa:n signaali näkyisi käynnistyksessä 55 kPa:n askeleena.
  void prime(float x) {
    s2_ = (b2_ - a2_) * x;
    s1_ = (b1_ - a1_) * x + s2_;
  }

  float process(float x) {
    const float y = b0_ * x + s1_;
    s1_ = b1_ * x - a1_ * y + s2_;
    s2_ = b2_ * x - a2_ * y;
    return y;
  }

 private:
  static constexpr float kPi = 3.14159265f;
  float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
  float s1_ = 0.0f, s2_ = 0.0f;
};

// 4. kertaluvun Butterworth-alipäästö kahtena biquadina.
//
// Miksi 4. kertaluku: letkuresonanssi on 90-215 Hz (SUUNNITTELU.md §2.3) ja
// sen amplitudi on samaa luokkaa kuin pienten (ristivuoto)pulssien. 40 Hz:n
// rajalla 4. kertaluku vaimentaa 90 Hz:n 0,04-kertaiseksi (3 kPa -> 0,12 kPa),
// 2. kertaluku vain 0,2-kertaiseksi, mikä riittäisi heilauttamaan hystereesiä.
// Butterworth, koska tasainen päästökaista ja maltillinen ylitys (~11 %) eivät
// tee pulssin jälkeen kaksoisylityksiä Schmittiin.
class ButterLp4 {
 public:
  void setup(float fc_hz, float fs_hz) {
    // Q-arvot napapareille: 1 / (2 cos(pi/8)) ja 1 / (2 cos(3 pi/8)).
    s_[0].setLowpass(fc_hz, fs_hz, 0.54119610f);
    s_[1].setLowpass(fc_hz, fs_hz, 1.30656296f);
  }
  void prime(float x) {
    s_[0].prime(x);
    s_[1].prime(x);
  }
  float process(float x) { return s_[1].process(s_[0].process(x)); }

 private:
  Biquad s_[2];
};

// ---------------------------------------------------------------------------
// Dsp - julkinen rajapinta docs/firmware.md §4.2.
// ---------------------------------------------------------------------------
class Dsp {
 public:
  // Kiinteät kapasiteetit (periaate 4: ei dynaamista muistia). avg_cycles ja
  // rpm_hist rajataan näihin; 32 sykliä = 4,5 s joutokäynnillä, enempää ei
  // säätönäytössä haluta, ja 64 jaksoa riittää hajonnan arvioon.
  static constexpr int kMaxAvgCycles = 32;
  static constexpr int kMaxRpmHist = 64;

  // Jaksoaikarajojen toleranssi. rpm_min/rpm_max ovat nimellisrajoja; ilman
  // toleranssia moottori juuri rajalla (esim. 6000 rpm) hylkäisi jitterin ja
  // mittauskohinan takia joka toisen syklin. Vuotopulssien (T/3) ja
  // puuttuvien rajojen (2T) torjunta ei nojaa tähän alueeseen - ne osuvat
  // usein sen sisälle - vaan amplitudi- ja jaksosuhde-ehtoihin.
  static constexpr float kPeriodTolerance = 0.10f;
  // Syklin pienin hyväksyttävä (suodatettu) huippu-huippu = tämä x
  // hyst_min_kpa. Pelkkä hystereesi ei riitä kohinan torjuntaan: suodatettu
  // kohina ylittää 0,3 kPa:n hystereesin satunnaisesti, ja silloin letku irti
  // (rivi 6) tai moottori seis (rivi 1) näyttäisi pulssivan.
  static constexpr float kMinPulseHystFactor = 3.0f;
  // Suurilla kierroksilla perustaajuus osuu alipäästön kaistalle (6000 rpm =
  // 50 Hz -> |H| 0,38 @ 40 Hz), ja aitokin pulssi näkyy suodatettuna pienenä.
  // Siksi kiinteä raja skaalataan suotimen vahvistuksella syklin omalla
  // perustaajuudella (ks. ampOk). Alaraja estää rajan katoamisen kokonaan,
  // jos rpm_max nostetaan reilusti.
  static constexpr float kMinGateGain = 0.25f;
  // Skaalatun rajan varassa hyväksytyn (pienen) syklin jakson on oltava
  // tämän suhteen sisällä edellisestä raja-raja-välistä. Kohina voi tuottaa
  // satunnaisen 0,4 kPa:n "syklin", mutta ei peräkkäisiä samanmittaisia;
  // oikea moottori 4500-6000 rpm:ssä toistaa jakson muutaman prosentin tarkkuudella.
  static constexpr float kSmallPulseRatio = 1.25f;
  // Amplitudien yhtäpitävyys peräkkäisten syklien välillä (suhde 0,7...1/0,7)
  // lukituksen haussa. Liian pieni amplitudi syklin alussa: ensimmäinen raja
  // voi osua vuotopulssiin (xtalk) tai pulssin alkuun - sykli on väärän
  // mittainen. Liian suuri: kynnykset ovat liian leveät ja rajoja jää
  // välistä, jolloin "sykli" onkin useita syklejä.
  static constexpr float kEnvStableFrac = 0.7f;
  // Lukittuna amplitudi saa muuttua syklistä toiseen enemmän (kaasunlisäys
  // muuttaa pulssin syvyyttä 20-30 % per sykli). 0,5 erottaa yhä vuotopulssin
  // (<= 0,3-0,5 x oma pulssi) omasta, ja jaksosuhde vahtii puuttuvia rajoja.
  static constexpr float kEnvLockFrac = 0.5f;
  // Lukituksen haussa syklin alku- ja loppurajan laukaisuamplitudien suhde.
  // Kynnys on 0,65 x A pulssin huipusta; 0,85:n ero siirtää rajaa
  // puolisinipulssin kyljellä ~1 % jaksosta, mikä on ensimmäiselle syklille
  // hyväksyttävä (se näkyy keskiarvossa avg_cycles syklin ajan).
  static constexpr float kFireMatch = 0.85f;
  // Peräkkäisten jaksojen suhde. Kampiakselin nopeus ei voi muuttua yhden
  // syklin aikana 1,8-kertaiseksi, joten sellainen jakso on puuttuva raja
  // (pulssi jäi kynnyksen alle) - ja 2T osuisi silti rpm-alueelle, jolloin
  // kierrosluku näyttäisi puolet todellisesta. Rajat ovat väljät, jotta
  // aidosti epätasainen käynti (cv 15 %, rivi 2) ei karsiudu: 1,8 ja 0,55
  // ovat ~2,8 sigmaa peräkkäisten jaksojen suhteesta cv:llä 0,15. Kahden
  // peräkkäisen hylkäyksen jälkeen viite vaihdetaan, jotta nopea kaasunlisäys
  // ei jää jumiin. Sama 1,8 x jakso on lukittuna myös amplitudiarvion
  // vanhenemisaika: jos rajaa ei ole tullut sinä aikana, pulssi on pienentynyt
  // kynnyksen alle eikä nykyistä sykliä hyväksyttäisi kuitenkaan.
  static constexpr float kMaxPeriodRatio = 1.8f;
  static constexpr float kMinPeriodRatio = 0.55f;
  static constexpr uint8_t kMaxRatioRejects = 2;
  // Rivi 10: saturaatio näkyy 1 s viimeisestä havainnosta.
  static constexpr uint32_t kSatWindowSamples = kSampleRateHz;

  Dsp() { configure(DspParams(), SensorParams()); }

  // Laskee johdetut vakiot ja nollaa tilan. Kutsutaan aina kun konfiguraatio
  // muuttuu (S5a) - tila nollataan, koska vanhat syklit on mitattu eri
  // parametreilla.
  void configure(const DspParams& dp, const SensorParams& sp) {
    dp_ = dp;
    sp_ = sp;
    const float fs = static_cast<float>(kSampleRateHz);

    // Rajataajuus rajataan vakaalle alueelle: alle 0,5 Hz ei ole järkevä
    // (sykli on >= 5 Hz) ja yli 0,45 fs bilineaarimuunnos ei ole mielekäs.
    float fc = dp.lp_cutoff_hz;
    if (!(fc >= 0.5f)) fc = 0.5f;
    if (fc > 0.45f * fs) fc = 0.45f * fs;
    lp_proto_.setup(fc, fs);
    tan_fc_ = std::tan(kPi * fc / fs);

    // EMA: alpha = 1 - exp(-dt/tau) antaa tarkan aikavakion myös lyhyellä tau:lla.
    level_alpha_ = 1.0f;
    if (dp.level_tau_s > 0.0f) level_alpha_ = 1.0f - std::exp(-kSamplePeriodS / dp.level_tau_s);

    avg_n_ = clampInt(dp.avg_cycles, 1, kMaxAvgCycles);
    rpm_hist_n_ = clampInt(dp.rpm_hist, 2, kMaxRpmHist);
    // Hajonta on luotettava vasta kun puolet historiasta on kertynyt (16 -> 8
    // jaksoa = ~1 s joutokäynnillä). Liian aikainen sd kahdesta jaksosta
    // heiluttaisi rivin 2 (Unstable) päätöstä edestakaisin.
    min_rpm_periods_ = rpm_hist_n_ / 2 > 2 ? rpm_hist_n_ / 2 : 2;

    float rpm_min = dp.rpm_min > 1.0f ? dp.rpm_min : 1.0f;
    float rpm_max = dp.rpm_max > rpm_min ? dp.rpm_max : rpm_min * 2.0f;
    min_period_ = 120.0f * fs / rpm_max * (1.0f - kPeriodTolerance);
    max_period_ = 120.0f * fs / rpm_min * (1.0f + kPeriodTolerance);
    // Lukituksen haussa (ei jaksoviitettä) amplitudiarvio vanhenee ja ketju
    // katkeaa, jos rajaa ei ole tullut pisimmän hyväksyttävän jakson aikana.
    window_n_ = static_cast<uint32_t>(max_period_) + 1u;
    // Suotimen asettumisaika (4. kert. Butterworthin askelvaste asettuu 2 %:iin
    // ~1,3/fc:ssä). Ensimmäinen näyte voi osua resonanssin huippuun, jolloin
    // prime() jättää muutaman kPa:n soimisen; sen aikana amplitudia ei
    // kerätä, muuten ensimmäiset kynnykset olisivat liian leveät ja rajat
    // jäisivät välistä vanhenemisajan (~0,5 s).
    settle_n_ = secondsToSamples(1.5f / fc);

    timeout_n_ = secondsToSamples(dp.pulse_timeout_s);
    fault_hold_n_ = secondsToSamples(sp.fault_hold_s);
    min_pulse_p2p_ = kMinPulseHystFactor * dp.hyst_min_kpa;
    reset();
  }

  // Nollaa tilan paikallaan: historiataulukoihin ei kosketa (laskurit
  // rajaavat niiden käytön), eikä pinoon rakenneta väliaikaista kanavaa -
  // configure() ajetaan ESP32:lla web-taskista (POST /api/config).
  void reset() {
    n_ = 0;
    for (int i = 0; i < kChannels; ++i) clearChan(ch_[i]);
  }

  // 1 kHz. kpa = kalibroitu paine per kanava, raw = alkuperäinen kehys (mV
  // vikamonitoreille). O(1) per kanava; hyväksytyn syklin kohdalla
  // O(avg_cycles + rpm_hist) <= 96 kertolaskua.
  void push(const float (&kpa)[kChannels], const RawFrame& raw) {
    ++n_;
    for (int i = 0; i < kChannels; ++i) pushChannel(ch_[i], kpa[i], raw.mv[i]);
  }

  // mean/min/p2p säilyttävät viimeisen arvonsa, kun has_pulse putoaa
  // (types.h: voimassa vain kun has_pulse); rpm nollataan.
  const ChannelState& channel(int i) const {
    return ch_[(i >= 0 && i < kChannels) ? i : 0].pub;
  }

  // Kierrosluku = pulssivien kanavien mediaani. Hajonta lasketaan kanavittain
  // ja siitäkin otetaan mediaani (ei yhdistetä jaksoja): kaikki kanavat näkevät
  // saman kampiakselin, joten aito sykli-sykliltä vaihtelu näkyy jokaisessa,
  // mutta yhden kanavan tunnistusvirhe (osittain tukkeutunut letku,
  // satunnainen kaksoisraja) jää mediaanissa vähemmistöön. Yhdistetyssä
  // historiassa yksikin huono kanava nostaisi hajonnan ja jumittaisi
  // näytön tilaan "epävakaa" (rivi 2).
  RpmEstimate rpm() const {
    RpmEstimate est;
    float r[kChannels];
    float s[kChannels];
    int nr = 0;
    int ns = 0;
    for (int i = 0; i < kChannels; ++i) {
      const Chan& c = ch_[i];
      if (!c.pub.has_pulse) continue;
      r[nr++] = c.pub.rpm;
      if (c.per_count >= min_rpm_periods_) s[ns++] = c.sd_rpm;
    }
    if (nr == 0) return est;
    est.rpm = median(r, nr);
    if (ns == 0 || !(est.rpm > 0.0f)) return est;
    est.sd_rpm = median(s, ns);
    est.cv = est.sd_rpm / est.rpm;
    est.valid = true;
    return est;
  }

  const DspParams& params() const { return dp_; }

  // Alipäästön vahvistus taajuudella f (testit ja ampOk). Bilineaarimuunnetulle
  // Butterworthille tarkasti |H|^2 = 1 / (1 + (tan(pi f/fs) / tan(pi fc/fs))^8).
  float lpGain(float f_hz) const {
    float w = kPi * f_hz / static_cast<float>(kSampleRateHz);
    if (!(w >= 0.0f)) w = 0.0f;
    if (w > 1.5f) w = 1.5f;  // tan:n napa on pi/2:ssa (Nyquist)
    const float r = std::tan(w) / tan_fc_;
    const float r2 = r * r;
    const float r4 = r2 * r2;
    return 1.0f / std::sqrt(1.0f + r4 * r4);
  }

 private:
  static constexpr float kPi = 3.14159265f;

  // Suodatetun signaalin ääriarvot ja heilahdukset yhdeltä segmentiltä
  // (= syklirajojen väli, tai vanhenemisaika jos rajaa ei tule).
  struct Seg {
    float hi = 0.0f, lo = 0.0f;
    float ru = 0.0f;    // suurin nousu: pohjasta myöhempään huippuun
    float dd = 0.0f;    // suurin lasku: huipusta myöhempään pohjaan
    bool full = false;  // alkoi syklirajalta (ei käynnistyksestä tai vanhenemisesta)
  };

  struct Chan {
    ChannelState pub;
    ButterLp4 lp;
    bool primed = false;
    uint32_t settle = 0;  // näytteitä jäljellä suotimen asettumiseen
    float x_prev = 0.0f;  // edellinen kalibroitu näyte (trapetsi-integraaliin)
    float y_prev = 0.0f;  // edellinen suodatettu näyte (rajan interpolointiin)

    // Amplitudiarvio kahdesta viimeisimmästä segmentistä ja suhteellinen
    // Schmitt: run_hi = huippu virityksen jälkeen, run_lo = pohja rajan jälkeen.
    Seg prev, cur;
    uint32_t seg_len = 0;
    float run_hi = 0.0f, run_lo = 0.0f;
    bool armed = false;
    uint32_t since_boundary = 0;

    // Sykliketju: edellinen raja (kokonais- + murto-osa näytteinä).
    bool chain = false;
    uint32_t b_int = 0;
    float b_frac = 0.0f;
    float acc = 0.0f;            // suodattamaton integraali edellisestä rajasta, kPa*näyte
    float last_amp = 0.0f;       // edellisen raja-raja-välin amplitudi (yhtäpitävyys)
    float last_period = 0.0f;    // edellisen raja-raja-välin pituus, 0 = ei
    float fire_amp = 0.0f;       // A, jolla kuluvan välin aloittanut raja laukesi
    float ref_period = 0.0f;     // viimeisin hyväksytty jakso, 0 = ei viitettä
    uint8_t ratio_rejects = 0;
    bool env_from_cycle = false; // kuluva väli alkoi ketjun toiselta tai myöhemmältä rajalta

    // Ehdokassykli, joka vahvistetaan kun sen päättänyt pulssi on nähty
    // pohjaan asti (ks. confirmPending).
    bool pending = false;
    float pend_int = 0.0f, pend_per = 0.0f, pend_min = 0.0f, pend_amp = 0.0f, pend_top = 0.0f;
    uint32_t since_cycle = 0;    // näytteitä viimeisestä hyväksytystä syklistä

    // Syklihistoria. integral/period erikseen, jotta keskiarvo on aikapainotettu
    // eli täsmälleen avg_cycles kokonaisen syklin yli (D1). Laskurit rajaavat
    // käytön, joten reset() ei nollaa taulukoita.
    float cyc_int[kMaxAvgCycles] = {};
    float cyc_per[kMaxAvgCycles] = {};
    float cyc_min[kMaxAvgCycles] = {};
    float cyc_p2p[kMaxAvgCycles] = {};
    int cyc_head = 0;
    int cyc_count = 0;
    float per[kMaxRpmHist] = {};
    int per_head = 0;
    int per_count = 0;
    float sd_rpm = 0.0f;

    float level = 0.0f;
    uint32_t fault_run = 0;
    uint32_t since_sat = 0;
  };

  static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

  static uint32_t secondsToSamples(float s) {
    const float n = s * static_cast<float>(kSampleRateHz) + 0.5f;
    if (!(n >= 1.0f)) return 1u;
    if (n > 4.0e9f) return 4000000000u;
    return static_cast<uint32_t>(n);
  }

  static float median(float* v, int n) {
    for (int i = 1; i < n; ++i) {  // n <= kChannels: lisäyslajittelu riittää
      const float t = v[i];
      int j = i - 1;
      while (j >= 0 && v[j] > t) {
        v[j + 1] = v[j];
        --j;
      }
      v[j + 1] = t;
    }
    return (n & 1) ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
  }

  static void segReset(Seg& s, float y, bool full) {
    s.hi = s.lo = y;
    s.ru = s.dd = 0.0f;
    s.full = full;
  }

  static void segAdd(Seg& s, float y) {
    if (y > s.hi) {
      s.hi = y;
    } else if (s.hi - y > s.dd) {
      s.dd = s.hi - y;
    }
    if (y < s.lo) {
      s.lo = y;
    } else if (y - s.lo > s.ru) {
      s.ru = y - s.lo;
    }
  }

  // Kaikki skalaarit alkuarvoihin kenttä kerrallaan (ei väliaikaista Chania).
  void clearChan(Chan& c) const {
    c.pub = ChannelState();
    c.lp = lp_proto_;
    c.primed = false;
    c.settle = 0;
    c.x_prev = c.y_prev = 0.0f;
    segReset(c.prev, 0.0f, false);
    segReset(c.cur, 0.0f, false);
    c.seg_len = 0;
    c.run_hi = c.run_lo = 0.0f;
    c.armed = false;
    c.since_boundary = 0;
    c.chain = false;
    c.b_int = 0;
    c.b_frac = 0.0f;
    c.acc = 0.0f;
    c.last_amp = 0.0f;
    c.last_period = 0.0f;
    c.fire_amp = 0.0f;
    c.ref_period = 0.0f;
    c.ratio_rejects = 0;
    c.env_from_cycle = false;
    c.pending = false;
    c.pend_int = c.pend_per = c.pend_min = c.pend_amp = c.pend_top = 0.0f;
    c.since_cycle = 0;
    c.cyc_head = c.cyc_count = 0;
    c.per_head = c.per_count = 0;
    c.sd_rpm = 0.0f;
    c.level = 0.0f;
    c.fault_run = 0;
    c.since_sat = kSatWindowSamples;  // ei saturaatiota ennen ensimmäistä havaintoa
  }

  static bool locked(const Chan& c) { return c.pub.has_pulse && c.ref_period > 0.0f; }

  // Pulssin huippu-huippu A kahden viimeisimmän segmentin yli:
  // A = min(suurin nousu, suurin lasku). Jaksollisella signaalilla molemmat
  // ovat p2p. Tason askel kasvattaa vain toista (alipaine kasvaa: laskua,
  // pienenee: nousua), joten minimi ei reagoi siihen - absoluuttinen
  // verhokäyrä levisi askeleen verran ja jätti pulssit kynnysten väliin
  // (0,8-1 s ilman syklejä joka kaasun liikkeellä ja moottorin
  // käynnistyksessä). Aito pulssin kasvu näkyy molemmissa, joten A laajenee
  // heti yhden kokonaisen pulssin jälkeen. Pieneneminen unohtuu, kun vanha
  // segmentti vierii pois (raja tai vanheneminen).
  static float amplitude(const Chan& c) {
    float ru = c.prev.ru > c.cur.ru ? c.prev.ru : c.cur.ru;
    const float ru_x = c.cur.hi - c.prev.lo;
    if (ru_x > ru) ru = ru_x;
    float dd = c.prev.dd > c.cur.dd ? c.prev.dd : c.cur.dd;
    const float dd_x = c.prev.hi - c.cur.lo;
    if (dd_x > dd) dd = dd_x;
    return ru < dd ? ru : dd;
  }

  static void roll(Chan& c, float y, bool at_boundary) {
    c.prev = c.cur;
    segReset(c.cur, y, at_boundary);
    c.seg_len = 0;
  }

  void pushChannel(Chan& c, float x, uint16_t mv) {
    // Monitorit raakajännitteestä, ei kPa:sta: anturivika pitää tunnistaa
    // vaikka kalibrointi olisi pielessä (rivi 7). Vika = yhtäjaksoisesti
    // fault_hold_s alueen ulkopuolella; yksikin hyvä näyte nollaa laskurin.
    if (mv < sp_.fault_lo_mv || mv > sp_.fault_hi_mv) {
      if (c.fault_run < fault_hold_n_) ++c.fault_run;
    } else {
      c.fault_run = 0;
    }
    c.pub.sensor_fault = c.fault_run >= fault_hold_n_;
    if (mv >= sp_.sat_mv) {
      c.since_sat = 0;
    } else if (c.since_sat < kSatWindowSamples) {
      ++c.since_sat;
    }
    c.pub.saturated = c.since_sat < kSatWindowSamples;
    c.pub.last_mv = mv;

    // NaN/Inf jäisi IIR-tiloihin pysyvästi; korvataan edellisellä näytteellä.
    if (!std::isfinite(x)) {
      if (!c.primed) return;
      x = c.x_prev;
    }
    c.pub.last_kpa = x;

    if (!c.primed) {
      // Ensimmäinen näyte: EMA ja suodin lähtevät suoraan signaalin tasolta,
      // muuten taso ryömisi nollasta ja näyttäisi hetken "moottori seis" (rivi 1).
      c.primed = true;
      c.settle = settle_n_;
      c.lp.prime(x);
      c.level = x;
      c.x_prev = x;
      c.y_prev = x;
      c.pub.level_kpa = x;
      return;
    }

    c.level += level_alpha_ * (x - c.level);
    c.pub.level_kpa = c.level;

    // has_pulse putoaa tasan pulse_timeout_s viimeisen syklin jälkeen
    // (tarkistus ennen rajantunnistusta, jotta syklin oma näyte ei kuluta
    // aikaa). Historia tyhjennetään, jotta uuden käynnin luvut eivät sekoitu
    // vanhoihin (esim. letku kytketty takaisin).
    if (c.pub.has_pulse && ++c.since_cycle >= timeout_n_) {
      c.pub.has_pulse = false;
      c.pub.rpm = 0.0f;
      c.cyc_count = 0;
      c.per_count = 0;
      c.sd_rpm = 0.0f;
      c.ref_period = 0.0f;
      c.ratio_rejects = 0;
    }

    const float y = c.lp.process(x);
    const float area = 0.5f * (c.x_prev + x);  // trapetsi välille [n-1, n]

    if (c.settle > 0) {
      // Viritetty heti asettumisen jälkeen: ensimmäinen raja voi osua mihin
      // tahansa vaiheeseen, mutta siitä alkavaa sykliä ei koskaan hyväksytä
      // (env_from_cycle), joten aikainen raja vain nopeuttaa lukitusta.
      if (--c.settle == 0) c.armed = true;
      segReset(c.prev, y, false);
      segReset(c.cur, y, false);
      c.run_hi = c.run_lo = y;
      c.x_prev = x;
      c.y_prev = y;
      return;
    }

    segAdd(c.cur, y);
    ++c.seg_len;
    if (c.since_boundary < 0xFFFFFFFFu) ++c.since_boundary;

    // Suhteellinen Schmitt: raja, kun signaali putoaa D verran viimeisimmän
    // huipun alle; viritys, kun se nousee D verran pohjasta.
    // D = (A + h) / 2, h = max(hyst_min_kpa, hyst_frac x A). Tasaisella
    // signaalilla tämä on täsmälleen hystereesi h pulssin puolivälin
    // ympärillä (huippu - A/2), mutta keskikohta seuraa tasoa syklin sisällä.
    // Puoliväli eikä keskiarvo: imupulssi on kapea (~25 % syklistä), joten
    // keskiarvo on lähellä yläreunaa ja ristivuodon pulssit (xtalk 0,3)
    // ylittäisivät sen. Puolivälistä laskettuna raja vaatii 0,65 x A:n
    // pudotuksen, vuotopulssi on vain ~0,3 x A -> kierrosluku ei kolminkertaistu.
    const float amp = amplitude(c);
    float h = dp_.hyst_frac * amp;
    if (h < dp_.hyst_min_kpa) h = dp_.hyst_min_kpa;
    const float d = 0.5f * (amp + h);

    if (!c.armed) {
      if (y < c.run_lo) c.run_lo = y;
      if (y > c.run_lo + d) {
        c.armed = true;
        if (c.pending) confirmPending(c);
        c.run_hi = y;
      }
      c.acc += area;
    } else {
      if (y > c.run_hi) c.run_hi = y;
      const float thr = c.run_hi - d;
      if (y < thr) {
        // Syklin raja = suodatetun signaalin laskeva ylitys. Ylityshetki
        // interpoloidaan näytteiden väliin, ja integraali katkaistaan samaan
        // kohtaan lineaarisesti interpoloidusta signaalista. Ilman tätä raja
        // pyöristyisi ±0,5 ms, mikä 6000 rpm:ssä (20 ms sykli, pulssin pohjalla
        // ~10 kPa keskiarvosta) on ±0,25 kPa per sykli.
        float f = 1.0f;
        const float dy = c.y_prev - y;
        if (dy > 0.0f) f = (c.y_prev - thr) / dy;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        const float part = f * c.x_prev + 0.5f * f * f * (x - c.x_prev);
        onBoundary(c, f, c.acc + part, y, amp);
        c.acc = area - part;
        c.armed = false;
        c.run_lo = y;
      } else {
        c.acc += area;
      }
    }

    // Amplitudiarvion vanheneminen: jos rajaa ei tule, pulssi on pienentynyt
    // kynnyksen alle (kaasu auki, letku vuotaa) tai loppunut. Vanha segmentti
    // vieritetään pois, jolloin A seuraa uutta pulssia. Lukittuna aika on
    // kMaxPeriodRatio x jakso - sitä pidempää sykliä ei hyväksyttäisi
    // kuitenkaan. Haussa jaksoa ei tiedetä, joten käytetään pisintä sallittua.
    uint32_t stale_n = window_n_;
    if (locked(c)) {
      const float s = kMaxPeriodRatio * c.ref_period;
      if (s < static_cast<float>(window_n_)) stale_n = static_cast<uint32_t>(s) + 1u;
    }
    if (c.seg_len >= stale_n) {
      roll(c, y, false);
      // Kuluvaa väliä ei arvioida (ei kokonainen raja-raja-segmentti), eikä
      // odottavaa sykliä: sen päättänyt "pulssi" ei noussut takaisin, eli se
      // oli tason askel eikä imupulssi.
      c.env_from_cycle = false;
      c.pending = false;
      if (c.since_boundary >= window_n_) {
        c.chain = false;  // näin pitkä väli ei ole kelvollinen jakso
        c.acc = 0.0f;
      }
    }

    c.x_prev = x;
    c.y_prev = y;
  }

  // Pulssin amplitudiehto (kohinasuoja). Raja 3 x hyst_min_kpa koskee
  // suodatettua signaalia, joten se skaalataan suotimen vahvistuksella syklin
  // omalla perustaajuudella: 6000 rpm:ssä 0,9 -> 0,34 kPa. Muuten 6 kPa:n
  // pulssi täydellä kaasulla (~0,8 kPa suodatettuna) hylättäisiin kokonaan ja
  // rpm katoaisi (D13, rivi 9). Kohina ei hyödy skaalauksesta, koska skaalattu
  // raja vaatii lisäksi edellisen kanssa yhtäpitävän jakson (kSmallPulseRatio).
  bool ampOk(float amp, float period, float last_period) const {
    if (amp >= min_pulse_p2p_) return true;
    float g = lpGain(static_cast<float>(kSampleRateHz) / period);
    if (g < kMinGateGain) g = kMinGateGain;
    if (amp < g * min_pulse_p2p_) return false;
    return last_period > 0.0f && period <= kSmallPulseRatio * last_period &&
           kSmallPulseRatio * period >= last_period;
  }

  void onBoundary(Chan& c, float f, float integral, float y, float fire_amp) {
    // Raja on välillä [n-1, n]: kokonaisosa n-1, murto-osa f. Jakso lasketaan
    // kokonaislukuerotuksena (kääntyy oikein uint32-ylivuodossa ~49 vrk:n
    // jälkeen) plus murto-osien erotus - float-aikaleima menettäisi
    // tarkkuutensa jo tunneissa.
    const uint32_t b_int = n_ - 1u;
    // Juuri päättyneen syklin amplitudi: nousu syklin pohjasta huippuun ja
    // lasku edellisen syklin huipusta tämän pohjaan. Pienempi kahdesta, koska
    // tason askel syklin aikana kasvattaa vain toista (vrt. amplitude()).
    float amp = c.cur.ru;
    if (c.prev.full) {
      const float dip = c.prev.hi - c.cur.lo;
      if (dip < amp) amp = dip;
    }
    const float cmin = c.cur.lo;
    float period = 0.0f;
    if (c.chain) period = static_cast<float>(b_int - c.b_int) + (f - c.b_frac);
    c.pending = false;  // ratkeaa aina virityksessä, joka edeltää rajaa
    // Ketjun ensimmäinen väli alkaa mielivaltaisesta kohdasta (käynnistys,
    // vanheneminen), ja sen amplitudi voi sattumalta osua vuotopulssin
    // kokoon - siksi arvioidaan vasta toisesta kokonaisesta välistä alkaen.
    if (c.chain && c.env_from_cycle) {
      const float frac = locked(c) ? kEnvLockFrac : kEnvStableFrac;
      const bool ok_period = period >= min_period_ && period <= max_period_;
      const bool ok_amp = ampOk(amp, period, c.last_period);
      const bool ok_env = c.last_amp >= frac * amp && amp >= frac * c.last_amp;
      // Haussa syklin molempien rajojen on lauettava samalla kynnystasolla,
      // muuten jakso mitataan pulssin kylkien eri kohdista. Kun käynnistys
      // osuu pulssin laskevalle kyljelle, ensimmäistä täyttä laskua ei ole
      // nähty, A seuraa keskeneräistä laskua ja raja laukeaa heti huipulla;
      // seuraava raja (A = p2p) laukeaa 0,65 x p2p alempana -> ensimmäinen
      // sykli 5-15 % liian pitkä. Lukittuna A tulee aina kokonaisista
      // sykleistä, eikä tätä tarvita (ja kaasunlisäyksessä se hylkäisi turhaan).
      const bool ok_fire = locked(c) || (c.fire_amp >= kFireMatch * fire_amp &&
                                         fire_amp >= kFireMatch * c.fire_amp &&
                                         fire_amp >= kEnvStableFrac * amp);
      if (ok_period && ok_amp && ok_env && ok_fire) {
        const bool ok_ratio = c.ref_period <= 0.0f ||
                              (period <= kMaxPeriodRatio * c.ref_period &&
                               period >= kMinPeriodRatio * c.ref_period);
        if (ok_ratio || c.ratio_rejects >= kMaxRatioRejects) {
          c.pending = true;
          c.pend_int = integral;
          c.pend_per = period;
          c.pend_min = cmin;
          c.pend_amp = amp;
          c.pend_top = c.run_hi;  // huippu ennen tätä rajaa
        } else {
          ++c.ratio_rejects;
        }
      }
    }
    c.last_amp = amp;
    c.last_period = period;
    c.fire_amp = fire_amp;
    c.env_from_cycle = c.chain;
    roll(c, y, true);
    c.since_boundary = 0;
    c.b_int = b_int;
    c.b_frac = f;
    c.chain = true;
  }

  // Vahvistus, kun syklin päättänyt pulssi on nähty pohjaan asti (viritys).
  //
  // Haussa: ristivuodolla kahden vuotopulssin väli ja vuotopulssista omaan
  // pulssiin ulottuva väli näyttävät amplitudin puolesta yhtä
  // johdonmukaisilta - ja niiden jakso on T/3, eli rpm kolminkertainen. Ero
  // näkyy vasta, kun rajan laukaissut pulssi on nähty pohjaan asti.
  // Hyväksytty sykli vaatii siis kolme peräkkäistä samankokoista pulssia.
  // Vuotopulsseja on peräkkäin enintään kaksi ja ne ovat xtalk <= 0,5:llä
  // alle 0,7 x oma, joten kolme samankokoista voivat olla vain omia.
  //
  // Lukittuna: raja voi laueta myös tason askeleesta (alipaine kasvaa
  // kerralla > 0,65 x A, esim. /api/sim). Askeleen "pulssi" on askel + oma
  // pulssi syvä, joten yläraja hylkää syklin. Hylätyn syklin jälkeinen väli
  // alkoi samalta epäilyttävältä rajalta, joten sitäkään ei arvioida.
  void confirmPending(Chan& c) {
    c.pending = false;
    const float depth = c.pend_top - c.run_lo;
    const float lo = locked(c) ? kEnvLockFrac : kEnvStableFrac;
    if (depth >= lo * c.pend_amp && c.pend_amp >= kEnvStableFrac * depth) {
      recordCycle(c, c.pend_int, c.pend_per, c.pend_min, c.pend_amp);
    } else {
      c.env_from_cycle = false;
    }
  }

  void recordCycle(Chan& c, float integral, float period, float cmin, float cp2p) {
    c.ref_period = period;
    c.ratio_rejects = 0;
    c.cyc_int[c.cyc_head] = integral;
    c.cyc_per[c.cyc_head] = period;
    c.cyc_min[c.cyc_head] = cmin;
    c.cyc_p2p[c.cyc_head] = cp2p;
    c.cyc_head = (c.cyc_head + 1) % kMaxAvgCycles;
    if (c.cyc_count < kMaxAvgCycles) ++c.cyc_count;
    c.per[c.per_head] = period;
    c.per_head = (c.per_head + 1) % kMaxRpmHist;
    if (c.per_count < kMaxRpmHist) ++c.per_count;

    ++c.pub.cycles;
    c.pub.has_pulse = true;
    c.since_cycle = 0;

    // Näyttöarvot avg_cycles viimeisimmän syklin yli. Keskiarvo on
    // sum(integraali) / sum(jakso) eli aikapainotettu: sama kuin yksi
    // integraali N kokonaisen syklin yli.
    const int n = c.cyc_count < avg_n_ ? c.cyc_count : avg_n_;
    float s_int = 0.0f, s_per = 0.0f, s_min = 0.0f, s_p2p = 0.0f;
    for (int k = 0; k < n; ++k) {
      const int i = (c.cyc_head - 1 - k + kMaxAvgCycles) % kMaxAvgCycles;
      s_int += c.cyc_int[i];
      s_per += c.cyc_per[i];
      s_min += c.cyc_min[i];
      s_p2p += c.cyc_p2p[i];
    }
    const float fs = static_cast<float>(kSampleRateHz);
    const float inv_n = 1.0f / static_cast<float>(n);
    c.pub.mean_kpa = s_int / s_per;
    c.pub.min_kpa = s_min * inv_n;
    c.pub.p2p_kpa = s_p2p * inv_n;
    // rpm = 120 / keskijakso (ei 120/T:n keskiarvo, joka on jitterillä harhainen
    // ylöspäin): kierroksia aikayksikössä.
    c.pub.rpm = 120.0f * fs * static_cast<float>(n) / s_per;

    // Sykli-sykliltä hajonta rpm-yksiköissä rpm_hist viimeisimmästä jaksosta.
    const int m = c.per_count < rpm_hist_n_ ? c.per_count : rpm_hist_n_;
    if (m >= 2) {
      float sum = 0.0f;
      for (int k = 0; k < m; ++k) {
        sum += 120.0f * fs / c.per[(c.per_head - 1 - k + kMaxRpmHist) % kMaxRpmHist];
      }
      const float avg = sum / static_cast<float>(m);
      float ss = 0.0f;
      for (int k = 0; k < m; ++k) {
        const float d = 120.0f * fs / c.per[(c.per_head - 1 - k + kMaxRpmHist) % kMaxRpmHist] - avg;
        ss += d * d;
      }
      c.sd_rpm = std::sqrt(ss / static_cast<float>(m - 1));
    } else {
      c.sd_rpm = 0.0f;
    }
  }

  DspParams dp_;
  SensorParams sp_;
  ButterLp4 lp_proto_;
  float tan_fc_ = 1.0f;
  float level_alpha_ = 1.0f;
  int avg_n_ = 8;
  int rpm_hist_n_ = 16;
  int min_rpm_periods_ = 8;
  float min_period_ = 0.0f;  // näytteinä
  float max_period_ = 0.0f;
  uint32_t settle_n_ = 1;
  uint32_t window_n_ = 1;
  uint32_t timeout_n_ = 1;
  uint32_t fault_hold_n_ = 1;
  float min_pulse_p2p_ = 0.0f;
  uint32_t n_ = 0;  // kuluvan näytteen indeksi (yhteinen kaikille kanaville)
  Chan ch_[kChannels];
};

}  // namespace sbx
