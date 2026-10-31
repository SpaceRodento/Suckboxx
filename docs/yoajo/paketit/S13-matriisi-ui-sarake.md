# S13 — Käytösmatriisin UI-sarake vastaan koodi, ja rivi 11

**Laji:** katselmus (tuottaa ehdotuksen). **Laitekoodi:** ei.
**Riippuvuus:** S6 on `dev/integraatio`ssa.

## Havainto (S6:n raportti 31.10.2026)
SUUNNITTELU.md §8:n sarakkeen "mitä UI näyttää" tekstit poikkeavat koodin todellisista
teksteistä (`firmware/src/core/status.h`:n `writeText`-kutsut, `web/index.html`). Lisäksi
§8:n rivin 11 tilannetta "anturi vaihdettu" koodi ei tunnista lainkaan. docs/kayttoohje.md
kuvaa koodin todelliset tekstit.

## Tehtävä
1. Tee taulukko: §8:n rivi, §8:n teksti, laitteen todellinen otsikko/neuvo (tiedosto:rivi),
   ja ero luokiteltuna (sanamuoto / merkitys / puuttuu).
2. Rivi 11: selvitä, mitä §8 lupaa, mitä koodi tekee nyt (esim. kalibroinnin mitätöinti,
   NVS) ja mitä toteutus vaatisi. Ehdota vaihtoehdot suosituksineen.
3. Älä muuta §8:aa äläkä koodia: sanamuotoerot ja rivi 11 ovat omistajan päätettäviä (Y8).
   Tuotos: `docs/katselmukset/<ISO-päivä>-matriisi-ui-sarake.md` ja PR, jonka **Kysymykset**
   listaa päätettävät.

## Valmis kun
Jokaiselle §8:n riville on taulukossa rivi, ja rivin 11 vaihtoehdot on perusteltu.
