#pragma once
// Suckboxx core - moottorisimulaattori (WP-1, D14 "simulaattori ensin").
//
// Tuottaa samaa RawFrame-dataa (mV ADC-pinnissä, 1 kHz) kuin oikea rauta:
// 3-sylinterisen nelitahtimoottorin imukanavan alipaine letkun ja
// XGZP6847A100KPGN-anturin läpi nähtynä. Data kulkee saman kalibrointi-,
// DSP- ja vikatunnistuspolun kuin rauta, joten totuusarvot (trueMeanKpa,
// trueRpm) tekevät käytösmatriisin (§8) riveistä automaattisia testejä.
//
// Malli (docs/firmware.md §4.1):
//  - Kampi: yksi sykli = 720° = 120/rpm s. Vaihe 0..1 kattaa syklin, ja
//    sylinterin c imutahti alkaa vaiheessa c/3 (240° välein). reset() aloittaa
//    vaiheesta 0 = SYL 1:n imun alku.
//  - Pulssimuoto määritellään vaihetasossa, ja sen keskiarvo syklin yli on
//    analyyttisesti nolla. Syklin keskiarvo on siksi tasan base + sync_err -
//    DSP:n D1-luvun totuus - ennen anturivirheitä ja leikkautumista.
//    Aikatasossa yksittäinen sykli poikkeaa tästä hiukan, kun kampi ei pyöri
//    tasaisesti: jitter venyttää 240°:n välejä eri tavoin (odotusarvo silti
//    tasan totuus, oletusjitterillä hajonta ~0,035 kPa/sykli), ja drift muuttaa
//    nopeutta syklin aikana (~ -0,016 kPa @ 100 rpm/s, 850 rpm). Oikea moottori
//    tekee saman, ja molemmat ovat selvästi alle D2:n 1,5 kPa:n.
//  - Letkuresonanssi on vaimeneva värähtely, jonka summa on myös nolla, joten
//    se ei siirrä keskiarvoa (vain kohinaa yksittäiseen sykliin).
//  - Ristivuoto (D6 tasauskanavat), anturivirheet (R4), GN-anturin leikkautuminen
//    (D4/H1), ADC-kohina ja vikatilat (rivit 6 ja 7).
//
// Laskenta per näyte: enintään 3 sinf + 12 xorshift-kierrosta + kolme
// kompleksikertolaskua, sytytysvälin rajalla lisäksi yksi expf. Ei
// muistinvarausta, ei historiasilmukoita (PC:llä ~50 ns/näyte).

#include <cmath>
#include <cstdint>

#include "types.h"

namespace sbx {

enum class SimFault : uint8_t { None = 0, HoseOff = 1, SensorLow = 2, SensorHigh = 3 };

struct SimParams {
  bool engine_on = true;
  float rpm = 850.0f;
  float rpm_jitter = 0.01f;        // sykli-sykliltä jaksonajan suhteellinen keskihajonta
  float rpm_drift = 0.0f;          // rpm/s (rivi 5)
  float base_kpa = -55.0f;         // keskimääräinen alipaine (gauge)
  float sync_err_kpa[kChannels] = {0.0f, 0.0f, 0.0f};  // kanavan keskiarvon poikkeama = synkronointivirhe
  float depth_kpa = 20.0f;         // imupulssin syvyys
  float intake_frac = 0.25f;       // imutahdin osuus syklistä (180°/720°)
  float res_hz[kChannels] = {112.0f, 112.0f, 112.0f};  // letkuresonanssi (docs/rauta.md §4.1)
  float res_kpa = 3.0f;            // resonanssin herätesamplitudi
  float res_decay_s = 0.02f;
  float xtalk = 0.0f;              // muiden sylinterien pulssien vuoto 0..1 (tasauskanavat, D6)
  float peak_kpa = 0.0f;           // positiivinen paluuaalto (rivi 10 / H1)
  float noise_mv = 2.0f;           // ADC-kohinan keskihajonta
  float sensor_offset_kpa[kChannels] = {0.0f, 0.0f, 0.0f};  // anturivirhe (R4)
  float sensor_gain_err[kChannels] = {0.0f, 0.0f, 0.0f};    // suhteellinen, 0.02 = +2 %
  SimFault fault[kChannels] = {SimFault::None, SimFault::None, SimFault::None};
  uint32_t seed = 1;
};

class EngineSim {
 public:
  // Nimellis-rpm leikataan tälle välille. Alaraja jättää tilaa testata DSP:n
  // rpm_min-hylkäystä (300) ja sammumista; yläraja pitää syklissä vähintään
  // 10 näytettä (§2.2).
  static constexpr float kRpmMin = 60.0f;
  static constexpr float kRpmMax = 12000.0f;
  // XGZP6847A100KPGN mittaa −100…0 kPa. GN-versio ei mittaa ilmanpainetta
  // korkeampaa painetta, joten ulostulo litistyy ylhäältä v_zero_mv:hen (D4, H1)
  // ja alhaalta −100 kPa:n kohtaan. Oletusparametreilla 200…2700 mV.
  static constexpr float kSensorSpanKpa = 100.0f;
  static constexpr float kAdcMaxMv = 3300.0f;
  // Anturivika (rivi 7): katkennut tai oikosuljettu ulostulo jää ADC:n laitaan,
  // reilusti fault_lo_mv/fault_hi_mv -rajojen ulkopuolelle (oletus 150 / 2950 mV,
  // D15; huoltosivun ääriarvot 50 / 3250 mV, config.h).
  static constexpr float kSensorLowMv = 10.0f;
  static constexpr float kSensorLowMaxMv = 30.0f;
  static constexpr float kSensorHighMv = 3290.0f;
  static constexpr float kSensorHighMinMv = 3270.0f;

