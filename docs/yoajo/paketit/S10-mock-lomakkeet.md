# S10 — Mockin lomakejäsennys laitteen kaltaiseksi

**Laji:** rakennus (pieni). **Laitekoodi:** ei (`tools/mock_server.py`).
**Riippuvuus:** S8 on `dev/integraatio`ssa.

## Havainto (S8:n raportti 31.10.2026)
`tools/mock_server.py` on UI:n viitetoteutus PC:llä, mutta sen lomakejäsennys on laitetta
sallivampi:
- `sim_apply` hyväksyy `1_000`:n, NBSP- ja em-välilyönnit sekä arabialais-intialaiset
  numerot, jotka laite (`simApplyForm`, `firmware/src/core/`) hylkää.
- `1e400`:n mock hylkää, laite rajaa ylärajaan.
- Konfiguraatio: `1e39` antaa laitteella "Virheellinen arvo", mockissa "Arvo sallitun alueen
  ulkopuolella". U8-kenttään laite hyväksyy `15.00000001`:n lukuna 15 (float pyöristää),
  mock hylkää.

Laitteen käytös on totuus (docs/firmware.md §4.4 ja `firmware/test/test_fuzz/`).

## Tehtävä
Tee mockin jäsennyksestä laitteen kaltainen: sama merkistötarkistus kuin `config_set_text`in
`_NUM_CHARS`, ääretön rajataan laitaan kuten laitteella, ja virhetekstit samat. Lisää
Python-testi (`tools/test_*.py`), joka ajaa samat rajatapaukset kuin S8:n raportti. Jos jokin
laitteen käytös näyttää itse vialta (esim. U8-pyöristys), älä muuta laitetta: kirjaa se PR:n
**Havainnot**-kohtaan.

## Valmis kun
Kaikki yllä luetellut erot on poistettu tai perusteltu PR:ssä, ja `python3 -m unittest
discover -s tools -p 'test_*.py'` ja `mock_server.py --check-config` ovat vihreitä.
