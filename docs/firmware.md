# Firmware-arkkitehtuuri ja rajapintasopimus

> Päätökset ja perustelut: [SUUNNITTELU.md](../SUUNNITTELU.md) (päätösloki §5, käytösmatriisi §8,
> roadmap §11). Tämä dokumentti kertoo **miten** ne toteutetaan ja mikä moduuli omistaa mitäkin.
> Yhteiset tyypit ja parametrit: [firmware/src/core/types.h](../firmware/src/core/types.h) —
> se on sopimus, ja muutos siihen on arkkitehtuurimuutos.

---

## 1. Periaatteet

1. **Kaksi kerrosta.** `src/core/` on puhdasta C++17:aa ilman Arduino-/ESP-riippuvuuksia, ja
   kaikki logiikka (DSP, kalibrointi, käytösmatriisi, konfiguraatio, CSV, JSON, simulaattori)
   on siellä. `src/hal/` + `src/main.cpp` on ohut ESP32-liima: näytteenotto, NVS, Wi-Fi, web.
   Kaikki mikä voi olla coressa, on coressa — koska vain core on testattavissa PC:llä.
2. **Simulaattori ensin.** `EngineSim` tuottaa samaa `RawFrame`-dataa (mV ADC-pinnissä) kuin
   oikea rauta, joten koko ketju muunnoksesta vikatunnistukseen ajetaan samaa polkua. Laite
   käynnistyy oletuksena simulaattorilähteellä, kunnes XGZP-anturit on kytketty.
3. **Header-only core.** Jokainen moduuli on yksi `.h`, jolloin native-testi kääntää vain sen,
   minkä se sisällyttää — keskeneräinen naapurimoduuli ei kaada toisen testejä.
4. **Ei dynaamista muistinvarausta näytepolulla.** Kiinteät taulukot, kapasiteetit
   käännösaikaisia. Ei poikkeuksia, ei RTTI:tä, ei `iostream`ia coressa.
5. **float, ei double.** ESP32:n FPU on yksitarkkuuksinen. Testit ajetaan `-Wdouble-promotion
   -Werror`:lla. Käytä `std::fabs`, `std::sqrt` jne. float-ylikuormilla, literaalit `1.0f`.
6. **GCC 8.4 -yhteensopivuus** (Arduino-ESP32 2.0.17): C++17 kyllä, C++20 ei
   (ei `std::span`, ei designated initializereita, ei `<charconv>`-liukulukuja). Lukujen
   muotoilu `snprintf`illä.
7. **Tyyli** kuten olemassa olevassa koodissa: kommentit suomeksi, tunnisteet englanniksi,
   2 välilyönnin sisennys, `namespace sbx`. Kommentit kertovat *miksi*, viittaukset
   päätöksiin muodossa `D1`, `R4`, `rivi 7` (käytösmatriisi).

---

## 2. Datavirta

```
 1 kHz (sampler-taski, core 1)                                  ~20 Hz (tick)            15 Hz
 ┌────────────┐  RawFrame   ┌──────────────┐  kPa[3]  ┌─────┐  ChannelState[3]  ┌───────────────┐
 │ lähde:     │────────────►│ calibration  │─────────►│ dsp │──────────────────►│ status        │
 │ Sim / ADC /│  mV ×3      │ mV→kPa, R4   │          │     │  RpmEstimate      │ käytösmatriisi│
 │ HX710B     │──┐          └──────────────┘          └─────┘                   └──────┬────────┘
 └────────────┘  │                                                                     │ StatusResult
                 │  ┌──────────┐   GET /api/raw.csv                              ┌──────▼────────┐
                 └─►│ raw_ring │──────────────────► CsvWriter                    │ snapshot JSON │──► SSE /events
                    │ S5b ~8 s │                                                 └───────────────┘
                    └──────────┘
```

`Processor` (vaihe 2) kokoaa tämän: `push(RawFrame)` 1 kHz:llä, `tick(now)` ~20 Hz:llä,
`snapshot()` JSONiksi. ESP32-liima kutsuu Processoria mutexin takaa.

---

## 3. Moduulit ja omistajuus

| tiedosto | omistaja (työpaketti) | sisältö |
|---|---|---|
| `src/core/types.h` | orkestroija | sopimus — älä muuta työpaketissa |
| `src/core/engine_sim.h` | WP-1 | moottorisimulaattori |
| `src/core/dsp.h` | WP-2 | suodatus, syklintunnistus, D1-luvut, RPM |
| `src/core/json_writer.h` | WP-3 | pieni JSON-kirjoitin kiinteään puskuriin |
| `src/core/config.h` | WP-3 | Config + kenttärekisteri (S5a) |
| `src/core/calibration.h` | WP-3 | mV→kPa, nolla- ja vahvistuskalibrointi (R4) |
| `src/core/raw_ring.h` | WP-3 | raakadatan rengaspuskuri + CSV (S5b) |
| `src/core/status.h` | WP-4 | käytösmatriisi (§8) → StatusResult + suomenkieliset tekstit |
| `web/index.html` | WP-5 | koko käyttöliittymä yhtenä tiedostona |
| `src/web/web_page.h` | WP-5 | generoitu `tools/embed_web.py`:llä — älä muokkaa käsin |
| `tools/mock_server.py` | WP-5 | PC-palvelin UI:n kehitykseen (sama API kuin laitteella) |
| `src/core/processor.h`, `snapshot.h`, `sim_api.h` | WP-6 (vaihe 2) | kokoaja + snapshot-JSON + `/api/sim`-muunnos + integraatiotestit (§4.8) |
| `src/hal/*`, `src/main.cpp` | WP-7 (vaihe 2) | ESP32-liima (§4.9) |

