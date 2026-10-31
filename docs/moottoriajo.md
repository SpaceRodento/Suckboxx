# Ensimmäinen moottoriajo: ajopöytäkirja (Honda BF50A, M6)

Tämä on veneelle tulostettava tai puhelimessa pidettävä ohje roadmapin vaiheeseen M6
(SUUNNITTELU.md §11). Ajolla on kaksi tavoitetta (SUUNNITTELU.md §7, vaihe 3):

1. toimiiko mittari oikealla moottorilla, ja
2. **raakadata talteen.** Nauhoitukset ovat ajon tärkein tuotos. Ilman niitä ajokerrasta ei
   jää mitään, mitä voi analysoida jälkikäteen (S5b, R3). Kaikki DSP:n luvut on tähän asti
   mitattu simulaattorilla (jatko.md, Avoimet 1).

Ajopäivä: ________ (esimerkeissä 7.11.2026; vaihda päivämäärä tiedostonimiin).
Tekijät: ________ Paikka (traileri / vesillä): ________ Ilman lämpötila: ______ °C

Merkinnät: ☐ = tarkistettava kohta. **Varmistettava huolto-ohjeesta** = arvo, jota repossa ei
ole. Sitä ei arvata, vaan katsotaan Hondan huolto-ohjeesta ennen ajoa.

---

## 1. Ennen ajoa: rauta (kotona)

Jatko.md:n "Raudan arvio" kohdat 1–5 sekä adapterit. Halpoja, ja ilman niitä data on huonompaa.

- ☐ **100 nF jokaiselta ADC-pinniltä GND:hen** (C0G/NP0, pinnin viereen, rauta.md §3.1), ja
  10 kΩ:n sarjavastus jokaisella kanavalla. Kondensaattori on anti-alias-suodatin: alle ~20 cm:n
  letkuilla letkun soiminen menee yli 500 Hz:n ja taittuu ilman sitä hyötykaistalle
  (rauta.md §4.1, päivitys 30.10.2026). Aliasointia ohjelmisto ei korjaa jälkikäteen (R2).
- ☐ **Juotetut tai lukittuvat liitokset** (JST tms.) antureille. Protolevyllä SYL 3 näytti
  0–300 mV, kunnes johto kytkettiin uudelleen (jatko.md). Perämoottorin tärinässä
  hyppylankaliitos ei kestä.
- ☐ **Anturien oma 3V3_SENS-LDO** (MCP1700-3302E, R6), jos anturit ovat vielä devkitin 3V3:ssa.
  Mittaa LDO:n lähtö 3,3 V ennen antureita (rauta.md §6.6, tarkistuslista).
- ☐ **T-haara ja käsikäyttöinen alipainepumppu** vahvistuskalibrointiin noin −50 kPa:ssa (R4,
  rauta.md §7). Se kompensoi myös H1:n: GN-anturin positiivinen offset ei näy lepotasossa,
  eikä nollaus poista sitä (jatko.md, Avoimet 3).
- ☐ **Polttoaine- ja öljysumuloukku** (rauta.md §4.4): laite liitäntöjä ylemmäs, letkuun alaspäin
  osoittava lenkki ennen laatikkoa, tarvittaessa suodatinvilla lyhyessä T-haarassa.
- ☐ **Identtiset lyhyet letkut** (R1): sama sisähalkaisija, sama pituus ±2 cm, sama materiaali,
  leikattu samasta rullasta samalla kertaa. Mukaan lukien kotelon sisäletkut (rauta.md §4.2).
  Kirjaa letkujen pituus: ______ cm (tarvitaan resonanssin päätöspisteessä, kohta 6.2).
- ☐ **Letkunipat synkronointitulppien tilalle**, kolme kappaletta, ja kuparialuslevyt. Kierre on
  todennäköisesti M6, mutta **varmistettava** (D6, hankinnat.md §4): irrota yksi tulppa ja
  kokeile M6×1,0- ja M5×0,8-muttereita ennen kuin tilaat.
- ☐ Kotelo ei ole ilmatiivis (R5): anturin kyljen tasausreikä on nollareferenssi (rauta.md §4.3).
- ☐ Letkut merkitty SYL 1 / SYL 2 / SYL 3. Mikä sylinteri on SYL 1 ja mikä kaasutin on kiinteä
  perus (master, D7): **varmistettava huolto-ohjeesta** (D6). Oletus SYL 1 = master säilyy, kunnes
  se on tarkistettu.
