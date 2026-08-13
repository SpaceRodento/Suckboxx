/*=====================================================================
  config.h - PlantMeister Configuration

  ALL settings centralized here. No scattered #defines elsewhere.
  Change pins, timings, and features from this single file.
=====================================================================*/

#ifndef CONFIG_H
#define CONFIG_H

// Local secrets (gitignored). Provides OTA_USERNAME / OTA_PASSWORD and
// optional WIFI_STA_*_DEFAULT overrides. Falls back to defaults below
// if secrets.h is not present.
// Skipattu natiivitestikäännössä ettei kehittäjän paikalliset oletukset
// (esim. WIFI_STA_AUTOCONNECT_DEFAULT=true) vuoda yksikkötesteihin.
#if !defined(PLANTMEISTER_NATIVE_TEST) && __has_include("secrets.h")
  #include "secrets.h"
#endif

// ═══════════════════════════════════════════════════════════════════
// FEATURE FLAGS
// ═══════════════════════════════════════════════════════════════════

// ── Laiteprofiili ────────────────────────────────────────────────
// PM_PROFILE valitsee laitesukupolven oletukset. Profiili asettaa vain
// oletuksia: jokainen lippu on yha #ifndef-suojattu, joten envin -D voittaa.
// V2 = kasvatusasema (aktuaattorit + laajennin). V3 = sensoripaketti.
#define PM_PROFILE_V2 2
#define PM_PROFILE_V3 3
#ifndef PM_PROFILE
  #define PM_PROFILE PM_PROFILE_V2
#endif

#if PM_PROFILE == PM_PROFILE_V3
  // V3-sensoripaketti: ei aktuaattoreita, ei sailiota, ei laajenninta.
  #ifndef ENABLE_MOTOR
    #define ENABLE_MOTOR false
  #endif
  #ifndef ENABLE_PUMP
    #define ENABLE_PUMP false
  #endif
  #ifndef ENABLE_LIGHT_RELAY
    #define ENABLE_LIGHT_RELAY false
  #endif
  #ifndef ENABLE_AIR_PUMP
    #define ENABLE_AIR_PUMP false
  #endif
  #ifndef ENABLE_EBB_FLOW
    #define ENABLE_EBB_FLOW false
  #endif
  #ifndef ENABLE_FLOAT_SWITCH
    #define ENABLE_FLOAT_SWITCH false
  #endif
  #ifndef ENABLE_OVERFLOW_SWITCH
    #define ENABLE_OVERFLOW_SWITCH false
  #endif
  #ifndef ENABLE_RESERVOIR_LEVEL_SAFETY
    #define ENABLE_RESERVOIR_LEVEL_SAFETY false
  #endif
  #ifndef ENABLE_WATER_TEMP
    #define ENABLE_WATER_TEMP false
  #endif
  #ifndef HW_DS18B20
    #define HW_DS18B20 0
  #endif
  #ifndef HW_FLOAT_SWITCH_MIN
    #define HW_FLOAT_SWITCH_MIN 0
  #endif
  // Anturikartoitus lukittu 12.8.2026 (docs/kehitys/v3-sensoripaketti.md):
  // korkeutta (VL53L0X) ei tarvita V3:ssa.
  #ifndef ENABLE_HEIGHT_SENSOR
    #define ENABLE_HEIGHT_SENSOR false
  #endif
  #ifndef HW_VL53L0X
    #define HW_VL53L0X 0
  #endif
  // CAP_*-vastinparit HW_*-flageille: sensor_capability_resolver.h #error'aa
  // kaannosaikaisesti jos kapabiliteetti on paalla mutta sen ainoa tuettu HW-
  // toimittaja ei ole. Ilman naita build ei rikkoutuisi ENABLE_*-lipuista vaan
  // resolverin ristiriitatarkistuksesta.
  #ifndef CAP_WATER_TEMP
    #define CAP_WATER_TEMP false
  #endif
  #ifndef CAP_WATER_LEVEL
    #define CAP_WATER_LEVEL false
  #endif
  #ifndef CAP_PLANT_HEIGHT
    #define CAP_PLANT_HEIGHT false
  #endif
#endif

#ifndef ENABLE_LORA
  #define ENABLE_LORA             false   // V1: LoRa pois, varattu V2:lle (Wio-SX1262)
#endif
#ifndef ENABLE_WIFI_PORTAL
  #define ENABLE_WIFI_PORTAL      true
#endif
#ifndef ENABLE_SENSORS
  #define ENABLE_SENSORS          true
#endif
#ifndef ENABLE_MOTOR
  #define ENABLE_MOTOR            true
#endif
#ifndef ENABLE_PUMP
  #define ENABLE_PUMP             true
#endif
#ifndef ENABLE_LIGHT_RELAY
  #define ENABLE_LIGHT_RELAY      true
#endif
#ifndef ENABLE_AIR_PUMP
  #define ENABLE_AIR_PUMP         false   // V1: kiertävä ravinneliuos, ei tarvetta. DWC vaatisi.
#endif
// PCB v2: MCP23017 I2C GPIO-laajennin (0x20) — tacho, relokoitu FLOAT_MIN,
// air pump / valaistun napin varaukset. Ks. docs/laitteisto/pcb_v2_layout.md § 1.1b.
#ifndef ENABLE_MCP23017
  #define ENABLE_MCP23017         false   // set true on PCB v2 (16-bit I2C expander)
#endif
// PCB v2: tuuletin VPD-säätöön — 25 kHz LEDC-PWM D6:lla (Q3 + J12 pin 4) +
// karkea tacho-kuntoseuranta MCP23017 GPA0:sta. Tuuletin ottaa D6:n → FLOAT_MIN
// siirtyy MCP23017 GPA6:lle, joten ENABLE_FAN edellyttää ENABLE_MCP23017:ää.
// Ks. pcb_v2_layout.md § 2. Sama kytkentä ajaa 3- ja 4-johtotuuletinta (SB1-jumpperi).
#ifndef ENABLE_FAN
  #define ENABLE_FAN              false   // set true on PCB v2 (Q3 + J12 fan)
#endif
// SB1-jumpperi / firmware-tulkinta: false = 3-johto (tehon-PWM Q3:lla),
// true = 4-johto (erillinen 25 kHz PWM-linja pin 4:lle). Sama LEDC-kanava molemmille.
#ifndef FAN_4WIRE
  #define FAN_4WIRE               false
#endif
#ifndef ENABLE_BATTERY_MONITOR
  #define ENABLE_BATTERY_MONITOR  false   // Phase 6: INA219 not yet connected
#endif
#ifndef ENABLE_DEEP_SLEEP
  #define ENABLE_DEEP_SLEEP       false   // Future: battery operation
#endif
#ifndef ENABLE_SENSOR_HISTORY
  #define ENABLE_SENSOR_HISTORY   true
#endif
#ifndef ENABLE_EINK_DISPLAY
  #define ENABLE_EINK_DISPLAY     false   // E-Ink display pipeline (software baseline)
#endif
#ifndef ENABLE_GUIDED_GROWING
  #define ENABLE_GUIDED_GROWING   true
#endif
#ifndef ENABLE_GUIDED_GROWING_UI
  #define ENABLE_GUIDED_GROWING_UI true
#endif
#ifndef ENABLE_GUIDED_GROWING_SIMULATION
  #define ENABLE_GUIDED_GROWING_SIMULATION true
#endif
// Hardware-free sensor-value simulation (sensor_sim.h): fills plausible
// synthetic readings (air/leaf/CO2/spectrum/height/water/battery) when no
// real driver provided them, so the e-ink/portal UI can be demoed without
// any sensors. Default false; build with -DENABLE_SENSOR_SIMULATION=true
// for a no-hardware UI check. NOT for production. Distinct from the grow-
// phase simulation above.
#ifndef ENABLE_SENSOR_SIMULATION
  #define ENABLE_SENSOR_SIMULATION false
#endif

#ifndef ENABLE_DEVICE_STATE
  #define ENABLE_DEVICE_STATE     true   // Top-level device state machine (CORE-A)
#endif

#ifndef ENABLE_UX_INDICATOR
  #define ENABLE_UX_INDICATOR     true   // CORE-B: LED + e-ink unified driver
#endif

// -- Self-test (CORE-E) --
#ifndef ENABLE_SELF_TEST
  #define ENABLE_SELF_TEST           true
#endif

// Maximum total duration. Test should finish well under this.
#ifndef SELF_TEST_TIMEOUT_MS
  #define SELF_TEST_TIMEOUT_MS       3000
#endif

// Per-step soft timeout (ms) - for sensors or radios that may be slow
#ifndef SELF_TEST_STEP_TIMEOUT_MS
  #define SELF_TEST_STEP_TIMEOUT_MS  500
#endif

#ifndef ENABLE_BUTTON_INPUT
  #define ENABLE_BUTTON_INPUT     true   // CORE-C: physical button
#endif

// -- Input router (CORE-D2) --
#ifndef ENABLE_INPUT_ROUTER
  #define ENABLE_INPUT_ROUTER     true
#endif

// -- WiFi API extensions (CORE-F) --
#ifndef ENABLE_API_TEST_ENDPOINTS
  #define ENABLE_API_TEST_ENDPOINTS   true   // CORE-F: /api/test/{module}
#endif

// OTA firmware update via /update endpoint (shares wifi_portal AsyncWebServer).
// Requires ENABLE_WIFI_PORTAL=true. Empty OTA_PASSWORD = no auth (dev only).
#ifndef ENABLE_OTA
  #define ENABLE_OTA              true
#endif
#ifndef OTA_USERNAME
  #define OTA_USERNAME            "admin"
#endif
#ifndef OTA_PASSWORD
  #define OTA_PASSWORD            ""
#endif
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION        "0.1.0"
#endif

// Versio-identiteetti: scripts/pio_git_version.py injektoi nama build-flageina
// (git-SHA + PIO-envin nimi). Fallbackit kattavat natiivitestit ja buildit
// ilman skriptia. Nakyvat /api/status-kentissa fw_git + fw_env, jotta laitteelta
// voi kysya MIKA versio ja KUMPI env siina oikeasti ajaa (pm.py version).
#ifndef FW_GIT_SHA
  #define FW_GIT_SHA              "unknown"
#endif
#ifndef FW_PIO_ENV
  #define FW_PIO_ENV              "unknown"
#endif

