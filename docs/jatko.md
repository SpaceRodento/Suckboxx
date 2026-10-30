# Jatko-ohje: M1/M2-tila 30.10.2026

Pilvi-istunnon työt keskeytettiin, kun työ siirtyy pöytäkoneelle. Tässä on
tila ja avoimet asiat, jotta jatko onnistuu ilman istunnon lokeja.

## Ensimmäiset komennot pöytäkoneella

```sh
cd firmware
pio test -e native        # oikea Unity; vrt. tools/run_native_tests.sh (shim + ASan/UBSan)
pio run -e esp32dev       # HUOM: src/main.cpp on yhä vanha HX710B-bring-up (M3 korvaa)
cd ..
python3 tools/mock_server.py   # UI selaimeen simulaattoria vasten
python3 tools/embed_web.py --check
```

ESP32-käännöstä ja `pio test` -ajoa ei ole ajettu kertaakaan, koska PlatformIO-rekisteri
oli estetty pilviympäristössä. Odotettavissa on GCC 8.4 -yhteensopivuuskorjauksia.

## Työpakettien tila

Jokainen paketti kulki: toteutus → vastakkainen katselmointi → korjaus → tarkistus.

| Paketti | Tiedostot | Tila |
|---|---|---|
| WP-1 simulaattori | `core/engine_sim.h`, `test_engine_sim` | ✅ katselmoitu, korjattu, tarkistettu |
| WP-2 DSP | `core/dsp.h`, `test_dsp` | ⚠️ korjaus **keskeytyi kesken**; 1 testi punaisena |
| WP-3 sovelluslogiikka | `core/calibration.h`, `config.h`, `json_writer.h`, `raw_ring.h` + testit | ✅ korjattu, tarkistus jäi ajamatta |
| WP-4 status | `core/status.h`, `test_status` | ✅ hyväksytty (vain minor-havaintoja) |
| WP-5 UI | `web/index.html`, `src/web/web_page.h`, `tools/mock_server.py`, `tools/embed_web.py`, `tools/ui_screenshots.js` | ⚠️ katselmoitu, korjaus ei ehtinyt alkaa |

`tools/run_native_tests.sh` commit-hetkellä: 8 testikokonaisuutta, 196 testiä, 1 virhe:
`test_dsp: test_avg_cycles_sets_response_and_noise` (rivi 663, `n1` = 3, odotettu ≤ 2).
Tämä on keskeytyneen WP-2-korjauksen välitila: `dsp.h` ja `test_dsp` voivat olla
keskenään epäjohdonmukaisia.

## Avoimet havainnot (prioriteettijärjestyksessä)

### WP-2 DSP (major)
1. **Syklintunnistus jumittuu 0,8–1,0 s nopean painemuutoksen jälkeen** (`dsp.h` ~rivi 447).
   Schmitt-kynnykset tulevat verhokäyrästä, joka kutistuu vain havaitulla syklirajalla.
   600 rpm:llä (D12-ikkunan sisällä) myös `has_pulse` putoaa.
2. **Korkeilla kierroksilla tunnistus katkeaa kerralla** (~rivi 478). Kiinteä 40 Hz:n alipäästö
   päästää 6000 rpm:n perustaajuudesta (50 Hz) läpi vain |H| ≈ 0,38, joten ≤ 6 kPa:n pulssi
   jää porttiehdon `3·hyst_min_kpa` alle. Ratkaisuehdotus: rajataajuus rpm:n mukaan tai portti
   suodatetun amplitudin suhteessa.
3. Minor-havainnot: hystereesin alarajaa `max(hyst_min_kpa, hyst_frac·p2p)` ei testata;
   `reset()` rakentaa pinoon noin 1 kt:n väliaikaisen `Chan`-olion; lukitusviive on testattu
   vain 850 rpm:llä; rpm-tarkkuustestit toimivat vain epärealistisen pienellä jitterillä (0,2 %).

### WP-5 UI (major)
1. **Palkin väri lasketaan |d|:stä tilasta riippumatta** (`web/index.html` ~rivi 374), joten
   kalibroimaton laite näyttää vihreää. Tämä rikkoo D11:n. Värin pitää tulla tilasta tai tuomiosta.
2. **Tilassa 'Lähellä – kalibroimaton' näytetään silti AVAA/SULJE-neuvo ja worst-kehys** (~rivi 392),
   vaikka status.h ei tarkoituksella anna ruuvineuvoa.