  EngineSim() {
    derive();
    reset();
  }

  // Ei nollaa vaihetta: UI muuttaa parametreja lennossa, eikä aaltomuotoon saa
  // tulla hyppyä. Poikkeus: jos resetin jälkeen ei ole vielä otettu yhtään
  // näytettä, aloitetaan puhtaalta pöydältä uusilla parametreilla (seed mukaan
  // lukien), jolloin "luo + configure" tuottaa saman datan kuin
  // "luo + configure + reset". Muuten uusi seed tulee voimaan vasta reset():ssä.
  //
  // Kierrosluku ja drift (rivi 5): params().rpm on asetusarvo, josta drift
  // lähtee; hetkellinen arvo on trueRpm(). p.rpm on uusi asetusarvo, jos se
  // poikkeaa params().rpm:stä tai rpm_set = true. Pelkkä driftin muutos jatkaa
  // nykyisestä kierrosluvusta ja kirjaa sen myös params().rpm:ään: driftin
  // pysäytyksen jälkeen GET /api/sim näyttää kierrosluvun, jossa simulaattori
  // oikeasti pysyy, ja paluu alkuperäiseen (esim. 850) on muutos eikä ohitu
  // hiljaa. Ohjelmallinen kutsuja, joka haluaa pitää pysäytetyn kierrosluvun,
  // lähtee siksi params():sta eikä omasta vanhasta kopiostaan.
  // rpm_set on UI:n POSTia varten, kun rpm-avain on mukana (vrt. mock_server.py
  // rpm_posted): kesken driftin "drift=0&rpm=850" ei muuten erottuisi pelkästä
  // "drift=0":sta, koska params().rpm on yhä driftin lähtöarvo 850.
  void configure(const SimParams& p, const SensorParams& s, bool rpm_set = false) {
    // Kierrosluku lasketaan vanhoilla parametreilla ennen vaihtoa, jotta pelkkä
    // driftin muutos jatkaa nykyisestä kierrosluvusta eikä hyppää.
    const float rpm_now = rpmAt(rpm_n_);
    const bool rpm_changed = rpm_set || !(p.rpm == p_.rpm);
    const bool drift_changed = !(p.rpm_drift == p_.rpm_drift);
    p_ = p;
    s_ = s;
    derive();
    if (fresh_) {
      reset();
      return;
    }
    if (rpm_changed) {
      anchor_rpm_ = clampRpm(p_.rpm);  // käyttäjä asetti uuden kierrosluvun
      rpm_n_ = 0;
    } else if (drift_changed) {
      anchor_rpm_ = rpm_now;
      p_.rpm = rpm_now;  // uusi asetusarvo = kierrosluku, jossa ollaan nyt
      rpm_n_ = 0;
    }
  }

  const SimParams& params() const { return p_; }

