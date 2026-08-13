// PlantMeister V1 — XIAO ESP32-S3 Plus pin allocation.
// Single source of truth, mirrors docs/laitteisto/kytkennat.md § 1.
// Update kytkennat.md FIRST, then this file. Both hw_smoke and the main
// firmware include from here.
//
// Lukittu 2026-05-21 — vain castellated D0–D10. Status-LED siirretty
// V2:n MCP23017-laajennukseen, ei boardilla V1:ssä.

#ifndef PM_V1_PINS_H
#define PM_V1_PINS_H

// Motor — MKS SERVO42C-MT, off-board (D0/D1/D2 vastaa MKS:n DIR-STEP-EN-johdotusta).
// 42C-MT tukee CR_vFOC (STEP/DIR pulse, default) ja CR_UART (closed-loop) -tiloja.
// Smoke-sketsit käyttävät CR_vFOC-tilaa — sama pin-allokaatio toimii.
#define PM_PIN_MOTOR_DIR    1   // D0
#define PM_PIN_MOTOR_STEP   2   // D1
#define PM_PIN_MOTOR_EN     3   // D2 — active LOW, strapping-safe

// OneWire — DS18B20 water temperature
#define PM_PIN_ONEWIRE      4   // D3

// I2C bus (Grove J6 → external I2C Hub)
#define PM_PIN_I2C_SDA      5   // D4
#define PM_PIN_I2C_SCL      6   // D5

// Float switches (vesi-min / ylivuoto), INPUT_PULLUP
#define PM_PIN_FLOAT_MIN   43   // D6
#define PM_PIN_FLOAT_OVF   44   // D7

// MOSFET gates — low-side switches, 3.3 V logic, active HIGH
#define PM_PIN_MOSFET_PUMP  7   // D8, PWM-capable
#define PM_PIN_MOSFET_LIGHT 8   // D9, PWM-capable

// User button — tactile switch GPIO ↔ GND, INPUT_PULLUP (active LOW)
#define PM_PIN_BTN_USER     9   // D10

// Status-LED — EI boardilla V1:ssä (-1 = ei käytössä, V2: MCP23017 I2C-expander)
#define PM_PIN_LED_STATUS  -1

#endif // PM_V1_PINS_H
