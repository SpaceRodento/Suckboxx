# S12 — Letkuvian hystereesimuisti säilyy asetusmuutoksen yli

**Laji:** rakennus (pieni). **Laitekoodi:** kyllä (`firmware/src/core/status.h`).
**Riippuvuus:** WP-9 ja S1 ovat `dev/integraatio`ssa.

## Havainto (S1-katselmus, löydös 6)
Asetusmuutos tai masterin vaihto (`configure()`) tyhjentää letkuvian hystereesin muistin
(test_status:1109). Jos irti olevan letkun taso on hystereesialueella (2,0–2,2 kPa), otsikko
vaihtuu "ei alipainetta" → "Odota" → "ei pulssia, tukos?", vaikka fyysinen tila on sama.
Masterin vaihto on tavallinen toimenpide kesken säädön (D7), joten välkkyvä otsikko osuu
juuri siihen hetkeen.

## Tehtävä
Säilytä kanavakohtainen hystereesimuisti `configure()`-kutsun yli, kun letkuvian parametrit
(`hose_off_kpa`, `hose_hyst_kpa`) eivät muutu. Jos ne muuttuvat, nollaus on perusteltu. Tämä
on mekanismi (Y8), ei uusi kynnys. Testi: hystereesialueella oleva irti oleva letku pitää
otsikkonsa masterin vaihdon yli. Päättäjä: käytösmatriisin rivi 6 ja D7 (nollataso ei hypi
kesken säädön). Päivitä test_status:1109:n odotus vain, jos se lukitsee juuri tämän
käytöksen, ja perustele PR:ssä.

## Valmis kun
Uusi testi kaatuu ilman korjausta, muut native-testit ja golden-toistot (`replay --expect`)
ovat ennallaan, ja CI:n ESP32-käännös on vihreä.