// Runtime must stay alive this long before the boot is considered healthy:
// OTA rollback is cancelled and the crash-reboot counter is cleared.
#ifndef BOOT_HEALTHY_RUNTIME_MS
  #define BOOT_HEALTHY_RUNTIME_MS 60000UL
#endif

// Safe mode: after this many consecutive crash reboots, boot a minimal image
// (WiFi + OTA + status only) so a fix can be flashed. One-boot state — the next
// reboot retries a normal boot once a healthy runtime clears the crash chain.
#ifndef ENABLE_SAFE_MODE
  #define ENABLE_SAFE_MODE        true
#endif
#ifndef SAFE_MODE_CRASH_THRESHOLD
  #define SAFE_MODE_CRASH_THRESHOLD 3   // consecutive crash reboots
#endif

// Ebb&Flow (flood/drain) baseline
#ifndef ENABLE_EBB_FLOW
  #define ENABLE_EBB_FLOW         false
#endif
#ifndef ENABLE_RESERVOIR_LEVEL_SAFETY
  #define ENABLE_RESERVOIR_LEVEL_SAFETY true
#endif
#ifndef ENABLE_OVERFLOW_SWITCH
  #define ENABLE_OVERFLOW_SWITCH  true    // V1: FLOAT_OVF mukana
#endif

// Solenoid brake (not used in proto v1 — TR8 lead screw is self-locking)
#ifndef ENABLE_BRAKE
  #define ENABLE_BRAKE            false
#endif

// Phase 4 sensors (disabled until hardware connected)
#ifndef ENABLE_TDS_SENSOR
  #define ENABLE_TDS_SENSOR       false   // TDS via ADS1115
#endif
#ifndef ENABLE_WATER_TEMP
  #define ENABLE_WATER_TEMP       true    // V1: DS18B20 mukana (J7)
#endif
#ifndef ENABLE_FLOAT_SWITCH
  #define ENABLE_FLOAT_SWITCH     true    // V1: FLOAT_MIN mukana (J9)
#endif
#ifndef ENABLE_HEIGHT_SENSOR
  #define ENABLE_HEIGHT_SENSOR    true    // VL53L0X ToF
#endif
#ifndef ENABLE_ENV_SENSOR
  // DEPRECATED 2026-06-04: use HW_BME280 + CAP_AIR_TEMP_HUMIDITY (capability-
  // based config). Aliased downward via shim later in this file so old tests
  // (#define ENABLE_ENV_SENSOR false) keep working until they are migrated.
  #define ENABLE_ENV_SENSOR       true    // BME280
#endif

// ═══════════════════════════════════════════════════════════════════
// MOTOR DRIVER TYPES (must be defined before platform_hal.h)
// ═══════════════════════════════════════════════════════════════════

#define MOTOR_DRIVER_L298N            1   // Proto: full-step direct drive
#define MOTOR_DRIVER_TMC2208          2   // Open-loop microstepping (STEP/DIR/EN), yhteensopiva 42C-MT:n CR_vFOC-tilan kanssa
#define MOTOR_DRIVER_SERVO            3   // Hobby servo via PWM (single pin)
#define MOTOR_DRIVER_MKS_SERVO42D     4   // V2-varaus: 42D = CAN/Modbus (ei sama kuin 42C-MT)
#define MOTOR_DRIVER_MKS_SERVO42C_UART 5  // V1 optio: MKS SERVO42C-MT UART (CR_UART, closed-loop)
#define MOTOR_DRIVER_TMC2209          6   // Open-loop microstepping (STEP/DIR/EN), max 2A, 256 microstep, StallGuard4

// ═══════════════════════════════════════════════════════════════════
// PLATFORM SELECT
// ═══════════════════════════════════════════════════════════════════
// Change to true when flashing XIAO ESP32S3 + Wio-SX1262.
// All pin mappings are in platform_hal.h.

#ifndef USE_XIAO_SX1262
  #define USE_XIAO_SX1262         true    // V1: XIAO ESP32-S3 Plus on pää-MCU
#endif

// Target hardware profile for XIAO path:
// true  = XIAO ESP32S3 Plus (recommended for PlantMeister full feature set)
// false = Base XIAO ESP32S3 (limited GPIO, some pin sharing remains)
#ifndef USE_XIAO_PLUS_PINS
  #define USE_XIAO_PLUS_PINS      true
#endif

#include "platform_hal.h"

// ═══════════════════════════════════════════════════════════════════
// MCP23017 GPIO EXPANDER — pinnijako (PCB v2, pcb_v2_layout.md § 1.1b)
// ═══════════════════════════════════════════════════════════════════
// PORTA GPA0–7. Pin-indeksit 0..15 (PORTA = 0..7, PORTB = 8..15) —
// mcp23017_hal.h mappaa rekistereihin. Osoite 0x20 = I2C_ADDR_MCP23017.
//
// Sijaitsee ennen pinnijakoa, koska PIN_BUTTON ja PIN_BTN_LED_GREEN
// johdetaan näistä kun laajennin on käytössä (PCB v2).
#define MCP_PIN_FAN_TACHO           0       // GPA0 IN  — tacho (karkea pyörii/ei, IOC-polling)
#define MCP_PIN_AIRPUMP_EN          1       // GPA1 OUT — air pump (varaus, ENABLE_AIR_PUMP)
#define MCP_PIN_LED_R               2       // GPA2 OUT — valaistun napin punainen (varaus)
#define MCP_PIN_LED_G               3       // GPA3 OUT — vihreä (johdotettu 15.7.2026)
#define MCP_PIN_LED_B               4       // GPA4 OUT — sininen (varaus)
#define MCP_PIN_BTN_USER            5       // GPA5 IN  — käyttäjänapin kytkin (johdotettu 15.7.2026)
#define MCP_PIN_FLOAT_MIN           6       // GPA6 IN  — alaraja-uimuri (relokoitu D6:lta)

// Laajentimen pinnien koodaus GPIO-numeroavaruuteen.
//
// Miksi: ux_indicator ja button_input käsittelevät pinnejä numeroina, ja niiden
// olemassa oleva sopimus on "-1 = ei johdotettu, >= 0 = johdotettu". Kun LED tai
// nappi siirtyy laajentimelle, halutaan säilyttää sama sopimus sen sijaan että
// jokaiseen kutsupaikkaan lisättäisiin #if-haara. Arvo >= MCP_PIN_BASE tarkoittaa
// "GPAn laajentimella"; kirjoitus/luku reititetään mcp23017_hal.h:n kautta yhdessä
// paikassa (ux_writePin / button_readRaw). Näin kolmen valon napin lisääminen on
// yksi config-rivi (MCP_GPIO(MCP_PIN_LED_R)) eikä logiikkamuutos — juuri kuten
// ux_indicator.h:n green-only-fallback lupaa.
//
// 100 on turvallisesti ESP32-S3:n GPIO-avaruuden (0..48) yläpuolella, joten
// natiivi-GPIO ja laajenninpinni eivät voi sekoittua keskenään.
#define MCP_PIN_BASE                100
#define MCP_GPIO(n)                 (MCP_PIN_BASE + (n))
#define PIN_IS_ON_MCP(p)            ((p) >= MCP_PIN_BASE)

// ═══════════════════════════════════════════════════════════════════
// PIN MAPPINGS (based on platform_hal.h configuration)
// ═══════════════════════════════════════════════════════════════════

