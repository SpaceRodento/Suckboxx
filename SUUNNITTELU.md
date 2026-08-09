# SUCKBOXX — suunnitteludokumentti

> Tila: **suunnittelu käynnissä**. Mikään ei ole lukittu ennen kuin se on merkitty päätöslokiin
> (§5) tunnuksella ja päivämäärällä. Rauta on suunniteltu ja tilattavissa; **D2, D6 ja D7 ovat
> yhä auki**, ja niistä D6 estää letkuadapterien hankinnan.
>
> | dokumentti | sisältö |
> |-----|-----|
> | tämä | päätökset, perustelut, käytösmatriisi, vaiheistus |
> | [docs/rauta.md](docs/rauta.md) | anturivalinta, kytkentä, mitoitus, pinnijako, PCB |
> | [docs/hankinnat.md](docs/hankinnat.md) | osaluettelo, tilauskoodit, mitä *ei* kannata ostaa |
> | [Suckboxx.md](Suckboxx.md) | lähtöaineisto: vapaamuotoinen spec + Geminin luonnos |
> | [tools/](tools/) | mitoituslaskennat — kaikki luvut näissä dokumenteissa tulevat näistä |

---

## 1. Mikä tämä on

Kolmen alipaineanturin mittalaite, joka kertoo mekaanikolle **(a)** ovatko kolmisylinterisen
nelitahtiperämoottorin kaasuttimet/kaasuläpät synkronissa ja **(b)** millä kierrosluvulla moottori käy —
ilman erillistä rpm-anturia.

Yhden lauseen missio, johon jokainen ominaisuus pitää pystyä perustelemaan:

> **Mekaanikko näkee yhdellä silmäyksellä, mitä ruuvia pitää kääntää ja mihin suuntaan.**

Kaikki muu (dataloggaus, NMEA 2000, EFI-esikäsittely) on pivot, ei MVP. Ne eivät saa vaikuttaa
MVP:n rakenteeseen muuten kuin siten, ettei niitä suljeta pois.

---

## 2. Vahvistetut fysikaaliset reunaehdot

Laskettu `tools/mitoitus.py`:llä, ei arvattu. Kaikki luvut 3-sylinteriselle nelitahtimoottorille.

### 2.1 Signaalin taajuus

Yksi sylinteri imaisee kerran kahta kampiakselin kierrosta kohti → **f_kanava = rpm / 120**.

| rpm | kanavan pulssitaajuus | jakso | imusarjan yhteistaajuus |
|-----|-----|-----|-----|
| 500 (kylmä joutokäynti) | 4,2 Hz | 240 ms | 12,5 Hz |
| **800–900 (tavoite)** | **6,7–7,5 Hz** | **133–150 ms** | **20,0–22,5 Hz** |
| 3000 | 25 Hz | 40 ms | 75 Hz |
| 6000 (täysi kaasu) | 50 Hz | 20 ms | 150 Hz |

### 2.2 Näytteenottovaatimus

Aaltomuodon rekonstruktioon tarvitaan ≥ 20 näytettä/jakso, ei pelkkää Nyquistia:

- joutokäynnillä (6,7 Hz) riittäisi **~135 SPS/kanava**
- täydellä kaasulla (50 Hz) tarvitaan **~1000 SPS/kanava**

### 2.3 Putkiresonanssi — se "urkupilli"

Letku + anturin kuollut tilavuus muodostaa resonaattorin. 3 mm sisähalkaisijan letkulla:

| letku | ¼-aaltoresonanssi | Helmholtz |
|-----|-----|-----|
| 0,5 m | 180 Hz | 215 Hz |
| 1,0 m | 90 Hz | 152 Hz |
| 2,0 m | 45 Hz | 108 Hz |

**Hyvä uutinen:** joutokäynnin hyötysignaali (6,7 Hz) ja resonanssi (90–215 Hz) ovat 10–30-kertaa
erillään taajuudessa. Suodatus on siis helppoa — *kunhan resonanssia ei päästetä aliasoitumaan*.

