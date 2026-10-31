# Jatko-ohje: M1/M2-tila 30.10.2026

Pilvi-istunnon työnkulku (5 työpakettia, 18 agenttia) on valmis, ja kaikki on commitoitu.
Tässä on tila ja avoimet asiat, jotta jatko onnistuu ilman istunnon lokeja.

## Ensimmäiset komennot pöytäkoneella

```sh
cd firmware
pio test -e native        # oikea Unity; vrt. tools/run_native_tests.sh (shim + ASan/UBSan)
pio run -e esp32dev       # HUOM: src/main.cpp on yhä vanha HX710B-bring-up (M3 korvaa)
cd ..
python3 tools/mock_server.py   # UI selaimeen simulaattoria vasten (http://localhost:8000)
python3 tools/embed_web.py --check
python3 tools/embed_web.py --selftest
python3 tools/mock_server.py --check-config
```

ESP32-käännöstä ja `pio test` -ajoa ei ole ajettu kertaakaan, koska PlatformIO-rekisteri
oli estetty pilviympäristössä. Odotettavissa on GCC 8.4 -yhteensopivuuskorjauksia (koodi
käyttää vain C++17:ää, joka GCC 8:ssa on). `[env:native]`-ympäristössä ei ole `-Werror`ia,
joten varoitukset eivät kaada `pio test`-ajoa. Kaikki testit kääntyvät silti -O2- ja
-Os-tasoilla ilman varoituksia (`-Wall -Wextra -Wshadow -Wdouble-promotion -Werror`).

## Työpakettien tila

Jokainen paketti kulki: toteutus → vastakkainen katselmointi → korjaus → tarkistus.

| Paketti | Tiedostot | Tila |
|---|---|---|
| WP-1 simulaattori | `core/engine_sim.h`, `test_engine_sim` | ✅ korjattu ja tarkistettu |
| WP-2 DSP | `core/dsp.h`, `test_dsp`, `test/support/synth_signal.h` | ✅ uudelleensuunniteltu (kanavien välinen omistajuus), 5 korjaus- ja tarkistuskierrosta; kierros 6 tarkistamatta, ks. alla |
| WP-3 sovelluslogiikka | `core/calibration.h`, `config.h`, `json_writer.h`, `raw_ring.h` + testit | ✅ korjattu ja tarkistettu |
| WP-4 status | `core/status.h`, `test_status` | ✅ hyväksytty (vain minor-havaintoja) |
| WP-5 UI | `web/index.html`, `src/web/web_page.h`, `tools/mock_server.py`, `tools/embed_web.py`, `tools/ui_screenshots.js` | ✅ korjattu ja tarkistettu (Playwright 126/126 lähde, 131/131 minimoitu) |

`tools/run_native_tests.sh`: 8 testikokonaisuutta, **223 testiä, 0 virhettä** (test_dsp 51).

## Avoimet havainnot (prioriteettijärjestyksessä)

### WP-2 DSP: uudelleensuunniteltu, kierros 6 tarkistamatta

Ensimmäisen korjauksen jälkeen syklintunnistus mitoitettiin uudelleen kuudella kierroksella
(korjaaja → kaksi riippumatonta tarkistajaa, jotka mittasivat EngineSim-ketjulla tiukalla
mittarilla: jokaisen kirjatun syklin molemmat merkit kohdistetaan simulaattorin imuihin).
Ydin, joka ratkaisi vuotopulssien ja kierrosmuutosten ongelmat:
- **Kanavien välinen pulssin omistajuus:** vuotopulssin aikana sen aiheuttanut sylinteri näkyy
  omalla kanavallaan paljon syvempänä, joten vuoto ei voi olla syklin raja. Vertailu tehdään
  aina, myös käynnistyksessä.
- **Todistajat:** muiden kanavien omien pulssien on osuttava syklissä opittuihin paikkoihin
  (1/3, 2/3), mikä hylkää ohitetun pulssin (2T) eikä vaadi viitejaksoa (rpm:n muutos näkyy
  noin yhden jakson päästä).
