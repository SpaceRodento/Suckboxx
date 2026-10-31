# S16 — DSP: ristivuoto ja paluuaalto yhdessä monistavat rpm:n

**Laji:** rakennus. **Laitekoodi:** kyllä (`firmware/src/core/dsp.h`).
**Riippuvuus:** S2 on `dev/integraatio`ssa.

## Havainto (S2:n raportti 31.10.2026)
S2 korjasi pelkän paluuaallon: syklin raja pysyy imupulssissa paluuaallon tasanteella
(dsp.h, tasannetila, `kPlateau*`-vakiot). Vanha vika jäi:
- `xtalk=0.3` ja `peak` ≥ 20 1500 rpm:ssä antavat rpm:n 2,5–2,8-kertaisena (cv-ehto menee
  silti läpi, joten tila voi näyttää vihreää väärällä rpm:llä).
- `base=-30 xtalk=0.3 peak=60` 600–900 rpm:ssä ei anna rpm:ää lainkaan.

S2:n kartoitusvaljaat (`sweep.cpp`, `bench.cpp`) eivät ole repossa; replay-CLI:n
`--sim`-vienti ja `test_dsp`in `test_return_wave_plateau_keeps_lock` ovat lähtökohta.

## Tehtävä
1. Kartoita ensin: rpm 600–3000, `xtalk` 0–0,4, `peak` 0–60, vähintään 8 siementä. Tee
   taulukko lukituksesta ja rpm-virheestä (docs/firmware.md §4.2:n tyyliin).
2. Selvitä juurisyy (tiedosto:rivi). Jos korjaus on mekanismi (Y8), toteuta se niin, ettei
   yksikään S2:n, WP-2c:n tai golden-toistojen luku muutu. Jos korjaus vaatisi
   arvovalinnan (esim. rpm-alueen rajaus), se on **Kysymys**.
3. Tärkein turva-asia: väärä rpm vihreällä tilalla. Jos täyttä korjausta ei löydy,
   harkitse, voiko DSP tunnistaa monistuneen rpm:n (esim. kanavien välinen jaksosuhde) ja
   pudottaa tilan "epätasainen"-tilaan. Päättäjä: käytösmatriisin rivi 2 ja D12.

Tasauskanavista ei ole tietoa (D6). BF50A:ssa SE-letkut puristetaan mittauksen ajaksi kiinni
(D16), joten todellinen ristivuoto voi olla pieni. Kerro PR:ssä, minkä `xtalk`-arvon
ensimmäinen moottorinauhoitus (`tools/sim_fit.py`) ratkaisee.

## Valmis kun
Kartoitustaulukko on PR:ssä, ja joko korjaus testeineen tai perusteltu kysymys omistajalle.
Native-testit, golden-toistot ja CI:n ESP32-käännös vihreinä.
