<!-- Aivoriihi 31.10.2026: neljä näkökulmaa (mekaanikko, signaali, rauta, käytettävyys) + synteesi. Ideoita, ei päätöksiä; päätökset kirjataan SUUNNITTELU.md:hen. -->

# Suckboxx: mihin laite voisi pystyä (aivoriihen yhteenveto)

Neljän näkökulman (mekaanikko, signaali, rauta, käytettävyys) noin 70 raakaideasta on yhdistetty päällekkäiset. Ne, joiden fysiikka ei toimi näillä antureilla, on karsittu. Loput on järjestetty sen mukaan, mitä hyötyä niistä on sinulle ja tälle moottorille ja kuinka helposti ne saa tehtyä.
Merkinnät: **S** = päiviä, **M** = 1–3 viikkoa harrastusaikaa, **L** = enemmän. **[spek.]** = fysiikka on uskottava, mutta toimivuus pitää todentaa oikealla datalla.

---

## TOP 7

1. **Äänituki säätöön** (S, vain ohjelmisto). Säädettävän sylinterin Δ kuuluu äänenä, joten katse voi pysyä ruuvissa. Tämä nopeuttaa itse säätötyötä eniten koko listalla.
2. **Letkutarkistus 30 sekunnissa** (S, ohjelmisto). Laite huomaa ristiin menneet letkut ja vuotavan nipan, ennen kuin se antaa varman näköisen mutta väärän ruuvineuvon.
3. **Lämpenemisportti ja ylikuumenemisvahti GY-906:lla** (S, osa on jo hankittu). Synkka tehdään vasta lämpimälle koneelle, ja huuhtelusovittimella ajettaessa laite valvoo lämpöä. Tämä antaa GY-906:lle selvän tehtävän.
4. **Ohjattu synkkatyö ja asettumisilmaisin** (S + M). Laite kertoo työvaiheet oikeassa järjestyksessä, ilmoittaa milloin lukemaan voi luottaa ja milloin työ on valmis. Ohjeen ei tarvitse olla muistissa kauden yli.
5. **Kalibrointi ilman T-haaraa** (S). Letkut vaihdetaan ristiin ja 60 ml:n ruisku toimii alipainelähteenä. Kalibroinnin voi tehdä laiturilla oikeassa toimintapisteessä.
6. **Sylinterinkatkaisukoe** (S, tarvitaan kipinätesteri n. 10 €). Kierrosluvun pudotus mitataan numeroina sylinteri kerrallaan. Heikko sylinteri löytyy ennen kuin kaasuttimiin kosketaan.
7. **Huoltoloki ja ennen/jälkeen-raportti** (M). Joka kerta tallentuu, paljonko kutakin kaasutinta jouduttiin korjaamaan. Kun sama kaasutin karkaa vuodesta toiseen, syy on todennäköisesti kulunut läpän akseli tai vivusto.

**Kärkiehdokas M6-datan jälkeen:** sytytyskatkolaskuri ja sylinterikohtainen tehopanos (ryhmä D). Arvo on suuri, mutta se pitää ensin todentaa.

### Mitä tämä vaatii roadmapilta
- **M3:** Snapshotiin tarvitaan `act_ch`/`act_dir`, `ch.cond`, DSP:n oppima järjestys (`order_`) ja sylinterikohtaiset pulssien aikaleimat. Nämä avaavat kohdat 1, 2 ja 4. Samalla pitää päättää ajastin- tai I2S-DMA-näytteenotosta (D8), koska katkolaskuri vaatii näytevälin jitteriksi alle noin 50 µs.
- **M4:** Äänituki testataan oikeilla iOS- ja Android-puhelimilla http-yhteyden yli. Tarkistettavat asiat: ääni lähtee vasta napautuksen jälkeen, suomenkielinen puhesyntetiikka, näytön sammuminen (Wake Lock ei toimi http:llä).
- **M5:** Protolevylle kannattaa lisätä samalla akkujännitteen jakaja GPIO32:een ja I²C-liitin (GPIO21/22) GY-906:lle. Nyt ne ovat halpoja, myöhemmin hankalia.
- **M6:** Nauhoitusprotokolla, jossa jokainen vaihe on oma 8 s:n CSV-tallenne:
  - lämmin tyhjäkäynti
  - jokainen letku puristettuna vuorollaan
  - yksi tahallinen ¼-kierros yhteen ruuviin
  - sylinterinkatkaisu kolme kertaa noin 1500 rpm:llä
  - kaasunnykäys
  - pyöritys hätäpysäytysklipsi irti
  - lämpökäyrä GY-906:lla

  Jos mahdollista, mukaan sytytyspulssianturi kierrosluvun vertailuarvoksi. Tämä data ratkaisee ryhmän D kohtalon.
