# Katselmus: WP-13 (HAL, ESPAsyncWebServer 3.12.1, AsyncTCP 3.5.0, `late_boot`)

**Päivä:** 31.10.2026. **Katselmoija:** riippumaton aliagentti, joka ei ole kirjoittanut
katselmoitavaa koodia. **Pohja:** `e15646b`. **WP-13:n merge:** `e9d7d5c` (`a939b57`, `017ee3f`).
HAL-tiedostot eivät muutu mergen jälkeen, joten rivinumerot viittaavat `e15646b`:hen.

**Lähteet:**
- `firmware/.pio/libdeps/esp32dev_adc/{ESPAsyncWebServer,AsyncTCP}/src`. Versiot on tarkistettu
  `library.json`:sta ja `*Version.h`:sta: 3.12.1 ja 3.5.0.
- Vertailua varten haettiin GitHub-tageista ESPAsyncWebServerin versiot v3.6.0, v3.6.2, v3.7.0,
  v3.7.6, v3.7.7, v3.9.4 ja v3.9.5 sekä AsyncTCP v3.3.2. Lisäksi luettiin kirjastojen CI-tiedostot
  ja `platformio.ini`.
- Arduino-ESP32 2.0.17: `framework-arduinoespressif32/tools/sdk/esp32/{qio,dio}_qspi/include/sdkconfig.h`
  ja `cores/esp32/esp32-hal-log.h`.

**Käännös:** `pio run -e esp32dev_adc` onnistui (SUCCESS) eikä antanut varoituksia.

Tässä tiedostossa AES tarkoittaa tiedostoa `AsyncEventSource.cpp`, AES.h tiedostoa
`AsyncEventSource.h` ja ATCP tiedostoa `AsyncTCP.cpp`. Jos versiota ei mainita, rivit viittaavat
versioihin 3.12.1 ja 3.5.0.

## Yhteenveto

- **Korkean vakavuuden löydöksiä ei ole.** Kaikki web.cpp:n, platformio.ini:n ja firmware.md:n
  väitteet pitävät lähdettä vasten. Kaksi niistä kaipaa tarkennusta:
  - "Neljännes"-sääntö laukeaa arvolla 8 vain, kun jono on tyhjä.
  - tcpip-perustelu on vahvempi kuin turvallisuus vaatii.
- **Nykyinen kierto riittää 3.12.1:llä.** Siinä `sbx_push` lähettää yksin ja `close()` on
  kielletty. 3.9.5:n null-tarkistukset ja listalukon antama elinikä tekevät `send()`:stä
  turvallisen, vaikka `async_tcp` pääsisi ajoon `sbx_push`in lwIP-odotuksen aikana. Turva ei siis
  riipu tcpip-taskin ytimestä (L2).
- **`late_boot` on oikein.** Järjestys, release/acquire-pari ja laskurit ovat kunnossa. Rajalla
  on mikrosekuntien ikkuna, jolla ei ole käytännön merkitystä (L8).
- **Ajonaikaiset muutokset 3.6.0 → 3.12.1** eivät riko liiman oletuksia. Tämä koskee
  käsittelijä-API:a, `send`-ajoitusta, aikakatkaisuja, taskeja ja URI-vastaavuutta. Kaksi
  laskuria muuttaa kuitenkin merkitystään (L7).
- **Suurin avoin asia ei ole regressio** vaan WP-7b:n aikainen SSE-mitoitus: diag lähetetään
  kaikille ennen snapia, ja jonon raja on 8 (L1). Asia mitataan laitetestissä.

## 1. Väitteiden tarkistus

