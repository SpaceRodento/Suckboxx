#!/usr/bin/env python3
"""Suckboxx - imusyklin profiilianalyysi laitteen raakadatasta (/api/raw.csv).

Laite pitää viimeiset ~8 s raakadataa 1 kHz:llä (docs/firmware.md §4.5). Tämä
työkalu tunnistaa siitä jokaisen imusyklin, asettaa syklit päällekkäin
kampikulma-asteikolle ja kirjoittaa itsenäisen HTML-raportin:

  - aikasarja (vieritettävä, syklirajat merkitty)
  - jokaisen sylinterin syklit päällekkäin: keskiprofiili ja hajontakaista
  - sylinterien keskiprofiilit samassa kuvassa (muodon vertailu)
  - syklikohtainen poikkeama keskiprofiilista (venttiili, sytytyskatko ...)
  - kierrosluku sykleittäin ja kanavien erot ajan yli (synkronointi)

Ilman moottorin pulssia (penkkitesti, letkut T-haarassa) raportti näyttää
aikasarjan ja kanavien yhtäpitävyyden.

Kampikulma-asteikko: 720° = yksi sykli (2 kierrosta). Nollakohta on imupulssin
laskevan reunan puoliväli, EI yläkuolokohta - laitteella ei ole kampikulma-
anturia. Sylinterien keskinäinen ajoitus on silti mitattu.

Käyttö:
  python tools/cycle_profile.py suckboxx_raw.csv
  python tools/cycle_profile.py --url http://192.168.4.1/api/raw.csv --open
  python tools/cycle_profile.py data.csv --out raportti.html
  python tools/cycle_profile.py data.csv --lp 100      # letkun resonanssi pois profiileista

Vain Pythonin standardikirjasto.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import sys
import urllib.request
import webbrowser
from html import escape
from pathlib import Path

CH = 3
NB = 240               # vaihepisteitä per sykli (3° välein)
PRE = 0.15             # profiilin alku ennen laskevaa reunaa, syklin osuutena
RPM_MIN, RPM_MAX = 300.0, 6000.0
LP_HZ = 40.0           # syklintunnistuksen alipäästö (kuten laitteen DSP, dsp.h)
PERIOD_TOL = 0.30      # syklin jakso saa poiketa mediaanista tämän verran
NAMES = ["SYL 1", "SYL 2", "SYL 3"]


# ---------------------------------------------------------------------------
# Lukeminen
# ---------------------------------------------------------------------------

def parse_csv(text: str) -> dict:
    meta: dict[str, str] = {}
    seq: list[int] = []
    kpa: list[list[float]] = [[] for _ in range(CH)]
    mv: list[list[int]] = [[] for _ in range(CH)]
    header = None
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            body = line[1:].strip()
            if "=" in body:
                key, val = body.split("=", 1)
                meta[key.strip()] = val.strip()
            continue
        if header is None:
            header = line.split(",")
            continue
        parts = line.split(",")
        if len(parts) < 2 + 2 * CH:
            continue
        try:
            s = int(parts[0])
            m = [int(parts[2 + i]) for i in range(CH)]
            k = [float(parts[2 + CH + i]) for i in range(CH)]
        except ValueError:
            continue
        seq.append(s)
        for i in range(CH):
            mv[i].append(m[i])
            kpa[i].append(k[i])
    if not seq:
        raise SystemExit("CSV:ssä ei ole näyterivejä")
    return {"meta": meta, "seq": seq, "kpa": kpa, "mv": mv}


def fill_gaps(d: dict) -> list[int]:
    """Seq-aukot täytetään lineaarisesti, jotta aika-akseli on yhtenäinen.
    Palauttaa aukkojen alkuindeksit (raportissa merkitään)."""
    seq = d["seq"]
    gaps = []
    out_kpa = [[] for _ in range(CH)]
    out_mv = [[] for _ in range(CH)]
    for j in range(len(seq)):
        if j > 0:
            miss = seq[j] - seq[j - 1] - 1
            if miss > 0:
                gaps.append(len(out_kpa[0]))
                for k in range(1, miss + 1):
                    a = k / (miss + 1)
                    for c in range(CH):
                        out_kpa[c].append(d["kpa"][c][j - 1] * (1 - a) + d["kpa"][c][j] * a)
                        out_mv[c].append(round(d["mv"][c][j - 1] * (1 - a) + d["mv"][c][j] * a))
        for c in range(CH):
            out_kpa[c].append(d["kpa"][c][j])
            out_mv[c].append(d["mv"][c][j])
    d["kpa"], d["mv"] = out_kpa, out_mv
    return gaps


# ---------------------------------------------------------------------------
# Signaalinkäsittely
# ---------------------------------------------------------------------------

def lowpass_coeffs(fc: float, fs: float):
    # 2. kertaluvun Butterworth bilineaarimuunnoksella (sama kuin dsp.h:n Biquad).
    k = math.tan(math.pi * fc / fs)
    q = 1.0 / math.sqrt(2.0)
    norm = 1.0 / (1.0 + k / q + k * k)
    b0 = k * k * norm
    return b0, 2 * b0, b0, 2 * (k * k - 1) * norm, (1 - k / q + k * k) * norm


def lfilter(c, x: list[float]) -> list[float]:
    b0, b1, b2, a1, a2 = c
    x0 = x[0]
    s2 = (b2 - a2) * x0          # lepotila ensimmäiseen näytteeseen (ei alkuaskelta)
    s1 = (b1 - a1) * x0 + s2
    y = [0.0] * len(x)
    for i, v in enumerate(x):
        o = b0 * v + s1
        s1 = b1 * v - a1 * o + s2
        s2 = b2 * v - a2 * o
        y[i] = o
    return y


def filtfilt(c, x: list[float]) -> list[float]:
    # Eteen- ja taaksepäin: ei vaihesiirtoa, joten syklirajat osuvat oikeaan kohtaan.
    return lfilter(c, lfilter(c, x)[::-1])[::-1]


def estimate_period(y: list[float], fs: float) -> tuple[float, float] | None:
    """Syklin pituus näytteinä autokorrelaatiosta. Palauttaa (jakso, korrelaatio)
    tai None, jos signaali ei ole jaksollinen (moottori seis, penkkitesti)."""
    dec = 4                                   # 250 Hz riittää 40 Hz:n suodatetulle
    z = y[::dec]
    n = len(z)
    mu = sum(z) / n
    z = [v - mu for v in z]
    var = sum(v * v for v in z) / n
    if var < 0.3 ** 2:                       # alle ~0,3 kPa heilahtelua: ei pulssia
        return None
    lo = max(2, int(120 * fs / RPM_MAX / dec * 0.9))
    hi = min(n // 2, int(120 * fs / RPM_MIN / dec * 1.1))
    acf = {}
    for lag in range(lo, hi + 1):
        s = 0.0
        for i in range(n - lag):
            s += z[i] * z[i + lag]
        acf[lag] = s / (n - lag) / var
    peaks = [L for L in range(lo + 1, hi) if acf[L] >= acf[L - 1] and acf[L] >= acf[L + 1]]
    if not peaks:
        return None
    best = max(peaks, key=lambda L: acf[L])
    # Puolikas tai kolmannes jaksosta (puuttuva pulssi, ristivuoto) näkyy
    # alempana huippuna; valitaan lyhin, joka on lähes yhtä vahva.
    for d in (3, 2):
        target = best / d
        near = [L for L in peaks if abs(L - target) <= 0.1 * target]
        if near:
            cand = max(near, key=lambda L: acf[L])
            if acf[cand] >= 0.8 * acf[best]:
                best = cand
                break
    if acf[best] < 0.2:
        return None
    # Paraabelisovitus huipun ympärille
    a, b, c = acf.get(best - 1, acf[best]), acf[best], acf.get(best + 1, acf[best])
    den = a - 2 * b + c
    off = 0.5 * (a - c) / den if den != 0 else 0.0
    return (best + off) * dec, b


def argmin(y: list[float], a: int, b: int) -> int:
    m = a
    for i in range(a + 1, b):
        if y[i] < y[m]:
            m = i
    return m


def interp(x: list[float], t: float) -> float:
    i = int(math.floor(t))
    if i < 0:
        return x[0]
    if i >= len(x) - 1:
        return x[-1]
    f = t - i
    return x[i] * (1 - f) + x[i + 1] * f


def integrate(x: list[float], t0: float, t1: float) -> float:
    """Lineaarisesti interpoloidun signaalin integraali [t0, t1] (näytteinä)."""
    i0, i1 = int(math.ceil(t0)), int(math.floor(t1))
    if i1 < i0:
        return 0.5 * (interp(x, t0) + interp(x, t1)) * (t1 - t0)
    s = 0.5 * (interp(x, t0) + x[i0]) * (i0 - t0)
    for i in range(i0, i1):
        s += 0.5 * (x[i] + x[i + 1])
    s += 0.5 * (x[i1] + interp(x, t1)) * (t1 - i1)
    return s


PROM_FRAC = 0.55       # oma imupulssi: prominenssi vähintään tämä x paikallinen p90
PROM_MIN_KPA = 0.5     # ... ja vähintään tämä (kohina, moottori seis)
PROM_REF_S = 1.5       # paikallisen vertailun ikkuna ±s (seuraa kaasun muutoksia)


def find_pulses(y: list[float], fs: float) -> list[dict]:
    """Imupulssien pohjat ja laskevan reunan puolivälit (= syklirajat).

    Pohjat valitaan topografisen prominenssin mukaan: oma imupulssi on syvä
    kuoppa, tasauskanavien kautta vuotava naapurisylinterin pulssi matala.
    Jakson arvaaminen autokorrelaatiosta valitsi ristivuodolla kolmanneksen
    oikeasta jaksosta, kun kierrokset muuttuivat kesken nauhoituksen.
    Vertailutaso on paikallinen (±PROM_REF_S), jotta kutistuva pulssi
    kaasua lisättäessä ei putoa pois."""
    n = len(y)
    span = int(0.5 * fs)
    mins = [i for i in range(1, n - 1) if y[i] < y[i - 1] and y[i] <= y[i + 1]]
    prom = []
    for i in mins:
        lm, j = y[i], i - 1
        while j >= max(0, i - span) and y[j] >= y[i]:
            lm = max(lm, y[j])
            j -= 1
        rm, j = y[i], i + 1
        while j <= min(n - 1, i + span) and y[j] >= y[i]:
            rm = max(rm, y[j])
            j += 1
        prom.append(min(lm, rm) - y[i])
    ref_n = int(PROM_REF_S * fs)
    strong = []
    lo = 0
    for k, i in enumerate(mins):
        while mins[lo] < i - ref_n:
            lo += 1
        hi = k
        while hi + 1 < len(mins) and mins[hi + 1] <= i + ref_n:
            hi += 1
        ref = percentile(sorted(prom[lo:hi + 1]), 0.9)
        if prom[k] >= max(PROM_MIN_KPA, PROM_FRAC * ref):
            strong.append(i)
    # Kaksi pohjaa saman pulssin sisällä (resonanssin värähtely pohjalla): syvempi jää.
    if len(strong) >= 3:
        spacing = median([b - a for a, b in zip(strong, strong[1:])])
        merged = [strong[0]]
        for i in strong[1:]:
            if i - merged[-1] < 0.35 * spacing:
                if y[i] < y[merged[-1]]:
                    merged[-1] = i
            else:
                merged.append(i)
        strong = merged

    pulses = []
    prev = None
    for m in strong:
        top_from = prev if prev is not None else max(0, m - span)
        top_i = max(range(top_from, m + 1), key=lambda i: y[i]) if m > top_from else m
        depth = y[top_i] - y[m]
        level = y[m] + 0.5 * depth
        t_b = None
        for i in range(m - 1, top_i - 1, -1):
            if y[i] >= level > y[i + 1]:
                t_b = i + (y[i] - level) / (y[i] - y[i + 1])
                break
        pulses.append({"min_i": m, "depth": depth, "t": t_b})
        prev = m
    return pulses


# ---------------------------------------------------------------------------
# Analyysi
# ---------------------------------------------------------------------------

def median(v):
    s = sorted(v)
    n = len(s)
    if n == 0:
        return float("nan")
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def percentile(s_sorted, p):
    if not s_sorted:
        return float("nan")
    k = (len(s_sorted) - 1) * p
    i = int(k)
    f = k - i
    return s_sorted[i] if i + 1 >= len(s_sorted) else s_sorted[i] * (1 - f) + s_sorted[i + 1] * f


def analyze_channel(x: list[float], y: list[float], fs: float) -> dict:
    pulses = find_pulses(y, fs)
    depths = [p["depth"] for p in pulses if p["t"] is not None]
    dmed = median(depths) if depths else 0.0
    weak_lim = max(0.4, 0.25 * dmed)
    bounds = [p for p in pulses if p["t"] is not None]
    for p in bounds:
        p["weak"] = p["depth"] < weak_lim
    cycles = []
    for k in range(len(bounds) - 1):
        t0, t1 = bounds[k]["t"], bounds[k + 1]["t"]
        cycles.append({"t0": t0, "t1": t1, "P": t1 - t0,
                       "weak": bounds[k]["weak"] or bounds[k + 1]["weak"]})
    pmed = median([c["P"] for c in cycles]) if cycles else float("nan")
    for c in cycles:
        c["valid"] = not c["weak"] and abs(c["P"] / pmed - 1) <= PERIOD_TOL
        c["rpm"] = 120.0 * fs / c["P"]
        c["mean"] = integrate(x, c["t0"], c["t1"]) / c["P"]
        seg = x[int(math.ceil(c["t0"])):int(math.floor(c["t1"])) + 1] or [interp(x, c["t0"])]
        c["min"], c["max"] = min(seg), max(seg)
        # Profiili: ikkuna alkaa PRE ennen laskevaa reunaa, pituus = syklin oma jakso.
        start = c["t0"] - PRE * c["P"]
        if start < 0 or start + c["P"] > len(x) - 1:
            c["prof"] = None
        else:
            c["prof"] = [interp(x, start + c["P"] * j / NB) for j in range(NB)]

    good = [c for c in cycles if c["valid"] and c["prof"] is not None]
    prof = None
    if good:
        mean_p, lo_p, hi_p = [], [], []
        for j in range(NB):
            col = sorted(c["prof"][j] for c in good)
            mean_p.append(sum(col) / len(col))
            lo_p.append(percentile(col, 0.1))
            hi_p.append(percentile(col, 0.9))
        # Poikkeama muodosta: syklin oma keskitaso poistetaan, jotta kaasun
        # aiheuttama tasonsiirto ei näy vikana (taso näkyy kanavaeroissa).
        ref_dc = sum(mean_p) / NB
        for c in good:
            dc = sum(c["prof"]) / NB
            c["score"] = math.sqrt(
                sum((c["prof"][j] - dc - mean_p[j] + ref_dc) ** 2 for j in range(NB)) / NB)
        smed = median([c["score"] for c in good]) or 1e-9
        for c in good:
            c["z"] = c["score"] / smed
        prof = {"mean": mean_p, "lo": lo_p, "hi": hi_p}
    return {"cycles": cycles, "good": good, "prof": prof, "bounds": [b["t"] for b in bounds],
            "pmed": pmed}


def rolling_mean(x: list[float], w: int) -> list[float]:
    w = max(1, w)
    out = []
    s = 0.0
    for i, v in enumerate(x):
        s += v
        if i >= w:
            s -= x[i - w]
        out.append(s / min(i + 1, w))
    return out


def phase_offsets(res: list[dict], fs: float) -> list[float | None]:
    """Kanavan imupulssin ajoitus SYL 1:n jälkeen kampiasteina (720° = sykli)."""
    out: list[float | None] = [0.0]
    b0 = res[0]["bounds"] if res[0] else []
    for c in range(1, CH):
        if not res[c] or len(b0) < 2:
            out.append(None)
            continue
        vals = []
        for t in res[c]["bounds"]:
            prev = [b for b in b0 if b <= t]
            if not prev:
                continue
            tp = prev[-1]
            nxt = [b for b in b0 if b > tp]
            if not nxt:
                continue
            P = nxt[0] - tp
            vals.append((t - tp) / P * 720.0)
        out.append(median(vals) if vals else None)
    return out


# ---------------------------------------------------------------------------
# Raportti
# ---------------------------------------------------------------------------

def r2(v: float) -> float:
    return round(v, 2)


def fi(text: str) -> str:
    """Desimaalipilkku ja oikea miinus kuten laitteen käyttöliittymässä."""
    return text.replace(".", ",").replace("-", "−")


def build_report(d: dict, src_name: str, gaps: list[int], prof_lp: float | None = None) -> str:
    meta = d["meta"]
    fs = float(meta.get("fs_hz", "1000") or 1000)
    kpa = d["kpa"]
    n = len(kpa[0])
    coeff = lowpass_coeffs(LP_HZ, fs)
    filt = [filtfilt(coeff, kpa[c]) for c in range(CH)]

    # Autokorrelaatio vain porttina: onko kanavassa jaksollista pulssia lainkaan.
    # Syklit ja jakso tulevat pulssien tunnistuksesta (find_pulses).
    res: list[dict | None] = [None] * CH
    for c in range(CH):
        if estimate_period(filt[c], fs) is not None:
            # Profiilit raakadatasta, tai --lp:llä vaihesiirrottomasti suodatetusta
            # (letkun resonanssi pois muodosta; syklirajat aina 40 Hz:n suodatuksesta).
            src = filtfilt(lowpass_coeffs(prof_lp, fs), kpa[c]) if prof_lp else kpa[c]
            r = analyze_channel(src, filt[c], fs)
            if r["good"]:
                res[c] = r
    periods = [r["pmed"] for r in res if r]
    period = median(periods) if periods else None

    # Kanavaerot: liukuva keskiarvo yhden syklin (tai 0,5 s) yli, d = SYL 1 - SYL n
    # kuten laitteella (positiivinen = imee enemmän kuin SYL 1).
    w = int(round(period)) if period else int(0.5 * fs)
    roll = [rolling_mean(kpa[c], w) for c in range(CH)]
    step = max(1, int(fs / 100))
    diff_t = [i / fs for i in range(w, n, step)]
    diffs = [[r2(roll[0][i] - roll[c][i]) for i in range(w, n, step)] for c in range(1, CH)]
    diff_stats = []
    for c in range(1, CH):
        seg = [roll[0][i] - roll[c][i] for i in range(w, n)]
        mu = sum(seg) / len(seg) if seg else float("nan")
        sd = math.sqrt(sum((v - mu) ** 2 for v in seg) / len(seg)) if seg else float("nan")
        diff_stats.append((mu, sd))

    offsets = phase_offsets(res, fs) if period else [None] * CH

    # --- tiivistelmätaulukko ---
    def good_mean(r):
        return sum(x["mean"] for x in r["good"]) / len(r["good"]) if r and r["good"] else None

    ref_mean = good_mean(res[0])
    rows = []
    for c in range(CH):
        r = res[c]
        raw_mean = sum(kpa[c]) / n
        mvs = d["mv"][c]
        if r and r["good"]:
            g = r["good"]
            rpms = [x["rpm"] for x in g]
            rpm_mu = sum(rpms) / len(rpms)
            rpm_sd = math.sqrt(sum((v - rpm_mu) ** 2 for v in rpms) / max(1, len(rpms) - 1))
            mean_mu = good_mean(r)
            if c == 0 or ref_mean is None:
                d_txt = "–"
            else:
                d_txt = f"{ref_mean - mean_mu:+.2f}"
            rows.append({
                "name": NAMES[c], "cycles": len(g), "rejected": len(r["cycles"]) - len(g),
                "rpm": f"{rpm_mu:.0f} ± {rpm_sd:.0f}",
                "mean": f"{mean_mu:.2f}",
                "d": d_txt,
                "min": f"{sum(x['min'] for x in g) / len(g):.1f}",
                "p2p": f"{sum(x['max'] - x['min'] for x in g) / len(g):.1f}",
                "zmax": f"{max(x['z'] for x in g):.1f}",
                "phase": "0° (viite)" if c == 0 else (f"{offsets[c]:.0f}°" if offsets[c] is not None else "–"),
                "mv": f"{min(mvs)}…{max(mvs)}",
            })
        else:
            rows.append({"name": NAMES[c], "cycles": 0, "rejected": 0, "rpm": "–",
                         "mean": f"{raw_mean:.2f}", "d": "–", "min": f"{min(kpa[c]):.1f}",
                         "p2p": f"{max(kpa[c]) - min(kpa[c]):.1f}", "zmax": "–", "phase": "–",
                         "mv": f"{min(mvs)}…{max(mvs)}"})

    # --- data selaimelle ---
    xdeg = [r2((-PRE + j / NB) * 720.0) for j in range(NB)]
    chans = []
    for c in range(CH):
        r = res[c]
        ch = {"name": NAMES[c], "raw": [r2(v) for v in kpa[c]], "bounds": [], "prof": None,
              "cycles": []}
        if r:
            ch["bounds"] = [r2(b / fs * 1000.0) for b in r["bounds"]]
            for k, cy in enumerate(r["cycles"]):
                ch["cycles"].append({
                    "t": r2(cy["t0"] / fs), "rpm": round(cy["rpm"]), "mean": r2(cy["mean"]),
                    "valid": cy["valid"], "ok": not cy["weak"], "z": r2(cy.get("z", 0.0)) if cy["valid"] else None,
                    "prof": [r2(v) for v in cy["prof"]] if cy["prof"] and cy["valid"] else None,
                })
            if r["prof"]:
                ch["prof"] = {k: [r2(v) for v in vals] for k, vals in r["prof"].items()}
        chans.append(ch)

    data = {
        "fs": fs, "n": n, "xdeg": xdeg, "ch": chans, "gaps": [r2(g / fs) for g in gaps],
        "diff": {"t": [r2(t) for t in diff_t], "d": diffs, "w_ms": round(w / fs * 1000)},
        "period_ms": r2(period / fs * 1000) if period else None,
    }

    has_pulse = any(r and r["good"] for r in res)
    rpm_txt = f"{120 * fs / period:.0f} rpm" if period else "ei pulssia"
    gv, zv = meta.get("gain_valid", "?"), meta.get("zero_valid", "?")
    info = [
        ("Tiedosto", src_name), ("Lähde", meta.get("source", "?")),
        ("Näytteitä", fi(f"{n} ({n / fs:.2f} s, {fs:.0f} Hz)")),
        ("Aukot", f"{len(gaps)} kpl (täytetty lineaarisesti)" if gaps else "ei"),
        ("Profiilit", fi(f"suodatettu {prof_lp:g} Hz (vaihesiirroton)") if prof_lp else "raakadata"),
        ("Kalibrointi", f"nollaus {'kyllä' if zv == '1' else 'ei'}, vahvistus {'kyllä' if gv == '1' else 'ei'}"),
        ("Jakso", fi(f"{data['period_ms']} ms ≈ {rpm_txt}") if period else rpm_txt),
    ]
    if meta.get("aborted"):
        info.append(("Huom", "lataus keskeytyi, rengas muuttui kesken"))

    diff_rows = "".join(
        f"<tr><td>SYL 1 − {NAMES[c + 1]}</td><td>{fi(f'{mu:+.2f}')}</td><td>{fi(f'{sd:.2f}')}</td></tr>"
        for c, (mu, sd) in enumerate(diff_stats))

    top = []
    for c in range(CH):
        r = res[c]
        if not r:
            continue
        for k, cy in enumerate(r["cycles"]):
            if cy["valid"] and cy.get("z") is not None:
                top.append((cy["z"], c, k, cy["t0"] / fs))
    top.sort(reverse=True)
    top_rows = "".join(
        f'<tr class="pick" data-ch="{c}" data-k="{k}"><td>{NAMES[c]}</td><td>{fi(f"{t:.2f}")} s</td>'
        f"<td>{fi(f'{z:.1f}')}×</td></tr>" for z, c, k, t in top[:8])

    def table_row(i, r):
        rej = " (+%d hyl.)" % r["rejected"] if r["rejected"] else ""
        cells = [fi(r["rpm"]), fi(r["mean"]), fi(r["d"]), fi(r["min"]), fi(r["p2p"]), r["phase"],
                 fi(r["zmax"]), r["mv"]]
        return ('<tr><td><span class="sw c%d"></span>%s</td><td>%d%s</td>' % (i, escape(r["name"]), r["cycles"], rej)
                + "".join("<td>%s</td>" % escape(v) for v in cells) + "</tr>")

    table_rows = "".join(table_row(i, r) for i, r in enumerate(rows))

    info_html = "".join(f"<div><dt>{escape(k)}</dt><dd>{escape(v)}</dd></div>" for k, v in info)
    page = TEMPLATE
    page = page.replace("%%INFO%%", info_html)
    page = page.replace("%%TABLE%%", table_rows)
    page = page.replace("%%DIFFROWS%%", diff_rows)
    page = page.replace("%%TOPROWS%%", top_rows or '<tr><td colspan="3">–</td></tr>')
    page = page.replace("%%HASPULSE%%", "1" if has_pulse else "0")
    page = page.replace("%%GENERATED%%", dt.datetime.now().strftime("%d.%m.%Y %H.%M"))
    page = page.replace("%%DATA%%", json.dumps(data, separators=(",", ":")))
    return page


TEMPLATE = r"""<!doctype html>
<html lang="fi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Imusyklin profiili</title>
<style>
:root {
  --bg: #fff; --bg2: #f4f1fb; --line: #cfc6e4; --tx: #0b0514; --mu: #4a405f;
  --c0: #0072a3; --c1: #b0008a; --c2: #2c1d52; --hl: #d17a00; --grid: rgba(0,0,0,.10);
  --band0: rgba(0,114,163,.16); --band1: rgba(176,0,138,.14); --band2: rgba(44,29,82,.14);
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #160a2b; --bg2: #221043; --line: #46297e; --tx: #f8f3ff; --mu: #c9b8ea;
    --c0: #31e7ff; --c1: #ff5ce1; --c2: #f2ecff; --hl: #ffb21e; --grid: rgba(255,255,255,.10);
    --band0: rgba(49,231,255,.16); --band1: rgba(255,92,225,.14); --band2: rgba(242,236,255,.12);
  }
}
:root[data-theme="dark"] {
  --bg: #160a2b; --bg2: #221043; --line: #46297e; --tx: #f8f3ff; --mu: #c9b8ea;
  --c0: #31e7ff; --c1: #ff5ce1; --c2: #f2ecff; --hl: #ffb21e; --grid: rgba(255,255,255,.10);
  --band0: rgba(49,231,255,.16); --band1: rgba(255,92,225,.14); --band2: rgba(242,236,255,.12);
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--tx);
  font: 15px/1.45 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
