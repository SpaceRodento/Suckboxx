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
| `src/core/processor.h`, `snapshot.h` | WP-6 (vaihe 2) | kokoaja + snapshot-JSON + integraatiotestit |
| `src/hal/*`, `src/main.cpp` | WP-7 (vaihe 2) | ESP32-liima |

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
  // Ei nollaa vaihetta. rpm_set = true: p.rpm ankkuroi kierrosluvun aina. Muuten, jos vain
  // drift muuttuu, ankkuri = nykyinen rpm ja params().rpm kirjoitetaan siihen.
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
| POST | `/api/config` | form-urlencoded `key=value[&…]` → `{"ok":true}` / `{"ok":false,"err":"…"}` |
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
- **UI:** `python3 tools/mock_server.py` → selain `http://localhost:8000`.
- **ESP32-käännös** tehdään kehittäjän koneella (`pio run`). Pilviympäristössä
  PlatformIO-rekisteri ei ole saatavilla.
