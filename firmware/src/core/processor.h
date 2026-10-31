#pragma once
// Suckboxx core - kokoaja: raakakehys -> kPa -> DSP -> käytösmatriisi (WP-6,
// docs/firmware.md §4.8).
//
// Processor on se yksi olio, jota ESP32-liima (WP-7) kutsuu mutexin takaa:
//   - push(RawFrame) 1 kHz:llä: rengas (S5b), mV -> nimellinen -> kalibroitu
//     kPa (R4), DSP, vahvistusmittauksen keruu ja aaltomuodon desimointi.
//     O(1): ESP32 kutsuu tätä 1000 kertaa sekunnissa.
//   - tick(now_s) ~20 Hz: käytösmatriisi (§8), automaattinen nollaus (rivi 1)
//     ja vahvistusmittauksen päättäminen.
//   - web-käsittelijät: alla oleva reittitaulukko ja snapshot (snapshot.h).
// Kaikki päätöslogiikka on täällä, jotta se on native-testattavissa. Liimaan
// jää ajastus, laitteet ja verkko.
//
// Reitit (docs/firmware.md §5). Vastaus kirjoitetaan JsonWriterilla, joten
// tekstit escapautuvat:
//   GET  /api/config        configJson()
//   POST /api/config        applyConfigForm() -> {"ok":true} / {"ok":false,"err":err}
//   POST /api/config/reset  resetConfig()     -> {"ok":true}
//   POST /api/master        applyMasterForm() -> kuten /api/config
//   POST /api/cal/zero      zeroNow()         -> {"ok":tulos==Ok,"msg":calStatus().msg}
//   POST /api/cal/gain      startGainCapture()-> {"ok":tulos,"msg":calStatus().msg}
//   POST /api/cal/reset     resetGain()       -> {"ok":true,"msg":calStatus().msg}
//   GET  /api/raw.csv       ring() + CsvWriter(ring(), config(), calibration())
// Pysyvät muutokset kertovat takeConfigDirty() ja takeCalDirty(); liima
// tallentaa NVS:ään viiveellä (§4.9). Lähteen vaihdon (config().source) liima
// tunnistaa vertaamalla ennen ja jälkeen: näytteenottolähde vaihdetaan siellä.
//
// Kalibroinnin muutos nollaa DSP:n tilan. DSP:n historia (EMA-taso,
// syklikeskiarvot) on kalibroitua kPa:ta, ja vanhan ja uuden kalibroinnin
// sekahistoria näyttäisi hetken väärää eroa masteriin (§8: väärä
// "synkronoitu" on pahempi kuin ei tulosta). Nollaus tekee myös
// automaattisen nollauksen käänteismuunnoksen (taso -> nimellinen) tarkaksi.
// Nollaus ja vahvistus onnistuvat vain ilman pulsseja (moottori seis tai
// T-haara), jolloin DSP:n nollauksesta ei näy mitään. resetGain() kesken
// käynnin näyttää "odota" lukituksen ja stable_hold_s:n ajan (~3,5 s).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "calibration.h"
#include "config.h"
#include "dsp.h"
#include "form.h"
#include "raw_ring.h"
#include "status.h"
#include "types.h"

namespace sbx {

struct CalStatus {
  bool busy = false;     // vahvistusmittaus käynnissä
  // Viimeisin kalibrointiviesti suomeksi, UI näyttää sellaisenaan. Aina
  // staattinen teksti (calResultText tai Processorin vakio), joten osoitin
  // pysyy voimassa myös mutexin vapauttamisen jälkeen. Ei char[96]:
  // calResultText(NotAtmosphere) on 149 tavua.
  const char* msg = "";
};

class Processor {
 public:
  // §5: wave.s = 16 näytteen lohkokeskiarvot (1 kHz / 16 = 62,5 Hz), enintään
  // 16 riviä per viesti. Jono on yhtä pitkä: hitaampi haku (ei asiakkaita)
  // pudottaa vanhimmat, kuten mock_server.py:n deque(maxlen=16).
  static constexpr int kWaveDecim = 16;
  static constexpr int kWaveRows = 16;
  static constexpr float kWaveFsHz =
      static_cast<float>(kSampleRateHz) / static_cast<float>(kWaveDecim);
  // §4.8: vahvistusmittaus 2 s nimellisillä näytteillä (2000 kpl).
  static constexpr uint32_t kGainCaptureSamples = 2u * kSampleRateHz;

