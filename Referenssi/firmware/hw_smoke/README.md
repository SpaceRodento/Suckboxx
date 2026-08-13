# PlantMeister — Hardware smoke tests (WiFi+OTA)

Yksinkertaisia standalone-sketsejä, joilla varmistetaan V1-laitteiston perustoiminta **yksi kytkentä kerrallaan**. Jokainen sketsi on itsenäinen — ei sisällä `plantmeister/`-firmwaren rakennetta, tilakoneita tai konfiguraatiota. Yksi tarkoitus, ~50 riviä.

**WiFi+OTA-pohjainen.** Ensimmäinen flash USB:n yli, sen jälkeen kaikki päivitykset selaimella `http://<XIAO-IP>/update`. Lokia voi kuunnella koko ajan WiFin yli (`scripts/wireless_log_listener.py`) ilman että COM-portti varataan.

Pin-allokaatio: [`../shared/v1_pins.h`](../shared/v1_pins.h) (mirror of [`docs/laitteisto/kytkennat.md` § 1](../../docs/laitteisto/kytkennat.md)). WiFi/OTA-helper: [`../shared/smoke_wifi.h`](../shared/smoke_wifi.h). Secrets: `../plantmeister/secrets.h` (gitignored, samat makrot kuin pää-firmwaressa — pohja: [`../plantmeister/secrets.h.example`](../plantmeister/secrets.h.example)).

---

## Käyttö

### Ensimmäinen flash (USB)

```powershell
pio run -d firmware/hw_smoke -e light_dead_man -t upload
```

Yhden kerran riittää. Loki kertoo IP:n:
```
[smoke] WiFi OK IP=192.168.0.196
[smoke] OTA enabled: http://192.168.0.196/update
```

### Lokin kuuntelu (jää auki koko sessioksi)

```powershell
python scripts/wireless_log_listener.py
```

### OTA-päivitykset

1. Käännä uusi binary: `pio run -d firmware/hw_smoke -e light_dead_man`
2. Avaa selaimella `http://<XIAO-IP>/update` (Basic Auth `admin/admin` jos secrets.h ei muuta)
3. Valitse `firmware/hw_smoke/.pio/build/light_dead_man/firmware.bin`
4. Upload. Laite bootaa uudella firmwarella n. 30 s sisään.

### Sketsin vaihtaminen

```powershell
pio run -d firmware/hw_smoke -e pump_dead_man    # käännä
```
Sitten OTA-flash kuten yllä. Et tarvitse USB:tä koskaan uudelleen ennen kuin laite katoaa verkosta.

---

## Smoket

| Env | Pinni | Tarkoitus |
|---|---|---|
| `blink` | (LED skip jos -1) | XIAO bootaa, WiFi nousee, loki tikittää |
| `button_led` | BTN GPIO 9 | INPUT_PULLUP + debounce todetaan reaaliajassa |
| `i2c_scan` | SDA/SCL GPIO 5/6 | I2C-väylän laitteet ja pull-up-vastukset |
| `light_dead_man` | BTN + MOSFET_LIGHT GPIO 8 | **Nappi pohjassa = lamppu päällä**, vapaa = pois |
| `pump_dead_man` | BTN + MOSFET_PUMP GPIO 7 | Sama mutta vesipumppu (huom: vesi roiskuu) |
| `dht20_read` | SDA/SCL GPIO 5/6 | DHT20 (0x38) lämpö+kosteus — I2C-hubin ja väylän toimivuustesti |
| `mcp23017_read` | SDA/SCL GPIO 5/6 | MCP23017-laajentimen PORTA/B-pinnit (kytkin-/nappitesti) |
| `motor_dead_man` | BTN + DIR/STEP/EN GPIO 1/2/3 | Sama mutta MKS SERVO42C-MT, 1 kHz STEP |

---

## Suositeltu järjestys ensimmäisellä V1 PCB:llä

1. **`blink`** — varmista että laite bootaa ja WiFi-yhteys nousee
2. **`i2c_scan`** — varmista I2C-väylä (pull-upit toimivat)
3. **`button_led`** — varmista BTN GPIO 9 + debounce
4. **`light_dead_man`** — ensimmäinen MOSFET-testi (turvallisin, ei mekaniikkaa)
5. **`pump_dead_man`** — toinen MOSFET (vesi)
6. **`motor_dead_man`** — STEP/DIR/EN, vapaa akseli

Kunkin testin tulos kirjataan [TESTAUSPAIVAKIRJA.md](../../docs/TESTAUSPAIVAKIRJA.md):een. Hyväksymiskriteerit per moduuli: [.claude/rules/testaus.md § Rauta-smoke-resepti](../../.claude/rules/testaus.md#rauta-smoke-testin-perusresepti-moottori--pumppu--valo).

---

## Vianetsintä

| Oire | Syy |
|---|---|
| `WiFi FAILED` boot-lokissa | secrets.h:n WIFI_STA_SSID_DEFAULT väärä, tai verkko ei näy. Sketsi jatkaa silti USB-Serial-lokilla. |
| `[smoke] alive` -beacon ei näy listenerissä mutta muut rivit näkyvät | UDP-broadcast estetty PC:n palomuurissa. Aja `scripts/send_test_udp.ps1` paikallisesti varmistaakseesi listenerin. |
| Flash epäonnistuu `PermissionError` | COM-portti varattu — sulje serial-monitorit (PuTTY, Arduino IDE). WiFi-listener ei varaa COM:ia, mutta SerialMon:t kyllä. |
| OTA-päivitys onnistuu mutta laite ei bootaa uutta | Tarkista että upload meni `firmware.bin`-tiedostosta (ei `firmware.factory.bin`). |
| `gpio_set_level(227)` -virhe | Jokin pinni on -1 ja sitä koitetaan kirjoittaa. Tarkista TEST_LED_PIN-määrittely sketsissä. |

---

## Suhde pää-firmwareen

Nämä smoket eivät jaa koodia `firmware/plantmeister/`-puun kanssa, paitsi:
- `secrets.h` (WiFi-credit + OTA-Basic-Auth)
- `shared/v1_pins.h` (pin-allokaatio)
- `shared/smoke_wifi.h` (WiFi/OTA/UDP-helper, tämä on smoke-puolen lisäys)

Pää-firmware on monimutkaisempi (DeviceState, scheduler, UX-indicator, ebbflow-FSM). Jos haluat testata koko ketjun, käytä `firmware/plantmeister/`-firmwarea. Jos haluat eristää **yhden** ominaisuuden ja varmistaa että rauta toimii, käytä näitä smoketestejä.