| # | Väite (missä) | Pitää? | Lähde |
|---|---|---|---|
| V1 | Täysi jono hylkää uusimman viestin, vanhat jäävät (web.cpp:48-49) | pitää | AES:228-231 |
| V2 | "Uusi viesti lähtee heti vain, kun jonossa on alle neljännes" (platformio.ini:31-32, web.cpp:46) | pitää, tarkennettava | AES:233-248: ehto `size < MAX >> 2` tarkistetaan **lisäyksen jälkeen**. Arvolla 8 se täyttyy vain, kun jono oli tyhjä. Viesti poistuu jonosta vasta täysin kuitattuna (AES:264-269), joten "tyhjä" tarkoittaa, että kaikki aiemmat viestit on kuitattu (L1) |
| V3 | Alle 8:lla ehto ei täyty koskaan, ja viesti lähtee vasta 500 ms:n pollissa (web.cpp:78-80) | pitää | AES:246, 8 >> 2 = 2. Tyhjästä jonosta ei odoteta kuittausta, joten vain poll lähettää. Poll-väli on 500 ms (ATCP:105-109, 412) |
| V4 | Muulloin viesti lähtee kuittauksen tai pollin yhteydessä (web.cpp:44-48) | pitää | `_onAck` AES:252-276, `_onPoll` AES:278-284 |
| V5 | `CORE_DEBUG_LEVEL=0` poistaa kirjastojen `log_e`- ja `log_w`-kutsut (platformio.ini:34-40) | pitää | AsyncWebServerLogging.h:203-219 (Arduino-haara, `USE_ESP_IDF_LOG` ei asetettu): `async_ws_log_w` → `log_w`. esp32-hal-log.h:36-39 asettaa `ARDUHAL_LOG_LEVEL` = 0, jolloin `log_w`/`log_e` ovat `do{}while(0)` (:141-171). AsyncTCPLogging.h:37-52 toimii samoin. Ylivuodon loki on AES:229. `build_flags` koskee myös kirjastoja |
| V6 | `log_printfv` odottaa UARTia aktiivisesti (platformio.ini:35) | pitää (ennallaan) | esp32-hal-uart.c:842 `ets_printf`, :855 `while(!uart_ll_is_tx_idle)` |
| V7 | AsyncTCP 3.5.0 lukee yhä makron `CONFIG_ASYNC_TCP_RUNNING_CORE` | pitää | AsyncTCP.h:36-38 (`#ifndef`, oletus -1). ATCP:394 käyttää sitä, ja ATCP:373-376 kiinnittää taskin, kun arvo on 0..1. web.cpp:77:n static_assert kaatuu, jos `-D` puuttuu |
| V8 | async_tcp: prio 10, pino 16 kt (web.cpp:87, 307) | pitää | AsyncTCP.h:45-51 |
| V9 | Lukot ovat rekursiivisia (web.cpp:8-9) | pitää | ESPAsyncWebServer.h:53-58 (ESP32 → `ASYNCWEBSERVER_USE_MUTEX 1`), :264-266 `std::recursive_mutex` |
| V10 | `send()` lukitsee listan (:414) ja `_lockmq`:n (:226) | pitää | AES:414, 226 |
| V11 | `_onDisconnect` nollaa `_client`in ilman lukkoa, joten kilpa on yhä auki (web.cpp:11-18, platformio.ini:47) | pitää | AES:292-298. Lisäksi `connected()` lukee `_client`in ilman lukkoa (AES.h:200-202) |
| V12 | 3.9.5:n null-tarkistukset rivillä 233, 246 ja 333 | pitää | v3.9.4:ssä tarkistuksia ei ole, v3.9.5:ssä ne ovat (:212, 225, 243, 256, 347) |
| V13 | AsyncClient elää, kunnes `_handleDisconnect` on saanut listalukon; `delete c` on rivillä 179 | pitää | AES:176-182, 367-379 |
| V14 | 3.12.1 korjaa `_handleDisconnect`in iteraattorin (break rivillä 375, korjattu 3.7.0:ssa) | pitää | v3.6.0:359-362 ei sisällä breakia, v3.6.2 ei myöskään, v3.7.0:390 sisältää |
| V15 | 3.12.1 korjaa `close()`n itselukituksen (rekursiivinen mutex, korjattu 3.7.7:ssä) | pitää | v3.7.6: `std::mutex`, v3.7.7: `std::recursive_mutex` |
| V16 | `close()` on silti kielletty: iteraattori mitätöityy (web.cpp:35-39) | pitää | AES:385-395. `c->close()` → ATCP:940-945 → `_close` ATCP:1016-1023 → `_discard_cb` synkronisesti → AES:296-297 → erase AES:374 kesken silmukan |
| V17 | 3.12.1 korjaa `avgPacketsWaiting`in jaon nollalla (rivi 409, korjattu 3.9.5:ssä) | pitää | v3.6.0:386-395 jakaa luvulla, joka voi olla 0; v3.9.4 samoin; v3.9.5:429 sisältää tarkistuksen |
| V18 | 3.5.0:n jono on rajaton (ATCP:201, 220-226), tcpip odottaa vain jonon mutexia (:183-198), ja async_tcp pitää mutexia ilman takaisinkutsuja (:236-296) | pitää | Rivit kuten väitetty. tcpip on ytimellä 0 (sdkconfig.h:496 `CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0 1`) prioriteetilla 18 (:446). Ydinlukitus ei ole käytössä (`CONFIG_LWIP_TCPIP_CORE_LOCKING` puuttuu) |
| V19 | 3.3.2:ssa tcpip saattoi jäädä odottamaan täyttä jonoa | pitää | AsyncTCP v3.3.2: xQueue (64) rivillä 140, `xQueueSend(..., portMAX_DELAY)` riveillä 148-150 |
| V20 | `_pcb` on NULL ennen `_onDisconnect`ia kaikilla palvelimen poluilla (web.cpp:30-34) | pitää | Sulkeminen ja FIN: ATCP:1068-1071 → 940-945 → 660. Virhe: :512. Abort: :688. Rx-timeout: :1131-1134 → `_close`. FIN-tilassa `connected()` on epätosi jo ennen tätä, koska CLOSE_WAIT ≠ ESTABLISHED (:1387-1392) |
| V21 | onConnect ajetaan ennen kuin asiakas lisätään listaan (web.cpp:412-414) | pitää | AES:357-362 (3.6.0:ssa onConnect ajettiin listalukon alla lisäyksen jälkeen, 3.6.0:344-348) |
| V22 | AsyncBasicResponse kopioi sisällön (web.cpp:124-125, 313) | pitää | WebRequest.cpp:1208-1213, WebResponses.cpp:260-271 (`_content = content` → String) |
| V23 | AsyncProgmemResponse lukee suoraan flashista (web.cpp:113) | pitää | WebRequest.cpp:1215-1218, WebResponses.cpp:900-905 (`memcpy_P` osoittimesta) |
| V24 | `_attachHandler` toimii ilman lukkoa (web.cpp:323-324) | pitää | WebServer.cpp:142-151 |
| V25 | Palvelin omistaa käsittelijän (web.cpp:417) | pitää | ESPAsyncWebServer.h:1684, WebServer.cpp:97-100 |
| V26 | Task watchdog 5 s vahtii IDLE0:aa ja async_tcp:tä (hal_config.h:20-22, web.cpp:110) | pitää | ATCP:331-352; sdkconfig.h:345-346 (CPU0, CPU1 ei). **Huom.:** :344 `CONFIG_ESP_TASK_WDT_PANIC 1`, joten yli 5 s:n jumi käynnistää laitteen uudelleen |
| V27 | Kirjastojen oma CI kääntää Arduino 2.x:lle (espressif32@6.12.0, sama 2.0.17) (platformio.ini:48) | pitää osittain | ESPAsyncWebServer `platformio.ini`:164-166 ja AsyncTCP :51-53 käyttävät ympäristöä `ci-arduino-2` = espressif32@6.12.0, ja CI ajaa sen. CI kuitenkin vain **kääntää esimerkit**. AsyncTCP:n CI:ssä on vain asiakasesimerkkejä, eikä kumpikaan aja koodia. Väitettä 6.12.0 = 2.0.17 ei tarkistettu rekisteristä, mutta se on uskottava |
| V28 | "Silloin async_tcp ei pääse käsittelemään katkeamista kesken send():n" (web.cpp:19-21) | pitää tällä sdkconfigilla | Ks. kohta 2. Turva ei edes vaadi tätä (L2) |