Testit: `firmware/test/test_<moduuli>/test_main.cpp`, yksi kansio per moduuli. Yhteiset
testiapurit `firmware/test/support/` (include-polussa).

---

## 4. Rajapinnat

Alla on **julkinen** rajapinta, johon muut moduulit nojaavat. Sisäinen toteutus on
työpaketin päätös. Jos rajapintaan tarvitaan muutos, se raportoidaan orkestroijalle eikä
tehdä omin päin.

### 4.1 `engine_sim.h` (WP-1)

```cpp
namespace sbx {
enum class SimFault : uint8_t { None = 0, HoseOff = 1, SensorLow = 2, SensorHigh = 3 };

struct SimParams {
  bool engine_on = true;
  float rpm = 850.0f;
  float rpm_jitter = 0.01f;        // sykli-sykliltä jaksonajan suhteellinen keskihajonta
  float rpm_drift = 0.0f;          // rpm/s (rivi 5)
  float base_kpa = -55.0f;         // keskimääräinen alipaine (gauge)
  float sync_err_kpa[kChannels] = {0, 0, 0};  // kanavan keskiarvon poikkeama = synkronointivirhe
  float depth_kpa = 20.0f;         // imupulssin syvyys
  float intake_frac = 0.25f;       // imutahdin osuus syklistä (180°/720°)
  float res_hz[kChannels] = {112, 112, 112};   // letkuresonanssi (docs/rauta.md §4.1)
  float res_kpa = 3.0f;            // resonanssin herätesamplitudi
  float res_decay_s = 0.02f;
  float xtalk = 0.0f;              // muiden sylinterien pulssien vuoto 0..1 (tasauskanavat, D6)
  float peak_kpa = 0.0f;           // positiivinen paluuaalto (rivi 10 / H1)
  float noise_mv = 2.0f;           // ADC-kohinan keskihajonta
  float sensor_offset_kpa[kChannels] = {0, 0, 0};  // anturivirhe (R4)
  float sensor_gain_err[kChannels] = {0, 0, 0};    // suhteellinen, 0.02 = +2 %
  SimFault fault[kChannels] = {SimFault::None, SimFault::None, SimFault::None};
  uint32_t seed = 1;
};

class EngineSim {
 public:
  EngineSim();
  // Ei nollaa vaihetta. rpm_set = true, kun POST /api/sim sisältää rpm-avaimen: pelkkä
  // driftin muutos jatkaa nykyisestä kierrosluvusta ja kirjaa sen params().rpm:ään, joten
  // /api/sim-käsittelijä yhdistää POSTin sim.params():iin (ks. engine_sim.h).
  void configure(const SimParams& p, const SensorParams& s, bool rpm_set = false);
  const SimParams& params() const;
  void reset();                       // vaihe, aika ja PRNG alkuun
  RawFrame next();                    // seuraava 1 kHz näyte, seq kasvaa yhdellä
  float trueMeanKpa(int ch) const;    // totuus: base + sync_err; 0 jos moottori seis tai HoseOff
  float trueRpm() const;              // hetkellinen nimellinen rpm (drift huomioiden)
};
}
```

`/api/sim`-käsittelijä yhdistää POSTin `sim.params()`:iin ja välittää
`rpm_set = (POST sisältää rpm-avaimen)`, jotta lomakkeen uudelleenlähetys palauttaa
drift-kokeen jälkeen pyydetyn rpm:n (sama logiikka `tools/mock_server.py`:ssä).

Vaatimukset: deterministinen (sama seed → samat näytteet), sylinterit 240° kampikulman
(= ⅓ syklin) välein, pulssimuoto sellainen että sen keskiarvo syklin yli on **täsmälleen**
`base + sync_err` (ennen anturivirheitä ja leikkautumista), ulostulo leikataan anturin
alueelle (GN: 200…2700 mV) ja ADC:n alueelle (0…3300 mV). `SensorLow` = ~0 mV,
`SensorHigh` = ~3300 mV, `HoseOff` = ilmanpaine (0 kPa) + kohina.

### 4.2 `dsp.h` (WP-2)

```cpp
namespace sbx {
class Dsp {
 public:
  void configure(const DspParams& dp, const SensorParams& sp);  // nollaa tilan
  void reset();
  // 1 kHz. kpa = kalibroitu paine per kanava, raw = alkuperäinen kehys (mV vikamonitoreille).
  void push(const float (&kpa)[kChannels], const RawFrame& raw);
  const ChannelState& channel(int i) const;
  RpmEstimate rpm() const;
  float lpGain(float f_hz) const;     // alipäästön |H(f)| (bilineaarinen Butterworth)
};
}
```

- Syklintunnistus alipäästösuodatetusta signaalista (`lp_cutoff_hz`, resonanssi pois),
  hystereesi `max(hyst_min_kpa, hyst_frac × A)` suhteellisena laukaisuna edellisestä
  huipusta (A = pulssin amplitudi; lukittuna min(nousu, lasku), jotta painehyppy ei paisuta
  sitä). Amplitudiportti `3 × hyst_min_kpa × |H(f_sykli)|` + jaksonpituuden johdonmukaisuus.
  Sykli kirjataan vasta, kun sen päättänyt pulssi on nähty pohjaan asti (~10–35 ms
  syklirajan jälkeen). Yksi sykli = yksi imupulssi = 2 kierrosta,
  **rpm = 120 / T**.
- `mean_kpa` (D1) lasketaan **kalibroiduista suodattamattomista** näytteistä täsmälleen yhden
  syklin yli (resonanssi keskiarvoistuu pois kokonaisessa syklissä), sitten keskiarvo
  `avg_cycles` viimeisimmän syklin yli. `min_kpa`/`p2p_kpa` suodatetusta signaalista;
  `p2p_kpa` on pulssin nousu (lukittuna min(nousu, lasku)), tasaisessa käynnissä sama kuin
  ikkunan p2p.
