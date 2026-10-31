# Jatko-ohje: tila 31.10.2026 illalla

Laite toimii oikealla käyttöliittymällä ja oikeilla antureilla protolevyllä. M1/M2 (core),
WP-7a (ESP32-alusta), WP-7b (liima: Processor, §5:n reitit, NVS) ja WP-2c (DSP:n
kaasunmuutokset) ovat masterissa.

31.10.2026 toteutettiin WP-8…WP-16, katselmukset S1 ja R13 sekä yöjonosta S2, S6, S8 ja S9
([paketit.md](paketit.md)). Kaikki on yhdistetty masteriin. **Laitekoodia ei ole vielä ajettu
laitteella:** omistaja päätti 31.10.2026 yhdistää ennen laitetestiä, koska laitetta ei saatu
heti (ks. Avoimet 1). Sen tilalla ovat riippumattomat katselmukset (S1, R13) ja WSL:n
ASan/UBSan-portti jokaisen yhdistämisen jälkeen. Native 14 kokonaisuutta / 329 testiä, GCC 8.4
OK, `esp32dev_adc` kääntyy (RAM 36,5 %, flash 70,1 %), golden-toistot 4/4.

Käyttäjälle: [kayttoohje.md](kayttoohje.md) (mekaanikon ohje, S6). Moottorin tiedot lähteineen:
[bf50a-huoltotiedot.md](bf50a-huoltotiedot.md) (D16).

## Oraakkelit

```sh
cd firmware && pio test -e native                         # 14 kokonaisuutta, 329 testiä
bash tools/check_core_gcc84.sh                            # core-headerit xtensa GCC 8.4:llä
cd firmware && pio run -e esp32dev_adc -t upload          # protolevy, oikeat anturit
python tools/embed_web.py --check                         # web/index.html == src/web/web_page.h
python tools/mock_server.py --check-config
python tools/mock_server.py --check-snapshot              # mockin snapshot == laitteen (WP-10)
python tools/mock_server.py                               # UI simulaattoria vasten, localhost:8000
python tools/cycle_profile.py --url http://192.168.4.1/api/raw.csv --open   # imusyklin profiili
python tools/serial_record.py --seconds 60 --profile --open                  # USB: pitkä tallennus
python tools/sim_fit.py nauhoitus.csv                     # EngineSim-parametrit, H1, resonanssi (WP-12)
python -m unittest discover -s tools -p "test_*.py"       # Python-työkalujen testit (~2 min)
bash tools/replay/build.sh && tools/replay/build/replay x.csv [--expect x.expect]  # toisto (WP-8, WSL)
```

CI ([.github/workflows/ci.yml](../.github/workflows/ci.yml), WP-11) ajaa nämä laitteelle
latausta ja palvelimia lukuun ottamatta jokaisesta pushista haaroihin `master`, `dev/**`,
`wp/**` ja `yo/**` sekä masteriin tehdyistä PR:istä. Ensimmäinen todellinen CI-ajo tapahtuu
vasta pushin jälkeen. Yöajo ([yoajo/PROMPTI.md](yoajo/PROMPTI.md)) ei voi ajaa pio:ta, joten
sen ESP32-käännös ja GCC 8.4 -tarkistus todennetaan CI:stä.

Ensimmäinen moottoriajo: [moottoriajo.md](moottoriajo.md) (WP-12). Nauhoitusten toisto ja
golden-testit: [firmware.md](firmware.md) §6.1 (WP-8).

`tools/serial_record.py` tallentaa USB-sarjaportin kautta jokaisen 1 kHz:n näytteen niin
pitkään kuin halutaan (laite: `hal/serial_stream.*`, komento `stream <s> <baud>`, 921600 baud).
Ei vaadi Wi-Fi-yhteyttä laitteeseen. Mitattu levossa 10 s: 0 pudonnutta näytettä,
kohina 3,5 mV RMS (0,14 kPa) per näyte, satunnaisia ±80–90 mV:n piikkejä (ei kondensaattoreita).

