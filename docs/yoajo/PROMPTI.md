# Yöajon kehote (Suckboxx)

Jokaisen Suckboxxin yöajon ainoa kehote. Ajastetun rutiinin konsoliteksti on lyhyt: se kertoo
slotin ja käskee lukemaan tämän tiedoston **`origin/dev/integraatio`sta**. Kopio muualla ei ole
voimassa, eikä yöajo voi muuttaa omaa kehotettaan, koska `dev/integraatio`on yhdistää vain
päiväistunto (Y3, Y11). Malli on Zignalmeisterin `docs/yoajo/PROMPTI.md`:stä (31.10.2026),
kevennettynä: yöllä vain toteutetaan, ja päiväistunto katselmoi ja yhdistää.

**Käynnistys (31.10.2026 alkaen käsin, ajastettuja rutiineja ei ole):** avaa claude.ai:ssa
pilvisessio repoon `SpaceRodento/Suckboxx` ja anna kehotteeksi:

> Lue docs/yoajo/PROMPTI.md haarasta origin/dev/integraatio ja toimi sen mukaan. Tee yksi paketti.

Useampi rinnakkainen sessio on sallittu: varaus (Y4) estää päällekkäisyyden.

---

## Yhteiset säännöt

Y1. **Kukaan ei ole paikalla.** Kukaan ei vastaa kysymyksiin, eikä mikään tekemäsi koske
    rautaa tai masteria. Ei AskUserQuestionia.

Y2. **`gh` ei ole asennettu.** Älä kutsu äläkä asenna sitä. GitHub-toiminnot tehdään
    komennolla `python3 tools/gh_api.py` (vain stdlib, käyttää ympäristön GH_TOKENia;
    `--help` listaa komennot).

Y3. **Haara ensin, lukeminen sitten.** Yön työ haarautuu `dev/integraatio`sta, ei masterista:

        git fetch origin
        git checkout -B dev/integraatio origin/dev/integraatio

    Älä pushaa `dev/integraatio`on äläkä masteriin. Lue sitten docs/yoajo/JONO.md tästä
    puusta.

Y4. **Valitse paketti, varaa se heti, lue vasta sitten.** Ota JONO.md:n jonotaulukon ylin
    rivi, jonka riippuvuus on täyttynyt (yhdistetty `dev/integraatio`on) ja jota ei ole
    varattu. Paketti on varattu, jos `git ls-remote --heads origin "yo/<ID>-*"` tulostaa
    jotain tai `python3 tools/gh_api.py find --head yo/<ID>` löytää PR:n. Varaa näillä
    kolmella komennolla peräkkäin:

        git ls-remote --heads origin "yo/<ID>-*"
        git push origin origin/dev/integraatio:refs/heads/yo/<ID>-<slug>
        git ls-remote --heads origin "yo/<ID>-*"

    Varaus on sinun vain, jos push tulosti "[new branch]" ja toinen ls-remote näyttää vain
    sinun haarasi. Muuten poista oma haarasi (ei koskaan toisen) ja valitse uudelleen. Sitten
    `git checkout -b yo/<ID>-<slug> origin/dev/integraatio` ja **lue vain sen paketin oma
    tiedosto** `docs/yoajo/paketit/<ID>-*.md` ja sen nimeämät lähteet.
    Jos yksikään paketti ei ole otettavissa, älä rakenna mitään: kirjoita yksi ehdotus
    JONO.md:n Ehdotukset-osioon sen määräämässä muodossa (tarkista ensin, ettei samaa ole jo
    ehdotettu) ja toimita se PR:nä haarasta `yo/ehdotus-<ISO-päivä>-<slug>`.

Y5. **Laitekoodikatto: enintään 2 laitekoodipakettia yössä.** Laitekoodi on mikä tahansa
    tiedosto `firmware/src/`:ssä. Jokainen tällainen paketti vaatii seuraavana päivänä
    ihmisen laitetestin. Ennen kuin varaat paketin, jonka tiedosto nimeää laitekoodia, laske
    viimeisen 12 tunnin avoimet PR:t (`gh_api.py prs --base dev/integraatio` ja
    `gh_api.py files <n>`). Jos niitä on kaksi tai enemmän, ohita paketti ja kerro se.