## 2. Riittävyys: `sbx_push` lähettää yksin, `close()` kielletty

**`_client`in luvut `sbx_push`in polulla.** `send()` → AES:412-427 pitää listalukkoa koko
silmukan ajan.

| Luku | Estävä kutsu ennen käyttöä? | Turva |
|---|---|---|
| `connected()` AES.h:200-202 (AES:418, `write()` AES.h:184) | ei | sama ydin, korkeampi prio |
| AES:233 → emplace | ei | sama |
| AES:246 `_client && _client->canSend()` | ei | sama |
| AES:314 → :322 `i->write(_client)` → `client->add()` | **kyllä:** `_tcp_write` → `tcpip_api_call` (ATCP:616) | Kutsu käyttää paikallista kopiota, ja olio elää, koska `delete c` (AES:179) odottaa listalukkoa (AES:371). Seuraava kierros lukee `_client`in uudelleen, ja `write(nullptr)` palauttaa 0 (AES:118-120) |
| AES:333-334 `_client && ...` → `_client->send()` | ei lukujen välissä; `send()` estää vasta sisällä (ATCP:594) | `this` elää kuten edellä |

`_messageQueue` ja `_inflight` ovat `_lockmq`:n takana (AES:226, 254, 280). Lista ja olion
tuhoaminen ovat listalukon takana (AES:361, 371, 190). Siksi `async_tcp` ei voi muuttaa
mitään, mitä `sbx_push` pitää kädessään, vaikka se pääsisi ajoon `sbx_push`in lwIP-odotuksen
aikana. Ainoat kaksoisluvut ilman estävää kutsua välissä ovat yllä, ja ne suojaa sama ydin
korkeammalla prioriteetilla. **Kierto siis riittää 3.12.1:llä.** Lisäksi tällä sdkconfigilla
async_tcp ei pääse ajoon edes lwIP-odotuksen aikana, koska tcpip on ytimellä 0 prioriteetilla 18
ja odottaa enintään jonon mutexia (V18).

