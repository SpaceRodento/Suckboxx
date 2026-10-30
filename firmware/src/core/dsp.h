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
//         ├──► 4. kert. Butterworth-LP ──► verhokäyrä + Schmitt ──► syklirajat
//         └──► suodattamaton trapetsi-integraali rajalta rajalle ──► D1-keskiarvo
//   mV ───────► vika- (rivi 7) ja saturaatiomonitori (rivi 10)
//
// Tärkein suunnitteluperiaate: suodatettua signaalia käytetään VAIN siihen,
// *missä* syklirajat ovat. Itse keskiarvo lasketaan suodattamattomasta
// signaalista täsmälleen yhden syklin yli, jolloin letkuresonanssi ja kohina
// keskiarvoistuvat pois ilman suodattimen vaihevääristymää (D1).
//
// Aika on näytenumeroina (1 näyte = 1 ms, kSampleRateHz). RawFrame.seq:iä ei
// käytetä aikana, koska pudonneita näytteitä ei voi kuitenkaan interpoloida
// IIR-suotimeen jälkikäteen - aukot näkyvät raw_ringin diagnostiikassa.

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
  // mittauskohinan takia joka toisen syklin. 10 % ei silti päästä läpi
  // vuotopulssien (1/3 tai 2/3 jaksosta) tai puuttuvan syklin (2x) virheitä.
  static constexpr float kPeriodTolerance = 0.10f;
  // Syklin pienin hyväksyttävä huippu-huippu = tämä x hyst_min_kpa. Pelkkä
  // hystereesi ei riitä kohinan torjuntaan: suodatettu kohina ylittää 0,3 kPa:n
  // hystereesin satunnaisesti, ja silloin letku irti (rivi 6) tai moottori seis
  // (rivi 1) näyttäisi pulssivan. Aito pulssi on joutokäynnillä ~20 kPa ja
  // täydellä kaasulla suodatettunakin useita kPa, joten 0,9 kPa on turvallinen.
  static constexpr float kMinPulseHystFactor = 3.0f;
  // Sykli hyväksytään vain, jos sen alussa käytössä ollut verhokäyrä vastasi
  // syklin omaa huippu-huippua (suhde 0,7...1/0,7). Liian pieni verhokäyrä:
  // käynnistyksessä ensimmäinen raja voi osua vuotopulssiin (xtalk) tai
  // pulssin alkuun - sykli on väärän mittainen. Liian suuri verhokäyrä
  // (suotimen alkutransientti, pudonnut pulssin syvyys): kynnykset ovat liian
  // leveät ja rajoja jää välistä, jolloin "sykli" onkin useita syklejä.
  static constexpr float kEnvStableFrac = 0.7f;
  // Peräkkäisten jaksojen suhde. Kampiakselin nopeus ei voi muuttua yhden
  // syklin aikana 1,8-kertaiseksi, joten sellainen jakso on puuttuva raja
  // (pulssi jäi kynnyksen alle) - ja 2T osuisi silti rpm-alueelle, jolloin
  // kierrosluku näyttäisi puolet todellisesta. Rajat ovat väljät, jotta
  // aidosti epätasainen käynti (cv 15 %, rivi 2) ei karsiudu: 1,8 ja 0,55
  // ovat ~2,8 sigmaa peräkkäisten jaksojen suhteesta cv:llä 0,15. Kahden
  // peräkkäisen hylkäyksen jälkeen viite vaihdetaan, jotta nopea kaasunlisäys
  // ei jää jumiin.
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

    // EMA: alpha = 1 - exp(-dt/tau) antaa tarkan aikavakion myös lyhyellä tau:lla.
    level_alpha_ = dp.level_tau_s > 0.0f ? 1.0f - std::exp(-kSamplePeriodS / dp.level_tau_s) : 1.0f;

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
    // Verhokäyrän vanhenemisikkuna = pisin hyväksyttävä jakso: jos rajaa ei
    // ole tullut näin pitkään aikaan, vanha verhokäyrä ei kuvaa signaalia.
    // Tarkistetaan puolikkaan lohkon välein, joten ikkuna = tasan 2 lohkoa.
    half_window_n_ = static_cast<uint32_t>(0.5f * max_period_) + 1u;
    window_n_ = 2u * half_window_n_;
    // Suotimen asettumisaika (4. kert. Butterworth asettuu 2 %:iin ~1,3/fc:ssä).
    // Ensimmäinen näyte voi osua resonanssin huippuun, jolloin prime() jättää
    // muutaman kPa:n transientin; sen aikana verhokäyrää ei kerätä, muuten
    // ensimmäiset kynnykset olisivat liian leveät ja rajat jäisivät välistä
    // vanhenemisikkunan ajan (~0,5 s).
    settle_n_ = secondsToSamples(2.0f / fc);

    timeout_n_ = secondsToSamples(dp.pulse_timeout_s);
    fault_hold_n_ = secondsToSamples(sp.fault_hold_s);
    min_pulse_p2p_ = kMinPulseHystFactor * dp.hyst_min_kpa;
    reset();
  }

  void reset() {
    n_ = 0;
    for (int i = 0; i < kChannels; ++i) {
      ch_[i] = Chan();
      ch_[i].lp = lp_proto_;
      ch_[i].fault_run = 0;
      ch_[i].since_sat = kSatWindowSamples;
    }
  }

  // 1 kHz. kpa = kalibroitu paine per kanava, raw = alkuperäinen kehys (mV
  // vikamonitoreille). O(1) per kanava; syklirajalla O(avg_cycles + rpm_hist).
  void push(const float (&kpa)[kChannels], const RawFrame& raw) {
    ++n_;
    for (int i = 0; i < kChannels; ++i) pushChannel(ch_[i], kpa[i], raw.mv[i]);
  }

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

 private:
  struct Chan {
    ChannelState pub;
    ButterLp4 lp;
    bool primed = false;
    uint32_t settle = 0;  // näytteitä jäljellä suotimen asettumiseen
    float x_prev = 0.0f;  // edellinen kalibroitu näyte (trapetsi-integraaliin)
    float y_prev = 0.0f;  // edellinen suodatettu näyte (rajan interpolointiin)

    // Verhokäyrä ja Schmitt. env_* määrää kynnykset, win_* kerää kuluvan
    // syklin ääriarvot (-> min_kpa, p2p_kpa ja seuraavan syklin env), blk_*
    // kerää vanhenemislohkon ääriarvot.
    float env_hi = 0.0f, env_lo = 0.0f;
    float win_hi = 0.0f, win_lo = 0.0f;
    float blk_hi = 0.0f, blk_lo = 0.0f;
    uint32_t blk_len = 0;
    uint32_t since_boundary = 0;
    bool armed = false;

    // Sykliketju: edellinen raja (kokonais- + murto-osa näytteinä).
    bool chain = false;
    uint32_t b_int = 0;
    float b_frac = 0.0f;
    float acc = 0.0f;            // suodattamaton integraali edellisestä rajasta, kPa*näyte
    float start_env_p2p = 0.0f;  // verhokäyrän p2p syklin alussa (kEnvStableFrac)
    float ref_period = 0.0f;     // viimeisin hyväksytty jakso, 0 = ei viitettä
    uint8_t ratio_rejects = 0;
    uint32_t since_cycle = 0;    // näytteitä viimeisestä hyväksytystä syklistä

    // Syklihistoria. integral/period erikseen, jotta keskiarvo on aikapainotettu
    // eli täsmälleen avg_cycles kokonaisen syklin yli (D1).
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
      c.env_hi = c.env_lo = x;
      c.win_hi = c.win_lo = x;
      c.blk_hi = c.blk_lo = x;
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
      --c.settle;
      c.env_hi = c.env_lo = c.win_hi = c.win_lo = c.blk_hi = c.blk_lo = y;
      c.x_prev = x;
      c.y_prev = y;
      return;
    }

    // Verhokäyrä laajenee heti (käynnistys, syvenevä pulssi), mutta kutistuu
    // vain syklirajalla tai vanhenemislohkossa - siksi kynnysten keskikohta
    // liikkuu hitaasti eikä seuraa yksittäistä pulssia.
    if (y > c.env_hi) c.env_hi = y;
    if (y < c.env_lo) c.env_lo = y;
    if (y > c.win_hi) c.win_hi = y;
    if (y < c.win_lo) c.win_lo = y;
    if (y > c.blk_hi) c.blk_hi = y;
    if (y < c.blk_lo) c.blk_lo = y;

    // Keskikohta = verhokäyrän puoliväli, EI signaalin keskiarvo: imupulssi on
    // kapea (~25 % syklistä), joten keskiarvo on lähellä yläreunaa, ja
    // ristivuodon pienet pulssit (xtalk 0,3) ylittäisivät sen. Puolivälistä
    // laskettuna alakynnys on 0,65 x p2p yläreunan alapuolella, vuotopulssi
    // vain ~0,3 x p2p -> kierrosluku ei kolminkertaistu.
    const float env_p2p = c.env_hi - c.env_lo;
    float h = dp_.hyst_frac * env_p2p;
    if (h < dp_.hyst_min_kpa) h = dp_.hyst_min_kpa;
    const float center = 0.5f * (c.env_hi + c.env_lo);
    const float thr_lo = center - 0.5f * h;
    const float thr_hi = center + 0.5f * h;

    if (!c.armed) {
      if (y > thr_hi) c.armed = true;
      c.acc += area;
    } else if (y < thr_lo) {
      // Syklin raja = suodatetun signaalin laskeva ylitys. Ylityshetki
      // interpoloidaan näytteiden väliin, ja integraali katkaistaan samaan
      // kohtaan lineaarisesti interpoloidusta signaalista. Ilman tätä raja
      // pyöristyisi ±0,5 ms, mikä 6000 rpm:ssä (20 ms sykli, pulssin pohjalla
      // ~10 kPa keskiarvosta) on ±0,25 kPa per sykli.
      float f = 1.0f;
      const float dy = c.y_prev - y;
      if (dy > 0.0f) f = (c.y_prev - thr_lo) / dy;
      if (f < 0.0f) f = 0.0f;
      if (f > 1.0f) f = 1.0f;
      const float part = f * c.x_prev + 0.5f * f * f * (x - c.x_prev);
      onBoundary(c, f, c.acc + part, y);
      c.acc = area - part;
      c.armed = false;
    } else {
      c.acc += area;
    }

    // Verhokäyrän vanheneminen: jos rajaa ei ole tullut pisimmän sallitun
    // jakson aikana, verhokäyrä (esim. moottorin käynnistyksen askel
    // ilmanpaineesta -55 kPa:iin tai pudonnut pulssin syvyys) ei enää kuvaa
    // signaalia. Uusi verhokäyrä otetaan vain viimeisestä puolikkaasta, jotta
    // itse askel ei jää siihen.
    if (c.since_boundary < 0xFFFFFFFFu) ++c.since_boundary;
    if (++c.blk_len >= half_window_n_) {
      if (c.since_boundary >= window_n_) {
        c.env_hi = c.win_hi = c.blk_hi;
        c.env_lo = c.win_lo = c.blk_lo;
        c.chain = false;  // näin pitkä väli ei ole kelvollinen jakso
        c.acc = 0.0f;
      }
      c.blk_hi = c.blk_lo = y;
      c.blk_len = 0;
    }

    c.x_prev = x;
    c.y_prev = y;
  }

  void onBoundary(Chan& c, float f, float integral, float y) {
    // Raja on välillä [n-1, n]: kokonaisosa n-1, murto-osa f. Jakso lasketaan
    // kokonaislukuerotuksena (kääntyy oikein uint32-ylivuodossa ~49 vrk:n
    // jälkeen) plus murto-osien erotus - float-aikaleima menettäisi
    // tarkkuutensa jo tunneissa.
    const uint32_t b_int = n_ - 1u;
    const float cyc_p2p = c.win_hi - c.win_lo;
    if (c.chain) {
      const float period = static_cast<float>(b_int - c.b_int) + (f - c.b_frac);
      const bool ok_period = period >= min_period_ && period <= max_period_;
      const bool ok_amp = cyc_p2p >= min_pulse_p2p_;
      const bool ok_env = c.start_env_p2p >= kEnvStableFrac * cyc_p2p &&
                          cyc_p2p >= kEnvStableFrac * c.start_env_p2p;
      if (ok_period && ok_amp && ok_env) {
        const bool ok_ratio = c.ref_period <= 0.0f ||
                              (period <= kMaxPeriodRatio * c.ref_period &&
                               period >= kMinPeriodRatio * c.ref_period);
        if (ok_ratio || c.ratio_rejects >= kMaxRatioRejects) {
          c.ref_period = period;
          c.ratio_rejects = 0;
          recordCycle(c, integral, period, c.win_lo, cyc_p2p);
        } else {
          ++c.ratio_rejects;
        }
      }
    }
    // Seuraavan syklin kynnykset juuri päättyneen syklin ääriarvoista: ikkuna
    // [edellinen raja, tämä raja] sisältää tasan yhden pulssin pohjan ja
    // yhden yläreunan.
    c.env_hi = c.win_hi;
    c.env_lo = c.win_lo;
    c.start_env_p2p = cyc_p2p;
    c.win_hi = c.win_lo = y;
    c.blk_hi = c.blk_lo = y;
    c.blk_len = 0;
    c.since_boundary = 0;
    c.b_int = b_int;
    c.b_frac = f;
    c.chain = true;
  }

  void recordCycle(Chan& c, float integral, float period, float cmin, float cp2p) {
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
  float level_alpha_ = 1.0f;
  int avg_n_ = 8;
  int rpm_hist_n_ = 16;
  int min_rpm_periods_ = 8;
  float min_period_ = 0.0f;  // näytteinä
  float max_period_ = 0.0f;
  uint32_t settle_n_ = 1;
  uint32_t window_n_ = 1;
  uint32_t half_window_n_ = 1;
  uint32_t timeout_n_ = 1;
  uint32_t fault_hold_n_ = 1;
  float min_pulse_p2p_ = 0.0f;
  uint32_t n_ = 0;  // kuluvan näytteen indeksi (yhteinen kaikille kanaville)
  Chan ch_[kChannels];
};

}  // namespace sbx
