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
| 1 | S1 | [Riippumaton katselmus: WP-9:n ja WP-10:n status- ja kalibrointimuutokset](paketit/S1-katselmus-wp9-wp10.md) | katselmus | ei | WP-9, WP-10 |
| 2 | S2 | [DSP: voimakas positiivinen paluuaalto sotkee syklintunnistuksen](paketit/S2-dsp-paluuaalto.md) | rakennus | kyllä | WP-8 |
| 3 | S3 | [Golden-nauhoitukset käytösmatriisin riveille](paketit/S3-golden-matriisirivit.md) | rakennus | ei | WP-8, WP-9 |
| 4 | S4 | [Mockin ja laitteen aikajanapariteetti automaattiseksi](paketit/S4-mock-aikajanapariteetti.md) | rakennus | ei | WP-8, WP-10 |
| 5 | S5 | [EngineSim: mitatut ADC-piikit simulaattoriin](paketit/S5-sim-adc-piikit.md) | rakennus | kyllä | WP-9 |
| 6 | S6 | [Käyttöohje mekaanikolle](paketit/S6-kayttoohje.md) | dokumentaatio | ei | WP-9, WP-10 |
| 7 | S7 | [Mutaatiotestauksen työkalu coreen](paketit/S7-mutaatiotyokalu.md) | rakennus | ei | – |
| 8 | S8 | [Syötteiden robustisuus: lomakkeet ja CSV-lukija satunnaissyötteillä](paketit/S8-syotteiden-fuzzaus.md) | rakennus | jos vika löytyy | WP-8 |
| 9 | S9 | [Konfiguraatio-JSONin puskurivara](paketit/S9-config-json-puskuri.md) | rakennus | kyllä | WP-9 |

S2 ja S5 koskevat molemmat simulaattorin ja DSP:n käytöstä: jos molemmat ovat samana yönä
avoimina, jälkimmäinen tarkistaa törmäyksen diffistä. S4 kirjaa S2:n tunnetun eron odotetuksi.

## Ehdotukset

Yöajo kirjoittaa tänne ehdotuksen, kun yksikään jonon paketti ei ole otettavissa
(PROMPTI.md). Muoto: **Ongelma** (todisteineen, tiedosto ja rivi) / **Pysyttävä totena** /
**Valmis kun** / **Laitekoodi** (kyllä/ei). Päiväistunto muuttaa hyväksytyn ehdotuksen
paketiksi.

*(tyhjä)*
