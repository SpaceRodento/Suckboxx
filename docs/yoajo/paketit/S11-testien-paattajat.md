# S11 — Testien päättäjät näkyviin ja cond/tila-invariantti

**Laji:** rakennus (testit ja dokumentaatio). **Laitekoodi:** ei.
**Riippuvuus:** S1 on `dev/integraatio`ssa.

## Havainnot (S1-katselmus, docs/katselmukset/2026-10-31-status-wp9-wp10.md)
- **Löydös 2:** osa WP-9:n ja WP-10:n asserteista on toteuttajan päättämiä: viiveiden ja
  marginaalien rajat (esim. 0,25 s, 3,9–4,3 s, 3,5 s, 90 %, 3/4, 0,5 · raja), config-rajat,
  CalResult-järjestys ja hystereesimuistin semantiikka. Niiden kommentit näyttävät päätöksiltä.
- **Löydös 7:** tilan ja `ch[i].cond`-arvojen yhtäpitävyydelle ei ole invarianttia samaan
  tapaan kuin `act`ille (`assertTextSane`, test_status:169).
- **Löydös 8:** docs/firmware.md ei mainitse letkuvian hystereesiä kohdissa, joissa WP-9 ja
  WP-10 kohtaavat (katselmuksen viittaukset firmware.md:595 ja :351; rivit ovat voineet
  siirtyä, etsi grepillä).
- **Löydös 1 (päätetty 31.10.2026):** kalibroimattomana annetaan ruuvineuvo (D11 kieltää vain
  vihreän). Testikommentti test_status:1312 nimeää D11:n väärin perusteeksi.

## Tehtävä
1. Merkitse katselmuksen taulukon toteuttajan päättämät assertit kommentilla muotoa
   `// mitattu lukitus (WP-x), ei päätös: <mitä mitattiin>`. Älä muuta arvoja.
2. Lisää `assertTextSane`en (tai vastaavaan yhteiseen tarkistukseen) invariantti: jokaisessa
   tilassa `cond`-arvot ovat sallitut (katselmuksen vastaus 2: tuomiotiloissa vain `ok`/`sat`;
   `fault_hose`-tilassa vikakanava `hose` tai `blocked`). Päättäjä: docs/firmware.md §5.
3. Korjaa test_status:1312:n kommentti: päättäjä on firmware.md §5 ja D15:n päätös
   31.10.2026, ei D11.
4. Lisää firmware.md:hen hystereesi (`hose_hyst_kpa`) niihin kohtiin, joissa letkuvian
   poistuminen kuvataan.

## Valmis kun
Native-testit vihreinä, invariantti kaatuu sabotaasissa (esim. `publicCond()` palauttaa
`ok`:n vikakanavalle), ja PR listaa merkityt assertit.