#if USE_XIAO_SX1262
  // ═══════════════════════════════════════════════════════════════════
  // V1 PIN ALLOCATION (XIAO ESP32-S3 Plus, vain castellated D0–D10)
  // Source of truth: docs/laitteisto/kytkennat.md § 1
  // V1 lukittu 2026-05-21. LoRa = erillinen XIAO (ei tämän laitteen pinneissä).
  // Moottori = MKS SERVO42C-MT (stepperi + integroitu driveri).
  //   Tehdasoletus: CR_vFOC (STEP/DIR/EN pulse) — yhteensopiva TMC2208-rajapinnan kanssa.
  //   Optio: CR_UART (closed-loop, encoder-feedback) — vaihdettavissa OLED-valikosta.
  //   Manuaali: docs/manuals/mks/MKS_SERVO42C_User_Manual_V1.1.2.pdf
  // Pinnijärjestys D0-D1-D2 = DIR-STEP-EN vastaa 42C-MT:n signaaliliittimen johdotusta.
  // Ei status-LEDiä boardilla V1:ssä — siirretty V2:n MCP23017-laajennukseen.
  // ═══════════════════════════════════════════════════════════════════
  #define PIN_MOTOR_DIR           1       // D0, GPIO1  — 42C-MT Dir-pinni (3.3-24V hyväksytty)
  #define PIN_MOTOR_STEP          2       // D1, GPIO2  — 42C-MT Stp-pinni (3.3-24V hyväksytty)
  #define PIN_MOTOR_EN            3       // D2, GPIO3  — 42C-MT En-pinni (active LOW oletus). HUOM strapping-pin.
  #define PIN_ONEWIRE             4       // D3, GPIO4  — DS18B20 ×N
  #define PIN_I2C_SDA             5       // D4, GPIO5
  #define PIN_I2C_SCL             6       // D5, GPIO6
  // D6 (GPIO43): PCB v2 antaa D6:n tuulettimen 25 kHz PWM:lle (Q3 + J12 pin 4).
  // Silloin FLOAT_MIN siirtyy MCP23017 GPA6:lle (PIN_FLOAT_SWITCH = -1 → luetaan
  // expanderilta, ks. sensor_manager.h). V1 (ENABLE_FAN=false): D6 = FLOAT_MIN.
  #if ENABLE_FAN
    #define PIN_FAN_PWM           43      // D6, GPIO43 — FAN_PWM (LEDC 25 kHz)
    #define PIN_FLOAT_SWITCH      -1      // FLOAT_MIN relokoitu → MCP23017 GPA6
  #else
    #define PIN_FAN_PWM           -1      // ei tuuletinta → D6 on FLOAT_MIN
    #define PIN_FLOAT_SWITCH      43      // D6, GPIO43 — FLOAT_MIN (vesi loppu, INPUT_PULLUP)
  #endif
  #define PIN_FLOAT_SWITCH_OVERFLOW 44    // D7, GPIO44 — FLOAT_OVF (ylivuoto, INPUT_PULLUP, aina natiivi)
  #define PIN_PUMP                7       // D8, GPIO7  — vesipumppu MOSFET gate
  #define PIN_RELAY_LIGHT         8       // D9, GPIO8  — kasvilamppu MOSFET gate
  #ifndef PIN_BUTTON
    #if ENABLE_MCP23017
      // PCB v2: käyttäjänappi on laajentimen GPA5:ssä (johdotettu 15.7.2026).
      // Pull-up tulee MCP:n GPPUA:sta (PORTA_PULLUP_MASK), ei pinModesta.
      #define PIN_BUTTON          MCP_GPIO(MCP_PIN_BTN_USER)
    #else
      #define PIN_BUTTON          9       // D10, GPIO9 — fyysinen TACT-nappi (INPUT_PULLUP)
    #endif
  #endif

  // ── Status-LED siirretty V2:n MCP23017-laajennukseen ──
  // V1: ei boardi-LEDiä. Käyttäjäpalaute tulee e-ink-seinätaulusta (reTerminal E1001, WiFi/MQTT).
  // ux_indicator.h:n LED-kutsut ovat no-op kun PIN_LED == -1.
  #define PIN_LED                 -1
  #ifndef PIN_LED_GREEN
    #define PIN_LED_GREEN         -1
  #endif
  #ifndef PIN_LED_YELLOW
    #define PIN_LED_YELLOW        -1
  #endif
  #ifndef PIN_LED_RED
    #define PIN_LED_RED           -1
  #endif

  // ── Napin oma merkkivalo (integroitu painikkeen LED) ──
  // V1: nappiin tulee aluksi VIHREÄ valo, myöhemmin myös punainen. Aseta
  // GPIO tähän kun johdotat valon — koodi (ux_indicator) ajaa sen heti.
  // Punainen voi jäädä -1:ksi: green-only-fallback näyttää värin vilkkuna.
  //
  // PCB v2 (15.7.2026): vihreä on johdotettu laajentimen GPA3:een ja on tästä
  // eteenpäin laitteen oletusmerkkivalo. Kun kolmen valon nappi saapuu, punainen
  // on yksi rivi lisää (MCP_GPIO(MCP_PIN_LED_R)) — ux_indicator siirtyy silloin
  // itsestään green-only-fallbackista kaksivärilogiikkaan ilman koodimuutosta.
  #ifndef PIN_BTN_LED_GREEN
    #if ENABLE_MCP23017
      #define PIN_BTN_LED_GREEN   MCP_GPIO(MCP_PIN_LED_G)
    #else
      #define PIN_BTN_LED_GREEN   -1
    #endif
  #endif
  #ifndef PIN_BTN_LED_RED
    #define PIN_BTN_LED_RED       -1      // kolmen valon nappi: MCP_GPIO(MCP_PIN_LED_R)
  #endif

  #define PIN_BATTERY_ADC         -1      // V1: ei battery-monitorointia

  // ── V1: ei käytössä — varattu V2:lle ──
  #define PIN_RELAY_AIR_PUMP      -1      // V2: DWC-aerator MCP23017:n kautta
  #ifndef PIN_RELAY_SOLENOID
    #define PIN_RELAY_SOLENOID    -1      // V2: solenoidiventtiili MCP23017:n kautta
  #endif

  // ── LoRa: erillinen XIAO Wio-SX1262, EI tämän laitteen pinneissä ──
  // ENABLE_LORA=false PlantMeister-MCU:ssa. Pinit -1 jotta lora_sx1262.h kompiloituu.
  #define PIN_LORA_RX             -1
  #define PIN_LORA_TX             -1
  #define PIN_LORA_NSS            -1
  #define PIN_LORA_SCLK           -1
  #define PIN_LORA_MOSI           -1
  #define PIN_LORA_MISO           -1
  #define PIN_LORA_RESET          -1
  #define PIN_LORA_BUSY           -1
  #define PIN_LORA_DIO1           -1
  #define PIN_LORA_RF_SW          -1

  // V1 oletus: TMC2208-rajapinta toimii 42C-MT:n CR_vFOC-tehdasoletuksella.
  // Vaihda MOTOR_DRIVER_MKS_SERVO42C_UART jos haluat closed-loop UART-ohjauksen
  // (vaatii OLED-konfiguroinnin: Mode→CR_UART, UartBaud→38400, UartAddr→0xE0).
  // #ifndef-suojaus mahdollistaa build_flags-overriden (-DMOTOR_DRIVER=5).
  #ifndef MOTOR_DRIVER
    #define MOTOR_DRIVER          MOTOR_DRIVER_TMC2208
  #endif
#else
  // ESP32 WROOM-32E (default)
  #define PIN_LED                 2
  #define PIN_I2C_SDA             21
  #define PIN_I2C_SCL             22
  #define PIN_MOTOR_STEP          12
  #define PIN_MOTOR_DIR           14
  #define PIN_MOTOR_EN            27
  #define PIN_PUMP                26
  #define PIN_RELAY_LIGHT         15
  #define PIN_RELAY_AIR_PUMP      4       // moved from 32/33 — reserved for MPG encoder
  #define PIN_LORA_RX             16      // Serial2 RX
  #define PIN_LORA_TX             17      // Serial2 TX
  #ifndef PIN_BUTTON
    #define PIN_BUTTON             18      // GPIO17 used by LoRa TX on WROOM
  #endif
  #define PIN_LORA_NSS            -1
  #define PIN_LORA_SCLK           -1
  #define PIN_LORA_MOSI           -1
  #define PIN_LORA_MISO           -1
  #define PIN_LORA_RESET          -1
  #define PIN_LORA_BUSY           -1
  #define PIN_LORA_DIO1           -1
  #define PIN_LORA_RXEN           -1
  #define PIN_ONEWIRE             13      // DS18B20
  #define PIN_FLOAT_SWITCH        25
  #define PIN_FLOAT_SWITCH_OVERFLOW 35    // moved from 32 — reserved for MPG encoder
  #define PIN_BATTERY_ADC         34
  // MPG encoder (Micronor MR 190.7, RS422 via level shifter 5V→3.3V)
  #define PIN_MPG_A               32
  #define PIN_MPG_B               33

// MPG handwheel settings
#define ENABLE_MPG                true
#define MPG_MM_PER_DETENT         2     // mm to move per click of the handwheel
#define MPG_TEST_ENABLED          false  // set true only for raw signal testing
#define MOTOR_DIAG_ON_BOOT        false  // set true to cycle L298N pins at startup for multimeter verification

  // L298N direct-drive pins (only used when MOTOR_DRIVER == MOTOR_DRIVER_L298N)
  #define PIN_MOTOR_IN1           27
  #define PIN_MOTOR_IN2           14
  #define PIN_MOTOR_IN3           12
  #define PIN_MOTOR_IN4           13
  #ifndef MOTOR_DRIVER
    #define MOTOR_DRIVER          MOTOR_DRIVER_L298N    // Proto: L298N full-step
  #endif
#endif

// Alias: motor_hal.h uses PIN_MOTOR_ENABLE, config defines PIN_MOTOR_EN
#define PIN_MOTOR_ENABLE          PIN_MOTOR_EN

// Fan PWM pin fallback for profiles that don't define it (only the XIAO Plus
// profile routes the fan to D6). -1 = no fan output on this profile.
#ifndef PIN_FAN_PWM
  #define PIN_FAN_PWM             -1
#endif

// Backward compatibility alias used in some sensor code
#define PIN_DS18B20               PIN_ONEWIRE

// Fallback LED pins for non-XIAO profiles
#ifndef PIN_LED_GREEN
  #define PIN_LED_GREEN            PIN_LED
#endif
#ifndef PIN_LED_YELLOW
  #define PIN_LED_YELLOW           -1
#endif
#ifndef PIN_LED_RED
  #define PIN_LED_RED              -1
#endif

// Fallback button-LED pins for non-XIAO profiles (default: not wired).
#ifndef PIN_BTN_LED_GREEN
  #define PIN_BTN_LED_GREEN        -1
#endif
#ifndef PIN_BTN_LED_RED
  #define PIN_BTN_LED_RED          -1
#endif

// LED active-high default. Set to 1 if wiring is active-low.
#ifndef LED_ACTIVE_LOW
  #define LED_ACTIVE_LOW           0
#endif

// Button-LED polarity. Defaults to the same wiring assumption as the
// status LED; override if the button's integrated LED is active-low.
#ifndef BTN_LED_ACTIVE_LOW
  #define BTN_LED_ACTIVE_LOW       LED_ACTIVE_LOW
#endif

// LED blink intervals (ms)
#ifndef LED_BLINK_FAST_MS
  #define LED_BLINK_FAST_MS        250    // Red fault, yellow self-test
#endif
#ifndef LED_BLINK_SLOW_MS
  #define LED_BLINK_SLOW_MS        1500   // Green awaiting user
#endif

// UX indicator update interval (ms)
#ifndef UX_TICK_INTERVAL_MS
  #define UX_TICK_INTERVAL_MS      100
#endif

// Napinpainalluksen vahvistusvälähdys: kun nappi painetaan pohjaan, merkkivalo
// (status + napin oma LED) antaa heti muutaman nopean pulssin — "rekisteröin
// painalluksesi" — riippumatta siitä mitä painallus lopulta tekee. Ohittaa
// tilakuvion vain väläyksen ajan. Kesto/pulssi valittu niin että näkyy 3
// selvästi erottuvaa välähdystä (900/150 = 6 vaihtoa). 100 ms:n vaihtoväli
// (aiempi arvo) osoittautui rautatestissa liian nopeaksi silmälle - näytti
// yhdeltä pieneltä vilahdukselta kolmen sijaan (kayttajan havainto 22.7.2026).
// ux_tick ohittaa tickrajoituksen väläyksen ajaksi, jotta pulssi on terävä
// eikä 100 ms tickiin sidottu.
#ifndef UX_ACK_FLASH_MS
  #define UX_ACK_FLASH_MS          900
#endif
#ifndef UX_ACK_FLASH_TOGGLE_MS
  #define UX_ACK_FLASH_TOGGLE_MS   150
#endif

// -- Button input (CORE-C) --
#ifndef BUTTON_ACTIVE_LOW
  #define BUTTON_ACTIVE_LOW        1
#endif

#ifndef BUTTON_DEBOUNCE_MS
  #define BUTTON_DEBOUNCE_MS       30
#endif

#ifndef BUTTON_LONG_PRESS_MS
  #define BUTTON_LONG_PRESS_MS     5000
#endif

#ifndef BUTTON_POLL_INTERVAL_MS
  #define BUTTON_POLL_INTERVAL_MS  10
