# S14 — Käynnistyksen häiriöt erilleen aukko-, ohitus- ja pudotuslaskureista

**Laji:** rakennus (pieni). **Laitekoodi:** kyllä (`firmware/src/hal/`).
**Riippuvuus:** WP-13 ja R13 ovat `dev/integraatio`ssa.

## Havainto (R13-katselmus, löydös L5)
WP-13 erotti setupin aikaiset myöhästymiset omaksi laskurikseen (`late_boot`, sarjalokissa
`kaynn`), mutta `aukot`, `ohit` ja `pudot` sisältävät yhä käynnistyksen häiriöt. Laitetestin
kohta "ensimmäinen DIAG-rivi: aukot/ohit/pudot +0" ei siksi ole luotettava ilman, että
käynnistysosuus tunnetaan (docs/katselmukset/2026-10-31-wp13-hal-kirjastot.md).

## Tehtävä
Kirjaa jokaisen laskurin käynnistyksenaikainen osuus erikseen samalla mekanismilla kuin
`late_boot` (`procBootDone()`, main.cpp), tai perustele PR:ssä, miksi jokin laskuri ei voi
kasvaa ennen `procBootDone()`a. Näytä osuudet sarjalokin DIAG-rivillä ja `/diag`issa. Tarkista
ytimien välinen näkyvyys samoin kuin R13 tarkisti `late_boot`in (atomisuus, muistijärjestys).
`pio` ei ole yöllä käytettävissä: CI:n ESP32-käännös on portti (Y6).

## Valmis kun
CI vihreä kaikilla kolmella ESP32-ympäristöllä, ja PR:n **Laitetesti tarvitaan** -kohta
kertoo, miltä uudet kentät näyttävät kolmella peräkkäisellä käynnistyksellä.