main { max-width: 1100px; margin: 0 auto; padding: 20px 16px 48px; }
h1 { font-size: 22px; margin: 0 0 4px; }
h2 { font-size: 17px; margin: 32px 0 6px; }
p.note, .mu { color: var(--mu); }
p.note { margin: 0 0 10px; max-width: 75ch; }
dl.info { display: grid; grid-template-columns: repeat(auto-fit, minmax(190px, 1fr)); gap: 6px 18px;
  margin: 14px 0; }
dl.info dt { color: var(--mu); font-size: 12px; } dl.info dd { margin: 0; font-variant-numeric: tabular-nums; }
.card { background: var(--bg2); border: 1px solid var(--line); border-radius: 10px; padding: 12px; }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; font-size: 14px; }
th, td { text-align: left; padding: 5px 10px 5px 0; border-bottom: 1px solid var(--line); white-space: nowrap; }
th { color: var(--mu); font-weight: 600; font-size: 12px; }
tr.pick { cursor: pointer; } tr.pick:hover td { color: var(--hl); }
.sw { display: inline-block; width: 10px; height: 10px; border-radius: 2px; margin-right: 6px; }
.sw.c0 { background: var(--c0); } .sw.c1 { background: var(--c1); } .sw.c2 { background: var(--c2); }
.plot { width: 100%; height: 300px; position: relative; }
.plot.tall { height: 360px; } .plot.short { height: 200px; }
.plot svg { display: block; width: 100%; height: 100%; }
.tip { position: absolute; pointer-events: none; background: var(--bg); border: 1px solid var(--line);
  border-radius: 6px; padding: 4px 8px; font-size: 12px; white-space: nowrap; display: none;
  font-variant-numeric: tabular-nums; }
