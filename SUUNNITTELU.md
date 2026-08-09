# SUCKBOXX — suunnitteludokumentti

> Tila: **LUONNOS / keskustelu käynnissä**. Mikään tässä ei ole lukittu ennen kuin se on merkitty
> päätöslokiin (§5) tunnuksella ja päivämäärällä. Ei koodia ennen kuin D1–D5 on ratkaistu.
>
> Lähtöaineisto: [Suckboxx.md](Suckboxx.md) (vapaamuotoinen spec + Geminin luonnos)

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

## 5. Päätösloki — AVOIMET PÄÄTÖKSET

Nämä ratkaistaan **ennen** ensimmäistä koodiriviä. Jokainen saa tunnuksen, päätöksen, perustelun ja
päivämäärän. Tämä taulukko on projektin tärkein tiedosto.

### D1 — Mikä luku on kanavan "lukema"? 🔴 AVOIN

Ehdokkaat ja niiden fysikaalinen merkitys:

| ehdokas | mittaa | huomio |
|-----|-----|-----|
| **Keskiarvo yli syklin** | sylinterin läpi kulkevaa ilmamassaa | Fysikaalisesti oikein: virtaus ∝ paine-ero kaasuläpän yli. Tätä mekaaniset mittarit mittaavat (vaimennettuna). |
| Minimi (syvin pohja) | imutahdin huippualipainetta | Herkin sylinterikohtaisille eroille (venttiilit, tiiviys) — mutta *ei* mittaa kaasuläpän asentoa. |
| Huippu–huippu | pulssin voimakkuutta | Diagnostiikka-arvo, ei säätöarvo. |
| Integraali | ilmamassaa tarkemmin | ~sama kuin keskiarvo, monimutkaisempi. |

**Alustava suositus: keskiarvo** — koska se vastaa sitä, mitä vuosikymmenten mekaanikkokokemus on
kertynyt tulkitsemaan, ja se mittaa oikeaa fysikaalista suuretta (ilmamassa).
**Mutta:** minimi ja huippu–huippu kannattaa laskea ja näyttää *diagnostiikkana*, koska ne
paljastavat eri viat. Kolme lukua per kanava on halpaa; kolme *säätöohjetta* olisi kallista.

### D2 — Mikä on toleranssi eli milloin "riittävän hyvä"? 🔴 AVOIN

Tarvitaan konkreettinen kynnys, esim. "max−min < 2,0 kPa → VIHREÄ". Ilman tätä UI ei voi sanoa
mitään, ja mekaanikko säätää ikuisesti. Pitää myös päättää, onko kynnys absoluuttinen (kPa) vai
suhteellinen (% keskiarvosta), koska alipaine muuttuu kierrosluvun mukana.

### D3 — Näytetäänkö absoluuttiset arvot vai erot? 🔴 AVOIN

Ks. §3.4. Vaihtoehdot: absoluuttiset palkit / poikkeama kanavien keskiarvosta / poikkeama valitusta
master-kanavasta (kaasutinsäädössä yhtä ei yleensä säädetä). Tämä ratkaisee, onko laite käytettävä.

### D4 — Anturivalinta 🔴 AVOIN

Riippuu §3.1:n vastauksesta ja siitä, onko rautaa jo hankittu.

### D5 — Mikä on MVP:n tarkka rajaus? 🔴 AVOIN

Onko ensimmäinen toimiva versio (a) synkronointinäyttö, (b) raakadatan nauhoitin ilman UI:ta, vai
(c) molemmat? Ks. §7 — suositukseni poikkeaa lähtöaineiston luonnoksesta.

### D6 — Kohdemoottori 🔴 AVOIN

Merkki, malli, kaasutin vai ITB. Vaikuttaa letkuliitosten tyyppiin ja siihen, onko tasauskanavia
(balance tubes) sylinterien välillä — ne muuttavat signaalia merkittävästi.

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
  → kalibrointi tehdään kanavien *keskinäiseksi*, ilmanpaineeseen sammuneella moottorilla.

---