- **WP-2:** Kaasunnykäys ja kierrosluvun putoaminen pitää korjata ennen kaikkia ideoita, jotka käyttävät kaasun muutoksia (velhon nykäysvaihe, synkka joutokäynnin yläpuolella).

---

## A. Ohjelmistolla, pieni työ: seuraavaksi MVP:n jälkeen

- **Äänituki (piippaus ja puhe)** (S)
  - Saat: piippaustahti nopeutuu Δ:n kasvaessa, äänenkorkeus kertoo suunnan (avaa/sulje), tasainen ääni tarkoittaa vihreää aluetta, ja "ding" kuuluu, kun lukema on pysynyt vihreällä 3 s. Valinnaisesti puhe, esim. "kolmonen, avaa vähän".
  - Miksi toimii: Δ tulee jo SSE:nä, ja selain tuottaa äänen Web Audiolla ja puheen speechSynthesisillä.
  - Riski: joutokäynnin melu on 75–85 dB, joten tarvitaan kuulokkeet tai BT-kaiutin. Ääni reagoi noin 1 s viiveellä (avg_cycles 8), joten ruuvia käännetään rauhassa.
- **Letkutarkistus** (S)
  - Passiivinen tarkistus: sytytysjärjestys määrää kanavien syklisen järjestyksen. Kun kaksi letkua on ristissä, järjestys kääntyy, ja DSP oppii sen jo nyt (`order_`).
  - Aktiivinen tarkistus: velho pyytää puristamaan letkua 1 kolmen sekunnin ajan. Pulssit katoavat siitä kanavasta, josta mapping varmistuu. Jos taso valuu kohti nollaa, nippa vuotaa (esim. "letku 2 vuotaa 6 kPa/s").
  - Riski: jos kaikki kolme letkua on kierretty yhden pykälän (1→2→3→1), passiivinen tarkistus ei huomaa sitä, mutta puristuskoe huomaa. Sytytysjärjestys pitää tarkistaa huolto-ohjeesta.
- **Kalibrointi letkunvaihdolla ja ruiskulla** (S)
  - Saat: kanavien omat virheet erotetaan todellisista sylinterieroista oikeassa toimintapisteessä, ilman pumppua tai T-haaraa.
  - Miksi toimii: kyseessä on metrologian kääntömenetelmä. Kun letkut vaihdetaan laatikon päässä ristiin, todellinen ero vaihtaa etumerkkiä mutta laitteen oma virhe ei. Vahvistus (gain) kalibroidaan 60 ml:n ruiskulla T-haarassa: Boylen lain mukaan noin 15 ml:n kuollut tilavuus ja 15–20 ml:n veto antavat −40…−55 kPa.
  - Riski: letkujen vaihto käyvältä koneelta vuotaa ilmaa imukanavaan, joten vaihdetaan sammutettuna tai puristimilla. Lisäksi pitää päättää, kelpaako tämä D11:n kalibroinniksi.
