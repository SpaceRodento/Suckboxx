# Suckboxx Roadmap

> Tämän dokumentin tarkoitus on toimia projektin kehityssuunnitelmana ja toteutuksen tarkistuksenä. Dokumentti on rakennettu niin, että yksi työpaketti vastaa yhtä käytännön istuntoa tai yhden selkeän toteutusjakson kriteerejä. Tarkoitus on pitää projekti hallittavana ja varmistaa, että anturijärjestelmän toiminta on vahvistettu ennen kuin siirrytään laajempaan kehitykseen.

## 0. Tila 31.10.2026

Tämän dokumentin paketit P0–P6 ovat anturilähtöinen näkymä samaan työhön, jota
[SUUNNITTELU.md](../SUUNNITTELU.md) §11 kuvaa vaiheina M0–M8. Toteutuksen tila on
[jatko.md](jatko.md):ssä, ja 31.10.2026 alkaen tehdyt työpaketit [paketit.md](paketit.md):ssä.

| Paketti | M-vaihe | Tila |
|---|---|---|
| P0 Legacy cleanup | M3 | Bring-up-firmware korvattu (WP-7). Pinnikartta 34/35/32 koodissa ja täällä. HX710B on yhä valinnainen penkkitestilähde (D14): säilytys tai poisto on omistajan päätös. |
| P1 Sensor baseline | M5 | Osittain: lepotaso 2692–2699 mV, eFuse-karakterisointi, ylipaine +1,2 kPa (H1 lievempi), kahden kanavan yhtäpitävyys 0,012 kPa. Puuttuu: painepisteet −20…−75 kPa käsipumpulla ja SYL 3. |
| P2 Bench harness | M5 | Valmis: `tools/serial_record.py` (1 kHz USB:n kautta), `tools/cycle_profile.py`, `tools/sim_fit.py` (lepotarkistus). `tools/sensor_bench_logger.py`:tä ei tehty; serial_record korvaa sen. |
| P3 ADC path | M4–M5 | Osittain: S&H-ylikuuluminen mitattu ja kierretty lämmittelyluvulla, kohina 3,5 mV RMS, piikit ±80–90 mV ~1/s. Puuttuu: 100 nF ja kiskomonitori (rauta). |
| P4 Signal conditioning | M1, M7 | DSP valmis simulaattorilla (WP-2, WP-2c); toisto ja golden-testit valmiina oikealle datalle (WP-8). Odottaa moottorinauhoituksia. |
| P5 Calibration & health | M1–M3 | Valmis koodissa (WP-3, WP-4, WP-6, WP-7b, WP-9, WP-10). Vahvistuskalibrointi testaamatta raudalla. |
| P6 Real engine | M6 | Pöytäkirja [moottoriajo.md](moottoriajo.md) (WP-12). Ei ajettu. |

## 1. Projektin tavoite

Tavoite on varmistaa, että kolmen XGZP6847A100KPGN -anturit toimivat luotettavasti imusarjan alipaineen mittaamiseen, ja että niistä voidaan laskea käyttökelpoinen synkronointiluku ilman että laite toimii vain “jostain syystä”.

Keskeinen periaate:
- Ei lähdetä kehittämään dataloggausta, NMEA 2000 -integraatiota tai EFI-esikäsittelijän toiminnallisuutta ennen kuin anturi-ADC-suodatus-kalibrointiketju on vahvistettu käytännössä.
- Kaikki tämä on alisteinen yhdelle tavoitteelle: laite kertoo mekaanikolle, missä yksi sylinteri eroaa muista, ja mihin suuntaan ruuvia tulee kääntää.

Tärkein ongelma tässä projektissa ei ole “ohjelmisto”, vaan käytännön järjestelmän validointi:
- anturi toimii oikealla alueella
- letkut ja liitännät ovat vertailukelpoiset
- ADC lukee oikean signaalin
- suodatus poistaa häiriöt ilman, että hyötysignaali katoaa
- kalibrointi ja health check tekevät järjestelmästä käyttäjäystävällisen

## 2. Projektin rajaus

### 2.1 Sisällytetään MVP:hen
- kolme anturia
- ESP32 devkit
- Wi-Fi AP + web-UI
- per-kanava mittaus
- master-kanavan vertailu
- synkronoinnin “spread” / poikkeama
- käyttökelpoinen säätöohje

### 2.2 Ei sisällytetä vielä MVP:hen
- pitkäaikainen dataloggaus
- NMEA 2000
- EFI-esikäsittely
- OTA-päivitys
- ulkoinen näyttö tai sovellus
- monimutkainen kaupallistettu käyttöliittymä

### 2.3 Uusi periaate
- “Pivotit” ovat jatkokehitystä, ei MVP:n osia.
- Ne voidaan ottaa mukaan vasta sen jälkeen, kun perustason synkronointimittaus on toistettavasti luotettava.

