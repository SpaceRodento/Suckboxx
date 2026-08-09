# Rautasuunnitelma

> Kaikki mitoitus on laskettu, ei arvattu: [tools/signaaliketju_xgzp.py](../tools/signaaliketju_xgzp.py)
> ja [tools/mitoitus.py](../tools/mitoitus.py). Suunnittelupäätökset ja niiden perustelut ovat
> [SUUNNITTELU.md](../SUUNNITTELU.md):n päätöslokissa (§5).
>
> Osto-osat ja hinnat: [hankinnat.md](hankinnat.md)

---

## 1. Anturivalinta — vastaus siihen mitä tilata

### 1.1 Valinta: **XGZP6847A100KPGN33**

CFSensorin kalibroitu analoginen paineanturi, DIP6-kotelo, letkunipalla.
Tilauskoodi purkautuu näin (datasheet V2.7, ORDER GUIDE):

| osa | merkitys |
|-----|-----|
| `XGZP6847` | sarja |
| `A` | **A**nalog (vaihtoehto `D` = I²C) |
| `100` | mitta-alueen luku |
| `KP` | yksikkö = **kP**a |
| `GN` | **G**auge **N**egative = alipaine, alue **−100…0 kPa** |
| `33` | käyttöjännite **3,3 Vdc** (oletus ilman päätettä olisi 5 V) |

**Miksi juuri tämä**, kolme ratkaisevaa syytä:

1. **3,3 V natiivi → jännitteenjakoa ei tarvita.** Ulostulo on 0,2–2,7 V, joka menee suoraan
   ESP32:n ADC-tuloon. Tämä poistaa kolme vastusta, niiden toleranssivirheen ja koko
   5 V:n analogiapuolen. 5 V:n anturilla (MPX4115AP) jokaiseen kanavaan tarvittaisiin jakaja,
   ja jakajan kuormitus on rajoitettu anturin ulostulovirralla.
2. **Gauge, ei absoluuttinen.** Imusarjan alipaine *on* gauge-suure. Emme tuhlaa mitta-aluetta
   ilmanpaineen offsetiin → koko 2,5 V:n ulostuloalue käytetään 100 kPa:lle, herkkyys
   **25 mV/kPa**. Absoluuttisella 15–115 kPa -anturilla sama alue jaettaisiin toisin.
3. **Saatavuus.** MPX4115AP on Motorola-aikainen osa, ja ainakin yksi jälleenmyyjä merkitsee sen
   valmistajan lopettamaksi. XGZP on nykytuotantoa ja saatavilla useasta lähteestä.

### 1.2 Siirtofunktio ja käyttöalue

Datasheetin kaava 3,3 V:n versiolle: **P = (V_out − 2,7) / 0,025**, eli **V_out = 2,7 + 0,025·P**
(P gauge-kilopascaleina, negatiivinen = alipaine).

| tilanne | P gauge | P abs | V_out |
|-----|-----|-----|-----|
| anturin alaraja | −100 kPa | 1,3 kPa | 0,200 V |
| joutokäynti, syvä pulssi | −75 kPa | 26,3 kPa | 0,825 V |
| joutokäynti, keskiarvo | −55 kPa | 46,3 kPa | 1,325 V |
| täysi kaasu | −8 kPa | 93,3 kPa | 2,500 V |
| moottori seis / letku irti | 0 kPa | 101,3 kPa | 2,700 V |
| paluuaallon huippu | +5 kPa | 106,3 kPa | **2,700 V (saturoituu)** |

Käyttöalue −75…0 kPa = **0,825…2,700 V**, joka osuu keskelle ESP32:n ADC1:n lineaarista aluetta
(n. 0,15–3,10 V 11 dB:n vaimennuksella). Reunoille jää marginaalia molempiin suuntiin.

### 1.3 Tunnustettu rajoitus: positiivinen paluuaalto leikkautuu

