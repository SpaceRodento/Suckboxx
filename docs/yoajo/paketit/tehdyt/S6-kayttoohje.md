# S6 — Käyttöohje mekaanikolle

**Laji:** dokumentaatio. **Laitekoodi:** ei.
**Riippuvuus:** WP-9 ja WP-10 ovat `dev/integraatio`ssa.

## Miksi
docs/roadmap.md §11.3 vaatii käyttöön liittyvän dokumentaation: mitä "synkronissa" tarkoittaa,
miten master valitaan, miten virhetilat tunnistetaan ja mikä on hyväksyttävä toleranssi.
Sellaista ei ole; tieto on hajallaan SUUNNITTELU.md:ssä, status.h:n teksteissä ja UI:ssa.

## Tehtävä
`docs/kayttoohje.md`, suomeksi, puhelimella luettava (lyhyet kappaleet, taulukot):
- Kytkentä ja käynnistys: verkko `Suckboxx`, osoite, nollaus moottori sammuksissa (D11).
- Mitä näyttö kertoo: palkit ja Δ masterista (D3), värit ja D2:n rajat (1,5 / 3,0 kPa), rpm ±
  hajonta, AVAA/SULJE-merkintä.
- Jokainen käytösmatriisin tila (SUUNNITTELU.md §8) taulukkona: otsikko **täsmälleen** kuten
  laite sen näyttää (`firmware/src/core/status.h`), syy ja mitä tehdä.
- Kanavan tila (`ch[i].cond`, docs/firmware.md §5) ja master-kanavan vaihto (D7).
- Kalibrointi: automaattinen nollaus, vahvistuskalibrointi T-haaralla (rauta.md §7), mitä
  "kalibroimaton" tarkoittaa.
- Huoltosivu: mitkä asetukset on turvallista muuttaa ja mitkä eivät.

## Pysyttävä totena
Ohje ei keksi käytöstä: jokainen väite on johdettavissa koodista tai päätöslokista. Jos
dokumentti ja koodi ovat ristiriidassa, koodi on totuus; kirjaa ristiriita PR:ään.

## Valmis kun
Jokainen status.h:n otsikkoteksti esiintyy ohjeessa (tarkista grepillä ja kerro PR:ssä
lukumäärät), ja ohje on linkitetty SUUNNITTELU.md:n alusta.
