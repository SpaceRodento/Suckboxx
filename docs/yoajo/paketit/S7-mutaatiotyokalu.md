# S7 — Mutaatiotestauksen työkalu coreen

**Laji:** rakennus (työkalu). **Laitekoodi:** ei.
**Riippuvuus:** ei.

## Miksi
Mutaatiokierrokset on tehty käsin kahdesti: WP-4 (30.10.2026, kolme selviäjää) ja WP-9
(31.10.2026, 69 mutanttia status.h:hon, calibration.h:hon ja processor.h:hon; kolme selviäjää
tapettiin uusilla testeillä). Työkalu jäi session tilapäishakemistoon, joten seuraava kierros
alkaisi taas tyhjästä. Mutaatiokierros on hyvä yötyö: hidas, mekaaninen ja oraakkelin
ratkaisema.

## Tehtävä
`tools/mutate.py` (vain stdlib) ja mutaatiolistat `tools/mutations/<header>.json`:
mutaatio = tiedosto, yksiselitteinen alkuperäinen tekstinpätkä, korvaava teksti, kuvaus ja
käytösmatriisin rivi. Työkalu tekee kopion `firmware/`-puusta tilapäishakemistoon, soveltaa
mutaation kerrallaan, kääntää ja ajaa valitut testikokonaisuudet samoilla lipuilla kuin
`tools/run_native_tests.sh` (ilman ASania nopeuden vuoksi, valinnaisesti päälle) ja raportoi
taulukon: tapettu (testi), kuollut käännökseen, selvisi. Työpuuta ei koskaan muuteta.
Aloita mutaatiolista status.h:n porteille (rivit 1, 6, 7, 10, 12, D11) ja calibration.h:lle.

## Pysyttävä totena
Työpuu on ajon jälkeen tavulleen sama (tarkista tiivisteellä). Mutaatio, jonka alkuperäinen
pätkä ei löydy yksiselitteisesti, on virhe eikä ohitus.

## Valmis kun
Ajo status.h:n listalla tuottaa raportin, selviäjät on listattu PR:ssä (älä kirjoita niille
testejä tässä paketissa, vaan ehdota jatkopaketti), ja docs/firmware.md §6:een on lisätty
käyttöohje.
