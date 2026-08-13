// 05_light_dead_man — nappi pohjassa = kasvilamppu-MOSFET päällä, vapaa = pois.
//
// Dead-man-switch -periaate: turvallisin mahdollinen testitila. Käyttäjä ei voi
// vahingossa jättää valoa päälle. Pinni PM_PIN_MOSFET_LIGHT ajetaan suoraan
// HIGH/LOW — MOSFET on active HIGH (gate HIGH = johtaa = kuorma päällä), eli
// kiertää pää-firmwaren RELAY_ACTIVE_LOW-makron joka olettaa relettä.

#include <Arduino.h>
#include "v1_pins.h"
#include "smoke_wifi.h"

static int lastStableState = HIGH;

void setup() {
    smoke_log_begin("light_dead_man");
    smoke_logf("[light] BTN=GPIO %d (INPUT_PULLUP), MOSFET_LIGHT=GPIO %d (active HIGH)\n",
               PM_PIN_BTN_USER, PM_PIN_MOSFET_LIGHT);
    pinMode(PM_PIN_BTN_USER, INPUT_PULLUP);
    pinMode(PM_PIN_MOSFET_LIGHT, OUTPUT);
    digitalWrite(PM_PIN_MOSFET_LIGHT, LOW);   // varmistus: pois bootissa
    smoke_log("[light] Dead-man armed. Hold button = light ON.");
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
            digitalWrite(PM_PIN_MOSFET_LIGHT, pressed ? HIGH : LOW);
            smoke_logf("[light] BTN %s -> MOSFET %s\n",
                       pressed ? "PRESSED" : "RELEASED",
                       pressed ? "ON" : "OFF");
        }
    }
}
