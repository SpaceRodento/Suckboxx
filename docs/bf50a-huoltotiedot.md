# Honda BF50A: kaasuttimien synkronoinnin huoltotiedot

> **Lähteet ovat verkosta, haettu 31.10.2026.** Tämä tiedosto ei korvaa Hondan
> BF40A/BF50A-korjaamokäsikirjaa. Jos käsikirja tai moottori on eri mieltä, ne ovat oikeassa.
> Kohdat, jotka on vielä tarkistettava käsikirjasta tai moottorilta, ovat
> [§3](#3-tarkistettava-fyysisestä-käsikirjasta-tai-moottorista):ssä.

Hondan alkuperäistä BF40A/BF50A-korjaamokäsikirjaa ei ole verkossa vapaasti luettavissa. Arvot
tulevat kolmesta paikasta: American Hondan *Carburetion Manualista* (joulukuu 2001), blogista,
joka kopioi korjaamokäsikirjan tekstin, ja Hondan omista PDF-ohjeista.

**Luotettavuusluokat:** **A** = Hondan virallinen aineisto, **B** = jälleenmyyjä tai tunnettu
huoltolähde, **C** = foorumi.

Käyttö repossa: [SUUNNITTELU.md](../SUUNNITTELU.md) D2, D6, D7 ja D16,
[moottoriajo.md](moottoriajo.md), [kayttoohje.md](kayttoohje.md) §2 ja
[hankinnat.md](hankinnat.md) A5.

---

## 1. Mikä muuttui repon aiempiin oletuksiin verrattuna

| kohta | ennen | lähteiden mukaan | missä otettu käyttöön |
|---|---|---|---|
| Master (kiinteä perus) | SYL 1 (D7:n oletus, ei tarkistettu) | **SYL 3 = alin kaasutin** (A + B) | D16: `types.h` `master = 2` |
| Synkronointitulppa | M6 todennäköisesti | **5 × 10 mm:n ristipääruuvi + 5 mm:n tiiviste** (B; A sisarmallista) | D6, hankinnat.md A5 |
| Tyhjäkäynti | 850–950 rpm (omistajan tieto) | **950 ± 50 rpm vapaalla** (A) | D6 |
| Korotetut kierrokset huuhtelukupeilla | ei tietoa | **kielletty**: kupeilla vain tyhjäkäynti (A) | moottoriajo.md §2 ja vaiheet 6–8 |

Hondan raja kaasuttimien erolle on 20 mmHg ≈ 2,67 kPa (B). Se osuu D2:n keltaiselle kaistalle
(1,5–3,0 kPa): laitteen vihreä täyttää Hondan rajan aina, keltainen vain, jos spread on alle
2,67 kPa. D2:n rajoja ei muuteta (D16).

---

## 2. Tiedot lähteineen

Kohdan numero viittaa tutkimuksen kysymykseen: 1 tulpat, 2 numerointi ja master, 3 säätöruuvit,
4 kierrosluku, 5 ero ja SE-letkut, 6 jäähdytys ja koekäyttö, 7 turvallisuus.

| kohta | arvo | luok. | lähde | lainaus / sivu |
|---|---|---|---|---|
| 1 Tulpat | Imusarjassa yksi tulppa per sylinteri: ristipääruuvi 5 × 10 mm ja tiivistealuslevy | B | [pennyonadollar](https://blog.pennyonadollar.com/outboard.motors/honda/carburetor-synchronization-on-honda-bf40a-bf50a-outboards-312.htm) | "Remove the 5 x 10 mm plugs and sealing washers from the intake manifold of each cylinder" |
| 1 Tulpat, osat | Kaasutinryhmän osaluettelossa 93500-05010-0B "Screw, Pan, 5x10" ja 90452-323-000 "Washer, Sealing, 5mm" | B | [outboardbits.com.au](https://outboardbits.com.au/honda/bf50/bf50a/865541/) | osat 48 ja 43; kappalemäärää ei ilmoiteta |
| 1 Sama 4-syl. mallissa | "Phillips head screw, 5 x 10 mm (4)" ja "Sealing washer (4)" | A (BF75/90:n kuva) | [Honda Carburetion Manual](https://www.virtualware.nl/wp-content/uploads/Honda-carburation-manual.pdf) | s. 7-12 |
| 1 Nousu | Ei löytynyt mistään lähteestä. M5:n ISO-karkea nousu on 0,8 mm, mutta tämä on päätelmä. Tiivisteen materiaali: ei löytynyt. | – | – | – |
| 2 Numerointi ja master | No. 3 on perus (base), eikä siinä ole synkronointiruuvia. Alin kaasutin on perus. Siitä seuraa No. 1 = ylin ja No. 3 = alin. | A + B | Carburetion Manual s. 7-13; pennyonadollar | "The bottom carburetor is the base carburetor." / "The No.3 carburetor is the synchronization base carburetor." |
| 2 Tuki | Tyhjäkäyntiruuvi on alimmassa kaasuttimessa, eikä siinä ruuvissa ole maalia | C | [iboats](https://forums.iboats.com/threads/bf50a3lrta-high-idle-speed-when-warmed-up.658619/) | "one screw on the bottom carb that shouldnt have any paint on it" |
| 3 Ruuvit | Säädetään No. 1:n ja No. 2:n synkronointiruuveja vaihtamon vipuston kohdalta. Ensin perusta lähin (No. 2), sitten No. 1. 1/8 kierrosta kerrallaan, ja jokaisen säädön jälkeen kaasu auki ja kiinni muutaman kerran. | A + B | Carburetion Manual s. 7-13; pennyonadollar | "Turn the No.1 and No.2 adjusting screws" |
| 3 Merkintä | Honda merkitsee ruuvit keltaisella maalilla. Foorumilla puhutaan sinisistä ruuveista. | A / C | Carburetion Manual s. 7-2; [marineengine 429397](https://marineengine.com/boat-forum/threads/honda-bf50a-carb-clean-sync.429397/) (403, vain hakutiivistelmä) | "Yellow fingernail polish…" / "Blue is used to bring the 3 carbs into balance" |
| 3 Kiertosuunta | Ei löytynyt. Haettu Hondan aineistosta, iboatsista ja marineenginestä. | – | – | – |
| 4 Tyhjäkäynti | 950 ± 50 rpm vapaalla (BF40A, BF40A1, BF50A; samoin BF35A/45A) | A | Carburetion Manual s. 7-3 | "In neutral: 950 ± 50 rpm" |
| 4 Vaihteella | Hondan arvoa ei löytynyt. Foorumin mukaan kierrokset putoavat noin 150 rpm. | C | hakutiivistelmä, marineengine 402590 | – |
| 4 Synkronointikierrokset | Tyhjäkäynnillä ja aina samoilla kierroksilla, mieluiten alueen yläpäässä. Kierroslukumittarin tarkkuus 50 rpm tai parempi. Lämmitys noin 10 min. | A + B | Carburetion Manual s. 7-12 ja 7-13; pennyonadollar | "must be done with the engine at the same idle rpm"; "three cylinders like the high side" |
| 5 Ero | Enintään 20 mmHg (0,75 inHg) ≈ 2,5–2,7 kPa. Carburetion Manualin mukaan 0,5 inHg (≈ 1,7 kPa) on saavutettavissa. | B / A | pennyonadollar; Carburetion Manual s. 7-13 | "It should be 20 mmHg (0.75 inHg) or less" |
| 5 Absoluuttinen arvo | Hondan arvoa ei löytynyt. Foorumin "24 Hg" on epäselvä, eikä sitä voinut tarkistaa. | – / C | – | – |
| 5 SE-letkut | Kaasuttimien väliset SE-letkut (rikastimen letkut) puristetaan kiinni ennen mittausta | B (C tukee) | pennyonadollar; marineengine 425858 | "Pinch the SE tubes using … tube clips" |
| 6 Huuhtelukupit | Vain vapaalla ja vain tyhjäkäynnillä, vähintään 5 min. Kuppien kanssa ylempi vedenotto teipataan, ja kupin on peitettävä sivuaukot kokonaan. | A | [Honda PCI54237, 4/2011](https://cdn.powerequipment.honda.com/marine/pdf/maintenance/cleaning_and_flushing_instructions_for_bf35a__bf40a_d__bf45a__bf50a_d_engines.pdf) s. 1–2 | "Running the engine above idle speed can damage the water pump and overheat the engine." |
| 6 Synkronointi | Synkronointi tehdään testisäiliössä, vesi vähintään 4" (≈ 10 cm) kavitaatiolevyn yläpuolella | B | pennyonadollar | "Run the outboard motor in an outboard test tank…" |
| 6 Ristiriita | Huuhtelun vähimmäisaika: englanninkielinen omistajan käsikirja 5 min, venäjänkielinen 10 min huuhteluliittimellä ja 5 min säiliössä | A / A | [31ZW4602](https://cdn.powerequipment.honda.com/marine/pdf/manuals/31ZW4602.pdf) s. 74; [mcgrp.ru](https://mcgrp.ru/files/viewer/10175/115) | "не менее 10 минут на холостом ходу" |
| 7 Turvallisuus | Jäähdytysveden suihkun on näyttävä, muuten moottori sammutetaan heti. Hiilimonoksidi: tuuletus. Potkuri irti. Lapset ja lemmikit kauemmas ja itse irti liikkuvista osista. | A + B | PCI54237; 31ZW4602 s. 73; pennyonadollar | "poisonous carbon monoxide gas that can cause unconsciousness and death" |

**Huomioita Suckboxxin kannalta**

- **Numerointi ja SYL-merkinnät:** laitteen SYL 1–3 vastaavat Hondan kaasutinnumeroita No. 1–3,
  kun letkut kytketään niin (No. 1 = ylin, No. 3 = alin). Laitteen oletusmaster on SYL 3 (D16).
- **Hondan raja ja D2:** 20 mmHg ≈ 2,67 kPa on D2:n keltaisella kaistalla. Laitteen vihreä raja
  (1,5 kPa) on Carburetion Manualin saavutettavissa olevan 0,5 inHg:n (≈ 1,7 kPa) alapuolella.
- **SE-letkut** yhdistävät kaasuttimet toisiinsa, joten ne ovat D6:n tasauskanavakysymyksen
  tunnettu osa. Ne puristetaan kiinni ennen mittausta (B). Muita tasauskanavia ei löytynyt
  lähteistä, joten ristivuoto tarkistetaan yhä nauhoituksesta (moottoriajo.md §6.5).

---

## 3. Tarkistettava fyysisestä käsikirjasta tai moottorista

1. Tulpan kierteen nousu (oletus M5 × 0,8) sekä tiivisteen koko ja materiaali: mittaa ennen
   nippojen tilaamista (hankinnat.md A5).
2. Kaasutinten numerot: osaluettelossa lukee "(CARBURETOR NO.)". Varmista, että alin on No. 3 ja
   ettei siinä ole synkronointiruuvia.
3. Ruuvien kiertosuunta: käännä 1/8 kierrosta ja katso mittarista, kumpaan suuntaan alipaine
   muuttuu.
4. Tyhjäkäynti vaihteella ja absoluuttinen alipaine: Hondan arvoja ei löytynyt.
5. SE-letkujen sijainti ja puristuskohdat sekä se, onko SE-venttiili kiinni lämpimänä (C: muuten
   tasapainotus ei onnistu).
6. Alkuperäinen BF40A/BF50A-korjaamokäsikirja. Blogikopiossa ei ole sivunumeroita eikä
   julkaisunumeroa.

---

## 4. Mallin tunnistus

- Tunnistekilpi on oikeassa perälaudan kiinnikkeessä. Moottorin sarjanumero on lohkossa
  käynnistinmoottorin alla (A, 31ZW4602 s. 3).
- BF50A:n tunnuskoodit: BAZL (pitkä rikka), BAZU (erikoispitkä rikka), BAZS (lyhyt rikka). BF40A
  on BAYL / BAYU / BAYS (A, omistajan käsikirja s. 100–101 ja mcgrp s. 142).
- Moottorinumero BAZE…; BF50A4 (2004): moottori BAZE3000001→, runko BAZL3400001→BAZU3999999 (B,
  [outboardbits.com.au](https://outboardbits.com.au/honda/bf50/)).
- Ei BF50A: BF50D:n runkonumero alkaa BBEJ ja moottorinumero BEBEJ.
- Vuodet: omistajan käsikirja on © 1994 (A). Foorumin mukaan BAZL1008154 on vuodelta 1995 (C).
  Mallimerkinnät A1 = 2001, A2 = 2002, A4 = 2004 (B). BF40D ja BF50D tulivat PGM-FI-ruiskulla
  6.3.2008 ([Honda, 2008](https://global.honda/en/newsroom/news/2008/p080306-bf50-eng.html), A).
  BF50A on siis noin vuosilta 1995–2007, ja siinä on Keihin BG31A -kaasuttimet.
- Sisarmallit: BF35A ja BF45A ovat samaa 808 cm³:n kaasutinperhettä, ja niiden tyhjäkäynti on sama.