- **Asettumisilmaisin** (S)
  - Saat: jokaisen Δ:n viereen "asettuu… / asettunut" ja suuntanuolen. Aloittelijan yleisin virhe, liian aikaisin luettu lukema ja liian iso käännös, jää pois.
  - Miksi toimii: Δ:n kulmakerroin lasketaan noin 2 s:n ikkunassa. Syklikeskiarvon kohina on noin 0,03 kPa, joten kulmakerroin on luotettava.
  - Riski: asettumisaika on arvaus, kunnes M6-data on olemassa.
- **Sylinterinkatkaisukoe** (S)
  - Saat: velho ohjaa maadoittamaan yhden sylinterin kipinätesterillä 5–10 s ajaksi ja toistaa tämän joka sylinterille. Tuloksena taulukko kierrosluvun pudotuksesta ja alipaineen muutoksesta per sylinteri. Samalla varmistuu, mikä kanava on missä sylinterissä.
  - Miksi toimii: kierrosluvun mittaus on jo nyt paljon tarkempi kuin moottorin oma vaihtelu.
  - Riski: tulpanhattua ei saa irrottaa käyvästä CDI-moottorista, joten käytetään maadoitettua testeriä. Koe ajetaan noin 1200–1500 rpm:llä, jottei kone sammu, ja katkaisut pidetään lyhyinä, koska kaasutin pumppaa polttoainetta kuolleeseen sylinteriin.
- **Joutokäynti- ja seosruuviavustin** (S)
  - Saat:
    - iso, 3–5 s keskiarvoistettu rpm ja ohjealue
    - huippuarvon pito ja "pudotus huipusta" seosruuvin laihan pudotuksen menetelmää varten
    - huojunnan tunnistus (0,2–2 Hz:n aaltoilu kierrosluvussa = laiha seos tai imuvuoto)
  - Miksi toimii: 3 s:n keskiarvolla rpm erottuu noin ±2–3 rpm:n tarkkuudella, ja seosruuvin huippu muuttaa kierroslukua 10–50 rpm.
  - Riski: seosruuveissa voi olla rajoitinhatut (EPA-aikakausi). Koneen pitää olla täysin lämmin, ja synkka tarkistetaan seossäädön jälkeen uudelleen.
- **Imuvuodon etsintätila** (S)
  - Saat: perustaso lukitaan, ja laite piippaa ja sanoo sylinterin, kun suihkutat imusarjan eristeiden, kaasuttimen liitosten ja synkkanippojen ympärille.
  - Miksi toimii: läpän jälkeisestä vuodosta imeytyvä suihke muuttaa sen sylinterin seosta, ja kierrosluku ja sen imukanavan keskiarvo hyppäävät.
  - Riski: palovaara veneessä, joten ensin vesisuihke ja jarrunpuhdistinta vain minimaalisesti. Pienet vuodot voivat jäädä kohinan alle.
- **Puhelinkäytön kitkat pois** (S)
  - Saat:
    - fokusnäkymä: valitulle sylinterille iso ▲ AVAA / ▼ SULJE ja Δ
    - aurinkokontrasti, kosketuslukko ja näytön valveilla pito videotempulla
    - QR-tarra Wi-Fiin ja osoitteeseen
    - WPA2-salasana ja PIN huoltosivulle (satama)
    - 2–3 puhelinta yhtä aikaa (toinen kaasulla)
  - Riski: valveilla pito videotempulla voi hajota selainpäivityksessä.
- **OTA-päivitys ja asetusten varmuuskopio** (S)
  - Saat: firmware päivittyy puhelimen kautta, joten suljettua koteloa ei tarvitse avata. Asetukset ja kalibrointi saa talteen JSONina.
  - Miksi toimii: oletusosiointitaulussa on jo kaksi OTA-paikkaa.
  - Riski: RAM-varaa on vähän rengaspuskurin ja AsyncWebServerin rinnalla. Palautus vanhaan versioon (rollback) on pakollinen.

## A2. Ohjelmistolla, isompi työ