`tools/cycle_profile.py` tekee laitteen raakadatasta (8 s, 1 kHz) HTML-raportin: syklit
päällekkäin kampiasteina, keskiprofiili ja hajonta sylintereittäin, syklikohtainen
muotopoikkeama, kierrosluku sykleittäin ja kanavien erot. Ilman moottoria (letkut T-haarassa)
raportti näyttää kanavien yhtäpitävyyden. `--lp 100` suodattaa letkun resonanssin profiileista.

Windowsilla `tools/run_native_tests.sh` (ASan/UBSan) ajetaan WSL:ssä. `pio test -e native`
toimii suoraan MSYS2:n g++:lla.

## Laitteen tila (mitattu 30.10.2026, `esp32dev_adc`)

- Näytteenotto 1000,00 Hz, ei aukkoja, pudotuksia eikä ohituksia. ADC-luku 3 kanavaa
  lämmittelyluvuilla ~253 µs / 1 ms. RAM 36 %, flash 69 %, vapaa heap ~89 kt.
- Anturit levossa (ilmanpaine): 2692–2699 mV kaikilla kolmella kanavalla.
- ADC-kohina levossa ilman pinnien kondensaattoreita: 11–18 mV huipusta huippuun 100 ms:n
  ikkunassa (~3 mV RMS ≈ 0,12 kPa RMS), Wi-Fi ilman asiakkaita. Sarjaloki näyttää tämän
  (`pp`). Mittaamatta: kohina, kun puhelin vastaanottaa SSE:tä.
- ADC:n eFuse-karakterisointi: Vref 1121 mV, raw 0 → 142 mV, raw 4095 → 3150 mV.
- Puhallus ja imu letkuun näkyvät kanavilla ilman ylikuulumista lämmittelyluvun jälkeen.

Penkkitesti 31.10.2026 (`tools/serial_record.py`, 60 s, puhelin yhdistettynä, imu suulla SYL 1:een
ja SYL 2:een yhtä aikaa, alle 20 cm:n letkut, ei vahvistuskalibrointia):
- Yhtäpitävyys tasaisessa imussa (70 × 100 ms): SYL 1 − SYL 2 = +0,012 kPa, hajonta 0,033,
  suurin 0,16 kPa. Vahvistusero −0,27 % alueella −25…−3 kPa (−55 kPa:ssa ~0,15 kPa).
  Kanavien välinen viive ~2 ms (letku tai suu), merkityksetön keskiarvolle.
- Kohina puhelimen kanssa 3,8–4,1 mV RMS (ilman 3,5–3,7), piikkejä ~1/s kuten ennenkin.
- Letkun soimista ei näkynyt: suulla päästetty imu on liian hidas herättämään sitä.
  Resonanssi selviää moottorilta (imuventtiilin sulkeutuminen on terävä).
- GN-anturi ei leikkaudu tiukasti 2,7 V:iin: ylipaine nosti SYL 1:n 2726 mV:iin (+1,2 kPa).
  Pieni positiivinen offset siis näkyy (H1 lievempi kuin oletettu); varsinainen leikkausraja
  mittaamatta.
- SYL 3 ei ollut mukana (levossa koko ajan): toistettava kolmella letkulla.

Protolevyn kytkentä poikkeaa suunnitelmasta (docs/rauta.md §2.2): SYL 3 on GPIO32:ssa,
kiskomonitoria (GPIO39) ja pinnien 100 nF:n kondensaattoreita ei ole.

## Valmiit 31.10.2026 (masterissa, laitetesti tekemättä)

