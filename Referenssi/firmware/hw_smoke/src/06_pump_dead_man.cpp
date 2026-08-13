// 06_pump_dead_man — nappi pohjassa = vesipumppu-MOSFET päällä, vapaa = pois.
//
// Sama kuin 05_light, mutta MOSFET on PM_PIN_MOSFET_PUMP. Vesi liikkuu vain
// napin pohjassa ollessa — turvallisin mahdollinen ensimmäinen pumpputesti.
// HUOM: aja laite astian päällä tai pyyhkeen päällä, vesi voi roiskua.

#include <Arduino.h>
#include "v1_pins.h"
#include "smoke_wifi.h"

static int lastStableState = HIGH;

void setup() {
    smoke_log_begin("pump_dead_man");
    smoke_logf("[pump] BTN=GPIO %d (INPUT_PULLUP), MOSFET_PUMP=GPIO %d (active HIGH)\n",
               PM_PIN_BTN_USER, PM_PIN_MOSFET_PUMP);
    pinMode(PM_PIN_BTN_USER, INPUT_PULLUP);
    pinMode(PM_PIN_MOSFET_PUMP, OUTPUT);
    digitalWrite(PM_PIN_MOSFET_PUMP, LOW);
    smoke_log("[pump] Dead-man armed. Hold button = pump RUNS. Vesi roiskua!");
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
            digitalWrite(PM_PIN_MOSFET_PUMP, pressed ? HIGH : LOW);
            smoke_logf("[pump] BTN %s -> MOSFET %s\n",
                       pressed ? "PRESSED" : "RELEASED",
                       pressed ? "ON" : "OFF");
        }
    }
}