`GN`-versio ei mittaa ilmanpainetta korkeampaa painetta — pulssin positiivinen huippu (jos sellainen
esiintyy) litistyy 2,700 V:iin. Tämä on hyväksytty, koska:

- leikkautuminen on **sama kaikissa kolmessa kanavassa** → D3:n erotusnäkymä säilyy pätevänä;
- joutokäynnillä, joka on synkronoinnin käyttötapaus, imusarjan paine pysyy alipaineen puolella;
- saturaatio on **havaittavissa** (ADC lukee kattoon) → firmware voi varoittaa, ks. käytösmatriisi.

Jos positiivinen puoli osoittautuu tarpeelliseksi, vaihto-osa on **XGZP6847A100KPGPN33**
(−100…+100 kPa, `GPN` = Negative + Positive). Hinta: herkkyys puolittuu 12,5 mV/kPa:iin.
Sama jalansija, sama kytkentä, vain kalibrointikerroin muuttuu → **ei vaadi PCB-muutosta.**

### 1.4 Anturin data lyhyesti

| ominaisuus | arvo | huomio |
|-----|-----|-----|
| Syöttö | 3,0 / 3,3 / 3,6 V | **yli 6,5 V tai yli 5 mA polttaa piirin** |
| Virrankulutus | 2 mA tyyp. | |
| Ulostulo | 0,2–2,7 V ratiometrinen | skaalautuu syöttöjännitteen mukana → §3.3 |
| Kokonaistarkkuus | ±2 %FSS | ei haittaa, ks. R4 — kalibroimme kanavien *eron* |
| Pitkäaikaisstabiilius | ±0,5 %FSS / 1000 h | |
| Vasteaika T90 | 2,5 ms | = 1. asteen napa n. **147 Hz** |
| Kompensoitu lämpötila | 0…+60 °C | riittää veneeseen; toiminta −30…+100 °C |
| Kotelo | DIP6, 2,54 mm raster | jalansija 13 × 10,16 mm, korkeus 19,9 mm |
| Letkunippa | ⌀3 mm | datasheet suosittaa letkun **sisähalkaisijaa 2,5 mm** |

**Pinnijärjestys (datasheet, PIN DEFINITION):**

| PIN 1 | PIN 2 | PIN 3 | PIN 4 | PIN 5 | PIN 6 |
|-----|-----|-----|-----|-----|-----|
| **N/C** | VDD | GND | VDD | **OUT** | GND |

⚠ PIN 1 on N/C — *älä kytke sitä mihinkään, ei edes maahan.* VDD ja GND ovat kahdennettuja.

---

## 2. Ohjainkortti: ESP32 classic, ei S3

**Valinta: ESP32-WROOM-32 -pohjainen devkit** (NodeMCU-32S / DevKitC, 30- tai 38-pinninen).

*Perustelu:* valinta tehdään ADC:n takia. ESP32 classicin ADC1 on tähän käyttöön paremmin
tunnettu ja mitattu kuin ESP32-S3:n, ja classicissa on I²S-DMA-reitti ajastettuun
ADC-näytteenottoon, jos `analogRead`-pohjainen ajoitus osoittautuu liian epätasaiseksi
1 kHz:llä. S3:lla se reitti on erilainen.

### 2.1 🔴 ADC2 ei toimi Wi-Fin kanssa

Tämä on ESP32:n tunnettu rajoite, ja se määrää pinnivalinnan: **Wi-Fi-radion ollessa päällä ADC2 ei
ole käytettävissä.** MVP on Wi-Fi AP, joten kaikki kolme kanavaa **on** oltava ADC1:ssä.

### 2.2 Pinnijako