3. Minor-havainnot:
   - 'Palauta oletukset' ei varoita uudelleenkäynnistyksestä, kun lähde vaihtuu.
   - `WEB_PAGE_HTML[]` kopioituu flashiin jokaiseen käännösyksikköön. Korjaus: `inline`/`extern`.
   - Kosketusalueet ovat alle 44 px.
   - SATUROITUU-merkki käyttää koristeväriä.
   - Asteikon tekstit pyöristyvät.
   - `hose_off_kpa` on kovakoodattu arvoon 2.
   - CSS ja JS on pakattu tiheiksi riveiksi (sivu on noin 100 tavua alle 40 KiB:n).
4. `tools/mock_server.py`: kun drift asetetaan nollaan, `dev.sim['rpm']` on päivitettävä arvosta
   `engine.p['rpm']`. Muuten seuraava POST hyppää takaisin vanhaan rpm:ään (vrt. WP-1-korjaus).

### WP-4 status (minor, ei estä)
- `channelList()` aiheuttaa `-Wformat-truncation`-varoituksen ilman sanitizereita. **`pio test -e native` voi
  kaatua tähän** (-Werror); korjataan heti, jos niin käy.
- HoseOff (rivi 6) luokitellaan ilman viivettä: matalalla alipaineella moottorin sammutus näyttää
  hetken väärää letkuvikaa.
- Saturaatiolippu välähtää jokaisella sammutuksella ja käynnistyksellä (`saturated` ja `has_pulse`
  pysyvät molemmat 1 s).
- `hose_off_kpa`-kynnykseltä puuttuu hystereesi, ja driftin tunnistus reagoi kohinaan.
- Kolme mutaatiota selviää testeistä. Yksi niistä on auto-zero-portti, jonka muutos laukaisisi
  nollauksen myös FaultSensor-tilassa.

### WP-3 (minor)
- `fault_lo_mv = 0` / `fault_hi_mv = 3300` kelpaavat, vaikka ne poistavat rivin 7 vikatunnistuksen.
- `calResultText(NotAtmosphere)` puhuu vain alipaineesta, vaikka syy voi olla myös ylipaine.
- `configSetText` käyttää `strtof`ia, joka riippuu localesta. Tämä vaikuttaa vain PC:llä.
- `test_json_writer` laukaisee -O2:lla `-Wformat-truncation`-varoituksen.
- **Rajapintapäätös:** `computeGain` hyväksyy kalibroinnin ilman nollausta (`zero_valid = false`).
  Sen pitäisi todennäköisesti vaatia nollaus. Korjaaja lisäsi testin `test_gain_requires_zero`;
  tarkista, että toteutus vastaa sitä.
- Siirrä `test_json_writer/json_validate.h` kansioon `test/support/` ja päivitä kaksi includea.

### Ristiriidat moduulien välillä
- **Anturi ilmanpaineessa = 2700 mV ≥ `sat_mv` = 2680**, joten DSP:n `saturated` on päällä jokaisella
  kanavalla moottorin ollessa sammuksissa. Joko `sat_mv` nostetaan (esim. 2695) tai saturaatio
  arvioidaan vain pulssin aikana. Päätös kirjataan SUUNNITTELU.md:hen.
- `docs/firmware.md` §4.1 on päivitettävä: `EngineSim::configure(p, s, bool rpm_set = false)`.
  Kun vain drift muuttuu, `params().rpm` kirjoitetaan nykyiseen rpm:ään. Firmwaren `/api/sim`-käsittelijän
  pitää yhdistää POST `sim.params()`:iin ja välittää `rpm_set = (POST sisältää rpm-avaimen)`.
- M4-muistiinpano: tarkista, mitä eFuse-kalibroitu ADC1 (11 dB) lukee GND:hen kytketystä pinnistä.
  Jos lukema on ≥ 100 mV, oletus `fault_lo_mv = 100` ei tunnista oikosulkua.

## Seuraavaksi
1. Korjaa WP-2:n punainen testi ja kaksi major-havaintoa, sitten WP-5:n kaksi major-havaintoa.
2. Aja `pio test -e native` ja korjaa GCC- ja Unity-erot.
3. **M3:** `core/processor.h` + `snapshot.h` (Processor push/tick/snapshot, auto-zero, 2 s:n
   gain-kaappaus, aaltomuodon desimointi 62,5 Hz:iin, snapshot-JSON docs/firmware.md §5:n mukaan)
   sekä integraatiotestit, joissa jokainen §8:n rivi on EngineSim-skenaario. Lisäksi ESP32-liima:
   `main.cpp`, `src/hal/`, 1 kHz:n näytteenotto ytimellä 1, NVS, AsyncWebServer ja SSE sekä
   CSV-vienti.