## 3. Roadmapin rakennelma

Tämä roadmap on jaettu työpaketteihin. Jokainen paketti on suunniteltu niin, että sen voi tehdä yhdessä istunnossa tai yhden selkeän toteutuskerran aikana. Paketit ovat järjestyksessä, koska jokainen seuraava vaihe perustuu edellisen validointitulokseen.

---

## 4. P0 — Legacy cleanup and canonical sensor architecture

### Tavoite
Siivota vanha HX710B-arkkitehtuuri pois ja ottaa käyttöön virallinen XGZP6847A-konfiguraatio, jonka perusteella kaikki myöhemmät testit ja validoinnit rakennetaan.

### Tehtävät
- Hylätään vanha HX710B-bring-up -arkkitehtuuri käytännössä, eikä se enää ole kehityksen perusta.
- Varmistetaan, että virallinen pinni-kartta on (protolevy, `firmware/src/hal/sampler.cpp` `kAdcCh`, penkkitesti 31.10.2026):
  - CH1 = GPIO34
  - CH2 = GPIO35 *(korjattu 31.10.2026: tässä luki GPIO39, joka on suunnitelman kiskomonitori, docs/rauta.md §2.2)*
  - CH3 = GPIO32
- Dokumentoidaan virallinen päätös siitä, miksi GPIO36 hylättiin: devkitin silkkipainatuksen "VP" ei ole toimiva D36-käyttö; käytännössä CH3 siirtyi GPIO32:een.
- Lisätään järjestelmän validointi nykyiseen sensoriverkostoon: per-kanava jännite, per-kanava lineaarisuus, anturidatan vertailu.

### Tuotos
- selkeä, yksi virallinen sensoritoteutuksen konfiguraatio, jota kaikki testit käyttävät

### Accept criteria
- repo ei enää viittaa HX710B:hen kehityksen perustana
- pinni-kartta on yhdenmukainen koko repo:ssa
- kaikki seuraavat testit perustuvat tähän pinnijärjestelmään

### Yksi istunto / toteutus
- cleanup + virallisen pinni-kartan vahvistaminen + alkutilanteen dokumentointi

---

## 5. Paketti 1 — Sensor baseline and truth table

### Tavoite
Varmistaa, että anturin toiminta tunnetaan toteutettavassa mittausalueessa ennen kuin siirrytään firmwareen tai UI:hin.

### Tehtävät
- Luo ensimmäinen benchmark-työkalu `tools/sensor_bench_logger.py`, joka lukee ESP32:n serial-vuodon ja tallentaa raakadatat CSV-tiedostoon.
- Määritä anturin fyysinen käyttöalue tarkasti:
  - ulostuloalue
  - validi painealue
  - nollakäyrä
  - saturointipiste
- Tee per-anturi testi seuraavissa pisteissä:
  - nollakuorma
  - −20 kPa
  - −40 kPa
  - −60 kPa
  - −75 kPa
  - lähellä 0 kPa
- Kirjaa jokainen mittaus:
  - todellinen paine
  - raw ADC
  - laskettu jännite
  - saturoituuko signaali
  - lämpötila
  - havaittu epälineaarisuus
- Testaa jokainen kanava erikseen ja sitten rinnakkain.

### Tuotos
- “Sensor truth table” dokumenttina tai CSV:na, jossa jokaista anturia koskevat realistiset mitatut arvot.

### Accept criteria
- tiedetään, miten anturi käyttäytyy validissa työalueessa
- tiedetään, missä kohtaa signaali saturoituu
- tiedetään, onko 3,3 V vai 5 V versio kannattaa käyttää tässä projektissa
- tiedetään, mikä on anturin realistinen nollareferenssi järjestelmässä

### Yksi istunto / toteutus
- yksi anturi, yksi testijärjestelmä, yksi testiskenaario.

### Vastuut
- Tekee: hardware/firmware -kehittäjä
- Tarkastus: yksiselitteinen dokumentaatio ennen seuraavaan vaiheeseen siirtymistä

---

## 5. Paketti 2 — Bench test harness

### Tavoite
Rakentaa yksinkertainen mutta luotettava testiympäristö, jossa kaikkia kolmea kanavaa voidaan verrata samalla tavalla.

### Tehtävät
- Luo testiympäristö, jossa jokainen anturi on:
  - samalla tavalla liitetty
  - samalla letkun pituudella
  - samanlaisessa kuormituksessa
  - samaan MCP/ADC-reittiin kytkettynä
- Testaa järjestelmän toiminta:
  - per-kanava vaste
  - kanavien keskinäinen ero
  - toistettavuus
  - häiriöiden vakioisuus