.controls { display: flex; flex-wrap: wrap; gap: 8px 14px; align-items: center; margin: 6px 0 8px; font-size: 14px; }
.controls input[type=range] { flex: 1 1 220px; }
button, select { font: inherit; color: var(--tx); background: var(--bg); border: 1px solid var(--line);
  border-radius: 6px; padding: 4px 10px; }
button.on { border-color: var(--hl); color: var(--hl); }
.grid3 { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 12px; }
.legend { display: flex; gap: 14px; flex-wrap: wrap; font-size: 13px; color: var(--mu); margin: 4px 0; }
.empty { color: var(--mu); padding: 30px 0; text-align: center; }
footer { margin-top: 36px; color: var(--mu); font-size: 12px; }
</style>
</head>
<body>
<main>
<h1>Imusyklin profiili</h1>
<div class="mu">Suckboxx-raakadata · luotu %%GENERATED%%</div>
<dl class="info">%%INFO%%</dl>

<div class="card scroll">
<table>
<tr><th>Kanava</th><th>Syklit</th><th>rpm (ka ± hajonta)</th><th>Keskiarvo kPa</th><th>Δ SYL 1 kPa</th>
<th>Pohja kPa</th><th>Huippu-huippu kPa</th><th>Ajoitus</th><th>Suurin poikkeama</th><th>Raaka mV</th></tr>
%%TABLE%%
</table>
</div>
<p class="note">Δ kuten laitteella: SYL 1 − kanava, positiivinen = imee enemmän kuin SYL 1. Ajoitus = imupulssin
alku SYL 1:n jälkeen kampiasteina (720° = sykli). Poikkeama = syklin RMS-ero keskiprofiilista suhteessa
kanavan tyypilliseen (1,0× = tavallinen sykli).</p>

