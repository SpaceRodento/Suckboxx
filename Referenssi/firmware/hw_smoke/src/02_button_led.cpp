// 02_button_led — nappi (GPIO 9, INPUT_PULLUP) ohjaa LEDiä reaaliajassa.
//
// V1 PCB:llä ei boardilla LEDiä. Loki kertoo silti napin tilan ja saadaan
// validoitua INPUT_PULLUP + debounce + reagointiaika. Jos kytket testiksi
// ulkoisen LED + 220R esim. expansion-headeriin, voit määritellä
// TEST_LED_PIN build_flagsissa.

#include <Arduino.h>
#include "v1_pins.h"
#include "smoke_wifi.h"

#ifndef TEST_LED_PIN
  #define TEST_LED_PIN PM_PIN_LED_STATUS  // -1 = skip
#endif

static int lastStableState = HIGH;

void setup() {
    smoke_log_begin("button_led");
    smoke_logf("[button_led] BTN pin=%d (INPUT_PULLUP) LED pin=%d\n",
               PM_PIN_BTN_USER, TEST_LED_PIN);
    pinMode(PM_PIN_BTN_USER, INPUT_PULLUP);
    if (TEST_LED_PIN >= 0) {
        pinMode(TEST_LED_PIN, OUTPUT);
        digitalWrite(TEST_LED_PIN, LOW);
    }
}

void loop() {
    smoke_log_loop();
    int rawState = digitalRead(PM_PIN_BTN_USER);

    if (rawState != lastStableState) {
        delay(20);  // debounce
        int confirmed = digitalRead(PM_PIN_BTN_USER);
        if (confirmed == rawState) {
            lastStableState = confirmed;
            if (TEST_LED_PIN >= 0) {
                digitalWrite(TEST_LED_PIN, confirmed == LOW ? HIGH : LOW);
            }
            smoke_logf("[button_led] BTN: %s\n", confirmed == LOW ? "LOW (pressed)" : "HIGH (released)");
        }
    }
}