- **Vikaantunut kanava** (mV-vika tai letku irti, tunnistetaan tasosta) on heti pois
  vertailusta ja asettuu palattuaan uudelleen.
Tulos kierroksen 5 tarkistuksessa (sadat tuhannet ajot, xtalk 0/0,3/0,5): väärät syklit 0
rpm-pudotuksissa, blipeissä, alipaineen askelissa ja rampeissa, käynnistyksissä,
uudelleenkäynnistyksissä (myös heikko sylinteri) ja letkun irrotuksessa/takaisinkytkennässä.
Mitatut rajat ovat `dsp.h`:n otsikossa ja tiivistettynä docs/firmware.md §4.2:ssa.

**Kierros 6 (commit "korjaajan viimeisin tila") jäi tarkistamatta:** korjaaja keskeytyi tilin
käyttörajaan loppumittauksen aikana. Siinä korjattiin kierroksen 5 tarkistajien löydökset:
hetken kuolleen kanavan veto (terveiden kanavien has_pulse saattoi pudota jopa 1,4 s lyhyen
letkuvian jälkeen), yhden todistajan taukopoikkeus (vuotosykli yksi letku irti, xtalk 0,5) ja
testit mutanteille A4/N12/N21. Testit menevät läpi (223/223), mutta riippumaton mittaus puuttuu.

Hyväksytyt poikkeamat (orkesteroijan päätös, dokumentoitu `dsp.h`:ssa):
1. Yksittäisen syklin rpm-virhe nopeassa kaasun katkaisussa ≤ 10 % ja hetkellisen rpm-askeleen
   ylittävä sykli ≤ 17 % (merkit oikeissa imuissa; näyttö ei vääristy).
2. Alle 200 ms:n alipaineramppi voi maksaa ≤ 2 sykliä.
3. Heikko sylinteri ≥ 4500 rpm:llä ja heikko kanava itse pikablipeissä voi menettää has_pulsen.
4. xtalk 0,5:n blipeissä pidemmät välit (≤ 450 / 750 ms).
5. Kaksi letkua irti (varatila) ja simulaattorin hetkellinen rpm-askel letkun ollessa irti:
   väärät syklit mahdollisia (tila on joka tapauksessa FaultHose).
6. Harvinaiset yhdistelmät: tauolliset tupla-/kolmoisblipit heikon sylinterin tai 3 %:n
   jitterin kanssa, 100 ms:n nousun blipit, vika tuplablipin aikana.

Avoimet:
- Riippumaton tarkistus kierrokselle 6 (kaksi tarkistajaa, ~1 h, suurehko tokenikulutus).
- Yhden letkun ollessa irti letkujen viive-eron raja on ~1,5 ms 6000 rpm:llä (kaikilla
  letkuilla 2 ms). R1 (samat letkut) riittää.
- Kun oikeaa moottoridataa on (M6), tarkista omistajuuden marginaali (`kOwnMargin` 25 %) ja
  oikea xtalk-taso; simulaattorin oletukset eivät ole todennettuja.

### WP-5 UI (korjattu)
Kaikki major- ja minor-havainnot on korjattu ja tarkistettu:
- Palkin väri tulee laitteen tuomiosta (D11), eikä kalibroimattomana näytetä ruuvineuvoa.
- Kosketusalueet ovat ≥ 44 px, ja `WEB_PAGE_HTML` on `inline constexpr`.
- `embed_web.py` minimoi sivun (37 896 t < 40 000 t) ja tarkistaa, että minimoitu DOM, CSSOM ja
  JS-AST vastaavat lähdettä. `mock_server.py --minified` tarjoilee täsmälleen laitteen tavut.
- `mock_server.py` vastaa nyt laitteen kalibrointilogiikkaa: NoZero-portti, pareittainen
  Mismatch-sääntö ja `calResultText`-tekstit. Lisäksi drift = 0 päivittää sim-rpm:n.