- `xtalk` 0,3:een asti ei saa kolminkertaistaa rpm:ää (vuotopulssit ovat pienempiä).
- Kaasun muutokset (WP-2c, 30.10.2026): lukitus puretaan kahden peräkkäisen vanhenemisen
  jälkeen (`kMaxStaleRolls`, rpm-pudotus yli 1,8-kertaisesti), ja viitteestä poikkeava jakso,
  joka toistaa edellisen raja-raja-välin, on uusi viite (kaasun lisäys). Odotetun rajan
  puuttuessa Schmittin amplitudi lasketaan kuluvasta segmentistä (kutistuva pulssi), ja
  vahvistuksen askeltarkistus ohitetaan, kun jakso jatkaa kiihdytyksen tai hidastuksen
  trendiä. Hiljaisuudesta herääminen (käynnistys) aloittaa suotimen asettumisen alusta.
  Mitattu EngineSimillä: rpm-pudotus ja kaasun katkaisu ≤ 0,55 s ilman syklejä, blippaus
  850 → 3000 rpm ≤ 0,65 s, käynnistys 6000 rpm:iin 147 ms (`test_integration`: `test_wp2c_*`).
- `sensor_fault`: mV ulkona `[fault_lo_mv, fault_hi_mv]` yhtäjaksoisesti `fault_hold_s`.
  `saturated`: `mv ≥ sat_mv` havaittu viimeisen 1 s aikana.
- `RpmEstimate`: kanavien rpm:ien mediaani; `sd_rpm`/`cv` viimeisten `rpm_hist` jakson yli.
- Laskentabudjetti: `push()` O(1) per näyte (sykliraja saa olla O(avg_cycles)).

### 4.3 `calibration.h` (WP-3)

```cpp
namespace sbx {
// mV -> nimellinen kPa (siirtofunktio + valinnainen kiskokorjaus R6)
float nominalKpa(uint16_t mv, uint16_t rail_mv, const SensorParams& sp);
// nimellinen -> kalibroitu: gain * nominal + offset
float applyCal(float nominal_kpa, int ch, const Calibration& cal);

enum class CalResult : uint8_t { Ok, NotAtmosphere, NotStable, TooSmall, Mismatch, GainOutOfRange,
                                 NoZero };

// Nollapiste: kanavat ilmanpaineessa -> offset niin että kalibroitu = 0. Säilyttää gainin.
CalResult computeZero(const float (&nominal_level)[kChannels], float max_abs_kpa, Calibration& cal);
// Vahvistus: kaikki kanavat samassa alipaineessa (T-haara) -> gain niin että kanavat täsmäävät
// toisiinsa (referenssi = kanavien keskiarvo). Säilyttää nollapisteen. Vaatii voimassa olevan
// nollauksen (muuten NoZero), koska anturien omat offsetit menisivät muuten gainiin.
CalResult computeGain(const float (&nominal_mean)[kChannels], const float (&nominal_sd)[kChannels],
                      Calibration& cal);
const char* calResultText(CalResult r);   // suomeksi, UI:hin

// Keskiarvo + keskihajonta -kerääjä vahvistuskalibrointia varten (Welford, float).
class CalCapture { public: void reset(); void add(const float (&nominal)[kChannels]);
  uint32_t count() const; void result(float (&mean)[kChannels], float (&sd)[kChannels]) const; };
}
```

Hyväksymisrajat (vakioina, perustelut kommentteihin): vahvistus vaatii |referenssi| ≥ 15 kPa,
jokaisen kanavan sd < 0,5 kPa, kanavat 15 %:n sisällä toisistaan (pareittain:
max − min ≤ 0,15 · min |c|) ja tuloksena gain 0,85…1,15. Tarkistusjärjestys: NoZero, TooSmall,
NotStable, Mismatch, GainOutOfRange.

### 4.4 `config.h` (WP-3)

```cpp
namespace sbx {
struct Config {
  SensorParams sensor;
  DspParams dsp;
  StatusParams status;
  SourceKind source = SourceKind::Sim;
  uint8_t push_rate_hz = 15;
};
constexpr uint16_t kConfigSchemaVersion = 1;

enum class FieldType : uint8_t { Float, U8, U16, Bool };
struct FieldInfo {
  const char* key;    // ≤ 15 merkkiä (NVS-avaimen raja), [a-z0-9_]
  const char* label;  // suomeksi, UI:hin
  const char* unit;   // "kPa", "Hz", "s", "" ...
  const char* group;  // "Synkronointi", "Signaali", "Anturi", "Järjestelmä"
  FieldType type;
  float min, max;
};
int configFieldCount();
const FieldInfo& configField(int i);
int configFindField(const char* key);                 // -1 jos ei löydy
float configGet(const Config& c, int i);
enum class SetResult : uint8_t { Ok, UnknownKey, OutOfRange, Invalid };
SetResult configSet(Config& c, int i, float v);       // alue + tyyppi; ristiintarkistus erikseen
SetResult configSetText(Config& c, const char* key, const char* text);  // web-lomakkeelta
bool configValidate(const Config& c, char* err, size_t n);  // esim. tol_green < tol_yellow
size_t configToJson(const Config& c, char* buf, size_t n);  // 0 = ei mahtunut
}
```

Kaikki `SensorParams`/`DspParams`/`StatusParams`-kentät, `source` ja `push_rate_hz` ovat
rekisterissä. NVS-liima tallentaa kentät avain kerrallaan rekisterin kautta, joten uusi kenttä
ei vaadi muutoksia liimaan.

`configToJson` on huoltosivun tietolähde: UI rakentaa lomakkeen kenttärekisteristä.
Muoto (WP-3:n toteutus, kirjattu 30.10.2026):