**Huono uutinen:** resonanssitaajuus riippuu letkun pituudesta. 10 cm pituusero 1 m letkussa siirtää
resonanssia 9 %. Jos kanavien letkut eroavat, kanavat eivät ole vertailukelpoisia — ja koko laitteen
ainoa tehtävä on vertailla kanavia. → **Vaatimus R1** (§6).

### 2.4 Mitta-alue

| tila | absoluuttinen | gauge |
|-----|-----|-----|
| moottori sammuksissa | 101 kPa | 0 |
| joutokäynti, keskiarvo | ~46 kPa | −55 kPa |
| joutokäynti, pulssin pohja | ~26 kPa | −75 kPa |
| täysi kaasu | ~93 kPa | −8 kPa |
| pulssin huippu (paluuaalto) | ~106 kPa | +5 kPa |

Tarve: **20–110 kPa absoluuttista**.

---

## 3. Kriittiset löydökset — asiat jotka lähtöaineistossa ovat pielessä

### 3.1 🔴 HX710B ei kelpaa tähän. Kahdesta erillisestä syystä.

**Syy 1 — mitta-alue on väärä.** Moduuli on 0–40 kPa. Mittaamme aluetta 20–110 kPa absoluuttista
eli −80…+9 kPa gauge. Anturi ei kata mitään osaa tarvittavasta alueesta. Se on suunniteltu
vesipatsaan korkeuden mittaamiseen (0–4 m vettä), ei imusarjan alipaineeseen.

**Syy 2 — näytteenotto on 1–2 kertaluokkaa liian hidas.** HX710B on HX711-sukuinen 24-bit
ΔΣ-mittausvahvistin, jonka ulostulo on 10 SPS tai 40 SPS:

| | 800 rpm (6,7 Hz) | 6000 rpm (50 Hz) |
|---|---|---|
| 10 SPS | **aliasoituu** | **aliasoituu** |
| 40 SPS | 6 näytettä/jakso (liian karkea) | **aliasoituu** |

24 bittiä resoluutiota on tässä täysin hyödytöntä — meiltä ei lopu resoluutio kesken, meiltä loppuu
*aika*. Tämä on klassinen vaihtokauppa väärään suuntaan.

> **Kysymys sinulle:** onko näitä jo ostettu? Ne eivät mene hukkaan — HX710B on erinomainen
> esim. vesisäiliön pinnankorkeuteen. Mutta tähän tarvitaan toinen anturi.

**Mitä tilalle:** ratiometrinen analoginen MAP-anturi, esim. **MPX4115A / MPXH6115A**
(15–115 kPa abs, 0,2–4,8 V, vasteaika ~1 ms) tai auton 1-bar MAP (GM/Bosch — halvempi, kestää
öljysumua, mutta isompi ja vaatii oman liittimen). Analoginen ulostulo → näytteenottotaajuuden
päätämme *me*, ei anturi.

### 3.2 🟡 Anti-alias-suodatin puuttuu luonnoksesta kokonaan

Tämä on koko projektin tärkein yksittäinen rautadetalji. Resonanssi soi 90–215 Hz:ssä. Jos
näytteistämme ilman analogista alipäästösuodinta, resonanssi taittuu hyötykaistalle ja on sen
jälkeen **poistamattomissa millään digitaalisella suodattimella** — data on jo pilalla.

Korjaus on triviaali: RC-alipäästö jokaiseen kanavaan ennen ADC:tä. Yksi vastus + yksi kondensaattori.
Se on myös sama komponentti kuin jännitteenjaon alaosa, jos anturi on 5 V. Käytännössä ilmainen.

Perinteinen mekaaninen synkronointimittari tekee saman asian pneumaattisesti neulaventtiilillä
(kuristus + tilavuus = alipäästö). Me emme saa kuristaa letkua, koska tarvitsemme pulssin muodon
rpm-laskentaan — joten sama suodatus tehdään sähköisesti, pulssin *jälkeen*.

### 3.3 🟡 Luonnos vertailee "alipainearvoja" määrittelemättä mitä se tarkoittaa

