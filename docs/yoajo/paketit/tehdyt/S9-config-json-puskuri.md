# S9 — Konfiguraatio-JSONin puskurivara

**Laji:** rakennus (pieni). **Laitekoodi:** kyllä (`firmware/src/core/config.h`).
**Riippuvuus:** WP-9 on `dev/integraatio`ssa.

## Havainto (WP-9:n raportti 31.10.2026)
`GET /api/config` -JSONin pahin tapaus on 5470 / 6144 tavua (`kConfigJsonSize`,
config.h:534), eli vara on 674 tavua, ja test_config kaatuu, kun vara alittaa 512 tavua.
Seuraava uusi konfiguraatiokenttä kaataa siis todennäköisesti testin. Puskuri varataan
pyynnön ajaksi keosta (`web.cpp`:n `/api/config`-käsittelijä, `new (std::nothrow)`).

## Tehtävä
Mittaa pahimman tapauksen koko kenttää kohden ja valitse uusi koko niin, että varaa on
vähintään noin viidelle uudelle kentälle. Perustele keon kestävyys: vapaa keko laitteella
~89 kt (jatko.md), ja varaus on lyhytaikainen. Päivitä test_configin raja-arvon kommentti
ja docs/firmware.md §4.4, jos se mainitsee koon. Vaihtoehto, jos koko ei saa kasvaa: JSONin
tiivistys (esim. pitkät `help`-tekstit erilliseen reittiin), mutta se muuttaa julkaistua
rajapintaa ja on siksi **Kysymykset**-kohta, ei tämän paketin ratkaisu.

## Valmis kun
test_config todistaa uuden varan, ja PR kertoo koon ennen ja jälkeen sekä kenttää kohden
lasketun kasvun.