```json
{"v": 1, "fields": [
  {"key": "tol_green", "label": "…", "unit": "kPa", "group": "Synkronointi",
   "type": "float", "min": 0.1, "max": 10, "def": 1.5, "value": 1.5}
]}
```

- `v` = `kConfigSchemaVersion`. `type` ∈ `float`, `u8`, `u16`, `bool`; bool-kentän `def` ja
  `value` ovat `true`/`false`, `min`/`max` silloin 0/1. Kentät rekisterin järjestyksessä, ja
  UI ryhmittelee `group`in mukaan. Puskuri: `kConfigJsonSize`.
- UI tuntee nimeltä vain kaksi valintakenttää, `source` ja `master`, joiden vaihtoehtojen
  nimet ovat UI:ssa.
- `POST /api/config` on atominen: kaikki kentät sovelletaan Configin kopioon, sitten
  `configValidate`, ja vasta sitten kopio otetaan käyttöön. Virhe on yksi teksti, jonka UI
  näyttää sellaisenaan: `{"ok":false,"err":"Nimi (avain): syy"}`. Viitetoteutus on
  `tools/mock_server.py`:n `api_config_set`, ja laitteen pitää vastata samoin (§4.8).

### 4.5 `raw_ring.h` (WP-3)

```cpp
namespace sbx {
struct RawSample { uint16_t seq_lo; uint16_t mv[kChannels]; };   // 8 tavua
class RawRing {
 public:
  RawRing(RawSample* storage, size_t capacity);   // muisti kutsujalta (ESP32: staattinen)
  void push(const RawFrame& f);                   // ei-op jos jäädytetty
  void freeze(); void unfreeze(); bool frozen() const;   // viitelaskettu
  uint32_t version() const;                       // kasvaa jokaisesta tallennetusta pushista ja clear()ista
  size_t size() const; size_t capacity() const;
  uint32_t newestSeq() const;
  uint32_t gaps() const;                          // havaitut seq-aukot (pudonneet näytteet)
  bool at(size_t i, RawSample& out) const;        // 0 = vanhin
};
// CSV paloina (AsyncWebServerin chunked-vastaus). Rengas jäädytettynä koko ajan.
class CsvWriter {
 public:
  CsvWriter(const RawRing& ring, const Config& cfg, const Calibration& cal);
  size_t fill(char* buf, size_t max_len);         // 0 = valmis
  bool aborted() const;                           // rengas muuttui kesken: viimeinen rivi
                                                  // '# aborted=ring_changed rows=K/N'
};
}
```

CSV: `#`-alkuiset metatietorivit (fs, anturiparametrit, kalibrointikertoimet, seq-aukot),
sitten `seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa`. Desimaalipiste, ei pilkku.
Kapasiteetti ESP32:lla 8192 näytettä ≈ 8,2 s (64 kt).

Jokaista `freeze()`-kutsua vastaa täsmälleen yksi `unfreeze()`, myös silloin kun asiakas
katkaisee latauksen kesken (AsyncWebServerin disconnect-käsittelijä). Ylimääräinen
`unfreeze()` ei tee mitään.

### 4.6 `json_writer.h` (WP-3)

Pieni kirjoitin kiinteään `char`-puskuriin: `beginObject/endObject/beginArray/endArray`,
`key()`, `value(float, decimals)`, `value(int)`, `value(bool)`, `value(const char*)` (escape),
`null()`. Pilkut automaattisesti. `overflow()` kertoo, ylittyikö puskuri. Ei NaN/Inf JSONiin
(→ `null`).

### 4.7 `status.h` (WP-4)

```cpp
namespace sbx {
struct StatusInput {
  ChannelState ch[kChannels];
  RpmEstimate rpm;
  bool zero_valid;     // Calibration::zero_valid
  bool gain_valid;     // Calibration::gain_valid
  float now_s;         // monotoninen aika
};
class StatusMachine {
 public:
  void configure(const StatusParams& p);   // nollaa ajastimet
  void reset();
  StatusResult update(const StatusInput& in);   // ~20 Hz
};
}
```

Prioriteetti (korkein ensin) — §8:n suunnittelusääntö: *vikatilat voittavat aina säätötilat*:

1. `FaultMaster` — master täyttää rivin 6 tai 7 ehdon
2. `FaultSensor` — jokin kanava rivi 7
3. `FaultHose` — jokin kanava rivi 6 (|taso| < `hose_off_kpa` ja ei pulssia, kun muut pulssivat)
4. `NoSignal` — rivit 1+8 (D10)
5. `Unstable` — rivi 2, sekä rpm < `sync_rpm_min` ja vakausajan (`stable_hold_s`) odotus
6. `RpmTooHigh` — rivi 9
7. `Synced` / `Near` / `OutOfSync` — rivit 3/4a/4b `spread`in mukaan

Liput ovat tilasta riippumattomia: `flag_saturation` (rivi 10), `flag_uncal_zero` (D11: ilman
nollausta **ei koskaan Synced** — näytetään enintään `Near`), `flag_uncal_gain` (rivi 11),
`flag_drift` (rivi 5: Δ-luvut pysyvät näkyvissä). `request_auto_zero` nousee kun `NoSignal`
on kestänyt `off_hold_s` ja `auto_zero` on päällä.

Tekstit UTF-8-suomeksi, desimaalipilkku, esim. headline `"Synkronoitu"` ja advice
`"SYL 3: avaa läppää – imee 2,1 kPa enemmän kuin SYL 1"`. Säätösuunta:
`vac_delta > 0` (imee enemmän kuin master, läppä suljetumpi) → "avaa", `< 0` → "sulje".
Ruuvin kiertosuunta riippuu moottorista (D6) eikä sitä kerrota.