#endif

// -- Tehdasreset-ele: nappi pohjassa kaynnistyksessa (factory_reset_gesture.h) --
//
// Miksi ele on olemassa vaikka /api/command FACTORY_RESET on jo: resetin kaksi
// tyypillisinta syyta ovat unohtunut salasana ja vaara WiFi-verkko, ja
// molemmissa API-reitti on juuri se joka ei toimi. Fyysinen paasy laitteeseen
// on oikea valtuutus — se ei tarvitse verkkoa, salasanaa eika tietokonetta.
#ifndef ENABLE_FACTORY_RESET_GESTURE
  #define ENABLE_FACTORY_RESET_GESTURE  true
#endif

// Kauanko nappia pidetaan bootissa ennen nollausta. Tarkoituksella 2x
// BUTTON_LONG_PRESS_MS (5 s = huoltotila): kaksi eri asiaa samalla napilla
// eivat saa sekoittua edes epahuomiossa. Ele vaatii lisaksi etta nappi on
// pohjassa jo kaynnistyshetkella, joten sita ei voi laukaista ajon aikana.
#ifndef FACTORY_RESET_HOLD_MS
  #define FACTORY_RESET_HOLD_MS         10000U
#endif

// Merkkivalon vilkkujakso eleen alussa ja juuri ennen laukeamista (ms).
// Kiihtyva vilkku kertoo etenemisen ilman naytto: kayttaja nakee milloin
// irrottaa jos ei sittenkaan halunnut nollata.
#ifndef FACTORY_GESTURE_BLINK_SLOW_MS
  #define FACTORY_GESTURE_BLINK_SLOW_MS 600U
#endif

#ifndef FACTORY_GESTURE_BLINK_FAST_MS
  #define FACTORY_GESTURE_BLINK_FAST_MS 80U
#endif

// Tasainen valo nollauksen jalkeen: "meni lapi". Nakyy ennen kuin kayttaja
// ehtii irrottaa sormensa, joten han ei joudu arvaamaan onnistuiko ele.
#ifndef FACTORY_GESTURE_CONFIRM_MS
  #define FACTORY_GESTURE_CONFIRM_MS    1000U
#endif

// Stepper: Saehan 2S42Q (5V/0.34A, NEMA 17, 1.8°/step) + TR8 lead screw
#define MOTOR_STEPS_PER_REV     200
#define MOTOR_LEAD_MM           8       // TR8 = 8mm per full revolution
#define MOTOR_MAX_HEIGHT_MM     200
#define MOTOR_HEIGHT_MARGIN_MM  30      // Raise lamp when plant is this close

// Set true to invert motor direction (swap UP/DOWN without rewiring)
#define MOTOR_DIRECTION_INVERT  true

// L298N settings (AccelStepper HALF4WIRE)
// Acceleration tuning notes (L298N HALF4WIRE, tested 2026-04-04):
//   1200: Quite good
//   1000 speed: best running sound, fast enough
//   800/600/400: bad resonance noise — avoid these speeds
//  1500: Excellent running sound
#define MOTOR_L298N_MAX_SPEED       1500.0f  // half-steps/sec — tested optimum

//   High accel sweeps through resonance fast but may cause mechanical jolt.
//   Best subjective baseline so far: profile 5 family.
#define MOTOR_L298N_ACCELERATION    11000.0f  // half-steps/sec²
// Two-stage braking profile near target to soften stop sound.
// Stage 1 decel starts first, then stage 2 final decel + lower final speed near stop.
#define MOTOR_L298N_DECELERATION    5200.0f   // stage 1 half-steps/sec²
#define MOTOR_L298N_DECEL_WINDOW_STEPS 420L   // stage 1 window
#define MOTOR_L298N_FINAL_DECELERATION 3300.0f  // stage 2 half-steps/sec² (locked 2026-04-04)
#define MOTOR_L298N_FINAL_DECEL_WINDOW_STEPS 180L
#define MOTOR_L298N_FINAL_MAX_SPEED 1420.0f   // half-steps/sec near target (locked 2026-04-04)

// Hold coils after stop to avoid mechanical snap/noise at start/stop transitions.
// If true, L298N outputs stay energized while idle (more heat/power, smoother restart).
#define MOTOR_L298N_HOLD_ON_IDLE    false
// Used only when MOTOR_L298N_HOLD_ON_IDLE is false.
#define MOTOR_L298N_RELEASE_DELAY_MS 800UL
// Optional wake pulse before first step after idle-disable (non-blocking).
// Helps some L298N + stepper combos reduce harsh startup/jammy sound.
#define MOTOR_L298N_WAKE_PULSE_MS    30UL

// MPG command shaping: group fast detents into one move to reduce start/stop chatter.
#define MPG_BATCH_WINDOW_MS         80UL
#define MPG_BATCH_MAX_MM            12

// Continuous rotation servo settings (MOTOR_DRIVER_SERVO)
#define PIN_MOTOR_SERVO           26    // PWM pin — muuta tarvittaessa
#define MOTOR_SERVO_STOP_PWM      90    // write() value for stop (neutral)
#define MOTOR_SERVO_SPEED_PWM     45    // offset from stop (stop±speed = direction)
#define MOTOR_SERVO_MM_PER_SEC    10.0f // calibrate: how many mm/sec at full speed

// TMC2208 settings (for future upgrade)
#define MOTOR_TMC_MICROSTEPS    16      // MS1/MS2 floating = 16
#define MOTOR_TMC_MAX_SPEED     500.0f  // steps/sec
#define MOTOR_TMC_ACCELERATION  200.0f  // steps/sec²

#define MOTOR_TMC_HOME_SPEED    200.0f  // steps/sec during homing

// ═══════════════════════════════════════════════════════════════════
// TMC2209 settings (MOTOR_DRIVER_TMC2209)
// ═══════════════════════════════════════════════════════════════════
// TMC2209 on TMC2208:n seuraaja: STEP/DIR/EN-rajapinta identtinen, mutta
//  - Max moottorivirta 2A RMS (vs TMC2208 1.4A) — paremmin NEMA17:lle
//  - Microstep 1..256 (MS1/MS2-pinejä luetaan vain bootissa; 256us vaatii UART:n)
//  - StallGuard4 + CoolStep (vaatii UART-konfiguroinnin — V1:ssä STEP/DIR-tila)
// V1:ssä käytämme STEP/DIR-tilaa, sama kuin TMC2208. Pin-allokaatio sama
// (DIR=D0, STEP=D1, EN=D2). MS1/MS2-pinit jätetään BTT-driver-PCB:n omille
// jumppereille (yleensä 16us oletus).
#define MOTOR_TMC2209_MICROSTEPS    16      // MS1/MS2 jumpperit = 16 (BTT-default)
#define MOTOR_TMC2209_MAX_SPEED     800.0f  // steps/sec — TMC2209 sietää korkeampaa kuin TMC2208
#define MOTOR_TMC2209_ACCELERATION  400.0f  // steps/sec²

#define MOTOR_TMC2209_HOME_SPEED    200.0f  // steps/sec during homing

// ═══════════════════════════════════════════════════════════════════
// MKS SERVO42C-MT UART settings (MOTOR_DRIVER_MKS_SERVO42C_UART)
// ═══════════════════════════════════════════════════════════════════
// Pakollinen ensikäyttöön: OLED-valikosta Mode→CR_UART, UartBaud, UartAddr.
// Manuaali: docs/manuals/mks/MKS_SERVO42C_User_Manual_V1.1.2.pdf
// UART TTL 3.3V — suora kytkentä XIAO:lle ilman level-shiftiä.
// HUOM: kun MOTOR_DRIVER = MKS_SERVO42C_UART, PIN_MOTOR_DIR/STEP/EN ovat
// käyttämättömiä — liikkeenohjaus tapahtuu UART-komennoilla F3/F6/FD/F7.
#define MKS_UART_BAUD               38400UL   // 42C-MT default. Valid: 9600/19200/25000/38400/57600/115200
#define MKS_UART_SLAVE_ADDR         0xE0      // Default; alue 0xE0..0xE9
#define MKS_UART_TIMEOUT_MS         200U      // Per-komento response-odotus (manuaali ei kerro tarkkaa)
#define MKS_UART_MICROSTEPS         16        // 0x84-komento; default 16 (MStep=0x10)
#define MKS_UART_DEFAULT_SPEED      0x10      // F6-komennon speed-kenttä (0..127), 16 = ~150 RPM @ MStep=16

// XIAO ESP32S3:n Serial1 -pinit MKS UART:lle.
// V1 PCB:n D0-D10 on lukittu (kts. kytkennät.md § 1). Kun MOTOR_DRIVER on
// MKS_SERVO42C_UART, STEP/DIR-pinejä ei käytetä → kierrätä ne UART:lle:
//   D0 (GPIO1) → 42C-MT UART Rx  (XIAO TX)
//   D1 (GPIO2) → 42C-MT UART Tx  (XIAO RX)
//   D2 (GPIO3) → vapaa (EN ei käytössä CR_UART-tilassa, ohjataan F3-komennolla)
// Patch-johto: 42C-MT signaaliliittimen Dir/Stp-pinit pois, UART-liittimen Rx/Tx
// XIAO:n D0/D1:een. Yhteinen GND molemmissa liittimissä.
#ifndef PIN_MKS_UART_TX
  #define PIN_MKS_UART_TX           1         // D0, GPIO1  → 42C-MT UART Rx
#endif
#ifndef PIN_MKS_UART_RX
  #define PIN_MKS_UART_RX           2         // D1, GPIO2  ← 42C-MT UART Tx
#endif

// Polling: encoder-aseman luenta motor_update():ssa
#define MKS_UART_POLL_INTERVAL_MS   100U      // 10 Hz position polling

// ═══════════════════════════════════════════════════════════════════
// TIMING
// ═══════════════════════════════════════════════════════════════════

// Per-driver read() wall-clock budget. Three consecutive overruns drop the
// driver's readyFlag (runtime degradation, architecture § 8). Advisory is
// raised so health_check / /api/status surface the slow driver.
#ifndef SENSOR_READ_BUDGET_MS
  #define SENSOR_READ_BUDGET_MS     500UL
#endif

#define SENSOR_READ_INTERVAL_MS     30000UL     // 30 seconds