- Loggaa:
  - raw ADC
  - laskettu jännite
  - laskettu paine
  - aika
  - häiriö, jos sellainen esiintyy
- Lisää analyysi:
  - lineaarisuus
  - keskimääräinen poikkeama
  - max poikkeama
  - saturoituneen signaalin havaitseminen

### Tuotos
- testiskripti tai firmware-mode, joka kerää yhdenmukaisen datan jokaisesta kanavasta.

### Accept criteria
- kaikki kanavat voidaan vertailla samoissa olosuhteissa
- virhe voidaan erottaa anturista, analogialinjasta ja häiriöstä
- data on saatavilla helposti analysoitavaksi

### Yksi istunto / toteutus
- bench-ympäristö + ensimmäinen testikierros

### Vastuut
- Hardware validation
- Data collection
- Perustason anomalia-jäljitys

---

## 6. Paketti 3 — ADC path validation

### Tavoite
Varmistaa, että ESP32:n ADC lukee signaalin oikein eikä tuota omia virheitä signaalin muotoon.

### Tehtävät
- Testaa kaikki käytetyt ADC-pinnit:
  - GPIO34
  - GPIO35
  - GPIO36 / 39 riippuen kytkennästä
  - GPIO32
- Varmista:
  - ADC1 on käytössä
  - Wi-Fi AP:n ollessa päällä ADC2 ei ole käytössä
  - sample-and-hold ongelma on tunnistettu ja mitattu
- Testaa peräkkäisten kanavien vaikutukset:
  - seuraavan kanavan poikkeama edellisestä
  - ylimääräinen vahvistus / vuoto
- Testaa jännitteenjakajan ja anti-alias-suodatuksen vaikutus:
  - 10 kΩ + 100 nF
  - käytännön vastaus 50 Hz, 100 Hz, 500 Hz alueella
  - maksimivaihtelu eri kanavissa
- Havaitse virhetilat:
  - linjan oikosulku
  - riittämätön lataus
  - “railed” signaali
  - virheellinen offset

### Tuotos
- dokumentoitu “ADC-reitti validi / ei validi”

### Accept criteria
- ADC lukee signaalin toistettavasti
- eri kanavat eivät erotu tolkuttomasti ilman fyysistä syytä
- signaalin virhettä ei synny Kanavien käytöstä itsestään

### Yksi istunto / toteutus
- bench test + ADC-analyysi + virheluettelo

### Vastuut
- Analog signal path validation
- ADC stability checking
- virhetyypin dokumentointi

---

## 7. Paketti 4 — Signal conditioning and filtering

### Tavoite
Erotella oikea alipainepulssi häiriöistä, resonanssista ja ADC-artefakteista.

### Tehtävät
- Määritä per-kanava suodatus:
  - RC-suodatus ennen ADC:tä
  - näytteenottoväli
  - desimointi
  - keskiarvokoko
- Laske käyttökelpoiset suureet:
  - keskiarvo syklin yli
  - huippu
  - pohja
  - spread
  - poikkeama masterista
- Havaitse eri komponentit:
  - hyötysignaali
  - putkiresonanssi
  - anturihäiriö
  - ADC-artefakti
- Tarkista, että suodatus ei peitä hyötysignaalia liian paljon.

### Tuotos
- validi signaalinkäsittelyputki, joka tuottaa käyttökelpoisen säätöarvon eikä vain raakaa ADC-lukua.

### Accept criteria
- oikea pulssi voidaan erottaa häiriöistä
- putkiresonanssi ei estä käyttökelpoisen mittauksen
- kanavien väliset erot säilyvät diagnostisesti käyttökelpoisina

### Yksi istunto / toteutus
- DSP + filter tuning + validointi

### Vastuut
- Signal processing
- Noise reduction validation
- Kaikki per-kanava laskelmat

---

## 8. Paketti 5 — Calibration and health-check flow

### Tavoite
Tehdä laitteesta toimiva huoltotyökalu, joka kertoo mekaanikolle, onko kaikki kunnossa ja mihin suuntaan tee säätö.

### Tehtävät
- Määritä nollausmekanismi:
  - moottori pysäytetty
  - per-kanava offset
  - uusi “zero” uudelleenkäynnistyksen yhteydessä
- Määritä master-kanava:
  - valinta UI:ssa
  - alustava oletus
- Lisää laskenta:
  - per-kanava keskiarvo
  - spread = max(Δ) - min(Δ)
  - poikkeama masterista
- Lisää statusviestit:
  - anturi ok
  - anturi saturoitunut
  - signaali epävakaa
  - kanavassa häiriö
  - letku tai liitäntä ongelma
- Toteuta UI:
  - kolme kanavaa
  - masterin vertailu
  - etumerkki
  - tekstimuotoinen säätöohje