Prioriteetin periytyminen tarkistettiin. `async_tcp` voi nousta `sbx_push`in yläpuolelle vain,
jos korkeamman prioriteetin taski odottaa sen hallussa olevaa mutexia. Tällaisia mutexeja on
kolme: jonon mutex (tcpip), lwIP:n `SYS_ARCH_PROTECT` (`pbuf_free` async_tcp:ssä) ja Wi-Fi-API
(`wifiApStations` handleDiagissa). Kaikki kolme kriittistä osaa ovat lyhyitä, eivätkä ne kosketa
AsyncEventSourcea. `procLock`ia odottavan `sbx_proc`in prioriteetti on 5, joten se ei nosta
async_tcp:tä.

**Muut ajonaikaiset muutokset 3.6.0 → 3.12.1 ja 3.3.2 → 3.5.0:**

- **Käsittelijä-API.** `on()` ottaa nyt `AsyncURIMatcher`in (WebServer.cpp:153-164). `const char*`
  muuttuu BackwardCompatible-tyypiksi (:229-232), jonka vastaavuus on `uri == path ||
  path.startsWith(uri + "/")` (:336). Tämä on sama kuin 3.6.0:ssa, joten web.cpp:326-327:n
  "tarkemmat polut ensin" pitää. `ASYNCWEBSERVER_REGEX` ei ole asetettu.
- **POST-lomake parsitaan.** `isRequestHandlerTrivial()` on epätosi, kun `onRequest` on asetettu
  (WebHandlerImpl.h:85-87, WebRequest.cpp:186).
- **`request->send(r)`** vain tallettaa vastauksen. Lähetys tapahtuu käsittelijän jälkeen
  (WebRequest.cpp:1261-1278, `_send` :1048-1067), kuten 3.6.0:ssa (3.6.0 WebRequest.cpp:793-799).
  Pinopuskurit (handleDiag, sendOk) ovat turvallisia, koska AsyncBasicResponse kopioi niiden
  sisällön (V22).
