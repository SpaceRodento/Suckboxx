# Jatko-ohje: tila 30.10.2026 illalla

Laite toimii oikealla käyttöliittymällä ja oikeilla antureilla protolevyllä. M1/M2 (core),
WP-7a (ESP32-alusta), WP-7b (liima: Processor, §5:n reitit, NVS) ja WP-2c (DSP:n
kaasunmuutokset) ovat masterissa. Native 284/284, GCC 8.4 OK, `esp32dev_adc` käy laitteella.

## Oraakkelit

```sh
cd firmware && pio test -e native                         # 12 kokonaisuutta, 284 testiä
bash tools/check_core_gcc84.sh                            # core-headerit xtensa GCC 8.4:llä
cd firmware && pio run -e esp32dev_adc -t upload          # protolevy, oikeat anturit
python tools/embed_web.py --check                         # web/index.html == src/web/web_page.h
python tools/mock_server.py --check-config
python tools/mock_server.py                               # UI simulaattoria vasten, localhost:8000
```

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

Protolevyn kytkentä poikkeaa suunnitelmasta (docs/rauta.md §2.2): SYL 3 on GPIO32:ssa,
kiskomonitoria (GPIO39) ja pinnien 100 nF:n kondensaattoreita ei ole.

## Valmiit tänään

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

1. **Oikea moottori (M5).** Kaikki DSP-luvut on mitattu simulaattorilla. Ensimmäisellä
   ajolla: lataa `/api/raw.csv` tyhjäkäynnillä ja kaasua blipaten, ja vertaa EngineSimin
   oletuksiin (pulssin syvyys, letkuresonanssi, ristivuoto tasauskanavien kautta, D6).
2. **Rauta**, ks. "Raudan arvio" alla ja docs/rauta.md.
3. **H1/R4, väärä vihreä.** GN-anturi leikkaa ilmanpaineessa (2700 mV). Positiivinen offset
   jää näkymättä, eikä automaattinen nollaus poista sitä. Vahvistuskalibrointi noin
   −50 kPa:ssa T-haarassa kompensoi; tai GPN-anturi.
4. **Rivin 7 rajat.** ADC lukee 142–3150 mV (eFuse). `fault_lo_mv` 100 ja `fault_hi_mv` 3150
   eivät osu ääripäihin; liima koodaa raw 0 / 4095 → 0 / 3300 mV, joten oikosulku näkyy.
   Suositus edelleen 150 / 2950 mV oletuksiksi (types.h).
5. **mock_server.py** ei vielä mallinna WP-2c:n statusmuutoksia (pitoajat, yhteismuotoinen
   muutos). UI:n kehitykseen ei vaikuta, mutta mock näyttää letkuvian heti.
6. **WP-5 jatkoon:** snapshotiin `ch[i].cond` ja `act {ch, open}`, jotta UI ei jäsennä
   advice-tekstiä; CI:hin `embed_web.py --check` ja `mock_server.py --check-config`;
   testaus oikeilla iOS Safari- ja Android Chrome -selaimilla.
7. **WP-4 minorit:** `hose_off_kpa`:lta puuttuu hystereesi, driftin tunnistus reagoi kohinaan,
   kolme mutaatiota selviää testeistä (yksi auto-zero-portti FaultSensor-tilassa).
8. **WP-6:n avoimet:** manuaalinen nollaus ei vaadi tason vakautta (vielä vuotava alipaine
   kirjautuu offsetiksi); automaattinen nollaus sammutuksen jälkeen kestää 5,6–8 s.
9. **Diagnostiikka:** `myoh` laskee käynnistyksen ensimmäisen hitaan ADC-luvun (~1,35 ms)
   jälkeisen kiinnioton myöhästyneiksi (4 kpl). Ei vaikutusta mittaukseen.
10. **ESPAsyncWebServer 3.6.0** (`AsyncEventSource`): `_onDisconnect`in kilpatilanne,
    `avgPacketsWaiting`-jako nollalla, `close()`-itselukitus ja `_handleDisconnect`in
    iteraattori (web.cpp:n alku). Uudempi 3.x tarkistettava ennen tuotantoa.

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
