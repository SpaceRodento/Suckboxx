#include <Arduino.h>

constexpr uint8_t PIN_SCK = 25;
constexpr uint8_t PIN_DOUT = 34;

int32_t hx710_read() {
  while (digitalRead(PIN_DOUT)) {}

  int32_t value = 0;
  for (uint8_t i = 0; i < 24; i++) {
    digitalWrite(PIN_SCK, HIGH);
    delayMicroseconds(1);
    value = (value << 1) | digitalRead(PIN_DOUT);
    digitalWrite(PIN_SCK, LOW);
    delayMicroseconds(1);
  }

  digitalWrite(PIN_SCK, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_SCK, LOW);
  delayMicroseconds(1);

  if (value & 0x800000) {
    value |= 0xFF000000;
  }
  return value;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_DOUT, INPUT);
  digitalWrite(PIN_SCK, LOW);
}

void loop() {
  int32_t raw = hx710_read();
  Serial.printf("raw=%ld\n", raw);
  delay(100);
}