| Paketti | Sisältö |
|---|---|
| WP-15 päätökset (D15) | rivin 7 rajat 150 / 2950 mV; WP-9:n kynnykset ja WP-12:n pöytäkirjan rajat alustavasti; `cond`-nimet sellaisinaan, `sat` ei peitä korttia; HX710B säilyy; kalibroimattomana ruuvineuvo säilyy |
| WP-16 BF50A (D16) | [bf50a-huoltotiedot.md](bf50a-huoltotiedot.md) lähteineen; **master-oletus SYL 3** (Hondan perus = alin kaasutin); tulpat 5×10 mm (M5), tyhjäkäynti 950 ± 50 rpm, SE-letkut kiinni, korotetut kierrokset vain testisäiliössä tai vesillä; neuvo "irrota letkut" → "sammuta moottori ja paina Nollaa"; "Napauta kahdesti" |
| S1 katselmus | WP-9/WP-10: ei korkeita löydöksiä ([katselmukset/2026-10-31-status-wp9-wp10.md](katselmukset/2026-10-31-status-wp9-wp10.md)); jatkot yöjonossa S11, S12 |
| R13 katselmus | WP-13: 28 kirjastoväitettä tarkistettu 3.12.1/3.5.0-lähteestä, ei korkeita löydöksiä; laitetestin tarkistuslista ([katselmukset/2026-10-31-wp13-hal-kirjastot.md](katselmukset/2026-10-31-wp13-hal-kirjastot.md)); jatkot S14, S15 |
| S2 DSP | voimakas paluuaalto: tasanne osui Schmittin laukaisutasolle, ja syklin raja hyppi; tasanne tunnistetaan viipymisestä ja laukaisu siirretään. base −30 / peak 40 / 900 rpm: lukitus 0/8 → 8/8, rpm-hajonta 18–23 % → 0,9 %; joutokäynti 600–1200 rpm × peak 0–60 kaikki 16/16 siementä. Aiemmat 115 mittausriviä ja WP-2c-penkki identtiset; RAM +64 t, flash +544 t. Uusi golden `sim_return_wave_h1`. Viisi `kPlateau*`-vakiota tarkistetaan moottorinauhoituksesta |
| S6 käyttöohje | [kayttoohje.md](kayttoohje.md): turvallisuus, pikaohje, tilat täsmälleen laitteen teksteinä, vianetsintä; 5 kuvaa |
| S8 fuzz | `test_fuzz`: 44 000 determinististä tapausta lomakkeille ja nauhoitus-CSV:lle (ASan), ei löydöksiä |
| S9 config-JSON | `kConfigJsonSize` johdetaan kenttälistasta käännösaikana (5913 t, pahin 5476 t), katto 8192 (`static_assert`) |
| WP-8 toisto | `tools/replay` (nauhoitus saman Processorin läpi, JSONL, yhteenveto, `--sim`-vienti EngineSimistä), `test_replay` + `firmware/test/data/recordings/` (3 synteettistä golden-nauhoitusta), firmware.md §6.1 |
| WP-9 status ja kalibrointi | rivi 6: letkuvian poistumishystereesi `hose_hyst_kpa` 0,2 (välkyntä 701 → 22 vaihtoa / 8 × 60 s); rivi 5: rpm suodatetaan `drift_tau_s` 0,5 s (väärät liput 3000 rpm / 3 % jitter 24,6 % → 1,7 %); nollaus vaatii tason vakauden `zero_settle_kpa` 0,1 / 1 s (`NotSettled`); automaattinen nollaus sammutuksesta 6,35–8,5 s → 4,1 s; WP-4:n kolme selviävää mutaatiota tapettu (69 mutanttia) |
| WP-10 act/cond | snapshotiin `act {ch, open}` ja `ch[i].cond` (`ok`, `sat`, `idle`, `no_pulse`, `hose`, `blocked`, `sensor`); UI ei jäsennä advice-tekstiä; mock mallintaa WP-2c:n pitoajat ja yhteismuotoisen muutoksen; `mock_server.py --check-snapshot` |
| WP-11 CI | GitHub Actions: native (ASan/UBSan), `pio test -e native`, ESP32-käännös kolmelle ympäristölle, GCC 8.4, web- ja Python-tarkistukset; `tools/mitoitus.py`:n syntaksivirhe korjattu |
| WP-12 kenttäpaketti | [moottoriajo.md](moottoriajo.md) (ajopöytäkirja), `tools/sim_fit.py` (EngineSim-parametrit nauhoituksesta, lepotarkistus), 18 testiä; ristiintodennettu C++-EngineSimiä vastaan |
| WP-13 HAL | `myoh` ei laske setupin aikaisia myöhästymisiä (`late_boot`, sarjaloki `kaynn`); ESPAsyncWebServer 3.6.0 → 3.12.1 ja AsyncTCP 3.3.2 → 3.5.0 (kolme neljästä ongelmasta korjattu kirjastossa; `_onDisconnect`in kilpatilanne yhä auki, joten `sbx_push`-järjestely jää) |
| yöajo | [yoajo/](yoajo/): PROMPTI, JONO (S1–S9), LOKI; `tools/gh_api.py` (Zignalmeisterista) |

