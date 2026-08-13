// 01_blink — perussmoke: bootaako XIAO, nouseeko WiFi, lähettääkö lokia.
//
// V1 PCB:llä ei status-LEDiä (PM_PIN_LED_STATUS = -1), joten LED-vilkku
// skipataan. Loki kertoo silti onko laite elossa. Jos kytket testiksi
// LED + 220R esim. headerin GPIO 9:ään, voit kovakoodata sen pinnin alle.

#include <Arduino.h>
#include "v1_pins.h"
#include "smoke_wifi.h"

#ifndef TEST_LED_PIN
  #define TEST_LED_PIN PM_PIN_LED_STATUS  // -1 = skip
#endif

static unsigned long ledPrev = 0;
static unsigned long serialPrev = 0;
static bool ledState = false;
static unsigned long tickCount = 0;

void setup() {
    smoke_log_begin("blink");
    if (TEST_LED_PIN >= 0) {
        pinMode(TEST_LED_PIN, OUTPUT);
        digitalWrite(TEST_LED_PIN, LOW);
        smoke_logf("[blink] LED pin=%d\n", TEST_LED_PIN);
    } else {
        smoke_log("[blink] LED pin=-1, vain loki-tikitys (V1: ei status-LEDia)");
    }
}

void loop() {
    smoke_log_loop();
    unsigned long now = millis();

    if (TEST_LED_PIN >= 0 && (now - ledPrev) >= 500) {
        ledPrev = now;
        ledState = !ledState;
        digitalWrite(TEST_LED_PIN, ledState ? HIGH : LOW);
    }

    if ((now - serialPrev) >= 1000) {
        serialPrev = now;
        tickCount++;
        smoke_logf("[blink] tick %lu\n", tickCount);
    }
}