- ☐ Firmware `esp32dev_adc` flashattu (jatko.md, oraakkelit), huoltosivulla lähde ADC.

**Kotona ennen lähtöä (penkkitesti kolmella letkulla).** Penkkitestissä 31.10.2026 SYL 3 ei
ollut mukana (jatko.md). Kytke kaikki kolme letkua T-haaraan, imaise tai pumppaa alipainetta ja
tallenna:

```sh
mkdir -p data/raw/2026-11-07
python tools/serial_record.py --seconds 60 --out data/raw/2026-11-07/2026-11-07_penkki_kolme_letkua.csv --profile --open
```

Raportin "Kanavien erot ajan yli" -taulukossa erojen pitäisi olla lähellä nollaa (jatko.md:
SYL 1 − SYL 2 +0,01 kPa, hajonta 0,03 kPa).

**Kannettava:** Python 3.10 ja `pip install pyserial numpy scipy`. Toistotyökalu käännetään
kotona valmiiksi (Git Bash): `SAN=0 bash tools/replay/build.sh` → `tools/replay/build/replay`
(docs/firmware.md §6.1).

## 2. Turvallisuus

- ☐ **Jäähdytysvesi.** Moottoria ei käytetä kuivana sekuntiakaan. Trailerilla tai telineessä
  huuhtelukupit (vesiletku) paikalleen ja vesi auki ennen käynnistystä, tai moottori vedessä.
  Tarkista heti käynnistyksen jälkeen, että jäähdytysveden tarkkailusuihku virtaa. Saako
  huuhtelukupeilla käyttää korotettuja kierroksia (kohdan 4 vaiheet 6–8) ja kuinka kauan:
  **varmistettava huolto-ohjeesta**. Jos ohje kieltää, tee tasaiset kierrokset vesillä.
- ☐ **Vapaa-asento** ennen käynnistystä ja koko ajan. Trailerilla potkuri on vapaana, eikä
  sen lähellä saa olla ketään. Vesillä vene kiinni laiturissa.
- ☐ **Sammutusnaru** (hätäkatkaisin) kiinni käyttäjään ja kokeiltu ennen ajoa.
- ☐ **Johdot ja letkut pois liikkuvista osista.** Synkronointi tehdään kansi auki, jolloin
  vauhtipyörä ja muut pyörivät osat ovat paljaina. Reititä anturiletkut, USB-kaapeli ja
  laitteen johdot nippusiteillä niin, etteivät ne voi osua vauhtipyörään, vipuihin tai
  pakosarjan kuumiin osiin. Ei löysiä vaatteita, hihoja tai hiuksia. Kuulonsuojaimet.
- ☐ **Tuuletus.** Vain ulkona, ei suljetussa tilassa (pakokaasu).
- ☐ **Polttoaine.** Ei avotulta, ei tupakointia. Sammutin lähellä. Kannettava ja virtapankki
  irti polttoainesäiliöstä ja -letkuista. Tulppia ja nippoja vaihdetaan vain moottori
  sammuksissa.
- ☐ Synkronointiruuvien alkuasento **merkitty maalikynällä** ennen kuin yhtään ruuvia käännetään,
  jotta alkuperäinen säätö voidaan palauttaa.
- Keskeytä, jos tulppa ei irtoa, kierre ei täsmää, letku vuotaa (joutokäynti muuttuu
  letkua kosketettaessa) tai jäähdytysvesi ei kierrä.

## 3. Kytkentä ja käynnistys

Järjestys on tärkeä: nollaus (D11) tehdään aina ennen vahvistuskalibrointia, ja nollaus
katoaa laitteen uudelleenkäynnistyksessä (huoltosivun ohje).

1. ☐ Moottori sammuksissa ja kylmä. Kansi auki.
2. ☐ Laite paikalleen liitäntöjä ylemmäs (rauta.md §4.4), kotelon letkuliittimet ottavat vedon
   (§4.2).
3. ☐ **Virta ja USB.** Laite kannettavan USB:hen. Sama kaapeli antaa virran ja sarjavirran
   (`serial_record.py`, ei vaadi Wi-Fiä). Ilman kannettavaa virta USB-virtapankista (D13).
   Jos koneeseen on kytketty muitakin ESP32-laitteita, anna portti itse:
   `python -m serial.tools.list_ports -v` ja `--port COMx`.
4. ☐ **Puhelin** Wi-Fi-verkkoon `Suckboxx` (salasana `alipaine123`, `firmware/src/hal/hal_config.h`)
   ja selaimella `http://192.168.4.1/`. Huoltosivulla (`#huolto`) lähde ADC. Diagnostiikasta
   aukot 0.
