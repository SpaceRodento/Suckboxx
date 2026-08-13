# Hankinnat

> Mitoituksen perustelut: [rauta.md](rauta.md). Hinnat ovat **suuruusluokka-arvioita** —
> tarkista todellinen hinta ja saatavuus tilaushetkellä, älä luota tämän dokumentin lukuihin.
> Tilauskoodit sen sijaan on tarkistettu valmistajien datasheeteistä.

---

## 1. Lyhyt vastaus: mitä tilata

**Anturi: `XGZP6847A100KPGN`** — CFSensor, analoginen, −100…0 kPa **alipaine (GN)**, DIP6,
letkunipalla, **3,3 V tai 5 V, kumpikin käy**. Kolme kappaletta, mieluiten **samasta erästä**
(peräkkäiset sarjanumerot → pienempi kanavien välinen hajonta ennen kalibrointia).

**Ratkaiseva osa tilauskoodista on `100KPGN`, ei jännite.** `100KP` = mitta-alue 100 kPa,
`GN` = negatiivinen gauge eli alipaine. Sekaannus on helppo tehdä: moni myyjä listaa saman
piirisarjan myös **verenpainemittarin anturina** (0–40 kPa, `G`-tyyppi, positiivinen ylipaine)
— se on väärä sekä alueeltaan että suunnaltaan, sama virhe kuin HX710B:llä (§3.3). Tarkempi
selitys tilausansasta: [rauta.md](rauta.md) §1.5.

Jos listauksessa lukee **3,3 V** (`...GN33`), se on hieman siistimpi ratkaisu eikä vaadi
jakajaa. Jos tarjolla on vain **5 V:n oletusversio**, se **kelpaa yhtä hyvin** — lisää jokaiseen
kolmeen kanavaan sama 20 kΩ / 30 kΩ -jakaja (mitoitettu, ks.
[tools/signaaliketju.py](../tools/signaaliketju.py)). Tämä ei ole enää kompromissi: tarkkaa
1 kHz:n kanavatäsmäystä ei tarvita kun tavoite on perustason tasapainotus eikä pulssinmuodon
tallennus RPM-laskentaan (ks. [rauta.md](rauta.md) §1.6).

---

## 2. Osaluettelo

### 2.1 Anturit ja pneumatiikka

| # | osa | määrä | tunnus / tarkennus | arvio |
|---|-----|-----|-----|-----|
| A1 | Paineanturi | **3** | `XGZP6847A100KPGN` (CFSensor), DIP6, ⌀3 mm nippa, 3,3 V tai 5 V | ~5–8 €/kpl |
| A2 | Silikoniletku | 3 m | **sisähalkaisija 2,5 mm**, ulkohalkaisija 5–6 mm | ~5 € |
| A3 | Läpivientinippa (bulkhead) | 3 | M5 tai ⌀4 mm letkulle, kotelon seinään | ~2 €/kpl |
| A4 | Letkunkiristimet / nippusiteet | 12 | pienet, ⌀5–8 mm | ~3 € |
| A5 | Adapteri imusarjaan | 3 | **riippuu moottorista — D6 auki**, ks. §4 | ? |

⚠ **A2 on R1:n toteutus.** Osta yksi rulla ja leikkaa kolme identtistä pätkää samalla kertaa.
Älä osta kolmea eri letkua.

### 2.2 Elektroniikka

| # | osa | määrä | tunnus / tarkennus | arvio |
|---|-----|-----|-----|-----|
| B1 | ESP32-devkit | 1 | **ESP32-WROOM-32** (NodeMCU-32S / DevKitC), ei S3 | ~6–10 € |
| B2 | Buck-muunnin 12 V→5 V | 1 | MP1584EN- tai LM2596-moduuli, säädettävä | ~2 € |
| B3 | LDO 3,3 V antureille | 1 | `MCP1700-3302E/TO` (TO-92) tai AMS1117-3.3-moduuli | ~0,5 € |
| B4 | Vastus 10 kΩ | 3 | **1 % metallikalvo** (anti-alias R) | ~1 € |
| B5 | Kondensaattori 100 nF | 3 | **C0G/NP0 tai vähintään X7R**, 50 V (anti-alias C) | ~1 € |
| B6 | Vastus 10 kΩ | 2 | 1 % (kiskomonitorin jakaja GPIO39) | — |
| B7 | Kondensaattori 1 µF | 2 | keraami, LDO:n tulo ja lähtö | — |
| B8 | Kondensaattori 10 µF | 2 | elektrolyytti tai keraami, kiskojen puskurointi | ~1 € |

