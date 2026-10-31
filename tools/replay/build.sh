#!/usr/bin/env bash
# Kääntää nauhoitusten toistotyökalun (WP-8, docs/firmware.md §6.1).
#
#   bash tools/replay/build.sh              # -> tools/replay/build/replay
#   SAN=0 bash tools/replay/build.sh        # ilman ASan/UBSania (esim. MinGW-g++)
#   CXX=clang++ bash tools/replay/build.sh
#
# Samat varoitusliput kuin tools/run_native_tests.sh:ssa (-Werror, -Wdouble-promotion)
# ja oletuksena sama ASan/UBSan: toistettava data tulee laitteelta tai
# puhelimesta, eli lukija saa rikkinäistäkin syötettä. Optimointi -O2, koska
# minuutin nauhoitus on 60 000 Processor::pushia.
#
# Windowsilla WSL:ssä, kuten native-testit (MinGW-g++:sta puuttuu ASan):
#   wsl --cd "$(pwd -W)" -- bash tools/replay/build.sh
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FW="$ROOT/firmware"
OUT="$ROOT/tools/replay/build"
CXX="${CXX:-g++}"
FLAGS=(-std=gnu++17 -O2 -g -Wall -Wextra -Wshadow -Wdouble-promotion -Werror
       -I"$FW/src" -I"$FW/test/support" -DSUCKBOXX_NATIVE_TEST)
if [ "${SAN:-1}" != "0" ]; then
  FLAGS+=(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
fi
mkdir -p "$OUT"
"$CXX" "${FLAGS[@]}" "$ROOT/tools/replay/replay.cpp" -o "$OUT/replay"
echo "käännetty: $OUT/replay"