  // Tekstit samat kuin tools/mock_server.py:ssä (UI testautuu oikeilla teksteillä).
  static constexpr const char* kMsgAutoZero = "Automaattinen nollaus tehty (moottori sammuksissa)";
  static constexpr const char* kMsgGainStart = "Mitataan vahvistusta (2 s) – pidä alipaine tasaisena";
  static constexpr const char* kMsgGainBusy = "Vahvistuskalibrointi on jo käynnissä";
  static constexpr const char* kMsgGainReset = "Vahvistuskalibrointi poistettu";
  // configure() keskeyttää mittauksen (§4.8). Mockissa ei vastinetta.
  static constexpr const char* kMsgGainAborted =
      "Vahvistusmittaus keskeytyi, koska asetukset muuttuivat. Aloita mittaus uudelleen";
  static constexpr const char* kErrNoPairs = "Ei muutettavia asetuksia";
  static constexpr const char* kErrMaster = "Virheellinen kanava (ch = 0, 1 tai 2)";

  // Rengasmuisti kutsujalta (ESP32: staattinen 8192 näytettä = 64 kt, §4.5).
  Processor(RawSample* ring_storage, size_t ring_capacity) : ring_(ring_storage, ring_capacity) {}
  // RawRing osoittaa kutsujan muistiin: kopio jakaisi saman renkaan.
  Processor(const Processor&) = delete;
  Processor& operator=(const Processor&) = delete;

  // Dsp + StatusMachine + muunnos. Nollaa DSP:n ja ajastimet ja keskeyttää
  // käynnissä olevan vahvistusmittauksen. Hylkää konfiguraation, jota
  // configValidate ei hyväksy (false), jolloin edellinen jää voimaan:
  // NVS:stä luettu rikkinäinen konfiguraatio ei pääse käyttöön, ja
  // käynnistyksessä voimaan jäävät oletukset.
  // D11: nollaus kuuluu siihen muunnokseen ja lähteeseen, jolla se mitattiin.
  // Jos anturin siirtofunktio tai lähde vaihtuu, nollaus mitätöityy (vahvistus
  // säilyy: se on kanavien keskinäinen suhde, ja muunnoksen muutos on kaikille
  // kanaville yhteinen).
  bool configure(const Config& c) {
    if (!configValidate(c, nullptr, 0)) return false;
    applyConfig(c);
    return true;
  }
  const Config& config() const { return cfg_; }

  // Käynnistyksessä NVS:stä. zero_valid = false (D11: nollaus ei ole pysyvä).
  // Gain, joka ei ole computeGainin tuottamalla välillä kGainMin…kGainMax
  // (rikki tai eri firmwaresta), tai gain ilman gain_valid-lippua korvataan
  // ykkösillä ja vahvistus merkitään puuttuvaksi (rivi 11); korjaus nostaa
  // takeCalDirtyn, jotta NVS siivoutuu. Offsetit sellaisenaan (NVS ei tallenna
  // niitä, §4.9), mutta ei-äärellinen offset -> 0.
  void setCalibration(const Calibration& cal) {
    Calibration c = cal;
    c.zero_valid = false;
    if (sanitize(c)) cal_dirty_ = true;
    commitCal(c);
  }
  const Calibration& calibration() const { return cal_; }

  // 1 kHz. Kaikki O(kChannels): DSP:n syklirajan työ on dsp.h:n budjetissa.
  void push(const RawFrame& f) {
    ring_.push(f);  // ei-op jäädytettynä (CSV-lataus); käsittely jatkuu silti
    float nominal[kChannels];
    float kpa[kChannels];
    for (int c = 0; c < kChannels; ++c) {
      nominal[c] = nominalKpa(f.mv[c], f.rail_mv, cfg_.sensor);
      kpa[c] = applyCal(nominal[c], c, cal_);
    }
    dsp_.push(kpa, f);
    if (samples_ < 0xFFFFFFFFu) ++samples_;
    // Keruu pysähtyy täyteen; tulos lasketaan tickissä, jotta push pysyy O(1):nä.
    if (cal_status_.busy && capture_.count() < kGainCaptureSamples) capture_.add(nominal);
    for (int c = 0; c < kChannels; ++c) decim_acc_[c] += kpa[c];
    if (++decim_n_ >= kWaveDecim) {
      constexpr float kInv = 1.0f / static_cast<float>(kWaveDecim);
      for (int c = 0; c < kChannels; ++c) {
        wave_[wave_head_][c] = decim_acc_[c] * kInv;
        decim_acc_[c] = 0.0f;
      }
      decim_n_ = 0;
      wave_head_ = (wave_head_ + 1) % kWaveRows;
      if (wave_count_ < kWaveRows) ++wave_count_;
    }
  }