// Kuinka kauan viimeisintä hyvää ympäristönäytettä (T/RH → VPD, CO₂)
// pidetään voimassa kun ajuri ei tuottanut lukemaa tällä kierroksella.
// Yksi hukattu SCD41-näyte pudotti aiemmin koko env-lohkon kerralla, jolloin
// VPD katosi seinätaululta satunnaisesti (havaittu 29.7.2026). Ks.
// sensor_sticky.h. Pitkä tarpeeksi kattamaan vaihelukko 30 s luennan ja
// anturin 5 s jakson välillä, lyhyt tarpeeksi ettei kuollut anturi jää
// näyttämään tuoreelta.
#ifndef SENSOR_ENV_STICKY_MS
  #define SENSOR_ENV_STICKY_MS      300000UL    // 5 min
#endif
#define LORA_REPORT_INTERVAL_MS     120000UL    // 2 minutes
#define WIFI_AP_DURATION_MS         900000UL    // 15 minutes after boot
#define HEIGHT_CHECK_INTERVAL_MS    3600000UL   // 1 hour
#define LED_HEARTBEAT_INTERVAL_MS   5000UL      // 5 sec heartbeat blink
#define WIFI_STATUS_POLL_MS         5000UL      // Web UI auto-refresh
#define EINK_UPDATE_INTERVAL_MS     300000UL    // 5 min refresh cadence

// ═══════════════════════════════════════════════════════════════════
// PUMP & WATERING
// ═══════════════════════════════════════════════════════════════════

#define PUMP_ML_PER_SEC             1.5f    // Calibrate for your pump!
#define PUMP_OUTPUT_ACTIVE_HIGH     true    // false if relay/MOSFET logic is inverted

// Runtime calibration defaults
#define CALIB_TDS_OFFSET_DEFAULT     0.0f
#define CALIB_TDS_GAIN_DEFAULT       1.0f
#define CALIB_PUMP_ML_PER_SEC_DEFAULT PUMP_ML_PER_SEC
#define CALIB_PPFD_FACTOR_DEFAULT    1.0f   // relative (uncalibrated); set via /api/calib/ppfd
// Sensor-to-canopy geometry scale (1.0 = AS7341 sits at canopy level).
// Kept separate from the absolute factor above because the two have different
// lifetimes: the absolute factor is a sensor property (changes only with gain
// or a reference meter), the geometry factor changes every time the lamp or the
// sensor is physically moved. Set via /api/calib/ppfd/geometry.
#define CALIB_PPFD_GEOMETRY_DEFAULT  1.0f
#define CALIB_PPFD_GEOMETRY_MAX      50.0f  // sanity bound; ~7x distance ratio

// --- DLI (Daily Light Integral) tracking ---
#ifndef DLI_DAY_LENGTH_MS
  #define DLI_DAY_LENGTH_MS        86400000UL  // 24 h accumulation window
#endif
#ifndef DLI_MAX_SAMPLE_GAP_MS
  #define DLI_MAX_SAMPLE_GAP_MS    900000UL    // 15 min — clamp dt so one long gap (sleep/outage) can't poison the day's integral
#endif
#ifndef DLI_OPTIMAL_MIN_DEFAULT
  #define DLI_OPTIMAL_MIN_DEFAULT  12.0f       // mol/m²/d — generic optimum lower bound
#endif
#ifndef DLI_OPTIMAL_MAX_DEFAULT
  #define DLI_OPTIMAL_MAX_DEFAULT  17.0f       // mol/m²/d — generic optimum upper bound
#endif

// Ebb&Flow timings. #ifndef-guarded so a build env (esim. xiao_esp32s3_ebbflow)
// voi yliajaa lyhyemmillä testiajoilla -D-flageilla ilman tämän tiedoston muokkausta.
#ifndef EBB_FLOW_FLOOD_INTERVAL_MIN
#define EBB_FLOW_FLOOD_INTERVAL_MIN 180UL   // 3h between flood cycles (lights on)
#endif
#ifndef EBB_FLOW_FLOOD_DURATION_SEC
#define EBB_FLOW_FLOOD_DURATION_SEC 45UL
#endif
#ifndef EBB_FLOW_SOAK_DURATION_SEC
#define EBB_FLOW_SOAK_DURATION_SEC  120UL
#endif
#ifndef EBB_FLOW_DRAIN_TIMEOUT_SEC
#define EBB_FLOW_DRAIN_TIMEOUT_SEC  240UL   // 4 min — anna hitaalle painovoimatyhjennykselle marginaalia (havainto 8.6: tyhjeni hitaasti)
#endif
#ifndef EBB_NIGHT_FLOOD_MULTIPLIER
#define EBB_NIGHT_FLOOD_MULTIPLIER  2U      // Night interval = day interval × this
#endif
#ifndef EBB_FLOW_MAX_OVERFLOW_RETRIES
#define EBB_FLOW_MAX_OVERFLOW_RETRIES 3     // perättäistä ylivuotoa ennen kovaa latchia (stand-pipe rikki)
#endif
// Kierto ("circulation"): matala anti-stagnaatio-kierto täysien tulvien välissä.
// Pumppu käy soak-duty:llä lyhyen ajan, ei nosta vettä kasveille. Opt-in.
#ifndef EBB_CIRCULATE_INTERVAL_MIN
#define EBB_CIRCULATE_INTERVAL_MIN 20U      // kierto joka 20 min (pumppu hiljainen)
#endif
#ifndef EBB_CIRCULATE_DURATION_SEC
#define EBB_CIRCULATE_DURATION_SEC 120U     // 2 min kiertoa per sykli
#endif
#ifndef EBB_CIRCULATE_DUTY_PCT
#define EBB_CIRCULATE_DUTY_PCT     PUMP_SOAK_MIN_DUTY_PCT  // matalin pyörivä = turvallisin oletus, säädä raudalla
#endif
// Float-kytkinten debounce: raaka lukema otetaan käyttöön vasta kun se on pysynyt
// vakaana tämän ajan. Estää yksittäisen kohinapiikin (mekaaninen pomppu, sähköhäiriö)
// aiheuttaman väärän pumpunpysäytyksen / dry-run-/overflow-reaktion. 300 ms on lyhyt
// suhteessa flood-aikaan mutta hylkää yksittäiset näytteet.
#ifndef FLOAT_DEBOUNCE_MS
#define FLOAT_DEBOUNCE_MS           300UL
#endif

// Hardware-level pump safety — absolute ceiling regardless of FSM state.
// If pump runs longer than this, pump_update() force-stops it.
#define PUMP_ABSOLUTE_MAX_ON_MS     300000UL  // 5 min hard limit

// Opt-in overflow latch auto-clear (DeviceConfig.ebbOverflowAutoClear, default
// OFF). When the operator enables it, the FLOAT_OVF latch releases this long
// after the bed has drained below the overflow float, so an isolated overflow
// recovers unattended. OFF keeps the manual-clear safety latch.
#ifndef OVERFLOW_AUTO_CLEAR_SETTLE_MS
#define OVERFLOW_AUTO_CLEAR_SETTLE_MS  30000UL  // 30 s drained before auto-clear
#endif

// Continuous-flow soak: lower bound for the trimmed soak duty. A peristaltic
// pump stalls / chatters below a certain duty, so the soak-hold wizard (+/- and
// numeric trim) and the SOAK phase clamp any non-zero duty up to this floor.
// 0 stays valid and means "soak with pump off" (legacy stand-pipe hold).
// To be refined on hardware (project_pump_pwm_wip min-duty floor ~<30%).
#ifndef PUMP_SOAK_MIN_DUTY_PCT
#define PUMP_SOAK_MIN_DUTY_PCT      25
#endif
// Duty the soak-hold wizard drops to when the operator presses "hold here"
// (then fine-tunes with +/- around it). ~30% per the soak-hold design.
#ifndef PUMP_SOAK_HOLD_START_PCT
#define PUMP_SOAK_HOLD_START_PCT    30
#endif

// ═══════════════════════════════════════════════════════════════════
// LORA — RYLR890 AT COMMANDS
// ═══════════════════════════════════════════════════════════════════

// Shared LoRa radio contract (must match gateway/other nodes)
// Used by both RYLR890 (AT params) and SX1262 (RadioLib).
#define LORA_RF_FREQUENCY_HZ       868000000UL
#define LORA_RF_FREQUENCY_MHZ      (LORA_RF_FREQUENCY_HZ / 1000000.0f)
#define LORA_RF_BANDWIDTH_KHZ      125
#define LORA_RF_SPREADING_FACTOR   10
#define LORA_RF_CODING_RATE_DENOM  5        // 4/5
#define LORA_RF_SYNC_WORD          0x34
#define LORA_RF_PREAMBLE_LEN       4

// RYLR890 AT+PARAMETER mapping for contract above
// BW index: 7 => 125 kHz, CR index: 1 => 4/5
#define RYLR_PARAM_SF              LORA_RF_SPREADING_FACTOR
#define RYLR_PARAM_BW              7
#define RYLR_PARAM_CR              1
#define RYLR_PARAM_PREAMBLE        LORA_RF_PREAMBLE_LEN

#define LORA_BAUD                   115200
#define LORA_DEVICE_ADDRESS         10
#define LORA_NETWORK_ID             5
#define LORA_TARGET_ADDRESS         1       // RPi RECEIVER address
#define LORA_BROADCAST_ADDR         0
#define LORA_AT_TIMEOUT_MS          2000
#define LORA_MAX_PAYLOAD            240     // RYLR890 max bytes

#define LoRaSerial                  Serial2

// ═══════════════════════════════════════════════════════════════════
// WIFI ACCESS POINT (AP + STA DUAL MODE)
// ═══════════════════════════════════════════════════════════════════

// AP: Device creates its own network
#define WIFI_AP_SSID                "PlantMeister"
#define WIFI_AP_PASSWORD            "kasvi1234"     // Min 8 chars
#define WIFI_AP_CHANNEL             6
#define WIFI_AP_MAX_CLIENTS         2

// STA: Connect to home WiFi (empty = skip, set via portal or config.json)
// These are defaults only — override via secrets.h, config.json or portal settings.
// secrets.h:n #define yliajaa namat (ifndef-guard), niin tyontiset
// kotiverkkocredit pysyvat repon ulkopuolella mutta lautyvat boottiin.
#ifndef WIFI_STA_SSID_DEFAULT
  #define WIFI_STA_SSID_DEFAULT       ""              // Home network SSID