Tämä on se PlantMeister-ansa. Luonnoksessa lukee "kolme pystypalkkia" ja "kun yläreunat ovat samalla
tasolla, kaasuttimet ovat synkronoitu". Mutta **minkä luvun** korkeutta palkki näyttää?

Keskiarvo, minimi, maksimi, huippu–huippu ja integraali ovat viisi eri lukua, jotka voivat olla
keskenään eri mieltä siitä, mikä kanava on "matalin". Jos tätä ei päätetä nyt, se päätetään
vahingossa ensimmäisessä koodirivissä ja perutaan kuukauden päästä. → **D1** (§5).

### 3.4 🟡 Absoluuttisten arvojen näyttäminen on käytettävyysansa

Kun mekaanikko kääntää yhtä kaasutinta, moottorin **kierrosluku muuttuu**. Alipaine riippuu
kierrosluvusta. Siis *kaikki kolme palkkia liikkuvat*, vaikka vain yhtä säädettiin. Säätäjä ei
näe tekemänsä muutoksen vaikutusta, koska yhteismuotoinen liike hukuttaa sen.

Tämä on todennäköisesti se, mikä ratkaisee onko laite parempi vai huonompi kuin 50 € mekaaninen
mittari. → **D3** (§5).

### 3.5 🟢 Mitä luonnoksessa on oikein

ESP32 + Wi-Fi AP + web-UI on hyvä valinta: ei sovellusasennusta, ei näyttöä laitteeseen, halpa.
Pivot-analyysi on realistinen ja pivotit ovat aidosti saavutettavissa samalla raudalla. RPM-johto
alipainepulsseista on oikein laskettu (2 kierrosta / imutahti).

---

## 4. Mielipide: mikä tässä on helppoa ja mikä vaikeaa

Olet oikeassa siinä, ettei mittari ole monimutkainen — **mutta vaikeus ei ole siellä missä sitä
yleensä etsitään.**

| osa-alue | vaikeus | miksi |
|-----|-----|-----|
| Elektroniikka | **helppo** | 3 analogista anturia, jakaja, RC, buck. Ei mitään uutta. |
| Firmware / DSP | **helppo** | Signaali ja häiriö ovat kaukana toisistaan taajuudessa. ESP32 on 10× ylimitoitettu tähän. |
| Verkko / UI | **helppo** | Ratkaistu ongelma, kirjastot olemassa. |
| **Mekaniikka (letkut, liitokset, tiiveys)** | **vaikea** | Määrää mittauksen pätevyyden. Ei korjattavissa ohjelmistolla. |
| **Määrittely: mitä "synkronissa" tarkoittaa** | **vaikein** | Ei ole teknistä oikeaa vastausta — se on tuotepäätös. Ja se on juuri se, mikä PlantMeisterissä jäi tekemättä. |

Toisin sanoen: riski ei ole se, ettei laite toimisi. Riski on, että laite toimii täydellisesti ja
näyttää numeroita, joista ei osaa säätää mitään.

---

## 5. Päätösloki

Jokainen päätös saa tunnuksen, perustelun ja päivämäärän. Tämä taulukko on projektin tärkein
tiedosto: jos jokin käytös on epäselvä myöhemmin, vastaus on täällä tai sitä ei ole päätetty.

### D1 — Kanavan "lukema" ✅ PÄÄTETTY 9.8.2026

**Säätöluku on keskiarvo yli syklin.** Minimi (syvin pohja) ja huippu–huippu lasketaan ja
näytetään **diagnostiikkana**, mutta ne eivät ohjaa säätöä.

*Perustelu:* keskiarvo mittaa sylinterin läpi kulkevaa ilmamassaa (virtaus ∝ paine-ero kaasuläpän
yli) eli sitä suuretta, jota kaasuttimien synkronoinnilla oikeasti tasataan. Se on myös se, mitä
mekaaniset mittarit näyttävät vaimennettuna — vuosikymmenten mekaanikkokokemus on kertynyt
tulkitsemaan tätä lukua. Minimi on herkempi sylinterikohtaisille eroille, mutta se mittaa
sylinterin *kuntoa* (venttiilit, tiiviys, puristus), ei kaasuläpän asentoa; sen nostaminen
säätöluvuksi houkuttelisi korjaamaan kaasuttimella jotain, joka ei ole kaasuttimessa.