  // ~20 Hz (StatusMachine toimii 10–50 Hz:llä). now_s monotoninen sekunteina.
  void tick(float now_s) {
    for (int i = 0; i < kChannels; ++i) in_.ch[i] = dsp_.channel(i);
    in_.rpm = dsp_.rpm();
    in_.zero_valid = cal_.zero_valid;
    in_.gain_valid = cal_.gain_valid;
    in_.now_s = now_s;
    status_ = sm_.update(in_);
    // Rivi 1: StatusMachine pyytää kerran per NoSignal-jakso, kun NoSignal on
    // kestänyt off_hold_s ja tasot ovat nollan tuntumassa ja paikallaan
    // (sama vakausportti kuin zeroNow:ssa). Ajetaan myös
    // vahvistusmittauksen aikana: pyyntö edellyttää ilmanpainetta, jolloin
    // mittaus päättyisi kuitenkin TooSmalliin, eikä reunaliipattu pyyntö
    // toistu ennen seuraavaa käyntiä.
    if (status_.request_auto_zero) {
      const CalResult r = zeroFromLevels();
      cal_status_.msg = r == CalResult::Ok ? kMsgAutoZero : calResultText(r);
    }
    if (cal_status_.busy && capture_.count() >= kGainCaptureSamples) finishGain();
  }

  // POST /api/cal/zero: vaatii ilmanpaineen (|nimellinen taso| <= off_kpa
  // jokaisessa kanavassa, D10) ja tason vakauden (WP-9: vaihteluväli
  // viimeisen 1 s:n aikana < zero_settle_kpa, muuten NotSettled). Sama portti
  // kuin automaattisessa nollauksessa, joten juuri sammutetun moottorin vielä
  // vuotava alipaine ei kirjaudu offsetiksi. Viesti calStatus().msg:iin.
  // Sallittu myös vahvistusmittauksen aikana: oikeassa mittauksessa T-haarassa
  // on alipainetta (-> NotAtmosphere, ei muutosta), ja jos alipaine on
  // päästetty, nollaus on aito ja mittaus päättyy TooSmalliin tai NotStableen.
  CalResult zeroNow() {
    const CalResult r = zeroFromLevels();
    cal_status_.msg = calResultText(r);
    return r;
  }

  // POST /api/cal/gain: 2 s keruu nimellisistä näytteistä, tulos tickissä
  // (computeGain: vaatii nollauksen, D11). false = mittaus on jo käynnissä.
  bool startGainCapture() {
    if (cal_status_.busy) {
      cal_status_.msg = kMsgGainBusy;
      return false;
    }
    capture_.reset();
    cal_status_.busy = true;
    cal_status_.msg = kMsgGainStart;
    return true;
  }

  // POST /api/cal/reset: gain = 1, gain_valid = false (rivi 11). Nollapiste
  // säilyy: z = -offset / gain pysyy, joten uusi offset = offset / gain.
  void resetGain() {
    Calibration c = cal_;
    for (int i = 0; i < kChannels; ++i) {
      c.offset_kpa[i] = c.offset_kpa[i] / c.gain[i];  // gain > 0 (sanitize, computeGain)
      c.gain[i] = 1.0f;
    }
    c.gain_valid = false;
    if (differs(c, cal_)) {
      commitCal(c);
      cal_dirty_ = true;
    }
    cal_status_.msg = kMsgGainReset;
  }

  // POST /api/master (D7). Ei nollaa DSP:tä: pelkkä viite vaihtuu, ja syklien
  // keskiarvot ovat kanavakohtaisia. StatusMachine nollaa ajastimensa: uusi
  // nollataso = uusi vakausjakso. Sama kanava uudelleen = ei muutosta.
  bool setMaster(int ch) {
    if (ch < 0 || ch >= kChannels) return false;
    if (ch == static_cast<int>(cfg_.status.master)) return true;
    cfg_.status.master = static_cast<uint8_t>(ch);
    sm_.configure(cfg_.status);
    config_dirty_ = true;
    return true;
  }