- **Ohjattu synkkavelho ja korjauskortit** (M)
  - Saat työvaiheet järjestyksessä:
    1. ennakkotarkistus: jäähdytysvesi, vapaa-asento, letkut
    2. nollaus
    3. letkutarkistus
    4. lämpeneminen
    5. joutokäynti ensin
    6. ruuvi kerrallaan: kuva, kiertosuunta ja live-Δ, ja velho siirtyy eteenpäin, kun lukema on ollut vihreällä 3 s
    7. kaasunnykäys ja uusintatarkistus
    8. joutokäynnin uusintatarkistus
    9. raportti
  - Vikatiloissa paljaan punaisen otsikon tilalle tulee kortti, jossa on syy, toimenpide ja "testaa uudelleen". BF50A-profiili pysyy merkittynä "varmistamaton", kunnes tiedot on tarkistettu huolto-ohjeesta.
  - Riippuvuudet: M3 ja WP-2 (nykäysvaihe).
- **Huoltoloki, raportti ja huoltomuistutukset** (M)
  - Saat: sessiot tallentuvat LittleFS:ään (puhelimen kello, koska laitteessa ei ole RTC:tä). Mukana ennen/jälkeen-Δ, rpm ± hajonta, min/p2p, käyttötunnit ja muistiinpanot. Raportti on yksi offline-HTML, jonka sisään CSV on upotettu, ja sen voi jakaa mekaanikolle. Huoltovälit (siipipyörä, vaihteistoöljy, tulpat, venttiilivälys) saa kalenteriin .ics-tiedostona.
  - Riski: sää heiluttaa joutokäynnin alipainetta noin ±1,6 kPa. Siksi trendit lasketaan sylinterien välisistä eroista tai BME280:n kanssa. Huoltovälit otetaan ohjekirjasta, ei arvata.
- **Pitkä tallennus puhelimeen ja uudelleentoisto** (M)
  - Saat: 1 kHz raakadataa minuuttikaupalla (noin 6 kt/s, 10 min ≈ 3,6 Mt) 8 s:n sijaan. `src/core/` on riippuvuudetonta C++17:ää, joten sama DSP pyörii PC:llä tai WASMina selaimessa.
  - Arvo: tekee M6/M7:n helpommaksi ja jokaisesta käynnistä regressiotestin.
  - Riski: puhelin keskeyttää taustavälilehden.
- **Sylinterien aaltomuodot päällekkäin** (M)
  - Saat: 32–64 syklin synkroninen keskiarvo 720°:n akselilla, kolme käyrää päällekkäin. Mekaanikko näkee heti, mikä sylinteri poikkeaa.
  - Kuva on varma, mutta sen tulkinta on ryhmää D.
  - Riski: tasauskanavat (jos niitä on) sekoittavat muodot.
- **Synkka joutokäynnin yläpuolella ja vivuston välys** (M)
  - Saat: hidas pyyhkäisy 900→2500→900 rpm vapaalla. Avautuessa ja sulkeutuessa mitattujen Δ:iden ero kertoo vivuston välyksestä. Joutokäynnille palaamisen aika ja taso paljastavat jumittavan kaasuvaijerin.
  - Riippuvuus: WP-2-korjaukset.
  - Huom: Honda määrittää synkan todennäköisesti vain joutokäynnille, joten tämä on diagnoosi eikä säätötavoite.

## B. Halvat rautalisät, jotka kannattaa

Pinnibudjetti: ADC1:stä on vapaana vain GPIO32 ja GPIO33. I²C, HX710B ja sytytyspulssit ovat digitaalisia eivätkä kilpaile niistä.

- **GY-906 (MLX90614-DCI, jo hankittu): lämpenemisportti ja ylikuumenemisvahti** (S)
  - Saat: synkkatuomio odottaa, kunnes termostaattikotelon lämpö tasaantuu (termostaatti auki). Huuhtelusovittimella hälytys tulee, jos lämpö jatkaa nousua tasanteen jälkeen (sovitin luiskahtanut tai siipipyörä rikki).
  - Miksi toimii: anturi kytketään I²C:hen GPIO21/22:een, ja ajuri löytyy jo PlantMeisteristä. Mittauskohtaan tarvitaan musta teippipala, koska paljaan alumiinin ε on noin 0,1. Tasanne tunnistetaan dT/dt:stä, joten emissiivisyysvirhe ei haittaa.
  - Ilman anturia saa karkean version kierrosluvun nousun ja alipaineen syvenemisen hitaasta ryöminnästä.
