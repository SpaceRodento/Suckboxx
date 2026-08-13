# LoraMeister

> **LEGACY / ARKISTOITU (2026-06-08):** Vanha proto-alusta (ESP32 DevKit + RYLR890 AT-moduuli). PlantMeister käyttää nyt XIAO ESP32S3 Plus + Wio-SX1262 (RadioLib). Tätä ei builddata pää-firmwaren mukana (`build_src_filter ... -<lorameister/>`) eikä kehitetä aktiivisesti. **Säilytetään kopiointilähteenä** — `LoRaMeister.ino` ja `system_monitoring.h` sisältävät valmiita rakenteita (watchdog REL-3, performance monitor REL-7, RTC_DATA_ATTR REL-8). Ks. [docs/arkisto/kehitys/aliprojekti-oppeja-2026-05-15.md](../../../docs/arkisto/kehitys/aliprojekti-oppeja-2026-05-15.md).

General-purpose ESP32 LoRa data exchange platform using RYLR890/896 modules with AT command interface.

## Hardware

| Component | Pin | Notes |
|-----------|-----|-------|
| ESP32 DevKit V1 | - | Main MCU, 80 MHz |
| RYLR890/896 TX | GPIO25 | LoRa UART RX |
| RYLR890/896 RX | GPIO26 | LoRa UART TX |
| LCD 16x2 I2C | GPIO21 (SDA), GPIO22 (SCL) | Address 0x27 |
| MAX4466 mic OUT | GPIO34 | ADC input (example sensor) |

### Role Detection (GPIO jumpers)

| Role | Connection | Function |
|------|-----------|----------|
| SENDER | GPIO16 floating | Reads sensors, sends data via LoRa |
| RECEIVER | GPIO16 ↔ GPIO17 | Receives data, logs to USB serial |
| RELAY | GPIO19 ↔ GPIO20 | Mesh forwarder (up to 3 hops) |

Same firmware on all devices — role is detected at boot.

## Quick Start

1. Install [PlatformIO](https://platformio.org/) (VS Code extension)
2. Open the project folder
3. Connect ESP32 DevKit V1 via USB
4. Upload: `pio run -t upload`
5. Serial Monitor: `pio device monitor` (115200 baud)

### Quick Settings (config.h)

```cpp
#define QS_LCD_LAYOUT              8       // 1=Signal, 2=Range, 4=Debug, 8=Mic
#define QS_LORA_SEND_INTERVAL_MS   3500    // Send interval
#define QS_ACK_INTERVAL            30      // ACK every Nth packet
#define QS_DEBUG_MIC               false   // Mic debug output
```

## Architecture

```
[Sensor] --> [ESP32 SENDER] --LoRa 868MHz--> [ESP32 RELAY (opt)]
                             --LoRa 868MHz--> [ESP32 RECEIVER] --USB--> [PC/RPi]
```

### File Structure

```
config.h              # All feature flags and pin definitions
structs.h             # Shared data structures
firmware.ino          # Main program (setup, loop, role handlers)

platform_hal.h        # Board detection, pin mapping, LED control
debug_hal.h           # Debug output routing
display_hal.h         # Unified display API (LCD/Serial fallback)
lora_hal.h            # LoRa abstraction (RYLR890 AT commands)
lora_handler.h        # RYLR890 AT command driver

lcd_display.h         # LCD 16x2 layouts (8 layout modes)
i2c_manager.h         # I2C bus management

microphone.h          # MAX4466 microphone driver (example sensor)

system_monitoring.h   # Health monitoring, RSSI stats, packet tracking
power_management.h    # CPU scaling, GPIO power, sleep modes
non_blocking_state_v2.h  # Relay queue state machine
```

## Adding a New Sensor

1. **config.h** — Add `ENABLE_xxx` flag and sensor config defines
2. **Create `xxx_sensor.h`** — Driver with `xxx_init()`, `xxx_update()`, `xxx_getValue()` (see `microphone.h`)
3. **structs.h** — Add fields to `DeviceState` for the sensor data
4. **firmware.ino** — Add to `buildPayload()` and `parsePayload()`
5. **lcd_display.h** — Add a layout to display the data (optional)

### Example: MAX4466 Microphone (included)

- `microphone.h` — ADC peak-to-peak sampling, dB conversion
- Payload fields: `MIC:value,MICDB:value`
- LCD Layout 8: dB reading + bar graph
- Config: `ENABLE_MICROPHONE`, `MIC_PIN`, `MIC_SAMPLE_WINDOW_MS`

## Features

- **LoRa mesh networking** — Up to 3 hops, automatic deduplication
- **Bidirectional ACK** — Receiver sends RSSI/SNR back to sender
- **Auto address** — Generated from MAC address (no manual config)
- **CSV data logging** — `DATA_CSV,V3,timestamp,role,rssi,snr,...` via USB
- **8 LCD layouts** — Signal, range test, debug, microphone, mesh stats
- **Kill-switch** — GPIO13↔14 jumper, hold 3s to restart
- **Watchdog** — 10s hardware watchdog timer
- **GPIO power** — Power sensors from GPIO pins with safety limits
- **NVS storage** — Boot count and reset reason tracking

## LoRa Payload Format

**Sender → Receiver:**
```
HOP:0/3,SRC:184,DST:1,SEQ:42,LED:1,TOUCH:0,SPIN:2,COUNT:42,MIC:1500,MICDB:65
```

**Receiver → Sender (ACK):**
```
ACK:PING:1:OK:RSSI:-75,SNR:10
```

## License

MIT
