# S2 — DSP: voimakas positiivinen paluuaalto sotkee syklintunnistuksen

**Laji:** rakennus (ensin diagnoosi). **Laitekoodi:** kyllä (`firmware/src/core/dsp.h`).
**Riippuvuus:** WP-8 (replay) ja WP-10 ovat `dev/integraatio`ssa.

## Havainto (31.10.2026, WP-10:n raportti)
Simulaattoriasetuksilla `base=-30` ja `peak=40` laitteen DSP antaa tilan "Käynti epätasainen"
(rivi 2, rpm:n hajonta 22–26 %), kun taas `tools/mock_server.py` näyttää samoilla asetuksilla
"Lähellä". Voimakas positiivinen paluuaalto (`EngineSim` `peak_kpa`, engine_sim.h:53, rivi 10 /
H1) näyttää tuottavan ylimääräisiä tai puuttuvia syklirajoja.

Taustaa: GN-anturi leikkaa ilmanpaineessa (EngineSim `sensor_hi_mv_` 2700 mV,
engine_sim.h:453; jatko.md: penkkitestissä ylipaine nosti SYL 1:n vain 2726 mV:iin). Leikattu
paluuaalto näkyy siis tasaisena kattona nollan kohdalla.

## Tehtävä
1. Toista: `bash tools/replay/build.sh` ja
   `tools/replay/build/replay --sim rpm=900 base=-30 peak=40 --seconds 8 --out /tmp/h1.csv`,
   sitten `tools/replay/build/replay /tmp/h1.csv`. Kartoita, millä `peak`-arvoilla (0…60) ja
   kierrosluvuilla (600…3000) ongelma alkaa, useammalla siemenellä.
2. Selvitä, onko skenaario fyysisesti mahdollinen anturin leikkautumisen kanssa (rauta.md §1.3,
   SUUNNITTELU.md §2.3). Jos `peak` ylittää realistisen alueen, kirjaa raja ja perustelu PR:ään.
3. Etsi juurisyy dsp.h:n syklirajasta (hystereesi `hyst_frac`/`hyst_min_kpa`, alipäästö
   `lp_cutoff_hz`, katon leikkautuminen) ja korjaa realistisella alueella.
4. Lisää golden-nauhoitus (`firmware/test/data/recordings/`, ≤ 6 s) ja `.expect`, sekä
   kohdennettu test_dsp-testi.

## Pysyttävä totena
- Kaikki nykyiset test_dsp-, test_integration- ja test_replay-testit; WP-2c:n luvut
  (jatko.md: rpm-pudotus 0,5 s, blippaus 0,55 s, käynnistys 147 ms) eivät heikkene yli 10 %.
- DSP:n julkinen rajapinta (docs/firmware.md §4.2) ja `DspParams`in kentät.

## Valmis kun
Realistisella `peak`-alueella joutokäynti lukittuu (`rpm_cv` < `unstable_cv`) 8/8 siemenellä,
golden-testi on lisätty, ja PR kertoo kartoituksen taulukkona ennen/jälkeen.
**Käytännön testitaso:** simulaattori; seuraava taso on ensimmäinen moottorinauhoitus
(docs/moottoriajo.md), josta nähdään todellinen paluuaalto.