- **Aikakatkaisut ovat ennallaan.** Rx-aikakatkaisu on 3 s ennen vastausta (WebServer.cpp:50) ja
  0 vastauksen alkaessa (WebRequest.cpp:1063, SSE AES:153). ACK-aikakatkaisu on 5 s
  (AsyncTCP.h:57-59), ja hidas SSE-asiakas suljetaan `_onTimeout`issa (AES:286-290). Poll-väli on
  500 ms. Arvot ovat samat kuin 3.6.0:ssa ja 3.3.2:ssa.
- **Muisti per asiakas.** SSE-viesti on yksi jaettu String (AES:413), joten pahimmillaan muistia
  kuluu noin 8 × 2,1 kt riippumatta asiakkaiden määrästä. HTTP-vastaus käyttää 2 872 tavun
  puskuria (WebResponseImpl.h:18-27, WebResponses.cpp:440). 3.6.0 varasi joka kuittauksella
  `malloc(space + head)` eli enintään noin 5,7 kt. CSV-täyttäjää kutsutaan nyt enintään 2 864
  tavulla (WebResponses.cpp:453), joten `procLock`-kierroksia tulee noin kaksi kertaa enemmän ja
  kukin on lyhyempi. Chunked-vastauksen in-flight-krediitit (:354-379) eivät jumita, koska krediitti
  pysyy vähintään yhdessä.
- **Taskit ja jonot.** ESPAsyncWebServer ei luo taskeja. async_tcp:n pino (16 kt), prioriteetti
  (10) ja ydin ovat ennallaan. Tapahtumajono vaihtui xQueue(64):stä rajattomaksi listaksi (L9).
- **lwIP ilman ydinlukkoa.** Sekä 3.3.2 että 3.5.0 kutsuvat `tcp_new`/`tcp_arg`/`tcp_accept`ia
  suoraan (`TCP_MUTEX_LOCK` ja `tcp_core_guard` ovat tyhjiä ilman
  `CONFIG_LWIP_TCPIP_CORE_LOCKING`ia, ATCP:79-100), joten tämä ei ole muutos.
- **`send()`n paluuarvo** muuttui (L7).

## 3. `late_boot` / `procBootDone()`

- **Järjestys.** main.cpp:83-91 käynnistää ensin samplerin, sitten `procStart`in, Wi-Fi AP:n,
  `webStart`in ja `diagLogBoot`in, ja kutsuu viimeisenä `procBootDone()`a. Raja on siis setupin
  lopussa, kuten diag.h:15-17 ja firmware.md:486-489 sanovat. Luokittelu tehdään näytteen
  aikaleiman mukaan (`m.t_us` = herätyshetki t0, sampler.cpp:259, 284) eikä käsittelyhetken
  mukaan. Siksi ennen rajaa otetut mutta sen jälkeen käsitellyt näytteet menevät oikein
  `late_boot`iin.
- **Näkyvyys.** proc_task.cpp:244-245 kirjoittaa ajan ennen lippua (release), ja :123 lukee lipun
  ensin (acquire), joten `g_boot_done_us` näkyy oikein. Molemmat taskit ovat ytimellä 1.
  `atomic<bool>` on Xtensalla lukoton. `g_boot_done_us` kirjoitetaan vain kerran, joten
  datakilpaa ei synny.
- **Laskurit.** Vain käsittelytaski kirjoittaa `g_late`a ja `g_late_boot`ia. Ne kopioidaan
  `g_mutex`in alla (:164-165, 175-186). Lukitus (:123-126) tekee kiertymisestä merkityksettömän,
  koska ero on alle 35 min vain hetken rajan ympärillä. Formaattijonot (diag.cpp:105/113,
  :171/175) ja argumentit vastaavat toisiaan. Diag-JSONin pahin pituus on noin 645 tavua, kun
  puskuri on 1 024 tavua.
- **Huomiot:** L5 ja L8.

## 4. Löydökset vakavuusjärjestyksessä