5. ☐ Yhteystesti kannettavalta: `mkdir -p data/raw/2026-11-07` ja 5 s:n tallenne (kohta 4,
   vaihe 0). Sitten lepo ennen nollausta, letkut ilmassa (vaihe 1a): anturien omat offsetit
   talteen.
6. ☐ **Nollaus 1 (D11):** letkut irti tai T-haarassa ilmanpaineessa → huoltosivu → **Nollaa**.
   Pilleri näyttää "Nollaus: tehty ✓".
7. ☐ **Vahvistuskalibrointi (jos T-haara on mukana):** kolme letkua T-haaraan, käsipumpulla
   40–60 kPa alipaine, odota lukeman asettumista, **Kalibroi vahvistus** ja pidä paine tasaisena
   2 s (huoltosivun ohje; hyväksymisrajat calibration.h: |paine| ≥ 15 kPa, hajonta < 0,5 kPa,
   kanavat 15 %:n sisällä toisistaan). Tallenna samalla (kohta 4, vaihe 2).
8. ☐ Tulpat irti, nipat ja kuparialuslevyt tilalle, letkut kiinni oikeisiin sylintereihin.
9. ☐ **Nollaus 2 (D11) moottorilla:** moottori seis, letkut kiinni. Sammuneen moottorin imusarja on
   ilmanpaineessa (huoltosivun ohje), joten **Nollaa** uudelleen. Laite nollaa myös itse, kun
   moottori on ollut sammuksissa muutaman sekunnin (rivi 1). Tallenna lepo (kohta 4, vaihe 1).
10. ☐ Jäähdytysvesi päälle, vapaa-asento, sammutusnaru kiinni. Aloita vaiheen 3 tallenne
    **ennen** käynnistystä.

## 4. Tallennusjärjestys

Kaikki komennot ajetaan repon juuressa Git Bashissa (`python …`-rivit toimivat sellaisinaan
myös PowerShellissä; `awk`, `tr` ja `$(…)` vain Bashissa). Tiedostot menevät kansioon
`data/raw/2026-11-07/`, joka on `.gitignore`ssa: **se ei siirry gitin mukana**, joten kopioi
kansio ajon jälkeen myös muualle (kohta 7.1). Nimet: `<ISO-päivä>_<vaihe>[_n].csv`.

`serial_record.py` tallentaa jokaisen 1 kHz:n näytteen enintään 3600 s (laitteen raja), ja
laite palvelee puhelinta tallennuksen aikana normaalisti (vain sarjaloki on tauolla). Komennon
voi aloittaa uudelleen heti edellisen jälkeen. Varalla: puhelimen huoltosivun **Tallenna
raakadata (CSV)** antaa viimeiset ~8 s (`/api/raw.csv`). Jos kannettava ei ole mukana, paina sitä
jokaisen vakaan vaiheen lopussa.

