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
// Mitatut rajat (oletusparametrit; test_dsp ja EngineSim-ajot):
//  - Lukitus (has_pulse) käyvään signaaliin tai moottorin käynnistyessä:
//    <= 3,5 sykliä + suotimen asettuminen 37,5 ms. Mitattu pahin: 600 rpm
//    650 ms (3,3 sykliä), 850 rpm 470 ms (3,3), 1500 rpm 285 ms (3,6),
//    3000 rpm 165 ms (4,1 - asettuminen on siellä ~1 sykli).
//  - Pienin havaittava imupulssi (EngineSim: sin^2, intake_frac 0,25, 112 Hz:n
//    3 kPa:n resonanssi, 2 mV kohina): 850-1500 rpm 1-1,5 kPa, 3000 rpm 2,
//    4500 rpm 2,5 ja 6000 rpm 4,5 kPa; xtalk 0,3:lla 3000 rpm 3, 4500 rpm
//    3,5 ja 6000 rpm 6,5 kPa. Rajana on suodatettu huippu-huippu ~0,5-0,7
//    kPa (hyst_min_kpa-lattia ja skaalattu kohinaraja, ks. ampOk). Suurilla
//    kierroksilla 40 Hz:n alipäästö vaimentaa perustaajuutta (50 Hz -> 0,38x)
//    ja ristivuoto vielä (1 - xtalk):lla. 90 Hz:n resonanssilla (1 m letku)
//    6000 rpm vaatii ~8 kPa, koska resonanssi on jo lähellä perustaajuutta.
//  - Alipaineen ramppi (kaasun liike) ei maksa yhtään sykliä. Hetkellinen
//    askel (/api/sim) tai pulssin puolittuminen maksaa enintään 2 sykliä
//    (väli ~3 jaksoa); has_pulse ei putoa, eikä väärää jaksoa hyväksytä.
//  - Kohinasuoja: hyst_min_kpa 0,3 kestää ~0,4 kPa RMS raakakohinan ilman
//    yhtään väärää sykliä; 0,6-0,8 kPa:n kohina vaatii ~0,5 (S5a).
//  - Positiivinen paluuaalto (EngineSim peak 0-60 kPa, depth 20, base -55 ja
//    -30 eli myös leikkautuvana, 600-3000 rpm): rpm-hajonta ~1-2 % (jitter 1
//    %) kaikilla 16 siemenellä joutokäyntialueella; ks. kPlateauBand (S2).

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
  // muuttaa pulssin syvyyttä 20-30 % per sykli), ja jakson mukaisilta
  // sykleiltä (kLockPeriodWindow) yhtäpitävyyttä ei vaadita lainkaan. 0,5
  // erottaa yhä vuotopulssin (xtalk 0,3 -> 0,3 x oma pulssi) omasta.
  static constexpr float kEnvLockFrac = 0.5f;
  // Lukittuna jakso, joka on tämän suhteellisen ikkunan sisällä viitteestä,
  // päättyy oikean pulssin rajaan, vaikka sykli näyttäisi askeleen takia
  // matalalta tai pulssi syvältä (ks. onBoundary, confirmPending). 10 % =
  // 2 sigmaa 5 %:n jitterillä.
  static constexpr float kLockPeriodWindow = 0.10f;
  // Lukittuna yli 20 % viitteestä poikkeava sykli odottaa seuraavaa rajaa
  // (ks. onBoundary). Väärä raja jakaa oikean syklin kahteen osaan:
  // vuotopulssin raja (pulssi syveni kerralla > 2x ristivuodon kanssa)
  // antaa 2T/3 + T/3, alipaineen askel + vuotopulssi satunnaisen tau + (T -
  // tau). Ilman tätä 2T/3 hyväksyttiin ja rpm hyppäsi ~4 %. Kiihdytyksessä
  // (mitattu jopa 0,58 ja 0,73 per sykli) sykli hyväksytään yhden syklin
  // viiveellä. 20 % = ~3 sigmaa peräkkäisten jaksojen suhteesta 5 %:n
  // jitterillä, joten tavallinen käynti ei odota lainkaan.
  static constexpr float kSplitSuspect = 0.20f;
  // Jaksotrendin toleranssi (confirmPending): kiihdytyksessä ja
  // hidastuksessa peräkkäisten jaksojen suhde muuttuu tasaisesti (mitattu
  // blippauksessa 1,13 -> 1,16 -> 1,19), 1 %:n jitterillä suhde heiluu ~±0,02.
  static constexpr float kTrendTol = 0.06f;
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
  // Lukittuna vanheneminen (1,8 x viite) katkaisee jokaisen sitä pidemmän
  // segmentin, joten pidempää jaksoa ei arvioida koskaan eikä viite voi
  // päivittyä: kierrosluvun pudotus yli 1,8-kertaisesti (kaasun katkaisu
  // 3000 -> 850) jätti kanavan odottamaan pulse_timeoutia (mitattu 1,2 s).
  // Yksi vanheneminen on yksi puuttuva pulssi (seuraava sykli korjaa); toinen
  // peräkkäinen ilman kirjattua sykliä tarkoittaa, että jakso on oikeasti
  // pidentynyt. Silloin lukitus puretaan (viite pois), mutta has_pulse ja
  // historia säilyvät, ja haku lukitsee uuteen jaksoon parissa syklissä.
  static constexpr uint8_t kMaxStaleRolls = 2;
  // Hiljaisuudesta herääminen (pushChannel): hiljaista = heilahdus kahden
  // segmentin yli alle min_pulse_p2p_ yhtäjaksoisesti pisimmän jakson
  // (window_n_) ajan; herääminen = ensimmäinen näyte, jolla heilahdus on
  // sen yli. Kohinan aiheuttama herääminen on vaaraton (pulsseja ei ole).
  // Uusi asettuminen on kStepSettleFactor x tavallinen, koska askeleen
  // soiminen kestää pidempään kuin prime():n jättämä (alitus + paluu ~50 ms
  // @ 40 Hz).
  static constexpr uint32_t kStepSettleFactor = 2;
  // Paluuaallon tasanne (S2, H1). Positiivinen paluuaalto heti imupulssin
  // jälkeen tekee syklistä kaksiportaisen: huippu -> lasku tasanteelle ->
  // pitkä tasanne -> imupulssi pohjaan. Laukaisutaso D = (A + h) / 2 (0,65 x
  // A) on kiinteä osuus huipusta, joten paluuaallon ja pulssin suhteella
  // ~1,5-2,2 tasanne osuu juuri laukaisutasolle. Silloin paluuaallon laskun
  // alitus (suotimen ~10 %:n ylitys tasanteen alle) laukaisi rajan vain
  // osassa sykleistä, ja raja hyppi imupulssin ja paluuaallon laskun välillä
  // (~0,4 T): rpm-hajonta 15-44 %, "Käynti epätasainen" (rivi 2), ei tuomiota.
  // Tasanne tunnistetaan viipymisestä: jos edellisellä raja-raja-välillä
  // signaali oli vähintään kPlateauDwell välistä normaalin laukaisutason
  // +-kPlateauBand x A -kaistalla, laukaisu siirretään tasanteen ja pohjan
  // puoliväliin (imupulssin keskelle), jolloin tasanne on reilusti sen
  // yläpuolella. Tasaisen pulssin reuna ylittää kaistan alle 0,02 jaksossa
  // ja sinimäinen (suodatettu, suuret kierrokset) ~0,16:ssa, tasanne ~0,6:ssa.
  // Kun tasanne on syvemmällä kuin kPlateauMaxRel, imupulssi on liian matala
  // keskikohdan laukaisulle, ja normaali taso laukeaa paluuaallon laskussa
  // vähintään ~0,1 x A tasanteen yläpuolella. Pysymiseen kaista ja raja ovat
  // väljemmät (hystereesi), ettei tila vaihtele: vaihto siirtää rajaa
  // imupulssin ja paluuaallon välillä. Viipyminen lasketaan pushChannelissa
  // (virityksen mukaan), arvio ja tilan vaihto onBoundaryssa. Kaikki viisi
  // ovat S2:n omia ehdotuksia EngineSim-kartoituksesta (peak 0-60, depth
  // 10-30, xtalk 0-0,3, 600-3000 rpm, 8-16 siementä).
  static constexpr float kPlateauBand = 0.12f;
  static constexpr float kPlateauKeepBand = 0.18f;
  static constexpr float kPlateauDwell = 0.3f;
  static constexpr float kPlateauMaxRel = 0.74f;
  static constexpr float kPlateauKeepMaxRel = 0.80f;
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
    // Haussa: näytteitä ilman heilahdusta (ks. kWakeFactor). Hiljaista, kun
    // tämä on window_n_ (pisin jakso): lyhyempi tauko on vain pulssien väli.
    uint32_t quiet_run = 0;
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
    float ref_period = 0.0f;     // viimeisin hyväksytty jakso, 0 = ei viitettä
    float prev_ref = 0.0f;       // sitä edeltävä hyväksytty jakso (trendi, confirmPending)
    uint8_t ratio_rejects = 0;
    uint8_t stale_rolls = 0;     // lukittuna vanhentuneet segmentit viimeisen syklin jälkeen
    bool env_from_cycle = false; // kuluva väli alkoi ketjun toiselta tai myöhemmältä rajalta

    // Ehdokassykli, joka vahvistetaan kun sen päättänyt pulssi on nähty
    // pohjaan asti (ks. confirmPending).
    bool pending = false;
    bool pend_rerate = false;    // uusi viite (jakso toistaa edellisen välin, ks. onBoundary)
    float pend_int = 0.0f, pend_per = 0.0f, pend_min = 0.0f, pend_amp = 0.0f;
    float fire_top = 0.0f;       // huippu ennen viimeisintä rajaa (pulssin syvyys virityksessä)
    uint32_t since_cycle = 0;    // näytteitä viimeisestä hyväksytystä syklistä
    // Vahvistettu mutta viitteestä paljon poikkeava sykli odottaa seuraavaa
    // rajaa (kSplitSuspect). tent_dip = sen päättäneen pulssin syvyys.
    bool tent = false;
    float tent_int = 0.0f, tent_per = 0.0f, tent_min = 0.0f, tent_amp = 0.0f, tent_dip = 0.0f;

    // Paluuaallon tasanne (ks. kPlateauBand): kuluvan raja-raja-välin näytteet
    // normaalin laukaisutason kaistalla ja niiden suhteellisten syvyyksien
    // (run_hi - y) / A summa. plat_r = tasanteen suhteellinen syvyys
    // huipusta, 0 = ei tasannetta (normaali laukaisutaso). dwell_dis = niistä
    // virityksen ulkopuolella (tasanne paluuaallon rajan jälkeen). plat_jump =
    // kuluva väli alkoi paluuaallon rajalta, jolta tila otettiin käyttöön.
    uint32_t dwell_n = 0;
    uint32_t dwell_dis = 0;
    float dwell_r = 0.0f;
    float plat_r = 0.0f;
    bool plat_jump = false;

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
    c.quiet_run = 0;
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
    c.ref_period = 0.0f;
    c.prev_ref = 0.0f;
    c.ratio_rejects = 0;
    c.stale_rolls = 0;
    c.env_from_cycle = false;
    c.pending = false;
    c.pend_rerate = false;
    c.pend_int = c.pend_per = c.pend_min = c.pend_amp = 0.0f;
    c.fire_top = 0.0f;
    c.since_cycle = 0;
    c.tent = false;
    c.tent_int = c.tent_per = c.tent_min = c.tent_amp = c.tent_dip = 0.0f;
    c.dwell_n = c.dwell_dis = 0;
    c.dwell_r = c.plat_r = 0.0f;
    c.plat_jump = false;
    c.cyc_head = c.cyc_count = 0;
    c.per_head = c.per_count = 0;
    c.sd_rpm = 0.0f;
    c.level = 0.0f;
    c.fault_run = 0;
    c.since_sat = kSatWindowSamples;  // ei saturaatiota ennen ensimmäistä havaintoa
  }

  static bool locked(const Chan& c) { return c.pub.has_pulse && c.ref_period > 0.0f; }


  // Pulssin huippu-huippu A kahden viimeisimmän segmentin yli.
  //
  // Lukittuna A = min(suurin nousu, suurin lasku). Jaksollisella signaalilla
  // molemmat ovat p2p. Tason askel kasvattaa vain toista (alipaine kasvaa:
  // laskua, pienenee: nousua), joten minimi ei reagoi siihen - absoluuttinen
  // verhokäyrä levisi askeleen verran ja jätti pulssit kynnysten väliin
  // (0,8-1 s ilman syklejä joka kaasun liikkeellä). Pieneneminen unohtuu,
  // kun vanha segmentti vierii pois (raja tai vanheneminen).
  //
  // Haussa A = suurin nousu. Ensimmäistä laskua ei yleensä nähdä huipusta
  // asti (asettuminen päättyy mielivaltaiseen kohtaan), jolloin minimi jäisi
  // vajaaksi ja ensimmäisen hyväksyttävän syklin alkuraja laukeaisi eri
  // tasolla kuin loppuraja (mitattu ~1 % jaksovirhe). Nousu on kokonainen
  // heti yhden pulssin jälkeen, eikä moottorin käynnistyksen askel
  // (ilmanpaine -> -55 kPa, alipaine kasvaa) kasvata sitä. Laskeva
  // keskeneräinen heilahdus ei myöskään vaikuta nousuun, joten raja ei
  // laukea "itseään vasten" heti huipulla.
  static float amplitude(const Chan& c, bool lock) {
    float ru = c.prev.ru > c.cur.ru ? c.prev.ru : c.cur.ru;
    const float ru_x = c.cur.hi - c.prev.lo;
    if (ru_x > ru) ru = ru_x;
    if (!lock) return ru;
    float dd = c.prev.dd > c.cur.dd ? c.prev.dd : c.cur.dd;
    const float dd_x = c.prev.hi - c.cur.lo;
    if (dd_x > dd) dd = dd_x;
    return ru < dd ? ru : dd;
  }

  // Schmittin käyttämä A. amplitude() kahden segmentin yli, kahdella
  // tarkennuksella kaasun liikkeitä varten (blippaus 850 -> 2500 rpm jätti
  // 0,9 s:n aukon):
  //  - Lukittuna, kun odotettua rajaa ei ole tullut (1 + kSplitSuspect) x
  //    jaksossa, edellisen segmentin vanha pulssi on todennäköisesti isompi kuin
  //    nykyiset: kaasua lisättäessä pulssi kutistuu (20 -> 12 kPa) samalla kun
  //    kierrokset nousevat, ja tyhjäkäynnin lasku piti kynnyksen kutistuneen
  //    pulssin ulottumattomissa vanhenemiseen asti. Silloin A = kuluvan
  //    segmentin lasku, mutta vähintään kEnvLockFrac x A, jotta kohina tai
  //    ristivuoto ei laukaise rajaa ennen oikeaa pulssia (vuotopulssi on
  //    ~0,3 x A, raja vaatii (0,5 + 0,15) / 2 x A).
  //  - Haussa pulssihistorian kanssa (lukitus purettu, has_pulse yhä päällä) A
  //    = min(nousu, lasku) kuten lukittuna: taso liikkuu tyypillisesti juuri
  //    silloin (kaasu), ja pelkkä nousu sisältäisi tason nousun koko
  //    hakuikkunan (pisin jakso, ~0,44 s) ajan. Kylmäkäynnistyksessä haku
  //    käyttää pelkkää nousua (ks. amplitude()).
  // Tasannetilaan siirtymisen välillä (plat_jump, ~1,7 T, ks. onBoundary)
  // ensimmäistä tarkennusta ei käytetä: kuluvan segmentin lasku on silloin
  // paluuaallon huipusta tasanteelle, ja sillä laskettu taso osui
  // tasanteelle 1,2 T:ssä (mitattu 1500 rpm, base -30, peak 45). Ilman
  // siirtymävälin käsittelyä test_return_wave_plateau_keeps_lock kaatuu (D3-ero
  // 0,20 kPa @ 1050 rpm). Muuten myöhästynyt raja purkaa tasannetilan ennen
  // tätä (pushChannel), ja tarkennus toimii kuten ilman paluuaaltoa.
  float schmittAmplitude(const Chan& c) const {
    const bool lock = locked(c);
    const float amp = amplitude(c, lock || c.pub.has_pulse);
    if (!lock || c.plat_jump ||
        static_cast<float>(c.since_boundary) <= (1.0f + kSplitSuspect) * c.ref_period) {
      return amp;
    }
    float recent = c.cur.dd < amp ? c.cur.dd : amp;
    const float floor = kEnvLockFrac * amp;
    return recent > floor ? recent : floor;
  }

  // Suurin heilahdus (nousu tai lasku) kahden segmentin yli.
  static float swing(const Chan& c) {
    float s = amplitude(c, false);
    if (c.prev.dd > s) s = c.prev.dd;
    if (c.cur.dd > s) s = c.cur.dd;
    if (c.prev.hi - c.cur.lo > s) s = c.prev.hi - c.cur.lo;
    return s;
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
      c.tent = false;
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

    // Hiljaisuudesta herääminen (moottori käynnistyy, letku kytketään): ensimmäinen
    // liike voi olla tason askel (0 -> -8 kPa kerralla täydellä kaasulla), ja
    // suotimen soiminen sen jälkeen (alitus ja paluu, ~2 kPa) näkyy haun
    // amplitudiarviossa nousuna, joka on suurempi kuin suodatettu pulssi
    // (~1 kPa @ 6000 rpm). Kynnys pysyi pulssien ulottumattomissa kaksi
    // hakuikkunaa (mitattu 0,95 s). Siksi ensimmäinen liike hiljaisuuden
    // jälkeen käynnistää asettumisen uudelleen (kStepSettleFactor x), ja
    // amplitudiarvio rakentuu vasta sen jälkeisistä pulsseista. Pulssivassa
    // signaalissa hiljaisuutta ei ole, joten käynti ei koskaan laukaise tätä;
    // tyhjäkäynnillä käynnistyessä se maksaa enintään ensimmäisen pulssin.
    if (!c.pub.has_pulse) {
      const float sw = swing(c);
      if (c.quiet_run >= window_n_ && sw >= min_pulse_p2p_) {
        c.quiet_run = 0;
        c.settle = kStepSettleFactor * settle_n_;
        c.armed = false;
        c.chain = false;
        c.pending = false;
        c.acc = 0.0f;
        c.dwell_n = c.dwell_dis = 0;
        c.dwell_r = c.plat_r = 0.0f;
        c.plat_jump = false;
        c.x_prev = x;
        c.y_prev = y;
        return;
      }
      if (sw >= min_pulse_p2p_) {
        c.quiet_run = 0;
      } else if (c.quiet_run < window_n_) {
        ++c.quiet_run;
      }
    } else {
      c.quiet_run = 0;
    }

    // Suhteellinen Schmitt: raja, kun signaali putoaa D = (A + h) / 2
    // viimeisimmän huipun alle, eli h/2 pulssin puolivälin (huippu - A/2)
    // alapuolelle; viritys, kun se on noussut pohjasta hystereesin
    // h = max(hyst_min_kpa, hyst_frac x A) verran. Kynnykset kulkevat siis
    // huipun ja pohjan mukana syklin sisällä: alipaineen muutos (kaasu,
    // /api/sim) ei voi jättää pulsseja kiinteiden kynnysten väliin, kuten
    // absoluuttisella verhokäyrällä kävi (0,8-1 s ilman syklejä).
    // Puoliväli eikä keskiarvo: imupulssi on kapea (~25 % syklistä), joten
    // keskiarvo on lähellä yläreunaa ja ristivuodon pulssit (xtalk 0,3)
    // ylittäisivät sen. Puolivälistä laskettuna raja vaatii 0,65 x A:n
    // pudotuksen, vuotopulssi on vain ~0,3 x A -> kierrosluku ei kolminkertaistu.
    // Kaksoisrajaa ei synny, vaikka viritys tulee jo h:n nousulla: rajan
    // jälkeen pulssi laskee enää (A - h) / 2 < D.
    // Viritys h:lla eikä D:llä, koska alipaineen kasvu kesken pulssin
    // lyhentää seuraavaa nousua askeleen verran - D:n vaatimus ohitti silloin
    // koko seuraavan pulssin.
    // Tasannetilassa myöhästynyt raja (yli (1 + kSplitSuspect) x jakso,
    // siirtymisväliä lukuun ottamatta) purkaa tilan: kaasun liikkeessä pulssi
    // kutistuu tai jakso muuttuu, ja silloin normaali taso ja kutistuvan
    // pulssin amplitudi (schmittAmplitude) seuraavat paremmin. Mitattu
    // blippauksessa 850 -> 2500 (16 siementä): pisin sykliväli peak 40:llä
    // 0,5 -> 0,4 s ja peak 30:llä 0,7 -> 0,65 s tämän ansiosta.
    if (c.plat_r > 0.0f && !c.plat_jump && locked(c) &&
        static_cast<float>(c.since_boundary) > (1.0f + kSplitSuspect) * c.ref_period) {
      c.plat_r = 0.0f;
    }
    const float amp = schmittAmplitude(c);
    float h = dp_.hyst_frac * amp;
    if (h < dp_.hyst_min_kpa) h = dp_.hyst_min_kpa;
    const float d_norm = 0.5f * (amp + h);
    float d = d_norm;
    if (c.plat_r > 0.0f) {
      const float dp = 0.5f * (1.0f + c.plat_r) * amp;  // tasanteen ja pohjan puoliväli
      if (dp > d) d = dp;
    }

    // Paluuaallon tasanne (kPlateauBand): viipyminen normaalin laukaisutason
    // kaistalla, aina normaalia tasoa vasten. Viritettynä tasanne on ennen
    // imupulssin rajaa: kaista on symmetrinen. Virityksen ulkopuolella
    // tasanne seuraa paluuaallon laskun rajaa, ja se lasketaan vain
    // laukaisutason yläpuolelta: silloin raja laukesi pelkästä suotimen
    // alituksesta ja on epävarma. Tason alla oleva tasanne laukaisee
    // paluuaallon rajan varmasti, eikä tilaa vaihdeta (vaihto siirtäisi rajaa
    // ~0,6 T; mitattu 600 rpm, peak 50: laukaisu tasanteella).
    if (amp > 0.0f) {
      const float below = c.run_hi - y;
      bool in_band;
      if (c.armed) {
        const float band = (c.plat_r > 0.0f ? kPlateauKeepBand : kPlateauBand) * amp;
        in_band = below > d_norm - band && below < d_norm + band;
      } else {
        in_band = !(c.plat_r > 0.0f) && below > d_norm - kPlateauBand * amp && below < d_norm;
        if (in_band) ++c.dwell_dis;
      }
      if (in_band) {
        ++c.dwell_n;
        c.dwell_r += below / amp;
      }
    }

    if (!c.armed) {
      if (y < c.run_lo) c.run_lo = y;
      if (y > c.run_lo + h) {
        // Rajan laukaissut pulssi on nyt nähty pohjaan asti.
        c.armed = true;
        if (c.pending) confirmPending(c, c.fire_top - c.run_lo);
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
        onBoundary(c, f, c.acc + part, y);
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
      if (locked(c) && ++c.stale_rolls >= kMaxStaleRolls) {
        c.ref_period = 0.0f;  // jakso pidentyi: haku (ks. kMaxStaleRolls)
        c.ratio_rejects = 0;
        c.stale_rolls = 0;
      }
      roll(c, y, false);
      // Kuluvaa väliä ei arvioida (ei kokonainen raja-raja-segmentti). Jos
      // sykli odotti vahvistusta, sen päättänyt "pulssi" ei noussut takaisin
      // - tason askel eikä imupulssi - ja kSplitSuspect-sykli jää ilman
      // ratkaisevaa seuraavaa rajaa; molemmat hylätään.
      c.env_from_cycle = false;
      c.pending = false;
      c.tent = false;
      c.plat_r = 0.0f;  // siirretty taso ei laukea: takaisin normaaliin (kPlateauBand)
      c.plat_jump = false;
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

  void onBoundary(Chan& c, float f, float integral, float y) {
    // Raja on välillä [n-1, n]: kokonaisosa n-1, murto-osa f. Jakso lasketaan
    // kokonaislukuerotuksena (kääntyy oikein uint32-ylivuodossa ~49 vrk:n
    // jälkeen) plus murto-osien erotus - float-aikaleima menettäisi
    // tarkkuutensa jo tunneissa.
    const uint32_t b_int = n_ - 1u;
    // Paluuaallon tasanne juuri päättyneeltä väliltä (kPlateauBand). Ketjun
    // ensimmäinen väli alkaa mielivaltaisesta kohdasta, joten sitä ei arvioida.
    // Jos tila otetaan käyttöön paluuaallon rajalta (tasanne enimmäkseen
    // virityksen ulkopuolella), seuraava raja on imupulssissa vasta ~1,7 T:n
    // päästä: sitä väliä ei arvioida (vrt. next_trusted alla).
    const bool keep = c.plat_r > 0.0f;
    c.plat_r = 0.0f;
    c.plat_jump = false;
    if (c.chain && c.dwell_n > 0 &&
        static_cast<float>(c.dwell_n) >= kPlateauDwell * static_cast<float>(c.since_boundary)) {
      const float r = c.dwell_r / static_cast<float>(c.dwell_n);
      if (r <= (keep ? kPlateauKeepMaxRel : kPlateauMaxRel)) {
        c.plat_r = r;
        c.plat_jump = !keep && 2u * c.dwell_dis > c.dwell_n;
      }
    }
    c.dwell_n = c.dwell_dis = 0;
    c.dwell_r = 0.0f;
    // Juuri päättyneen syklin amplitudi: nousu syklin pohjasta huippuun, ja
    // lukittuna pienempi siitä ja laskusta edellisen syklin huipusta tämän
    // pohjaan, koska tason askel kasvattaa vain toista (vrt. amplitude()).
    float amp = c.cur.ru;
    float cmin = c.cur.lo;
    const bool lock = locked(c);
    if (!c.cur.full) {
      // Vanheneminen jakoi syklin kahteen segmenttiin: nousu kummasta
      // tahansa (lasku edeltävään sykliin ei ole enää tallessa).
      if (c.prev.ru > amp) amp = c.prev.ru;
      if (c.cur.hi - c.prev.lo > amp) amp = c.cur.hi - c.prev.lo;
      if (c.prev.lo < cmin) cmin = c.prev.lo;
    } else if (lock && c.prev.full) {
      const float dip = c.prev.hi - c.cur.lo;
      if (dip < amp) amp = dip;
    }
    float period = 0.0f;
    if (c.chain) period = static_cast<float>(b_int - c.b_int) + (f - c.b_frac);
    c.pending = false;  // ratkeaa aina virityksessä, joka edeltää rajaa
    // Seuraava väli arvioidaan vain, jos tämä raja oli imupulssi. Ketjun
    // ensimmäinen väli alkaa mielivaltaisesta kohdasta (käynnistys,
    // vanheneminen), ja sen amplitudi voi sattumalta osua vuotopulssin
    // kokoon - siksi arvioidaan vasta toisesta kokonaisesta välistä alkaen.
    bool next_trusted = c.chain;
    bool evaluate = c.chain && c.env_from_cycle;
    if (c.tent) {
      // Odottava sykli P1 ja juuri päättynyt väli P2. Väärän rajan
      // tunnusmerkit: P2 on vain jakamisen jäännös (< kMinPeriodRatio x P1),
      // tai P1:n päättänyt "pulssi" ei noussut takaisin (nousu P2:ssa < 0,7 x
      // sen syvyys): se oli alipaineen askel. Oikea pulssi nousee yhtä
      // paljon kuin laski, ja kiihdytyksessä jaksot lyhenevät asteittain.
      c.tent = false;
      const bool split = !c.chain || period < kMinPeriodRatio * c.tent_per ||
                         c.cur.ru < kEnvStableFrac * c.tent_dip;
      if (split) {
        evaluate = false;  // P2 alkoi väärältä rajalta
      } else {
        recordCycle(c, c.tent_int, c.tent_per, c.tent_min, c.tent_amp);
      }
    }
    const float ref = c.ref_period;
    if (evaluate) {
      // Lukittuna jakso viitteen mukaan = molemmat rajat ovat imupulsseja:
      // vuotopulssin raja antaisi T/3 tai 2T/3, puuttuva pulssi 2T. Silloin
      // amplitudin yhtäpitävyyttä ei vaadita, koska alipaineen askel kesken
      // nousun tai laskun pienentää yhden syklin amplitudia hetkellisesti
      // (mitattu: 20 -> 7,6 kPa 15 kPa:n askeleessa) eikä se tarkoita väärää rajaa.
      const bool on_time = lock && period <= (1.0f + kLockPeriodWindow) * ref &&
                           period >= (1.0f - kLockPeriodWindow) * ref;
      const float frac = lock ? kEnvLockFrac : kEnvStableFrac;
      const bool ok_period = period >= min_period_ && period <= max_period_;
      const bool ok_amp = ampOk(amp, period, c.last_period);
      const bool ok_env = on_time || (c.last_amp >= frac * amp && amp >= frac * c.last_amp);
      // Liian lyhyt jakso lukittuna: tämä raja ei ollut imupulssi (alipaineen
      // askel, vuotopulssi), joten siitä alkavaa väliä ei arvioida - se
      // hyväksyttäisiin muuten väärällä jaksolla (mitattu 208 ms 140 ms:n
      // sijaan). Liian pitkä jakso (puuttuva pulssi) päättyy oikeaan rajaan.
      const bool too_short = ref > 0.0f && period < kMinPeriodRatio * ref;
      const bool too_long = ref > 0.0f && period > kMaxPeriodRatio * ref;
      // Uudelleenreferointi: viitteestä poikkeava jakso, joka toistaa
      // edellisen raja-raja-välin (±kSplitSuspect), on aito kierrosluvun
      // muutos eikä väärä raja - vuotopulssin tai askeleen raja jakaa syklin
      // eri mittaisiin osiin. Ilman tätä hylkäykset kertyivät vain joka
      // toisella rajalla (too_short jättää seuraavan välin arvioimatta), ja
      // kaasun lisäys 850 -> 2000 rpm oli lukossa vanhaan jaksoon koko pidon ajan.
      const bool rerate = lock && (too_short || too_long) && c.last_period > 0.0f &&
                          period <= (1.0f + kSplitSuspect) * c.last_period &&
                          period >= (1.0f - kSplitSuspect) * c.last_period;
      if (lock && too_short && !rerate && c.ratio_rejects < kMaxRatioRejects) next_trusted = false;
      if (ok_period && ok_amp && ok_env) {
        const bool ok_ratio = !too_short && !too_long;
        if (ok_ratio || rerate || c.ratio_rejects >= kMaxRatioRejects) {
          c.pending = true;
          c.pend_int = integral;
          c.pend_per = period;
          c.pend_min = cmin;
          c.pend_amp = amp;
          c.pend_rerate = !ok_ratio;
        } else {
          ++c.ratio_rejects;
        }
      }
    }
    c.last_amp = amp;
    c.last_period = period;
    c.fire_top = c.run_hi;  // huippu ennen tätä rajaa
    c.env_from_cycle = next_trusted && !c.plat_jump;
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
  // pulssi syvä, joten yläraja hylkää syklin - mutta vain jos jaksokin
  // poikkeaa viitteestä. Jakson mukainen raja on oikea pulssi, jonka pohjaa
  // askel vain syvensi; askelraja taas osuu satunnaiseen kohtaan sykliä, ja
  // jos se sattuu jakson mukaiseen kohtaan, virhe on alle kLockPeriodWindow
  // ja seuraava sykli kompensoi sen keskiarvossa (välit ovat yhtenäiset).
  // Hylätyn syklin jälkeinen väli alkoi samalta epäilyttävältä rajalta,
  // joten sitäkään ei arvioida.
  void confirmPending(Chan& c, float depth) {
    c.pending = false;
    const bool lock = locked(c);
    const float lo = lock ? kEnvLockFrac : kEnvStableFrac;
    bool ok = depth >= lo * c.pend_amp;
    // Askeltarkistus (yläraja) ohitetaan, kun jakso on viitteen mukainen.
    // Kiihdytys ja hidastus muuttavat jaksoa 10-20 % per sykli, ja samalla
    // syvenevä tai laimeneva alipaine kasvattaa laskua ja pienentää nousua
    // yhtä paljon, jolloin aidonkin pulssin suhde painuu alle 0,7:n (mitattu
    // 17,70 / 25,36 blippauksen hidastuksen lopussa). Siksi myös
    // kSplitSuspectin sisällä poikkeava jakso käy, jos se jatkaa edellisen
    // jakson muutosta (kTrendTol): askeleen raja osuu satunnaiseen kohtaan
    // sykliä eikä seuraa trendiä (test_vacuum_step_at_any_phase). Yli
    // kSplitSuspectin poikkeava sykli menee tent-polulle. Uudelleenreferoitu
    // jakso on jo todettu toistuvaksi (onBoundary): askeleen raja ei toista
    // edellistä väliä.
    const float r = c.ref_period > 0.0f ? c.pend_per / c.ref_period : 0.0f;
    const float trend = c.prev_ref > 0.0f ? c.ref_period / c.prev_ref : 1.0f;
    const bool on_time =
        lock && (c.pend_rerate || (r <= 1.0f + kLockPeriodWindow && r >= 1.0f - kLockPeriodWindow) ||
                 (r <= 1.0f + kSplitSuspect && r >= 1.0f - kSplitSuspect &&
                  std::fabs(r - trend) <= kTrendTol && std::fabs(trend - 1.0f) > kTrendTol));
    if (!on_time) ok = ok && c.pend_amp >= kEnvStableFrac * depth;
    if (ok) {
      const bool suspect = lock && !c.pend_rerate &&
                           (c.pend_per > (1.0f + kSplitSuspect) * c.ref_period ||
                            c.pend_per < (1.0f - kSplitSuspect) * c.ref_period);
      if (suspect) {
        c.tent = true;
        c.tent_int = c.pend_int;
        c.tent_per = c.pend_per;
        c.tent_min = c.pend_min;
        c.tent_amp = c.pend_amp;
        c.tent_dip = depth;
      } else {
        recordCycle(c, c.pend_int, c.pend_per, c.pend_min, c.pend_amp);
      }
    } else {
      c.env_from_cycle = false;
    }
  }

  void recordCycle(Chan& c, float integral, float period, float cmin, float cp2p) {
    c.prev_ref = c.ref_period;
    c.ref_period = period;
    c.ratio_rejects = 0;
    c.stale_rolls = 0;
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
