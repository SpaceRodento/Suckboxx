# S5 — EngineSim: mitatut ADC-piikit simulaattoriin

**Laji:** rakennus. **Laitekoodi:** kyllä (`firmware/src/core/engine_sim.h`, `sim_api.h`).
**Riippuvuus:** WP-9 on `dev/integraatio`ssa.

## Mitattu totuus (ei saatavilla yöllä muualta)
jatko.md, penkkitesti 30.–31.10.2026 (`tools/serial_record.py`, 1 kHz, protolevy ilman
pinnien 100 nF:ää): kohina 3,5 mV RMS levossa (3,8–4,1 mV puhelin yhdistettynä), lisäksi
satunnaisia **±80–90 mV:n yksittäisiä piikkejä noin kerran sekunnissa** kanavaa kohden.
EngineSim mallintaa nyt vain gaussisen kohinan (`noise`).

## Miksi
WP-9:n nollauksen vakausportti (`zero_settle_kpa` 0,1 kPa / 1 s) ja letkuvian hystereesi on
viritetty simulaattorilla ilman piikkejä; WP-9 kirjasi tämän avoimeksi riskiksi. Piikki on
80–90 mV ≈ 3,5 kPa yhdessä näytteessä.

## Tehtävä
Lisää EngineSimiin piikkimalli (amplitudi mV ja taajuus /s, kanavakohtaisesti riippumaton) ja
`/api/sim`-avaimet (lisäys, ei olemassa olevan muutosta; docs/firmware.md §5:n avainlista ja
`tools/mock_server.py`:n vastine). Oletus 0 = nykyinen käytös. Mittaa piikeillä
(80–90 mV, 1/s): automaattisen nollauksen onnistuminen ja aika, väärät letkuvika- ja
drift-liput, lukitusaika ja D3-erotuksen hajonta. Lisää testit, jotka lukitsevat, ettei
mitattu piikkitaso riko näitä.

## Pysyttävä totena
`noise`-avaimen ja muiden avainten merkitys ennallaan; vanhat siemenet tuottavat samat
näytteet, kun piikit ovat pois (test_engine_sim).

## Valmis kun
Testit ajettu piikeillä ja ilman, ja PR:ssä on taulukko mitatuista vaikutuksista. Jos jokin
WP-9:n kynnys ei kestä mitattua piikkitasoa, se on **Kysymykset**-kohta (kynnys on omistajan),
ei hiljainen muutos.
