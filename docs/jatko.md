# Jatko-ohje: M1/M2-tila 30.10.2026

Kaksi istuntoa työskenteli rinnakkain 30.10.2026. Pilvi-istunto teki WP-1…WP-5 ja niiden
jälkikorjaukset, viimeisenä commit 63c865c. Pöytäkoneen istunto teki WP-5b:n, WP-6:n ja
WP-7a:n haaraan `dev/firmware`. Molemmat on yhdistetty `dev/firmware`en. UI:ssa molemmat
korjasivat samat WP-5-havainnot, ja pohjaksi valittiin WP-5b: sivu on laitteella raakana
(raja 64 KiB), ei minimoituna. Pilvestä tulivat DSP-, status- ja testikorjaukset sekä
mock_server.py:n kalibrointilogiikka (NoZero, pareittainen Mismatch, calResultText).

## Oraakkelit

```sh
wsl --cd "<repo>" -- bash tools/run_native_tests.sh      # native + ASan/UBSan, -Werror
bash tools/check_core_gcc84.sh                            # core-headerit xtensa GCC 8.4:llä
cd firmware && PYTHONUTF8=1 pio run -e esp32dev           # myös esp32dev_adc, esp32dev_hx
python3 tools/embed_web.py --check
python3 tools/mock_server.py --check-config
python3 tools/mock_server.py --port 8765 &                # sitten:
NODE_PATH="$(npm root -g)" node tools/ui_screenshots.js --url http://localhost:8765 --out <hakemisto>
```

Tila yhdistämisen jälkeen: native 12 kokonaisuutta, 279 testiä, joista 4 punaista. Kaikki
neljä ovat `test_integration`issa (rivit 5, 6, 9, 10, ks. ”Seuraavaksi”), ja ne olivat
punaisia jo ennen yhdistämistä. GCC 8.4 OK (12 headeria). esp32dev SUCCESS (RAM 13,9 %,
flash 61,5 %). UI-savutesti mockia vasten: TULOS: OK.

## Työpakettien tila

Jokainen paketti kulki: toteutus → vastakkainen katselmointi → korjaus → tarkistus.

| Paketti | Tiedostot | Tila |
|---|---|---|
| WP-1 simulaattori | `core/engine_sim.h`, `test_engine_sim` | ✅ korjattu ja tarkistettu |
| WP-2 DSP | `core/dsp.h`, `test_dsp`, `test/support/synth_signal.h` | ⚠️ korjattu osittain: tarkistus löysi regression, ks. alla |
| WP-3 sovelluslogiikka | `core/calibration.h`, `config.h`, `json_writer.h`, `raw_ring.h` + testit | ✅ korjattu ja tarkistettu |
| WP-4 status | `core/status.h`, `test_status` | ✅ hyväksytty (vain minor-havaintoja) |
| WP-5 / WP-5b UI | `web/index.html`, `src/web/web_page.h`, `tools/mock_server.py`, `tools/embed_web.py`, `tools/ui_screenshots.js` | ✅ WP-5b:n toteutus (5da5ca1), mockissa pilven kalibrointilogiikka |
| WP-6 Processor | `core/processor.h`, `snapshot.h`, `sim_api.h`, `form.h` + testit, `test_integration` (§8 riveittäin) | ✅ valmis (09ce1b9); integraatiotesteissä 4 tiedettyä punaista |
| WP-7a ESP32-alusta | `src/` (1 kHz näytteenotto, AP, web, SSE-diagnostiikka) | ✅ valmis (96f7caa) |
| WP-7b ESP32-liima | ks. ”Seuraavaksi” | ⏳ ei aloitettu |

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

### WP-5 UI (korjattu, WP-5b:n toteutus)
Kaikki major- ja minor-havainnot on korjattu ja tarkistettu (5da5ca1:n viesti):
- Vihreä on varattu tilalle synced + verdict, ja AVAA/SULJE luetaan laitteen advice-tekstistä
  (D11). Kalibroimaton laite ei näytä vihreää eikä ruuvineuvoa.
- Kosketusalueet ovat ≥ 44 px, ja `WEB_PAGE_HTML` on `inline constexpr char[]`.
- `embed_web.py` upottaa sivun raakana (raja 64 KiB), joten mock ja laite tarjoilevat samat tavut.
  Pilven minimointi (DOM/CSSOM/AST-vastaavuus, `--minified`) jäi pois yhdistämisessä.
- `mock_server.py` vastaa laitteen kalibrointilogiikkaa: NoZero-portti, pareittainen
  Mismatch-sääntö ja `calResultText`-tekstit. Lisäksi drift = 0 päivittää sim-rpm:n.

Jatkoon:
- Snapshotiin rakenteelliset kentät `ch[i].cond` (ok/idle/sensor/hose/blocked/lost) ja
  `act {ch, open}`, jotta UI ei jäsennä advice-tekstiä eikä johda status.h:n päätöksiä itse.
  `worst` asetetaan nyt aina, ja `spread` on pyöristetty kahteen desimaaliin.