| # | vaihe | kesto | komento (`python tools/serial_record.py` …) | tiedosto | katso UI:sta | kirjaa käsin |
|---|---|---|---|---|---|---|
| 0 | yhteystesti | 5 s | `--seconds 5 --out data/raw/2026-11-07/2026-11-07_testi.csv` | `_testi.csv` | – | portti, näytteitä 5000, puuttuvia 0 |
| 1a | lepo ennen nollausta, letkut ilmassa | 30 s | `--seconds 30 --out data/raw/2026-11-07/2026-11-07_lepo_ennen_nollausta.csv` | `_lepo_ennen_nollausta.csv` | tila "Ei alipainesignaalia", mV-lukemat | kellonaika, lämpötila |
| 1b | lepo nollattuna, letkut moottorissa | 30 s | `--seconds 30 --out data/raw/2026-11-07/2026-11-07_lepo.csv` | `_lepo.csv` | "Nollaus: tehty ✓", Δ ≈ 0 | nollattu klo ____ |
| 2 | vahvistuskalibrointi T-haarassa (jos mukana) | 60 s | `--seconds 60 --out data/raw/2026-11-07/2026-11-07_vahvistus_t_haara.csv` | `_vahvistus_t_haara.csv` | kalibroinnin viesti, kanavaerot ennen ja jälkeen | paine kPa, onnistuiko |
| 3 | kylmäkäynnistys ja lämpeneminen (aloita ennen käynnistystä) | 600 s | `--seconds 600 --out data/raw/2026-11-07/2026-11-07_kylmakaynnistys_lampeneminen.csv` | `_kylmakaynnistys_lampeneminen.csv` | tila (ei signaalia → tasaantuu → tuomio), rpm, ensimmäinen värituomio | käynnistys klo, rikastin pois klo (jos käsikäyttöinen), milloin joutokäynti tasainen, sammumiset |
| 4 | lämmin joutokäynti 1 (ruuveihin ei kosketa) | 60 s | `--seconds 60 --out data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv` | `_joutokaynti_lammin_1.csv` | tila ja väri, Δ2, Δ3, rpm ± hajonta, kanavan "pulssi · leikkautuu" (rivi 10) | Δ2, Δ3 ja rpm 30 s:n kohdalla |
| 5 | lämmin joutokäynti 2 (n. 2 min myöhemmin) | 60 s | `--seconds 60 --out data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_2.csv` | `_joutokaynti_lammin_2.csv` | sama kuin 4 | sama kuin 4 (toistettavuus) |
| 6 | kaasunblippaukset ~2500 rpm:iin, 5 kpl 10 s:n välein | 60 s | `--seconds 60 --out data/raw/2026-11-07/2026-11-07_blippaukset.csv` | `_blippaukset.csv` | "Odota, käynti tasaantuu" ja paluu tuomioon, rpm:n seuranta | blippausten kellonajat |
| 7 | tasainen 1500 rpm | 30 s | `--seconds 30 --out data/raw/2026-11-07/2026-11-07_tasainen_1500.csv` | `_tasainen_1500.csv` | tila, Δ:t, rpm | kierrosluku (myös erillinen kierroslukumittari, jos on) |
| 8a | tasainen 2000 rpm | 30 s | `--seconds 30 --out data/raw/2026-11-07/2026-11-07_tasainen_2000.csv` | `_tasainen_2000.csv` | sama | sama |
| 8b | tasainen 3000 rpm (rivi 9, D12:n yläraja) | 30 s | `--seconds 30 --out data/raw/2026-11-07/2026-11-07_tasainen_3000.csv` | `_tasainen_3000.csv` | "Säädä joutokäynnillä" (rivi 9) vai tuomio; vilkkuuko rajalla | sama |
| 9 | sammutus (aloita joutokäynnillä, sammuta ~10 s kohdalla) | 60 s | `--seconds 60 --out data/raw/2026-11-07/2026-11-07_sammutus.csv` | `_sammutus.csv` | "Ei alipainesignaalia", huoltosivun viesti "Automaattinen nollaus tehty (moottori sammuksissa)" | sammutus klo, sekuntikellolla aika viestiin (simulaattorissa WP-9:n jälkeen ~4,1 s; ennen 5,6–8 s) |
| 10 | synkronointiyritys (uudelleenkäynnistys, lämmin joutokäynti) | 300 s | `--seconds 300 --out data/raw/2026-11-07/2026-11-07_synkronointi_1.csv` | `_synkronointi_1.csv` | säätöohje (mikä SYL, avaa/sulje), Δ:t, väri | säätöloki alla |
| 11 | valinnainen: letku irti (rivi 6), SYL 3:n letku irti **laitteen päästä** 10 s ja takaisin | 40 s | `--seconds 40 --out data/raw/2026-11-07/2026-11-07_letku_irti_syl3.csv` | `_letku_irti_syl3.csv` | "Kanava 3: ei alipainetta – tarkista letku" | irrotus ja kiinnitys klo |

Vaiheet 7–8 vain, jos kohdan 2 jäähdytysveden ehto sallii (muuten vesillä). Vaiheessa 11
imusarjaan pääsee irrotetun letkun kautta hetkeksi vuotoilmaa, joten joutokäynti voi muuttua;
jätä väliin, jos käynti on muutenkin epävakaa.

**Synkronoinnin säätöloki (vaihe 10).** Säädä vain kaasutinta, jonka laite nimeää, ja vain jos
master (kiinteä perus) on tarkistettu huolto-ohjeesta (D7). Pieni askel kerrallaan (esim. 1/8
kierrosta), odota 5 s, kirjaa. Lopuksi ruuvit joko uudessa asennossa tai palautettu
maalimerkkeihin.

| klo | kaasutin (SYL) | käännös (suunta, määrä) | Δ2 kPa | Δ3 kPa | väri | huom |
|---|---|---|---|---|---|---|
| | | | | | | |
| | | | | | | |
| | | | | | | |

**Yleisloki.**