⚠ **B5: älä käytä Y5V- tai Z5U-keraamia.** Niiden kapasitanssi muuttuu kymmeniä prosentteja
lämpötilan ja jännitteen mukana. Kolme eri tavalla ryömivää suodatinta = kolme eri tavalla
vaimennettua kanavaa, mikä on täsmälleen se virhe, jota koko laite yrittää mitata.

### 2.3 Virransyöttö ja suojaus

| # | osa | määrä | tunnus / tarkennus | arvio |
|---|-----|-----|-----|-----|
| C1 | Sulakepidin + sulake | 1 | **1 A hidas**, veneasennukseen sopiva | ~3 € |
| C2 | TVS-diodi | 1 | `SMBJ16A` (16 V standoff, unidirectional) | ~0,5 € |
| C3 | Käänteisnapaisuussuoja | 1 | P-MOSFET `AO3401` / `IRF4905`, tai Schottky `SS34` | ~0,5 € |
| C4 | Ruuviliitin | 1 | 2-napainen, 5,08 mm raster | ~1 € |
| C5 | Virtajohto | 2 m | 2 × 0,75 mm², veneasennukseen | ~4 € |

### 2.4 Mekaniikka

| # | osa | määrä | tunnus / tarkennus | arvio |
|---|-----|-----|-----|-----|
| D1 | Kotelo | 1 | ~120 × 90 × 40 mm, **paineentasattu — ei täysin tiivis** | ~10 € |
| D2 | Paineentasauselementti | 1 | Gore-vent M12 tai vastaava; halpa vaihtoehto: alaspäin osoittava suojattu reikä | ~5 € |
| D3 | Reikälevy (protoversio) | 1 | 100 × 80 mm, 2,54 mm | ~2 € |
| D4 | Naaraspiikkirima | 2 | devkitin kannaksi, riman pituus kortin mukaan | ~1 € |
| D5 | M3-ruuvit ja välikkeet | 4 | levyn kiinnitykseen | ~2 € |

⚠ **D1+D2 ovat yhdessä pakollinen pari.** Gauge-anturin nollareferenssi on kotelon sisäilma.
Täysin tiivis kotelo tarkoittaa, että lämpötilan muutos siirtää kaikkien kanavien nollaa —
hiljaa, ja tavalla jota D3:n erotusnäkymä *ei* paljasta. Ks. [rauta.md](rauta.md) §4.3.

**Karkea kokonaisarvio: 60–90 €** ilman moottorikohtaisia adaptereita (A5) ja PCB:tä.

---

## 3. Tilausreitit

### 3.1 AliExpress (suositeltu anturille)

| osa | hakusana |
|-----|-----|
| A1 anturi | `XGZP6847A -100~0kPa GN` tai `XGZP6847A 100KPGN` (3.3V tai 5V, kumpikin käy) |
| A2 letku | `silicone tube 2.5mm ID 5mm OD` |
| B1 ESP32 | `ESP32 WROOM 32 devkit NodeMCU 38pin` |
| B2 buck | `MP1584EN mini buck converter` |