## Valmiit 30.10.2026

| Paketti | Sisältö |
|---|---|
| WP-7b liima | Processor `proc_task`issa mutexin takana (push 1 kHz, tick 20 Hz), §5:n reitit, SSE `snap` 15 Hz, `/diag`, NVS (`hal/nvs_store.*`), lähteen vaihto → uudelleenkäynnistys |
| ADC protolevy | pinnit 34/35/32, ei kiskoa, lämmittelyluku (S&H-ylikuuluminen) |
| WP-2c DSP | lukituksen purku rpm-pudotuksessa, uudelleenreferointi kaasun lisäyksessä, amplitudi kutistuvalle pulssille, trendin mukainen vahvistus, hiljaisuudesta herääminen |
| status | letkuvian pitoaika 0,5 s, saturaatiovaroituksen pitoaika 1,2 s, yhteismuotoinen alipaineen muutos → "odota" |

DSP EngineSim-penkissä (16 siementä, ennen → nyt): rpm-pudotus 3000 → 850 putosi 16/16,
1,2 s → 0/16, 0,5 s; blippaus 850 → 2500 aukko 0,9 s → 0,55 s; käynnistys 6000 rpm:iin
947 → 147 ms. Hinta: tyhjäkäynnille käynnistyminen lukittuu 414 ms:n sijaan 508 ms:ssa
(ensimmäinen pulssi jää uuden asettumisen alle), DSP:n määrittelyn sisällä.

## Avoimet (prioriteettijärjestyksessä)