<h2>Aikasarja</h2>
<p class="note">Raakadata kaikilta kanavilta. Pystyviivat ovat tunnistettuja syklirajoja (imupulssin
laskevan reunan puoliväli). Vedä liukusäädintä tai valitse ikkunan pituus.</p>
<div class="card">
  <div class="controls">
    <label>Ikkuna <select id="win"><option value="0.5">0,5 s</option><option value="1" selected>1 s</option>
      <option value="2">2 s</option><option value="0">koko</option></select></label>
    <input type="range" id="pos" min="0" max="1000" value="0" aria-label="Aikaikkunan alku">
    <span id="posl" class="mu"></span>
  </div>
  <div class="legend"><span><span class="sw c0"></span>SYL 1</span><span><span class="sw c1"></span>SYL 2</span>
    <span><span class="sw c2"></span>SYL 3</span></div>
  <div class="plot tall" id="p_time"></div>
</div>

<div id="pulse_sections">
<h2>Syklit päällekkäin</h2>
<p class="note">Jokainen ohut viiva on yksi imusykli, paksu viiva keskiprofiili ja kaista 10–90 %:n
vaihteluväli. Vaaka-akselin nolla on imupulssin laskevan reunan puoliväli, ei yläkuolokohta.
Korostettu sykli (oranssi) valitaan poikkeamataulukosta.</p>
<div class="grid3">
  <div class="card"><b>SYL 1</b><div class="plot" id="p_ov0"></div></div>
  <div class="card"><b>SYL 2</b><div class="plot" id="p_ov1"></div></div>
  <div class="card"><b>SYL 3</b><div class="plot" id="p_ov2"></div></div>