## 7. Ehdotettu etenemisjärjestys (poikkeaa lähtöluonnoksesta)

Lähtöluonnos etenee: rauta → UI → valmis. Ehdotan tilalle:

**Vaihe 0 — Päätökset.** D1–D6 ratkaistu ja kirjattu. Ei koodia.

**Vaihe 1 — Nauhoitin, ei mittaria.** ESP32 lukee 3 kanavaa ~1 kHz ja kirjoittaa CSV:tä
(SD tai Wi-Fi). Ei suodatusta, ei UI:ta, ei tulkintaa. Käy moottorilla kerran ja tuo data kotiin.
> **Perustelu:** jokainen suodatus- ja kynnyspäätös on arvaus, kunnes on nähty miltä *tämän
> moottorin* signaali oikeasti näyttää. Ilman tätä vaihetta rakennamme algoritmin kuvitellulle
> signaalille. Yksi ajokerta säästää viikkoja arvailua. Datamäärä on olematon: 60 s @ 1 kHz = 0,3 MB.

**Vaihe 2 — Algoritmi PC:llä.** Suodatus, syklintunnistus, D1:n lukema, RPM — kaikki Pythonilla
nauhoitetusta datasta. Iteraatio sekunneissa, ei veneretkissä. Tuloksena tiedetään mikä toimii.

**Vaihe 3 — Portaus C++:aan + golden-vector-testit.** Sama nauhoitettu data syötetään
firmware-toteutukseen ja verrataan Python-referenssiin. Jos ne eroavat, portauksessa on bugi.
Tämä antaa regressiotestit ilman rautaa — sama kuvio kuin PlantMeisterin `pio test`.

**Vaihe 4 — UI ja käytösmatriisi.** Vasta nyt tiedetään mitä näytetään.

**Vaihe 5 — Rautatesti oikealla moottorilla.**

Kustannus: vaihe 1 maksaa ehkä päivän. Vastineeksi vaiheet 2–4 tehdään tiedon eikä oletusten varassa.

---

## 8. Käytösmatriisi (runko — täytetään kun D1–D3 on ratkaistu)

Jokaisen rivin on oltava yksikäsitteinen. Tyhjä solu = löydetty aukko määrittelyssä.

| tilanne | miten tunnistetaan | mitä UI näyttää | mitä käyttäjä tekee |
|-----|-----|-----|-----|
| Moottori sammuksissa | kaikki kanavat ≈ ilmanpaine, ei pulssia | | |
| Käynnistyy / epävakaa | rpm-hajonta suuri | | |
| Joutokäynti vakaa, synkronissa | | | |
| Joutokäynti vakaa, EI synkronissa | | | |
| Kierrosluku ajautuu säädön aikana | | | |
| Yksi letku irti / vuotaa | kanava ≈ ilmanpaine muiden pulssiessa | | |
| Anturi rikki tai irti | | | |
| Kaikki letkut irti mutta moottori käy | | | |
| Rpm liian korkea mittaukseen | | | |

---

## 9. Riskit

| riski | vakavuus | hallinta |
|-----|-----|-----|
| Letkujen epäsymmetria vääristää vertailua | **korkea** | R1; tarkistus mittaamalla sammuneella moottorilla |
| Määrittely jää auki → uudelleenkirjoitus myöhemmin | **korkea** | §5 päätösloki, vaihe 0 |
| Signaali ei näytäkään oletetulta (tasauskanavat, paluuaallot) | keskitaso | Vaihe 1 nauhoitin ennen algoritmia |
| ESP32:n ADC-kanavien keskinäinen ero | keskitaso | R4-kalibrointi; tarvittaessa ulkoinen ADC |
| Kosteus/öljysumu tukkii anturin | keskitaso | Vesilukko letkuun, anturi letkun yläpäähän |
| Scope creep pivoteihin ennen MVP:tä | keskitaso | §1 missiolause portinvartijana |

---

## 10. Muutosloki

| pvm | muutos |
|-----|-----|
| 9.8.2026 | Dokumentti luotu. Mitoituslaskenta tehty, HX710B hylätty, D1–D6 avattu. |