`flag_saturation` nousee vain pulssivista kanavista (`has_pulse && saturated`). Rivi 10 koskee
pulssin huippua, ja GN-anturi on ilmanpaineessa leikkausrajallaan (docs/rauta.md §1.2), joten
pelkkä `saturated` olisi päällä jokaisella sammutuksella (tarkennettu 30.10.2026).

Pitoajat ja yhteismuotoinen muutos (WP-2c, 30.10.2026):
- `HoseOff` (rivi 6) vasta `kHoseHoldS` = 0,5 s pulssittomuuden jälkeen. Käynnistyksessä ja
  sammutuksessa kanavat saavat ja menettävät pulssin eri sykleillä. Masterin vaihto nollaa
  ajastimet, joten pitoaika alkaa alusta.
- `flag_saturation` vasta, kun ehto on jatkunut `kSatHoldS` = 1,2 s (DSP:n `saturated`- ja
  `has_pulse`-muistit ovat 1 s, joten käynnistys ja sammutus eivät välähdä).
- Rivi 5: kun taso-EMA karkaa syklikeskiarvosta tai keskiarvot siirtyvät 0,3–0,6 s:ssa
  kaikissa kanavissa samaan suuntaan yli `kCommonMoveKpa` = 1 kPa, tila on `Unstable`
  ("Alipaine muuttuu kaikissa sylintereissä"). Kanavien liukuvat keskiarvot päivittyvät eri
  hetkinä, joten yhteismuotoinen muutos näkyisi muuten hetkellisenä spreadinä (20 kPa →
  1,7–2,5 kPa). Vakausajastin ei nollaudu. Yhden ruuvin säätö ei laukaise tätä.

### 4.8 `processor.h`, `snapshot.h`, `sim_api.h` (WP-6)

Processor kokoaa coren yhdeksi olioksi, jota ESP32-liima kutsuu mutexin takaa. Kaikki
päätöslogiikka (milloin nollataan, milloin vahvistusmittaus päättyy, mitä snapshotissa on) on
täällä, jotta se on native-testattavissa. Liimaan jää vain ajastus, laitteet ja verkko.

```cpp
namespace sbx {
struct CalStatus {
  bool busy = false;          // vahvistusmittaus käynnissä
  char msg[96] = {0};         // viimeisin kalibrointiviesti suomeksi, UI näyttää sellaisenaan
};

class Processor {
 public:
  Processor(RawSample* ring_storage, size_t ring_capacity);   // ESP32: staattinen 8192 näytettä

  void configure(const Config& c);         // Dsp + StatusMachine + muunnos; nollaa DSP:n ja ajastimet,
                                           // keskeyttää käynnissä olevan vahvistusmittauksen
  const Config& config() const;
  void setCalibration(const Calibration& cal);   // käynnistyksessä NVS:stä; zero_valid = false (D11)
  const Calibration& calibration() const;

  void push(const RawFrame& f);            // 1 kHz: rengas, mV → kPa (nominalKpa + applyCal), Dsp,
                                           // vahvistusmittauksen keruu, aaltomuodon desimointi
  void tick(float now_s);                  // ~20 Hz: StatusMachine, automaattinen nollaus
                                           // (request_auto_zero), vahvistusmittauksen päättäminen

  CalResult zeroNow();                     // POST /api/cal/zero (vaatii ilmanpaineen, |taso| < off_kpa)
  bool startGainCapture();                 // POST /api/cal/gain: 2 s keruu nimellisistä näytteistä
  void resetGain();                        // POST /api/cal/reset: gain = 1, gain_valid = false
  bool setMaster(int ch);                  // POST /api/master (D7); ei nollaa DSP:tä

  bool takeConfigDirty();                  // pysyvä muutos (master) → liima tallentaa NVS:ään
  bool takeCalDirty();                     // gain muuttui → liima tallentaa NVS:ään

  const StatusResult& status() const;
  const ChannelState& channel(int i) const;
  RpmEstimate rpm() const;
  const CalStatus& calStatus() const;
  RawRing& ring();                         // /api/raw.csv jäädyttää ja vapauttaa
  int takeWave(float (*rows)[kChannels], int max_rows);  // 16 näytteen lohkokeskiarvot (62,5 Hz)
                                                         // edellisen haun jälkeen, vanhin ensin
};

struct SnapshotMeta {
  uint32_t t_ms;        // millis()
  const char* src;      // "sim" | "adc" | "hx710b"
  float fs_hz;          // mitattu näytetaajuus (liima laskee)
  uint32_t heap;        // vapaa heap (native: 0)
};
// §5:n snapshot yhdelle riville. Tyhjentää aaltojonon. 0 = ei mahtunut.
size_t snapshotJson(Processor& p, const SnapshotMeta& m, char* buf, size_t n);
}
```

- Automaattinen nollaus: kun `StatusResult::request_auto_zero` nousee, Processor laskee
  nimelliset tasot kääntämällä kalibroinnin DSP:n `level_kpa`:sta ja kutsuu `computeZero`a
  rajalla `off_kpa`. Onnistuminen asettaa `zero_valid`in; viesti `CalStatus::msg`:iin.
- Vahvistusmittaus kerää 2000 nimellistä näytettä (`CalCapture`) ja päättyy `tick`issä
  (`computeGain`), jotta `push` pysyy O(1):nä. Tulos `calResultText`illä `msg`:iin.
- `sim_api.h`: `/api/sim`-avainten (§5) ja `SimParams`in välinen muunnos: arvon asetus
  tekstistä avaimella ja kaikki avaimet JSON-objektiksi. Simulaattori on liiman omistama
  (näytteenottotaski), mutta muunnos on corea, jotta se on testattavissa.
- Integraatiotestit (`test/test_integration/`): EngineSim → Processor simuloidulla ajalla,
  **jokainen §8:n rivi omana testinään** ja lisäksi D10, D11 ja D12.

