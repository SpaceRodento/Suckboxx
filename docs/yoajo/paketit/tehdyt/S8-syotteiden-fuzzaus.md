# S8 — Syötteiden robustisuus: lomakkeet ja CSV-lukija satunnaissyötteillä

**Laji:** rakennus (testit; löydöksen korjaus voi koskea laitekoodia). **Laitekoodi:** vain jos
vika löytyy.
**Riippuvuus:** WP-8 on `dev/integraatio`ssa.

## Miksi
Laite jäsentää verkosta tulevaa syötettä: `POST /api/config` ja `POST /api/sim`
(form-urlencoded, `form.h`, `config.h`, `sim_api.h`) sekä PC:llä nauhoituksia
(`firmware/test/support/recording.h`, 980 riviä, lukee puhelimen tai sarjaportin tuottamaa
dataa). Testit kattavat tunnetut virhetapaukset, mutta eivät satunnaista syötettä ASanin alla.

## Tehtävä
Native-testikokonaisuus `test_fuzz`: deterministinen pseudosatunnainen generaattori (kiinteät
siemenet, ≥ 10 000 tapausta per jäsennin, ajoaika alle 10 s) `FormPair`-taulukoille
(`form.h`; laitteella ESPAsyncWebServer purkaa lomakkeen, web.cpp:175) syötettyinä
`Processor::applyConfigForm`ille ja `simApplyForm`ille (sim_api.h), sekä recording.h:n
CSV-lukijalle: satunnaiset tavut,
mutatoidut kelvolliset syötteet (katkaisu, toistot, ylipitkät arvot, NaN/inf, negatiiviset,
UTF-8:n rikkinäiset sekvenssit). Väitteet: ei kaatumista eikä ASan/UBSan-löydöstä, hylätty
syöte ei muuta tilaa (atomisuus, docs/firmware.md §4.4), hyväksytty arvo on rajoissaan.

## Pysyttävä totena
Löydetty vika korjataan omana committinaan minimaalisesti, ja PR kertoo syötteen, joka sen
laukaisi. Jos korjaus koskee `firmware/src/`:tä, paketti laskee laitekoodikattoon (PROMPTI.md Y5).

## Valmis kun
`bash tools/run_native_tests.sh test_fuzz` on vihreä ASan/UBSanilla, ja PR listaa löydökset
(myös "ei löydöksiä" on tulos).