- **Akku- ja lataustarkistus** (n. 1 €, S)
  - Kytkentä: 100k/22k-jakaja ja suojadiodi GPIO32:een.
  - Saat: lepojännitteen, käynnistyksen notkahduksen (akun kunto) ja latausjännitteen joutokäynnillä ja noin 2000 rpm:llä. Siitä näkee, toimivatko regulaattori/tasasuuntaaja ja latauskela. Rpm-akseli tulee ilmaiseksi.
  - Mahdollistaa myös puristuskokeen akkujännitteen aaltoilusta (D).
  - Tarkistettavaa: BF50A:n latausjärjestelmä. Maadoitukset kytketään tähtipisteeseen.
- **Sytytyspulssianturi tulpanjohtoon** (2–5 €, S–M)
  - Kytkentä: muutama kierros johtoa johdon ympäri, Schmitt-liipaisin ja GPIO, aikaleimat MCPWM-kaappauksella.
  - Saat:
    - todellisen rpm:n pyörityksestä täyskaasuun
    - riippumattoman vertailun DSP:lle M6/M7-nauhoituksiin
    - kolmella anturilla tiedon "kipinä OK" per sylinteri, eli onko vika polttoaineessa vai sytytyksessä
  - Riski: sytytyksen häiriöt pääsevät analogiapuolelle, joten reititys pitää tehdä huolella. Ajastusvalo on matala-arvoinen lisä, koska BF50A:n ajoitus tuskin on säädettävä.
- **Jäähdytysveden paine jo hankitulla HX710B:llä** (n. 2 € T-liitin, S–M)
  - Kytkentä: T-liitin tarkkailusuihkun letkuun. Anturi asennetaan ylemmäs ilmataskun taakse, koska MPS20N0040D:n kalvo ei kestä vettä. Letku tyhjennetään talveksi.
  - Saat: "ei vettä" -hälytyksen sekunneissa ja siipipyörän kuluman trendinä (paine/rpm² vuodesta toiseen).
  - Rajoitus: mittaus pätee vain vesillä, koska huuhtelusovittimen vesijohtopaine peittää pumpun tuoton.
- **LED-palkki ja summeri koteloon** (n. 5 €, M)
  - Saat: keskinolla-palkin valitulle sylinterille, "ding"-äänen synkassa ja syklin vaihdon BOOT-napista. Pikatarkistus onnistuu ilman puhelinta.
  - Riski: palkki näkyy huonosti auringossa ilman varjostinta, ja summeri hukkuu meluun. Tämä kannattaa vasta, kun puhelimen äänituki on tehty.
- **BME280** (n. 3 €, S; ajuri on jo PlantMeisterissä)
  - Saat: absoluuttisen ilmanpaineen, jolloin vuosien trendit ovat vertailukelpoisia. Kotelon lämpötila kehottaa nollaamaan uudelleen, kun lämpö on muuttunut (XGZP:n lämpökompensointi kattaa vain 0–60 °C).
  - Itse synkkaan tästä on vähän hyötyä.

## C. Isommat ja pysyvän asennuksen ideat

- **Pysyvä asennus: tuntimittari, valvonta ja tapahtumaloki** (L)
  - Saat:
    - käyttötunnit ja huoltomuistutukset
    - synkan ryöminnän seurannan jokaisesta tyhjäkäynnistä
    - hälytykset ylikuumenemisesta, vedettömyydestä ja latausviasta
    - ajon aikana automaattisen tallennuksen poikkeamista (katkopurskeet, kierrosten notkahdukset)
  - Vaatii: 12 V -suojausketjun (rauta.md §5), syötön virta-avaimen kautta (AP kuluttaa 12 V:sta noin 3 Ah/vrk), R5-kotelon ja rpm:lle sytytyspulssianturin.
  - Suurin riski: pysyvät letkut ovat kolme vuotoreittiä imukanaviin. Ratkaisuksi noin 0,5 mm kuristinsuuttimet portteihin, tai vain yksi pysyvä letku rpm:lle ja kuormalle. Vasta kun H1–H5 on ratkaistu.
