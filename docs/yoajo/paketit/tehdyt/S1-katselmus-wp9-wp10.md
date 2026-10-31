# S1 — Riippumaton katselmus: WP-9:n ja WP-10:n status- ja kalibrointimuutokset

**Laji:** katselmuspaketti (löydöksiä, ei koodimuutoksia). **Laitekoodi:** ei.
**Riippuvuus:** WP-9 ja WP-10 ovat `dev/integraatio`ssa (31.10.2026).

## Miksi
31.10.2026 kaksi rinnakkaista aliagenttia muutti käytösmatriisin ydintä samassa tiedostossa
(`firmware/src/core/status.h`): WP-9 vika-, drift- ja nollauslogiikan, WP-10 kanavaluokituksen
(`ch_cond`) ja neuvon (`act`). Orkestroija katselmoi kummankin erikseen ja ajoi portit, mutta
kukaan ei ole lukenut yhdistettyä lopputulosta käytösmatriisia vasten. Katselmoijan on oltava
eri sessio kuin kirjoittajan; yöajo on.

## Mitä luetaan
- `git log --oneline 2c63e86..origin/dev/integraatio -- firmware/src/core/` ja diffit
  `git diff 2c63e86 origin/dev/integraatio -- firmware/src/core/status.h calibration.h processor.h types.h snapshot.h`
- SUUNNITTELU.md §5 (D1–D14) ja §8 (käytösmatriisi, suunnittelusääntö: vikatilat voittavat
  säätötilat, ei koskaan vihreää epäluotettavalla kanavalla), docs/firmware.md §4.3, §4.7, §5,
  docs/paketit.md (WP-9, WP-10).

## Kysymykset, joihin vastataan
1. Voiko jokin uusi polku näyttää vihreän (`synced`), kun kanava on epäluotettava
   (rivit 6, 7, 12, D11)? Erityisesti `hose_hyst_kpa`:n poistumishystereesi: voiko vika
   poistua ja tuomio näkyä ennen kuin kanava on oikeasti kunnossa?
2. Vastaako `ch[i].cond` aina tilaa: jos `state` on `fault_hose`, onko vikakanavan cond
   `hose`; jos `synced`, ovatko kaikki ei-master-kanavat `ok` tai `sat`?
3. Asetetaanko `act` vain silloin, kun advice-teksti on ruuvineuvo, ja nollautuuko se joka
   päivityksessä?
4. Nollauksen vakausportti (`zero_settle_kpa`, `NotSettled`): voiko automaattinen nollaus
   jäädä kokonaan tapahtumatta realistisella kohinalla (jatko.md: 3,5 mV RMS ja ±80–90 mV
   piikit ~1/s)? Arvioi taso-EMA:n (`level_tau_s`) vaimennus piikeille laskemalla.
5. Driftin suodatus (`drift_tau_s`): pitääkö rivin 5 "> 50 rpm / 2 s" yhä paikkansa
   sanamuotona, vai muuttuiko käytös niin, että §8:n rivi pitää päivittää (WP-9 ehdotti
   lisäystä "(suodatettu rpm)")?
6. Testit: kuka päätti kunkin uuden assertin (D-numero, rivi, firmware.md)? Listaa assertit,
   joiden ainoa päättäjä on toteuttaja.

## Tuotos
`docs/katselmukset/2026-xx-xx-status-wp9-wp10.md`: löydökset numeroituina, kukin tiedosto ja
rivi, vakavuus (korkea/keski/matala) ja ehdotettu korjaus. Älä korjaa koodia tässä paketissa;
korkean vakavuuden löydöksestä kirjoita JONO.md:n Ehdotukset-osioon uusi paketti.

## Valmis kun
Kaikkiin kuuteen kysymykseen on vastaus perusteluineen, ja native-testit on ajettu
(`bash tools/run_native_tests.sh`), jotta katselmus koskee vihreää puuta.