</div>

<h2>Sylinterit vertailussa</h2>
<p class="note">Keskiprofiilit samassa kuvassa omaan laskevaan reunaansa kohdistettuina. Muodon erot
(syvyys, palautumisen nopeus, sulkeutumisen jälkeinen värähtely) kertovat sylinterikohtaisista eroista.
Tasoero on synkronointi.</p>
<div class="card"><div class="plot tall" id="p_cmp"></div></div>

<h2>Poikkeamat ja kierrosluku sykleittäin</h2>
<div class="grid3">
  <div class="card">
    <b>Suurimmat poikkeamat</b>
    <table><tr><th>Kanava</th><th>Aika</th><th>Poikkeama</th></tr>%%TOPROWS%%</table>
    <p class="note" style="margin-top:8px">Napsauta riviä: sykli korostuu ja aikasarja siirtyy siihen.
    Yli ~3× poikkeava sykli on syytä katsoa.</p>
  </div>
  <div class="card" style="grid-column: span 2"><div class="plot" id="p_score"></div></div>
</div>
<div class="card" style="margin-top:12px"><div class="plot short" id="p_rpm"></div></div>
</div>
<div id="no_pulse" class="card empty" style="display:none">Moottorin imupulssia ei tunnistettu
(moottori seis, penkkitesti tai letkut T-haarassa). Alla aikasarja ja kanavien yhtäpitävyys.</div>