  // Vaihe, aika, seq ja PRNG alkuun (PRNG p.seed:stä).
  void reset() {
    rng_ = seedState(p_.seed);
    seq_ = 0;
    phase_ = 0.0f;
    seg_ = 0;
    anchor_rpm_ = clampRpm(p_.rpm);
    rpm_n_ = 0;
    for (int c = 0; c < kChannels; ++c) {
      res_re_[c] = 0.0f;
      res_im_[c] = 0.0f;
    }
    seg_factor_ = drawSegmentFactor();
    excite(0);  // vaihe 0 on SYL 1:n imun alku
    fresh_ = true;
  }

  // Seuraava 1 kHz näyte. seq kasvaa yhdellä, ensimmäinen resetin jälkeen on 0.
  RawFrame next() {
    RawFrame f;
    f.seq = seq_++;
    f.rail_mv = 0;  // simulaattori ei mallinna anturikiskoa (R6): "ei mitattu"
    fresh_ = false;

    const bool on = p_.engine_on;
    float rpm = 0.0f;
    float shape[kChannels] = {0.0f, 0.0f, 0.0f};
    float shape_sum = 0.0f;
    if (on) {
      rpm = rpmAt(rpm_n_);
      for (int c = 0; c < kChannels; ++c) {
        shape[c] = pulseShape(localPhase(c));
        shape_sum += shape[c];
      }
    }

    for (int c = 0; c < kChannels; ++c) {
      // Kohina arvotaan aina, myös vikatiloissa ja noise_mv = 0:lla, jotta PRNG:n
      // kulutus ei riipu parametreista: kaksi ajoa, jotka eroavat vain esim.
      // resonanssin osalta, pysyvät näytteittäin vertailukelpoisina (testit).
      const float g = gauss();
      float mv;
      switch (p_.fault[c]) {
        case SimFault::SensorLow:
          mv = clampf(kSensorLowMv + noise_mv_ * g, 0.0f, kSensorLowMaxMv);
          break;
        case SimFault::SensorHigh:
          mv = clampf(kSensorHighMv + noise_mv_ * g, kSensorHighMinMv, kAdcMaxMv);
          break;
        default: {
          // Letku irti (rivi 6): anturi näkee ilmanpaineen, ei pulsseja eikä resonanssia.
          float kpa = 0.0f;
          if (on && p_.fault[c] != SimFault::HoseOff) {
            kpa = p_.base_kpa + p_.sync_err_kpa[c] + shape[c] +
                  xtalk_ * (shape_sum - shape[c]) + res_im_[c];
          }
          const float measured = gain_[c] * kpa + offset_[c];  // R4: anturin oma virhe
          mv = clampf(s_.v_zero_mv + s_.mv_per_kpa * measured, sensor_lo_mv_, sensor_hi_mv_);
          mv = clampf(mv + noise_mv_ * g, 0.0f, kAdcMaxMv);  // ADC-kohina anturin jälkeen
          break;
        }
      }
      f.mv[c] = static_cast<uint16_t>(mv + 0.5f);
    }

    rotateResonators();
    if (on) {
      advancePhase(rpm);  // voi herättää resonanssin -> näkyy seuraavassa näytteessä
      ++rpm_n_;           // sammuksissa kierrosluku ja drift jäätyvät
    }
    return f;
  }

  // Totuus D1-luvulle: base + sync_err (ennen anturivirheitä). 0 jos moottori on
  // sammuksissa tai letku irti. Anturivioissa paine on yhä olemassa, joten totuus
  // pysyy base + sync_err:nä, vaikka anturi ei sitä näe.
  float trueMeanKpa(int ch) const {
    if (ch < 0 || ch >= kChannels) return 0.0f;
    if (!p_.engine_on || p_.fault[ch] == SimFault::HoseOff) return 0.0f;
    return p_.base_kpa + p_.sync_err_kpa[ch];
  }

  // Seuraavan näytteen nimellis-rpm (drift huomioiden, ilman jitteriä). N näytteen
  // jälkeen viimeisestä ankkuroinnista: params().rpm + rpm_drift * N / 1000,
  // rajattuna kRpmMin..kRpmMax. 0 kun moottori on sammuksissa.
  float trueRpm() const { return p_.engine_on ? rpmAt(rpm_n_) : 0.0f; }