- **Täyskaasutarkistus ja potkuri** (M)
  - Saat: täyskaasun rpm ohjealueella tarkoittaa, että potkurin nousu on oikea. Jos jokin sylinteri jää täyskaasulla muita syvempään alipaineeseen, sen läppä ei avaudu täysin.
  - Vaatii: koeajon vesillä ja apukuskin. Imupulssiin perustuva rpm on täyskaasulla rajoilla (pienet pulssit, GN leikkaa), joten sytytyspulssianturi tekee mittauksesta luotettavan.
- **Puhelimeton laite: oma näyttö ja 12 V -huoltopakki** (M)
  - Vaihtoehdot: TFT (n. 5 €, ei näy auringossa) tai Sharp Memory LCD (n. 30 €, näkyy auringossa), 3 nappia, 12 V hauenleuoilla, IP65-kotelo ja huohotin.
  - Riski: toinen käyttöliittymä ylläpidettäväksi. Powerbank kannattaa pitää, koska käynnistysnotkahdus voi kaataa 12 V -syötön.
- **Polttoaineen syöttöpaine tai kuristustesti** (M)
  - Kytkentä: polttoaineelle luokiteltu anturi (15–25 €) pumpun yhteyteen.
  - Saat: täydellä kaasulla tukkeutunut suodatin, tankin huohotin tai pumppauspallo näkyy. Palovaara, ja koetta tarvitaan harvoin.
- **NMEA 2000 tai SignalK** (M)
  - Saat: rpm, lämpö, jännite ja tunnit näkyvät plotterilla (TWAI + SN65HVD230, n. 2 €).
  - Arvoa on vain, jos veneessä on runkoväylä tai SignalK-palvelin.

## D. Rohkeat ideat, testataan ensin M6-datalla

- **Sytytyskatkolaskuri ja sylinterikohtainen tehopanos** [spek., lupaavin] (M)
  - Saat: tiedon, minkä kolmesta kaasuttimesta puhdistat keväällä (tukkeutunut joutokäyntisuutin), ilman että kaikki kolme puretaan.
  - Miksi toimii: imupulssit toimivat kolmihampaisena kampiakselianturina, 240° välein. Sytytyskatko pidentää omaa väliään arviolta muutaman prosentin. Simulaattorissa välin hajonta oli 0,1 %, joten marginaalia on reilusti.
  - Vaatii: ajastetun näytteenoton, aikaleimat DSP:stä ja välien kartoituksen katkaisukokeella.
  - Testi: piteneekö katkaistun sylinterin väli M6:ssa selvästi?
- **Synkka, imuvuoto vai polttoaine -luokittelu** [spek.] (S, kun edellinen toimii)
  - Miksi toimii: synkkavirheissä alipaine-ero ja tehopanos liikkuvat yhdessä, kun taas vuoto tai polttoainevika poikkeaa tästä.
  - Saat samalla vahdin: jos ruuvi ei vaikuta tai vaatii paljon enemmän korjausta kuin muut, laite kehottaa tarkistamaan venttiilit ja imuvuodot ennen säätöä. Tämä estää peittämästä vikaa synkkaruuvilla.
- **Suhteellinen puristuskoe käynnistysmoottorilla** [spek.] (M)
  - Saat: tulpat pysyvät paikallaan. Hätäpysäytysklipsi irti, 3–5 s pyöritys, ja tulos on esim. 100/97/81 %.
  - Miksi toimii: puristustahti jarruttaa kampea, mikä näkyy välien pituuksissa tai akkujännitteen aaltoilussa.
  - Avoimet kysymykset: näkyvätkö imupulssit pyörityksellä, ja pyöriikö starttimoottori klipsi irti. Tulos verrataan kerran oikeaan puristusmittariin.