*Seuraus:* kolme lukua per kanava on halpaa laskea. Yksi **säätöohje** — se on kallis, ja niitä on
tasan yksi.

### D2 — Toleranssi ✅ PÄÄTETTY 9.8.2026

**Absoluuttinen kynnys `spread` = max(Δ) − min(Δ) -luvulle:**

| spread | tila |
|-----|-----|
| ≤ **1,5 kPa** | 🟢 synkronoitu |
| ≤ **3,0 kPa** | 🟡 lähellä, säätöä varaa |
| > 3,0 kPa | 🔴 ei synkronissa |

*Perustelu:* vastaa yleistä synkronointiohjeistusta (±1–2 cmHg ≈ 1,3–2,7 kPa). Absoluuttinen
kPa-kynnys suhteellisen sijaan, koska synkronointi tehdään joutokäynnillä eikä alipaine ehdi
muuttua merkittävästi, ja koska kPa-luku on vertailukelpoinen muihin mittareihin ja
valmistajan huolto-ohjeisiin.

*Mittaustekninen kate:* resoluutio keskiarvolla N=16 on 0,046 kPa, eli 30× kynnystä tarkempi.
Kynnys ei ole mittauksen rajoittama. **Mutta:** anturin oma tarkkuus on ±2 kPa, joten
kalibroimaton laite ei voi täyttää tätä kynnystä lainkaan → R4.

*Nämä ovat lähtöarvoja.* S5a:n mukaan molemmat kynnykset ovat ajonaikaisesti säädettäviä, ja
lopulliset luvut asetetaan vaiheessa 4 nauhoitetun datan perusteella.

### D3 — Näyttötapa ✅ PÄÄTETTY 9.8.2026

**Poikkeama valitusta master-kanavasta.** Nolla = master. Muut kaksi kanavaa näytetään
poikkeamana siitä, etumerkillä, ja UI kertoo säätösuunnan sanallisesti.

*Perustelu:* vastaa fyysistä säätötyötä — kaasutinsäädössä yhtä läppää ei yleensä voi säätää, ja
muut tuodaan siihen. Kumoaa myös §3.4:n ansan: kun kierrosluku ajautuu säädön aikana, yhteismuotoinen
liike katoaa erotuksesta ja mekaanikko näkee vain oman muutoksensa vaikutuksen.

*Seuraus:* master on valittava UI:sta → ks. **D7**.

### D4 — Anturi ✅ PÄÄTETTY 9.8.2026

**`XGZP6847A100KPGN33`** (CFSensor): analoginen, gauge −100…0 kPa, **3,3 V**, DIP6, letkunipalla.
Kolme kappaletta samasta erästä. HX710B:t (jo ostetut) siirtyvät muuhun käyttöön.

*Perustelu:* kolme ratkaisevaa etua, ks. [docs/rauta.md](docs/rauta.md) §1.1.
Lyhyesti: **3,3 V natiivi** poistaa jännitteenjaon kokonaan (ulostulo 0,2–2,7 V menee suoraan
ESP32:n ADC:hen); **gauge-alue** ei tuhlaa mitta-aluetta ilmanpaineen offsettiin, jolloin
herkkyydeksi tulee 25 mV/kPa; ja **saatavuus** on nykytuotantoa toisin kuin MPX4115AP:n, joka on
Motorola-aikainen osa ja ainakin yhdellä jälleenmyyjällä lopetetuksi merkitty.

*Tunnustettu rajoitus:* `GN`-versio saturoituu 0 kPa:ssa, eli positiivinen paluuaaltohuippu
leikkautuu. Hyväksytty, koska leikkautuminen on sama kaikissa kanavissa (D3:n erotus säilyy
pätevänä), joutokäynnillä painetta ei ole nollan yläpuolella, ja saturaatio on havaittavissa →
UI voi varoittaa. Vaihto-osa `XGZP6847A100KPGPN33` (−100…+100 kPa) on **pinnikompatibeli**, joten
tämä ei lukitse PCB:tä. → **H1**.

