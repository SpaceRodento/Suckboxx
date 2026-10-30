#!/usr/bin/env python3
"""Upottaa web/index.html firmwareen: firmware/src/web/web_page.h (WP-5).

    python3 tools/embed_web.py            # kirjoittaa web_page.h:n
    python3 tools/embed_web.py --check    # exit 1 jos web_page.h puuttuu tai on vanhentunut
    python3 tools/embed_web.py --print    # tulostaa minimoidun HTML:n (tarkastelu)
    python3 tools/embed_web.py --selftest # minimoijan omat testit

web/index.html pidetään luettavana (sisennykset, kommentit). Laitteelle
upotetaan minimoitu versio, jotta sivu pysyy pienenä (tavoite < 40 kt):
koko UI on yksi C++-raakamerkkijono flashissa ja kulkee jokaisella
sivunlatauksella AP:n yli.

Minimointi on tarkoituksella varovainen, koska JS:lle ei ole parseria
(pelkkä standardikirjasto):
  - HTML: kommentit pois, rivien alku- ja loppuvälit pois, tyhjät rivit pois.
    Rivinvaihto säilyy, joten tekstisolmujen välilyönnit eivät katoa.
  - CSS: kommentit pois ja välilyönnit tiivistetään merkkijonojen
    ulkopuolella (pieni tilakone, ei regexiä merkkijonojen yli).
  - JS: kokonaiset //-kommenttirivit ja sisennykset pois, ja rivin sisäiset
    välit pois vain riveiltä, joilla ei ole '/'-merkkiä (ei regexiä eikä
    kommenttia; merkkijonot ohitetaan tilakoneella). Rivit
    yhdistetään vain kun se ei voi muuttaa merkitystä (ASI): edellinen rivi
    päättyy ; { , ( tai seuraava alkaa } ) ] - eikä kummallakaan ole //:tä
    tai /*:ta, joten loppukommentti ei voi niellä seuraavaa riviä.
    Template-merkkijonot (`) ja rivin jatko (\\ rivin lopussa) hylätään,
    koska niissä sisennys kuuluu merkkijonoon.
Mock (tools/mock_server.py --minified) tarjoilee saman minimoidun sivun,
jolloin Playwright-testi (tools/ui_screenshots.js) ajetaan sitä vastaan.
"""

import argparse
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "web", "index.html")
DST = os.path.join(ROOT, "firmware", "src", "web", "web_page.h")
DELIM = "SBXHTML"
# Raakamerkkijono päättyy ensimmäiseen ')SBXHTML"' -jonoon. Hylätään jo pelkkä
# tunniste, ettei raja-tapausta (esim. kommentissa) tarvitse miettiä.
FORBIDDEN = DELIM


class EmbedError(Exception):
    pass


# ---------------------------------------------------------------------------
# Minimointi
# ---------------------------------------------------------------------------
def _min_html(text):
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    lines = [ln.strip() for ln in text.split("\n")]
    return "\n".join(ln for ln in lines if ln)


def _min_css(text):
    # Tilakone: kommentit pois, merkkijonot sellaisinaan, muu tiivistetään.
    out = []
    buf = []
    i, n = 0, len(text)

    def flush():
        s = re.sub(r"\s+", " ", "".join(buf))
        # Välit pois merkkien { } ; , ympäriltä ja kaksoispisteen jälkeen.
        # Välilyöntiä ENNEN kaksoispistettä ei poisteta (".a :hover" on eri
        # valitsin kuin ".a:hover"), eikä operaattoreiden + - ympäriltä
        # (calc() vaatii ne).
        s = re.sub(r"\s*([{};,])\s*", r"\1", s)
        s = re.sub(r":\s+", ":", s)
        s = s.replace(";}", "}")
        out.append(s)
        del buf[:]

    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                raise EmbedError("CSS: sulkematon /* -kommentti")
            buf.append(" ")
            i = j + 2
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == "\\":
                    j += 1
                elif text[j] == "\n":
                    raise EmbedError("CSS: merkkijono jatkuu yli rivin")
                j += 1
            if j >= n:
                raise EmbedError("CSS: sulkematon merkkijono")
            flush()
            out.append(text[i:j + 1])
            i = j + 1
        else:
            buf.append(c)
            i += 1
    flush()
    return "".join(out).strip()


def _is_word(ch):
    return ch.isalnum() or ch in "_$" or ord(ch) > 127


def _keep_space(a, b):
    """Tarvitaanko välilyönti merkkien a ja b välissä (JS, merkkijonojen ulkopuolella)."""
    if not a or not b:
        return False
    if _is_word(a) and _is_word(b):
        return True            # "var x", "typeof v", "k in o"
    if a in "+-" and b in "+-":
        return True            # "a + ++b" != "a+++b"
    if b == ".":
        return True            # "1 .toFixed"
    return (a, b) in (("<", "!"), ("-", ">"))   # HTML-kommenttien <!-- ja -->


