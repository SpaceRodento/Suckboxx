# Työpaketit 31.10.2026 alkaen (WP-8…WP-16)

Lähtötila: master `2c63e86` (M1–M3, WP-7b, WP-2c, penkkitesti 31.10.2026). Native 12/12
kokonaisuutta vihreänä. Avoimet kohdat ovat [jatko.md](jatko.md):n listasta "Avoimet".
Integraatiohaara `dev/integraatio`; jokainen paketti omassa haarassaan `wp/<n>-<nimi>`, ja
orkestroija yhdistää. Tulokset ja luvut: [jatko.md](jatko.md) "Valmiit 31.10.2026". Seuraavat
koneella tehtävät työt ovat yöjonossa [yoajo/JONO.md](yoajo/JONO.md).

**Masteriin ilman laitetestiä (omistajan päätös 31.10.2026 iltapäivällä):** laitetestiä ei
saatu heti, joten `dev/integraatio` yhdistettiin masteriin ennen sitä. Sen tilalla ovat
riippumattomat katselmukset (S1, R13) ja WSL:n ASan/UBSan-portti jokaisen yhdistämisen
jälkeen. Laitetesti ([jatko.md](jatko.md) "Laitetesti 1") tehdään mahdollisimman pian, ja
löydökset korjataan masterin päälle.

Painopiste: kaikki, mitä voi tehdä ilman moottoria, tehdään nyt, jotta ensimmäinen
moottoriajo (M6) tuottaa käyttökelpoista dataa ja viritys (M7) on nopea.

| # | paketti | jatko.md | vaatii | tiedostot (omistus) | tila 31.10.2026 |
|---|---|---|---|---|---|
| WP-8 | Nauhoitusten toisto ja golden-vektorit | M7, R3 | — | `tools/replay/`, `firmware/test/test_replay/`, `firmware/test/support/recording.h`, `firmware/test/data/` | ✅ yhdistetty |
| WP-9 | Käytösmatriisin ja kalibroinnin jälkityöt | 7, 8 | — | `status.h` (vika-, drift- ja nollauslogiikka), `calibration.h`, `processor.h`, `config.h`, `types.h` (`StatusParams`) | ✅ yhdistetty; kynnykset omistajalle |
| WP-10 | Rakenteellinen neuvo, kanavatila ja mockin pariteetti | 5, 6 | — | `snapshot.h`, `status.h` (vain `verdict`), `types.h` (`StatusResult`), `web/index.html`, `web_page.h`, `mock_server.py` | ✅ yhdistetty |
| WP-11 | CI (GitHub Actions) | 6 | — | `.github/workflows/` | ✅ yhdistetty; ensimmäinen ajo pushin jälkeen |
| WP-12 | Moottoriajon kenttäpaketti | 1, 2 | — | `docs/moottoriajo.md`, `tools/sim_fit.py` | ✅ yhdistetty |
| WP-13 | HAL-huolto: ESPAsyncWebServer ja `myoh` | 9, 10 | ESP32-käännös; laitetesti | `src/hal/*`, `platformio.ini` | ✅ yhdistetty; katselmoitu (R13); laitetesti tekemättä |
| WP-14 | Dokumentaation yhtenäistys | — | muiden paketit | `docs/roadmap.md`, `SUUNNITTELU.md` §11, `jatko.md` (orkestroija) | ✅ tämä dokumentaatio |
| WP-15 | Omistajan päätökset kirjaksi (D15) | 5 | — | `types.h` (rivi 7), SUUNNITTELU.md §5/§8, `roadmap.md`, `firmware.md`, `moottoriajo.md` | ✅ yhdistetty |
| WP-16 | BF50A-huoltotiedot ja käyttöohjeen UI-löydökset | 2 | WP-15, S6 | `docs/bf50a-huoltotiedot.md`, `types.h` (master), `status.h` (neuvoteksti), `web/index.html`, `moottoriajo.md`, `kayttoohje.md`, `hankinnat.md` | käynnissä |
| S1 | Katselmus: WP-9 ja WP-10 | — | — | `docs/katselmukset/2026-10-31-status-wp9-wp10.md` | ✅ yhdistetty; ei korkeita löydöksiä |
| R13 | Katselmus: WP-13 (HAL, kirjastot 3.12.1/3.5.0) | — | — | `docs/katselmukset/2026-10-31-wp13-hal-kirjastot.md` | ✅ yhdistetty; ei korkeita löydöksiä |
| S2 | DSP: voimakas paluuaalto | 6 | WP-8 | `dsp.h`, DSP-testit | käynnissä |
| S6 | Käyttöohje mekaanikolle | — | — | `docs/kayttoohje.md`, `docs/kuvat/` | ✅ yhdistetty |
| S8 | Lomakkeiden ja CSV:n fuzzaus | — | WP-8 | `firmware/test/test_fuzz/` | ✅ yhdistetty; ei löydöksiä (ASan) |
| S9 | Konfiguraatio-JSONin puskuri | 8 | WP-9 | `config.h` (`kConfigJsonSize` johdetaan) | ✅ yhdistetty |