### D8 — Ohjainkortti ✅ PÄÄTETTY 9.8.2026

**ESP32-WROOM-32 (classic), ei S3.** Valinta tehdään ADC:n takia: classicin ADC1 on tähän
käyttöön paremmin tunnettu, ja siinä on I²S-DMA-reitti ajastettuun näytteenottoon varalla, jos
`analogRead`-pohjainen 1 kHz osoittautuu jitteriseksi.

*Pakotettu seuraus:* **ADC2 ei toimi Wi-Fin ollessa päällä**, ja MVP on Wi-Fi AP → kaikkien
kolmen kanavan on oltava ADC1:ssä. Pinnit GPIO34/35/36 (+ GPIO39 kiskomonitorille), kaikki
input-only. Ks. [docs/rauta.md](docs/rauta.md) §2.2.

### D5 — MVP:n rajaus ✅ PÄÄTETTY 9.8.2026

**Suoraan synkronointinäyttö.** Rauta → Wi-Fi AP → web-UI. Erillistä nauhoitinvaihetta ei tehdä.

*Perustelu:* käyttäjän päätös. Nopein tie näkyvään tulokseen ja todelliseen käyttöön.

*Tunnustettu vaihtokauppa:* suodatuksen ja kynnysten ensimmäinen versio perustuu laskettuun
oletukseen signaalin muodosta (§2), ei mitattuun. Se on hyvin perusteltu oletus, mutta oletus silti.

*Kaksi seurausta, jotka tämä pakottaa arkkitehtuuriin:*

- **S5a — Kaikki kynnykset ja suodatinvakiot ajonaikaisesti säädettäviä.** Ei `#define`-vakioita
  koodissa. Ne *tullaan* säätämään ensimmäisen moottorikäynnin aikana, ja jokainen uudelleenkäännös
  laiturilla on kallis. Web-UI:hin huoltosivu, arvot NVS:ään.
- **S5b — Raakadatan rengaspuskuri alusta asti** (R3). Firmware pitää muistissa viimeiset ~10 s
  raakanäytteitä; UI:ssa on "Tallenna" → CSV. Maksaa muutaman kilotavun RAM:ia ja yhden
  reitin, mutta säilyttää mahdollisuuden analysoida oikeaa dataa jälkikäteen — eli sen ainoan
  hyödyn, joka erillisestä nauhoitinvaiheesta olisi saatu. **Ilman tätä ensimmäinen moottorikäynti
  ei tuota mitään pysyvää tietoa signaalista.**

### D6 — Kohdemoottori 🔴 AVOIN

Merkki, malli, kaasutin vai ITB. Vaikuttaa letkuliitosten tyyppiin (kierre vs. tulppa vs.
T-haara) ja siihen, onko sylinterien välillä tasauskanavia (balance tubes) — ne vaimentavat
kanavien välisiä eroja ja muuttavat signaalin muotoa merkittävästi.

### D7 — Master-kanava ✅ PÄÄTETTY 9.8.2026

**Käyttäjän valittavissa UI:sta, oletus SYL 1.** Valinta säilyy NVS:ssä.

*Perustelu:* vastaa fyysistä säätötyötä — monessa moottorissa yhtä kaasuläppää ei voi säätää, ja
mekaanikko tietää kumpi se on. Nollataso pysyy paikallaan koko säädön ajan.

*Hylätty vaihtoehto ja miksi:* automaattinen "keskimmäinen kanava" olisi näyttänyt kätevältä,
mutta master vaihtuisi kesken säädön → nollataso hyppäisi ja mekaanikko menettäisi viitteen
juuri sillä hetkellä kun hän kääntää ruuvia. Sama ansa kuin §3.4:ssä, eri muodossa.

*Seuraus:* jos master itse on vialla (letku irti, anturivika), erotusnäkymä on merkityksetön →
käytösmatriisin rivi 12.