#endif
#ifndef WIFI_STA_PASSWORD_DEFAULT
  #define WIFI_STA_PASSWORD_DEFAULT   ""              // Home network password
#endif
#ifndef WIFI_STA_AUTOCONNECT_DEFAULT
  #define WIFI_STA_AUTOCONNECT_DEFAULT false          // Auto-connect on boot if SSID set
#endif

// Timing
#define WIFI_WEB_PORT               80
#define WIFI_MDNS_HOSTNAME          "plantmeister"   // -> plantmeister.local
#define WIFI_CONNECT_TIMEOUT_MS     10000UL         // STA connection timeout
#define WIFI_PORTAL_REQUIRE_PIN     false           // Require PIN login when adminPin is configured
#define WIFI_PORTAL_KEEP_STA        false           // Keep STA + /api/* up after AP timeout (e-ink wall display) — disabled: crashes
// AP-timeout: 15 min boot:n jälkeen portal_stop() sammuttaa portaalin.
//
// E-ink-seinätaulu tarvitsee /api/state:n JATKUVASTI (24/7). Oikea tuotanto-
// käytös olisi WIFI_PORTAL_KEEP_STA=true (vain AP sammuu, STA + web-server
// jäävät) — mutta se kaatuu (ks. yllä), joten KEEP_STA on disabloitu. Sen
// else-haara (g_webServer.end() + WiFi.mode(WIFI_OFF)) tappaa koko API:n, ja
// STA-reconnect nostaa vain pingin takaisin → laite näyttää "online" mutta
// /api/state ei vastaa, ja e-ink putoaa showroom-tilaan ~15 min boot:n jälkeen.
//
// Väliaikainen workaround: AP-timeout kokonaan pois → AP + STA + API pysyvät
// ikuisesti pystyssä, e-ink toimii 24/7. Haitta: AP-verkko näkyy jatkuvasti.
// TODO: korjaa KEEP_STA=true:n kaatuminen ja palauta AP-timeout (vain AP sammuu).
// Voit yliajaa secrets.h:ssa (#define WIFI_PORTAL_NO_TIMEOUT false) jos haluat
// 15 min timeoutin takaisin esim. virrankulutuksen vuoksi.
#ifndef WIFI_PORTAL_NO_TIMEOUT
  #define WIFI_PORTAL_NO_TIMEOUT    true
#endif
// Captive portal: AP-tilassa DNS-wildcard ohjaa kaikki hostit setup-sivulle,
// joten AP:hen liittyvä puhelin avaa portaalin automaattisesti (hotelli-WiFi).
// Vain AP-interface; ei vaikuta STA:han. Kytke pois jos aiheuttaa ongelmia.
#ifndef ENABLE_CAPTIVE_PORTAL
  #define ENABLE_CAPTIVE_PORTAL     true
#endif
#define WIFI_PORTAL_SESSION_MS      (30UL * 60UL * 1000UL)

// ═══════════════════════════════════════════════════════════════════
// RELAY
// ═══════════════════════════════════════════════════════════════════

#define RELAY_ACTIVE_LOW            true    // SPD-05VDC-SL-C is active LOW

// Grow light polarity. On the XIAO V1/v2 PCB the light is driven by an
// N-channel MOSFET gate (D9) — active HIGH. RELAY_ACTIVE_LOW only applies to
// the SPD-05VDC relay module on the legacy ESP32 profile. Using the relay
// polarity on the MOSFET inverts the light: it turns ON at boot ("OFF" drives
// the gate HIGH) and every command acts backwards (found on hardware 8.7.2026).
#ifndef LIGHT_ACTIVE_LOW
  #if USE_XIAO_SX1262
    #define LIGHT_ACTIVE_LOW        false   // MOSFET gate, active HIGH
  #else
    #define LIGHT_ACTIVE_LOW        RELAY_ACTIVE_LOW
  #endif
#endif

// ═══════════════════════════════════════════════════════════════════
// I2C BUS
// ═══════════════════════════════════════════════════════════════════

// TwoWire::setTimeOut(). Without this, clock-stretching sensors (SCD41
// allows ~30 ms) can hold the bus indefinitely, turning a transient stall
// into a watchdog reboot. 100 ms is well above the SCD41 spec with margin.
#ifndef I2C_BUS_TIMEOUT_MS
  #define I2C_BUS_TIMEOUT_MS        100
#endif

// Total wall-clock budget for the diagnostic boot-time I2C scan (setup()).
// A healthy 0x01-0x7E scan finishes in well under 200 ms, but a device that
// holds SDA/SCL low (miswired/damaged) makes each probe wait ~1 s for the bus
// to go idle — 126 probes then blow the WATCHDOG_TIMEOUT_S budget and the boot
// reboot-loops (architecture.md § 8 Aukko A: no unbounded blocking I/O in
// setup). Abort the scan once this budget elapses so a stuck bus degrades the
// device to an advisory, not a boot loop. Must stay well under WATCHDOG_TIMEOUT_S.
#ifndef I2C_SCAN_BUDGET_MS
  #define I2C_SCAN_BUDGET_MS        2000
#endif

// ═══════════════════════════════════════════════════════════════════
// I2C SENSOR ADDRESSES
// ═══════════════════════════════════════════════════════════════════

#define I2C_ADDR_VL53L0X            0x29
#define I2C_ADDR_BME280             0x76    // SDO→GND=0x76, SDO→VCC=0x77
#define I2C_ADDR_ADS1115            0x48
#define I2C_ADDR_INA219             0x40
#define I2C_ADDR_SCD41              0x62    // Sensirion SCD4x CO2/T/RH (V2-rekisteri)
#define I2C_ADDR_MLX90614           0x5A    // Melexis IR leaf temperature (V2-rekisteri)
#define I2C_ADDR_AS7341             0x39    // ams 11-channel spectral sensor (V2-rekisteri)
#define I2C_ADDR_BME680             0x77    // Bosch VOC+T+RH+P (V2-rekisteri, opt-in)
#define I2C_ADDR_INA226             0x40    // sama kuin INA219 — vain toinen käytössä
#define I2C_ADDR_INA228             0x40    // TI 20-bit teho/virta — sama 0x40, vain yksi väylällä
#define I2C_ADDR_TCA9548A           0x70    // I2C-multiplexer (V2 6-port hub)
#define I2C_ADDR_MCP23017           0x20    // Microchip 16-bit I2C GPIO expander (A0/A1/A2=GND)

// MCP23017-pinnijako (MCP_PIN_*, MCP_GPIO) on määritelty ylempänä pinnijaon
// yhteydessä, koska PIN_BUTTON ja PIN_BTN_LED_GREEN johdetaan siitä.

// PORTA-välimuistin (INTFA-reunat + GPIOA-tasot) päivitysväli. Yksi I2C-luku per
// jakso; tacho-reunat OR-akkumuloidaan yli fan-poll-ikkunan (FAN_TACHO_POLL_MS).
//
// HUOM: kun nappi on laajentimella (PCB v2), tämä väli on napin todellinen
// näytteenottotaajuus — button_input lukee välimuistia, ei väylää. 100 ms olisi
// karkeampi kuin BUTTON_DEBOUNCE_MS (30 ms) ja hukkaisi nopeat näpäytykset, joten
// väli on 20 ms: nappi tuntuu välittömältä ja debounce toimii kuten natiivi-GPIO:lla.
// Kustannus on 2 rekisterilukua / 20 ms — mitätön I2C-kuorma.
#ifndef MCP23017_REFRESH_MS
  #define MCP23017_REFRESH_MS       20U
#endif

// ═══════════════════════════════════════════════════════════════════
// FAN — tuulettimen PWM + tacho-kuntoseuranta (PCB v2)
// ═══════════════════════════════════════════════════════════════════
// Portaaton nopeus 25 kHz LEDC-PWM:llä (kuulumaton). LEDC-kanava 5 (pumppu = 4).
#ifndef FAN_PWM_CHANNEL
  #define FAN_PWM_CHANNEL           5
#endif
#ifndef FAN_PWM_FREQ_HZ
  #define FAN_PWM_FREQ_HZ           25000   // 25 kHz — standardi 4-johtotuulettimelle, kuulumaton 3-johdolla
#endif
#ifndef FAN_PWM_RESOLUTION_BITS
  #define FAN_PWM_RESOLUTION_BITS   8
#endif
// Kuntoseuranta: koska PWM:llä asetetaan haluttu nopeus, on oleellista todentaa
// että tuuletin oikeasti pyörii. Tacho luetaan MCP23017:n interrupt-on-change-
// latchista (INTFA) hitaalla pollingilla → karkea pyörii/ei-pyöri. Stall on
// ADVISORY (ei turvakriittinen), ei lukitse laitetta (DEVICE_FAULT_FAN, § 8 Aukko B).
//
// ENABLE_FAN_TACHO: kytkee koko tacho-luennan (fan_update lukee MCP GPA0:n vain
// kun tämä on true). Oletus FALSE — PCB v2:n tuulettimen tacho-lähtö on avokollektori
// joka vetää 12 V:iin (mikro-ohjain ei kestä), joten sitä ei ole kytketty MCP:hen.
// Ilman kytkentää luenta antaisi väärän "Tuuletin ei pyöri" -advisoryn joka syklissä.
// FALSE → open-loop: PWM ajaa tuuletinta, kuntoseuranta pois, ei väärää hälytystä.
// Kytke true vasta kun tacho on kytketty 3.3 V-tasoisena (pull-up 3.3 V:iin, ei 12 V).
#ifndef ENABLE_FAN_TACHO
  #define ENABLE_FAN_TACHO          false
#endif
#ifndef FAN_TACHO_POLL_MS
  #define FAN_TACHO_POLL_MS         500U    // tacho-latchin luku-/arviointiväli
#endif
#ifndef FAN_SPINUP_GRACE_MS
  #define FAN_SPINUP_GRACE_MS       3000U   // anna tuulettimen käynnistyä ennen stall-arviota
#endif
#ifndef FAN_STALL_MISS_POLLS
  #define FAN_STALL_MISS_POLLS      4       // perättäistä reunatonta pollia (komennettu ON) → advisory
#endif

// ═══════════════════════════════════════════════════════════════════
// MLX90614 LEAF-IR AIM-LOSS DETECTION (WP-F0-3 D1 firmware safety)
// ═══════════════════════════════════════════════════════════════════
// Thresholds for detecting when the MLX90614 IR sensor is no longer
// aimed at the leaf canopy. When any condition triggers, VPD falls back
// to T_air (V1 behaviour) and an advisory is raised. See vpd_calc.h.