 private:
  static constexpr float kPi = 3.14159265f;
  static constexpr float kIntakeFracMin = 0.05f;
  static constexpr float kIntakeFracMax = 0.6f;
  // Paluuaalto (imuventtiilin sulkeutuminen) on lyhyt isku heti imun jälkeen:
  // leveys puolet imutahdista. Imu + aalto mahtuu aina sykliin (0.6 * 1.5 < 1).
  static constexpr float kBumpFrac = 0.5f;
  // Lognormaalin kertoimen 3.46σ-häntä (Irwin-Hall) pysyy tämän sisällä jitter-
  // rajalla 0.4, eli leikkaus on vain turvaverkko eikä vääristä hajontaa.
  static constexpr float kJitterMax = 0.4f;
  static constexpr float kSegFactorMin = 0.1f;
  static constexpr float kSegFactorMax = 10.0f;
  static constexpr int kMaxBoundariesPerSample = 8;
  static constexpr float kResHzMax = 490.0f;  // Nyquistin alapuolella (fs = 1 kHz)
  static constexpr float kResKpaMax = 50.0f;
  // Letkun soiminen vaimenee millisekunneissa (oletus 20 ms). 1 s:n yläraja pitää
  // |ρ| ≤ e^-0.001, jolloin herätteen normitus |1 - ρ| ≥ 0.001 ei voi olla 0/0,
  // ja resonanssiin osuvakin herätesarja kertyy enintään amp/(1 - |ρ|):ään.
  static constexpr float kResDecayMaxS = 1.0f;
  static constexpr float kResMagMin = 1e-6f;
  static constexpr float kNoiseMvMax = 1000.0f;
  static constexpr float kResFlush = 1e-6f;   // denormaalien välttämiseksi
  // Turvaverkko: rajatuilla parametreilla tila jää alle amp/(1 - |ρ|) ≈ 5e4 kPa,
  // vaikka heräte tulisi joka näytteellä. Tätä suurempi, inf tai NaN nollataan,
  // ettei yksi huono arvo jää kiertämään, koska configure() ei koske
  // resonaattorien tilaan.
  static constexpr float kResStateMax = 1e6f;
  // Sytytysvälien rajat vaiheessa. Sama float-vakio sekä rajana että sylinterin
  // vaihesiirtona, jotta paikallinen vaihe on tasan 0 imun alussa.
  static constexpr float kSegStart[kChannels] = {0.0f, 1.0f / 3.0f, 2.0f / 3.0f};
  static constexpr float kSegEnd[kChannels] = {1.0f / 3.0f, 2.0f / 3.0f, 1.0f};

  // NaN-turvallinen rajaus: NaN -> lo, jotta mikään parametri ei päädy
  // määrittelemättömään float->int-muunnokseen.
  static float clampf(float v, float lo, float hi) {
    if (!(v >= lo)) return lo;
    if (v > hi) return hi;
    return v;
  }
  static float clampRpm(float rpm) { return clampf(rpm, kRpmMin, kRpmMax); }

  // Pieni seed (1, 2, ...) sekoitetaan (murmur3 fmix32), jotta peräkkäiset
  // seedit eroavat heti eivätkä vasta kymmenien kierrosten jälkeen.
  static uint32_t seedState(uint32_t seed) {
    uint32_t h = seed + 0x9E3779B9u;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h != 0 ? h : 0x6D2B79F5u;  // xorshift ei saa olla nollatilassa
  }

  uint32_t nextU32() {
    uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return x;
  }

  // [0, 1), 24 bittiä = floatin mantissa.
  float uniform() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }

  // Normaalijakauma Irwin-Hallina (4 tasajakaumaa, var 1/3 -> skaalaus √3): halpa
  // ilman logf/cosf:ää ja rajattu ±3.46σ:aan, joten kohina ei koskaan yksinään
  // ylitä vikarajoja (rivi 7).
  float gauss() {
    const float s = uniform() + uniform() + uniform() + uniform();
    return (s - 2.0f) * 1.7320508f;
  }

  // Yhteinen kampi: jaksonajan vaihtelu arvotaan jokaiselle 240°:n sytytysvälille
  // erikseen ja riippumattomasti. Jokaisen sylinterin 720°:n jakso on kolmen
  // peräkkäisen välin summa, joten kaikilla kolmella kanavalla jakson suhteellinen
  // keskihajonta on tasan rpm_jitter, kun yhden välin hajonta on √3·jitter.
  // (Yksi arvonta per sykli SYL 1:n imun kohdalla antaisi SYL 2:lle ja 3:lle vain
  // √5/3 ≈ 0.75 -kertaisen hajonnan, koska niiden jakso jakautuisi kahdelle syklille.)
  // Lognormaali kerroin pysyy aina positiivisena ja sen odotusarvo on 1, joten
  // keskimääräinen jakso on tasan 120/rpm.
  float drawSegmentFactor() {
    const float g = gauss();  // aina, jotta PRNG-virta ei riipu jitteristä
    return clampf(std::exp(seg_sigma_ * g - seg_half_var_), kSegFactorMin, kSegFactorMax);
  }