### 4.9 ESP32-liima (WP-7)

Tiedostot `src/main.cpp` ja `src/hal/*` (Arduino-ESP32 2.0.17, ESPAsyncWebServer 3.6.0).

- **Näytteenotto:** oma FreeRTOS-taski ytimellä 1, `vTaskDelayUntil` 1 ms:n välein
  (`CONFIG_FREERTOS_HZ` = 1000). Lähde `Config::source`n mukaan: `EngineSim::next()`, ADC1
  tai HX710B (penkkitesti, D14). ADC: protolevyllä GPIO34/35/32 ilman kiskoa (`rail_mv` = 0)
  ja hylättävä lämmittelyluku per kanava (S&H-ylikuuluminen ilman pinnien 100 nF:ää,
  docs/rauta.md §2.2). Suunnitelman levyllä GPIO34/35/36 + kisko GPIO39. Kehykset
  jonoon, josta käsittelytaski vie ne `Processor::push`iin. Täysi jono pudottaa kehyksen, ja
  pudotus näkyy seq-aukkona (`diag.gaps`).
- **Ei pitkiä keskeytyskieltoja eikä busy-waitia.** Bring-up-firmwaressa `noInterrupts()`
  yli 90 % ajasta myöhästytti AP:n beaconit ja laukaisi watchdogin (commit b838911).
- **Tahdistus (WP-7a):** `seq` laskee 1 ms:n jaksoja. Alle 3 ms:n myöhästyminen otetaan
  kiinni. Pidempi pysähdys (esim. flash-kirjoitus) ohitetaan, ja seq hyppää yli, jolloin
  pudotus näkyy aukkona eikä näytteitä väärennetä. Jonon alkio on `RawFrame` sekä aikaleima,
  myöhästyminen ja lähteen lukuaika (diagnostiikka).
- **Käsittely:** `tick` 20 Hz, snapshot `push_rate_hz`:llä SSE-tapahtumana `snap`. Processor
  mutexin takana. Web-käsittelijät ajetaan AsyncTCP-taskissa ytimellä 0.

  | taski | ydin | prio | tehtävä |
  |---|---|---|---|
  | `sbx_sample` | 1 | 20 | näytteenotto, `vTaskDelayUntil` 1 ms |
  | `sbx_proc` | 1 | 5 | jonon tyhjennys 4 ms:n välein → `Processor::push`, `tick` 20 Hz |
  | `sbx_push` | 0 | 11 | SSE-lähetys; `async_tcp` + 1, koska ESPAsyncWebServer 3.6.0:n `AsyncEventSource` lukee asiakkaan TCP-osoittimen ilman lukkoa, joten `send()` ja `count()` kutsutaan vain täältä |
  | `async_tcp` | 0 | 10 | web-käsittelijät (task watchdog 5 s) |

- **Wi-Fi AP:** SSID `Suckboxx`, salasana `alipaine123`, kanava 1, enintään 4 asiakasta
  (bring-up-firmwaren arvot), `WiFi.persistent(false)`, jotta asetuksia ei kirjoiteta
  flashiin. `setSleep(false)` ei vaikuta pelkässä AP-tilassa (WiFiGeneric.cpp), mutta se
  jätetään paikalleen.
- **Käännösliput:** `SBX_SAMPLE_SOURCE` valitsee oletuslähteen (`esp32dev_adc`,
  `esp32dev_hx`), `SSE_MAX_QUEUED_MESSAGES=8` (hitaalle asiakkaalle enintään ~0,5 s vanhaa
  dataa) ja `CORE_DEBUG_LEVEL=0` (kirjaston loki odottaisi sarjaporttia).
- **NVS** (`Preferences`, nimiavaruus `sbx`, `hal/nvs_store.*`): konfiguraatio kenttä
  kerrallaan rekisterin avaimilla (§4.4) ja skeemaversio `_v`; kalibroinnista vain gainit
  `_g0`…`_g2` ja `_gv` (D11: nollaus ei ole pysyvä). `loopTask` tallentaa dirty-lipuista
  10 s välein, jotta flash ei kulu (kirjoitus näkyy samplerin aukkoina). Lähteen vaihto
  tallentuu ja käynnistää laitteen uudelleen. Ilman tallennettua konfiguraatiota lähde on
  käännöksen `SBX_SAMPLE_SOURCE`, ja asetusten nollaus (`/api/config/reset`) säilyttää sen.
- **Web (WP-7b, 30.10.2026):** §5:n reitit, sivu `src/web/web_page.h`:n `WEB_PAGE_HTML`ista
  raakana (ei gzip:iä, `inline constexpr`, yksi kopio flashissa). WP-7a:n diagnostiikkasivu
  on `/diag`:ssa (`/api/diag`, SSE-tapahtuma `diag` 5 Hz). `/api/raw.csv` jäädyttää renkaan
  ja striimaa `CsvWriter`illa chunked-vastauksena; rengas vapautetaan latauksen lopussa tai
  `onDisconnect`issa, kumpi tahansa ensin. Snapshot rakennetaan `sbx_push`issa Processorin
  mutexin alla juuri ennen lähetystä.

---

## 5. Web-rajapinta (laite ja `tools/mock_server.py` identtisiä)

Transport **D9**: Server-Sent Events push + REST-komennot. Puhelin → laite -suunnassa
liikkuu vain harvoja komentoja, joten WebSocketin kaksisuuntaisuutta ei tarvita; SSE
yhdistää automaattisesti uudelleen ja on mockattavissa pelkällä Pythonin standardikirjastolla.