**Ennen tilausta, tarkista listauksesta:**
1. Mitta-alue on **−100…0 kPa** ja tyyppi **`GN`** (negatiivinen gauge). Myyjät myyvät samaa
   koteloa kymmenessä eri alueessa ja myös **positiivisena** (`G`, esim. "0–40kPa
   sphygmomanometer" -verenpainemittarianturina) — väärä alue tai väärä suunta on helppo
   tilata vahingossa. Ks. [rauta.md](rauta.md) §1.5.
2. Ulostulo on **analoginen** (`A`), ei I²C (`D` = XGZP6847**D**).
3. Käyttöjännite: **3,3 V on siistimpi jos löytyy, mutta 5 V käy myös** (ks. §1 yllä ja
   [rauta.md](rauta.md) §1.6) — ei enää este.

Tilaa **4 kpl**, ei 3. Yksi vara-anturi on halpa vakuutus, ja ylimääräinen kanava on hyödyllinen
kalibroinnin ristiintarkistuksessa.

### 3.2 TME — kaikelle muulle, ja kalliimpi varareitti anturille

TME on suositeltu reitti kaikelle **muulle kuin anturille** (osaluettelo alla). Anturina
`MPX4115AP` on kelvollinen mutta **ei enää ensisijainen suositus** — se on 3–10× kalliimpi kuin
AliExpress-XGZP6847A (~20–70 € vs. ~5–8 €/kpl) ja tarpeellinen vain, jos AliExpress-tilaus
jostain syystä ei onnistu (ks. [rauta.md](rauta.md) §1.6).

TME:n valikoimassa oleva vastine on **`MPX4115A`** — mutta huomaa: datasheetin ORDERING
INFORMATION -taulukon mukaan `MPX4115A` (Case 867-08) on *Basic Element*, **ilman letkunippaa**.
Nipallinen versio on **`MPX4115AP`** (Case 867B-04). Näiden sekoittaminen on helppo ja kallis
virhe.

MPX4115AP:n ongelmat tässä käytössä:

- **5 V:n osa** → vaatii jännitteenjaon jokaiseen kanavaan (20 kΩ / 30 kΩ, mitoitettu).
- **Absoluuttinen 15–115 kPa** → mitta-alueesta menee suuri osa ilmanpaineen offsettiin.
- **Saatavuus epävarma.** Osa on Motorola-aikainen (datasheet Rev 3, 1997), ja ainakin yksi
  jälleenmyyjä merkitsee sen valmistajan lopettamaksi. Tarkista status ennen kuin suunnittelet
  sen varaan.

Se on silti **täysin toimiva** vaihtoehto, ja jos haluat kaiken yhdestä paikasta luotettavalla
toimituksella, se on se reitti. Vasteaika 1,0 ms on jopa parempi kuin XGZP:n 2,5 ms (kummallakaan
ei ole käytännön merkitystä: molemmat ovat kaukana 50 Hz:n hyötysignaalin yläpuolella).

**TME:stä joka tapauksessa:** C1 sulakepidin, C2 TVS, C4 ruuviliitin, B4/B5 tarkkuusvastukset ja
C0G-kondensaattorit, D1 kotelo, D2 paineentasauselementti. Nämä ovat osia, joissa
AliExpress-laadun arpominen ei kannata.

### 3.3 Mitä EI kannata ostaa

| osa | miksi ei |
|-----|-----|
| **HX710B** (jo hankittu) | 0–40 kPa väärä alue *ja* 10/40 SPS aliasoituu. Ks. [SUUNNITTELU.md](../SUUNNITTELU.md) §3.1. Säästä ne vesisäiliön pinnankorkeuteen. |
| MPS20N0040D / vastaavat 40 kPa -moduulit | Sama alueongelma kuin HX710B:llä. |
| ADS1115 | 860 SPS jaettuna kanaville = 215 SPS/kanava. Riittää joutokäynnille, ei täydelle kaasulle (tarve 1000 SPS/kanava). Jos ulkoinen ADC tarvitaan, se on **MCP3208** (SPI, 100 kSPS). |
| 3-bar MAP-anturi (esim. GM 12223861) | Kolminkertainen mitta-alue = kolmasosa resoluutiosta meidän alueellamme. Jos auton MAP, niin **1 bar**. |
| XGZP6847A-anturi **`G`-tyyppinä** (esim. "0-40kPa electronic sphygmomanometer sensor") | Positiivinen ylipaine (verenpainemittarikäyttöön), ei alipaine — väärä suunta *ja* liian pieni alue. Tarvitaan nimenomaan `GN`-tyyppi, ks. [rauta.md](rauta.md) §1.5. |
| Auton MAP-anturi ensisijaisena valintana | Toimiva mutta 3–10× kalliimpi kuin XGZP6847A. Käytä vain jos AliExpress-tilaus ei onnistu, ks. §3.2. |

---

## 4. 🔴 Estävä tieto: D6 — mikä moottori?

Kohta **A5 (adapteri imusarjaan)** on ainoa, jota ei voi tilata ennen kuin tiedetään merkki ja
malli. Se ei estä muun tilaamista, mutta se estää mittaamisen.

Tarvittava tieto:

1. **Merkki, malli ja vuosimalli** (esim. Yamaha F40 2015).
2. **Onko imusarjassa valmiit synkronointitulpat?** Useimmissa monisylinterisissä on kierretulpat
   juuri tätä varten — silloin adapteri on vain oikeankokoinen kierrenippa. Jos ei ole, letku
   liitetään olemassa olevaan alipainelähteeseen T-haaralla, tai imusarjaan porataan ja kierteytetään.
3. **Onko sylinterien välillä tasauskanavia** (balance tubes)? Ne vaimentavat kanavien välisiä
   eroja ja muuttavat signaalin muotoa — vaikuttaa siihen, miltä data näyttää ja mikä D2:n
   toleranssin pitäisi olla.
4. **Kaasuttimet vai kaasuläpät (ITB)?** Vaikuttaa siihen, onko säädettävissä yksi vai kaksi
   ruuvia per sylinteri, ja mikä kanava on luonteva master (**D7**).