| klo | vaihe # | tiedosto valmis (puuttuvia näytteitä) | rpm UI | tila / väri | huom |
|---|---|---|---|---|---|
| | | | | | |
| | | | | | |

`serial_record.py` tulostaa lopuksi "N näytettä, puuttuvia M". Puuttuvia pitäisi olla 0.

**Pikatarkistus veneellä** heti vaiheiden 1b ja 4 jälkeen (kestää ~10 s):

```sh
python tools/sim_fit.py data/raw/2026-11-07/2026-11-07_lepo.csv
python tools/sim_fit.py data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv
```

Lepo: offsetit ±2 kPa:n sisällä (R4), kohina samaa luokkaa kuin penkillä (3,5–4,1 mV RMS,
jatko.md), ei vikakanavaa. Joutokäynti: kaikilla kanavilla hyväksyttyjä syklejä, rpm
järkevä (tyhjäkäynti 850–950 rpm D6:n mukaan, **varmistettava huolto-ohjeesta**).

## 5. Mitä työkalut kertovat

- `tools/sim_fit.py <csv>`: EngineSimin parametrit `/api/sim`-lomakkeena (`rpm, jitter, base,
  err1..3, depth, res, xtalk, peak, noise`), kanavakohtaiset luvut (taso, huippu–huippu,
  mallin syvyys ja paluuaalto, ristivuoto +240°/+480°, resonanssi Hz ja dB, kohina), H1-rivit,
  kanavaerot (Δ = master − kanava, 8 syklin keskiarvo kuten laite) ja huomiot. Parametri, jota ei
  voi arvioida luotettavasti, on "Pois jätetyt" -listassa syineen. `--json` koko tulos,
  `--form` pelkkä lomake, `--start S --end S` ikkuna, `--zero-from lepo.csv` nollaus
  jälkikäteen, `--res-band 50 490` laajempi resonanssihaku, `--cal nominal` ilman otsakkeen
  kalibrointia. Lepotallenteesta (ei pulssia) offsetit mV/kPa, kohina, 100 ms:n huipusta
  huippuun ja kanavaerot.
- `tools/cycle_profile.py <csv> --open`: syklit päällekkäin kampiasteina, keskiprofiilit,
  poikkeavat syklit, rpm sykleittäin ja kanavaerot ajan yli. `--lp 100` suodattaa letkun soimisen
  profiileista.
- `tools/replay/build/replay <csv>`: sama `Processor` kuin laitteella. Aika tiloittain,
  ensimmäinen lukitus, rpm ± hajonta, spreadin mediaani ja p95 tuomiotikeistä (docs/firmware.md
  §6.1). `--from lock` tilastot ensimmäisestä tuomiosta, `--set avain=arvo` viritys (S5a).

## 6. Päätöspisteet

Päätökset kirjataan päätöslokiin (SUUNNITTELU.md §5). Kohtien 6.1, 6.3 ja 6.4 rajat on
hyväksytty alustavasti 31.10.2026 (D15), ja ne tarkistetaan ensimmäisestä ajosta. Muut uudet
kynnysarvot ovat ehdotuksia, kunnes omistaja on hyväksynyt ne.

### 6.1 H1: GN vai GPN

*Kysymys:* leikkautuvatko pulssien huiput 2,7 V:iin tyhjäkäynnillä (jatko.md, Raudan arvio;
rauta.md §1.3 ja §8; D4)?

*Data:* vaiheet 4 ja 5 (lämmin joutokäynti). Vertailuksi 6–8, mutta niiden leikkautuminen ei
ratkaise H1:tä, koska synkronointi tehdään joutokäynnillä (D4:n perustelu).

*Työkalu:* `python tools/sim_fit.py data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv`,
rivit `H1: tulkinta`, `≥ sat_mv näytteistä`, `saturoituvat syklit`, `syklin huippu mV`,
`litteä huippu ms`, `leikkautuu yli kPa` ja `saturaation vaihe °`. Kuva:
`python tools/cycle_profile.py <sama> --open`, jossa leikkautuminen näkyy syklien litteänä
yläreunana.

*Tulkinta (rajat hyväksytty alustavasti 31.10.2026; tarkistetaan ensimmäisestä ajosta):*
- `ei`: huiput jäävät alle sat_mv:n (2680 mV, types.h). GN riittää.
- `koskettaa`: huiput käyvät sat_mv:n yli mutta eivät litisty, eli positiivinen paine näkyy.
  GN riittää. Penkkitesti 31.10.2026 viittaa tähän: ylipaine näkyi 2726 mV:iin asti
  (jatko.md).
