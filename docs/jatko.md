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
| WP-2 DSP | `core/dsp.h`, `test_dsp`, `test/support/synth_signal.h` | ⚠️ korjattu osittain: tarkistus löysi regression, ks. alla |
| WP-3 sovelluslogiikka | `core/calibration.h`, `config.h`, `json_writer.h`, `raw_ring.h` + testit | ✅ korjattu ja tarkistettu |
| WP-4 status | `core/status.h`, `test_status` | ✅ hyväksytty (vain minor-havaintoja) |
| WP-5 UI | `web/index.html`, `src/web/web_page.h`, `tools/mock_server.py`, `tools/embed_web.py`, `tools/ui_screenshots.js` | ✅ korjattu ja tarkistettu (Playwright 126/126 lähde, 131/131 minimoitu) |

`tools/run_native_tests.sh`: 8 testikokonaisuutta, **207 testiä, 0 virhettä**.

## Avoimet havainnot (prioriteettijärjestyksessä)

### WP-2 DSP: tarkistus ei hyväksynyt (2 × major)

Korjaus ratkaisi alkuperäiset ongelmat (painehypyt vakiokierroksilla, korkeiden kierrosten
tunnistus, käynnistyslukitus 0,9 s → 0,45 s), mutta tarkistaja löysi EngineSim-ajoilla kaksi
asiaa. Tarkistajan mittausohjelmat olivat pilven scratchpadissa eivätkä säily; tilanteet on
toistettava testeinä.

1. **UUSI REGRESSIO: kierrosluvun pudotus yli ~1,8-kertaisesti lukitsee kanavan pulse_timeoutiin
   asti** (`dsp.h` ~rivit 626–644, perustelu ~186–188). Lukittuna stale-aikakatkaisu on
   1,8 × ref_period. Kun todellinen jakso on sitä pidempi, jokainen segmentti katkaistaan kesken
   syklin, yhtään väliä ei arvioida, `ratio_rejects` ei koskaan täyty eikä `ref_period`
   referoidu uudelleen. Kanava odottaa 1 s:n pulse_timeoutin ja lukittuu sitten alusta.
   - Mitattu (32 ajoa per rivi; vanha = 5f4e908): `/api/sim` rpm 3000→850: `has_pulse` putoaa
     32/32 ajossa (max 688 ms), vanha 0/32. 2000→850 ja 1700→850: 32/32, vanha 0/32.
     1500→850: 4/32. Ramppi 3000→850 300 ms:ssa: 32/32. Kaasun katkaisu 3000→850 rpm ja
     −15→−55 kPa: 32/32 (vanha 12–16/32, lyhyemmät).
   - Osuu simulaattoritilaan (jokainen rpm:n lasku on askel) ja oikeaan kaasun katkaisuun
     (kaasutus → tyhjäkäynti).
   - Korjausehdotukset: poistu lukituksesta (ref_period = 0), kun N peräkkäistä stale-katkaisua
     ilman kirjausta; tai laske stale-katkaisu `ratio_rejects`iin; tai käytä stale-rajana
     max(1,8 × ref, window_n_), kun since_cycle > 1,8 × ref. Lisää EngineSim-testit
     3000→850 ja 2000→850 sekä nopea kaasun katkaisu.
2. **Kaasun blippaus ei vieläkään toimi EngineSimillä.** 850→2500 rpm, −55→−25 kPa, syvyys 20→12,
   300 ms ylös / 300 ms pito / 700 ms alas: 24/24 ajossa yli 500 ms:n aukko, kaksi kolmesta
   kanavasta jäätyy 850–900 ms tyhjäkäyntiarvoihin, ja mediaani-rpm näyttää huipusta vain
   0,43–0,48 (vanha 0,50).
   - Nousu: tyhjäkäynnin A (20,7 kPa) elää pidempään kuin kutistuva pulssi (~12 kPa vs. 13,4 kPa
     laukaisuraja). Uudelleenreferointi vie ~5 rajaa, koska jokainen liian lyhyt hylkäys
     epäluotettavoittaa myös seuraavan välin (`kMaxRatioRejects` = 2).
   - Lasku: `confirmPending`in ehto `pend_amp >= 0.7*depth` (`dsp.h` ~781) hylkää jokaisen syklin,
     kun alipaine syvenee hidastuksessa.
   - 1500–2000 rpm ≥ 300 ms:n nousulla toimii nyt (vanha jäätyi 0,6–1,0 s). 2500 rpm ei toimi
     millään nousuajalla, 3000 rpm putoaa 24/24.
   - `test_throttle_blip_tracks_rpm_and_vacuum` menee läpi vain siksi, että SynthSignal generoi
     imutapahtumat ~2 sykliä etukäteen. Testin pitää ajaa EngineSimillä, tai SynthSignalin pitää
     soveltaa rpm-muutos heti.
3. Tiedoksi (ei estä):
   - Hetkellinen 10–15 kPa:n painehyppy voi hukata enintään 2 sykliä (aukko ≤ 3,05 T), jos se
     osuu pulssin kohdalle. Korjaaja hylkäsi vaatimuksen "≤ 2 T" perustellusti: yhden pulssin
     menetyksen jälkeen 2T-väli on hylättävä. Rampit (todellinen kaasu) eivät hukkaa sykliä.
   - Kohinansieto on hieman huonompi dokumentoidun 0,4 kPa RMS -rajan yläpuolella (0,5 kPa:
     7 väärää sykliä / 60 s, vanha 4). Ota huomioon `hyst_min_kpa`n oletuksessa: 0,3 riittää
     ~0,4 kPa RMS:lle, 0,6–0,8 kPa tarvitsee ~0,5.
   - 6000 rpm:llä rpm-tarkkuus on ~1,5 % tunnistusrajan lähellä (rajat dokumentoitu `dsp.h`:n
     otsikossa). Ei koske synkronointia (500–3000 rpm, tyhjäkäynti 850–950).

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
1. **WP-2:** korjaa rpm-pudotuksen lukkiutuminen ja blippaus EngineSim-testien kanssa (ks. yllä).
   Synkronointi tyhjäkäynnillä vakiokaasulla toimii jo; nämä vaikuttavat kaasun muutoksiin.
2. Aja `pio test -e native` ja `pio run -e esp32dev`, ja korjaa GCC 8.4- ja Unity-erot.
3. **M3:** `core/processor.h` + `snapshot.h` (Processor push/tick/snapshot, auto-zero, 2 s:n
   gain-kaappaus, aaltomuodon desimointi 62,5 Hz:iin, snapshot-JSON docs/firmware.md §5:n mukaan)
   sekä integraatiotestit, joissa jokainen §8:n rivi on EngineSim-skenaario. Lisäksi ESP32-liima:
   `main.cpp`, `src/hal/`, 1 kHz:n näytteenotto ytimellä 1, NVS, AsyncWebServer ja SSE sekä
   CSV-vienti.