  // POST /api/master lomakkeelta: ch = "0", "1" tai "2" tarkalleen (kuten
  // mock_server.py api_master). Viimeinen ch-kenttä ratkaisee. Liiman ei
  // tarvitse jäsentää: String::toInt("x") = 0 valitsisi SYL 1:n hiljaa.
  bool applyMasterForm(const FormPair* pairs, int count, char* err, size_t err_len) {
    form_detail::formatText(err, err_len, "%s", "");
    const char* v = nullptr;
    for (int k = 0; pairs != nullptr && k < count; ++k) {
      if (pairs[k].key != nullptr && std::strcmp(pairs[k].key, "ch") == 0) v = pairs[k].value;
    }
    const bool ok = v != nullptr && v[0] >= '0' && v[0] < '0' + kChannels && v[1] == '\0';
    if (!ok) {
      form_detail::formatText(err, err_len, "%s", kErrMaster);
      return false;
    }
    return setMaster(v[0] - '0');
  }

  // POST /api/config (§4.4): atominen. Jokainen pari configSetTextillä
  // kopioon, sitten configValidate, ja vasta sitten kopio käyttöön (configure)
  // ja takeConfigDirty. Ensimmäinen virhe err:iin samassa muodossa kuin
  // tools/mock_server.py:n api_config_set:
  //   "Tuntematon asetus: avain" | "Nimi (avain): syy" | configValidaten teksti.
  bool applyConfigForm(const FormPair* pairs, int count, char* err, size_t err_len) {
    form_detail::formatText(err, err_len, "%s", "");
    if (pairs == nullptr || count <= 0) {
      form_detail::formatText(err, err_len, "%s", kErrNoPairs);
      return false;
    }
    Config c = cfg_;
    for (int k = 0; k < count; ++k) {
      const char* key = pairs[k].key != nullptr ? pairs[k].key : "";
      const SetResult r = configSetText(c, key, pairs[k].value);
      if (r == SetResult::UnknownKey) {
        form_detail::formatText(err, err_len, "%s: %s", setResultText(r), key);
        return false;
      }
      if (r != SetResult::Ok) {
        const FieldInfo& f = configField(configFindField(key));
        form_detail::formatText(err, err_len, "%s (%s): %s", f.label, f.key, setResultText(r));
        return false;
      }
    }
    // Oma puskuri, jotta pieni err-puskuri katkaisee UTF-8-turvallisesti.
    char why[kFormErrLen];
    if (!configValidate(c, why, sizeof(why))) {
      form_detail::formatText(err, err_len, "%s", why);
      return false;
    }
    applyConfig(c);
    config_dirty_ = true;
    return true;
  }

  // POST /api/config/reset: oletukset (myös master = SYL 1 ja source = sim).
  void resetConfig() {
    applyConfig(Config());
    config_dirty_ = true;
  }

  // GET /api/config (§4.4). Puskuri kConfigJsonSize; 0 = ei mahtunut.
  size_t configJson(char* buf, size_t n) const { return configToJson(cfg_, buf, n); }

  // Pysyvä muutos -> liima tallentaa NVS:ään. Lukeminen kuittaa.
  bool takeConfigDirty() {
    const bool d = config_dirty_;
    config_dirty_ = false;
    return d;
  }
  bool takeCalDirty() {  // gain tai gain_valid muuttui (D11: nollaus ei ole pysyvä)
    const bool d = cal_dirty_;
    cal_dirty_ = false;
    return d;
  }

  // Viimeisimmän tickin tila. channel() ja rpm() ovat saman tickin syötteet,
  // joten snapshotin luvut ja tila vastaavat toisiaan (d = mean_master - mean_ch).
  const StatusResult& status() const { return status_; }
  const ChannelState& channel(int i) const { return in_.ch[(i >= 0 && i < kChannels) ? i : 0]; }
  RpmEstimate rpm() const { return in_.rpm; }
  int master() const { return sm_.master(); }
  const CalStatus& calStatus() const { return cal_status_; }
  RawRing& ring() { return ring_; }
  const RawRing& ring() const { return ring_; }

  // 16 näytteen lohkokeskiarvot (62,5 Hz, kalibroitua kPa:ta) edellisen haun
  // jälkeen, vanhin ensin. Palauttaa rivien määrän; hakematta jääneet jäävät
  // jonoon seuraavalle haulle.
  int takeWave(float (*rows)[kChannels], int max_rows) {
    if (rows == nullptr || max_rows <= 0) return 0;
    const int n = wave_count_ < max_rows ? wave_count_ : max_rows;
    int idx = (wave_head_ - wave_count_ + kWaveRows) % kWaveRows;
    for (int k = 0; k < n; ++k) {
      for (int c = 0; c < kChannels; ++c) rows[k][c] = wave_[idx][c];
      idx = (idx + 1) % kWaveRows;
    }
    wave_count_ -= n;
    return n;
  }