---

## 6. Vaatimukset (kertyvä lista)

- **R1** — Kaikkien kolmen letkun on oltava **identtiset**: sama sisähalkaisija, sama pituus ±2 cm,
  sama materiaali. Poikkeama siirtää kanavan resonanssia ja tekee kanavista vertailukelvottomia.
  Tämä on mekaaninen vaatimus, jota ohjelmisto ei voi korjata.
- **R2** — Analoginen anti-alias-suodatin jokaiseen kanavaan ennen ADC:tä. Ks. §3.2.
- **R3** — Laitteen on kyettävä nauhoittamaan raakadataa myöhempää offline-analyysiä varten.
  Perustelu: §7.
- **R4** — Kanavien keskinäinen tarkkuus on tärkeämpi kuin absoluuttinen tarkkuus. Yhteismuotoinen
  virhe (kaikki kolme lukevat 3 kPa liikaa) ei haittaa; kanavien välinen virhe pilaa mittauksen.
  → kaksipistekalibrointi kanavien *keskinäiseksi*, ks. [docs/rauta.md](docs/rauta.md) §7.
  Anturin oma tarkkuus on ±2 kPa, eli **kalibroimaton laite voisi näyttää 4 kPa eroa täysin
  synkronoidulla moottorilla** — enemmän kuin koko D2-toleranssi. Kalibrointi ei ole valinnainen.
- **R5** — Kotelo ei saa olla ilmatiivis. Gauge-anturin nollareferenssi on kotelon sisäilma
  (anturissa on tasausreikä kyljessä). Tiiviissä kotelossa lämpötilan muutos siirtää kaikkien
  kanavien nollaa **hiljaa ja yhteismuotoisesti** — juuri niin, ettei D3:n erotusnäkymä paljasta
  sitä. Tarvitaan paineentasauselementti.
- **R6** — Anturit saavat käyttöjännitteensä omasta LDO:sta, ei ESP32-devkitin kiskolta. Anturi on
  ratiometrinen, ja 50 mV:n dippi kiskossa = **2,0 kPa virhe** absoluuttiseen lukemaan. Wi-Fi-lähetys
  tuottaa juuri sen kokoluokan piikkejä. Erotusnäkymä kestää tämän, absoluuttinen lukema ei.

---

## 7. Etenemisjärjestys

D5:n mukaisesti mennään suoraan synkronointinäyttöön. Nauhoitinvaihetta ei ole, mutta S5b
(rengaspuskuri) tuo saman tiedollisen hyödyn ensimmäisestä moottorikäynnistä.

**Vaihe 0 — Päätökset ja hankinnat.** ✅ **valmis lukuun ottamatta D6:ta.** D1–D5, D7, D8
kirjattu; rautasuunnitelma ja BOM valmiit ([docs/rauta.md](docs/rauta.md),
[docs/hankinnat.md](docs/hankinnat.md)). Kaikki muu on tilattavissa, mutta **D6 (kohdemoottori)
estää letkuadapterien hankinnan** — ja siten mittaamisen.

**Vaihe 1 — Rauta protolevylle.** ✅ päätetty 9.8.2026: reikälevy, ei PCB. Johdotustaulukko on
[docs/rauta.md](docs/rauta.md) §6.6. Perustelu: §6.5 — kolme rautakysymystä (H1 anturin alue,
H2 riittääkö ADC, H3 kotelo) ratkeavat vasta ensimmäisellä moottorikäynnillä, ja ne kaikki
pakottaisivat toisen levykierroksen.

**Vaihe 2 — Firmware, alhaalta ylös.**
1. ADC-luku 1 kHz × 3 kanavaa + desimointi 16 → 62,5 Hz
2. Rengaspuskuri + `/api/raw.csv` (S5b) — **tämä ennen UI:ta**, se on ensimmäisen
   moottorikäynnin ainoa pysyvä tuotos
