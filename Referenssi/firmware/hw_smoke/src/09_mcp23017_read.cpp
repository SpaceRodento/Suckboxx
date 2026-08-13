// 09_mcp23017_read — lukee MCP23017 GPIO-expanderin PORTA/PORTB-pinnit ja
// lokittaa tilan (muutoksella heti + 3 s heartbeat). Kytkintestiin: käännä
// kytkin -> näet bitin vaihtuvan livenä. Loki menee sekä USB-Serialiin (COM6)
// että UDP-broadcastiin (smoke_wifi.h).
//
// Osoite: auto-detect 0x20..0x27 (A0/A1/A2-jumpperit). PCB-design olettaa
// 0x20 (A0/A1/A2=GND); jos chip vastaa muualla, jumpperit eivät ole maassa.
//
// Kaikki 16 pinniä konfiguroidaan INPUT + pull-up, joten avoin kytkin lukee
// HIGH (1) ja GND:hen kytketty LOW (0). Kytkimen toisen navan on oltava GND
// jotta muutos näkyy — kahden pull-up-inputin väliin kytketty kytkin ei muuta
// kumpaakaan (molemmat pysyvät HIGH).

#include <Arduino.h>
#include <Wire.h>
#include "v1_pins.h"
#include "smoke_wifi.h"

// MCP23017-rekisterit (IOCON.BANK = 0, power-on default → sekventiaalinen A/B).
static const uint8_t REG_IODIRA = 0x00;
static const uint8_t REG_IODIRB = 0x01;
static const uint8_t REG_GPPUA  = 0x0C;
static const uint8_t REG_GPPUB  = 0x0D;
static const uint8_t REG_GPIOA  = 0x12;
static const uint8_t REG_GPIOB  = 0x13;

static uint8_t mcpAddr    = 0x00;
static bool    mcpReady   = false;
static uint8_t lastA      = 0xFF;
static uint8_t lastB      = 0xFF;
static unsigned long beatPrev = 0;

static bool mcpWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(mcpAddr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool mcpRead(uint8_t reg, uint8_t* out) {
    Wire.beginTransmission(mcpAddr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;   // repeated start
    if (Wire.requestFrom((int)mcpAddr, 1) != 1) return false;
    *out = (uint8_t)Wire.read();
    return true;
}

// Etsi expander väliltä 0x20..0x27. Ensimmäinen joka ACKkaa = mcpAddr.
static bool mcpDetect() {
    for (uint8_t a = 0x20; a <= 0x27; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            mcpAddr = a;
            return true;
        }
    }
    return false;
}

static void logState(const char* tag, uint8_t a, uint8_t b) {
    // Binääri MSB..LSB, GPA7..GPA0 ja GPB7..GPB0.
    smoke_logf("[mcp] %s PORTA=0x%02X GPA[7..0]=%d%d%d%d%d%d%d%d  "
               "PORTB=0x%02X GPB[7..0]=%d%d%d%d%d%d%d%d  "
               "(pin0=GPA0=%d pin1=GPA1=%d)\n",
               tag, a,
               (a>>7)&1,(a>>6)&1,(a>>5)&1,(a>>4)&1,(a>>3)&1,(a>>2)&1,(a>>1)&1,a&1,
               b,
               (b>>7)&1,(b>>6)&1,(b>>5)&1,(b>>4)&1,(b>>3)&1,(b>>2)&1,(b>>1)&1,b&1,
               a&1, (a>>1)&1);
}

void setup() {
    smoke_log_begin("mcp23017_read");
    smoke_logf("[mcp] SDA=GPIO %d, SCL=GPIO %d\n", PM_PIN_I2C_SDA, PM_PIN_I2C_SCL);
    Wire.begin(PM_PIN_I2C_SDA, PM_PIN_I2C_SCL);

    if (!mcpDetect()) {
        smoke_log("[mcp] EI vastausta 0x20..0x27 — expander ei valylla");
        return;
    }
    smoke_logf("[mcp] loytyi osoitteesta 0x%02X%s\n", mcpAddr,
               mcpAddr == 0x20 ? " (0x20 = A0/A1/A2 GND, design-oletus)"
                               : " (EI 0x20 — A0/A1/A2 eivat kaikki GND:ssa)");

    // Kaikki 16 pinnia inputiksi + pull-up.
    bool ok = true;
    ok &= mcpWrite(REG_IODIRA, 0xFF);
    ok &= mcpWrite(REG_IODIRB, 0xFF);
    ok &= mcpWrite(REG_GPPUA,  0xFF);
    ok &= mcpWrite(REG_GPPUB,  0xFF);
    if (!ok) {
        smoke_log("[mcp] konfigurointi epaonnistui (kirjoitus ei mennyt lapi)");
        return;
    }
    mcpReady = true;

    uint8_t a = 0xFF, b = 0xFF;
    mcpRead(REG_GPIOA, &a);
    mcpRead(REG_GPIOB, &b);
    lastA = a; lastB = b;
    logState("INIT ", a, b);
    smoke_log("[mcp] valmis — kaanna kytkin, muutos nakyy heti");
    beatPrev = millis();
}

void loop() {
    smoke_log_loop();
    if (!mcpReady) return;

    uint8_t a = 0xFF, b = 0xFF;
    if (!mcpRead(REG_GPIOA, &a) || !mcpRead(REG_GPIOB, &b)) {
        smoke_log("[mcp] luku epaonnistui — expander irti?");
        delay(500);
        return;
    }

    if (a != lastA || b != lastB) {
        logState("MUUTOS", a, b);
        lastA = a; lastB = b;
    }

    unsigned long now = millis();
    if (now - beatPrev >= 3000) {
        beatPrev = now;
        logState("tila  ", a, b);
    }
    delay(20);   // ~50 Hz poll, riittää käsikytkimelle
}
