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
//         │                             └► pulssin keskikohta ───► jakso, rpm
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
// Jakso (rpm) mitataan eri kohdasta kuin integraalin raja: pulssin
// keskikohdasta, eli laskevan ja nousevan reunan puolivälistä tasolla
// pohja + 0,3 x syvyys. Laukaisukynnys riippuu A-arviosta, joka on
// muutoksissa (kaasu, rpm-pudotus) väistämättä hetken väärä; keskikohta ei
// riipu kynnyksestä, pulssin koosta eikä leveydestä. Ilman tätä yksittäisen
// syklin jakso heitti kaasunlisäyksessä 3-9 % (kynnys siirtyi pulssissa).
//
// Aika on näytenumeroina (1 näyte = 1 ms, kSampleRateHz). RawFrame.seq:iä ei
// käytetä aikana, koska pudonneita näytteitä ei voi kuitenkaan interpoloida
// IIR-suotimeen jälkikäteen - aukot näkyvät raw_ringin diagnostiikassa.
//
// Mitatut rajat (oletusparametrit; test_dsp ja EngineSim-ajot, 1 % jitter):
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
//    Rajan lähelläkin rpm on keskikohtamerkin ansiosta < 0,5 % (6000 rpm,
//    4 kPa, jitter 0: pahin kanava 0,45 %).
//  - Alipaineen ramppi (kaasun liike) ei maksa yhtään sykliä. Hetkellinen
//    askel (/api/sim) tai pulssin puolittuminen maksaa enintään 2 sykliä
//    (väli ~3 jaksoa); has_pulse ei putoa, eikä väärää jaksoa hyväksytä.
//  - Kierrosluvun muutokset (EngineSim, 4 siementä x xtalk 0/0,3 x 6 vaihetta):
//    kaasunlisäys 850 -> 1500...3000 rpm, nousu 300-600 ms: pisin väli
//    hyväksyttyjen syklien välillä <= 300 ms, paitsi 2500 rpm (<= 330 ms) ja
//    3000 rpm xtalk 0,3:lla (<= 445 ms: pulssi kutistuu 20 -> 4,5 kPa
//    suodatettuna kahdessa syklissä). Nousu 150 ms: <= 400 ms. Näytetty
//    huippu-rpm >= 0,9 x todellinen. Kaasun katkaisu ja rpm-askel alas
//    (3000/2000/6000 -> 850, /api/sim): has_pulse ei putoa, väli <= ~640 ms
//    (4 uutta jaksoa + 1 vahvistus), näyttö uudessa kierrosluvussa (< 3 %)
//    1 s kuluttua. Yksittäisen syklin jakso vastaa toteutunutta <= 3 %:n
//    sisällä, paitsi kerralla vaihtuvan kierrosluvun ylittävä sykli (5-9 %:
//    pulssi on puoliksi vanhalla, puoliksi uudella nopeudella) ja 40 kPa/300 ms
//    alipaineramppi 3 kPa:n pulssilla (<= 10 %).
//  - Kohinasuoja: hyst_min_kpa 0,3 kestää ~0,4 kPa RMS raakakohinan ilman
//    yhtään väärää sykliä; 0,6-0,8 kPa:n kohina vaatii ~0,5 (S5a).
//  - Hajonta (rivi 2): jitter 5 % -> cv 0,046, 15 % -> 0,126, 20 % -> 0,15.
//    Suuret yksittäiset poikkeamat vaativat seuraavan jakson vahvistuksen,
//    joten hyvin epätasaisella käynnillä osa ääripäistä jää pois ja cv
//    lukee ~15 % todellista pienempänä.

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
  // usein sen sisälle - vaan pulssien ja jaksojen keskinäiseen vertailuun.
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
  // Lukituksen haku: kolmen peräkkäisen rajapulssin amplitudit tämän suhteen
  // sisällä (0,7...1/0,7). Liian pieni amplitudi syklin alussa: ensimmäinen
  // raja voi osua vuotopulssiin (xtalk) tai pulssin alkuun - sykli on väärän
  // mittainen. Liian suuri: kynnykset ovat liian leveät ja rajoja jää
  // välistä, jolloin "sykli" onkin useita syklejä. Lukittuna samaa suhdetta
  // käytetään "tasaisen" pulssiparin tunnistukseen (ks. confirmLocked).
  static constexpr float kEnvStableFrac = 0.7f;
  // Lukittuna rajapulssien syvyyksien (lasku huipusta pohjaan) suhde
  // peräkkäin enintään 1 / tämä. Syvyys eikä nousu, koska alipaineen ramppi
  // (kaasu kiinni: -15 -> -55 kPa 300 ms:ssa) kasvattaa jokaisen pulssin
  // laskua yhtä paljon mutta pienentää nousua - nousuun perustuva vertailu
  // hylkäsi rampilla lähes jokaisen syklin. Vuotopulssi on <= 0,37 x oma
  // pulssi (xtalk 0,3 + suotimen ylitys), joten 0,45 erottaa sen; rpm:n
  // putoaminen 3000 -> 850 kasvattaa suodatettua pulssia ~2x (alipäästö).
  static constexpr float kDepthRatio = 0.45f;
  // Lukittuna jakso tämän suhteellisen ikkunan sisällä viitteestä (viimeisin
  // hyväksytty jakso) kirjataan heti. 10 % = 10 sigmaa 1 %:n jitterillä ja 2
  // sigmaa 5 %:n jitterillä; laajempi ikkuna hyväksyi rpm-pudotuksessa
  // vuotopulssien välin (T_uusi / 3 = 1,17 x T_vanha kun 3000 -> 850 rpm).
  static constexpr float kLockPeriodWindow = 0.10f;
  // Kiihdytyksessä (pulssi pieneni samalla, kaasu auki) välitön ikkuna
  // ulottuu 20 %:iin: kaasunlisäyksen ensimmäinen sykli lyhenee 10-20 %.
  // Sama 20 % on myös pidetyn jakson "jitter"-vahvistuksen yläraja.
  static constexpr float kSplitSuspect = 0.20f;
  // Kampiakselin nopeus ei voi muuttua yhden syklin aikana 1,8-kertaiseksi
  // eikä pudota alle 0,55-kertaiseksi (rajat ~2,8 sigmaa peräkkäisten
  // jaksojen suhteesta cv:llä 0,15, rivi 2). Pidetyn jakson vahvistus vaatii
  // seuraavalta jaksolta näiden sisällä pysyvää jatkoa, ja yli 1,8-kertainen
  // hidastuminen kerralla (vain /api/sim) vahvistetaan vasta seuraavasta
  // pulssista (ks. classify).
  static constexpr float kMaxPeriodRatio = 1.8f;
  static constexpr float kMinPeriodRatio = 0.55f;
  // A:n vaimennus lukittuna: jos rajaa ei ole tullut 1,2 x odotetun jakson
  // aikana, A vaimenee aikavakiolla 0,3 x jakso. Kaasunlisäyksessä pulssi
  // kutistuu (20 -> 4,5 kPa suodatettuna kahdessa syklissä), ja vanha A
  // pitäisi kynnyksen pulssin yläpuolella; kiinteä vanhenemisaika 1,8 x jakso
  // jäätyi kanavan ~0,9 s:ksi. Odotettu jakso = max(viite, kahden
  // täysikokoisen pulssin väli), jotta rpm-pudotuksen jälkeen (viite vielä
  // vanha, lyhyt) vaimennus ei käynnisty ennen uutta omaa pulssia.
  static constexpr float kDecayStart = 1.2f;
  static constexpr float kDecayTau = 0.3f;
  // Raja laukesi selvästi vaimennetulla A:lla (alle 0,7) ja pulssi oli alle
  // 0,6 x muistin A: pulssi on pienentynyt pysyvästi, ja vanha segmentti
  // unohdetaan, jotta seuraava samankokoinen laukaisee ilman uutta odotusta.
  static constexpr float kFreshDecay = 0.7f;
  static constexpr float kFullPulse = 0.6f;
  // Vaimennuksen lattia: 0,6 x kahden viimeisimmän rajapulssin syvyys
  // (kuitenkin enintään muistin A). Kynnys pysyy silloin >= 0,39 x oma
  // pulssi eli vuotopulssien (<= 0,37) yläpuolella, vaikka uusi oma pulssi
  // tulee rpm-pudotuksen jälkeen moninkertaisesti viitettä myöhemmin. Lattia
  // ei koske tilannetta, jossa taso nousee (kaasu auki, pulssi kutistuu), ja
  // se raukeaa 3 jakson jälkeen (pulssi loppui tai kutistui kerralla).
  static constexpr float kFloorFrac = 0.6f;
  static constexpr float kLevelRise = 0.1f;
  // Pulssin keskikohtamerkin taso: pohja + 0,3 x syvyys. Lähempänä pohjaa
  // laskeva ja nouseva reuna ovat symmetrisiä, eikä alipaineen ramppi
  // (pulssin jälkeinen taso eri kuin ennen) siirrä keskikohtaa; 0,5:llä
  // rampin ylittävä sykli heitti 5 %.
  static constexpr float kMarkFrac = 0.3f;
  // Suodatetun signaalin rengaspuskuri merkin laskevan reunan jälkihakuun:
  // pohjasta taaksepäin merkkitasolle ja viritykseen asti on <= ~60 ms
  // vielä 300 rpm:llä (imu 100 ms).
  static constexpr int kRing = 64;
  // Pohjan viive merkistä enintään 0,4 x jakso. Tason askel laukaisee rajan
  // ilman omaa pulssia, ja "pohjaksi" tulee vasta seuraavan pulssin pohja
  // - kaukana rajasta. Oikean pulssin pohja on <= ~0,26 x jakson päässä
  // (6000 rpm:llä perustaajuus on lähes siniaalto).
  static constexpr float kBottomDelay = 0.4f;
  // Omankokoinen kuoppa: min(lasku, nousu) >= 0,5 x A. Vuotopulssit ovat <=
  // 0,37 x A (xtalk <= 0,3), joten tällainen kuoppa on oma pulssi, joka ei
  // laukaissut (pienentyi). Rajan jälkeen 0,5 x jakson jälkeen ja tason
  // noustessa (kaasu auki) siitä tehdään raja jälkikäteen (kaasunlisäyksen
  // ensimmäinen pulssi on 0,6-0,7 x tyhjäkäynnin pulssi, kynnys 0,65 x A).
  // Tasaisella tasolla jälkiraja ei laukea, jotta hystereesin määräämä
  // vuotopulssisieto säilyy (xtalk 0,57 ei laukaise hyst_min 1,0:lla).
  static constexpr float kOwnDip = 0.5f;
  static constexpr float kLateStart = 0.5f;
  // Näytön keskiarvo aloitetaan alusta, jos uusi jakso on yli 1,5 x (tai
  // alle 0,55 x) näytettävien syklien keskijakso: kierrosluku muuttui
  // kerralla (/api/sim, kaasun katkaisu), eikä vanhan käynnin syklejä ole
  // syytä näyttää vielä sekunnin ajan. Hajonta (per[]) ei nollaudu.
  static constexpr float kRestartRatio = 1.5f;
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
  // vikamonitoreille). O(1) per kanava; pulssin mittaus kerran syklissä
  // <= 2 x kRing vertailua ja hyväksytyn syklin kohdalla
  // O(avg_cycles + rpm_hist) <= 128 kertolaskua.
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
  // (= syklirajojen väli, tai haussa vanhenemisaika jos rajaa ei tule).
  struct Seg {
    float hi = 0.0f, lo = 0.0f;
    float ru = 0.0f;    // suurin nousu: pohjasta myöhempään huippuun
    float dd = 0.0f;    // suurin lasku: huipusta myöhempään pohjaan
    bool full = false;  // alkoi syklirajalta (ei käynnistyksestä tai vanhenemisesta)
  };

  // Hyväksymistä odottava sykli (integraali, integraalin väli, jakso, min, p2p).
  struct Cyc {
    float integral = 0.0f, span = 0.0f, period = 0.0f, cmin = 0.0f, p2p = 0.0f;
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
    uint32_t t_lo = 0;           // pohjan näyteindeksi
    bool armed = false;
    uint32_t since_boundary = 0;

    // A:n vaimennus lukittuna, kun odotettu raja ei tule (ks. kDecayStart).
    float decay = 1.0f;
    float decay_k = 1.0f;        // kerroin per näyte jaksolle decay_for
    float decay_for = 0.0f;
    float nat_period = 0.0f;     // kahden täysikokoisen pulssin väli
    uint32_t nat_int = 0;
    float nat_frac = 0.0f;
    bool nat_valid = false;
    float bnd_amp = 0.0f;        // vaimentamaton A rajan hetkellä
    bool bnd_decayed = false;    // raja laukesi selvästi vaimennetulla A:lla
    float floor_amp = 0.0f;      // vaimennuksen lattia (kFloorFrac)

    // Pienet kuopat virityksen jälkeen (eivät laukaisseet rajaa).
    float mlo = 0.0f;            // kuluvan kuopan pohja
    uint32_t t_mlo = 0;
    float acc_mlo = 0.0f;        // integraali kuopan pohjaan asti (jälkiraja)
    float cur_dip = 0.0f;        // kuluvan kuopan min(lasku, nousu)
    float max_dip = 0.0f;        // välin suurin päättynyt kuoppa

    // Sykliketju: edellinen raja (kokonais- + murto-osa näytteinä).
    bool chain = false;
    uint32_t b_int = 0;
    float b_frac = 0.0f;
    float acc = 0.0f;            // suodattamaton integraali edellisestä rajasta, kPa*näyte
    float last_amp = 0.0f;       // edellisen raja-raja-välin amplitudi (yhtäpitävyys)
    float last_period = 0.0f;    // edellisen raja-raja-välin pituus, 0 = ei
    bool env_from_cycle = false; // kuluva väli alkoi ketjun toiselta tai myöhemmältä rajalta
    float fire_top = 0.0f;       // huippu ennen viimeisintä rajaa

    // Rajapulssin mittaus: suodatettu signaali rengaspuskurissa, keskikohta
    // laskevan (hf) ja nousevan reunan puolivälistä merkkitasolla.
    float ring[kRing] = {};
    bool wait_rise = false;      // odotetaan nousevaa reunaa merkkitasolle
    float mark_lvl = 0.0f;
    float pulse_depth = 0.0f;    // lasku huipusta pohjaan
    uint32_t hf_int = 0;
    float hf_frac = 0.0f;
    bool m_valid = false;        // edellisen pulssin keskikohta voimassa
    uint32_t m_int = 0;
    float m_frac = 0.0f;
    float last_depth = 0.0f;     // edellisen rajapulssin syvyys

    // Ehdokassykli, joka vahvistetaan kun sen päättänyt pulssi on mitattu.
    bool pending = false;
    Cyc pend;
    float pend_amp = 0.0f;       // syklin alkupulssin amplitudi (haun vertailu)
    float pend_dip = 0.0f;       // syklin suurin pieni kuoppa
    // Lukittuna viitteestä poikkeava sykli odottaa seuraavaa (ks. classify).
    bool hold = false;
    Cyc held;
    bool pair = false;           // yli 1,8 x hidastunut pari odottaa seuraavaa pulssia
    Cyc paired;
    float last_valid = 0.0f;     // edellisen kelvollisen lukitun välin jakso
    float ref_period = 0.0f;     // viimeisin hyväksytty jakso, 0 = ei viitettä
    float prev_ref = 0.0f;       // sitä edeltävä hyväksytty jakso
    uint32_t since_cycle = 0;    // näytteitä viimeisestä hyväksytystä syklistä

    // Syklihistoria. integral/span erikseen, jotta keskiarvo on aikapainotettu
    // eli täsmälleen avg_cycles kokonaisen syklin yli (D1); rpm keskikohtien
    // jaksoista (period). Laskurit rajaavat käytön, joten reset() ei nollaa
    // taulukoita.
    float cyc_int[kMaxAvgCycles] = {};
    float cyc_span[kMaxAvgCycles] = {};
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

  static bool within(float p, float r, float frac) { return p <= (1.0f + frac) * r && p >= (1.0f - frac) * r; }

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
    c.t_lo = 0;
    c.armed = false;
    c.since_boundary = 0;
    c.decay = c.decay_k = 1.0f;
    c.decay_for = c.nat_period = 0.0f;
    c.nat_int = 0;
    c.nat_frac = 0.0f;
    c.nat_valid = false;
    c.bnd_amp = 0.0f;
    c.bnd_decayed = false;
    c.floor_amp = 0.0f;
    c.mlo = c.acc_mlo = c.cur_dip = c.max_dip = 0.0f;
    c.t_mlo = 0;
    c.chain = false;
    c.b_int = 0;
    c.b_frac = 0.0f;
    c.acc = 0.0f;
    c.last_amp = 0.0f;
    c.last_period = 0.0f;
    c.env_from_cycle = false;
    c.fire_top = 0.0f;
    c.wait_rise = false;
    c.mark_lvl = c.pulse_depth = 0.0f;
    c.hf_int = 0;
    c.hf_frac = 0.0f;
    c.m_valid = false;
    c.m_int = 0;
    c.m_frac = 0.0f;
    c.last_depth = 0.0f;
    c.pending = false;
    c.pend = Cyc();
    c.pend_amp = c.pend_dip = 0.0f;
    c.hold = false;
    c.held = Cyc();
    c.pair = false;
    c.paired = Cyc();
    c.last_valid = 0.0f;
    c.ref_period = 0.0f;
    c.prev_ref = 0.0f;
    c.since_cycle = 0;
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
  // kun vanha segmentti vierii pois rajalla; lukittuna lisäksi vaimennus ja
  // tuore aloitus (ks. pushChannel, finishPulse).
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
      c.prev_ref = 0.0f;
      c.hold = false;
      c.pair = false;
      c.nat_period = 0.0f;
    }

    const float y = c.lp.process(x);
    c.ring[n_ % kRing] = y;
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
    const bool lock = locked(c);
    const float amp_mem = amplitude(c, lock);
    float amp = amp_mem;
    if (lock) {
      // Odotettu raja myöhässä: A vaimenee (kaasu auki -> pulssi kutistuu),
      // mutta ei alle lattian silloin, kun taso ei nouse (rpm-pudotus:
      // uusi oma pulssi tulee viitettä myöhemmin, välissä vuotopulsseja).
      const float expect = c.nat_period > c.ref_period ? c.nat_period : c.ref_period;
      if (static_cast<float>(c.since_boundary) > kDecayStart * expect) {
        if (expect != c.decay_for) {
          c.decay_for = expect;
          c.decay_k = std::exp(-1.0f / (kDecayTau * expect));
        }
        c.decay *= c.decay_k;
      }
      amp = amp_mem * c.decay;
      if (c.decay < 1.0f && c.run_hi - c.fire_top < kLevelRise * amp_mem &&
          static_cast<float>(c.since_boundary) < 3.0f * c.decay_for) {
        const float fl = c.floor_amp < amp_mem ? c.floor_amp : amp_mem;
        if (fl > amp) amp = fl;
      }
    }
    float h = dp_.hyst_frac * amp;
    if (h < dp_.hyst_min_kpa) h = dp_.hyst_min_kpa;
    const float d = 0.5f * (amp + h);

    if (!c.armed) {
      if (y < c.run_lo) {
        c.run_lo = y;
        c.t_lo = n_;
      }
      if (y > c.run_lo + h) {
        // Rajan laukaissut pulssi on nyt nähty pohjaan asti: mitataan se.
        c.armed = true;
        startPulse(c);
        c.run_hi = y;
        c.mlo = y;
      }
      c.acc += area;
    } else {
      bool late = false;
      if (y > c.run_hi) {
        c.run_hi = y;
        c.mlo = y;
        if (c.cur_dip > c.max_dip) c.max_dip = c.cur_dip;
        c.cur_dip = 0.0f;
      } else if (y < c.mlo) {
        c.mlo = y;
        c.t_mlo = n_;
        c.acc_mlo = c.acc + area;
      } else {
        // Pieni kuoppa nousee: koko min(lasku, nousu). Omankokoinen kuoppa
        // tason noustessa (kaasu auki) puolen jakson jälkeen = pienentynyt
        // oma pulssi, joka ei laukaissut (ks. kOwnDip).
        float dip = c.run_hi - c.mlo;
        if (y - c.mlo < dip) dip = y - c.mlo;
        if (dip > c.cur_dip) c.cur_dip = dip;
        late = lock && static_cast<float>(c.since_boundary) >= kLateStart * c.ref_period &&
               dip >= kOwnDip * amp_mem && c.run_hi - c.fire_top >= kLevelRise * amp_mem;
      }
      const float thr = c.run_hi - d;
      if (late) {
        // Jälkiraja kuopan pohjaan: integraali katkaistaan siihen kohtaan,
        // ja pulssi on jo nähty pohjaan asti, joten se mitataan heti.
        const float integral = c.acc_mlo;
        onBoundary(c, c.t_mlo - 1u, 1.0f, integral, c.mlo);
        c.acc = c.acc + area - integral;
        c.since_boundary = n_ - c.t_mlo;
        segReset(c.cur, c.mlo, true);
        segAdd(c.cur, y);
        c.bnd_decayed = true;
        c.run_lo = c.mlo;
        c.t_lo = c.t_mlo;
        startPulse(c);
        c.run_hi = y;
        c.mlo = y;
      } else if (y < thr) {
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
        if (c.cur_dip > c.max_dip) c.max_dip = c.cur_dip;
        onBoundary(c, n_ - 1u, f, c.acc + part, y);
        c.acc = area - part;
        c.armed = false;
        c.run_lo = y;
        c.t_lo = n_;
      } else {
        c.acc += area;
      }
    }

    // Rajapulssin nouseva reuna merkkitasolla -> pulssi mitattu.
    if (c.wait_rise && y >= c.mark_lvl && c.y_prev < c.mark_lvl) {
      finishPulse(c, n_ - 1u, (c.mark_lvl - c.y_prev) / (y - c.y_prev));
    }

    if (locked(c)) {
      // Lukittuna segmenttejä ei vieritetä ajan perusteella (vaimennus hoitaa
      // pienentyneen pulssin), mutta näin pitkä väli ei ole jakso.
      if (c.since_boundary >= window_n_) {
        c.chain = false;
        c.acc = 0.0f;
        c.pending = false;
        c.hold = false;
        c.pair = false;
        c.m_valid = false;
      }
    } else if (c.seg_len >= window_n_) {
      // Haussa amplitudiarvio vanhenee: jos rajaa ei tule pisimmän
      // hyväksyttävän jakson aikana, vanha segmentti vieritetään pois.
      // Kuluvaa väliä ei arvioida (ei kokonainen raja-raja-segmentti).
      roll(c, y, false);
      c.env_from_cycle = false;
      c.pending = false;
      c.hold = false;
      if (c.since_boundary >= window_n_) {
        c.chain = false;
        c.acc = 0.0f;
        c.m_valid = false;
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

  void onBoundary(Chan& c, uint32_t b_int, float f, float integral, float y) {
    // Raja on välillä [b_int, b_int + 1], murto-osa f. Jakso lasketaan
    // kokonaislukuerotuksena (kääntyy oikein uint32-ylivuodossa ~49 vrk:n
    // jälkeen) plus murto-osien erotus - float-aikaleima menettäisi
    // tarkkuutensa jo tunneissa.
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
    c.pending = false;  // ratkeaa aina pulssin mittauksessa, joka edeltää rajaa
    if (c.wait_rise) {
      // Edellinen pulssi ei noussut merkkitasolle ennen uutta rajaa: sen
      // keskikohtaa ei tunneta, joten seuraavaa jaksoa ei voi mitata.
      c.wait_rise = false;
      c.m_valid = false;
    }
    // Seuraava väli arvioidaan vain, jos tämä raja oli imupulssi. Ketjun
    // ensimmäinen väli alkaa mielivaltaisesta kohdasta (käynnistys,
    // vanheneminen), ja sen amplitudi voi sattumalta osua vuotopulssin
    // kokoon - siksi arvioidaan vasta toisesta kokonaisesta välistä alkaen.
    const bool next_trusted = c.chain;
    bool cand = false;
    if (c.chain && c.env_from_cycle) {
      // Haussa kolmen peräkkäisen rajapulssin on oltava samankokoisia (tämä
      // ja edellinen alkupulssi tässä, loppupulssi vahvistuksessa). Lukittuna
      // vertailu tehdään syvyyksillä vahvistuksessa (ks. confirmLocked).
      const bool ok_period = period >= min_period_ && period <= max_period_;
      const bool ok_amp = ampOk(amp, period, c.last_period);
      const bool ok_env = lock || (c.last_amp >= kEnvStableFrac * amp && amp >= kEnvStableFrac * c.last_amp);
      if (ok_period && ok_amp && ok_env) {
        cand = true;
        c.pending = true;
        c.pend.integral = integral;
        c.pend.span = period;
        c.pend.period = period;
        c.pend.cmin = cmin;
        c.pend.p2p = amp;
        c.pend_amp = amp;
      }
    }
    if (!cand) c.hold = false;
    c.last_amp = amp;
    c.last_period = period;
    c.fire_top = c.run_hi;  // huippu ennen tätä rajaa
    c.env_from_cycle = next_trusted;
    c.bnd_amp = amplitude(c, lock);
    c.bnd_decayed = c.decay < kFreshDecay;
    c.pend_dip = c.max_dip;
    c.max_dip = 0.0f;
    c.cur_dip = 0.0f;
    c.decay = 1.0f;
    roll(c, y, true);
    c.since_boundary = 0;
    c.b_int = b_int;
    c.b_frac = f;
    c.chain = true;
  }

  // Rajapulssin mittaus virityshetkellä (pohja tunnetaan): merkkitason
  // laskeva ylitys haetaan rengaspuskurista taaksepäin pohjasta, nouseva
  // pohjan jälkeen puskurista tai seuraavista näytteistä (finishPulse).
  void startPulse(Chan& c) {
    c.pulse_depth = c.fire_top - c.run_lo;
    const float lvl = c.run_lo + kMarkFrac * c.pulse_depth;
    c.mark_lvl = lvl;
    bool found = false;
    for (uint32_t k = 0; k < static_cast<uint32_t>(kRing - 2); ++k) {
      const uint32_t j1 = c.t_lo - k;
      const uint32_t j0 = j1 - 1u;
      if (n_ - j0 >= static_cast<uint32_t>(kRing)) break;
      const float y0 = c.ring[j0 % kRing];
      const float y1 = c.ring[j1 % kRing];
      if (y0 >= lvl && y1 < lvl) {
        c.hf_int = j0;
        c.hf_frac = (y0 - lvl) / (y0 - y1);
        found = true;
        break;
      }
    }
    if (!found) {
      // Lasku kesti yli puskurin (< 300 rpm): raja kelpaa laskevaksi reunaksi.
      c.hf_int = c.b_int;
      c.hf_frac = c.b_frac;
    }
    for (uint32_t j = c.t_lo; j != n_; ++j) {
      const float y0 = c.ring[j % kRing];
      const float y1 = c.ring[(j + 1u) % kRing];
      if (y0 < lvl && y1 >= lvl) {
        finishPulse(c, j, (lvl - y0) / (y1 - y0));
        return;
      }
    }
    c.wait_rise = true;
  }

  // Pulssi mitattu: keskikohta, jakso edellisestä keskikohdasta, A:n
  // seuranta ja odottavan syklin vahvistus.
  void finishPulse(Chan& c, uint32_t hr_int, float hr_frac) {
    c.wait_rise = false;
    const uint32_t ci = c.hf_int;
    const float cf = c.hf_frac + 0.5f * (static_cast<float>(hr_int - c.hf_int) + (hr_frac - c.hf_frac));
    const float mperiod = c.m_valid ? static_cast<float>(ci - c.m_int) + (cf - c.m_frac) : 0.0f;
    const bool mk = mperiod > 0.0f;
    c.m_int = ci;
    c.m_frac = cf;
    c.m_valid = c.chain;
    const float depth = c.pulse_depth;
    bool fresh = false;
    if (depth >= kFullPulse * c.bnd_amp) {
      // Täysikokoinen pulssi: odotettu jakso (vaimennuksen viite) = väli
      // edelliseen täysikokoiseen. Rpm-pudotuksen jälkeen viite on vielä
      // vanha (lyhyt), mutta omat pulssit ovat täysikokoisia ja välissä
      // olevat vuotopulssit eivät.
      if (c.nat_valid) {
        const float iv = static_cast<float>(ci - c.nat_int) + (cf - c.nat_frac);
        if (iv > 0.0f && iv < static_cast<float>(window_n_)) c.nat_period = iv;
      }
      c.nat_int = ci;
      c.nat_frac = cf;
      c.nat_valid = true;
    } else if (c.bnd_decayed) {
      // Pieni pulssi laukaisi vasta vaimennetulla A:lla: vanha (isompi)
      // pulssi unohdetaan, jotta seuraava samankokoinen laukaisee.
      segReset(c.prev, c.run_lo, true);
      c.prev.hi = c.fire_top;
      fresh = true;
    }
    if (c.pair) {
      // Odottava pari kirjataan, ellei tämä pulssi ole moninkertaisesti
      // syvempi (silloin pari oli vuotopulsseja ja tämä vasta oma pulssi).
      c.pair = false;
      c.hold = false;
      if (depth <= c.last_depth / kDepthRatio && locked(c)) {
        recordCycle(c, c.held);
        recordCycle(c, c.paired);
        c.last_valid = c.paired.period;
      }
    }
    if (c.pending) {
      c.pending = false;
      if (mk) c.pend.period = mperiod;
      if (locked(c)) {
        confirmLocked(c, depth, mk);
      } else if (mk && depth >= kEnvStableFrac * c.pend_amp && c.pend_amp >= kEnvStableFrac * depth) {
        // Haussa: ristivuodolla kahden vuotopulssin väli ja vuotopulssista
        // omaan pulssiin ulottuva väli näyttävät amplitudin puolesta yhtä
        // johdonmukaisilta - ja niiden jakso on T/3, eli rpm kolminkertainen.
        // Ero näkyy vasta, kun rajan laukaissut pulssi on nähty pohjaan asti.
        // Hyväksytty sykli vaatii siis kolme peräkkäistä samankokoista
        // pulssia. Vuotopulsseja on peräkkäin enintään kaksi ja ne ovat
        // xtalk <= 0,5:llä alle 0,7 x oma, joten kolme samankokoista voivat
        // olla vain omia.
        c.hold = false;
        recordCycle(c, c.pend);
      } else {
        // Hylätyn syklin jälkeinen väli alkoi samalta epäilyttävältä rajalta.
        c.env_from_cycle = false;
        c.hold = false;
      }
    }
    const float base = fresh || depth > c.last_depth ? depth : c.last_depth;
    c.floor_amp = kFloorFrac * base;
    c.last_depth = depth;
  }

  // Lukitun syklin vahvistus. Molempien rajapulssien on oltava omia pulsseja:
  //  - syvyydet peräkkäin 0,45...2,2-kertaiset (vuotopulssi <= 0,37 x oma);
  //  - pohja enintään 0,4 x jakson päässä merkistä: tason askel laukaisee
  //    rajan ilman pulssia, ja sen "pohja" on vasta seuraavan pulssin;
  //  - jos rajapulssit eivät ole samankokoisia (kaasu), välissä ei saa olla
  //    omankokoista kuoppaa: se on ohitettu pulssi, ja väli kaksi sykliä,
  //    joka voi kiihdytyksessä osua vanhan viitteen pituiseksi.
  void confirmLocked(Chan& c, float depth, bool mk) {
    const float last = c.last_depth > 0.0f ? c.last_depth : depth;
    bool ok = mk && depth >= kDepthRatio * last && kDepthRatio * depth <= last;
    if (static_cast<float>(c.t_lo - c.hf_int) - c.hf_frac > kBottomDelay * c.pend.period) ok = false;
    const bool steady = depth >= kEnvStableFrac * last && kEnvStableFrac * depth <= last;
    const float maxd = depth > last ? depth : last;
    const float mind = depth < last ? depth : last;
    if (!steady && c.pend_dip >= kOwnDip * mind && c.pend_dip >= 0.4f * maxd) ok = false;
    if (ok) {
      classify(c, last > 0.0f ? depth / last : 1.0f);
    } else {
      c.hold = false;
      c.last_valid = 0.0f;
    }
  }

  // Jaksologiikka lukittuna. Viitteen (viimeisin hyväksytty jakso) mukainen
  // sykli kirjataan heti. Poikkeava pidetään ja ratkaistaan seuraavalla
  // kelvollisella syklillä P:
  //  - jatko samaan suuntaan (kiihtyy: P/H 0,55...1,2, hidastuu: 0,8...1,8)
  //    tai molemmat viitteen ympärillä käynnin oman hajonnan sisällä ->
  //    molemmat kirjataan ja viite vaihtuu (kierrosluku muuttui);
  //  - muuten pidetty hylätään ja P arvioidaan itse.
  // Tämä korvaa kiinteät suhderajat ja "kahden hylkäyksen jälkeen
  // hyväksytään" -säännön, joka ei koskaan täyttynyt rpm-pudotuksessa
  // (vanhenemisaika katkaisi välit) ja hyväksyi kiihdytyksessä väärän jakson.
  // Väärä raja jakaa syklin osiin (2T/3 + T/3, askel tau + T - tau), joiden
  // peräkkäiset suhteet eivät ole jatkoa toisilleen tai summautuvat
  // viitejaksoon; puuttuva pulssi (2T, T) palaa viitteeseen.
  void classify(Chan& c, float q) {
    const float r = c.ref_period;
    const float p = c.pend.period;
    // Edellinen kelvollinen jakso selvästi viitettä lyhyempi: kierrokset
    // nousevat, ja viitteen mittainen väli on silloin todennäköisemmin kaksi
    // sykliä ohitetulla pulssilla.
    const bool fast_before = c.last_valid > 0.0f && c.last_valid < (1.0f - kSplitSuspect) * r;
    c.last_valid = p;
    if (c.hold) {
      const float h = c.held.period;
      // Jaettu sykli: erikokoiset osat summautuvat viitejaksoon.
      const bool split = h < r && within(h + p, r, kLockPeriodWindow) && !within(p, h, kSplitSuspect);
      // Molemmat viitteen ympärillä: hyväksytään, jos poikkeama mahtuu käynnin
      // omaan hajontaan (3 x cv, 5...20 %). Tasaisella käynnillä 17 %:n
      // heitto ei ole hajontaa vaan väärä raja (vuotopulssi rpm-pudotuksessa).
      float tol = c.pub.rpm > 0.0f ? 3.0f * c.sd_rpm / c.pub.rpm : kSplitSuspect;
      if (tol < 0.05f) tol = 0.05f;
      if (tol > kSplitSuspect) tol = kSplitSuspect;
      const bool jitter = within(h, r, tol) && within(p, r, tol);
      const bool trend = h < r ? (p >= kMinPeriodRatio * h && p <= (1.0f + kSplitSuspect) * h)
                               : (p >= (1.0f - kSplitSuspect) * h && p <= kMaxPeriodRatio * h);
      if (!split && (trend || jitter)) {
        if (h > kMaxPeriodRatio * r) {
          // Kierrokset putosivat kerralla yli 1,8-kertaisesti (vain /api/sim):
          // uuden käynnin vuotopulssit (T/3 välein) näyttävät kahden välin
          // ajan samalta kuin omat. Pari kirjataan vasta, kun seuraava pulssi
          // on nähty eikä se ole moninkertaisesti syvempi (finishPulse).
          c.pair = true;
          c.paired = c.pend;
          return;
        }
        c.hold = false;
        recordCycle(c, c.held);
        recordCycle(c, c.pend);
        return;
      }
      c.hold = false;
    }
    // Pulssi pieneni selvästi (kaasu auki -> kierrokset nousevat): viitettä
    // pidempi väli on todennäköisemmin kaksi sykliä ohitetulla pulssilla.
    // Kun kiihdytys on jo alkanut (viite > 5 % edeltäjäänsä lyhyempi),
    // riittää 10 %:n pienentyminen: 150 ms:n kaasunlisäyksessä nouseva taso
    // peitti pulssin, ja kahden syklin väli osui jo lyhentyneen viitteen
    // +10 %:n sisään. Vakiokierroksilla alipaineramppi pienentää pulssia
    // ilman kiihdytystä, eikä sitä silloin pidetä (ramppi ei maksa sykliä).
    const bool accel = c.prev_ref > 0.0f && r < 0.95f * c.prev_ref;
    const bool slow_on_shrink = p > 1.03f * r && (q < kEnvStableFrac || (accel && q < 0.9f));
    // Yli 10 % lyhyempi jakso hyväksytään heti vain, jos pulssi samalla
    // pieneni (kaasu auki): vakioalipaineessa niin nopea kiihtyvyys ei ole
    // mahdollinen, vaan raja on todennäköisemmin vuotopulssi.
    const float lo = q < 0.9f ? 1.0f - kSplitSuspect : 1.0f - kLockPeriodWindow;
    if (!fast_before && !slow_on_shrink && p <= (1.0f + kLockPeriodWindow) * r && p >= lo * r) {
      recordCycle(c, c.pend);
      return;
    }
    c.hold = true;
    c.held = c.pend;
  }

  void recordCycle(Chan& c, const Cyc& cy) {
    // Näyttökeskiarvo alusta, jos kierrosluku muuttui kerralla (kRestartRatio).
    const int n0 = c.cyc_count < avg_n_ ? c.cyc_count : avg_n_;
    if (n0 > 0) {
      float sp = 0.0f;
      for (int k = 0; k < n0; ++k) sp += c.cyc_per[(c.cyc_head - 1 - k + kMaxAvgCycles) % kMaxAvgCycles];
      const float avgp = sp / static_cast<float>(n0);
      if (cy.period > kRestartRatio * avgp || cy.period < kMinPeriodRatio * avgp) c.cyc_count = 0;
    }
    c.prev_ref = c.ref_period;
    c.ref_period = cy.period;
    c.cyc_int[c.cyc_head] = cy.integral;
    c.cyc_span[c.cyc_head] = cy.span;
    c.cyc_per[c.cyc_head] = cy.period;
    c.cyc_min[c.cyc_head] = cy.cmin;
    c.cyc_p2p[c.cyc_head] = cy.p2p;
    c.cyc_head = (c.cyc_head + 1) % kMaxAvgCycles;
    if (c.cyc_count < kMaxAvgCycles) ++c.cyc_count;
    c.per[c.per_head] = cy.period;
    c.per_head = (c.per_head + 1) % kMaxRpmHist;
    if (c.per_count < kMaxRpmHist) ++c.per_count;

    ++c.pub.cycles;
    c.pub.has_pulse = true;
    c.since_cycle = 0;

    // Näyttöarvot avg_cycles viimeisimmän syklin yli. Keskiarvo on
    // sum(integraali) / sum(integraalin väli) eli aikapainotettu: sama kuin
    // yksi integraali N kokonaisen syklin yli.
    const int n = c.cyc_count < avg_n_ ? c.cyc_count : avg_n_;
    float s_int = 0.0f, s_span = 0.0f, s_per = 0.0f, s_min = 0.0f, s_p2p = 0.0f;
    for (int k = 0; k < n; ++k) {
      const int i = (c.cyc_head - 1 - k + kMaxAvgCycles) % kMaxAvgCycles;
      s_int += c.cyc_int[i];
      s_span += c.cyc_span[i];
      s_per += c.cyc_per[i];
      s_min += c.cyc_min[i];
      s_p2p += c.cyc_p2p[i];
    }
    const float fs = static_cast<float>(kSampleRateHz);
    const float inv_n = 1.0f / static_cast<float>(n);
    c.pub.mean_kpa = s_int / s_span;
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
        const float dv = 120.0f * fs / c.per[(c.per_head - 1 - k + kMaxRpmHist) % kMaxRpmHist] - avg;
        ss += dv * dv;
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