- **Aaltomuodon vikaliput ja venttiilivälyn seuranta** [spek.] (M)
  - Ideat: matala pulssi normaalilla keskiarvolla viittaa pakoventtiiliin tai puristukseen. Levenevä imutapahtuma viittaa tiukkaan välykseen. Perustaso tallennetaan heti venttiilisäädön jälkeen.
  - Riski: vaikutusten suuruudet ovat tuntemattomia. GN-anturi leikkaa ylipaineen, joten imuventtiilin vuoto näkyy vain leikkautumisena (GPN-anturi, H1).
  - Testi: nauhoitus ennen ja jälkeen seuraavan venttiilisäädön.
- **Letkujen resonanssi-itsetesti** [spek.] (M)
  - Miksi toimii: letkun ¼-aallon resonanssi (90–180 Hz) on näytteenottotaajuuden alapuolella, ja sen taajuus on kääntäen verrannollinen letkun pituuteen. Eripituinen letku, vesipisara, mutka tai puoliksi tukossa oleva portti näkyy siis taajuudessa.
  - Riski: pulssien harmoniset vääristävät estimaattia, ja herätteen voimakkuus oikeassa moottorissa on auki. M6-raakadatan spektri ratkaisee.
- **Kääntövinkki ("noin 1/8 kierrosta auki")** [spek.] (M)
  - Laite oppii, paljonko kPa muuttuu käännöstä kohden.
  - Riski: vivuston välys ja epälineaarisuus, joten neuvo on aina "noin".
- **Kiihtyvyysanturin tärinäluku** [spek.] (n. 3 €, M)
  - Fysiikka pätee, mutta lukema riippuu kiinnityksestä ja kumityynyistä, eikä se kerro syytä. Kampinopeus antaa saman tiedon ilman rautaa, joten prioriteetti on matala.

## Hylätyt tai parkkiin

- **Pakokaasupulssit tyhjäkäyntiaukosta:** vesitäytteinen pakoputkisto sotkee pulssit, kaasu on kuumaa ja märkää ja mittaus vaatisi ylipaineanturin. Kampinopeus antaa saman tiedon. Hylätty.
- **Kuollut sylinteri IR-mittauksella:** vesijäähdytys tasaa lämpöerot. Hylätty (GY-906 menee termostaatin valvontaan).
- **Kaasuttimen jäätymisvaroitus:** kaasuttimen kuoren lämpötila ei kerro kurkun lämpötilaa. Parkkiin.
- **Polttoaineen virtausmittari (Hall-turbiini):** ei ole bensiinille luokiteltu eikä erota joutokäynnin 1–2 l/h. Hylätty.
- **GPS ja potkurin tai pohjan kunto:** tuuli, virta, kuorma ja trimmi peittävät haetun efektin. Puhelimen GPS ei myöskään toimi http-sivulla. Parkkiin.
- **Ajastusvalo:** BF50A:n ajoitus tuskin on säädettävä, joten valolla voisi vain tarkistaa. Matala arvo.
- **Turvallisuussäännöt:** XGZP ei koskaan vedelle eikä polttoaineelle. HX710B ei polttoaineelle. Tulpanhattua ei irroteta käyvästä CDI-moottorista, vaan käytetään kipinätesteriä.

## Tarkista BF50A:n huolto-ohjeesta ennen kuin tiedot menevät koodiin

- peruskaasutin (master) ja synkkaruuvin kiertosuunta
- joutokäynnin ohjearvo ja sytytysjärjestys
- seosruuvien ohjearvo ja mahdolliset rajoitinhatut
- termostaatin aukeamislämpö ja täyskaasualue
- pyöriikö starttimoottori hätäpysäytysklipsi irti
- latausjärjestelmän tyyppi
- huoltovälit
