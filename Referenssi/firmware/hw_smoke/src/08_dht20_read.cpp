// 08_dht20_read — lukee Grove Temp&Humidity v2.1 (DHT20/AHT20-yhteensopiva,
// I2C 0x38), lokittaa lämpötilan ja kosteuden 3 s välein UDP-broadcastina.
//
// Protokolla (DHT20 datasheet):
//   init check : lue status (1 tavu). bit3 (0x08) = kalibroitu.
//                jos ei, lähetä 0xBE 0x08 0x00 + 10 ms.
//   mittaus    : kirjoita 0xAC 0x33 0x00, odota 80 ms.
//   luku       : lue 7 tavua: status + hum[19:0] + temp[19:0] + CRC8.
//   muunnos    : RH%  = hum_raw  * 100 / 2^20
//                T°C  = temp_raw * 200 / 2^20 - 50

#include <Arduino.h>
#include <Wire.h>
#include "v1_pins.h"
#include "smoke_wifi.h"

static const uint8_t DHT20_ADDR = 0x38;

static unsigned long readPrev = 0;
static bool dht20Ready = false;

// CRC8, polynomi 0x31, init 0xFF (DHT20 datasheet).
static uint8_t dht20Crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// Lukee status-tavun (0x71-komento). Palauttaa -1 jos anturi ei vastaa.
static int dht20Status() {
    Wire.beginTransmission(DHT20_ADDR);
    Wire.write(0x71);
    if (Wire.endTransmission() != 0) return -1;
    if (Wire.requestFrom((int)DHT20_ADDR, 1) != 1) return -1;
    return Wire.read();
}

static bool dht20Init() {
    delay(100);  // power-on
    int st = dht20Status();
    if (st < 0) {
        smoke_log("[dht20] ei vastausta 0x38 — tarkista kytkenta");
        return false;
    }
    if ((st & 0x08) == 0) {
        // ei kalibroitu → aja init-komento
        smoke_logf("[dht20] status 0x%02X, ajetaan init...\n", st);
        Wire.beginTransmission(DHT20_ADDR);
        Wire.write(0xBE); Wire.write(0x08); Wire.write(0x00);
        Wire.endTransmission();
        delay(10);
    }
    smoke_logf("[dht20] init OK (status 0x%02X)\n", st);
    return true;
}

static void dht20Measure() {
    // trigger
    Wire.beginTransmission(DHT20_ADDR);
    Wire.write(0xAC); Wire.write(0x33); Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        smoke_log("[dht20] mittauskomento epaonnistui (vayla?)");
        return;
    }
    delay(80);

    uint8_t d[7];
    if (Wire.requestFrom((int)DHT20_ADDR, 7) != 7) {
        smoke_log("[dht20] luku epaonnistui (< 7 tavua)");
        return;
    }
    for (uint8_t i = 0; i < 7; i++) d[i] = Wire.read();

    if (d[0] & 0x80) {
        smoke_logf("[dht20] anturi varattu (status 0x%02X), skip\n", d[0]);
        return;
    }

    uint32_t humRaw  = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t tempRaw = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];

    float rh = (float)humRaw  * 100.0f / 1048576.0f;
    float t  = (float)tempRaw * 200.0f / 1048576.0f - 50.0f;

    uint8_t crc = dht20Crc8(d, 6);
    const char* crcTag = (crc == d[6]) ? "CRC ok" : "CRC FAIL";

    smoke_logf("[dht20] T=%.2f C  RH=%.2f %%  (%s)\n", t, rh, crcTag);
}

void setup() {
    smoke_log_begin("dht20_read");
    smoke_logf("[dht20] SDA=GPIO %d, SCL=GPIO %d, addr=0x38\n",
               PM_PIN_I2C_SDA, PM_PIN_I2C_SCL);
    Wire.begin(PM_PIN_I2C_SDA, PM_PIN_I2C_SCL);
    dht20Ready = dht20Init();
    readPrev = millis();
}

void loop() {
    smoke_log_loop();
    if (!dht20Ready) return;  // ei anturia → älä spämmää väylää
    unsigned long now = millis();
    if ((now - readPrev) >= 3000) {
        readPrev = now;
        dht20Measure();
    }
}
