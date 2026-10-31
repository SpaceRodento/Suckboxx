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
//   kPa ──┬──► EMA ───────────────────────────────────────► level_kpa (aina voimassa)
//         ├──► 4. kert. Butterworth-LP ──► suhteellinen Schmitt ──► pulssiehdokas
//         │      (rengaspuskuri)            └► keskikohta, omistajuus ──► oma pulssi
//         └──► suodattamaton integraali omasta keskikohdasta omaan ──► D1-keskiarvo
//   mV ───────► vika- (rivi 7) ja saturaatiomonitori (rivi 10)
//
// Suodatettua signaalia käytetään VAIN siihen, missä pulssit ovat. Keskiarvo
// lasketaan suodattamattomasta signaalista täsmälleen yhden syklin yli (oman
// pulssin keskikohdasta seuraavaan), jolloin letkuresonanssi ja kohina
// keskiarvoistuvat pois ilman suotimen viivettä (D1). Jakso ja rpm tulevat
// samoista keskikohdista: laskevan ja nousevan reunan puoliväli tasolla pohja +
// 0,3 x syvyys, joka ei riipu laukaisukynnyksestä eikä pulssin koosta.
//
// Pulssin omistajuus (isOwn). Sylinterien imut ovat 240 kampiasteen välein, ja
// Dsp saa kaikki kolme kanavaa samalla näytteellä. Vuotopulssin (xtalk) keskellä
// sen aiheuttanut sylinteri on omassa imussaan, joten sen kanava putoaa samaan
// aikaan 1/xtalk-kertaisesti; oman pulssin keskellä muut kanavat näkevät vain
// vuodon. Pulssi on oma, kun sen pudotus on >= kOwnMargin (25 %) suurempi kuin
// minkään muun kanavan samanaikainen pudotus: vuodolla ero on negatiivinen,
// yhteisellä alipaineen askeleella ~0, omalla pulssilla 1 - xtalk >= 0,5. Suhde
// ei riipu sylinterien pulssien keskinäisestä koosta. Vuotopulssi ei koskaan
// ole syklin raja eikä päivitä viitteitä. Toinen kanava kantaa tietoa, kun sillä
// on has_pulse, anturi kunnossa ja oma pulssi pisimmän jakson sisällä.
//
// Sykli = kaksi peräkkäistä omaa pulssia. Kirjataan, kun
//  - jakso on rpm-alueella ja pulssi läpäisee amplitudiehdon (ampOk);
//  - haussa (ei has_pulse) kolme peräkkäistä omaa pulssia ovat samankokoisia;
//  - lomitus: jokaisella tietoa kantavalla kanavalla on syklin sisällä
//    täsmälleen yksi oma pulssi, 1/3:n ja 2/3:n kohdalla +- kSlotTol. Ohitettu
//    pulssi (2T) siirtää merkit 1/6 jaksoa paikoiltaan, myös silloin kun kaikki
//    kanavat ohittivat saman kierroksen. Lomitus ei riipu kierrosluvun
//    muutoksesta, joten uusi jakso kelpaa heti ilman viitettä.
//  - varatila (muut kanavat eivät kanna tietoa, esim. kaksi letkua irti):
//    rajapulssit samankokoiset ja jakso viitteen +- kFallbackWindow.
// Jakson hypätessä yli 1,5-kertaiseksi näytön keskiarvo alkaa alusta seuraavasta
// syklistä (askeleen ylittävä sykli ei jää näyttöön).
//
// Laukaisu (suhteellinen Schmitt) tarvitsee pulssin koon A. Kaasunlisäyksessä
// kaikkien sylinterien pulssit kutistuvat yhtä aikaa: A laskee muiden kanavien
// tuoreeseen omaan syvyyteen (peerDepth), ja jos rajaa ei tule 1,2 x jakson
// aikana, A vaimenee (kDecayStart). Vuotopulssi saa laukaista - omistajuus
// hylkää sen.
//
// Mitatut rajat (EngineSim -> mV -> nimellinen kPa -> Dsp, oletusparametrit,
// 1 % jitter; 4 siementä x xtalk 0/0,3 x 6 vaihetta, C:ssä x xtalk 0,5 x 16
// vaihetta; väärä sykli = jokin merkki (loppu, loppu - jakso) osuu lähimpänä
// toisen sylinterin imuun tai merkit eivät ole peräkkäiset omat imut):
//  - Väärät syklit: 0 kaikissa rpm-pudotuksissa (3000...6000 -> 600...1500,
//    askel, ramppi 150 ms-1 s, kaasun katkaisu 150-700 ms), blipeissä (850/950
//    -> 1500...3000 rpm, nousu 150-600 ms, pikablipit, tuplablipit) ja
//    alipaineen askelissa ja rampeissa vakiokierroksilla (8-25 kPa, 20-200 ms,
//    600-950 rpm, xtalk 0...0,5, jitter 1 ja 5 %). Vakiokierroksilla kanavan
//    rpm ei ylitä 1,05 x todellista.
//  - has_pulse ei putoa näissä. Pisin väli kirjattujen syklien välillä:
//    rpm-askel alas <= 2 uutta jaksoa + 10 ms (850 rpm: 290 ms, 600 rpm: 382
//    ms), kaasun katkaisu 4500 -> 1500 / 150 ms 278 ms; blip nousu >= 300 ms
//    <= 272 ms, nousu 150 ms <= 358 ms, pikablip <= 393 ms; alipaineen askel
//    <= 3,04 jaksoa (5 % jitter 3,21), ramppi 200 ms 1,05 jaksoa, 20-100 ms
//    <= 3,03 jaksoa (peittää askeleen tavoin yhden pulssin).
//  - Jokaisen kanavan rpm ja keskiarvo oikein 1,5 s muutoksen jälkeen (rpm
//    0,01 %, keskiarvo <= 0,051 kPa); RpmEstimate 1,2 s jälkeen <= 0,2 %.
//  - Yksittäisen syklin jakso vs saman syklin toteutunut (imun keskeltä
//    keskelle) < 3 %, paitsi: 40 kPa / 300 ms kaasun katkaisu 3000 rpm:stä <= 9,6
//    % ja 4500 -> 1500 / 150 ms <= 7,3 % (suodatettu 4 kPa:n pulssi rampin
//    päällä: keskikohta siirtyy, raja on silti oikea); kerralla vaihtuvan rpm:n
//    ylittävä sykli <= 16,3 % (6000 -> 850: pulssi puoliksi 7 x hitaampi) ja
//    sen naapurisykli <= 5,3 %.
//  - Lukitus käyvään signaaliin tai käynnistyksessä: <= 3,5 sykliä + 40 ms
//    (EngineSim ilmanpaineesta: 600 rpm 651 ms, 850 rpm 465 ms, 1500 rpm 271
//    ms, 3000 rpm 184 ms).
//  - Pienin havaittava imupulssi: 850-1500 rpm 1-1,5 kPa, 3000 rpm 2, 4500 rpm
//    2,5, 6000 rpm 4,5 kPa; xtalk 0,3:lla 3 / 3,5 / 6,5 kPa (6000 rpm 4 kPa
//    xtalk 0,3: 89 % sykleistä). Rpm-virhe rajallakin <= 1 %.
//  - Kohina: hyst_min_kpa 0,3 kestää 0,4 kPa RMS (+ 3 kPa 112 Hz) ilman yhtään
//    väärää sykliä; 0,6-0,8 kPa vaatii ~0,5 (S5a).
//  - Hajonta (rivi 2): jitter 5 % -> cv 0,046, 15 % -> 0,135.
//  - Viive: sykli kirjataan, kun sen päättänyt pulssi on noussut takaisin
//    merkkitasolle, ~15-20 ms imun keskikohdan jälkeen.
//  - Suunnitteluraja xtalk: omistajuus 0,5 asti (marginaali 1 - xtalk >= 0,5 >
//    kOwnMargin); varatila (yksi kanava) 0,3. Letkujen viive-ero (R1: samat
//    letkut +-2 cm, < 0,1 ms) saa olla enintään ~0,1 x jakso - jitter: 3 ms
//    toimii 1500 rpm:iin, 3000 rpm:stä ylöspäin ei (lomitus hylkää syklit,
//    pudotuksissa 4500/6000 -> 1500 kirjautui vuotosyklejä).
//  - Varatilan rajat (kaksi letkua irti; tila on joka tapauksessa FaultHose):
//    rpm-muutos yli +-10 % syklissä katkaisee lukituksen (uusi haku ~1 s), ja
//    hetkellinen 3000 -> 850 / 4500 -> 1500 rpm:n pudotus xtalk 0,3:lla kirjasi
//    vuotosyklin 2-5 ajossa 48:sta: yhdeltä kanavalta uuden käynnin vuotopulssi
//    (xtalk x kasvanut pulssi) on vanhan oman kokoinen ja osuu vanhaan
//    jaksoon. Yksi letku irti + sama pudotus 4500 -> 1500: 4/48 (omistajan
//    kanava on tasainen ja toisen kanavan oma palautuminen peittää vuodon).
//
// Testikoukku: jos SBX_DSP_CYCLE_HOOK(kanava, jakso, loppu_int, loppu_frac) on
// määritelty ennen tätä otsaketta, recordCycle kutsuu sitä jokaisesta
// kirjatusta syklistä (native-testit). Määrittelemättömänä ei koodia eikä tilaa.

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

  // Jaksoaikarajojen toleranssi: rpm_min/rpm_max ovat nimellisrajoja, ja ilman
  // toleranssia moottori juuri rajalla hylkäisi jitterin takia joka toisen syklin.
  static constexpr float kPeriodTolerance = 0.10f;
  // Pienin hyväksyttävä (suodatettu) pulssi = tämä x hyst_min_kpa: suodatettu
  // kohina ylittää pelkän hystereesin satunnaisesti (rivit 1 ja 6).
  static constexpr float kMinPulseHystFactor = 3.0f;
  // Raja skaalataan suotimen vahvistuksella syklin perustaajuudella (6000 rpm
  // = 50 Hz -> 0,38); alaraja estää rajan katoamisen, jos rpm_max nostetaan.
  static constexpr float kMinGateGain = 0.25f;
  // Skaalatun rajan varassa hyväksytyn syklin jakso tämän suhteen sisällä
  // edellisestä: kohina ei tuota peräkkäisiä samanmittaisia "syklejä".
  static constexpr float kSmallPulseRatio = 1.25f;
  // Lukituksen haku: kolme peräkkäistä omaa pulssia syvyydeltään tämän suhteen
  // sisällä (moottorin käynnistyksen askel ja kohina eivät toistu tasaisina).
  static constexpr float kEnvStableFrac = 0.7f;

  // Pulssin omistajuus (ks. isOwn). Oma pulssi: sen pudotus on vähintään
  // tämän osuuden verran suurempi kuin minkään muun kanavan samanaikainen
  // pudotus. Vuotopulssilla ero on negatiivinen (omistajan kanava putoaa
  // 1/xtalk-kertaisesti), alipaineaskeleella ~0 (kaikki putoavat yhtä paljon),
  // omalla pulssilla (1 - xtalk) >= 0,5 xtalk 0,5:een asti.
  static constexpr float kOwnMargin = 0.25f;
  // "Samanaikainen" pudotus = minimi pulssin keskikohdan +- tämän monen
  // näytteen sisällä. Suodatetun pulssin pohja on leveä (joutokäynnillä 3 ms:n
  // viive-ero letkuissa muuttaa sitä ~2 %), mutta 6000 rpm:llä naapurisylinterin
  // pulssi on vain 6,7 ms päässä: leveämpi ikkuna (0,25 x pulssin leveys)
  // kasvatti naapurin pudotusta ja hukkasi pieniä omia pulsseja (821 vs 892/1001).
  static constexpr float kOwnHalfWindow = 1.0f;
  // Varatilassa jakso hyväksytään vain viitteen +- tämän sisällä. Yhdeltä
  // kanavalta ei voi erottaa rpm-pudotuksen jälkeistä vuotopulssia (xtalk x
  // kasvanut pulssi) vanhan nopeuden omasta: väli voi osua mihin tahansa
  // 0,33...1,17 x vanhaa jaksoa. Kapea ikkuna hylkää useimmat (mitattu: 54 -> 18
  // ajoa 192:sta); suurempi rpm-muutos maksaa lukituksen ja uuden haun.
  static constexpr float kFallbackWindow = 0.1f;
  // Näytön keskiarvon uudelleenaloituksen alaraja (ks. kRestartRatio):
  // kampiakselin nopeus ei kasva syklissä 1,8-kertaiseksi (~2,8 sigmaa cv:llä
  // 0,15, rivi 2).
  static constexpr float kMinPeriodRatio = 0.55f;
  // A:n vaimennus lukittuna: jos rajaa ei ole tullut 1,2 x viitejakson aikana,
  // A vaimenee aikavakiolla 0,3 x jakso. Kaasunlisäyksessä pulssi kutistuu (20
  // -> 4,5 kPa suodatettuna kahdessa syklissä), ja vanha A pitäisi kynnyksen
  // pulssin yläpuolella.
  static constexpr float kDecayStart = 1.2f;
  static constexpr float kDecayTau = 0.3f;
  // Pulssin keskikohtamerkin taso: pohja + 0,3 x syvyys. Lähellä pohjaa
  // laskeva ja nouseva reuna ovat symmetrisiä, eikä alipaineen ramppi siirrä
  // keskikohtaa; 0,5:llä rampin ylittävä sykli heitti 5 %.
  static constexpr float kMarkFrac = 0.3f;
  // Suodatetun ja suodattamattoman signaalin rengaspuskuri: merkin reunat,
  // omistajuuden ikkunat ja integraalin katkaisu ovat <= ~60 ms taaksepäin.
  static constexpr int kRing = 64;
  // Lomituksen paikkatoleranssi (osuus jaksosta): toisen kanavan oma pulssi
  // 1/3:n tai 2/3:n kohdalla +- tämä. Jitter 5 % (3 sigmaa ~0,08) ja
  // kiihdytys siirtävät sitä < 0,1; ohitetun pulssin välissä (2T) merkit ovat
  // 1/6 sivussa. 0,125 päästi kiihdytyksessä 3T-välin (sivussa 0,12); letkujen
  // viive-ero (R1: < 0,1 ms) syö tästä 6000 rpm:llä 0,005 / ms.
  static constexpr float kSlotTol = 0.1f;
  // Kanavan muistamat omat keskikohdat (lomitustarkistus muille kanaville):
  // kolme riittää toteamaan, osuuko väliin kaksi.
  static constexpr int kOwnHist = 3;
  // Näytön keskiarvo aloitetaan alusta, jos uusi jakso on yli 1,5 x (tai alle
  // kMinPeriodRatio x) näytettävien syklien keskijakso: kierrosluku muuttui
  // kerralla, eikä vanhan käynnin syklejä ole syytä näyttää vielä sekunnin ajan.
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
    // Lukituksen haussa amplitudiarvio vanhenee, jos rajaa ei ole tullut
    // pisimmän hyväksyttävän jakson aikana.
    window_n_ = static_cast<uint32_t>(max_period_) + 1u;
    // Suotimen asettumisaika (4. kert. Butterworthin askelvaste asettuu 2 %:iin
    // ~1,3/fc:ssä). Ensimmäinen näyte voi osua resonanssin huippuun, jolloin
    // prime() jättää muutaman kPa:n soimisen; sen aikana amplitudia ei
    // kerätä, muuten ensimmäiset kynnykset olisivat liian leveät.
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
  // vikamonitoreille). Ensin kaikki kanavat suotimen läpi ja renkaisiin, sitten
  // tunnistus: pulssin omistajuus vertaa saman näytehetken muita kanavia.
  // O(1) per kanava: jokainen silmukka on rajattu kRing:llä ja ajetaan
  // korkeintaan kerran pulssia kohden; hyväksytyn syklin kohdalla
  // O(avg_cycles + rpm_hist).
  void push(const float (&kpa)[kChannels], const RawFrame& raw) {
    ++n_;
    for (int i = 0; i < kChannels; ++i) front(ch_[i], kpa[i], raw.mv[i]);
    for (int i = 0; i < kChannels; ++i) {
      if (ch_[i].ready) detect(i);
    }
  }

  // mean/min/p2p säilyttävät viimeisen arvonsa, kun has_pulse putoaa
  // (types.h: voimassa vain kun has_pulse); rpm nollataan.
  const ChannelState& channel(int i) const {
    return ch_[(i >= 0 && i < kChannels) ? i : 0].pub;
  }

  // Kierrosluku = pulssivien kanavien mediaani. Hajonta lasketaan kanavittain
  // ja siitäkin otetaan mediaani (ei yhdistetä jaksoja): kaikki kanavat näkevät
  // saman kampiakselin, joten aito sykli-sykliltä vaihtelu näkyy jokaisessa,
  // mutta yhden kanavan tunnistusvirhe jää mediaanissa vähemmistöön.
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
  // (= Schmitt-rajojen väli, tai haussa vanhenemisaika jos rajaa ei tule).
  struct Seg {
    float hi = 0.0f, lo = 0.0f;
    float ru = 0.0f;  // suurin nousu: pohjasta myöhempään huippuun
    float dd = 0.0f;  // suurin lasku: huipusta myöhempään pohjaan
  };

  // Kirjattava sykli: integraali (kPa x näyte), jakso = integraalin väli
  // (keskikohdasta keskikohtaan), suodatettu pohja ja pulssin syvyys.
  struct Cyc {
    float integral, period, cmin, p2p;
    uint32_t end_int;  // loppumerkki (keskikohta) näyteindeksinä + murto-osa
    float end_frac;
  };

  struct Chan {
    ChannelState pub;
    ButterLp4 lp;
    bool primed = false;
    bool ready = false;   // tunnistus ajetaan tällä näytteellä (asettunut)
    uint32_t settle = 0;  // näytteitä jäljellä suotimen asettumiseen
    float x = 0.0f, y = 0.0f;            // kuluva näyte: kalibroitu ja suodatettu
    float x_prev = 0.0f, y_prev = 0.0f;  // edellinen

    // Suhteellinen Schmitt (ks. detect): amplitudi kahdesta viimeisimmästä
    // segmentistä, run_hi = huippu virityksen jälkeen, run_lo = pohja rajan jälkeen.
    Seg prev, cur;
    uint32_t seg_len = 0;
    float run_hi = 0.0f, run_lo = 0.0f;
    uint32_t t_lo = 0;           // pohjan näyteindeksi
    bool armed = false;
    uint32_t since_boundary = 0;
    float decay = 1.0f;          // A:n vaimennus lukittuna (kDecayStart)
    float decay_k = 1.0f;        // kerroin per näyte jaksolle decay_for
    float decay_for = 0.0f;
    float fire_top = 0.0f;       // huippu ennen viimeisintä rajaa

    // Rengaspuskurit: suodatettu (merkki, omistajuus) ja kalibroitu (integraali).
    float ring[kRing] = {};
    float xring[kRing] = {};

    // Rajapulssin mittaus: keskikohta laskevan (hf) ja nousevan reunan
    // puolivälistä merkkitasolla.
    bool wait_rise = false;      // odotetaan nousevaa reunaa merkkitasolle
    float mark_lvl = 0.0f;
    float pulse_depth = 0.0f;    // lasku huipusta pohjaan
    uint32_t hf_int = 0;
    float hf_frac = 0.0f;

    // Omat pulssit: keskikohdat (uusin ensin), syvyydet ja integraali
    // viimeisimmästä omasta keskikohdasta.
    uint32_t own_int[kOwnHist] = {};
    float own_frac[kOwnHist] = {};
    int own_n = 0;
    float own_d1 = 0.0f, own_d2 = 0.0f;  // kahden edellisen oman pulssin syvyys
    float acc = 0.0f;            // suodattamaton integraali viimeisimmästä omasta keskikohdasta
    bool chain = false;          // acc on voimassa (keskikohta oli puskurissa)
    float last_period = 0.0f;    // edellinen oma-oma-väli (pienen pulssin rajaehto)
    float ref_period = 0.0f;     // viimeisin hyväksytty jakso, 0 = ei viitettä
    uint32_t since_cycle = 0;    // näytteitä viimeisestä hyväksytystä syklistä

    // Syklihistoria. Keskiarvo sum(integraali) / sum(jakso) on aikapainotettu
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

  static bool similar(float a, float b) { return a >= kEnvStableFrac * b && b >= kEnvStableFrac * a; }

  static void segReset(Seg& s, float y) {
    s.hi = s.lo = y;
    s.ru = s.dd = 0.0f;
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
    c.primed = c.ready = false;
    c.settle = 0;
    c.x = c.y = c.x_prev = c.y_prev = 0.0f;
    segReset(c.prev, 0.0f);
    segReset(c.cur, 0.0f);
    c.seg_len = 0;
    c.run_hi = c.run_lo = 0.0f;
    c.t_lo = 0;
    c.armed = false;
    c.since_boundary = 0;
    c.decay = c.decay_k = 1.0f;
    c.decay_for = c.fire_top = 0.0f;
    c.wait_rise = false;
    c.mark_lvl = c.pulse_depth = 0.0f;
    c.hf_int = 0;
    c.hf_frac = 0.0f;
    c.own_n = 0;
    c.own_d1 = c.own_d2 = 0.0f;
    c.acc = 0.0f;
    c.chain = false;
    c.last_period = c.ref_period = 0.0f;
    c.since_cycle = 0;
    c.cyc_head = c.cyc_count = 0;
    c.per_head = c.per_count = 0;
    c.sd_rpm = 0.0f;
    c.level = 0.0f;
    c.fault_run = 0;
    c.since_sat = kSatWindowSamples;  // ei saturaatiota ennen ensimmäistä havaintoa
  }

  // Ajanhetken (kokonais- + murto-osa näytteinä) ikä nyt, näytteinä. Erotus
  // kokonaisluvuin kääntyy oikein uint32-ylivuodossa (~49 vrk).
  float age(uint32_t t_int, float t_frac) const { return static_cast<float>(n_ - t_int) - t_frac; }

  // Pulssin huippu-huippu A kahden viimeisimmän segmentin yli.
  //
  // Lukittuna A = min(suurin nousu, suurin lasku). Jaksollisella signaalilla
  // molemmat ovat p2p. Tason askel kasvattaa vain toista (alipaine kasvaa:
  // laskua, pienenee: nousua), joten minimi ei reagoi siihen - absoluuttinen
  // verhokäyrä levisi askeleen verran ja jätti pulssit kynnysten väliin.
  //
  // Haussa A = suurin nousu. Ensimmäistä laskua ei yleensä nähdä huipusta
  // asti (asettuminen päättyy mielivaltaiseen kohtaan), ja moottorin
  // käynnistyksen askel (ilmanpaine -> -55 kPa) kasvattaa vain laskua.
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

  static void roll(Chan& c, float y) {
    c.prev = c.cur;
    segReset(c.cur, y);
    c.seg_len = 0;
  }

  // Monitorit, EMA, suodin ja renkaat - kaikille kanaville ennen tunnistusta.
  void front(Chan& c, float x, uint16_t mv) {
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
    c.ready = false;

    // NaN/Inf jäisi IIR-tiloihin pysyvästi; korvataan edellisellä näytteellä.
    if (!std::isfinite(x)) {
      if (!c.primed) return;
      x = c.x_prev;
    }
    c.pub.last_kpa = x;

    if (!c.primed) {
      // Ensimmäinen näyte: EMA, suodin ja renkaat lähtevät suoraan signaalin
      // tasolta, muuten taso ryömisi nollasta ja näyttäisi hetken "moottori
      // seis" (rivi 1), ja renkaan nollat näyttäisivät pudotukselta.
      c.primed = true;
      c.settle = settle_n_;
      c.lp.prime(x);
      c.level = x;
      c.x = c.x_prev = x;
      c.y = c.y_prev = x;
      for (int k = 0; k < kRing; ++k) c.ring[k] = c.xring[k] = x;
      c.pub.level_kpa = x;
      return;
    }

    c.level += level_alpha_ * (x - c.level);
    c.pub.level_kpa = c.level;

    // has_pulse putoaa tasan pulse_timeout_s viimeisen syklin jälkeen. Historia
    // tyhjennetään, jotta uuden käynnin luvut eivät sekoitu vanhoihin (esim.
    // letku kytketty takaisin).
    if (c.pub.has_pulse && ++c.since_cycle >= timeout_n_) {
      c.pub.has_pulse = false;
      c.pub.rpm = 0.0f;
      c.cyc_count = 0;
      c.per_count = 0;
      c.sd_rpm = 0.0f;
      c.ref_period = 0.0f;
    }

    c.x = x;
    c.y = c.lp.process(x);
    c.ring[n_ % kRing] = c.y;
    c.xring[n_ % kRing] = x;
    c.acc += 0.5f * (c.x_prev + x);  // trapetsi välille [n-1, n]

    if (c.settle > 0) {
      // Viritetty heti asettumisen jälkeen: ensimmäinen raja voi osua mihin
      // tahansa vaiheeseen, mutta lukitus vaatii kolme samankokoista omaa pulssia.
      if (--c.settle == 0) c.armed = true;
      segReset(c.prev, c.y);
      segReset(c.cur, c.y);
      c.run_hi = c.run_lo = c.y;
      c.x_prev = x;
      c.y_prev = c.y;
      return;
    }
    c.ready = true;
  }

  // Suhteellinen Schmitt: raja, kun signaali putoaa D = (A + h) / 2
  // viimeisimmän huipun alle, eli h/2 pulssin puolivälin alapuolelle;
  // viritys, kun se on noussut pohjasta hystereesin h = max(hyst_min_kpa,
  // hyst_frac x A) verran. Kynnykset kulkevat huipun ja pohjan mukana, joten
  // alipaineen muutos ei voi jättää pulsseja kiinteiden kynnysten väliin.
  // Raja vain käynnistää pulssin mittauksen: jakso ja D1-integraali tulevat
  // omien pulssien keskikohdista (finishPulse), joten vuotopulssin laukaisema
  // raja ei katkaise sykliä.
  void detect(int i) {
    Chan& c = ch_[i];
    const float y = c.y;
    segAdd(c.cur, y);
    ++c.seg_len;
    if (c.since_boundary < 0xFFFFFFFFu) ++c.since_boundary;

    const bool lock = c.pub.has_pulse;
    float amp = amplitude(c, lock);
    if (lock) {
      // Odotettu raja myöhässä: A vaimenee (kaasu auki -> pulssi kutistuu).
      if (static_cast<float>(c.since_boundary) > kDecayStart * c.ref_period) {
        if (c.ref_period != c.decay_for) {
          c.decay_for = c.ref_period;
          c.decay_k = std::exp(-1.0f / (kDecayTau * c.ref_period));
        }
        c.decay *= c.decay_k;
      }
      amp *= c.decay;
    }
    // Sylinterit jakavat kaasun, joten pulssit kutistuvat kaikilla kanavilla
    // yhtä aikaa (kaasunlisäys). Kun muut tietoa kantavat kanavat ovat jo
    // mitanneet uuden, pienemmän oman pulssin tämän kanavan viimeisimmän
    // oman jälkeen, A laskee niistä suurimpaan. Tämän kanavan vuotopulssit
    // tulevat juuri niistä sylintereistä (<= xtalk x niiden syvyys), joten ne
    // jäävät yhä laukaisutason (A + h) / 2 alle.
    const float peer = peerDepth(i);
    if (peer > 0.0f && peer < amp) amp = peer;
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
        startPulse(i);
        c.run_hi = y;
      }
    } else {
      if (y > c.run_hi) c.run_hi = y;
      const float thr = c.run_hi - d;
      if (y < thr) {
        c.fire_top = c.run_hi;
        // Edellinen pulssi ei noussut merkkitasolle ennen uutta rajaa: sen
        // keskikohtaa ei tunneta, joten se ohitetaan (oma ketju jatkuu ja
        // lomitustarkistus hylkää 2T-välin).
        c.wait_rise = false;
        roll(c, y);
        c.since_boundary = 0;
        c.decay = 1.0f;
        c.armed = false;
        c.run_lo = y;
        c.t_lo = n_;
      }
    }

    // Rajapulssin nouseva reuna merkkitasolla -> pulssi mitattu.
    if (c.wait_rise && y >= c.mark_lvl && c.y_prev < c.mark_lvl) {
      finishPulse(i, n_ - 1u, (c.mark_lvl - c.y_prev) / (y - c.y_prev));
    }

    // Haussa amplitudiarvio vanhenee: jos rajaa ei tule pisimmän hyväksyttävän
    // jakson aikana, vanha segmentti vieritetään pois (letku irti, moottori seis).
    if (!lock && c.seg_len >= window_n_) roll(c, y);

    c.x_prev = c.x;
    c.y_prev = y;
  }

  // Rajapulssin mittaus virityshetkellä (pohja tunnetaan): merkkitason
  // laskeva ylitys haetaan rengaspuskurista taaksepäin pohjasta, nouseva
  // pohjan jälkeen puskurista tai seuraavista näytteistä (finishPulse).
  // Molemmat haut on rajattu puskurin pituuteen.
  void startPulse(int i) {
    Chan& c = ch_[i];
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
    // Lasku kesti yli puskurin (vain alle rpm_min:n): keskikohtaa ei tunneta.
    if (!found) return;
    uint32_t from = c.t_lo;
    if (n_ - from > static_cast<uint32_t>(kRing - 2)) from = n_ - static_cast<uint32_t>(kRing - 2);
    for (uint32_t j = from; j != n_; ++j) {
      const float y0 = c.ring[j % kRing];
      const float y1 = c.ring[(j + 1u) % kRing];
      if (y0 < lvl && y1 >= lvl) {
        finishPulse(i, j, (lvl - y0) / (y1 - y0));
        return;
      }
    }
    c.wait_rise = true;
  }

  // Kanavan k pudotus ikkunassa: suurin arvo [hf - L, hr]:ssä (ennen pulssia)
  // miinus pienin arvo keskikohdan +- s:ssä. Iät näytteinä nyt-hetkestä.
  float fallOf(const Chan& k, float top_old, float top_new, float lo_old, float lo_new) const {
    float hi = -1.0e30f, lo = 1.0e30f;
    int a0 = static_cast<int>(top_new + 0.999f);
    int a1 = static_cast<int>(top_old);
    if (a1 > kRing - 1) a1 = kRing - 1;
    for (int a = a0; a <= a1; ++a) {
      const float v = k.ring[(n_ - static_cast<uint32_t>(a)) % kRing];
      if (v > hi) hi = v;
    }
    int b0 = static_cast<int>(lo_new + 0.999f);
    int b1 = static_cast<int>(lo_old);
    if (b1 > kRing - 1) b1 = kRing - 1;
    if (b0 > b1) b0 = b1;
    for (int a = b0; a <= b1; ++a) {
      const float v = k.ring[(n_ - static_cast<uint32_t>(a)) % kRing];
      if (v < lo) lo = v;
    }
    return hi > lo ? hi - lo : 0.0f;
  }

  // Toinen kanava kantaa tietoa, kun se seuraa omia pulssejaan: has_pulse,
  // anturi kunnossa ja oma pulssi pisimmän hyväksyttävän jakson sisällä. Ei
  // amplitudi- eikä viitevertailua: rpm-pudotuksen jälkeen muiden kanavien A ja
  // jakso ovat vielä vanhan käynnin. Ilman tuoreusehtoa irrotettu letku estäisi
  // muiden kanavien syklit lomitusehdolla koko pulse_timeout_s:n ajan.
  bool informative(int j) const {
    const Chan& o = ch_[j];
    return o.pub.has_pulse && !o.pub.sensor_fault && o.own_n > 0 &&
           age(o.own_int[0], o.own_frac[0]) < max_period_;
  }

  // Suurin muiden tietoa kantavien kanavien oma pulssisyvyys, joka on mitattu
  // tämän kanavan viimeisimmän oman pulssin jälkeen (0 = ei yhtään).
  float peerDepth(int i) const {
    const Chan& c = ch_[i];
    const float mine = c.own_n > 0 ? age(c.own_int[0], c.own_frac[0]) : 1.0e9f;
    float best = 0.0f;
    for (int j = 0; j < kChannels; ++j) {
      const Chan& o = ch_[j];
      if (j == i || o.own_n == 0 || !informative(j)) continue;
      if (age(o.own_int[0], o.own_frac[0]) < mine && o.own_d1 > best) best = o.own_d1;
    }
    return best;
  }

  // Pulssin omistajuus. Sylinterien imut ovat 240 kampiasteen välein, joten
  // vuotopulssin keskellä sen aiheuttanut sylinteri on omassa imussaan ja sen
  // kanava putoaa samaan aikaan 1/xtalk-kertaisesti; oman pulssin keskellä
  // muut kanavat näkevät vain vuodon (xtalk x). Suhde ei riipu sylinterien
  // pulssien keskinäisestä koosta. Alipaineen askel pudottaa kaikkia yhtä
  // paljon, joten sekään ei ole oma pulssi. Pudotus = suurin arvo ikkunassa
  // [hf - leveys, hr] (pulssia ennen) miinus pienin keskikohdan +-
  // kOwnHalfWindow:ssa, sama ikkuna kaikille kanaville (sama suodin = sama viive).
  // info kertoo, kantoiko jokin muu kanava tietoa (ks. informative).
  bool isOwn(int i, float m_age, float width, bool& info) const {
    const float s = kOwnHalfWindow;
    float top_new = m_age - 0.5f * width;
    if (top_new < 0.0f) top_new = 0.0f;
    float lo_new = m_age - s;
    if (lo_new < 0.0f) lo_new = 0.0f;
    const float top_old = m_age + 1.5f * width;
    const float lo_old = m_age + s;
    const float fi = fallOf(ch_[i], top_old, top_new, lo_old, lo_new);
    float fo = 0.0f;
    info = false;
    for (int j = 0; j < kChannels; ++j) {
      if (j == i || ch_[j].pub.sensor_fault) continue;
      const float f = fallOf(ch_[j], top_old, top_new, lo_old, lo_new);
      if (f > fo) fo = f;
      if (informative(j)) info = true;
    }
    // Ilman tietoa kantavia kanavia jokainen pulssi on ehdokas: syklin
    // hyväksyntä (ownPulse) vaatii silloin samankokoiset rajapulssit.
    return !info || (fi > 0.0f && fi - fo >= kOwnMargin * fi);
  }

  // Pulssi mitattu: keskikohta, omistajuus ja oman pulssin sykli.
  void finishPulse(int i, uint32_t hr_int, float hr_frac) {
    Chan& c = ch_[i];
    c.wait_rise = false;
    const float width = static_cast<float>(hr_int - c.hf_int) + (hr_frac - c.hf_frac);
    float mf = c.hf_frac + 0.5f * width;
    const float whole = std::floor(mf);
    const uint32_t mi = c.hf_int + static_cast<uint32_t>(whole);
    mf -= whole;
    bool info = false;
    const bool own = isOwn(i, age(mi, mf), width, info);
    if (own) ownPulse(i, mi, mf, info);
  }

  // Integraali keskikohdasta m = mi + mf nykyhetkeen (lineaarisesti
  // interpoloidusta signaalista, kuten trapetsi).
  float tailIntegral(const Chan& c, uint32_t mi, float mf) const {
    const float x0 = c.xring[mi % kRing];
    const float x1 = c.xring[(mi + 1u) % kRing];
    const float xm = x0 + mf * (x1 - x0);
    float t = (1.0f - mf) * 0.5f * (xm + x1);
    for (uint32_t j = mi + 1u; j != n_; ++j) t += 0.5f * (c.xring[j % kRing] + c.xring[(j + 1u) % kRing]);
    return t;
  }

  // Lomitus: jokaisen tietoa kantavan toisen kanavan omia keskikohtia on
  // välillä (ikä a_new, ikä a_old) täsmälleen yksi, ja ne ovat sylinterien
  // vuorossa: 1/3:n ja 2/3:n kohdalla jaksoa (yksi vahvistava kanava: jommassa
  // kummassa). Ohitetun pulssin 2T-välissä jokin merkki on aina 1/6 jaksoa
  // sivussa paikastaan - myös kun kaikki kanavat ohittivat saman kierroksen ja
  // jokaisen ikkunaan osui yksi merkki kultakin.
  bool interleaved(int i, float a_new, float a_old, float period) const {
    float u[kChannels - 1];
    int nm = 0;
    for (int j = 0; j < kChannels; ++j) {
      if (j == i || !informative(j)) continue;
      const Chan& o = ch_[j];
      int n = 0;
      for (int k = 0; k < o.own_n; ++k) {
        const float a = age(o.own_int[k], o.own_frac[k]);
        if (a > a_new && a < a_old) {
          ++n;
          u[nm] = (a_old - a) / period;
        }
      }
      if (n != 1) return false;
      ++nm;
    }
    if (nm == 2) {
      const float lo = u[0] < u[1] ? u[0] : u[1];
      const float hi = u[0] < u[1] ? u[1] : u[0];
      return std::fabs(lo - 1.0f / 3.0f) <= kSlotTol && std::fabs(hi - 2.0f / 3.0f) <= kSlotTol;
    }
    return nm == 0 || std::fabs(u[0] - 1.0f / 3.0f) <= kSlotTol || std::fabs(u[0] - 2.0f / 3.0f) <= kSlotTol;
  }

  // Oma pulssi: sykli edellisestä omasta keskikohdasta tähän.
  //  - Lukitus haussa: kolme peräkkäistä samankokoista omaa pulssia.
  //  - Lomitus: imut kiertävät kaikki sylinterit, joten jokaisen tietoa
  //    kantavan kanavan omia pulsseja on syklin sisällä täsmälleen yksi. Kaksi
  //    = tämän kanavan pulssi jäi välistä (2T); nolla = toinen kanava ei
  //    vahvista väliä. Kaikkien on vahvistettava: nopeassa alipainerampissa
  //    kaikki kanavat ohittavat pulsseja yhtä aikaa, ja yksi vanha merkki
  //    "vahvisti" 3T-välin. Kierrosluvun muutos ei vaikuta lomitukseen, joten
  //    uusi jakso kelpaa heti ilman viitettä.
  //  - Varatila (muut kanavat eivät kanna tietoa): saman kokoiset rajapulssit
  //    ja jakso viitteen +- kFallbackWindow.
  void ownPulse(int i, uint32_t mi, float mf, bool info) {
    Chan& c = ch_[i];
    const bool cut = n_ - mi < static_cast<uint32_t>(kRing - 1);
    const float tail = cut ? tailIntegral(c, mi, mf) : 0.0f;
    float period = 0.0f;
    if (c.own_n > 0) period = static_cast<float>(mi - c.own_int[0]) + (mf - c.own_frac[0]);
    const float depth = c.pulse_depth;
    if (c.chain && cut && c.own_n > 0) {
      bool ok = period >= min_period_ && period <= max_period_ && ampOk(depth, period, c.last_period);
      if (ok && !c.pub.has_pulse) ok = similar(depth, c.own_d1) && similar(c.own_d1, c.own_d2);
      if (ok && info) {
        ok = interleaved(i, age(mi, mf), age(c.own_int[0], c.own_frac[0]), period);
      } else if (ok && c.pub.has_pulse) {
        ok = similar(depth, c.own_d1) && period <= (1.0f + kFallbackWindow) * c.ref_period &&
             period >= (1.0f - kFallbackWindow) * c.ref_period;
      }
      if (ok) {
        Cyc cy;
        cy.integral = c.acc - tail;
        cy.period = period;
        cy.cmin = c.run_lo;
        cy.p2p = depth;
        cy.end_int = mi;
        cy.end_frac = mf;
        recordCycle(c, cy);
      }
    }
    c.acc = tail;
    c.chain = cut;
    c.last_period = period;
    for (int k = kOwnHist - 1; k > 0; --k) {
      c.own_int[k] = c.own_int[k - 1];
      c.own_frac[k] = c.own_frac[k - 1];
    }
    c.own_int[0] = mi;
    c.own_frac[0] = mf;
    if (c.own_n < kOwnHist) ++c.own_n;
    c.own_d2 = c.own_d1;
    c.own_d1 = depth;
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

  void recordCycle(Chan& c, const Cyc& cy) {
#ifdef SBX_DSP_CYCLE_HOOK
    // Testikoukku (vain native-testit määrittelevät): jokainen kirjattu sykli
    // kanavan, jakson ja loppumerkin kanssa. Määrittelemättömänä ei koodia.
    SBX_DSP_CYCLE_HOOK(static_cast<int>(&c - ch_), cy.period, cy.end_int, cy.end_frac);
#endif
    // Näytön keskiarvo alkaa alusta, jos kierrosluku muuttui kerralla yli
    // kRestartRatio-kertaisesti, ja hypännyt sykli jätetään siitä pois: askeleen
    // ylittävä sykli on osin vanhaa käyntiä ja pysyisi 600 rpm:llä 8 syklin
    // näytössä 1,6 s. Näyttö pysyy silloin yhden syklin vanhoissa arvoissa.
    const bool jumped = periodJumped(c, cy.period);
    if (jumped) {
      c.cyc_count = 0;
    } else {
      c.cyc_int[c.cyc_head] = cy.integral;
      c.cyc_per[c.cyc_head] = cy.period;
      c.cyc_min[c.cyc_head] = cy.cmin;
      c.cyc_p2p[c.cyc_head] = cy.p2p;
      c.cyc_head = (c.cyc_head + 1) % kMaxAvgCycles;
      if (c.cyc_count < kMaxAvgCycles) ++c.cyc_count;
    }
    c.ref_period = cy.period;
    c.per[c.per_head] = cy.period;
    c.per_head = (c.per_head + 1) % kMaxRpmHist;
    if (c.per_count < kMaxRpmHist) ++c.per_count;

    ++c.pub.cycles;
    c.pub.has_pulse = true;
    c.since_cycle = 0;
    if (!jumped) publishAverages(c);
    c.sd_rpm = rpmSpread(c);
  }

  // Poikkeaako jakso yli kRestartRatio-kertaisesti (tai alle kMinPeriodRatio)
  // näyttöikkunan keskijaksosta? Omat funktionsa, jotta recordCyclen pino
  // pysyy -O0:llakin <= 128 tavussa.
  bool periodJumped(const Chan& c, float period) const {
    const int n0 = c.cyc_count < avg_n_ ? c.cyc_count : avg_n_;
    if (n0 <= 0) return false;
    float sp = 0.0f;
    for (int k = 0; k < n0; ++k) sp += c.cyc_per[(c.cyc_head - 1 - k + kMaxAvgCycles) % kMaxAvgCycles];
    const float avgp = sp / static_cast<float>(n0);
    return period > kRestartRatio * avgp || period < kMinPeriodRatio * avgp;
  }

  // Näyttöarvot avg_cycles viimeisimmän syklin yli. Keskiarvo on
  // sum(integraali) / sum(jakso) eli aikapainotettu: sama kuin yksi
  // integraali N kokonaisen syklin yli.
  void publishAverages(Chan& c) const {
    const int n = c.cyc_count < avg_n_ ? c.cyc_count : avg_n_;
    float s_int = 0.0f, s_per = 0.0f, s_min = 0.0f, s_p2p = 0.0f;
    for (int k = 0; k < n; ++k) {
      const int i = (c.cyc_head - 1 - k + kMaxAvgCycles) % kMaxAvgCycles;
      s_int += c.cyc_int[i];
      s_per += c.cyc_per[i];
      s_min += c.cyc_min[i];
      s_p2p += c.cyc_p2p[i];
    }
    const float inv_n = 1.0f / static_cast<float>(n);
    c.pub.mean_kpa = s_int / s_per;
    c.pub.min_kpa = s_min * inv_n;
    c.pub.p2p_kpa = s_p2p * inv_n;
    // rpm = 120 / keskijakso (ei 120/T:n keskiarvo, joka on jitterillä harhainen
    // ylöspäin): kierroksia aikayksikössä.
    c.pub.rpm = 120.0f * static_cast<float>(kSampleRateHz) * static_cast<float>(n) / s_per;
  }

  // Sykli-sykliltä hajonta rpm-yksiköissä rpm_hist viimeisimmästä jaksosta.
  float rpmSpread(const Chan& c) const {
    const int m = c.per_count < rpm_hist_n_ ? c.per_count : rpm_hist_n_;
    if (m < 2) return 0.0f;
    const float fs = static_cast<float>(kSampleRateHz);
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
    return std::sqrt(ss / static_cast<float>(m - 1));
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
