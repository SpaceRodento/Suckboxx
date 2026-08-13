// 04_i2c_scan — etsii I2C-väylältä laitteita 3 s välein, lokittaa löydökset.

#include <Arduino.h>
#include <Wire.h>
#include "v1_pins.h"
#include "smoke_wifi.h"

static unsigned long scanPrev = 0;

static void runScan() {
    int found = 0;
    smoke_log("[i2c_scan] scanning...");
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            smoke_logf("[i2c_scan] found 0x%02X\n", addr);
            found++;
        }
    }
    smoke_logf("[i2c_scan] done. %d device(s) found.\n", found);
}

void setup() {
    smoke_log_begin("i2c_scan");
    smoke_logf("[i2c_scan] SDA=GPIO %d, SCL=GPIO %d\n",
               PM_PIN_I2C_SDA, PM_PIN_I2C_SCL);
    Wire.begin(PM_PIN_I2C_SDA, PM_PIN_I2C_SCL);
    runScan();
    scanPrev = millis();
}

void loop() {
    smoke_log_loop();
    unsigned long now = millis();
    if ((now - scanPrev) >= 3000) {
        scanPrev = now;
        runScan();
    }
}