3. Syklintunnistus ja D1:n keskiarvo, min, huippu–huippu
4. RPM jaksonaikamittauksella + sykli-syklittäinen hajonta
5. Kalibrointi (R4) ja NVS-tallennus
6. Wi-Fi AP + web-UI, D3:n erotusnäkymä
7. Huoltosivu: kaikki kynnykset ja suodatinvakiot säädettävissä (S5a)

**Vaihe 3 — Ensimmäinen moottorikäynti.** Tavoite kaksijakoinen: (a) toimiiko mittari,
(b) **nauhoita raakadata talteen.** Ilman (b) ajokerta ei tuota mitään, mitä voi analysoida
jälkikäteen.

**Vaihe 4 — Algoritmin viritys nauhoitetulla datalla.** Python PC:llä, sitten portaus C++:aan
niin että sama nauhoitus toimii golden-vector-regressiotestinä (`pio test`, sama kuvio kuin
PlantMeisterissä). Tässä vaiheessa D2:n toleranssi saa lopulliset lukunsa.

**Vaihe 5 — PCB, kotelo, veneasennus.** Vasta kun H1–H5 ovat ratkenneet.

---

## 8. Käytösmatriisi

Tämä on §3.3:n vastalääke: jokaisen tilanteen on oltava yksikäsitteinen **ennen** kuin se
koodataan. Merkintä 🔴 = riippuu vielä avoimesta päätöksestä.

Lukema on aina D1:n **keskiarvo yli syklin**, ja näyttö on aina D3:n **poikkeama masterista**.
`Δ` = kanavan lukema − masterin lukema. `spread` = max(Δ) − min(Δ).

| # | tilanne | miten tunnistetaan | mitä UI näyttää | mitä käyttäjä tekee |
|---|-----|-----|-----|-----|
| 1 | Moottori sammuksissa | kaikki kanavat 0 ± 2 kPa gauge, ei pulssia ≥3 s | "Moottori ei käy" + **automaattinen nollakalibrointi** ajetaan | käynnistää moottorin |
| 2 | Käynnistyy / epävakaa | pulssi havaittu, mutta rpm-hajonta > 15 % | "Odota, käynti tasaantuu" + rpm harmaana, palkit piilossa | odottaa |
| 3 | Joutokäynti vakaa, **synkronissa** | rpm 600–1200 ja vakaa ≥3 s, `spread` ≤ **1,5 kPa** | 🟢 "Synkronoitu", Δ-luvut, rpm ja ±hajonta | valmis |
| 4a | Joutokäynti vakaa, **lähellä** | sama, `spread` **1,5–3,0 kPa** | 🟡 suurin poikkeama korostettuna + säätösuunta sanallisesti | hienosäätää |
| 4b | Joutokäynti vakaa, **ei synkronissa** | sama, `spread` > **3,0 kPa** | 🔴 sama näkymä, voimakkaampi korostus | säätää nimettyä sylinteriä |
| 5 | Kierrosluku ajautuu säädön aikana | rpm muuttuu > 50 rpm / 2 s | Δ-luvut **pysyvät näkyvissä** (yhteismuotoinen liike on jo poistettu), rpm korostuu muuttuvana | jatkaa säätöä |
| 6 | Yksi letku irti tai vuotaa | kanava 0 ± 2 kPa muiden pulssiessa | **VIKA**: "Kanava N: ei alipainetta — tarkista letku" | kytkee letkun |
| 7 | Anturi rikki tai irti | ADC-lukema kiinni ääripäässä (< 0,10 V tai > 3,15 V) ≥ 1 s | **VIKA**: "Kanava N: anturivika" | vaihtaa anturin |
| 8 | Kaikki letkut irti, moottori käy | kaikki kanavat 0 ± 2 kPa, mutta rpm tuntematon | "Ei mittaussignaalia — kaikki letkut irti?" | kytkee letkut |
| 9 | Rpm liian korkea mittaukseen | rpm > 3000 | rpm näytetään, **synkronointiarvio piilotetaan** ("Säädä joutokäynnillä") | laskee kaasua |
| 10 | Signaali saturoituu | ADC ≥ 2,68 V pulssin huipulla | varoitus: "Positiivinen paine leikkautuu (H1)" | — (kirjataan, ks. H1) |
| 11 | Kalibrointi puuttuu tai vanhentunut | NVS:ssä ei kertoimia, tai anturi vaihdettu | **KELTAINEN**: "Kalibroimaton — lukemat suuntaa-antavia" | ajaa kalibroinnin |
| 12 | Master-kanava on itse vialla | master täyttää rivin 6 tai 7 ehdon | **VIKA**: "Master (SYL N) ei kelpaa viitteeksi" + master-valitsin korostettuna | valitsee toisen masterin |