def _compact_js_line(ln):
    """Poistaa rivin sisäiset turhat välilyönnit. Rivi, jolla on '/', jätetään
    ennalleen: kauttaviiva voi aloittaa regexin tai kommentin, eikä niitä voi
    erottaa jakolaskusta ilman parseria."""
    if "/" in ln:
        return ln
    out = []
    i, n = 0, len(ln)
    while i < n:
        c = ln[i]
        if c in "'\"":
            j = i + 1
            while j < n and ln[j] != c:
                j += 2 if ln[j] == "\\" else 1
            if j >= n:
                raise EmbedError("JS: sulkematon merkkijono: %r" % ln)
            out.append(ln[i:j + 1])
            i = j + 1
        elif c in " \t":
            j = i
            while j < n and ln[j] in " \t":
                j += 1
            prev = out[-1][-1] if out else ""
            if _keep_space(prev, ln[j] if j < n else ""):
                out.append(" ")
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _min_js(text):
    if "`" in text:
        raise EmbedError("JS: template-merkkijono (`) ei ole sallittu (ES2017-raja ja minimointi)")
    out = []
    for raw in text.split("\n"):
        if raw.rstrip().endswith("\\"):
            raise EmbedError("JS: rivin jatko (\\) ei ole sallittu: %r" % raw.strip())
        ln = raw.strip()
        if not ln or ln.startswith("//"):
            continue
        ln = _compact_js_line(ln)
        if out:
            prev = out[-1]
            safe = "//" not in prev and "/*" not in prev and "//" not in ln and "/*" not in ln
            if safe and (prev[-1] in ";{,(" or ln[0] in "})]"):
                out[-1] = prev + ln
                continue
        out.append(ln)
    return "\n".join(out)


_BLOCK = re.compile(r"(<style[^>]*>)(.*?)(</style>)|(<script[^>]*>)(.*?)(</script>)", re.S | re.I)


def minify(html):
    if re.search(r"<(pre|textarea)\b", html, re.I):
        raise EmbedError("<pre>/<textarea>: sisennyksen poisto muuttaisi sisältöä")
    out = []
    pos = 0
    for m in _BLOCK.finditer(html):
        out.append(_min_html(html[pos:m.start()]))
        if m.group(1):
            out.append("\n" + m.group(1) + _min_css(m.group(2)) + m.group(3) + "\n")
        else:
            out.append("\n" + m.group(4) + "\n" + _min_js(m.group(5)) + "\n" + m.group(6) + "\n")
        pos = m.end()
    out.append(_min_html(html[pos:]))
    text = "".join(out)
    text = re.sub(r"\n{2,}", "\n", text).strip("\n")
    return text + "\n"


# ---------------------------------------------------------------------------
# Header
# ---------------------------------------------------------------------------
def render(page, src_size):
    data = page.encode("utf-8")
    digest = hashlib.sha256(data).hexdigest()[:16]
    return (
        "#pragma once\n"
        "// GENEROITU TIEDOSTO - ÄLÄ MUOKKAA KÄSIN.\n"
        "// Lähde: web/index.html, generaattori: tools/embed_web.py\n"
        "//   python3 tools/embed_web.py          # päivitä\n"
        "//   python3 tools/embed_web.py --check  # onko ajan tasalla\n"
        "//\n"
        "// Koko web-UI yhtenä minimoituna merkkijonona flashissa (docs/firmware.md §5).\n"
        "// Laitteen AP:lla ei ole internetiä, joten sivu ei lataa mitään ulkoista.\n"
        "// Lähde %d tavua, upotettu %d tavua, sha256 %s.\n"
        "//\n"
        "// inline constexpr (C++17): yksi kopio flashissa, vaikka header olisi\n"
        "// mukana useammassa käännösyksikössä (pelkkä const olisi sisäisesti\n"
        "// linkitetty -> 40 kt kopio jokaiseen .cpp:hen).\n"
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
        "inline constexpr char WEB_PAGE_HTML[] PROGMEM = R\"%s(%s)%s\";\n"
        % (src_size, len(data), digest, DELIM, page, DELIM)
    )


def load(path=SRC):
    """Lukee lähteen ja normalisoi rivinvaihdot. EmbedError jos ei kelpaa."""
    with open(path, "r", encoding="utf-8", newline="") as f:
        html = f.read()
    html = html.replace("\r\n", "\n").replace("\r", "\n")
    if FORBIDDEN in html:
        line = html[: html.index(FORBIDDEN)].count("\n") + 1
        raise EmbedError("web/index.html rivi %d sisältää raakamerkkijonon erottimen '%s'"
                         % (line, FORBIDDEN))
    if "\0" in html:
        raise EmbedError("web/index.html sisältää NUL-merkin")
    return html