### L1 (keski, ei regressio): diag ja neljännes-sääntö viivästävät snapia, ja vaikutus riippuu asiakkaan viivästetystä ACK:sta

**Missä:** web.cpp:364-367 ja 384, platformio.ini:29-33, AES:246.

**Mekanismi:**
- `diag` lähetetään kaikille SSE-asiakkaille joka kolmannella kierroksella juuri ennen snapia.
  Pääsivu ei kuuntele diagia (web_page.h:953 kuuntelee vain `snap`ia).
- Arvolla 8 snap ei lähde heti, koska jonossa on jo kuittaamaton diag. Silloin `2 < 2` on epätosi,
  ja snap odottaa diagin kuittausta tai 500 ms:n pollia. Sama koskee jokaista snapia, jonka
  edeltäjää ei ole kuitattu 66 ms:ssa.
- Asiakkaan TCP-pino voi viivästää kuittausta yksittäisestä segmentistä 40 ms (Linux/Android).
  Windowsissa viive on noin 200 ms, ja iOS:n arvo on mittaamatta. Jos viive on yli 66 ms, data
  saapuu kuittausten tahdissa purskeina, esimerkiksi Windows-PC:n `/diag`issa noin 5 Hz.
- Tämä ei ole WP-13:n regressio, koska sama sääntö oli jo 3.6.0:ssa (rivi 239). WP-13 kuitenkin
  muokkasi kommenttia, joka kuvaa sääntöä epätarkasti (V2).

**Korjaus:** mitataan ensin laitetestissä (lista alla). Jos purskeita näkyy, vaihtoehdot ovat:
- (a) diag omaan `AsyncEventSource`en (`/events/diag`), jolloin pääsivun jono ei saa diagia
- (b) `SSE_MAX_QUEUED_MESSAGES` 12–16: raja 3–4, ja pahin viive noin 0,6–0,8 s
- (c) molemmat.

Lisäksi platformio.ini:31-32 tarkennetaan muotoon "lisäyksen jälkeen alle neljännes; arvolla 8
vain tyhjään jonoon".

### L2 (matala): web.cpp:n turvaperustelu nojaa vahvempaan oletukseen kuin tarpeen

**Missä:** web.cpp:19-34 ja firmware.md:519-525.

Perustelu sanoo, että async_tcp ei pääse ajoon kesken `send()`:n, koska tcpip odottaa vain jonon
mutexia. Tämä pitää, mutta vain sdkconfigin ansiosta (tcpip on ytimellä 0), eikä sitä tarkisteta
käännöksessä. Todellinen ja kestävämpi invariantti on kohdan 2 taulukko:
- (i) `_client`in kaksoisluvut ilman estävää kutsua välissä (AES.h:200-202, AES:246, :333-334)
  suojaa sama ydin korkeammalla prioriteetilla
- (ii) AsyncClient elää listalukon yli (AES:176-182, :371)
- (iii) estävän lwIP-kutsun jälkeen `_client` luetaan ja tarkistetaan uudelleen (AES:322 →
  :118-120, :333).

**Korjaus:** kirjoitetaan nämä kolme kommenttiin ensisijaiseksi perusteluksi ja tcpip-argumentti
toiseksi kerrokseksi. Koodimuutosta ei tarvita. Valinnainen lisäys on CI-grep, joka kieltää
`g_events->close`-, `removeHandler`- ja `reset()`-kutsut.

### L3 (matala): dokumentit sanovat "laitetesti ennen masteria"

**Missä:** firmware.md:525 ja paketit.md:19.

Omistaja on päättänyt yhdistää ennen laitetestiä, joten teksti on ristiriidassa päätöksen kanssa.

**Korjaus:** "yhdistetty masteriin 31.10.2026 ennen laitetestiä; laitetesti 1 (jatko.md) tekemättä".

### L4 (matala): jatko.md:n kahden rivin upstream-korjaus ei yksin riitä

**Missä:** jatko.md:134-136.