- CI:hin: `embed_web.py --check`, `mock_server.py --check-config` ja UI-savutesti.
- Ei testattu oikeilla iOS Safari- tai Android Chrome -selaimilla.

### Pöytäkoneen istunnon löydökset (käyttäjän päätettävät)
1. **H1/R4, väärä vihreä.** GN-anturi leikkaa ilmanpaineessa (2700 mV = leikkausraja,
   rauta.md §1.2). Positiivinen offset jää siksi näkymättä, eikä automaattinen nollaus poista
   sitä. WP-6:n integraatiotestissä laite näytti ”Synkronoitu” (0,49 kPa), vaikka todellinen ero
   oli 2,50 kPa. Varmistus M5:ssä: puhalla letkuun ja katso, nouseeko lukema yli 2,7 V:n.
   Vaihtoehdot: vahvistuskalibrointi noin −50 kPa:ssa tai GPN-anturi.
2. **ADC:n epälineaarisuus.** 11 dB:n lineaarinen alue on noin 150–2450 mV, ja nollapiste 2,7 V
   on sen yläpuolella. Mittaa penkissä; tarvittaessa jakaja.
3. **Rivin 7 rajat.** 100 / 3150 mV eivät osu ADC:n todellisiin ääripäihin (raw 0 ≈ 142 mV,
   raw 4095 ≈ 3060–3209 mV). WP-7a koodaa lattian ja katon 0 / 3300 mV:ksi. Suositus
   150 / 2950 mV (types.h).
4. **Kanavien keskiarvot päivittyvät porrastetusti.** 8 syklin liukuvat keskiarvot eri hetkinä:
   X kPa:n yhteismuotoinen muutos näkyy spreadinä X/8 (integraatiotesti rivi 5). Suositus: sama
   aikaikkuna (N·T) kaikille kanaville.
5. **WP-6:n avoimet:** manuaalinen nollaus ei vaadi vakautta; vanha tuomio näkyy noin 1 s letkun
   irrotuksen jälkeen; automaattinen nollaus sammutuksen jälkeen kestää 5,6–8 s.

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
1. **WP-2c (DSP):** korjaa pilven dsp.h:n kaksi majoria (rpm-pudotuksen lukkiutuminen ja
   blippaus) EngineSim-testien kanssa (ks. yllä). Samalla `test_integration`in punaiset:
   - rivi 9: lukitus painemuutoksen jälkeen 0,950 s > 0,367 s
   - rivi 10: saturaatiovaroitus käynnistyksessä ja sammutuksessa
   - rivi 5: väärä tuomio kaasun nostossa (osin löydös 4, porrastetut keskiarvot)
   - rivi 6: letkuvika sammutuksessa (WP-4:n minor: HoseOff ilman viivettä)

   Vertailukohdaksi käy commitoimaton worktree `.claude/worktrees/wp-2b`. Se korjasi 546d980:n
   majorit 1 ja 2 eri tavalla, ja sen oraakkelit olivat vihreinä (203/203). `saturated` =
   viimeisen 1 s:n aikana päättyneen syklin huippu ≥ sat_mv. D1-ikkuna on siirretty 0,75/fc
   aiemmaksi. Lukitusviiveet ovat 679 / 493 / 303 / 183 / 129 ms (600 / 850 / 1500 / 3000 /
   6000 rpm). Rivin 6 viive (`fault_hold_s`) on keskeneräisessä worktreessä
   `.claude/worktrees/wp-34b`.
2. **WP-7b (ESP32-liima)**, speksi docs/firmware.md §4.8–4.9:
   - Processor `proc_task.cpp`:n `7b:`-kohtiin mutexin alle (`push` per kehys, `tick` 20 Hz).
   - SSE `snap` `sbx_push`ista `push_rate_hz`:llä, puskuri `kSnapshotJsonSize`. Ei asiakkaita →
     ei snapshotia. Aloitus ensimmäisen tickin jälkeen.
   - NVS (Preferences `sbx`) rekisterin kautta sekä gainit ja `gain_valid`. Tallennus viiveellä
     `loopTask`ista. Lähteen vaihto → NVS + uudelleenkäynnistys.
   - §5:n reitit mockin muodoissa: `/` = `WEB_PAGE_HTML` raakana ilman gzip-otsaketta. CSV
     chunked: filler ei saa palauttaa 0:aa kesken, ja vapautus tehdään `onDisconnect`issa.
     `/api/sim`: `sim.params()` → `simApplyForm` → `rpm_set`.
   - Poista vanha `src/web_page.h` (sama symboli `WEB_PAGE_HTML`). `/diag` voi jäädä M4:ään.
   - ESPAsyncWebServer 3.6.0:n `AsyncEventSource.cpp`:ssä on vikoja: `_onDisconnect`in
     kilpatilanne, `avgPacketsWaiting`-jako nollalla, `close()`-itselukitus ja
     `_handleDisconnect`in iteraattori. Tarkista uudempi 3.x ennen 7b:tä.
