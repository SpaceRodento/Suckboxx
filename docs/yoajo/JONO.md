# Yöajon jono

Paketit, jotka odottavat yötä. Yöajo ottaa **ylimmän rivin, jonka riippuvuus on täyttynyt ja
jota ei ole varattu** ([PROMPTI.md](PROMPTI.md) Y4). Jokaisen paketin koko teksti on omassa
tiedostossaan [paketit/](paketit)-kansiossa; alla oleva taulukko on hakemisto.

Jokaisen paketin on oltava tehtävissä **ilman rautaa, ilman `pio`:ta ja ilman ihmisen
vastausta**. Laitetta vaativa työ kuuluu [docs/jatko.md](../jatko.md):n Avoimet-listaan, ei
tänne. Malli on Zignalmeisterin ja PlantMeisterin `docs/yoajo/`:sta (31.10.2026).

## Säännöt lyhyesti

- **Tunnus on identiteetti, ei paikka.** Tunnuksia (S1, S2, …) ei koskaan käytetä uudelleen;
  prioriteetti on taulukon rivijärjestys. Valmis paketti poistuu taulukosta, ja sen tiedosto
  siirtyy `paketit/tehdyt/`-kansioon.
- **Riippuvuus täyttyy, kun se on yhdistetty `dev/integraatio`on.** Tarkista puusta, älä tästä
  tiedostosta.
- **Törmäys koskee vain avointa työtä.** Kaksi pakettia törmää, jos ne koskevat samaa tiedostoa
  ja toinen on avoin PR tai elävä `yo/<ID>-*`-varaus. Johda törmäys diffeistä
  (`python3 tools/gh_api.py files <n>`), ei pakettien muistiinpanoista.
- **Laitekoodi** = `firmware/src/`. Enintään kaksi laitekoodipakettia yössä (Y5), koska
  jokainen vaatii seuraavana päivänä ihmisen laitetestin.
- **Kaksi pakettilajia.** Rakennuspaketti tuottaa koodia. Katselmuspaketti tuottaa löydöksiä:
  tuore sessio lukee olemassa olevan muutoksen koodia ja päätöksiä vasten. Katselmoija ei saa
  olla sama sessio kuin kirjoittaja.
- **Mikä on yöajon päätettävissä:** mekanismi kyllä, tarkoitus ei (Y8). Uusi kynnys, joka
  sisältää arvovalinnan, on kysymys omistajalle, ei paketin ratkaisu.

## Jono

| # | Tunnus | Paketti | Laji | Laitekoodi | Riippuvuus |
|---|---|---|---|---|---|
| 1 | S3 | [Golden-nauhoitukset käytösmatriisin riveille](paketit/S3-golden-matriisirivit.md) | rakennus | ei | WP-8, WP-9 |
| 2 | S4 | [Mockin ja laitteen aikajanapariteetti automaattiseksi](paketit/S4-mock-aikajanapariteetti.md) | rakennus | ei | WP-8, WP-10 |
| 3 | S11 | [Testien päättäjät näkyviin ja cond/tila-invariantti](paketit/S11-testien-paattajat.md) | rakennus | ei | S1 |
| 4 | S16 | [DSP: ristivuoto ja paluuaalto yhdessä monistavat rpm:n](paketit/S16-dsp-ristivuoto-paluuaalto.md) | rakennus | kyllä | S2 |
| 5 | S10 | [Mockin lomakejäsennys laitteen kaltaiseksi](paketit/S10-mock-lomakkeet.md) | rakennus | ei | S8 |
| 6 | S12 | [Letkuvian hystereesimuisti säilyy asetusmuutoksen yli](paketit/S12-hystereesimuisti.md) | rakennus | kyllä | WP-9, S1 |
| 7 | S5 | [EngineSim: mitatut ADC-piikit simulaattoriin](paketit/S5-sim-adc-piikit.md) | rakennus | kyllä | WP-9 |
| 8 | S13 | [Käytösmatriisin UI-sarake vastaan koodi, ja rivi 11](paketit/S13-matriisi-ui-sarake.md) | katselmus | ei | S6 |
| 9 | S7 | [Mutaatiotestauksen työkalu coreen](paketit/S7-mutaatiotyokalu.md) | rakennus | ei | – |
| 10 | S15 | [Korjausehdotuksen luonnos ESPAsyncWebServerille (ei lähetetä)](paketit/S15-upstream-luonnos.md) | dokumentaatio | ei | R13 |
| 11 | S14 | [Käynnistyksen häiriöt erilleen aukko-, ohitus- ja pudotuslaskureista](paketit/S14-kaynnistyksen-laskurit.md) | rakennus | kyllä | WP-13, R13 |

S16 ja S5 koskevat molemmat simulaattorin ja DSP:n käytöstä: jos molemmat ovat samana yönä
avoimina, jälkimmäinen tarkistaa törmäyksen diffistä. S4 kirjaa S16:n tunnetun eron odotetuksi.

Tehty päiväistunnossa 31.10.2026 (tiedostot [paketit/tehdyt/](paketit/tehdyt)): S1, S2, S6,
S8, S9. Samana päivänä tehtiin päiväistunnon omat katselmukset ja paketit R13, WP-15 ja WP-16
([docs/paketit.md](../paketit.md)).

## Ehdotukset

Yöajo kirjoittaa tänne ehdotuksen, kun yksikään jonon paketti ei ole otettavissa
(PROMPTI.md). Muoto: **Ongelma** (todisteineen, tiedosto ja rivi) / **Pysyttävä totena** /
**Valmis kun** / **Laitekoodi** (kyllä/ei). Päiväistunto muuttaa hyväksytyn ehdotuksen
paketiksi.

*(tyhjä)*