### Tuotos
- käyttökelpoinen säätöohjaus UI:lle, joka auttaa mekaanikkoa tekemään oikean säätötoimenpiteen.

### Accept criteria
- laite kertoo, mitä pitää säätää
- käyttöliittymä ei näytä vain raaka-ADC-numeroita
- virhetilat ovat ymmärrettäviä ja toimivia

### Yksi istunto / toteutus
- kalibrointi + status + UI

### Vastuut
- Firmware logic
- UI logic
- Measurement interpretation

---

## 9. Paketti 6 — Real engine validation

### Tavoite
Varmistaa, että järjestelmä toimii käytännössä imusarjassa, ei ainoastaan laboratorio-olosuhteissa.

### Tehtävät
- Asenna prototyyppi alkuperäiseen moottoriin / simulaattoriin
- Mittaa:
  - joutokäynti
  - hidas käynti
  - normaali työalue
  - säätötilanteet
- Vertaa:
  - raw data
  - suodatettu data
  - laskettu spread
  - käytännön säätötyö
- Kuvaa:
  - mitkä kanavat ovat tasapainossa
  - mikä kanava vaatii säätöä
  - onko data johdonmukainen eri ajotilanteissa

### Tuotos
- validi toimiva laitekokonaisuus, joka vastaa projektin varsinaista käyttötilannetta.

### Accept criteria
- järjestelmä erottaa kanavat luotettavasti
- eri ajotilanteissa data on käyttökelpoinen
- säätötyö on mahdollista tehdä laitteen kanssa

### Yksi istunto / toteutus
- real-world test + tulosten validointi

### Vastuut
- Field validation
- Huoltotyökalu testaus
- Käytännön soveltamiskelpoisuuden arvio

---

## 10. Kehitysjärjestys ja sitovuus

Tämä on tärkein osio projektin hallinnan kannalta:

### Järjestys
1. Paketti 1 — Sensor baseline and truth table
2. Paketti 2 — Bench test harness
3. Paketti 3 — ADC path validation
4. Paketti 4 — Signal conditioning and filtering
5. Paketti 5 — Calibration and health-check flow
6. Paketti 6 — Real engine validation

### Ennen siirtymistä seuraavaan pakettiin pitää olla selvillä
- anturi toimii oikealla alueella
- ADC-reitti ei vääristä signaalia
- signal processing toimii käytännössä
- kalibrointi ja zeroing on määritelty
- data on toistettavaa

### Pakettien väliset kriteerit
- Jos Paketti 3 epäonnistuu, P4 ja P5 ovat riskialttiita.
- Jos Paketti 1 epäonnistuu, koko projektin käyttökelpoisuus on kyseenalainen.
- Jos Paketti 6 epäonnistuu, koko järjestelmä vaatii uudelleensuunnittelun eikä vain UI-päivitystä.

---

## 11. Tukityökalut ja projektin käytännön dokumentaatio

Kehitys vaatii pieniä mutta tärkeitä työkaluja:

### 11.1 Mittausdokumentaatio
- sensor truth table
- raw ADC log
- filtered output logs
- kanavien vertailu ja testitulokset

### 11.2 Automaattinen validointi
- CSV export
- per-kanava kuvaajat
- keskiarvo / huippu / pohja / spread laskelma
- virhetilakoodi

### 11.3 Käyttöön liittyvä dokumentaatio
- mita tarkoittaa “synkronissa” käytännössä
- miten master-kanava valitaan
- miten virhetilat tunnistetaan
- mikä on hyväksyttävä spread-toleranssi käytännössä

---

## 12. Koko roadmapin yhteenveto

Tämä projekti ei ole ensisijaisesti software-project. Se on sensor reliability -projekti. Antureita, ADC-reittiä, suodatusta, kalibrointia ja käytännön validointia pitää käsitellä samana kokonaisuutena.

Oikea kehityssuunta on:
- vahvista antureiden toimiis.
- vahvista mitattu järjestelmä.
- luo kalibrointiflow.
- varmista moottorikäytännön toiminta.

Kun nämä vaiheet on käyty läpi, vasta silloin voidaan rakentaa käyttöliittymä, pitkän aikavälin dataloggaus ja muut pivotit ilman, että projekti nojaa epävarmoihin oletuksiin.

---

## 13. Viimeinen huomautus

MVP:n tärkein vaatimus ei ole, että laite näyttää “raaka dataa”.

MVP:n tärkein vaatimus on, että mekaanikko näkee heti:
- mikä kanava on poikkeavassa asennossa
- mihin suuntaan sitä pitää liikuttaa
- onko laite yleisesti luotettava vai pitääkö kansio, anturi, letku tai signaalireitti tarkistaa

Tämä on projektin oikea ydin. Kaikki muu tulee myöhemmin, mutta ennen sitä pitää rakentaa luotettava sensoristate.
