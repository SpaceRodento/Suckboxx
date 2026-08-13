# Unit testing (PlatformIO)

Tämä projekti tukee laitteetonta unit-testausta `native`-ympäristössä.
Testit on suunniteltu ajettavaksi Linux-ympäristössä (CI: Ubuntu).

## Ajo

```bash
pio test -e native
```

## Testitiedostot

### test_ebbflow_fsm (11 testiä)

Ebb & Flow -tilakoneen logiikka (`lib/ebbflow_fsm/`).

- Alkutila on IDLE, vika on FAULT_NONE
- IDLE → FLOOD kun intervalli on kulunut
- DRY_RUN-vika kun vedenpinta liian matala
- OVERFLOW-vika tulvausvaiheessa
- Vikakuittaus palauttaa IDLE:hen (vedenpinta OK)
- Tulvaus-timeout → FAULT_FLOOD_TIMEOUT
- Tyhjennys-timeout → FAULT_DRAIN_TIMEOUT
- Täysi sykli IDLE → FLOOD → SOAK → DRAIN → IDLE
- Vikakuittaus ilman vedenpintaa → DRAIN
- IDLE → FLOOD toimii myös millis-wraparoundin yli
- FLOOD-timeout toimii myös millis-wraparoundin yli

### test_scheduler (37 testiä)

Scheduleerin puhdas logiikka (`scheduler.h`).
Ei laitteistokutsuja — käyttää stub-implementaatioita pump/motor/power -funktioille.

**scheduler_shouldLightBeOn** (7):
- Valo päällä syklijaksolla
- Valo pois syklijaksolta
- Ei valoa kun 0 tuntia asetettu
- forceOn ohittaa aikataulun
- forceOff ohittaa aikataulun
- Kasvuvaihe (grow phase) ohittaa laitteen perusasetukset
- Valosykli toimii oikein myös millis-wraparoundin yli

**scheduler_shouldWater** (6):
- Pumppu jo käynnissä → ei kastele
- Vedenpinta matala → ei kastele
- Aikaväli ei kulunut → ei kastele
- Kaikki ehdot täyttyvät → kastelee
- TDS-arvo yli tavoitteen → ohittaa kastelu
- Kastelun aikaväli toimii oikein myös millis-wraparoundin yli

**scheduler_checkTemp** (4):
- Lämpötila OK-alueella → 0
- Liian kylmä → -1
- Liian kuuma → +1
- Sensori ei kalibroitu (envValid=false) → 0

**scheduler_calcNewHeight** (3):
- Normaali nosto +10mm
- Lähellä maksimia → rajataan maksimiin
- Juuri maksimissa → pysyy maksimissa

**scheduler_shouldRaiseHeight** (5):
- Sensori ei validi → false
- Aikaväliä ei ole kulunut → false
- Kasvi kaukana lampusta (marginaalin ulkopuolella) → false
- Kasvi lähellä lamppua → true
- Korkeustarkistuksen aikaväli toimii myös millis-wraparoundin yli

**scheduler_getActivePhase** (3):
- Kasvu ei aktiivinen → NULL
- Ei faaseja → NULL
- Oikea faasi palautetaan

**scheduler_updateGrowPhase** (8):
- Kasvu ei aktiivinen → ei muutosta
- 24h kulunut → päivälaskuri kasvaa
- Alle 24h → ei päivätikkausta
- Faasin kesto kulunut → ehdotus käyttäjälle
- Indefinite-faasi (`durationDays=0`) ei ehdota siirtymää
- Pending-siirtymä ei auto-advancea ennen 3 päivää
- 3 päivän auto-advance (käyttäjä ei vastannut)
- Viimeinen faasi → ei auto-advancea

### test_motor_math (10 testiä)

Moottorin mm→steps ja steps→mm -muunnokset.
Täysin itsenäinen — ei sisällä motor_hal.h:ta (vaatii AccelStepper).
Muunnoskaavat peilaavat motor_hal.h:n logiikkaa.

**L298N HALF4WIRE** (6): 0mm, 100mm, 1mm, käänteinen, round-trip
**TMC2208 (16 microsteps)** (4): 100mm, 1mm, käänteinen, round-trip

### test_config_defaults (17 testiä)

`config_setDefaults()` -funktion oletusarvot (`config_defaults.h`).
Ei ArduinoJson- tai LittleFS-riippuvuuksia.

- LoRa-osoite, verkko-ID ja kohde
- Kasvin oletusarvo ("basil")
- Valon aloitustunti (6)
- Moottorin kohde-asema (50mm) ja nykyinen asema (0mm)
- Kasvu ei aktiivinen oletuksena
- WiFi-autoyhdistys pois oletuksena
- Light force -liput pois oletuksena
- Stringit null-terminoitu
- `config_clampBounds`: `lightOnHour` out-of-range → oletus 6
- `config_clampBounds`: `motorTargetMm` negatiivinen → 0
- `config_clampBounds`: `motorTargetMm` yli max → `MOTOR_MAX_HEIGHT_MM`
- `config_clampBounds`: `growStartMethod` out-of-range → 0
- `config_clampBounds`: `growPhase` out-of-range → 0
- `config_clampBounds`: validi config ei muutu