<h2>Kanavien erot ajan yli</h2>
<p class="note" id="diffnote"></p>
<div class="card">
  <div class="plot" id="p_diff"></div>
  <table style="margin-top:8px"><tr><th>Ero</th><th>Keskiarvo kPa</th><th>Hajonta kPa</th></tr>%%DIFFROWS%%</table>
</div>

<h2>Miten tätä luetaan</h2>
<div class="card">
<p class="note">Imusyklin aikana kanavan paine putoaa, kun mäntä lähtee alas avoimen imuventtiilin
takana. Syvin kohta osuu männän suurimpaan nopeuteen, ja paine alkaa palata ennen alakuolokohtaa.
Venttiilin sulkeutuessa virtaus pysähtyy ja kanavaan jää värähtely. Muiden tahtien aikana kanava
täyttyy kaasuläpän raon kautta.</p>
<p class="note">Mitä etsiä: yksittäinen poikkeava sykli (sytytyskatko, laiha sykli), toistuva kuoppa tai
piikki "hiljaisessa" vaiheessa (vuotava imuventtiili näkyy paineiskuna puristus- ja työtahdin aikana),
sylinterin muodon ero muihin (kaasutin, venttiilivälys) ja suuri hajontakaista (epätasainen käynti).</p>
<p class="note">Rajoitukset: letku anturin ja tulpan välissä soi omalla taajuudellaan (pitkä letku noin
100–200 Hz) ja pyöristää muotoa, joten lyhyt letku on tärkein yksittäinen asia. Anturin oma vasteaika
on noin 2,5 ms, ja 1 kHz:n näytteistyksellä tyhjäkäynnin imutahtiin osuu noin 35 näytettä,
3000 rpm:llä noin 10. Kampikulma on suhteellinen, koska yläkuolokohdan merkkiä ei ole.</p>
</div>
<footer>tools/cycle_profile.py · Suckboxx</footer>
</main>

<script>
const D = %%DATA%%;
const HAS_PULSE = %%HASPULSE%% === 1;
const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const COL = () => [css('--c0'), css('--c1'), css('--c2')];
const BAND = () => [css('--band0'), css('--band1'), css('--band2')];
let sel = null;  // {ch, k}

function nice(lo, hi, n) {
  const span = hi - lo || 1, raw = span / n, mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const st = [1, 2, 2.5, 5, 10].map(m => m * mag).find(s => span / s <= n) || 10 * mag;
  const out = [];
  for (let v = Math.ceil(lo / st) * st; v <= hi + st * 1e-6; v += st) out.push(+v.toFixed(10));
  return out;
}
function fmt(v) {
  const s = Number.isInteger(v) ? String(v) : Math.abs(v) >= 100 ? v.toFixed(0) : Math.abs(v) >= 10 ? v.toFixed(1) : v.toFixed(2);
  return s.replace('.', ',').replace('-', '−');
}

