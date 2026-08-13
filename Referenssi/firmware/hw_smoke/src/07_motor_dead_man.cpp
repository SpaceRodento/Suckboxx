// 07_motor_dead_man — nappi pohjassa = stepperi pyörii, vapaa = pysähtyy.
//
// MKS SERVO42C-MT STEP/DIR/EN-tilassa (CR_vFOC, tehdasoletus):
//   - EN on active LOW (LOW = driver enabled, HIGH = disabled)
//   - STEP-pulssi: 500 us HIGH + 500 us LOW = 1 kHz
//   - DIR pidetään LOW = yksi suunta
//
// 42C-MT default 16 microsteps + NEMA17 (200 step/rev) -> 1 kHz pulssi = ~18.75 rpm.
// Jos OLEDilla on muu microstep, rpm muuttuu vastaavasti — testissä riittää että akseli liikkuu.
//
// Pidä napin pohjassa = liikettä tulee jatkuvasti. Vapauta = EN HIGH = driver pois,
// stepperi vapaa eikä pidä virtaa. Tämä on turvallisin: et voi unohtaa moottoria päälle.
//
// Vaatii moottorin OLED-tilan: Mode -> CR_vFOC (tehdasoletus, tarkista ennen testiä).
// HUOM: testaa **vapaalla akselilla**, älä mekaaniseen järjestelmään kytkettynä.

#include <Arduino.h>
#include "v1_pins.h"
#include "smoke_wifi.h"

static int lastStableState = HIGH;

void setup() {
    smoke_log_begin("motor_dead_man");
    smoke_logf("[motor] BTN=GPIO %d, DIR=GPIO %d, STEP=GPIO %d, EN=GPIO %d (active LOW)\n",
               PM_PIN_BTN_USER, PM_PIN_MOTOR_DIR, PM_PIN_MOTOR_STEP, PM_PIN_MOTOR_EN);
    pinMode(PM_PIN_BTN_USER, INPUT_PULLUP);
    pinMode(PM_PIN_MOTOR_EN, OUTPUT);
    pinMode(PM_PIN_MOTOR_DIR, OUTPUT);
    pinMode(PM_PIN_MOTOR_STEP, OUTPUT);
    digitalWrite(PM_PIN_MOTOR_EN, HIGH);    // disabled
    digitalWrite(PM_PIN_MOTOR_DIR, LOW);
    digitalWrite(PM_PIN_MOTOR_STEP, LOW);
    smoke_log("[motor] Dead-man armed. Hold button = stepper runs (1 kHz STEP).");
}

void loop() {
    smoke_log_loop();
    int rawState = digitalRead(PM_PIN_BTN_USER);

    if (rawState != lastStableState) {
        delay(20);
        int confirmed = digitalRead(PM_PIN_BTN_USER);
        if (confirmed == rawState) {
            lastStableState = confirmed;
            bool pressed = (confirmed == LOW);
            digitalWrite(PM_PIN_MOTOR_EN, pressed ? LOW : HIGH);
            if (!pressed) digitalWrite(PM_PIN_MOTOR_STEP, LOW);
            smoke_logf("[motor] BTN %s -> EN %s\n",
                       pressed ? "PRESSED" : "RELEASED",
                       pressed ? "LOW (enabled)" : "HIGH (disabled)");
        }
    }

    // STEP-pulssit napin pohjassa ollessa. Pidetään loop tiiviinä,
    // smoke_log_loop ajaa ~5 s välein beaconin joka ei ole aikakriittinen.
    if (lastStableState == LOW) {
        digitalWrite(PM_PIN_MOTOR_STEP, HIGH);
        delayMicroseconds(500);
        digitalWrite(PM_PIN_MOTOR_STEP, LOW);
        delayMicroseconds(500);
    }
}