| metodi | polku | sisältö |
|---|---|---|
| GET | `/` | `web/index.html` |
| GET | `/events` | SSE, tapahtuma `snap`, data = snapshot-JSON yhdellä rivillä, `push_rate_hz` |
| GET | `/api/config` | `configToJson` |
| POST | `/api/config` | form-urlencoded `key=value[&…]` → `{"ok":true}` / `{"ok":false,"err":"…"}`, atominen (§4.4) |
| POST | `/api/config/reset` | oletukset |
| POST | `/api/master` | `ch=0..2` |
| POST | `/api/cal/zero` | `{"ok":…,"msg":"…"}` |
| POST | `/api/cal/gain` | aloittaa 2 s mittauksen; tulos snapshotin `cal`-osiossa |
| POST | `/api/cal/reset` | poistaa vahvistuskalibroinnin |
| GET | `/api/raw.csv` | jäädyttää renkaan, striimaa CSV:n, vapauttaa |
| GET | `/api/sim` | simulaattorin parametrit JSON-objektina (avaimet alla) |
| POST | `/api/sim` | form-urlencoded, mikä tahansa osajoukko avaimista |

Sim-avaimet: `engine_on` (0/1), `rpm`, `jitter`, `drift`, `base`, `err1`, `err2`, `err3`,
`depth`, `res`, `xtalk`, `peak`, `noise`, `fault1`, `fault2`, `fault3` (0 = ei, 1 = letku irti,
2 = anturi alas, 3 = anturi ylös).

### Snapshot (SSE `snap`)

```json
{
  "v": 1, "t": 123456, "src": "sim",
  "state": "near", "fault_ch": -1, "bars": true, "verdict": true,
  "head": "Lähellä", "advice": "SYL 3: avaa läppää – imee 2,1 kPa enemmän kuin SYL 1",
  "flags": {"sat": false, "uncal_zero": false, "uncal_gain": true, "drift": false},
  "master": 0, "tol": [1.5, 3.0],
  "rpm": 851.2, "rpm_sd": 9.1, "rpm_cv": 0.011,
  "spread": 2.1, "worst": 2,
  "ch": [
    {"d": 0.0, "mean": -55.1, "level": -55.0, "min": -72.3, "p2p": 25.0, "rpm": 851.0,
     "mv": 1323, "pulse": true, "fault": false, "sat": false}
  ],
  "wave": {"fs": 62.5, "s": [[-55.1, -54.0, -56.2]]},
  "cal": {"busy": false, "zero": true, "gain": false, "msg": ""},
  "diag": {"fs": 1000.0, "gaps": 0, "heap": 180000, "frozen": false}
}
```

- `state` ∈ `no_signal, unstable, rpm_too_high, synced, near, out_of_sync, fault_sensor,
  fault_hose, fault_master`.
- `ch[i].d` = `vac_delta_kpa` (positiivinen = imee enemmän kuin master). `rpm` on `null`
  kun ei voimassa.
- `wave.s` = edellisen viestin jälkeen kertyneet desimoidut näytteet (16 näytteen
  lohkokeskiarvo → 62,5 Hz), kalibroitua kPa:ta, enintään 16 riviä.

---

## 6. Testaus

- **Native:** `tools/run_native_tests.sh [test_x …]` (g++, ASan/UBSan, `-Werror`), tai
  PlatformIOlla `pio test -e native`. Sallitut Unity-makrot: ks.
  [tools/native_test/unity.h](../tools/native_test/unity.h) — käytä vain niitä.
  Windowsilla molemmat ajetaan WSL:ssä (MinGW-g++:sta puuttuu ASan), Git Bashista
  `wsl --cd "$(pwd -W)" -- bash tools/run_native_tests.sh`.
- **GCC 8.4:** `tools/check_core_gcc84.sh [x.h …]` kääntää jokaisen core-headerin yksinään
  ESP32:n omalla kääntäjällä samoilla varoituksilla. Native-g++ on uudempi eikä huomaa
  §1:n kohdan 6 rajoituksia (esim. liukulukujen `std::to_chars`). Vaatii PlatformIOn
  ESP32-työkaluketjun, eli toimii kehittäjän koneella.
- **UI:** `python3 tools/mock_server.py` → selain `http://localhost:8000`.
- **ESP32-käännös** tehdään kehittäjän koneella (`pio run`). Pilviympäristössä
  PlatformIO-rekisteri ei ole saatavilla.

### 6.1 Nauhoitusten toisto (WP-8)

Raakanauhoitus (laitteen `GET /api/raw.csv` tai `tools/serial_record.py`) ajetaan PC:llä
saman `Processor`-koodin läpi kuin laitteella, ja nauhoitus odotuksineen on
regressiotesti (SUUNNITTELU.md §7 vaihe 4). Lukija, toisto, yhteenveto ja odotukset ovat
`test/support/recording.h`:ssa, jota käyttävät sekä `test_replay` että `tools/replay`.

```sh
bash tools/replay/build.sh            # -> tools/replay/build/replay (WSL; MinGW-g++: SAN=0)
R=tools/replay/build/replay
$R ajo.csv                                       # yhteenveto
$R ajo.csv --jsonl ajo.jsonl                     # tikkikohtainen aikasarja
$R ajo.csv --set hyst_min_kpa=0.5 --set avg_cycles=16   # viritys (S5a, config.h:n avaimet)
$R ajo.csv --no-cal                              # kalibroimattomana (D11: ei vihreää)
$R ajo.csv --from lock                           # tilastot ensimmäisestä tuomiosta alkaen
$R ajo.csv --expect ajo.expect                   # odotukset ilman testiajoa (koodi 1 = rikottu)
$R --sim rpm=900 err2=2 noise=4 --seconds 8 --seed 2 --out x.csv   # EngineSimin nauhoitus
```