| signaali | GPIO | ADC | huomio |
|-----|-----|-----|-----|
| CH1 alipaine | **GPIO34** | ADC1_CH6 | input-only, ei sisäistä pull-uppia — ihanteellinen analogialle |
| CH2 alipaine | **GPIO35** | ADC1_CH7 | input-only |
| CH3 alipaine | **GPIO36** (VP) | ADC1_CH0 | input-only |
| V_REF-monitori | **GPIO39** (VN) | ADC1_CH3 | anturikiskon mittaus, ks. §3.3 |
| Tila-LED | GPIO2 | — | devkitin oma LED useimmissa korteissa |
| Käyttäjän nappi | GPIO0 | — | BOOT-nappi; pitkä paine = kalibrointi |

GPIO34–39 ovat **input-only** eikä niissä ole sisäisiä ylösvetoja tai -laskuja, joten ne eivät
kuormita eivätkä vääristä analogiasignaalia. Se on niiden oikea käyttötarkoitus.

---

## 3. Signaaliketju

```
                                                   ┌──────────────────────┐
  imusarja                                         │      ESP32 ADC1      │
  nippa      letku 2,5 mm ID          XGZP6847A    │                      │
    │        (identtinen ×3)          100KPGN33    │   sisäinen S&H       │
    ├────────────────────────────────►O nippa      │                      │
    │         R1: sama pituus ±2 cm   │            │                      │
    │                                 │ OUT        │                      │
    │                            ┌────┴────┐       │                      │
    │                            │  10 kΩ  │       │                      │
    │                            └────┬────┘───────► GPIO34/35/36         │
    │                                 │             │                      │
    │                              100 nF           │                      │
    │                                 │             └──────────────────────┘
    │                                GND
```

### 3.1 Anti-alias-suodatin: 10 kΩ + 100 nF per kanava

Näytteenotto on **1000 Hz/kanava**, joten Nyquist on 500 Hz. RC:n ainoa tehtävä on estää yli
500 Hz:n komponenttien taittuminen hyötykaistalle.

| R | C | f_c | vaimennus @50 Hz | @500 Hz (RC) | @500 Hz (RC + anturin oma napa) |
|-----|-----|-----|-----|-----|-----|
| 4,7 kΩ | 100 nF | 339 Hz | −0,09 dB | −5,0 dB | −16,0 dB |
| **10 kΩ** | **100 nF** | **159 Hz** | **−0,41 dB** | **−10,4 dB** | **−21,4 dB** ← valittu |
| 22 kΩ | 100 nF | 72 Hz | −1,70 dB | −16,9 dB | −27,9 dB |

Anturi itse on jo 1. asteen alipäästö (T90 2,5 ms → f_c ≈ 147 Hz), joten kokonaisuus on
2. asteen suodatin ilman lisäosia.

**Tärkeä ymmärrys, joka poikkeaa ensimmäisestä intuitiosta:** putkiresonanssia (90–180 Hz, §4)
**ei poisteta analogisesti.** Se on Nyquistin alapuolella, siis oikein näytteistetty, ja se
poistetaan digitaalisesti — jolloin sen voi myös *mitata* ja käyttää diagnostiikkaan.
Analoginen suodatin estää vain aliasoitumisen. Jos RC valittaisiin resonanssin tappamiseen
(esim. 22 kΩ → 72 Hz), leikattaisiin samalla hyötysignaalia korkeilla kierroksilla.

−0,41 dB:n vaimennus 50 Hz:ssä on sama kaikissa kolmessa kanavassa, joten se **kumoutuu**
D3:n erotusnäkymässä. Vaimennuksella ei siis ole väliä, kunhan se on identtinen — mikä on
peruste käyttää **1 % vastuksia ja 5 % (mieluiten C0G/NP0) kondensaattoreita**.

### 3.2 Resoluutio

| | kPa/LSB |
|-----|-----|
| ESP32, nimellinen 12 bittiä | 0,032 |
| ESP32, realistinen ENOB ≈ 9,5 bittiä | 0,182 |

D2:n alustava kynnys on 1,5 kPa, ja mittausresoluution pitäisi olla ~1/10 siitä eli ≤0,15 kPa.
Yksittäinen näyte ei riitä, mutta keskiarvoistus riittää heti:

| keskiarvo N | resoluutio | |
|-----|-----|-----|
| 1 | 0,182 kPa | ei riitä |
| **4** | 0,091 kPa | OK |
| **16** | 0,046 kPa | OK, hyvä marginaali |
| 64 | 0,023 kPa | tarpeeton |

**Valinta: fs = 1000 Hz, desimointi N = 16 → 62,5 Hz efektiivinen näytevirta.** UI tarvitsee
noin 20 Hz, joten jäljelle jää varaa. Raakadata (1 kHz) menee rengaspuskuriin (S5b).

### 3.3 🔴 Ratiometrisyys ja oma LDO antureille

Anturi on ratiometrinen: sen ulostulo skaalautuu syöttöjännitteen mukana. ESP32:n ADC:n referenssi
sen sijaan on sisäinen bandgap, ei syöttöjännite. Ne eivät siis kumoa toisiaan.

Laskettu vaikutus: **50 mV:n dippi 3,3 V:n kiskossa = 2,0 kPa virhe absoluuttiseen lukemaan.**
Se on enemmän kuin koko D2-toleranssi. Wi-Fi-lähetys aiheuttaa juuri tämän kokoluokan piikkejä
devkitin omalla kiskolla.

Kaksi toisiaan täydentävää korjausta:

- **Erillinen LDO antureille** (5 V → 3,3 V, esim. MCP1700-3302E tai AMS1117-3.3). Kuorma on
  vain 6 mA, joten mikä tahansa pieni LDO riittää. Tämä irrottaa anturit ESP32:n radiokuormasta
  kokonaan. Hinta ~0,3 €, kolme osaa.
- **Kiskon mittaus GPIO39:llä** jakajan 10 kΩ / 10 kΩ kautta. Firmware voi normalisoida lukemat
  mitattuun kiskojännitteeseen ja poistaa jäljelle jäävän driftin.

Huomaa, että **D3 suojaa tältä jo itsessään**: kun kaikki kolme kanavaa heiluvat samaan suuntaan,
yhteismuotoinen virhe katoaa kanavien erotuksessa. Absoluuttinen lukema on se, joka kärsii — ja
sitä näytetään kontekstina. Siksi tämä on korjattava, mutta ei kriittinen.

---

## 4. Pneumatiikka — se vaikein osa

Ohjelmisto ei voi korjata mitään tässä luvussa. Ks. [SUUNNITTELU.md](../SUUNNITTELU.md) §2.3.

### 4.1 R1: letkujen on oltava identtiset

- **Sisähalkaisija 2,5 mm** (anturin datasheetin suositus).
- **Sama pituus ±2 cm**, sama materiaali, sama reititys. Suositus **0,8–1,0 m**.
- Leikkaa kaikki kolme samasta rullasta samalla kertaa, mittaa metrimitalla, älä silmämääräisesti.

Resonanssitaajuudet 2,5 mm letkulle:

| pituus | ¼-aaltoresonanssi | Helmholtz |
|-----|-----|-----|
| 0,5 m | 180 Hz | 179 Hz |
| **0,8 m** | **112 Hz** | **142 Hz** |
| 1,0 m | 90 Hz | 127 Hz |
| 1,5 m | 60 Hz | 104 Hz |

Kaikki ovat reilusti hyötysignaalin (6,7–50 Hz) yläpuolella ja Nyquistin (500 Hz) alapuolella,
eli oikein näytteistettävissä ja digitaalisesti poistettavissa. **Pituuseron vaikutus:** 10 cm ero
1 m:n letkussa siirtää resonanssia 9 %, mikä näkyy kanavien välisenä erona jota ei ole olemassa.

### 4.2 Kotelon läpiviennit ottavat vedon, ei anturi

Datasheet varoittaa: *"avoiding excessive external force operation"*. Anturin nippa on
juotettu piirilevyyn kiinni, ja letkun nykäisy repii jalat irti.