`_lockmq` `_onDisconnect`issa ei suojaa `connected()`ia (AES.h:200-202), jota `send()` ja
`count()` kutsuvat ilman `_lockmq`:ta (AES:418, 433).

Jos `_lockmq` otetaan `_onDisconnect`in ajaksi `lock_guard`illa, lukko on yhä voimassa, kun
`_handleDisconnect` tuhoaa olion (AES:374 → ~AsyncEventSourceClient AES:188-193). Lukitun mutexin
tuhoaminen on määrittelemätöntä käytöstä.

**Korjaus ehdotukseen:**
- `{ lock_guard l(_lockmq); _client = nullptr; }` omana lohkonaan ennen `_handleDisconnect`ia
- `connected()` kopioi `_client`in kerran paikalliseen muuttujaan tai lukitsee.

### L5 (matala): `late_boot` erottaa vain myöhästymiset

**Missä:** proc_task.cpp:114-116 (`gaps`), sampler.cpp:268-270 (`skipped`) ja :286-288 (`q_drop`),
sekä jatko.md:147.

Aukot, ohitukset ja pudotukset sisältävät yhä käynnistyksen transientit. Jos Wi-Fin käynnistys
pysäyttää ytimen yli 3 ms:ksi, `ohit`/`aukot` ovat nollasta poikkeavia jo ensimmäisellä
DIAG-rivillä, eikä hyväksyntä "myoh=0" kerro koko kuvaa.

**Korjaus:** laitetestissä kirjataan ensimmäisen DIAG-rivin `aukot`/`ohit`/`pudot`. Jos ne ovat
nollasta poikkeavia, joko jaetaan nekin `procBootDone`n rajalla tai kirjataan hyväksyntään
"+0 ensimmäisen rivin jälkeen".

### L6 (matala, kirjasto): AsyncTCP 3.5.0:n accept-polku muistin loppuessa

**Missä:** ATCP:1605-1610.

Jos `new AsyncClient` epäonnistuu, polku kutsuu `tcp_abort(pcb)` ja palauttaa `ERR_OK`.
lwIP:n sääntöjen mukaan takaisinkutsun on palautettava abortin jälkeen `ERR_ABRT`. Muuten lwIP
jatkaa vapautetulla pcb:llä, mikä voi korruptoida heapin. Tilanne syntyy vain, kun heap on
lopussa.

**Korjaus:** raportoidaan kirjastolle. Paikallisesti riittää varmistaa heapin marginaali.
Laitetestin `min ≥ 40 kt` kattaa tämän.

### L7 (matala): `send()`n tilat muuttivat merkitystään

**Missä:** AES:417-426 (3.6.0:406-412).

3.6.0 laski sulkeutuvan mutta yhä listassa olevan asiakkaan hudiksi: `osit` kasvoi, ja yksinään
se kasvatti `hyl`iä. 3.12.1 ohittaa ei-yhdistetyt asiakkaat. Siksi jatko.md:145:n ehdottama
vertailu masteriin (3.6.0) ei ole suora `osit`- ja `hyl`-lukujen osalta. Lisäksi `hyl` kasvaa
3.12.1:ssä, jos ainoa asiakas katkeaa `count()`n (web.cpp:359) ja `send()`n (:384) välissä.

**Korjaus:** huomautus testisuunnitelmaan. Koodimuutosta ei tarvita.

### L8 (tieto): `procBootDone`n rajaikkuna

**Missä:** proc_task.cpp:244-245 ja :123-126.

Jos loopTask keskeytyy ajan luennan ja lipun asetuksen välissä ja käsittelytaski ehtii käsitellä
sinä aikana näytteen, jonka `t_us ≥ g_boot_done_us`, näyte lasketaan `late_boot`iin. Ikkuna on
mikrosekunteja, ja virhe koskee korkeintaan muutamaa näytettä.

**Korjaus:** ei tarvita.

### L9 (tieto): AsyncTCP 3.5.0:n rajaton tapahtumajono

**Missä:** ATCP:201 ja 220-226.