Y6. **Portit. PlatformIO ei ole käytettävissä: älä asenna äläkä aja `pio`:ta.** Aja itse:

        bash tools/run_native_tests.sh 2>&1 | tail -5         # g++, ASan/UBSan, -Werror
        bash tools/replay/build.sh && for f in firmware/test/data/recordings/*.csv; do \
          tools/replay/build/replay "$f" --expect "${f%.csv}.expect" | tail -1; done
        python3 tools/embed_web.py --check
        python3 tools/mock_server.py --check-config
        python3 tools/mock_server.py --check-snapshot
        python3 -m unittest discover -s tools -p 'test_*.py' 2>&1 | tail -3

    ESP32-käännöksen ja GCC 8.4 -tarkistuksen (`check_core_gcc84.sh`) ajaa CI jokaisesta
    pushista `yo/**`-haaraan. Lue tulos: `python3 tools/gh_api.py checks <PR>`. Jos `esp32`-
    tai GCC 8.4 -job on punainen, korjaa ennen kuin lopetat. Älä koskaan väitä ajaneesi
    testiä, jota et ajanut.

Y7. **Toimitus aikaisin, tässä järjestyksessä:** varaus → ensimmäinen commit → push → PR heti
    ensimmäisen commitin jälkeen, ei vasta lopussa:

        python3 tools/gh_api.py create --base dev/integraatio --head yo/<ID>-<slug> \
          --title "<ID>: <otsikko>" --body-file <tiedosto>

    PR:n otsikossa on paketin tunnus. Runko vastaa viiteen otsikkoon: **Mitä tehtiin** /
    **Todennus** (yhteenvetorivit, myös sabotaasi: mikä testi kaatuu, kun korjaus poistetaan) /
    **Laitetesti tarvitaan** (mitä ihmisen pitää tarkistaa laitteella, tai "ei") /
    **Poikkeamat paketista** / **Havainnot päiväistunnolle** (huomattu, ei korjattu). Jos
    `create` epäonnistuu, yritä kolmesti ja jätä haara pushatuksi. Työ ei saa jäädä vain
    session muistiin.

Y8. **Mikä on sinun päätettävissäsi.** Mekanismi on sinun: koodin muoto, nimet, testien
    rakenne, mitattava ja muutettava oletus (puskurin koko, aikaraja). Tarkoitus on ihmisen:
    mitä laite näyttää käyttäjälle, kynnykset, joissa on arvovalinta (D2:n toleranssit,
    käytösmatriisin rajat), julkaistun rajapinnan rikkova muutos ja kaikki rautaa koskeva.
    Päätökset ovat SUUNNITTELU.md §5:n päätöslokissa (D-numerot) ja käytösmatriisissa (§8);
    älä kumoa niitä.
    Ihmisen kysymys menee PR:n otsikon **Kysymykset** alle vaihtoehtoineen ja suosituksineen,
    ja otat sillä välin turvallisimman peruttavan vaihtoehdon.

Y9. **Testi on väite.** Jokaiselle uudelle assertille on päättäjä: D-numero, käytösmatriisin
    rivi, docs/firmware.md:n sopimus tai mitattu arvo. Jos arvo on oma ehdotuksesi, sano se
    kommentissa ja PR:ssä. Kun nojaat kirjaston tai raudan käytökseen, nimeä lähde (tiedosto
    ja rivi).

Y10. **Tyyli ja kieli.** docs/firmware.md §1: kommentit suomeksi, tunnisteet englanniksi,
    2 välilyöntiä, `namespace sbx`, float eikä double, GCC 8.4 / C++17, ei dynaamista muistia
    näytepolulla. Commitit, PR ja LOKI-rivi suomeksi. Päivämäärät: commit-viesteissä,
    tiedostonimissä ja LOKI-taulukossa ISO 8601 (2026-11-01), proosassa 1.11.2026; ei
    koskaan kuukautta ensin. Commit-viestin loppuun yöajon mallin Co-Authored-By-rivi.

Y11. **Turvallisuus.** Ei flashausta, ei sarjaporttia, ei `pio device`a. Ei pushia masteriin
    eikä `dev/integraatio`on, ei force-pushia, ei `--no-verify`, ei muutoksia `.claude/`-
    kansioon eikä tähän tiedostoon. Ei viestejä kolmansille osapuolille (ei kirjastojen
    issueita tai PR:iä).

Y12. **Tokenikuri.** Älä päästä käännös- tai testilokia kontekstiin: putkita ja lue
    yhteenveto (`| tail -25`). Etsi symbolia grepillä äläkä lue isoa tiedostoa kokonaan
    (`dsp.h` ~1000 riviä, `status.h` ~700, `mock_server.py` ~1500).

Y13. **Jos estyt:** commitoi valmis osa, pushaa, kuvaa este PR:ssä otsikon **Estynyt** alla,
    kirjoita LOKI-rivi ja lopeta. Älä arvaa äläkä vaihda toiseen tehtävään kesken.

Y14. **LOKI-rivi.** Lisää ennen lopetusta omaan haaraasi yksi rivi `docs/yoajo/LOKI.md`:n
    taulukon loppuun ja pushaa se samaan PR:ään:

        | <ISO-päivä> | <ID tai tyhjä> | <valmis|osittain|estynyt|tyhjä> – yksi lause, jota PR:n otsikko ei kerro | #<PR> |

    Tulossolu enintään 400 merkkiä. Jos edellinen yö ei kirjoittanut riviä, mainitse se.
    Jätä työpuu puhtaaksi.

Laatu ja testikattavuus ennen nopeutta. Kerro rehellisesti, mikä jäi kesken.