**Ratkaisu:** letkuliittimet kotelon seinään (bulkhead-nippa M5 tai ⌀4 mm), ja niistä lyhyt
sisäletku (~60 mm) anturille. Ulkoletkun veto kohdistuu koteloon.

Sisäletkujen on myös oltava keskenään samanpituiset — ne ovat osa R1:n ketjua.

### 4.3 🔴 Kotelo EI saa olla ilmatiivis

Gauge-anturissa on **ilmanpaineen tasausreikä kotelon kyljessä** (datasheet: *"Do not block the
inlet pipe and atmosphere hole (at side of housing) with gel or glue"*). Se on anturin
nollareferenssi.

Seuraukset:

- **Älä conformal-coat tai liimaa anturin kylkeä.**
- **Kotelo tarvitsee paineentasauksen.** Täysin tiiviissä IP67-kotelossa sisäilman paine muuttuu
  lämpötilan mukana, ja koko mittaus ryömii. Käytä paineentasauselementtiä (Gore-vent tai
  vastaava, M12-kierre) tai alaspäin osoittavaa suojattua reikää.
- Tämä ansa **osuisi kaikkiin kolmeen kanavaan yhtä aikaa ja samansuuntaisesti**, joten D3:n
  erotusnäkymä ei paljastaisi sitä. Absoluuttinen lukema valehtelisi hiljaa.

### 4.4 Neste ja öljysumu pois anturista

Perämoottorin imusarjasta tulee öljysumua ja kosteutta. Nestepisara anturin kalvolla pilaa
lukeman ja lopulta anturin.

- Asenna **laite korkeammalle kuin liitäntäkohdat** → kondenssi valuu takaisin moottoriin.
- Tee letkuun **alaspäin osoittava lenkki** (vesilukko) ennen laatikkoa.
- Vaihtoehtoisesti pieni pölynerotin (esim. akvaariosuodatinvilla lyhyessä T-haarassa).

---

## 5. Virransyöttö ja veneympäristön suojaus

### 5.1 Virtabudjetti

| kuorma | virta |
|-----|-----|
| ESP32-WROOM-32, Wi-Fi AP, lähetyspiikki | 240 mA @ 5 V |
| Tila-LED + varaus | 30 mA @ 5 V |
| 3 × XGZP6847A @ 2 mA (3,3 V:n kiskolta) | 6 mA @ 3,3 V |
| **yhteensä** | **≈ 274 mA @ 5 V = 1,37 W** |
| 12 V:n puolelta (buck 85 %) | ≈ 134 mA |

Sulake **1 A hidas** — mitoitettu johdon suojaksi, ei laitteen.

### 5.2 Suojausketju

Veneen 12 V on sähköisesti likainen: latausjännite nousee 14,4 V:iin, käynnistysmoottori aiheuttaa
piikkejä ja notkahduksia, ja napojen sekoittaminen on realistinen käyttövirhe.

```
  12 V ──[ sulake 1 A ]──[ P-MOSFET, käänteisnapaisuussuoja ]──┬──[ TVS SMBJ16A ]──┬──► buck 12→5 V
                                                                │                  │
                                                               GND                GND
```

- **Sulake 1 A hidas** — tulopuolella, ennen kaikkea muuta.
- **Käänteisnapaisuus:** P-kanava-MOSFET (esim. IRF4905 / AO3401 pienemmällä virralla) on
  parempi kuin sarjadiodi, koska se ei polta 0,4 V:ta hukkaan. Schottky (SS34) kelpaa myös.
- **TVS-diodi SMBJ16A** (16 V standoff): sietää 14,4 V:n latausjännitteen, leikkaa piikit.
- **Buck 12 V → 5 V:** MP1584- tai LM2596-moduuli. MP1584 on pienempi ja tehokkaampi; LM2596 on
  yleisempi ja sietävämpi. Kumpi tahansa kelpaa 274 mA:lle.
- ESP32-devkitin 5 V / VIN-pinniin, jolloin kortin oma regulaattori tekee 3,3 V:n logiikalle.
- **Erillinen LDO 5 V → 3,3 V antureille** (§3.3), ei devkitin kiskolta.

---

## 6. PCB-suunnitelma

### 6.1 Rakenneperiaate: devkit kannassa, ei juotettuna

Ensimmäinen levy on **kantalevy (carrier board)**: ESP32-devkit ja buck-moduuli tulevat
naaraspiikkirimoihin, kaikki muu juotetaan levylle. Näin devkitin voi vaihtaa, buckin voi
korvata, eikä ensimmäinen versio kaadu yhteen väärin juotettuun moduuliin.

Levy on **kaksipuolinen, 1,6 mm, HASL** — mitään erikoista ei tarvita, joten se sopii halvimpaan
prototilausluokkaan.

### 6.2 Lohkot ja sijoittelu

```
  ┌──────────────────────────────────────────────────────────────┐
  │  [12V+GND]  ┌────────┐  ┌──────────────┐                     │ ← virtakulma
  │   ruuvi-    │ sulake │  │ buck 12→5V   │      ①              │
  │   liitin    │  TVS   │  │  (moduuli)   │                     │
  │             │  FET   │  └──────────────┘                     │
  ├─────────────┴────────┴───────────────────────────────────────┤
  │                                                              │
  │    ┌───────────────────────────────┐        [LDO 3V3]        │
  │    │                               │           ②             │
  │    │   ESP32-WROOM-32 devkit       │                         │
  │    │   (naaraspiikkirimat)         │      ③  R+C ×3          │
  │    │                               │      (ADC-pinnien       │
  │    └───────────────────────────────┘       vieressä)         │
  │                                                              │
  ├──────────────────────────────────────────────────────────────┤
  │      ⌷CH1⌷            ⌷CH2⌷            ⌷CH3⌷                 │ ← anturireuna ④
  │     XGZP6847A        XGZP6847A        XGZP6847A              │
  └──────┼─────────────────┼─────────────────┼───────────────────┘
         │                 │                 │
      nippa ↑           nippa ↑           nippa ↑   (nipat samaan suuntaan)
```

**① Virtakulma.** Tulo yhteen nurkkaan, suojausketju heti liittimen perässä. Buck-moduuli
sijoitetaan **mahdollisimman kauas antureista** — se on hakkuri ja siten kortin ainoa merkittävä
häiriölähde.

**② Anturi-LDO** omalla paikallaan, ei buckin vieressä. Tulo- ja lähtökondensaattorit (1 µF / 1 µF
keraami, MCP1700 vaatii vähintään 1 µF lähtöön) suoraan sen jalkojen viereen.

**③ RC-suodattimet.** Vastus lähelle anturia, **kondensaattori lähelle ESP32:n ADC-pinniä.** Tässä
järjestyksessä: kondensaattori toimii silloin myös ADC:n sample-and-hold-piirin varastona ja
vaimentaa vetoa pisimmältä johdinosuudelta.

**④ Anturireuna.** Kaikki kolme anturia **samaan riviin, samalle reunalle, nipat samaan suuntaan,
samalle etäisyydelle levyn reunasta.** Tämä on R1:n jatke piirilevylle: sisäletkuista tulee
automaattisesti samanpituiset, kun anturit ovat symmetrisesti.

### 6.3 Maadoitus

Yksi yhtenäinen maataso, mutta **analogia- ja tehovirrat erotetaan reitityksellä**:

- Anturien ja RC:iden paluuvirrat omalle alueelleen, joka yhtyy päämaahan **yhdessä pisteessä**
  ESP32:n GND-pinnin juuressa (star ground).
- Buckin paluuvirta **ei saa** kulkea anturialueen läpi. Vedä buckin GND suoraan syöttöliittimeen.
- Maataso katkaistaan buckin kelan ja anturialueen välistä, jos leveys sen sallii.

### 6.4 Mitat ja liittimet

| kohta | valinta |
|-----|-----|
| Levykoko | mahtuu **100 × 80 mm**:iin → halvin prototilausluokka (≤100 × 100 mm) |
| Virtaliitin | 2-napainen ruuviliitin, 5,08 mm raster |
| Anturit | 6 × läpivientireikä ⌀1,0 mm, 2,54 mm raster, jalansija 13 × 10,16 mm |
| Devkit | 2 × naaraspiikkirima; **tarkista kortin leveys ennen jalansijaa** (30- ja 38-pinniset eroavat) |
| Kiinnitys | 4 × M3-reikä nurkissa |
| Testipisteet | 3V3_SENS, 5V, GND, CH1/2/3 OUT — juotetut lenkit riittävät |

### 6.5 Suositeltu järjestys: protolevy ennen PCB:tä

Kytkentä on niin yksinkertainen (3 anturia, 3 RC:tä, LDO, buck-moduuli), että **ensimmäinen
toimiva laite kannattaa rakentaa reikälevylle.** Silloin ensimmäinen moottorikäynti ja siitä
seuraavat D2/D7-päätökset ehtivät vaikuttaa PCB:hen ennen kuin se tilataan.

PCB kannattaa tilata sitten, kun tiedetään:

- pitääkö anturi vaihtaa `GPN`-versioon (positiivinen paluuaalto),
- riittääkö ESP32:n oma ADC vai tarvitaanko ulkoinen (MCP3208),
- mikä on lopullinen kotelo ja siten levyn mitat ja reikäpaikat.

Nämä kolme ovat juuri ne, jotka pakottaisivat toisen levykierroksen. Yksi ajokerta protolevyllä
maksaa muutaman euron; turha PCB-kierros maksaa viikkoja.

---

## 7. Kalibrointi (R4) — pakollinen, ei valinnainen

Anturin kokonaistarkkuus on ±2 %FSS = **±2 kPa**. Kolme kalibroimatonta anturia voisi siis näyttää
4 kPa:n eroa täysin synkronoidulla moottorilla — enemmän kuin koko D2-toleranssi (1,5 kPa).
Absoluuttinen tarkkuus ei kiinnosta meitä lainkaan; **kanavien keskinäinen ero on koko tuote.**

### Kaksipistekalibrointi

| piste | miten | mitä saadaan |
|-----|-----|-----|
| **Nolla** | moottori sammuksissa, kaikki letkut kytkettyinä, kaikki kanavat samassa ilmanpaineessa | kanavakohtainen **offset** |
| **Vahvistus** | kaikki kolme letkua yhdistetään **samaan** alipainelähteeseen T-haaralla (käsipumppu tai imuri) | kanavakohtainen **gain** |

Nollapiste on triviaali ja se pitäisi ajaa **automaattisesti aina käynnistyksessä**, kun laite
havaitsee ettei pulssia ole. Vahvistuspiste on huoltotoimenpide.

Kertoimet NVS:ään (S5a). Kalibrointi vanhenee, jos anturi vaihdetaan → talleta myös aikaleima.

---

## 8. Avoimet rautakysymykset

| # | kysymys | vaikutus | milloin ratkeaa |
|-----|-----|-----|-----|
| H1 | `GN` (−100…0) vai `GPN` (−100…+100)? | herkkyys puolittuu, PCB ei muutu | ensimmäinen nauhoitus: saturoituuko 2,7 V? |
| H2 | Riittääkö ESP32:n ADC vai tarvitaanko MCP3208? | +1 IC, SPI-väylä | kun kalibroitu kanavaero on mitattu |
| H3 | Kotelo ja sen paineentasaus | mitat, IP-luokka | ennen PCB-tilausta |
| H4 | Letkuliitos imusarjaan: onko moottorissa valmiit tulpat? | adapterit, kierrekoot | **D6** — kohdemoottori |
| H5 | Devkitin malli (30- vs 38-pinninen) | jalansijan leveys | ennen PCB-tilausta |