  float rpmAt(uint32_t n) const {
    return clampRpm(anchor_rpm_ + p_.rpm_drift * static_cast<float>(n) * kSamplePeriodS);
  }

  float localPhase(int c) const {
    float psi = phase_ - kSegStart[c];
    if (psi < 0.0f) psi += 1.0f;
    return psi;
  }

  // Yhden sylinterin pulssi paikallisessa vaiheessa psi (0 = imun alku).
  // sin² on C¹-jatkuva päistään, joten näytteistetty summa syklin yli osuu
  // integraaliin tarkasti. sin²:n keskiarvo on 1/2, joten keskiarvo on
  // analyyttinen eikä numeerinen - D1-totuus on tasan base + sync_err.
  float pulseShape(float psi) const {
    float v = 0.0f;
    if (psi < w_) {
      const float s = std::sin(kPi * psi * inv_w_);
      v = -depth_ * s * s;
    } else if (peak_ > 0.0f && psi < w_ + wb_) {
      const float s = std::sin(kPi * (psi - w_) * inv_wb_);
      v = peak_ * s * s;
    }
    return v - shape_mean_;
  }

  // Resonaattori kompleksilukuna z: joka näyte z *= ρ = r·e^{iωT}, ulostulo Im(z).
  // Inkrementaalinen kierto on stabiili (|ρ| < 1) eikä vaadi sinf:ää per näyte.
  void rotateResonators() {
    for (int c = 0; c < kChannels; ++c) {
      const float re = res_re_[c] * rot_re_[c] - res_im_[c] * rot_im_[c];
      const float im = res_re_[c] * rot_im_[c] + res_im_[c] * rot_re_[c];
      const float m = std::fabs(re) + std::fabs(im);
      // Käänteinen vertailu: NaN ei läpäise kumpaakaan ehtoa, joten se nollautuu.
      if (!(m >= kResFlush && m <= kResStateMax)) {
        res_re_[c] = 0.0f;
        res_im_[c] = 0.0f;
      } else {
        res_re_[c] = re;
        res_im_[c] = im;
      }
    }
  }

  void excite(int c) {
    res_re_[c] += exc_re_[c];
    res_im_[c] += exc_im_[c];
  }

  void advancePhase(float rpm) {
    // Nimellisvaihetta (sykleinä) jäljellä tällä näytevälillä; kuluvan välin kerroin
    // venyttää (> 1) tai lyhentää (< 1) sitä.
    float left = kSamplePeriodS * rpm * (1.0f / 120.0f);
    for (int guard = 0; guard < kMaxBoundariesPerSample; ++guard) {
      const float end = kSegEnd[seg_];
      const float step = left / seg_factor_;
      if (phase_ + step < end) {
        phase_ += step;
        return;
      }
      // Raja ylittyy kesken näytevälin: loppuosa kuluu seuraavan välin nopeudella,
      // jolloin jaksonaika on tarkka eikä kvantisoidu näytteisiin.
      left -= (end - phase_) * seg_factor_;
      if (left < 0.0f) left = 0.0f;
      seg_ = (seg_ + 1) % kChannels;
      phase_ = kSegStart[seg_];
      seg_factor_ = drawSegmentFactor();
      excite(seg_);  // sylinterin seg_ imu alkaa -> letku soi
    }
  }