**Suunnittelusääntö, joka ratkaisee ristiriidat:** vikatilat (6, 7, 8, 12) voittavat aina
säätötilat (3, 4a, 4b). Laite ei koskaan näytä vihreää, jos yksikään kanava on epäluotettava —
väärä "synkronoitu" on pahempi kuin ei tulosta lainkaan.

**Vielä tarkistettavaa:** rivi 9:n 3000 rpm on alustava raja — se vahvistetaan vaiheessa 4, kun
tiedetään mihin asti mittaus pysyy pätevänä. Rivien 1 ja 6 "±2 kPa" on sama luku kahdessa eri
merkityksessä (nollan tunnistus vs. letkuvian tunnistus); ne saattavat tarvita eri arvot.

---

## 9. Riskit

| riski | vakavuus | hallinta |
|-----|-----|-----|
| Letkujen epäsymmetria vääristää vertailua | **korkea** | R1; anturit symmetrisesti PCB:llä; tarkistus mittaamalla sammuneella moottorilla |
| Määrittely jää auki → uudelleenkirjoitus myöhemmin | **korkea** | §5 päätösloki + §8 käytösmatriisi ennen koodia |
| Kalibroimaton laite näyttää 4 kPa eroa synkatulla moottorilla | **korkea** | R4 pakollinen kaksipistekalibrointi; nollapiste automaattisesti käynnistyksessä |
| Signaali ei näytäkään oletetulta (tasauskanavat, paluuaallot) | **korkea** *(nousi, koska D5 poisti nauhoitinvaiheen)* | S5b rengaspuskuri + S5a säädettävät vakiot; ensimmäisestä ajokerrasta on saatava data talteen |
| Tiivis kotelo siirtää nollaa hiljaa | keskitaso | R5 paineentasaus; ei näy D3:n erotuksessa → ei löydy testaamalla |
| Wi-Fi-piikit kiskossa = 2 kPa virhe | keskitaso | R6 oma LDO + kiskomonitori; D3 suojaa erotusnäkymän |
| ESP32:n ADC-kanavien keskinäinen ero | keskitaso | R4-kalibrointi; varalla MCP3208 (H2) |
| Kosteus/öljysumu tukkii anturin | keskitaso | Vesilukko letkuun, laite liitäntöjä ylemmäs |
| MPX/XGZP-saatavuus muuttuu | matala | GN ja GPN ovat pinnikompatibeleja; 5 V:n versio toimii jakajalla |
| Scope creep pivoteihin ennen MVP:tä | keskitaso | §1 missiolause portinvartijana |

---

## 10. Muutosloki

| pvm | muutos |
|-----|-----|
| 9.8.2026 | Dokumentti luotu. Mitoituslaskenta tehty, HX710B hylätty, D1–D6 avattu. |
| 9.8.2026 | D1, D3, D4, D5 päätetty. D5:n seuraukset S5a/S5b kirjattu, D7 avattu. |
| 9.8.2026 | Anturi valittu (`XGZP6847A100KPGN33`) datasheetin pohjalta; D8 (ESP32 classic) päätetty. Rautasuunnitelma ja BOM laadittu. R5, R6 lisätty. §7 kirjoitettu D5:n mukaan, §8 käytösmatriisi täytetty. |
| 9.8.2026 | D2 (1,5 / 3,0 kPa) ja D7 (master valittavissa, oletus SYL 1) päätetty. Vaihe 1 = protolevy. **Kaikki päätökset kiinni paitsi D6.** |