S-paketit tulivat yöjonosta ([yoajo/paketit/tehdyt/](yoajo/paketit/tehdyt)); ne tehtiin
päiväistunnossa, koska laitetestiä odottaessa koneella oli tilaa. R13 on päiväistunnon oma
katselmus, koska WP-13:n laitekoodi meni masteriin ilman laitetestiä.

## WP-8 — Nauhoitusten toisto ja golden-vektorit

Raakanauhoitus (`/api/raw.csv` tai `tools/serial_record.py`) ajetaan PC:llä täsmälleen saman
`Processor`-koodin läpi kuin laitteella. Tuloksena on tilan, rpm:n ja erojen aikasarja, ja
nauhoitus odotusarvoineen on regressiotesti (SUUNNITTELU.md §7 vaihe 4).

- Lukija molemmille CSV-muodoille, seq-aukot kuten laitteella.
- `tools/replay/`: `replay <csv>` → tikkikohtainen JSONL ja yhteenveto (aika tiloittain,
  lukitusaika, rpm ± hajonta, spreadin mediaani ja p95). `--set key=value` konfiguraatiolle.
  `--sim key=value --seconds N --out x.csv` kirjoittaa EngineSimin nauhoituksen samassa muodossa.
- `test_replay`: `firmware/test/data/recordings/*.csv` + odotustiedosto per nauhoitus.
  Aluksi synteettiset nauhoitukset (synkassa, lähellä, letku irti).

Hyväksyntä: oikea nauhoitus lisätään testiksi kopioimalla CSV ja kirjoittamalla odotukset,
ilman koodimuutosta. Samasta EngineSim-ajosta toisto ja `test_integration` antavat saman tilan.

## WP-9 — Käytösmatriisin ja kalibroinnin jälkityöt

- Rivi 6: `hose_off_kpa`:lle hystereesi (vika ei välky rajalla).
- Rivi 5: driftin tunnistus ei reagoi rpm-kohinaan, mutta 50 rpm / 2 s tunnistuu yhä.
- WP-4:n kolme selviävää mutaatiota (yksi auto-zero-portti FaultSensor-tilassa) tapetaan testeillä.
- Manuaalinen nollaus vaatii tason vakauden (vuotava alipaine ei kirjaudu offsetiksi).
- Automaattinen nollaus sammutuksen jälkeen 5,6–8 s → selvitetään, mikä osa on fysiikkaa
  ja mikä toteutusta, ja lyhennetään toteutuksen osuus.

Uudet kynnysarvot ovat ehdotuksia omistajalle, kunnes ne on kirjattu päätöslokiin.

## WP-10 — Rakenteellinen neuvo, kanavatila ja mockin pariteetti

- Snapshotiin `act: {ch, open}` (tai `null`) ja `ch[i].cond`. UI lakkaa jäsentämästä
  advice-tekstiä.
- `mock_server.py` mallintaa WP-2c:n statusmuutokset: letkuvian pitoaika 0,5 s,
  saturaatiovaroituksen pitoaika 1,2 s, yhteismuotoinen alipaineen muutos → "odota".

## WP-11 — CI

GitHub Actions: native-testit (ASan/UBSan), `pio test -e native`, ESP32-käännös
(`esp32dev`, `esp32dev_adc`, `esp32dev_hx`), `check_core_gcc84.sh`, `embed_web.py --check`,
`mock_server.py --check-config`. CI on portti myös yöajoille, jotka eivät voi ajaa pio:ta.

## WP-12 — Moottoriajon kenttäpaketti

- `docs/moottoriajo.md`: ensimmäisen moottoriajon pöytäkirja (Honda BF50A): raudan
  esivalmistelut, turvallisuus, tallennussuunnitelma komentoineen ja kestoineen,
  tiedostojen nimeäminen, päätöspisteet (H1, letkuresonanssi, rivi 9).
- `tools/sim_fit.py`: arvioi nauhoituksesta EngineSimin parametrit (pulssin syvyys,
  resonanssi, ristivuoto, kohina, jitter, H1-leikkautuminen) ja tulostaa ne `/api/sim`-muodossa.

## WP-13 — HAL-huolto

- ESPAsyncWebServer 3.6.0:n neljä tunnettua ongelmaa (jatko.md 10) tarkistetaan uudemmista
  versioista lähdekoodista; versionnosto käännettynä haarassa. Alun perin masteriin vasta
  laitetestin jälkeen; omistaja päätti 31.10.2026 yhdistää ennen laitetestiä (R13 katselmoi).
- `myoh` ei laske käynnistyksen ensimmäisen hitaan ADC-luvun jälkeisiä kiinniottoja.

## Omistajan päätettävät

Päätetty 31.10.2026 (D15, omistaja delegoi orkestroijalle): rivin 7 rajat 150 / 2950 mV,
WP-9:n kynnykset ja WP-12:n pöytäkirjan rajat alustavasti, `cond`-nimet sellaisinaan,
HX710B säilyy. Avoimet: [jatko.md](jatko.md) "Avoimet".
