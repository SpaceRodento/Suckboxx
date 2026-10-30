#!/usr/bin/env python3
"""Upottaa web/index.html firmwareen: firmware/src/web/web_page.h (WP-5).

    python3 tools/embed_web.py           # kirjoittaa web_page.h:n
    python3 tools/embed_web.py --check   # exit 1 jos web_page.h puuttuu tai on vanhentunut

Koko UI on yksi C++-raakamerkkijono flashissa (PROGMEM). HTML upotetaan
tavulleen sellaisenaan (vain rivinvaihdot normalisoidaan \\n:ksi), jotta
--check on pelkkä vertailu ja laitteen sivu on täsmälleen sama kuin mockin.
Pelkkä standardikirjasto.
"""

import argparse
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "web", "index.html")
DST = os.path.join(ROOT, "firmware", "src", "web", "web_page.h")
DELIM = "SBXHTML"
# Raakamerkkijono päättyy ensimmäiseen ')SBXHTML"' -jonoon. Hylätään jo pelkkä
# tunniste, ettei raja-tapausta (esim. kommentissa) tarvitse miettiä.
FORBIDDEN = DELIM


def render(html):
    size = len(html.encode("utf-8"))
    digest = hashlib.sha256(html.encode("utf-8")).hexdigest()[:16]
    return (
        "#pragma once\n"
        "// GENEROITU TIEDOSTO - ÄLÄ MUOKKAA KÄSIN.\n"
        "// Lähde: web/index.html, generaattori: tools/embed_web.py\n"
        "//   python3 tools/embed_web.py          # päivitä\n"
        "//   python3 tools/embed_web.py --check  # onko ajan tasalla\n"
        "//\n"
        "// Koko web-UI yhtenä merkkijonona flashissa (docs/firmware.md §5). Laitteen\n"
        "// AP:lla ei ole internetiä, joten sivu ei lataa mitään ulkoista.\n"
        "// Lähde %d tavua, sha256 %s.\n"
        "\n"
        "#ifdef ARDUINO\n"
        "#include <Arduino.h>\n"
        "#else\n"
        "// PC-käännös (native-testit): PROGMEM ei merkitse mitään.\n"
        "#ifndef PROGMEM\n"
        "#define PROGMEM\n"
        "#endif\n"
        "#endif\n"
        "\n"
        "const char WEB_PAGE_HTML[] PROGMEM = R\"%s(%s)%s\";\n"
        % (size, digest, DELIM, html, DELIM)
    )


def load():
    with open(SRC, "r", encoding="utf-8", newline="") as f:
        html = f.read()
    html = html.replace("\r\n", "\n").replace("\r", "\n")
    if FORBIDDEN in html:
        line = html[: html.index(FORBIDDEN)].count("\n") + 1
        sys.exit("virhe: web/index.html rivi %d sisältää raakamerkkijonon erottimen '%s'"
                 % (line, FORBIDDEN))
    if "\0" in html:
        sys.exit("virhe: web/index.html sisältää NUL-merkin")
    return html


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="tarkista vain, älä kirjoita")
    args = ap.parse_args()
    html = load()
    out = render(html)
    rel = os.path.relpath(DST, ROOT)
    if args.check:
        try:
            with open(DST, "r", encoding="utf-8", newline="") as f:
                cur = f.read()
        except OSError:
            print("%s puuttuu - aja: python3 tools/embed_web.py" % rel)
            return 1
        if cur != out:
            print("%s on vanhentunut - aja: python3 tools/embed_web.py" % rel)
            return 1
        print("%s ajan tasalla" % rel)
        return 0
    os.makedirs(os.path.dirname(DST), exist_ok=True)
    with open(DST, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    print("kirjoitettu %s (HTML %d tavua)" % (rel, len(html.encode("utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