Jonossa ei ole enää vastapainetta, joten tapahtumat varataan heapista tcpip-kontekstissa.
Kasvua rajaa TCP-ikkuna: `tcp_recved` kutsutaan vasta käsittelyssä, ja `CONFIG_LWIP_TCP_WND_DEFAULT`
on 5 760. Lisäksi pollit kuristetaan (ATCP:269, 446).

**Korjaus:** ei tarvita. Heapin `min` seurataan katkaisutestissä.

**WP-13:n diffistä ei löytynyt muuta:**
- Pino: uusia pinokäyttäjiä ei ole. `ProcWindow` kasvoi 4 tavua.
- Prioriteetit ja ytimet ovat ennallaan.
- Vahtikoirat: muutosta ei ole. TWDT-paniikki on päällä (V26).
- Versiolukko on kaksinkertainen ja kunnossa: `lib_deps` sisältää tarkat versiot (AsyncTCP
  ensin, joten ESPAsyncWebServerin `^3.5.0` täyttyy), ja static_assert tarkistaa versiot.
  libdeps sisältää vain AsyncTCP 3.5.0:n.

## 5. Korjataanko ennen masteria?

| Löydös | Ennen masteria? | Perustelu |
|---|---|---|
| L1 | ei | ei regressio; mitataan ensin |
| L2 | ei | kommentti, ei vaikuta käytökseen |
| L3 | kyllä (dokumentti, kaksi riviä) | muuten master väittää, että laitetesti on tehty ennen yhdistämistä |
| L4 | ei | koskee upstream-ehdotusta, ei laitekoodia |
| L5 | ei | testisuunnitelman tarkennus |
| L6 | ei | kirjaston vika, vain muistin loppuessa |
| L7 | ei | testisuunnitelman tarkennus |
| L8, L9 | ei | tietoa |

## Laitetestissä erityisesti katsottava

1. **Käynnistysloki.** Odotetaan `async_tcp=c0/p10` ja `sbx_push=c0/p11` (diagLogBoot). Väärä ydin
   tai prioriteetti kumoaa koko kohdan 2 perustelun.
2. **Ensimmäinen DIAG-rivi kolmesti.** Odotetaan `myoh=0` ja `kaynn` 1–5 (jatko.md:147). Kirjataan myös `aukot`,
   `ohit` ja `pudot` (L5).
3. **SSE-tahti eri asiakkailla (L1).** Selaimen DevTools → Network → EventStream näyttää jokaisen
   viestin ajan. Mitataan snapien väli Android-puhelimella, iPhonella ja Windows-PC:llä
   (`/diag` ja pääsivu). Odotetaan noin 66 ms. 150–250 ms:n purskeet ja tasaiset välit niiden
   välissä viittaavat L1:een.
4. **Katkaisumyrsky.** jatko.md:n vaihe 5 on jäännösriskin pääkohde. Seurataan:
   - kaatuminen (LoadProhibited) funktioissa `AsyncEventSourceClient::_runQueue/_queueMessage`
     tai `AsyncClient::*` (exception decoder on nyt päällä)
   - `task_wdt`-paniikki (V26)
   - palaako `sse` todelliseen arvoon.
5. **Heap ja `min`.** Kuormitetaan yhtä aikaa CSV:llä, SSE:llä ja `GET /api/config`illa
   (6 kt + kopio). `min` ≥ 40 kt ja suurin lohko seurataan (L6, L9).
6. **Pinot.** `[PINO]`-rivin `async_tcp`- ja `push`-arvot raskaimman vaiheen jälkeen. Varaa
   pitää olla vähintään noin 1 kt.
7. **CSV.** Latausaika verrattuna masteriin: puskuri on nyt 2,8 kt, ja täyttäjää kutsutaan noin
   kaksi kertaa useammin. SSE:n on jatkuttava, ja `jono`-huipun (q_peak) on pysyttävä kaukana
   128:sta procLock-kilpailun aikana.
8. **`osit` ja `hyl` masterivertailussa (L7).** Lukuja ei verrata suoraan. 3.12.1:ssä `hyl` voi
   kasvaa yksittäin katkaisuhetkellä.
