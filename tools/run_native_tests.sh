#!/usr/bin/env bash
# Ajaa firmware/test/test_*/ -testit PC:llä ilman PlatformIO:ta.
#
#   tools/run_native_tests.sh                 # kaikki testit
#   tools/run_native_tests.sh test_dsp ...    # vain nimetyt
#
# PlatformIO:lla sama ajetaan komennolla `pio test -e native` (firmware/-kansiossa).
# Tämä skripti käyttää tools/native_test/unity.h -korviketta ja samoja tiukkoja
# varoitusasetuksia + ASan/UBSan. Jokainen ajo kääntää omaan väliaikaiskansioon,
# joten rinnakkaiset ajot eivät häiritse toisiaan.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FW="$ROOT/firmware"
CXX="${CXX:-g++}"
CXXFLAGS=(-std=gnu++17 -O1 -g -Wall -Wextra -Wshadow -Wdouble-promotion -Werror
          -fsanitize=address,undefined -fno-sanitize-recover=undefined
          -I"$ROOT/tools/native_test" -I"$FW/src" -I"$FW/test/support" -DSUCKBOXX_NATIVE_TEST)

if [ $# -gt 0 ]; then
  TESTS=("$@")
else
  TESTS=()
  for d in "$FW"/test/test_*/; do TESTS+=("$(basename "$d")"); done
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
fail=0
for t in "${TESTS[@]}"; do
  dir="$FW/test/$t"
  if [ ! -d "$dir" ]; then echo "[$t] ei löydy: $dir"; fail=1; continue; fi
  echo "=== $t ==="
  if ! "$CXX" "${CXXFLAGS[@]}" "$dir"/*.cpp -o "$OUT/$t"; then
    echo "[$t] KÄÄNNÖS EPÄONNISTUI"; fail=1; continue
  fi
  if ! (cd "$dir" && "$OUT/$t"); then
    echo "[$t] TESTIT EPÄONNISTUIVAT"; fail=1
  fi
done

if [ $fail -ne 0 ]; then echo "TULOS: EPÄONNISTUI"; exit 1; fi
echo "TULOS: OK (${#TESTS[@]} testikokonaisuutta)"