// Yleinen viivakuvaaja SVG:nä. series: {x, y, color, w, op, name}; bands: {x, lo, hi, fill};
// vlines: {x, color, op}; bars: {x, y, w, color, onclick}.
function plot(el, o) {
  const W = el.clientWidth, H = el.clientHeight, ml = 48, mr = 10, mt = 8, mb = 30;
  const pw = W - ml - mr, ph = H - mt - mb;
  let xs = [], ys = [];
  (o.series || []).forEach(s => { xs.push(...minmax(s.x)); ys.push(...minmax(s.y)); });
  (o.bands || []).forEach(b => { ys.push(...minmax(b.lo), ...minmax(b.hi)); });
  (o.bars || []).forEach(b => { xs.push(b.x); ys.push(b.y, 0); });
  let [x0, x1] = o.xr || [Math.min(...xs), Math.max(...xs)];
  let [y0, y1] = o.yr || [Math.min(...ys), Math.max(...ys)];
  if (!isFinite(x0) || !isFinite(y0)) { el.innerHTML = '<div class="empty">ei dataa</div>'; return; }
  if (y1 - y0 < 1e-6) { y0 -= 1; y1 += 1; }
  const pad = (y1 - y0) * 0.06; y0 -= pad; y1 += pad;
  if (x1 - x0 < 1e-9) x1 = x0 + 1;
  const X = v => ml + (v - x0) / (x1 - x0) * pw, Y = v => mt + (y1 - v) / (y1 - y0) * ph;
  const g = [];
  const grid = css('--grid'), mu = css('--mu');
  nice(y0, y1, Math.max(3, Math.floor(ph / 45))).forEach(v => {
    g.push(`<line x1="${ml}" x2="${ml + pw}" y1="${Y(v)}" y2="${Y(v)}" stroke="${grid}"/>`,
      `<text x="${ml - 6}" y="${Y(v) + 4}" text-anchor="end" font-size="11" fill="${mu}">${fmt(v)}</text>`);
  });
  nice(x0, x1, Math.max(3, Math.floor(pw / 80))).forEach(v => {
    g.push(`<line x1="${X(v)}" x2="${X(v)}" y1="${mt}" y2="${mt + ph}" stroke="${grid}"/>`,
      `<text x="${X(v)}" y="${H - 10}" text-anchor="middle" font-size="11" fill="${mu}">${fmt(v)}</text>`);
  });
  g.push(`<text x="${ml + pw}" y="${H - 1}" text-anchor="end" font-size="11" fill="${mu}">${o.xl || ''}</text>`,
    `<text x="4" y="${mt + 10}" font-size="11" fill="${mu}">${o.yl || ''}</text>`);
  g.push(`<svg x="${ml}" y="${mt}" width="${pw}" height="${ph}" viewBox="${ml} ${mt} ${pw} ${ph}" overflow="hidden">`);
  (o.vlines || []).forEach(v => { if (v.x >= x0 && v.x <= x1)
    g.push(`<line x1="${X(v.x)}" x2="${X(v.x)}" y1="${mt}" y2="${mt + ph}" stroke="${v.color}" stroke-opacity="${v.op || .35}" stroke-dasharray="${v.dash || ''}"/>`); });
  (o.bands || []).forEach(b => {
    let p = b.x.map((x, i) => `${X(x).toFixed(1)},${Y(b.hi[i]).toFixed(1)}`);
    p = p.concat(b.x.map((x, i) => `${X(x).toFixed(1)},${Y(b.lo[i]).toFixed(1)}`).reverse());
    g.push(`<polygon points="${p.join(' ')}" fill="${b.fill}" stroke="none"/>`);
  });
  (o.bars || []).forEach((b, i) => {
    const bw = Math.max(1.5, (b.w || 1) / (x1 - x0) * pw * 0.8);
    g.push(`<rect data-bar="${i}" x="${X(b.x) - bw / 2}" y="${Y(Math.max(b.y, 0))}" width="${bw}" height="${Math.abs(Y(0) - Y(b.y))}" fill="${b.color}" ${b.onclick ? 'style="cursor:pointer"' : ''}/>`);
  });
  (o.series || []).forEach(s => {
    const n = s.x.length, stepN = Math.max(1, Math.floor(n / (pw * 2)));
    const pts = [];
    for (let i = 0; i < n; i += stepN) if (s.x[i] >= x0 - (x1 - x0) * .01 && s.x[i] <= x1 + (x1 - x0) * .01)
      pts.push(`${X(s.x[i]).toFixed(1)},${Y(s.y[i]).toFixed(1)}`);
    g.push(`<polyline points="${pts.join(' ')}" fill="none" stroke="${s.color}" stroke-width="${s.w || 1.5}" stroke-opacity="${s.op || 1}" stroke-linejoin="round"/>`);
  });
  g.push('</svg>');
  g.push(`<line class="cross" x1="0" x2="0" y1="${mt}" y2="${mt + ph}" stroke="${mu}" stroke-opacity="0" />`);
  el.innerHTML = `<svg viewBox="0 0 ${W} ${H}">${g.join('')}</svg><div class="tip"></div>`;
  const svg = el.querySelector('svg'), tip = el.querySelector('.tip'), cross = el.querySelector('.cross');
  (o.bars || []).forEach((b, i) => { if (b.onclick) svg.querySelector(`[data-bar="${i}"]`).addEventListener('click', b.onclick); });
  const named = (o.series || []).filter(s => s.name);
  if (!named.length) return;
  svg.addEventListener('mousemove', e => {
    const r = svg.getBoundingClientRect(), px = (e.clientX - r.left) * W / r.width;
    if (px < ml || px > ml + pw) { tip.style.display = 'none'; cross.setAttribute('stroke-opacity', 0); return; }
    const xv = x0 + (px - ml) / pw * (x1 - x0);
    const lines = named.map(s => { const i = nearest(s.x, xv); return `<span style="color:${s.color}">■</span> ${s.name}: ${fmt(s.y[i])}`; });
    tip.innerHTML = `${o.xl ? fmt(xv) + ' ' + o.xl : ''}<br>${lines.join('<br>')}`;
    tip.style.display = 'block';
    const tx = (e.clientX - r.left) + 14;
    tip.style.left = Math.min(tx, r.width - tip.offsetWidth - 4) + 'px';
    tip.style.top = '8px';
    cross.setAttribute('x1', px); cross.setAttribute('x2', px); cross.setAttribute('stroke-opacity', .5);
  });
  svg.addEventListener('mouseleave', () => { tip.style.display = 'none'; cross.setAttribute('stroke-opacity', 0); });
}
function minmax(a) { let lo = Infinity, hi = -Infinity; for (const v of a) { if (v < lo) lo = v; if (v > hi) hi = v; } return [lo, hi]; }
function nearest(a, v) { let lo = 0, hi = a.length - 1; while (hi - lo > 1) { const m = (lo + hi) >> 1; if (a[m] < v) lo = m; else hi = m; } return Math.abs(a[lo] - v) <= Math.abs(a[hi] - v) ? lo : hi; }

const T = Array.from({ length: D.n }, (_, i) => i / D.fs);

function drawTime() {
  const win = +document.getElementById('win').value, total = D.n / D.fs;
  const span = win > 0 ? Math.min(win, total) : total;
  const pos = +document.getElementById('pos').value / 1000 * (total - span);
  document.getElementById('posl').textContent = `${pos.toFixed(2)}–${(pos + span).toFixed(2)} s`;
  const i0 = Math.max(0, Math.floor(pos * D.fs)), i1 = Math.min(D.n, Math.ceil((pos + span) * D.fs) + 1);
  const col = COL();
  const series = D.ch.map((c, k) => ({ x: T.slice(i0, i1), y: c.raw.slice(i0, i1), color: col[k], w: 1.4, name: c.name }));
  const vlines = [];
  D.ch.forEach((c, k) => c.bounds.forEach(b => vlines.push({ x: b / 1000, color: col[k], op: .35 })));
  D.gaps.forEach(g => vlines.push({ x: g, color: css('--hl'), op: .9, dash: '4 3' }));
  if (sel) { const cy = D.ch[sel.ch].cycles[sel.k]; vlines.push({ x: cy.t, color: css('--hl'), op: 1 }); }
  plot(document.getElementById('p_time'), { series, vlines, xr: [pos, pos + span], xl: 's', yl: 'kPa' });
}