// Max plausible delta |T_leaf - T_air|. Transpiring leaves are 0-10°C
// cooler than air; >15°C almost always means misaim or sensor fault.
#ifndef LEAF_IR_MAX_DELTA_FROM_AIR_C
  #define LEAF_IR_MAX_DELTA_FROM_AIR_C  15.0f
#endif

// Allow leaf to be this many °C below the dew point before flagging as
// "below dewpoint" (small negative = tight margin). Condensing leaves
// genuinely occur but the reading is not useful for VPD.
#ifndef LEAF_IR_MIN_ABOVE_DEWPOINT_C
  #define LEAF_IR_MIN_ABOVE_DEWPOINT_C  (-1.0f)
#endif

// If |T_leaf - T_water| < this threshold AND water temp is known, sensor
// is probably aimed at the reservoir instead of the canopy.
#ifndef LEAF_IR_WATER_PROXIMITY_C
  #define LEAF_IR_WATER_PROXIMITY_C     2.0f
#endif

// ═══════════════════════════════════════════════════════════════════
// CAPABILITY-BASED SENSOR CONFIG (V2 migration, parallel to ENABLE_*)
// ═══════════════════════════════════════════════════════════════════
//
// Two-layer model that separates "what we want to measure" (CAP_*)
// from "what hardware is physically present" (HW_*).
//
// Why: changing a sensor (BME280 → SCD41) should be a single-line
// config change, not a 4-file refactor. See docs §12 (suunnitteluselostus
// 3-6 muistion liite) for full rationale.
//
// This block is PARALLEL to the legacy ENABLE_* flags during migration.
// Migration path:
//   Phase A (now): add CAP_*/HW_* alongside ENABLE_* (no behavior change)
//   Phase B: capability_resolver + driver_registry headers (rinnan)
//   Phase C: sensor_manager.h käyttää driver_registry:ä
//   Phase D-H: ENABLE_* deprecated → poistettu
//
// Resolver picks the best available provider per capability — see
// sensor_capability_resolver.h for priority order.

// ── A. Capabilities — mitä halutaan mitata ──
#ifndef CAP_AIR_TEMP_HUMIDITY
  #define CAP_AIR_TEMP_HUMIDITY     true   // VPD:n pohja, ilmaympäristö
#endif
#ifndef CAP_AIR_CO2
  #define CAP_AIR_CO2               false  // PE-stack (vaatii HW_SCD41), V2:ssa true
#endif
#ifndef CAP_AIR_PRESSURE
  #define CAP_AIR_PRESSURE          false  // Ilmanpaine (vain BME280/BME680 antavat)
#endif
#ifndef CAP_LEAF_TEMP
  #define CAP_LEAF_TEMP             false  // V2: tarkka VPD (vaatii HW_MLX90614)
#endif
#ifndef CAP_LIGHT_PAR
  #define CAP_LIGHT_PAR             false  // PPFD jatkuva (vaatii HW_AS7341 tai HW_TSL2591)
#endif
#ifndef CAP_LIGHT_SPECTRUM
  #define CAP_LIGHT_SPECTRUM        false  // Spektrikanavat (vaatii HW_AS7341)
#endif
#ifndef CAP_AIR_VOC
  #define CAP_AIR_VOC               false  // Haihtuvat orgaaniset (vaatii HW_BME680)
#endif
#ifndef CAP_PLANT_HEIGHT
  #define CAP_PLANT_HEIGHT          true   // Kasvin korkeus (ToF)
#endif
#ifndef CAP_WATER_TEMP
  #define CAP_WATER_TEMP            true   // Veden lämpötila
#endif
#ifndef CAP_WATER_LEVEL
  #define CAP_WATER_LEVEL           true   // Min-pinta float switch
#endif
#ifndef CAP_WATER_OVERFLOW
  #define CAP_WATER_OVERFLOW        false  // Ylivuoto float switch
#endif
#ifndef CAP_WATER_TDS
  #define CAP_WATER_TDS             false  // TDS in-line (vaatii HW_ADS1115 + probe)
#endif
#ifndef CAP_WATER_EC
  #define CAP_WATER_EC              false  // EC in-line (V2: DFRobot SEN0451)
#endif
#ifndef CAP_WATER_PH
  #define CAP_WATER_PH              false  // pH in-line (V2: DFRobot SEN0161-V2)
#endif
// EC/pH are declared but have no implementation anywhere: no driver, no
// SensorReading field, no resolver row (found by the V3 sensor survey, PR #268).
// The flags stay as a reservation for a future hydroponic build, but enabling
// one must fail loudly at compile time instead of silently reading nothing.
// Decision 12.8.2026 (KYSYMYKSET Q7): gate now, delete if EC/pH never lands.
#if CAP_WATER_EC
  #error "CAP_WATER_EC=true: no driver, no SensorReading field, no resolver row. Implement the sensor before enabling the flag (docs/ohjeet/uuden-sensorin-lisays.md)."
#endif
#if CAP_WATER_PH
  #error "CAP_WATER_PH=true: no driver, no SensorReading field, no resolver row. Implement the sensor before enabling the flag (docs/ohjeet/uuden-sensorin-lisays.md)."
#endif

#ifndef CAP_SUBSTRATE_MOISTURE
  #define CAP_SUBSTRATE_MOISTURE    false  // V2: DFRobot SEN0193 capacitive
#endif
#ifndef CAP_POWER_MONITORING
  #define CAP_POWER_MONITORING      false  // 12V/lampun/pumpun virta
#endif

// ── B. Hardware presence — mitkä anturit ovat laitteessa ──
#ifndef HW_BME280
  #define HW_BME280                 true   // V1: päällä, V2: false (korvataan SCD41:llä)
#endif
#ifndef HW_SCD41
  #define HW_SCD41                  false  // V2 base (ostettu, ei vielä firmwaressa)
#endif
#ifndef HW_BME680
  #define HW_BME680                 false  // V2+ opt-in (VOC)
#endif
#ifndef HW_MLX90614
  #define HW_MLX90614               false  // V2 add-on (IR-lehtilämpö)
#endif
#ifndef HW_AS7341
  #define HW_AS7341                 false  // Ostettu, ei vielä firmwaressa
#endif
#ifndef HW_TSL2591
  #define HW_TSL2591                false  // Halpa fallback PAR-anturi
#endif
#ifndef HW_VL53L0X
  #define HW_VL53L0X                true   // V1
#endif
#ifndef HW_DS18B20
  #define HW_DS18B20                true   // V1
#endif
#ifndef HW_FLOAT_SWITCH_MIN
  #define HW_FLOAT_SWITCH_MIN       true   // V1
#endif
#ifndef HW_FLOAT_SWITCH_OVERFLOW
  #define HW_FLOAT_SWITCH_OVERFLOW  false  // V2-varaus
#endif
#ifndef HW_ADS1115
  #define HW_ADS1115                false  // V2 (kapasitiivinen kosteus + TDS)
#endif
#ifndef HW_INA219
  #define HW_INA219                 false  // V1 PCB:llä INA226, ei INA219
#endif
#ifndef HW_INA226
  #define HW_INA226                 false  // V1 PCB:llä mutta firmware ei vielä käytä
#endif
#ifndef HW_INA228
  #define HW_INA228                 false  // TI 20-bit teho/virtamonitori (portable ina228.h)
#endif
#ifndef HW_TCA9548A
  #define HW_TCA9548A               false  // V2 6-port I2C hub
#endif
// ═══════════════════════════════════════════════════════════════════
// INA228 POWER MONITOR (CAP_POWER_MONITORING)
// ═══════════════════════════════════════════════════════════════════
// Portable core: ina228.h. Adapter: sensor_driver_ina228.h.
// Enable: HW_INA228=true + CAP_POWER_MONITORING=true.
//
// PlantMeister mittaa koko laitteen 12 V -syöttövirtaa (shunt D1:n jälkeen,
// pcb_v2_layout.md § 1.6). Pienvirtaprojektissa vaihda shunt suuremmaksi ja
// aseta ADC_RANGE=1 — mitoitusrajat: docs/arkisto/kehitys/ina228-virtamittaus.md.
#ifndef INA228_SHUNT_OHMS
  #define INA228_SHUNT_OHMS        0.015f  // Shunt (Ω). Adafruit/AliExpress-moduulit 0.015Ω/±10A
#endif
#ifndef INA228_MAX_CURRENT_A
  #define INA228_MAX_CURRENT_A     5.0f    // Odotettu maksimivirta → CURRENT_LSB = tämä / 2^19
#endif
#ifndef INA228_ADC_RANGE
  #define INA228_ADC_RANGE         0       // 0 = ±163.84 mV (isot virrat), 1 = ±40.96 mV (pienvirta)
#endif

// ═══════════════════════════════════════════════════════════════════
// BATTERY
// ═══════════════════════════════════════════════════════════════════

#define BATTERY_VOLTAGE_DIVIDER     2.0f    // If using voltage divider
#define BATTERY_FULL_MV             4200    // Fully charged 18650
#define BATTERY_EMPTY_MV            3000    // Minimum safe voltage
#define BATTERY_ADC_SAMPLES         16      // Oversample for accuracy

// ═══════════════════════════════════════════════════════════════════
// LITTLEFS PATHS
// ═══════════════════════════════════════════════════════════════════

#define PATH_PLANTS_DB              "/plants.json"
#define PATH_DEVICE_CONFIG          "/config.json"
#define PATH_CALIBRATION            "/calibration.json"
#define PATH_GROW_CLOCK             "/growclock.json"

// Grow-clock persistence interval (grow_clock.h). Coarse on purpose: LittleFS
// wear stays negligible (~50 writes/day) and a power cut loses at most this
// much grow-day progress. No RTC/internet required — see docs/kehitys/
// valo-ja-keskeytymattomyys-suunnitelma.md (vika 3).
#ifndef GROW_CLOCK_SAVE_INTERVAL_MS
  #define GROW_CLOCK_SAVE_INTERVAL_MS (30UL * 60UL * 1000UL)
#endif

// ═══════════════════════════════════════════════════════════════════
// DEBUG
// ═══════════════════════════════════════════════════════════════════

#define DEBUG_SERIAL                Serial
#define DEBUG_BAUD                  115200
#define ENABLE_DEBUG                true