 private:
  void applyConfig(const Config& c) {
    const SensorParams& a = cfg_.sensor;
    const SensorParams& b = c.sensor;
    if (!(a.v_zero_mv == b.v_zero_mv && a.mv_per_kpa == b.mv_per_kpa &&
          a.rail_comp == b.rail_comp && a.rail_nominal_mv == b.rail_nominal_mv &&
          cfg_.source == c.source)) {
      cal_.zero_valid = false;  // D11
    }
    cfg_ = c;
    dsp_.configure(c.dsp, c.sensor);
    sm_.configure(c.status);
    samples_ = 0;
    if (cal_status_.busy) {
      cal_status_.busy = false;
      cal_status_.msg = kMsgGainAborted;
    }
  }

  static bool sanitize(Calibration& c) {
    bool changed = false;
    bool gains_ok = c.gain_valid;
    for (int i = 0; i < kChannels; ++i) {
      if (!(c.gain[i] >= kGainMin && c.gain[i] <= kGainMax)) gains_ok = false;  // myös NaN
    }
    if (!gains_ok) {
      for (int i = 0; i < kChannels; ++i) {
        if (!(c.gain[i] == 1.0f)) changed = true;
        c.gain[i] = 1.0f;
      }
      if (c.gain_valid) changed = true;
      c.gain_valid = false;
    }
    for (int i = 0; i < kChannels; ++i) {
      if (!std::isfinite(c.offset_kpa[i])) {
        c.offset_kpa[i] = 0.0f;
        changed = true;
      }
    }
    return changed;
  }

  static bool differs(const Calibration& a, const Calibration& b) {
    for (int i = 0; i < kChannels; ++i) {
      if (!(a.gain[i] == b.gain[i] && a.offset_kpa[i] == b.offset_kpa[i])) return true;
    }
    return a.zero_valid != b.zero_valid || a.gain_valid != b.gain_valid;
  }

  void commitCal(const Calibration& c) {
    cal_ = c;
    dsp_.reset();
    sm_.resetSettle();  // taso alkaa alusta uudella kalibroinnilla
    samples_ = 0;
  }

  // Nimelliset tasot kääntämällä kalibrointi DSP:n tasosta (EMA on lineaarinen,
  // ja DSP nollataan aina kalibroinnin muuttuessa, joten taso on yhdellä
  // kalibroinnilla mitattu): nimellinen = (taso - offset) / gain.
  // Vakausportti: StatusMachinen ikkuna viimeisimmästä tickistä, kalibroidulla
  // asteikolla kuten automaattisen nollauksen pyynnössä, jotta pyyntö ja sen
  // toteutus eivät voi olla eri mieltä rajalla (gain 0,85-1,15 muuttaisi rajaa).
  CalResult zeroFromLevels() {
    if (samples_ == 0) return CalResult::NotAtmosphere;  // ei mittausta nollauksen jälkeen
    float nominal[kChannels];
    float range[kChannels];
    for (int i = 0; i < kChannels; ++i) {
      nominal[i] = (dsp_.channel(i).level_kpa - cal_.offset_kpa[i]) / cal_.gain[i];
      range[i] = sm_.settle().range(i);
    }
    Calibration c = cal_;
    const CalResult r =
        computeZero(nominal, range, cfg_.status.off_kpa, cfg_.status.zero_settle_kpa, c);
    if (r == CalResult::Ok) commitCal(c);
    return r;
  }

  void finishGain() {
    float mean[kChannels];
    float sd[kChannels];
    capture_.result(mean, sd);
    Calibration c = cal_;
    const CalResult r = computeGain(mean, sd, c);
    cal_status_.busy = false;
    cal_status_.msg = calResultText(r);
    if (r == CalResult::Ok) {
      commitCal(c);
      cal_dirty_ = true;
    }
  }

  Config cfg_;
  Calibration cal_;
  Dsp dsp_;
  StatusMachine sm_;
  StatusInput in_;
  StatusResult status_;
  RawRing ring_;
  CalCapture capture_;
  CalStatus cal_status_;
  bool config_dirty_ = false;
  bool cal_dirty_ = false;
  uint32_t samples_ = 0;  // näytteitä viimeisimmän DSP-nollauksen jälkeen

  float decim_acc_[kChannels] = {0.0f, 0.0f, 0.0f};
  int decim_n_ = 0;
  float wave_[kWaveRows][kChannels] = {};
  int wave_head_ = 0;   // seuraava kirjoituspaikka
  int wave_count_ = 0;
};

}  // namespace sbx
