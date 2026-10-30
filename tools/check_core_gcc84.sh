#!/usr/bin/env bash
# Tarkistaa, että jokainen firmware/src/core/*.h kääntyy ESP32:n omalla
# kääntäjällä (xtensa GCC 8.4, Arduino-ESP32 2.0.17) samoilla varoituksilla
# kuin native-testit. Native-g++ on uudempi eikä huomaa GCC 8.4:n puutteita
# (docs/firmware.md §1 kohta 6). Jokainen header käännetään yksinään, joten
# tarkistus kertoo myös, sisältääkö header kaiken tarvitsemansa.
#
#   tools/check_core_gcc84.sh              # kaikki core-headerit
#   tools/check_core_gcc84.sh dsp.h ...    # vain nimetyt
#
# Kääntäjä tulee PlatformIOsta: `pio run -e esp32dev` asentaa sen kerran.
# Toisen polun voi antaa muuttujalla XTENSA_CXX.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE="$ROOT/firmware/src/core"
CXX="${XTENSA_CXX:-$HOME/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++}"
if ! "$CXX" --version >/dev/null 2>&1; then
  echo "xtensa-esp32-elf-g++ puuttuu ($CXX) - aja ensin: pio run -d firmware -e esp32dev"
  exit 2
fi
# Kääntäjä voi olla Windows-ohjelma (Git Bash), joka ei ymmärrä /d/...-polkuja.
SRC="$( (cd "$ROOT/firmware/src" && pwd -W) 2>/dev/null || echo "$ROOT/firmware/src")"
CXXFLAGS=(-x c++ -std=gnu++17 -fsyntax-only -Wall -Wextra -Wshadow -Wdouble-promotion -Werror
          -I"$SRC")

if [ $# -gt 0 ]; then
  HEADERS=("$@")
else
  HEADERS=()
  for f in "$CORE"/*.h; do HEADERS+=("$(basename "$f")"); done
fi

fail=0
for h in "${HEADERS[@]}"; do
  if [ ! -f "$CORE/$h" ]; then echo "[$h] ei löydy: $CORE/$h"; fail=1; continue; fi
  if printf '#include "core/%s"\n' "$h" | "$CXX" "${CXXFLAGS[@]}" -; then
    echo "[$h] OK"
  else
    echo "[$h] EPÄONNISTUI"; fail=1
  fi
done

if [ $fail -ne 0 ]; then echo "TULOS: EPÄONNISTUI"; exit 1; fi
echo "TULOS: OK (${#HEADERS[@]} headeria, $("$CXX" -dumpversion))"