1. **Laitetesti mahdollisimman pian (master on jo ohittanut sen).** Masterissa on laitekoodia
   WP-9:ltä (status, nollaus), WP-10:ltä (snapshot, UI), WP-13:lta (kirjastot 3.12.1/3.5.0,
   `late_boot`), WP-15/WP-16:lta (oletukset, neuvoteksti) ja S2:lta (DSP). Testi: "Laitetesti 1"
   alla + UI puhelimessa (AVAA/SULJE, vikapeitteet, huoltosivun kolme uutta kenttää) + nollaus
   sammutuksen jälkeen (viesti "Paine ei ole vakaa" ennen asettumista, sitten "Automaattinen
   nollaus tehty"). Löydökset korjataan masterin päälle.
2. **Oikea moottori (M6)** pöytäkirjan [moottoriajo.md](moottoriajo.md) mukaan. Kaikki DSP-luvut
   on mitattu simulaattorilla; nauhoitukset → `sim_fit.py` ja golden-testit (firmware.md §6.1).
   Huoltotiedot on lähteistetty ([bf50a-huoltotiedot.md](bf50a-huoltotiedot.md), D16). Moottorilta
   tarkistettava: tulpan kierteen nousu (oletus M5×0,8) ja tiiviste, alin kaasutin = No. 3 ilman
   synkronointiruuvia, ruuvin kiertosuunta (1/8 kierrosta ja katso mittarista), SE-letkujen
   puristuskohdat.
3. **Rauta**, ks. "Raudan arvio" alla ja docs/rauta.md. Letkut tehdään mahdollisimman lyhyiksi
   (profiilitutkimus, rauta.md §4.1:n päivitys): alle ~20 cm:n letkuilla 100 nF on tarpeen
   anti-alias-suodattimena. Penkkitesti toistettava kolmella letkulla (SYL 3 ei ollut mukana).
4. **H1/R4, väärä vihreä.** GN-anturi leikkaa ilmanpaineessa (2700 mV). Positiivinen offset
   jää näkymättä, eikä automaattinen nollaus poista sitä (WP-12 lukitsi tämän testiin).
   Vahvistuskalibrointi noin −50 kPa:ssa T-haarassa kompensoi; tai GPN-anturi.
5. **Omistajan päätettävät:** 31.10.2026 avoimet päätettiin (D15, D16; omistaja delegoi).
   Uusia avoimia: §8:n "mitä UI näyttää" -sarakkeen erot koodiin ja rivin 11 "anturi
   vaihdettu", jota koodi ei tunnista (yöpaketti S13 tuottaa vaihtoehdot).
6. **DSP, ristivuoto ja paluuaalto yhdessä (S2:n havainto):** `xtalk` 0,3 ja `peak` ≥ 20
   1500 rpm:ssä antaa rpm:n 2,5–2,8-kertaisena (cv-ehto silti läpi), ja base −30 + xtalk 0,3
   + peak 60 600–900 rpm:ssä ei anna rpm:ää lainkaan. Vika on vanha (S2 ei muuta sitä).
   Tasauskanavista ei ole tietoa (D6, SE-letkut puristetaan kiinni, D16), joten ensimmäinen
   moottorinauhoitus ratkaisee, onko tämä todellinen. Yöpaketti S16. S2 korjasi pelkän
   paluuaallon (Valmiit-taulukko).
7. **ESPAsyncWebServer:** `_onDisconnect`in kilpatilanne on auki myös kirjaston mainissa
   (26.10.2026). Aiempi kahden rivin ehdotus (`_lockmq` `_onDisconnect`issa) on R13:n mukaan
   vajaa: lukko on vapautettava ennen `_handleDisconnect`ia, ja `connected()` lukee `_client`in
   yhä ilman lukkoa. Yöpaketti S15 kirjoittaa luonnoksen; lähettäminen kirjaston tekijöille on
   omistajan päätös. Siihen asti `sbx_push` lähettää yksin ja `close()` on kielletty.
8. **SSE-snapien purskeet (R13 L1):** diag lähetetään kaikille juuri ennen snapia, joten
   `SSE_MAX_QUEUED_MESSAGES=8`:lla seuraava snap odottaa diagin kuittausta. Jos asiakas
   viivästää kuittausta yli 66 ms (Windows, mahdollisesti iOS), snapit tulevat purskeina.
   Mitataan laitetestissä (kohta 7 alla); jos purskeita näkyy, diag omaan EventSourceensa tai
   jonon raja 12–16.
9. **UI oikeilla selaimilla:** iOS Safari ja Android Chrome testaamatta.

### Laitetesti 1 (WP-13, `esp32dev_adc`)

Flashaa `--upload-port` VID:PID:n mukaan, sarjamonitori auki (115200). Kirjaa ennen ja jälkeen
jokaisen vaiheen: `myoh`, `kaynn`, `aukot`, `pudot`, `ohit`, `heap`, `min`, `sse`, `tx`, `osit`,
`hyl`, `push max`. Vertailuksi vaiheet 3–5 voi ajaa ensin kirjastoversiolla 3.6.0 (commit
`2c63e86`, master ennen 31.10.2026:n yhdistämistä).

1. Käynnistys kolmesti: ensimmäisellä DIAG-rivillä `myoh=0`, `kaynn` 1–5.
2. 60 s ilman asiakkaita: heapin lähtötaso.
3. Puhelin SSE:llä 10 min: aukot, pudotukset, ohitukset ja `myoh` +0; `tx` ~150 / 10 s;
   `hyl` ja `osit` +0; heap lähtötasossa ±2 kt; `min` ≥ 40 kt; UI ilman katkoja.
4. `/api/raw.csv` SSE:n aikana kolmesti (`curl -o raw.csv -w "%{time_total} %{size_download}
"
   http://192.168.4.1/api/raw.csv`), yksi keskeytettynä ~1 s:n kohdalla: CSV täysi,
   latausaika ≤ 1,5 × masterin, SSE ei katkea, seuraava lataus antaa tuoretta dataa, aukot +0.
5. Katkaisut: `for i in $(seq 200); do curl -sN --max-time 1 http://192.168.4.1/events >/dev/null; done`,
   50 kierrosta `--max-time 0.3`:lla, puhelimen Wi-Fi pois/päälle 20 kertaa: ei
   uudelleenkäynnistystä, `sse` palaa todelliseen 5 s:ssa, heap ±2 kt 10 s:n kuluttua.
6. Kaksi asiakasta 10 min (puhelin pääsivulla, PC `/diag`issa): `sse=2`, `osit` < 1 % `tx`:stä,
   molemmat päivittyvät, toisen sulkemisen jälkeen `sse=1`.
7. Snapien väli selaimen DevToolsin EventStream-näkymästä Androidilla, iPhonella ja
   Windows-PC:llä: odotus ~66 ms, purskeet viittaavat R13:n löydökseen L1 (Avoimet 8).
8. `GET /api/config` 20 kertaa SSE:n aikana (S9): heap ±2 kt, `[PINO] async_tcp` ei laske.

Lisäksi ennen vaiheita (R13:n tarkistuslista, katselmuksen loppu):
- Käynnistyslokissa `async_tcp=c0/p10` ja `sbx_push=c0/p11`; väärä ydin tai prioriteetti kumoaa
  web.cpp:n turvaperustelun.
- `[INFO] asetukset: NVS|oletukset`: jos NVS, laite pitää vanhat oletukset (rivi 7 100/3150,
  master SYL 1). Paina huoltosivulla **Palauta oletukset** ja tarkista, että master on SYL 3
  ja `fault_lo_mv`/`fault_hi_mv` 150/2950 (D15, D16).
- `osit` ja `hyl` eivät ole suoraan vertailukelpoisia masterin 3.6.0-lukujen kanssa (R13 L7).
- Neuvo kalibroimattomana: "Nollaus puuttuu – sammuta moottori ja paina Nollaa" (WP-16).

## Raudan arvio (30.10.2026, protolevyn mittausten perusteella)

Ennen moottoria (halpoja, isoin hyöty):
1. **100 nF jokaiselta ADC-pinniltä GND:hen** (C0G/NP0, pinnin viereen, rauta.md §3.1) ja
   tarkista, että 10 kΩ:n sarjavastus on jokaisella kanavalla. Kondensaattori on sekä
   anti-alias-suodatin (1 kHz:n näytteistys ilman sitä taittaa Wi-Fin ja hakkurin yli 500 Hz:n
   häiriöt hyötykaistalle) että S&H:n varaussäiliö. Ylikuuluminen on nyt kierretty
   lämmittelyluvulla, mutta aliasointia ohjelmisto ei voi korjata jälkikäteen.
2. **Juotetut tai lukittuvat liitokset** (JST tms.) antureille. SYL 3 näytti 0–300 mV, kunnes
   johto kytkettiin uudelleen: perämoottorin tärinässä hyppylankaliitos ei kestä.
3. **Anturien oma 3V3_SENS-LDO** (MCP1700-3302E, R6), jos anturit ovat nyt devkitin 3V3:ssa.
   50 mV:n notkahdus = 2 kPa absoluuttivirhe; yhteismuotoinen osa kumoutuu erotuksessa, mutta
   anturikohtaiset erot eivät. Mittaa kohina (`pp` sarjalokissa), kun puhelin on yhdistetty ja
   SSE käy: nyt mitattu vain ilman asiakkaita.
4. **T-haara ja käsikäyttöinen alipainepumppu** vahvistuskalibrointiin noin −50 kPa:ssa (R4).
   Se kompensoi myös H1:n (GN-anturin leikkautuvan positiivisen offsetin) joutokäynnin
   tasolla.
5. **Polttoaine- ja öljysumuloukku letkuihin** (rauta.md §4.4) ja identtiset letkut (R1).

PCB-vaiheessa:
- Kiskomonitori GPIO39:ään (10 k / 10 k) ja firmwaren `kAdcHasRail = true`.
- SYL 3: joko suunnitelman GPIO36 (palauta `kAdcCh` sampler.cpp:ssä) tai pidä GPIO32 ja
  päivitä rauta.md:n taulukko. Molemmat ovat ADC1:ssä; GPIO36/39:n erratum 3.11 on hoidettu
  (`adc_power_acquire`).
- H2 (MCP3208): ei tarvetta nyt näkyvissä. Raakakohina ~0,12 kPa RMS per näyte, ja syklin
  keskiarvo (~140 näytettä × 8 sykliä) vie sen kauas alle D2:n 1,5 kPa:n. ADC:n
  epälineaarisuus on kaikille kanaville yhteinen (sama SAR), joten se kumoutuu erotuksessa.
- H1 (GN vs. GPN): levossa 2692–2699 mV eli juuri leikkausrajan alla. Päätös ensimmäisestä
  moottorinauhoituksesta: jos pulssien huiput leikkautuvat 2,7 V:iin tyhjäkäynnillä, GPN.