def page_html(path=SRC):
    """Laitteelle upotettava (minimoitu) sivu. Mock käyttää tätä --minified-tilassa."""
    return minify(load(path))


# ---------------------------------------------------------------------------
# Itsetesti: minimoijan reunatapaukset
# ---------------------------------------------------------------------------
def selftest():
    fails = []

    def eq(name, got, want):
        if got != want:
            fails.append("%s:\n  saatiin %r\n  odotettu %r" % (name, got, want))

    def raises(name, fn):
        try:
            fn()
        except EmbedError:
            return
        fails.append("%s: EmbedError puuttuu" % name)

    eq("css kommentti+välit", _min_css(" .a , .b {\n  color : red ;\n  /* x */ margin: 0 auto; }\n"),
       ".a,.b{color :red;margin:0 auto}")
    eq("css merkkijono koskematon", _min_css('.a::after { content: "a : b ;  /* c */"; }'),
       '.a::after{content:"a : b ;  /* c */"}')
    eq("css calc säilyy", _min_css(".a { padding: calc(20px + env(x)); }"), ".a{padding:calc(20px + env(x))}")
    eq("css jälkeläisvalitsin säilyy", _min_css(".a :focus { x: 1 }"), ".a :focus{x:1}")
    raises("css sulkematon kommentti", lambda: _min_css(".a { /* x }"))

    eq("js kommenttirivi ja sisennys", _min_js("  // kommentti\n  var a = 1;\n  if (a) {\n    f();\n  }\n"),
       "var a=1;if(a){f();}")
    eq("js loppukommentti ei niele riviä", _min_js("x(); // a,\ny();\n"), "x(); // a,\ny();")
    eq("js ei yhdistetä ilman erotinta", _min_js("a = b\n(c)\n"), "a=b\n(c)")
    eq("js ASI return", _min_js("return\nx;\n"), "return\nx;")
    eq("js url merkkijonossa", _min_js("var u = 'http://x';\nf();\n"), "var u = 'http://x';\nf();")
    eq("js merkkijonon välit säilyvät", _min_js("h += '<b class=\"a b\"> ' + x;"), "h+='<b class=\"a b\"> '+x;")
    eq("js lainausmerkki toisen sisällä", _min_js("var a = \"it's \" + 'say \\'x\\' ' ;"),
       "var a=\"it's \"+'say \\'x\\' ';")
    eq("js sanat erillään", _min_js("if (typeof v === 'x' && k in o) return -1;"),
       "if(typeof v==='x'&&k in o)return-1;")
    eq("js ++ ei muodostu", _min_js("a = b + ++c - -d;"), "a=b+ ++c- -d;")
    eq("js <!-- ei muodostu", _min_js("a = b < !c;"), "a=b< !c;")
    eq("js jakolasku ennallaan", _min_js("var y = v / sc * 50;"), "var y = v / sc * 50;")
    raises("js sulkematon merkkijono", lambda: _min_js("var a = 'x;"))
    raises("js template", lambda: _min_js("var a = `x\n  y`;"))
    raises("js rivin jatko", lambda: _min_js("var a = 'x\\\n  y';"))

    doc = ("<!DOCTYPE html>\n<!-- kommentti\n  monirivinen -->\n<p>\n  a\n  <b>b</b>\n</p>\n"
           "<style>\n.a { x: 1; }\n</style>\n<script>\n  // c\n  f();\n</script>\n")
    eq("dokumentti", minify(doc),
       "<!DOCTYPE html>\n<p>\na\n<b>b</b>\n</p>\n<style>.a{x:1}</style>\n<script>\nf();\n</script>\n")
    raises("pre hylätään", lambda: minify("<pre>\n  x\n</pre>"))

    if fails:
        print("\n".join(fails))
        return 1
    print("selftest OK")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--check", action="store_true", help="tarkista vain, älä kirjoita")
    g.add_argument("--print", action="store_true", help="tulosta minimoitu HTML")
    g.add_argument("--selftest", action="store_true", help="minimoijan testit")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    try:
        html = load()
        page = minify(html)
    except (OSError, EmbedError) as e:
        print("virhe: %s" % e, file=sys.stderr)
        return 1
    if args.print:
        sys.stdout.write(page)
        return 0
    out = render(page, len(html.encode("utf-8")))
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
    print("kirjoitettu %s (lähde %d tavua, upotettu %d tavua)"
          % (rel, len(html.encode("utf-8")), len(page.encode("utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