- `leikkautuu`: huipussa on litteä jakso (≥ 3 ms) tai reunoista ennustettu huippu jatkuu katon
  yli. Jos näin käy **tyhjäkäynnillä**, ehdotus on GPN (jatko.md: "jos pulssien huiput
  leikkautuvat 2,7 V:iin tyhjäkäynnillä, GPN"). Silloin keskiarvo (D1) vinoutuu.

Huom: lepotasot ovat GN-anturilla aina ≥ sat_mv (2692–2699 mV), eikä se ole saturaatiota
(status.h laskee saturaation vain pulssin kanssa).

### 6.2 Letkuresonanssi ja anti-alias-suodatus

*Kysymys:* missä letkut soivat, ovatko kanavat samanlaiset (R1) ja riittääkö 10 kΩ + 100 nF
(R2, rauta.md §3.1 ja §4.1)?

*Data:* vaiheet 4 ja 5 (tyhjäkäynnillä sykli on pitkä, ja soiminen ehtii vaimeta).

*Työkalu:* `sim_fit.py`, rivit `resonanssi Hz`, `piikki dB kaistan mediaanista`, `heräte kPa`,
`vaimeneminen ms` ja huomio "Letkuresonanssit eroavat kanavien välillä yli 5 %". Jos piikkiä ei
löydy 50–300 Hz:ltä ("Pois jätetyt: res"), lyhyt letku soi todennäköisesti korkeammalla. Hae
laajemmin: `python tools/sim_fit.py <csv> --res-band 50 490` (synteettisellä datalla 380 Hz:n
soiminen löytyy näin 3 %:n tarkkuudella).

*Tulkinta:*
- Odotus letkun pituudesta L (kohta 1, sisäletku mukaan lukien): ¼-aalto ≈ 343 / (4 · L) Hz
  (rauta.md §4.1).
- Kanavat eroavat yli 5 %: letkut eivät ole identtiset. Tee uudet samasta rullasta (R1).
- Odotettu resonanssi on yli 500 Hz (letku alle ~17 cm), ja mitattu piikki löytyy lähempää
  kohtaa 1000 − odotettu Hz: soiminen taittuu. Tarkista, että 100 nF on jokaisella pinnillä,
  tai pidennä letkuja (rauta.md §4.1: 0,3 m ≈ 290 Hz on Nyquistin alla).
- Piikki on selvä ja oikeassa kohdassa: letku ja suodatus kunnossa. Soiminen ei siirrä syklin
  keskiarvoa, ja DSP suodattaa sen (rauta.md §3.1).

### 6.3 Rivin 9 raja: 3000 rpm

*Kysymys:* pysyykö mittaus pätevänä 3000 rpm:iin asti (SUUNNITTELU.md §8, "Vielä
tarkistettavaa"; D12:n ikkuna 500–3000 rpm)?

*Data:* vaiheet 7, 8a ja 8b.

*Työkalut:*
- `python tools/sim_fit.py data/raw/2026-11-07/2026-11-07_tasainen_3000.csv`: hyväksytyt syklit
  kanavittain (hylkäyksiä vähän), kanavaerojen `8 syklin ka hajonta` ja `spread p95`
  joutokäyntiin verrattuna.
- Toisto, jossa raja on nostettu, jotta tuomio näkyy myös 3000 rpm:n ympärillä:
  `tools/replay/build/replay data/raw/2026-11-07/2026-11-07_tasainen_3000.csv --set sync_rpm_max=3500 --from lock`.
  Vertaa spreadin mediaania ja p95:tä joutokäyntiin. Vilkkuuko tila oletusrajalla
  (`replay` ilman `--set`: aika tiloittain `rpm_too_high` vs `unstable`)?

*Tulkinta (hyväksytty alustavasti 31.10.2026; tarkistetaan ensimmäisestä ajosta):* jos
3000 rpm:n spread-p95 pysyy lähellä joutokäynnin arvoa ja syklit tunnistuvat, raja voi jäädä
tai nousta. Jos hajonta kasvaa selvästi, rajaa lasketaan
sille kierrosluvulle, jolla mittaus vielä pitää (vertaa 1500 ja 2000). Raja on säädettävä
(`sync_rpm_max`, S5a).

### 6.4 D2:n toleranssit (1,5 / 3,0 kPa)

*Kysymys:* ovatko D2:n lähtöarvot sopivat oikealla moottorilla (SUUNNITTELU.md §5, D2: "lopulliset
luvut asetetaan vaiheessa 4 nauhoitetun datan perusteella")?

*Data:* vaiheet 4, 5 ja 10 sekä säätöloki.

*Työkalut:*
- `tools/replay/build/replay data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv --from lock`:
  spreadin mediaani ja p95 tuomiotikeistä, eli juuri se, mitä laite näyttäisi.
- `sim_fit.py`: Δ:t, `8 syklin ka hajonta` ja `p95-poikkeama` (kuinka paljon näytön luku heiluu
  ilman säätöä).
- Vaihe 10: Δ:n muutos kierroksen osaa kohti säätölokista ja `cycle_profile.py`:n "Kanavien erot
  ajan yli" -kuvaaja.

*Tulkinta (hyväksytty alustavasti 31.10.2026; tarkistetaan ensimmäisestä ajosta):*
- Näytön heilunta (p95-poikkeama) on oltava selvästi vihreän rajan alla (esim. alle 1/5 eli
  0,3 kPa), muuten väri vilkkuu.
- Vaiheiden 4 ja 5 Δ:t ovat samat ilman säätöä (toistettavuus).
- Kuinka pieni ero on käsin saavutettavissa (säätöloki), ja vastaako se mekaanisen mittarin
  tulosta (roadmapin hyväksymiskriteeri §11)?

### 6.5 Tasauskanavat ja ristivuoto (D6)

`sim_fit.py`:n rivi `ristivuoto +240° / +480°`: naapurisylinterin imun näkyminen kanavalla.
Nolla = ei vuotoa. Selvästi nollasta poikkeava arvo viittaa tasauskanaviin (D6: "ei tiedossa").
Jos +240° ja +480° eroavat, kanavat ovat vain osan sylintereistä välillä. Tämä on tietoa D2:n ja
simulaattorin `xtalk`-arvon pohjaksi, ei toimenpide.

## 7. Ajon jälkeen

### 7.1 Talteen

- ☐ Kopioi `data/raw/2026-11-07/` vähintään kahteen paikkaan (esim. muistitikku ja pilvi). Kansio
  ei ole gitissä.
- ☐ Valokuvat käsilokista ja puhelimen näkymistä samaan kansioon.
- ☐ Tulpat ja kuparialuslevyt takaisin, vuototarkistus.

### 7.2 EngineSim-parametrit (`tools/sim_fit.py`)

```sh
python tools/sim_fit.py data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv
python tools/sim_fit.py data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv --json > data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.json
python tools/sim_fit.py data/raw/2026-11-07/2026-11-07_lepo.csv
```

Lomakkeen voi lähettää simulaattorille, jolloin UI:ta ja DSP:tä voi kehittää oikean moottorin
kaltaisella signaalilla: mock (`python tools/mock_server.py`, `http://localhost:8000/`)
tai laite simulaattorilähteellä, `POST /api/sim` (huoltosivun "Simulaattori"-kortti). Samat
arvot `replay`n EngineSim-vientiin, jolloin oikean nauhoituksen ja simulaattorin toistoja voi
verrata:

```sh
python tools/sim_fit.py data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv --form
# tulos esim. engine_on=1&rpm=900&jitter=0.0101&base=-55.13&err1=0.13&...
tools/replay/build/replay --sim engine_on=1 rpm=900 jitter=0.0101 base=-55.13 ... --seconds 30 --out data/raw/2026-11-07/2026-11-07_sim_vastine.csv
tools/replay/build/replay data/raw/2026-11-07/2026-11-07_sim_vastine.csv --from lock
```

Bashissa välilyönnein erotetun listan saa suoraan: `--sim $(python tools/sim_fit.py <csv> --form | tr '&' ' ')`.

Rajoitukset (tarkkuus synteettisellä datalla ja C++-EngineSimin viennillä, ks. alla):
`res_hz`, `res_decay_s` ja `intake_frac` eivät ole `/api/sim`-avaimia, joten ne tulostetaan
erikseen. `res` (herätteen amplitudi) arvioituu joutokäynnillä 6–14 % alakanttiin (näytteistys
ja syklikeskiarvo pyöristävät herätteen alun), ja korkeilla kierroksilla (sykli < 3 × vaimeneminen)
se jätetään pois; arvioi se tyhjäkäynniltä. `jitter` on ±15 %:n tarkkuudella 30 s:n
nauhoituksesta. Positiivinen anturioffset ei näy lepotallenteessa (GN, H1/R4),
joten `--zero-from` ei korjaa sitä.

### 7.3 Profiilit (`tools/cycle_profile.py`)

```sh
python tools/cycle_profile.py data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv --open
python tools/cycle_profile.py data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv --lp 100 --out data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1_lp100.html
```

Katso: syklien muoto ja hajonta, poikkeavat syklit (sytytyskatko, laiha sykli), sylinterien
muodon erot ja ajoitus SYL 1:stä (720° = sykli). EngineSim olettaa järjestyksen 240° / 480°.

### 7.4 Nauhoitus regressiotestiksi (WP-8)

Komennot tarkistettu 31.10.2026 WP-8:n yhdistämisen jälkeen (docs/firmware.md §6.1). Testin
muoto on `firmware/test/test_replay/test_main.cpp`:n otsakkeessa.

1. Toista koko nauhoitus ja katso, mikä osa on edustava:
   `tools/replay/build/replay data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv`
2. Leikkaa lyhyt pätkä (otsake säilyy, tässä näytteet 10 000…25 000 eli 10–25 s):
   ```sh
   awk -v a=10000 -v b=25000 '/^#/ || /^seq/ {print; next} {n++} n > a && n <= b' data/raw/2026-11-07/2026-11-07_joutokaynti_lammin_1.csv > firmware/test/data/recordings/bf50a_joutokaynti_lammin.csv
   ```
   Otsakkeen `samples=` jää alkuperäiseksi, mistä toisto antaa vain varoituksen. Ensimmäinen seq
   saa olla mikä tahansa.
3. Kirjoita `firmware/test/data/recordings/bf50a_joutokaynti_lammin.expect`: väitteet
   (`from_s=lock`, `allowed_states=…`, `rpm=…`, `rpm_tol=…`, `spread_median_min/max`, `flags_never`
   …) ja jokaisen viereen sen päättäjä (D-numero, käytösmatriisin rivi) tai "ehdotus". Mallina
   `sim_idle_synced.expect`. Kirjaa kommenttiin, että data on oikeaa (BF50A, 7.11.2026, vaihe 4).
4. Tarkista: `tools/replay/build/replay firmware/test/data/recordings/bf50a_joutokaynti_lammin.csv --expect firmware/test/data/recordings/bf50a_joutokaynti_lammin.expect`
   (paluuarvo 1 = väite rikki).
5. Testiksi: `cd firmware && pio test -e native -f test_replay` (tai WSL:ssä
   `bash tools/run_native_tests.sh test_replay`). Koodia ei muuteta.

Hyviä ensimmäisiä testejä: lämmin joutokäynti (synkassa tai lähellä), sammutus (rivi 1:
`no_signal` ja automaattinen nollaus), letku irti (rivi 6), blippaukset (WP-2c:n
uudelleenlukitus).

### 7.5 Ristiintodennus: `sim_fit.py` C++-EngineSimiä vastaan

`replay --sim` kirjoittaa totuuden otsakkeeseen (`# sim=`), ja `sim_fit.py` tulostaa silloin
vertailun "arvio / totuus / ero". Työkalun voi siis tarkistaa milloin tahansa:

```sh
tools/replay/build/replay --sim rpm=900 err2=2 noise=4 --seconds 30 --seed 2 --out x.csv
python tools/sim_fit.py x.csv
```

Tulos 31.10.2026 (10 yhdistelmää 850–3000 rpm, 8–30 s): rpm ±0,2 %, kanavatasot (base + errN)
±0,01 kPa (leikkautuvilla huipuilla −0,28 kPa, H1), depth ±0,5 % 1200 rpm:iin ja −3…−5 %
2000–3000 rpm:llä, xtalk ±0,002, peak −3…+2 %, noise −1…+5 %, res_hz ±0,5 % (2000 rpm:llä
−1,3 %; 3000 rpm:lläkin, jossa spektripiikki osuu 25 Hz:n monikertaan), res −6…−14 %
joutokäynnillä ja −20…+13 % muualla, res_decay_s ±11 %, jitter −13…+3 %. Sama tarkkuus kuin
testien Python-generaattorilla (`tools/test_sim_fit.py`), eli mallit vastaavat toisiaan.

## 8. Tulokset jatko.md:hen

Ajon jälkeen (omistaja tai orkestroija) jatko.md:n "Laitteen tila" -osioon: kohinat ja offsetit
lepotallenteesta, H1:n tulkinta, resonanssitaajuudet kanavittain, rpm-rajan havainto,
automaattisen nollauksen aika, synkronoinnin lopputulos ja EngineSim-lomake. Päätökset
(H1, rivi 9, D2) päätöslokiin.