  void derive() {
    w_ = clampf(p_.intake_frac, kIntakeFracMin, kIntakeFracMax);
    wb_ = kBumpFrac * w_;
    inv_w_ = 1.0f / w_;
    inv_wb_ = 1.0f / wb_;
    depth_ = clampf(p_.depth_kpa, 0.0f, kSensorSpanKpa * 2.0f);
    peak_ = clampf(p_.peak_kpa, 0.0f, kSensorSpanKpa * 2.0f);
    // Kuopan ala -depth·w/2, paluuaallon ala +peak·wb/2.
    shape_mean_ = 0.5f * (peak_ * wb_ - depth_ * w_);
    xtalk_ = clampf(p_.xtalk, 0.0f, 1.0f);
    noise_mv_ = clampf(p_.noise_mv, 0.0f, kNoiseMvMax);

    const float j = clampf(p_.rpm_jitter, 0.0f, kJitterMax);
    // Välin suhteellinen varianssi 3·j² lognormaalina: σ² = ln(1 + 3j²).
    seg_sigma_ = std::sqrt(std::log1p(3.0f * j * j));
    seg_half_var_ = 0.5f * seg_sigma_ * seg_sigma_;

    const float amp = clampf(p_.res_kpa, 0.0f, kResKpaMax);
    const float decay = clampf(p_.res_decay_s, 0.0f, kResDecayMaxS);
    for (int c = 0; c < kChannels; ++c) {
      const float hz = clampf(p_.res_hz[c], 0.0f, kResHzMax);
      rot_re_[c] = 0.0f;
      rot_im_[c] = 0.0f;
      exc_re_[c] = 0.0f;
      exc_im_[c] = 0.0f;
      if (hz > 0.0f && decay > 0.0f && amp > 0.0f) {
        const float r = std::exp(-kSamplePeriodS / decay);
        const float wt = 2.0f * kPi * hz * kSamplePeriodS;
        // Herätteen vaihe θ = arg(1 - ρ): silloin yhden herätteen koko vaste
        // Σ Im(E·ρ^k) = Im(E / (1 - ρ)) = 0, eli resonanssi ei siirrä syklin
        // keskiarvoa edes diskreetissä ajassa. Samalla ensimmäinen näyte on
        // negatiivinen (~ -0.9·amp): paine putoaa, kun imuventtiili aukeaa.
        const float rre = r * std::cos(wt);
        const float rim = r * std::sin(wt);
        const float dre = 1.0f - rre;
        const float dim = -rim;
        const float mag = std::sqrt(dre * dre + dim * dim);
        if (mag > kResMagMin) {  // decay-rajan ansiosta aina; vain varmistus
          rot_re_[c] = rre;
          rot_im_[c] = rim;
          exc_re_[c] = amp * dre / mag;
          exc_im_[c] = amp * dim / mag;
        }
      }
      gain_[c] = 1.0f + p_.sensor_gain_err[c];
      offset_[c] = p_.sensor_offset_kpa[c];
    }

    // GN-anturin ulostuloalue SensorParamsista: 3,3 V -versio 200…2700 mV,
    // 5 V -versio jakajan jälkeen 300…2700 mV (docs/rauta.md §1.6).
    const float a = s_.v_zero_mv - kSensorSpanKpa * s_.mv_per_kpa;
    const float b = s_.v_zero_mv;
    sensor_lo_mv_ = a < b ? a : b;
    sensor_hi_mv_ = a < b ? b : a;
  }

  SimParams p_;
  SensorParams s_;

  // Tila
  uint32_t rng_ = 1;
  uint32_t seq_ = 0;
  float phase_ = 0.0f;       // 0..1 syklistä, 0 = SYL 1:n imun alku
  int seg_ = 0;              // kuluva 240°:n väli = sylinteri, jonka imu alkoi viimeksi
  float seg_factor_ = 1.0f;  // kuluvan välin kestokerroin (jitter)
  float anchor_rpm_ = 850.0f;
  uint32_t rpm_n_ = 0;       // näytteitä ankkurista (drift)
  float res_re_[kChannels] = {0.0f, 0.0f, 0.0f};
  float res_im_[kChannels] = {0.0f, 0.0f, 0.0f};
  bool fresh_ = true;        // ei näytteitä resetin jälkeen

  // Johdetut (derive)
  float w_ = 0.25f, wb_ = 0.125f, inv_w_ = 4.0f, inv_wb_ = 8.0f;
  float depth_ = 0.0f, peak_ = 0.0f, shape_mean_ = 0.0f;
  float xtalk_ = 0.0f, noise_mv_ = 0.0f;
  float seg_sigma_ = 0.0f, seg_half_var_ = 0.0f;
  float rot_re_[kChannels] = {0.0f, 0.0f, 0.0f};
  float rot_im_[kChannels] = {0.0f, 0.0f, 0.0f};
  float exc_re_[kChannels] = {0.0f, 0.0f, 0.0f};
  float exc_im_[kChannels] = {0.0f, 0.0f, 0.0f};
  float gain_[kChannels] = {1.0f, 1.0f, 1.0f};
  float offset_[kChannels] = {0.0f, 0.0f, 0.0f};
  float sensor_lo_mv_ = 200.0f, sensor_hi_mv_ = 2700.0f;
};

}  // namespace sbx