function drawOverlays() {
  const col = COL(), band = BAND(), hl = css('--hl');
  D.ch.forEach((c, k) => {
    const el = document.getElementById('p_ov' + k);
    if (!c.prof) { el.innerHTML = '<div class="empty">ei syklejä</div>'; return; }
    const series = [];
    c.cycles.forEach(cy => { if (cy.prof) series.push({ x: D.xdeg, y: cy.prof, color: col[k], w: 1, op: .22 }); });
    series.push({ x: D.xdeg, y: c.prof.mean, color: col[k], w: 2.6, name: 'keskiprofiili' });
    if (sel && sel.ch === k && c.cycles[sel.k].prof)
      series.push({ x: D.xdeg, y: c.cycles[sel.k].prof, color: hl, w: 2.2, name: 'valittu' });
    plot(el, { series, bands: [{ x: D.xdeg, lo: c.prof.lo, hi: c.prof.hi, fill: band[k] }], xl: '°', yl: 'kPa',
      vlines: [{ x: 0, color: css('--mu'), op: .6, dash: '3 3' }] });
  });
  const series = D.ch.filter(c => c.prof).map(c => ({ x: D.xdeg, y: c.prof.mean, color: col[D.ch.indexOf(c)], w: 2.4, name: c.name }));
  plot(document.getElementById('p_cmp'), { series, xl: '° (suhteellinen)', yl: 'kPa' });
}

function drawScores() {
  const col = COL(), bars = [], rpm = [];
  D.ch.forEach((c, k) => c.cycles.forEach((cy, i) => {
    if (cy.z != null) bars.push({ x: cy.t + k * 0.004, y: cy.z, w: 0.02, color: sel && sel.ch === k && sel.k === i ? css('--hl') : col[k],
      onclick: () => pick(k, i) });
  }));
  plot(document.getElementById('p_score'), { bars, xl: 's', yl: 'poikkeama ×', xr: [0, D.n / D.fs],
    vlines: [{ x: 0, color: css('--mu'), op: 0 }] });
  const series = D.ch.map((c, k) => {
    const v = c.cycles.filter(cy => cy.ok);  // myös kiihdytys ja hidastus
    return { x: v.map(cy => cy.t), y: v.map(cy => cy.rpm), color: col[k], w: 1.6, name: c.name };
  }).filter(s => s.x.length);
  plot(document.getElementById('p_rpm'), { series, xl: 's', yl: 'rpm', xr: [0, D.n / D.fs] });
}

function drawDiff() {
  const col = COL();
  const series = D.diff.d.map((y, k) => ({ x: D.diff.t, y, color: col[k + 1], w: 1.6, name: 'SYL 1 − ' + D.ch[k + 1].name }));
  plot(document.getElementById('p_diff'), { series, xl: 's', yl: 'Δ kPa' });
  document.getElementById('diffnote').textContent = HAS_PULSE
    ? `Liukuva keskiarvo yhden syklin (${D.diff.w_ms} ms) yli, Δ = SYL 1 − kanava kuten laitteella. Tasainen viiva = vakaa synkronointi.`
    : `Liukuva keskiarvo ${D.diff.w_ms} ms. Penkkitestissä (kaikki letkut samassa alipaineessa T-haaralla) erojen pitäisi olla lähellä nollaa: se kertoo anturien ja kalibroinnin yhtäpitävyyden.`;
}

function pick(ch, k) {
  sel = { ch, k };
  const t = D.ch[ch].cycles[k].t, total = D.n / D.fs;
  const win = +document.getElementById('win').value || total, span = Math.min(win, total);
  document.getElementById('pos').value = Math.round(Math.max(0, Math.min(1, (t - span / 3) / Math.max(1e-9, total - span))) * 1000);
  drawAll();
}

function drawAll() {
  drawTime();
  if (HAS_PULSE) { drawOverlays(); drawScores(); }
  drawDiff();
}
if (!HAS_PULSE) { document.getElementById('pulse_sections').style.display = 'none'; document.getElementById('no_pulse').style.display = 'block'; }
document.getElementById('win').addEventListener('change', drawTime);
document.getElementById('pos').addEventListener('input', drawTime);
document.querySelectorAll('tr.pick').forEach(tr => tr.addEventListener('click', () => pick(+tr.dataset.ch, +tr.dataset.k)));
let rt = 0; addEventListener('resize', () => { clearTimeout(rt); rt = setTimeout(drawAll, 150); });
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', drawAll);
drawAll();
</script>
</body>
</html>
"""


def main() -> int:
    ap = argparse.ArgumentParser(description="Imusyklin profiilianalyysi Suckboxxin raakadatasta")
    ap.add_argument("csv", nargs="?", help="laitteelta ladattu suckboxx_raw.csv")
    ap.add_argument("--url", help="lataa suoraan laitteelta, esim. http://192.168.4.1/api/raw.csv")
    ap.add_argument("--out", help="raportin polku (oletus: CSV:n viereen _profiili.html)")
    ap.add_argument("--open", action="store_true", help="avaa raportti selaimeen")
    ap.add_argument("--lp", type=float, metavar="HZ",
                    help="suodata profiilit (esim. 100): letkun resonanssi pois muodosta")
    args = ap.parse_args()

    if args.url:
        with urllib.request.urlopen(args.url, timeout=30) as r:
            text = r.read().decode("utf-8")
        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        csv_path = Path(f"suckboxx_raw_{stamp}.csv")
        csv_path.write_text(text, encoding="utf-8")
        print(f"raakadata tallennettu: {csv_path}")
    elif args.csv:
        csv_path = Path(args.csv)
        text = csv_path.read_text(encoding="utf-8")
    else:
        ap.error("anna CSV-tiedosto tai --url")

    d = parse_csv(text)
    gaps = fill_gaps(d)
    if args.lp is not None and not (5.0 <= args.lp <= 0.45 * float(d["meta"].get("fs_hz", "1000") or 1000)):
        ap.error("--lp: 5 Hz ... 0,45 x näytetaajuus")
    page = build_report(d, csv_path.name, gaps, args.lp)
    out = Path(args.out) if args.out else csv_path.with_name(csv_path.stem + "_profiili.html")
    out.write_text(page, encoding="utf-8")
    print(f"raportti: {out}")
    if args.open:
        webbrowser.open(out.resolve().as_uri())
    return 0


if __name__ == "__main__":
    sys.exit(main())
