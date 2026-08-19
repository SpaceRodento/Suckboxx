#pragma once
#include <Arduino.h>

// HX710B - 24-bit sigma-delta ADC, käytössä halvoissa "0-40 kPa" digitaalisissa
// painesensorimoduuleissa. Sama 2-johtiminen bittisarjaprotokolla kuin HX711:ssä:
// DOUT menee matalaksi kun uusi näyte on valmis (10 tai 40 SPS, kiinteä), ja
// SCK-pulssit kellottavat 24 databittiä ulos MSB ensin. 25. pulssi asettaa
// seuraavan mittauksen vahvistuksen/kanavan - HX710B on yksikanavainen mutta
// pulssi vaaditaan siitä huolimatta protokollan mukaan.
class Hx710b {
 public:
  // HUOM: GPIO34-39 ovat input-only eikä niissä ole sisäistä pull-uppia. Jos
  // moduuli on irti, linja kelluu ja lukee satunnaisesti LOW - readRaw() nappaa
  // sen kiinni palauttamalla false, mutta oikea korjaus on ulkoinen 10k pull-up
  // DOUT:sta 3V3:een, jos kanava jätetään tarkoituksella kytkemättä.
  void begin(uint8_t sckPin, uint8_t doutPin) {
    sck_ = sckPin;
    dout_ = doutPin;
    pinMode(sck_, OUTPUT);
    digitalWrite(sck_, LOW);
    pinMode(dout_, INPUT);
  }

  // DOUT matala = uusi näyte odottaa lukemista. Ei-blokkaava - kutsuja päättää
  // milloin kysyy.
  bool isReady() const { return digitalRead(dout_) == LOW; }

  // Lukee yhden 24-bittisen näytteen ja etumerkkilaajentaa sen 32-bittiseksi.
  // Kutsu vasta kun isReady() == true. Palauttaa false jos DOUT jäi matalaksi
  // 25. pulssin jälkeen: toimiva HX710B nostaa linjan heti ylös seuraavan
  // muunnoksen ajaksi, joten matalaksi jäänyt linja tarkoittaa ettei siellä ole
  // sirua vastaamassa (moduuli irti, kelluva pinni, huono kytkentä). Silloin
  // näyte on roskaa eikä sitä saa käyttää.
  bool readRaw(int32_t& out) {
    uint32_t value = 0;

    noInterrupts();
    for (uint8_t i = 0; i < 24; i++) {
      digitalWrite(sck_, HIGH);
      delayMicroseconds(1);
      value = (value << 1) | digitalRead(dout_);
      digitalWrite(sck_, LOW);
      delayMicroseconds(1);
    }
    // 25. pulssi (gain/channel-valinta seuraavalle näytteelle)
    digitalWrite(sck_, HIGH);
    delayMicroseconds(1);
    digitalWrite(sck_, LOW);
    delayMicroseconds(1);
    interrupts();

    if (digitalRead(dout_) == LOW) {
      return false;  // linja ei vapautunut -> ei toimivaa sirua linjalla
    }

    if (value & 0x800000UL) {
      value |= 0xFF000000UL;  // etumerkkilaajennus 24 -> 32 bittiä
    }
    out = static_cast<int32_t>(value);
    return true;
  }

 private:
  uint8_t sck_ = 0;
  uint8_t dout_ = 0;
};