### test_command_validation (9 testiä)

Komentojen validointi- ja parsintalogiikka (`command_validation.h`).
Katsoo ettei API/komentopolku hyväksy virheellisiä arvoja.

- `cmd_parseIntStrict`: hyväksyy vain kokonaisluvun, hylkää roskadatan
- `cmd_parseFloatStrict`: hyväksyy vain validin liukuluvun
- `portal_validateCommand`: komento-whitelist + arvorajat (HEIGHT, WATER, AIRPUMP)
- Tuntemattomat komennot hylätään
- Plant-ID token validoidaan (`[a-zA-Z0-9_-]`, max pituus)

### test_api_contract_validation (11 testiä)

Endpoint-tason API-contract validointi (`api_contract_validation.h`).
Varmistaa erityisesti virhepolut ennen varsinaista endpoint-käsittelyä.

- Grow action whitelist: `start|next|delay|stop`
- `start_method` rajat: 0..2
- History-query field whitelist
- History-query `max` rajat: `1..API_HISTORY_MAX_ENTRIES`
- Tyhjä history-field normalisoidaan oletukseen `air_temp`
- `/api/config` LoRa-kenttien rajat (`lora_address`, `lora_network_id`, `lora_target`)
- `/api/config` `light_on_hour` rajat (0..23)
- `/api/config` WiFi SSID/salasana pituusrajat
- `/api/config` `plant_id` tokenin validointi

### test_lora_parser (7 testiä)

LoRa-komentoparserin eriytetty logiikka (`lora_parser.h`).
Testaa parserin turvallista toimintaa ilman radio-/UART-riippuvuuksia.

- Validi `CMD:KEY=VALUE` parseri
- Validi `CMD:KEY` ilman arvoa
- Puuttuva `CMD:`-prefix hylätään
- Tyhjä syöte hylätään
- Liian pitkä key katkaistaan turvallisesti
- Liian pitkä value katkaistaan turvallisesti
- `NULL`-syötteet hylätään

### test_plant_lookup (7 testiä)

Kasvidatan oletuslatauksen ja hakufunktioiden logiikka (`plant_lookup.h`).
Testaa puhtaan lookup-polun ilman LittleFS/ArduinoJson-riippuvuuksia.

- `plants_loadDefaults()` luo vähintään yhden kasvin
- `plants_getById("basil")` palauttaa validin kasvin
- Tuntematon ID palauttaa `NULL`
- `plants_getByIndex(0)` palauttaa validin kasvin
- Negatiivinen indeksi palauttaa `NULL`
- Liian suuri indeksi palauttaa `NULL`
- Oletusparametrit ovat järkeviä (`lightHours > 0`, `tempMaxC > tempMinC`)

### test_command_handler (9 testiä)

Komentokäsittelijän edge case -logiikka (`command_handler.h`) kevennetyllä
feature-surfaceella (native), ilman rautariippuvaisia polkuja.

- Tuntematon komento ei riko tilaa
- `LIGHT=1` asettaa forceOn/forceOff-liput oikein
- `LIGHT` tyhjällä arvolla käyttää nykyistä valotilaa togglaukseen
- `PLANT=mint` vaihtaa aktiivikasvin ja päivittää configin
- Tuntematon `PLANT`-arvo ei muuta aktiivikasvia
- `GROW_START` aktivoi kasvujakson
- `GROW_START` siemen-startilla valitsee taimivaiheen
- `GROW_NEXT` etenee seuraavaan vaiheeseen ja nollaa päivälaskurin
- `GROW_STOP` pysäyttää kasvujakson

## Hakemistorakenne

```
test/
  stubs/
    Arduino.h              millis()-stub, Serial-stub, String-stub (native build)
    test_feature_flags.h   yhteinen feature-flag setti kaikille testeille
  test_ebbflow_fsm/
    test_main.cpp
  test_scheduler/
    test_main.cpp
  test_motor_math/
    test_main.cpp
  test_config_defaults/
    test_main.cpp
  test_command_validation/
    test_main.cpp
  test_api_contract_validation/
    test_main.cpp
  test_lora_parser/
    test_main.cpp
  test_plant_lookup/
    test_main.cpp
  test_command_handler/
    test_main.cpp
  README.md
```

## Huomioita

- `millis()` on kontrolloitavissa testeissä: aseta `g_stub_millis`
- Hardware-funktiot (pump, motor, power) on stub-implementoitu test_scheduler:ssa
- `scheduler_updateEbbFlow()` ei ole testattu — FSM-logiikka katettu test_ebbflow_fsm:ssä
- `config_setDefaults()` on eriytetty `config_defaults.h`:iin jotta se on testattavissa ilman LittleFS-riippuvuutta