- **Muodot.** Sarakkeet tunnistetaan nimestä (`seq`, `ch1_mv`, `ch2_mv`, `ch3_mv`
  pakollisia), joten `/api/raw.csv`, `serial_record.py`:n tiedosto (`%g`-luvut, CRLF) ja
  laitteen raaka sarjavirta kelpaavat. Toisto käyttää vain mV:tä; kPa-sarakkeet ovat
  laitteen laskemia. `# aborted=` lopettaa luvun (ehjä alkuosa toistetaan), ja viimeinen rivi
  ilman rivinvaihtoa ohitetaan varoituksella. Rikkinäinen rivi, kasvamaton seq, yli 65535:n
  seq-hyppy, `fs_hz` ≠ 1000 tai `format` ≠ 1 hylkää tiedoston.
- **Konfiguraatio ja kalibrointi otsakkeesta.** `v_zero_mv`, `mv_per_kpa`, `rail_comp`,
  `rail_nominal_mv` ja `source` → `Config` (`configSetText`); `offset_kpa`, `gain`,
  `zero_valid` ja `gain_valid` → `Processor::restoreCalibration`. Se on vain
  native-käännöksessä (`SUCKBOXX_NATIVE_TEST`), koska laitteen `setCalibration` mitätöi
  nollauksen aina (D11). `--set` sovelletaan sen jälkeen `configure`lla, joten anturin
  siirtofunktion tai lähteen muutos mitätöi nollauksen kuten laitteella. Otsake on
  lataushetken tila: jos nollaus tehtiin kesken nauhoitetun ikkunan, alkuosa toistuu uudella
  offsetilla. Kiskojännitettä ei tallenneta (`rail_mv` = 0, kuten protolevyllä).
- **Ajoitus kuten `proc_task.cpp`.** Herätys 4 ms:n välein (`kProcPollMs`) vie kehykset,
  joiden `seq - seq0` < T, ja tick kutsutaan, kun edellisestä on ≥ 50 ms (`last_tick = now`,
  joten tikit tulevat 52 ms:n välein). Puuttuvaa seq:iä ei syötetä (DSP laskee aikaa
  näytteinä), mutta kello etenee aukon yli. `now_s` on aika nauhoituksen alusta, ja toisto
  alkaa kylmästä Processorista. `--poll-ms 1 --tick-ms 50` on `test_integration`in
  Bench-ajoitus.
- **JSONL** (`--jsonl`), rivi per tick: `t`, `state`, `rpm`, `rpm_cv` (null, kun arvio ei
  ole voimassa), `spread`, `d` [3], `worst`, `flags` {`sat`, `uncal_zero`, `uncal_gain`,
  `drift`}, `fault_ch`, `headline`, `advice`. Nimet ja kanavanumerot kuten snapshotissa (§5).
- **Yhteenveto** (stdout): aika tiloittain, ensimmäinen lukitus (synced/near/out_of_sync),
  rpm ka ± hajonta tikeistä, joilla arvio on voimassa, spreadin mediaani ja p95
  tuomiotikeistä (`show_verdict`) sekä vikatilat kanavineen.
- **`--sim`** (myös WP-12:n `tools/sim_fit.py`:n rajapinta): `/api/sim`-avaimet
  (`sim_api.h`, §5) `key=value`-pareina `--sim`in jälkeen, `--seconds` (≤ 65,536 s,
  16-bittinen seq-ikkuna), `--seed` (oletus 1), `--set` anturiparametreille. Tiedosto
  kirjoitetaan `CsvWriter`illa tuoreesta EngineSimistä (seq alkaa nollasta), ja otsakkeen
  loppuun lisätään `generator=`, `sim_seed=` ja `sim=` (GET `/api/sim` -JSON), jotta totuus
  kulkee tiedostossa. Otsakkeen kalibrointi on offset 0 ja gain 1, `zero_valid=1`
  (`--zero-valid 0`: kuin laite ilman nollausta) ja `gain_valid=0` (`--gain-valid 1`).
  EngineSimissä ei ole anturivirheitä `/api/sim`-avaimilla, joten nollaus antaisi offsetin 0.
  Sama komento ja kääntäjä tuottavat tavulleen saman tiedoston.
- **Golden-vektorit:** `firmware/test/data/recordings/<nimi>.csv` + `<nimi>.expect`.
  `test_replay` ajaa jokaisen parin, ja pari ilman toista kaataa testin. Uusi nauhoitus:
  kopioi CSV (lyhyt pätkä riittää, aloita otsakkeesta), kirjoita `.expect` väitteineen ja
  niiden päättäjineen, tarkista `replay x.csv --expect x.expect`. Koodia ei muuteta.
  Muoto on `test/test_replay/test_main.cpp`:n otsakkeessa: `from_s` (s tai `lock`),
  `final_state`, `allowed_states`, `never_states`, `lock_min_s`, `lock_max_s`, `rpm` +
  `rpm_tol`, `spread_median_min`/`_max`, `spread_p95_max`, `worst_ch`, `fault_ch`,
  `headline`, `advice_prefix`, `flags_never`, `flags_always`, `no_cal`, `set.<avain>`.
  Tuntematon avain on virhe. Testi löytää kansion `__FILE__`:n tai työhakemiston kautta
  (`run_native_tests.sh` ja `pio test -e native`); `SBX_RECORDINGS_DIR` ohittaa haun.
- **Ero laitteeseen:** ESP32:n float-laskenta ja kirjastot (esim. `tanf`) voivat erota
  PC:stä viimeisissä biteissä, joten odotukset ovat toleransseja eivätkä bittivertailuja.
  Pitkän pysähdyksen (flash) aikana laitteen käsittelytaski seisoo myös ja tikkaa kerran
  perään, toisto 52 ms:n välein. Offset tallentuu 4 ja gain 6 desimaalilla (< 1e-4 kPa).
