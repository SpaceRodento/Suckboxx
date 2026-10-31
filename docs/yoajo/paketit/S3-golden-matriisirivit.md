# S3 — Golden-nauhoitukset käytösmatriisin riveille

**Laji:** rakennus (testit). **Laitekoodi:** ei (`firmware/test/` ei laske).
**Riippuvuus:** WP-8 ja WP-9 ovat `dev/integraatio`ssa.

## Miksi
`test_replay` (WP-8) ajaa nyt kolme synteettistä nauhoitusta: synkassa, lähellä ja letku irti.
Käytösmatriisissa (SUUNNITTELU.md §8) on 12 riviä. Kun ensimmäiset oikeat nauhoitukset tulevat,
sama kansio on M7:n regressiopenkki, joten jokaisen rivin pitää olla siinä edustettuna
tiedostomuodossa, joka kulkee saman lukijan ja ajoituksen läpi kuin oikea data.

## Tehtävä
Tee `tools/replay/build/replay --sim … --out` -viennillä nauhoitus ja `.expect` ainakin riveille
1 (sammutus → automaattinen nollaus, `--zero-valid 0`), 2 (käynnistys/epävakaa), 5 (drift),
7 (anturivika, `fault1=2`), 9 (rpm liian korkea), 10 (saturaatio), 11 (kalibroimaton,
`--gain-valid 0` + `flags_always`), 12 (master vialla). `.expect`-muoto:
`firmware/test/test_replay/test_main.cpp`:n otsake ja docs/firmware.md §6.1.

## Pysyttävä totena
- Jokaisen `.expect`-väitteen päättäjä on kommentissa (rivi, D-numero tai firmware.md). Raja,
  jonka keksit itse, merkitään "ehdotus".
- Kansion koko: uudet nauhoitukset yhteensä ≤ 2 Mt (enintään 6 s / nauhoitus; lyhyempi riittää,
  kun `from_s` rajaa).
- Olemassa olevia `.expect`-tiedostoja ei löysätä.

## Valmis kun
`bash tools/run_native_tests.sh test_replay` on vihreä, jokaiselle luetellulle riville on
nauhoitus, ja sabotaasi on näytetty: yksi tahallinen virhe status.h:ssa (esim. rivin 9 raja)
kaataa vastaavan golden-testin.