Jatkoon (M3):
- Snapshotiin eksplisiittiset kentät `ch[i].cond` (ok/idle/sensor/hose/blocked/lost) sekä `act_ch`
  ja `act_dir`, jotta UI ei johda status.h:n päätöksiä itse (`adjCh()`, letku vs. tukos).
  Snapshot-serialisoijan pitää pyöristää `spread` vähintään kahteen desimaaliin.
- Vanha `firmware/src/web_page.h` määrittelee yhä globaalin `WEB_PAGE_HTML`:n, ja `main.cpp`
  käyttää sitä. Poista se, kun `main.cpp` vaihdetaan uuteen `src/web/web_page.h`:hon.
- CI:hin: `embed_web.py --check`, `embed_web.py --selftest`, `mock_server.py --check-config`.
- Ei testattu oikeilla iOS Safari- tai Android Chrome -selaimilla.

### WP-4 status (minor, ei estä)
- HoseOff (rivi 6) luokitellaan ilman viivettä: matalalla alipaineella moottorin sammutus näyttää
  hetken väärää letkuvikaa.
- Saturaatiolippu välähtää jokaisella sammutuksella ja käynnistyksellä (`saturated` ja `has_pulse`
  pysyvät molemmat 1 s).
- `hose_off_kpa`-kynnykseltä puuttuu hystereesi, ja driftin tunnistus reagoi kohinaan.
- Kolme mutaatiota selviää testeistä. Yksi niistä on auto-zero-portti, jonka muutos laukaisisi
  nollauksen myös FaultSensor-tilassa.

### WP-3 (korjattu)
Molemmat major-havainnot (pareittainen Mismatch, viitelaskettu freeze + `version()` +
CsvWriterin keskeytys) ja minor-havainnot on korjattu ja tarkistettu. `computeGain` vaatii nyt
nollauksen (`CalResult::NoZero`). `fault_lo_mv` on 50…500 ja `fault_hi_mv` 2500…3250, ja
`configSetText` ei enää riipu localesta. `json_validate.h` on siirretty `test/support/`-kansioon.
- WP-7: jokaista `freeze()`-kutsua vastaa täsmälleen yksi `unfreeze()`, myös kun asiakas
  katkaisee CSV-latauksen (kirjattu docs/firmware.md §4.5).

### Ristiriidat ja muistiinpanot
- **Anturi ilmanpaineessa = 2700 mV ≥ `sat_mv` = 2680**, joten DSP:n `saturated` on päällä
  jokaisella kanavalla moottorin ollessa sammuksissa. status.h kiertää tämän näyttämällä lipun
  vain pulssin aikana. Siistimpi ratkaisu: `sat_mv` ylös (esim. 2695) tai saturaatio arvioidaan
  DSP:ssä vain pulssin aikana. Päätös kirjataan SUUNNITTELU.md:hen.
- M4: tarkista, mitä eFuse-kalibroitu ADC1 (11 dB) lukee GND:hen kytketystä pinnistä. Jos
  lukema on ≥ 100 mV, oletus `fault_lo_mv = 100` ei tunnista oikosulkua (nostettavissa 500:aan).
- docs/firmware.md §4.1–4.5 on päivitetty: `configure(…, rpm_set)`, `lpGain()`, uusi
  syklintunnistus, `NoZero`, viitelaskettu `freeze()`, `version()` ja `aborted()`.

## Seuraavaksi
1. **WP-2:** halutessa riippumaton tarkistus kierrokselle 6 (ks. yllä); muuten valmis M3:een.
2. Aja `pio test -e native` ja `pio run -e esp32dev`, ja korjaa GCC 8.4- ja Unity-erot.
3. **M3:** `core/processor.h` + `snapshot.h` (Processor push/tick/snapshot, auto-zero, 2 s:n
   gain-kaappaus, aaltomuodon desimointi 62,5 Hz:iin, snapshot-JSON docs/firmware.md §5:n mukaan)
   sekä integraatiotestit, joissa jokainen §8:n rivi on EngineSim-skenaario. Lisäksi ESP32-liima:
   `main.cpp`, `src/hal/`, 1 kHz:n näytteenotto ytimellä 1, NVS, AsyncWebServer ja SSE sekä
   CSV-vienti.