// ── Wireless log (UDP broadcast mirror-sink) ───────────────────────
// Kun XIAO on protoboardilla ja USB-johto ei kytkeydy, logit voidaan
// kuunnella PC:llä WiFi-broadcastin yli (scripts/wireless_log_listener.py).
// DEBUG_*F-makrot peilataan SEKÄ Serialiin ETTÄ UDP:hen kun WiFi yhdistetty.
#ifndef ENABLE_WIRELESS_LOG
  #define ENABLE_WIRELESS_LOG        true
#endif
#ifndef WIRELESS_LOG_PORT
  #define WIRELESS_LOG_PORT          14533
#endif
#ifndef WIRELESS_LOG_BUFFER_SIZE
  #define WIRELESS_LOG_BUFFER_SIZE   2048
#endif

// Debug verbosity levels — higher number = more output
#define DEBUG_LEVEL_NONE     0
#define DEBUG_LEVEL_ERROR    1
#define DEBUG_LEVEL_WARN     2
#define DEBUG_LEVEL_INFO     3
#define DEBUG_LEVEL_VERBOSE  4

// Production default: INFO drops periodic VERBOSE chatter, keeps boot + state logs.
#ifndef DEBUG_LEVEL
  #define DEBUG_LEVEL                DEBUG_LEVEL_INFO
#endif

// Forward declaration wireless_log.h -funktiosta. Vältetään include-sykli
// config.h ↔ wireless_log.h. Yksikkötesteissä (UNIT_TEST) WLOG_WRITE_FMT on
// no-op, jotta käännösyksiköt jotka EIVÄT includoi wireless_log.h:ta eivät
// jätä linkkeriin avoimia symboleita.
#if ENABLE_WIRELESS_LOG && !defined(UNIT_TEST)
  void wlog_write(const char* s);
  #define WLOG_WRITE_FMT(fmt, ...) do {                                  \
      char _wb[192];                                                     \
      snprintf(_wb, sizeof(_wb), fmt, ##__VA_ARGS__);                    \
      wlog_write(_wb);                                                   \
    } while (0)
#else
  #define WLOG_WRITE_FMT(fmt, ...)
#endif

#if ENABLE_DEBUG
  #define DEBUG_PRINTF(fmt, ...) do { DEBUG_SERIAL.printf(fmt, ##__VA_ARGS__); WLOG_WRITE_FMT(fmt, ##__VA_ARGS__); } while (0)

  // DEBUG_*F (formatoidut) peilataan SEKÄ Serialiin ETTÄ UDP:hen.
  // DEBUG_* (ei-formatoidut) jätetään vain Serialiin: msg voi olla int/float/
  // String, mitä wlog_write(const char*) ei tukisi ilman tyyppivariantteja.
  #if DEBUG_LEVEL >= DEBUG_LEVEL_ERROR
    #define DEBUG_ERROR(msg)        { DEBUG_SERIAL.print(F("[ERROR] ")); DEBUG_SERIAL.println(msg); }
    #define DEBUG_ERRORF(fmt, ...)  do { DEBUG_SERIAL.print(F("[ERROR] ")); DEBUG_SERIAL.printf(fmt, ##__VA_ARGS__); WLOG_WRITE_FMT("[ERROR] " fmt, ##__VA_ARGS__); } while (0)
  #else
    #define DEBUG_ERROR(msg)
    #define DEBUG_ERRORF(fmt, ...)
  #endif

  #if DEBUG_LEVEL >= DEBUG_LEVEL_WARN
    #define DEBUG_WARN(msg)         { DEBUG_SERIAL.print(F("[WARN]  ")); DEBUG_SERIAL.println(msg); }
    #define DEBUG_WARNF(fmt, ...)   do { DEBUG_SERIAL.print(F("[WARN]  ")); DEBUG_SERIAL.printf(fmt, ##__VA_ARGS__); WLOG_WRITE_FMT("[WARN]  " fmt, ##__VA_ARGS__); } while (0)
  #else
    #define DEBUG_WARN(msg)
    #define DEBUG_WARNF(fmt, ...)
  #endif

  #if DEBUG_LEVEL >= DEBUG_LEVEL_INFO
    #define DEBUG_INFO(msg)         { DEBUG_SERIAL.print(F("[INFO]  ")); DEBUG_SERIAL.println(msg); }
    #define DEBUG_INFOF(fmt, ...)   do { DEBUG_SERIAL.print(F("[INFO]  ")); DEBUG_SERIAL.printf(fmt, ##__VA_ARGS__); WLOG_WRITE_FMT("[INFO]  " fmt, ##__VA_ARGS__); } while (0)
  #else
    #define DEBUG_INFO(msg)
    #define DEBUG_INFOF(fmt, ...)
  #endif

  #if DEBUG_LEVEL >= DEBUG_LEVEL_VERBOSE
    #define DEBUG_VERBOSE(msg)      { DEBUG_SERIAL.print(F("[VERB]  ")); DEBUG_SERIAL.println(msg); }
    #define DEBUG_VERBOSEF(fmt, ...) do { DEBUG_SERIAL.print(F("[VERB]  ")); DEBUG_SERIAL.printf(fmt, ##__VA_ARGS__); WLOG_WRITE_FMT("[VERB]  " fmt, ##__VA_ARGS__); } while (0)
  #else
    #define DEBUG_VERBOSE(msg)
    #define DEBUG_VERBOSEF(fmt, ...)
  #endif
#else
  #define DEBUG_ERROR(msg)
  #define DEBUG_ERRORF(fmt, ...)
  #define DEBUG_WARN(msg)
  #define DEBUG_WARNF(fmt, ...)
  #define DEBUG_INFO(msg)
  #define DEBUG_INFOF(fmt, ...)
  #define DEBUG_VERBOSE(msg)
  #define DEBUG_VERBOSEF(fmt, ...)
  #define DEBUG_PRINTF(fmt, ...)
#endif

// ═══════════════════════════════════════════════════════════════════
// WATCHDOG
// ═══════════════════════════════════════════════════════════════════

#define ENABLE_WATCHDOG             true
// 60s: PlantMeister ei ole nopea reaaliaikalaite. E-ink-paivitys ja muut
// hitaat operaatiot mahtuvat reilusti taman alle, ja jos loop tosiaan
// jumittaa, palautuminen on silti minuutin sisalla.
#define WATCHDOG_TIMEOUT_S          60      // Reboot if loop() blocks >60s

// ═══════════════════════════════════════════════════════════════════
// PIN SANITY CHECKS
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_PUMP && ENABLE_LIGHT_RELAY && (PIN_PUMP == PIN_RELAY_LIGHT)
  #define PUMP_LIGHT_SHARED_PIN 1
#else
  #define PUMP_LIGHT_SHARED_PIN 0
#endif

// Optional dedicated overflow switch pin (set -1 if not connected)
#ifndef PIN_FLOAT_SWITCH_OVERFLOW
  #define PIN_FLOAT_SWITCH_OVERFLOW -1
#endif

// ═══════════════════════════════════════════════════════════════════
// CONFIG SANITY CHECKS
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_EBB_FLOW && !ENABLE_PUMP
  #error "ENABLE_EBB_FLOW requires ENABLE_PUMP=true"
#endif

// Fan takes native D6, forcing FLOAT_MIN and the tacho onto the MCP23017 expander.
// Without the expander there is no relocated FLOAT_MIN input and no tacho health.
#if ENABLE_FAN && !ENABLE_MCP23017
  #error "ENABLE_FAN requires ENABLE_MCP23017=true (tacho + relocated FLOAT_MIN live on the expander)"
#endif

#if ENABLE_EBB_FLOW && ENABLE_RESERVOIR_LEVEL_SAFETY && !ENABLE_FLOAT_SWITCH
  #error "ENABLE_RESERVOIR_LEVEL_SAFETY for Ebb&Flow requires ENABLE_FLOAT_SWITCH=true"
#endif

#if (ENABLE_TDS_SENSOR || ENABLE_WATER_TEMP || ENABLE_FLOAT_SWITCH || ENABLE_HEIGHT_SENSOR || ENABLE_ENV_SENSOR || ENABLE_BATTERY_MONITOR) && !ENABLE_SENSORS
  #error "Sensor sub-features require ENABLE_SENSORS=true"
#endif

#if MOTOR_DRIVER == MOTOR_DRIVER_L298N
  #ifndef PIN_MOTOR_IN1
    #error "MOTOR_DRIVER_L298N requires PIN_MOTOR_IN1 define"
  #endif
  #ifndef PIN_MOTOR_IN2
    #error "MOTOR_DRIVER_L298N requires PIN_MOTOR_IN2 define"
  #endif
  #ifndef PIN_MOTOR_IN3
    #error "MOTOR_DRIVER_L298N requires PIN_MOTOR_IN3 define"
  #endif
  #ifndef PIN_MOTOR_IN4
    #error "MOTOR_DRIVER_L298N requires PIN_MOTOR_IN4 define"
  #endif
#endif

#if USE_XIAO_SX1262 && ENABLE_LORA
  #if (PIN_LORA_NSS < 0) || (PIN_LORA_DIO1 < 0) || (PIN_LORA_RESET < 0) || (PIN_LORA_BUSY < 0)
    #error "USE_XIAO_SX1262 + ENABLE_LORA requires SX1262 SPI pin mapping"
  #endif
#endif

#if USE_XIAO_SX1262 && USE_XIAO_PLUS_PINS
  #if (PIN_RELAY_LIGHT == PIN_PUMP)
    #error "XIAO Plus profile requires dedicated light relay pin (must differ from PIN_PUMP)"
  #endif
  #if ENABLE_BATTERY_MONITOR && (PIN_BATTERY_ADC >= 0) && (PIN_BATTERY_ADC == PIN_LORA_SCLK)
    #error "XIAO Plus profile requires dedicated battery ADC pin (must differ from PIN_LORA_SCLK)"
  #endif
  #if ENABLE_OVERFLOW_SWITCH && (PIN_FLOAT_SWITCH_OVERFLOW < 0)
    #error "XIAO Plus profile with ENABLE_OVERFLOW_SWITCH requires PIN_FLOAT_SWITCH_OVERFLOW"
  #endif
  #if (PIN_LED_RED >= 0) && (PIN_LED_RED == PIN_RELAY_AIR_PUMP)
    #error "PIN_LED_RED and PIN_RELAY_AIR_PUMP must not share the same GPIO"
  #endif
#endif

#endif // CONFIG_H
