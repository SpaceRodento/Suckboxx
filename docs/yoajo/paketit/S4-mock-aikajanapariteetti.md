# S4 — Mockin ja laitteen aikajanapariteetti automaattiseksi

**Laji:** rakennus (työkalu + CI). **Laitekoodi:** ei.
**Riippuvuus:** WP-8 ja WP-10 ovat `dev/integraatio`ssa.

## Miksi
`tools/mock_server.py` on Python-uudelleentoteutus laitteen statuslogiikasta, jotta UI:ta voi
kehittää ilman laitetta. `--check-snapshot` (WP-10) vertaa vain snapshotin muotoa. Käytöksen
eroja on löydetty käsin: WP-10 vertasi neljä tilannetta taulukkona, ja ero `base=-30 peak=40`
-asetuksilla (laite "epätasainen", mock "lähellä") löytyi vain sattumalta.

## Tehtävä
`tools/check_mock_parity.py`: samat skenaariot (sim-avaimet ja tapahtumat ajassa) ajetaan
(a) laitteen koodilla `tools/replay/build/replay --sim … --out x.csv` + `replay x.csv --jsonl`
ja (b) mockin simulaatiolla ilman HTTP-palvelinta (tuo mockin luokat). Vertaa tilojen
aikajanat: kussakin skenaariossa tilasarja ja siirtymähetket toleranssilla (esim. ±0,25 s).
Aloita WP-10:n taulukon neljästä tilanteesta ja lisää rivit 1, 6, 7, 9, 12. Ero raportoidaan
taulukkona; tunnettu ero merkitään odotetuksi eroksi viitteineen, ei piiloteta. S2
(31.10.2026) korjasi paluuaallon DSP:ssä, joten `base=-30 peak=40` pitäisi nyt täsmätä. Mock
käyttää simulaattorin valmiita syklirajoja eikä voi toistaa DSP:n syklintunnistuksen vikoja
(S2:n havainto); esim. `xtalk` + paluuaalto (S16) on siksi odotettu ero.
Lisää CI:n web-jobiin askel, joka kääntää replayn ja ajaa tarkistuksen.

## Pysyttävä totena
Mockin tekstit ja tilanimet tulevat status.h:sta (WP-10), eikä mockin käytöstä muuteta tässä
paketissa: löydetty ero kirjataan, ja jos se on mockin virhe, korjaus on oma committinsa
perusteluineen.

## Valmis kun
Tarkistus ajaa vähintään yhdeksän skenaariota, poistuu nollasta poikkeavalla koodilla
odottamattomasta erosta, ja sabotaasi on näytetty (mockin letkuvian pitoaika 0,5 → 1,0 s
kaataa tarkistuksen).
