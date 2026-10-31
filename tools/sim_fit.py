#!/usr/bin/env python3
"""Suckboxx - EngineSimin parametrit oikeasta raakanauhoituksesta (WP-12).

Lukee laitteen /api/raw.csv:n tai tools/serial_record.py:n CSV:n (sama muoto)
ja arvioi, millä EngineSimin (firmware/src/core/engine_sim.h) parametreilla
simulaattori tuottaisi samanlaisen signaalin. Tulos on /api/sim-lomake, jonka
voi lähettää laitteelle tai mock_serverille (POST /api/sim), ja --json-raportti.

Moottori käy (imupulssi tunnistettu, syklit cycle_profile.py:n tunnistuksella):
  rpm, jitter    syklien jaksoista: 120·fs / keskijakso ja jakson suhteellinen
                 hajonta peräkkäisten jaksojen erotuksista (hidas ajautuminen ei
                 kasvata sitä)
  base, err1..3  syklin keskiarvot (D1) kPa:na; base = kanavien keskiarvo,
                 err = kanava − base (EngineSim: + = vähemmän alipainetta)
  depth, peak,   EngineSimin pulssimallin (sin²-imukuoppa, paluuaalto, muiden
  xtalk          sylinterien vuoto 240°:n välein) sovitus syklisynkroniseen
                 keskiprofiiliin
  res            letkun soiminen: taajuus spektripiikistä 50–300 Hz (keskiprofiilin
                 pulssiosa poistettu), herätteen amplitudi ja vaimeneminen
                 syklitahtisesta vaimenevan värähtelyn sovituksesta
  noise          ADC-kohina 350–490 Hz:n kaistalta (valkoinen kohina, mediaanipohjainen,
                 joten yksittäiset piikit eivät nosta sitä)
  H1             osuus näytteistä ≥ sat_mv, saturoituvat syklit, litteän huipun
                 kesto ja kuinka paljon huipusta jää leikkautumatta näkyvän
                 muodon perusteella (D4, rauta.md §1.3)
Moottori seis (lepotila): kanavakohtaiset offsetit mV:nä ja kPa:na, kohina,
100 ms:n huipusta huippuun -vaihtelu ja kanavaerot, eli lähtötason tarkistus.

Parametri, jota ei voi arvioida luotettavasti, jätetään lomakkeesta pois ja syy
kerrotaan. res_hz, res_decay_s ja intake_frac eivät ole /api/sim-avaimia
(SimParams-kenttiä), joten ne tulostetaan erikseen.

Käyttö:
  python tools/sim_fit.py 2026-11-07_joutokaynti_lammin_1.csv
  python tools/sim_fit.py nauhoitus.csv --json > tulos.json
  python tools/sim_fit.py nauhoitus.csv --start 5 --end 35
  python tools/sim_fit.py joutokaynti.csv --zero-from lepo.csv
  python tools/sim_fit.py 2026-11-07_lepo.csv          # lepotila tunnistetaan itse
  tools/replay/build/replay --sim $(python tools/sim_fit.py x.csv --form | tr '&' ' ') \\
      --seconds 30 --out x_sim.csv                      # EngineSim-vastine toistoon (WP-8)

tools/replay --sim -viennin otsakkeen sim= (totuus) vertaillaan arvioihin
automaattisesti (ristiintodennus, docs/moottoriajo.md).

Vaatii numpyn ja scipyn. Syklintunnistus tuodaan tools/cycle_profile.py:stä.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from scipy import optimize, signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cycle_profile as cp  # noqa: E402  (sama hakemisto)

CH = cp.CH
NAMES = cp.NAMES

# firmware/src/core/sim_api.h: avaimet ja rajat (rajaus, ei hylkäys).
SIM_KEYS: dict[str, tuple[float, float]] = {
    "engine_on": (0.0, 1.0), "rpm": (60.0, 12000.0), "jitter": (0.0, 0.4),
    "drift": (-2000.0, 2000.0), "base": (-100.0, 0.0),
    "err1": (-30.0, 30.0), "err2": (-30.0, 30.0), "err3": (-30.0, 30.0),
    "depth": (0.0, 60.0), "res": (0.0, 50.0), "xtalk": (0.0, 1.0), "peak": (0.0, 60.0),
    "noise": (0.0, 1000.0), "fault1": (0.0, 3.0), "fault2": (0.0, 3.0), "fault3": (0.0, 3.0),
}
# Lomakkeen järjestys ja desimaalit.
FORM_ORDER = [("engine_on", 0), ("rpm", 0), ("jitter", 4), ("base", 2), ("err1", 2),
              ("err2", 2), ("err3", 2), ("depth", 2), ("res", 2), ("xtalk", 3), ("peak", 2),
              ("noise", 2), ("fault1", 0), ("fault2", 0), ("fault3", 0)]

# firmware/src/core/types.h (SensorParams, StatusParams), engine_sim.h
SAT_MV = 2680          # rivi 10
FAULT_LO_MV = 100      # rivi 7
FAULT_HI_MV = 3150
OFF_KPA = 5.0          # D10: lepotilan tunnistus ennen nollausta
SENSOR_ACC_KPA = 2.0   # R4: anturin oma tarkkuus ±2 kPa
HOSE_OFF_KPA = 2.0     # rivi 6
K_BUMP = 0.5           # engine_sim.h kBumpFrac: paluuaallon leveys = puolet imutahdista
QUANT_VAR = 1.0 / 12.0  # mV-pyöristyksen varianssi (EngineSim pyöristää kokonaisiksi)

RES_BAND = (50.0, 300.0)
NOISE_LO_HZ = 350.0
NOISE_HI_FRAC = 0.49   # × fs
PROM_MIN_DB = 6.0      # resonanssipiikin vähimmäiskorkeus kaistan mediaanista
RING_R2_MIN = 0.6
FIT_R2_MIN = 0.8
FLAT_MS_CLIP = 3.0     # näin pitkä litteä huippu = leikkautuminen
DELTA_AVG = 8          # laitteen avg_cycles (DspParams)
WELCH_N = 256

PSI = -cp.PRE + np.arange(cp.NB) / cp.NB   # keskiprofiilin vaihe syklirajasta (sykleinä)


# ---------------------------------------------------------------------------
# Luku ja kalibrointi
# ---------------------------------------------------------------------------

def _floats(text: str | None, default: list[float]) -> list[float]:
    if not text:
        return list(default)
    try:
        v = [float(s) for s in text.split(",")]
    except ValueError:
        return list(default)
    return v if len(v) == CH else list(default)


def load_recording(path: str | Path, start: float | None = None,
                   end: float | None = None) -> dict:
    """CSV (molemmat lähteet) -> mV-matriisi 3 × n, seq-aukot täytettyinä."""
    text = Path(path).read_text(encoding="utf-8")
    d = cp.parse_csv(text)
    gaps = cp.fill_gaps(d)
    meta = d["meta"]
    fs = float(meta.get("fs_hz") or 1000)
    mv = np.array(d["mv"], dtype=float)
    n = mv.shape[1]
    i0 = 0 if start is None else max(0, int(round(start * fs)))
    i1 = n if end is None else min(n, int(round(end * fs)))
    if i1 - i0 < int(2 * fs):
        raise SystemExit("liian lyhyt ikkuna: vähintään 2 s dataa")
    vz = float(meta.get("v_zero_mv") or 2700)
    k = float(meta.get("mv_per_kpa") or 25)
    if not k > 0:
        raise SystemExit(f"mv_per_kpa={k}: odotettiin positiivista")
    return {
        "path": str(path), "name": Path(path).name, "meta": meta, "fs": fs,
        "mv": mv[:, i0:i1], "t0_s": i0 / fs,
        "gaps": [g - i0 for g in gaps if i0 <= g < i1],
        "vz": vz, "k": k,
        "hdr_off": _floats(meta.get("offset_kpa"), [0.0] * CH),
        "hdr_gain": _floats(meta.get("gain"), [1.0] * CH),
    }


def calibration(rec: dict, mode: str, zero_from: dict | None = None) -> dict:
    """kPa = gain·(mV − v_zero)/mv_per_kpa + offset kuten laitteella (calibration.h).
    nominal: gain 1, offset 0. header: CSV:n otsake (= CSV:n kPa-sarakkeet).
    zero_from: offset lepotilan nauhoituksesta (nollaus jälkikäteen), gain otsakkeesta."""
    vz, k = rec["vz"], rec["k"]
    if mode == "nominal":
        gain, off = [1.0] * CH, [0.0] * CH
    else:
        gain, off = list(rec["hdr_gain"]), list(rec["hdr_off"])
    src = mode
    if zero_from is not None:
        rest_mv = zero_from["mv"].mean(axis=1)
        off = [-gain[c] * (rest_mv[c] - vz) / k for c in range(CH)]
        src = f"{mode} + nolla tiedostosta {zero_from['name']}"
    return {"mode": src, "gain": gain, "off": off, "vz": vz, "k": k}


def to_kpa(cal: dict, c: int, mv):
    return cal["gain"][c] * (np.asarray(mv, dtype=float) - cal["vz"]) / cal["k"] + cal["off"][c]


def kpa_per_mv(cal: dict, c: int) -> float:
    return cal["gain"][c] / cal["k"]


# ---------------------------------------------------------------------------
# Apufunktiot
# ---------------------------------------------------------------------------

def robust_sigma(x) -> float:
    x = np.asarray(x, dtype=float)
    if x.size == 0:
        return float("nan")
    return float(1.4826 * np.median(np.abs(x - np.median(x))))


def runs(mask) -> list[tuple[int, int]]:
    """Totuusarvotaulukon yhtenäiset True-jaksot [a, b)."""
    m = np.concatenate([[False], np.asarray(mask, dtype=bool), [False]])
    dm = np.diff(m.astype(int))
    return list(zip(np.flatnonzero(dm == 1), np.flatnonzero(dm == -1)))


def welch_runs(x, fs: float, nperseg: int = WELCH_N):
    """Welch-tiheys (yksipuolinen) äärellisistä yhtenäisistä jaksoista. NaN = ei dataa."""
    x = np.asarray(x, dtype=float)
    acc, wsum = None, 0
    f = None
    for a, b in runs(np.isfinite(x)):
        if b - a < nperseg:
            continue
        f, p = signal.welch(x[a:b], fs=fs, nperseg=nperseg, noverlap=nperseg // 2,
                            detrend="constant")
        nseg = 1 + (b - a - nperseg) // (nperseg // 2)
        acc = p * nseg if acc is None else acc + p * nseg
        wsum += nseg
    if acc is None:
        return None, None, 0
    return f, acc / wsum, wsum


def nice(v, nd=2):
    """JSON:iin: numpy -> float, NaN -> None."""
    if v is None:
        return None
    if isinstance(v, (bool, np.bool_)):
        return bool(v)
    if isinstance(v, (int, np.integer)):
        return int(v)
    v = float(v)
    if not math.isfinite(v):
        return None
    return round(v, nd)


# ---------------------------------------------------------------------------
# EngineSimin pulssimalli (engine_sim.h pulseShape)
# ---------------------------------------------------------------------------

def _dip_bump(u, w):
    wb = K_BUMP * w
    dip = np.where(u < w, np.sin(np.pi * u / w) ** 2, 0.0)
    bump = np.where((u >= w) & (u < w + wb), np.sin(np.pi * (u - w) / wb) ** 2, 0.0)
    return dip, bump


def _basis(phi0: float, w: float) -> np.ndarray:
    """Sarakkeet [1, −kuoppa, paluuaalto] omalle sylinterille."""
    dip, bump = _dip_bump(np.mod(PSI - phi0, 1.0), w)
    return np.column_stack([np.ones_like(PSI), -dip, bump])


def pulse_model(p, psi=PSI):
    """Keskiprofiili vaiheessa psi (sykleinä syklirajasta): taso m, oma imukuoppa a
    ja paluuaalto b imun alusta phi0 alkaen, sekä samanmuotoiset muiden sylinterien
    pulssit kertoimilla x1 (+240°) ja x2 (+480°), kuten EngineSimin
    xtalk·(shape_sum − shape[c])."""
    m, a, b, x1, x2, phi0, w = p
    u0 = np.mod(psi - phi0, 1.0)

    def shape(u):
        dip, bump = _dip_bump(u, w)
        return -a * dip + b * bump

    return (m + shape(u0) + x1 * shape(np.mod(u0 - 1.0 / 3.0, 1.0))
            + x2 * shape(np.mod(u0 - 2.0 / 3.0, 1.0)))


def res_columns(psi, phir: float, f: float, tau: float, P_s: float):
    """Syklitahtinen vaimeneva värähtely (EngineSimin resonaattori): heräte E
    vaiheessa phir joka syklissä, z(t) = E·e^(st) / (1 − e^(sP)), s = −1/τ + i2πf,
    t = ((ψ − phir) mod 1)·P. Ulostulo Im(z) = Re(E)·Im(g) + Im(E)·Re(g)."""
    t = np.mod(psi - phir, 1.0) * P_s
    s = complex(-1.0 / tau, 2.0 * np.pi * f)
    g = np.exp(s * t) / (1.0 - np.exp(s * P_s))
    return g.imag, g.real


def joint_model(q, P_s: float, psi=PSI):
    er, ei, f, tau, phir = q[7:]
    gi, gr = res_columns(psi, phir, f, tau, P_s)
    return pulse_model(q[:7], psi) + er * gi + ei * gr


def fit_profile(prof, mask=None) -> dict | None:
    """Sovittaa EngineSimin pulssimallin keskiprofiiliin. mask = käytettävät pisteet
    (H1: saturoituneet vaiheet jätetään pois, jolloin huippu arvioidaan reunoistaan)."""
    y = np.asarray(prof, dtype=float)
    use = np.ones_like(y, dtype=bool) if mask is None else np.asarray(mask, dtype=bool)
    if use.sum() < 40:
        return None

    def grid(ws, phis):
        # Vain oma kuoppa ja paluuaalto: muiden sylinterien vapaat sarakkeet
        # tekisivät mallista ylijoustavan (leveä w ja naapurit yhdessä sopivat
        # mihin tahansa). Ristivuoto tulee vasta tarkennuksessa.
        out = []
        for w in ws:
            for phi0 in phis:
                A = _basis(phi0, w)[use]
                coef, *_ = np.linalg.lstsq(A, y[use], rcond=None)
                if coef[1] <= 0:
                    continue
                out.append((float(np.sum((A @ coef - y[use]) ** 2)), float(w), float(phi0), coef))
        return sorted(out, key=lambda v: v[0])

    coarse = grid(np.arange(0.08, 0.601, 0.02), np.arange(-0.40, 0.10, 0.01))
    if not coarse:
        return None
    starts = []
    for _, w0, phi00, _ in coarse[:3]:
        fine = grid(np.arange(w0 - 0.02, w0 + 0.0201, 0.0025),
                    np.arange(phi00 - 0.01, phi00 + 0.0101, 0.001))
        if fine:
            starts.append(fine[0])
    lo_b = [-np.inf, 0.0, 0.0, -0.5, -0.5, -0.6, 0.05]
    hi_b = [np.inf, 250.0, 250.0, 1.5, 1.5, 0.3, 0.6]
    best = None
    for _, w0, phi00, coef in starts:
        x0 = [float(coef[0]), float(coef[1]), max(float(coef[2]), 0.0), 0.0, 0.0, phi00,
              float(np.clip(w0, 0.05, 0.6))]
        lo = list(lo_b)
        hi = list(hi_b)
        lo[5], hi[5] = phi00 - 0.05, phi00 + 0.05
        x0 = list(np.clip(x0, np.array(lo) + 1e-9, np.array(hi) - 1e-9))
        try:
            r = optimize.least_squares(lambda p: (pulse_model(p) - y)[use], x0,
                                       bounds=(lo, hi), x_scale="jac")
            p, cost = r.x, float(r.cost)
        except (ValueError, np.linalg.LinAlgError):
            continue
        if best is None or cost < best[0]:
            best = (cost, p)
    if best is None:
        return None
    p = best[1]
    res = pulse_model(p) - y
    sst = float(np.sum((y[use] - y[use].mean()) ** 2)) or 1e-12
    r2 = 1.0 - float(np.sum(res[use] ** 2)) / sst
    m, a, b, x1, x2, phi0, w = (float(v) for v in p)
    return {"m": m, "depth": a, "peak": b, "x1": x1, "x2": x2, "phi0": phi0, "w": w,
            "r2": r2, "rmse": float(np.sqrt(np.mean(res[use] ** 2))), "p": p,
            "top": float(np.max(pulse_model(p))), "used": int(use.sum()), "sse": float(np.sum(res[use] ** 2))}


def fit_joint(prof, mask, fit: dict, f0: float, tau0: float, P_s: float,
              f_lim: tuple[float, float]) -> dict | None:
    """Pulssimalli ja letkuresonanssi yhtä aikaa keskiprofiiliin. Syklitahtinen
    soiminen on osa keskiprofiilia; ilman sitä sin²-sovitus vinoutuu, kun soiminen
    on syklin mittainen (korkeat kierrokset), ja spektrissä syklitahtinen soiminen
    näkyy vain syklitaajuuden monikertoina. Heräte haetaan imun alusta, keskeltä
    ja lopusta (EngineSim: imun alku)."""
    y = np.asarray(prof, dtype=float)
    use = np.ones_like(y, dtype=bool) if mask is None else np.asarray(mask, dtype=bool)
    base = y - pulse_model(fit["p"])
    best = None
    for dphi in (0.0, 0.5 * fit["w"], fit["w"]):
        phir = fit["phi0"] + dphi
        gi, gr = res_columns(PSI, phir, f0, tau0, P_s)
        e, *_ = np.linalg.lstsq(np.column_stack([gi, gr])[use], base[use], rcond=None)
        q0 = list(fit["p"]) + [float(e[0]), float(e[1]), f0, tau0, phir]
        lo = [-np.inf, 0.0, 0.0, -0.5, -0.5, fit["phi0"] - 0.05, 0.05, -np.inf, -np.inf,
              f_lim[0], 0.001, phir - 0.1]
        hi = [np.inf, 250.0, 250.0, 1.5, 1.5, fit["phi0"] + 0.05, 0.6, np.inf, np.inf,
              f_lim[1], 0.5, phir + 0.1]
        q0 = list(np.clip(q0, np.array(lo) + 1e-9, np.array(hi) - 1e-9))
        try:
            r = optimize.least_squares(lambda q: (joint_model(q, P_s) - y)[use], q0,
                                       bounds=(lo, hi), x_scale="jac")
        except (ValueError, np.linalg.LinAlgError):
            continue
        if best is None or r.cost < best[0]:
            best = (float(r.cost), r.x, lo, hi)
    if best is None:
        return None
    _, q, lo, hi = best
    res = joint_model(q, P_s) - y
    sse = float(np.sum(res[use] ** 2))
    sst = float(np.sum((y[use] - y[use].mean()) ** 2)) or 1e-12
    at_bound = [i for i in (9, 10) if min(q[i] - lo[i], hi[i] - q[i]) < 1e-6 * max(1.0, abs(q[i]))]
    m, a, b, x1, x2, phi0, w = (float(v) for v in q[:7])
    return {"m": m, "depth": a, "peak": b, "x1": x1, "x2": x2, "phi0": phi0, "w": w,
            "r2": 1.0 - sse / sst, "rmse": float(np.sqrt(np.mean(res[use] ** 2))), "p": q[:7],
            "q": q, "top": float(np.max(joint_model(q, P_s))), "used": int(use.sum()), "sse": sse,
            "res_amp": float(math.hypot(q[7], q[8])), "res_hz": float(q[9]),
            "res_decay_s": float(q[10]), "res_phase": float(q[11]),
            "res_theta_deg": float(math.degrees(math.atan2(q[8], q[7]))),
            "sse_gain": sse / fit["sse"] if fit["sse"] > 0 else 1.0, "at_bound": bool(at_bound)}


# ---------------------------------------------------------------------------
# Kanavan analyysi (moottori käy)
# ---------------------------------------------------------------------------

def detect_cycles(x: np.ndarray, fs: float) -> dict | None:
    xl = [float(v) for v in x]
    filt = cp.filtfilt(cp.lowpass_coeffs(cp.LP_HZ, fs), xl)
    if cp.estimate_period(filt, fs) is None:
        return None
    r = cp.analyze_channel(xl, filt, fs)
    return r if r["good"] else None


def lp_profile_at(prof: np.ndarray, n_keep: int, jpos: np.ndarray) -> np.ndarray:
    """Keskiprofiilin n_keep ensimmäistä harmonista (jaksollinen trigonometrinen
    polynomi) murtolukuindekseissä jpos."""
    nb = len(prof)
    F = np.fft.rfft(prof)
    out = np.full(jpos.shape, F[0].real / nb)
    for n in range(1, min(n_keep, nb // 2 - 1) + 1):
        th = 2.0 * np.pi * n * jpos / nb
        out += (2.0 / nb) * (F[n].real * np.cos(th) - F[n].imag * np.sin(th))
    return out


def residual(x: np.ndarray, r: dict, fs: float, f_keep: float,
             fit: dict | None = None, ceil_kpa: float | None = None) -> np.ndarray:
    """x − syklisynkroninen keskiprofiilin pulssiosa − syklin tasomuutos.

    Pulssiosa = sovitettu EngineSim-pulssimalli (jos on; leikattuna anturin
    kattoon ceil_kpa kuten EngineSimissä) + mallin ja keskiprofiilin erotuksesta
    harmoniset alle f_keep Hz. Letkun soiminen on syklitahdissa (imutahti
    herättää sen joka syklissä samassa kohdassa), joten se kuuluu keskiprofiiliin:
    koko profiilin poisto veisi resonanssin mukanaan, ja pelkkä alipäästetty
    profiili jättäisi pulssin (ja ristivuodon 3 × syklitaajuuden) harmoniset
    hakukaistalle. NaN hylättyjen syklien kohdalla."""
    prof = np.asarray(r["prof"]["mean"], dtype=float)
    pmean = float(np.mean(prof))
    hi = np.inf if ceil_kpa is None else ceil_kpa

    def model(psi):
        return np.minimum(pulse_model(fit["p"], psi), hi)

    mis = prof - model(PSI) if fit is not None else prof
    n_keep = int(math.floor(f_keep * r["pmed"] / fs))
    out = np.full(len(x), np.nan)
    for c in r["good"]:
        i0, i1 = int(math.ceil(c["t0"])), int(math.ceil(c["t1"]))
        if i1 <= i0:
            continue
        i = np.arange(i0, i1)
        jpos = np.mod(((i - c["t0"]) / c["P"] + cp.PRE) * cp.NB, cp.NB)
        val = lp_profile_at(mis, n_keep, jpos)
        if fit is not None:
            val = val + model(jpos / cp.NB - cp.PRE)
        out[i0:i1] = x[i0:i1] - val - (c["mean"] - pmean)
    return out


def ringdown(res_sig: np.ndarray, r: dict, fs: float, f_hint: float) -> dict | None:
    """Syklitahtinen keskiarvo omien syklirajojen ympäriltä ja vaimenevan
    värähtelyn A·e^(−t/τ)·sin(2πft + θ) sovitus verhokäyrän huipusta alkaen.
    A vastaa EngineSimin herätesamplitudia (res), τ res_decay_s:ää."""
    P = r["pmed"]
    pre, post = int(0.3 * P), int(0.6 * P)
    taus = np.arange(-pre, post + 1, dtype=float)
    idx = np.arange(len(res_sig), dtype=float)
    acc = []
    for c in r["good"]:
        pos = c["t0"] + taus
        if pos[0] < 0 or pos[-1] > len(res_sig) - 1:
            continue
        v = np.interp(pos, idx, res_sig)
        if np.all(np.isfinite(v)):
            acc.append(v)
    if len(acc) < 8:
        return None
    acc = np.array(acc)
    e = acc.mean(axis=0)
    se = float(np.std(acc - e, axis=0).mean() / math.sqrt(len(acc)))
    hi = min(1.6 * f_hint, 0.45 * fs)
    lo = 0.6 * f_hint
    if not lo < hi:
        return None
    sos = signal.butter(2, [lo, hi], btype="bandpass", fs=fs, output="sos")
    try:
        e_bp = signal.sosfiltfilt(sos, e)
    except ValueError:
        return None
    env = np.abs(signal.hilbert(e_bp))
    edge = 3
    kp = edge + int(np.argmax(env[edge:-edge]))
    L = min(len(e) - kp, max(int(0.45 * P), int(4 * fs / f_hint)))
    if L < int(3 * fs / f_hint):
        return None
    seg_env = env[kp:kp + L]
    ok = seg_env > 0.15 * seg_env[0]
    tt = np.arange(L, dtype=float)
    d0 = 0.02 * fs
    if ok.sum() >= 4:
        sl = np.polyfit(tt[ok], np.log(seg_env[ok]), 1)[0]
        if sl < 0:
            d0 = float(np.clip(-1.0 / sl, 2.0, 0.5 * fs))
    an = signal.hilbert(e_bp)[kp]
    th0 = float(np.angle(an) + np.pi / 2)
    y = e[kp:kp + L]

    def f(t, A, fr, d, th, c0):
        return A * np.exp(-t / d) * np.sin(2 * np.pi * fr * t / fs + th) + c0

    p0 = [float(env[kp]), f_hint, d0, th0, 0.0]
    bounds = ([0.0, lo, 1.0, -10.0, -np.inf], [np.inf, hi, 0.5 * fs, 10.0, np.inf])
    p0 = list(np.clip(p0, np.array(bounds[0]) + 1e-9, np.array(bounds[1]) - 1e-9))
    try:
        p, _ = optimize.curve_fit(f, tt, y, p0=p0, bounds=bounds, maxfev=20000)
    except (RuntimeError, ValueError):
        return None
    fr, d = float(p[1]), float(p[2])
    # Herätteen hetki: kaistanpäästön verhokäyrä huipentuu vasta noin puoli jaksoa
    # alun jälkeen, ja EngineSimin res on amplitudi herätteen hetkellä (excite).
    # Haetaan alku τ0 ennen huippua mallilla "0 ennen τ0, vaimeneva värähtely
    # τ0:sta", kiinteillä f:llä ja τ:lla (lineaarinen PNS jokaiselle τ0:lle).
    per = fs / fr
    pre_w = max(2, int(round(0.5 * per)))
    best = None
    for t0 in np.arange(max(float(pre_w), kp - 1.5 * per), kp + 0.01, 0.25):
        a, b = int(math.floor(t0 - pre_w)), int(math.ceil(t0 + L))
        if a < 0 or b > len(e):
            continue
        t = np.arange(a, b, dtype=float)
        dt = t - t0
        on = dt >= 0
        env_t = np.where(on, np.exp(-np.maximum(dt, 0) / d), 0.0)
        A = np.column_stack([env_t * np.sin(2 * np.pi * fr * dt / fs),
                             env_t * np.cos(2 * np.pi * fr * dt / fs), np.ones_like(t)])
        coef, *_ = np.linalg.lstsq(A, e[a:b], rcond=None)
        sse = float(np.sum((A @ coef - e[a:b]) ** 2))
        if best is None or sse < best[0]:
            yy = e[a:b]
            best = (sse, t0, coef, float(np.sum((yy - yy.mean()) ** 2)) or 1e-12)
    if best is None:
        return None
    sse, t0, coef, sst = best
    return {"amp": float(math.hypot(coef[0], coef[1])), "amp_peak": float(p[0]), "hz": fr,
            "decay_s": d / fs, "r2": 1.0 - sse / sst, "se": se,
            "t_onset_ms": float((t0 - pre) / fs * 1000.0),
            "t_peak_ms": float(taus[kp] / fs * 1000.0), "events": len(acc)}


def resonance(x: np.ndarray, r: dict, fs: float, band: tuple[float, float],
              fit: dict | None, ceil_kpa: float | None = None) -> dict:
    depth = fit["depth"] if fit else None
    if fit is not None and fit["r2"] < FIT_R2_MIN:
        fit = None
    # Vaihe 1: piikin haku. Pulssimalli ja keskiprofiilin loput harmoniset alle
    # 0,8 × kaistan alarajan poistetaan, jolloin kaistalle jää soiminen ja kohina.
    res_sig = residual(x, r, fs, 0.8 * band[0], fit, ceil_kpa)
    f, p, nseg = welch_runs(res_sig, fs)
    out: dict = {"ok": False, "amp_ok": False}
    if f is None or nseg < 4:
        out["why"] = "liian vähän yhtenäisiä syklejä spektriin"
        return out
    sel = (f >= band[0]) & (f <= band[1])
    fb, pb = f[sel], p[sel]
    floor = float(np.median(pb))
    # Korkein kaistan sisäinen paikallinen maksimi: kaistan reunalle laskeva
    # jäännös (pulssin yläpää) ei ole piikki. Reuna valitaan vain, jos sisältä
    # ei löydy mitään.
    inner = [i for i in range(1, len(pb) - 1) if pb[i] >= pb[i - 1] and pb[i] >= pb[i + 1]]
    k = max(inner, key=lambda i: pb[i]) if inner else int(np.argmax(pb))
    if floor > 0 and 10.0 * math.log10(pb[k] / floor) < PROM_MIN_DB:
        # Yläreunaa kohti nouseva teho = soiminen kaistan yläpuolella; se ratkaisee
        # ennen alareunaa, jossa on aina pulssin syklikohtaisen vaihtelun jäännöstä.
        last = len(pb) - 1
        if pb[last] >= pb[last - 1] and 10.0 * math.log10(pb[last] / floor) >= PROM_MIN_DB:
            k = last
        elif pb[0] >= pb[1] and 10.0 * math.log10(pb[0] / floor) >= PROM_MIN_DB:
            k = 0
    prom = 10.0 * math.log10(pb[k] / floor) if floor > 0 else 0.0
    f_pk = float(fb[k])
    if 0 < k < len(pb) - 1:
        la, lb, lc = np.log(pb[k - 1:k + 2])
        den = la - 2 * lb + lc
        if den < 0:
            f_pk += 0.5 * (la - lc) / den * float(f[1] - f[0])
    out.update({"hz_spec": f_pk, "prom_db": prom, "segments": nseg})
    hint = (" (lyhyt letku soi korkeammalla: kokeile --res-band 50 490)"
            if band[1] < 0.45 * fs else "")
    if k == 0:
        out["why"] = (f"ei resonanssipiikkiä {band[0]:g}–{band[1]:g} Hz:n kaistalla: teho vain "
                      f"nousee alareunaa kohti (pulssin syklikohtainen vaihtelu){hint}")
        return out
    if k == len(pb) - 1:
        out["why"] = (f"teho suurin kaistan yläreunalla {band[1]:g} Hz: soiminen on "
                      f"kaistan yläpuolella{hint}")
        return out
    if prom < PROM_MIN_DB:
        out["why"] = (f"ei resonanssipiikkiä {band[0]:g}–{band[1]:g} Hz:n kaistalla "
                      f"(korkein {prom:.1f} dB < {PROM_MIN_DB:g} dB){hint}")
        return out
    out["ok"] = True
    out["hz"] = f_pk
    # Vaihe 2: pulssin harmoniset poistetaan 0,6 × piikin taajuuteen asti (pulssin
    # reunat eivät enää vuoda soimisen sovitukseen; resonanssin oma kaista
    # 1/(πτ) on tätä paljon kapeampi).
    res_sig = residual(x, r, fs, 0.6 * f_pk, fit, ceil_kpa)
    f2, p2, _ = welch_runs(res_sig, fs)
    if f2 is not None:
        f, p = f2, p2
    # Kaistan RMS piikin ympäriltä (soimisen voimakkuus kPa:na)
    lo, hi = 0.7 * f_pk, min(1.4 * f_pk, 0.45 * fs)
    bsel = (f >= lo) & (f <= hi)
    out["rms_kpa"] = float(math.sqrt(np.sum(p[bsel]) * float(f[1] - f[0])))
    rd = ringdown(res_sig, r, fs, f_pk)
    if rd is None:
        out["amp_why"] = "vaimenevan värähtelyn sovitus ei onnistunut"
        return out
    out["ring"] = rd
    if abs(rd["hz"] / f_pk - 1.0) <= 0.15:
        out["hz"] = rd["hz"]
    if rd["r2"] >= RING_R2_MIN and rd["amp"] > 4.0 * rd["se"]:
        out["amp_ok"] = True
        out["amp"] = rd["amp"]
        out["decay_s"] = rd["decay_s"]
        if depth:
            out["rel_depth"] = rd["amp"] / depth
    else:
        out["amp_why"] = (f"vaimeneva värähtely ei erotu (sovitus R² {rd['r2']:.2f}, "
                          f"amplitudi {rd['amp']:.2f} kPa, keskiarvon kohina {rd['se']:.2f} kPa)")
    return out


def joint_resonance(rs: dict, prof, mask, fit: dict | None, P_s: float,
                    band: tuple[float, float], fs: float) -> tuple[dict | None, dict]:
    """Tarkentaa resonanssin ja pulssimallin yhteissovituksella keskiprofiiliin.
    Hyväksytään, jos taajuus ja vaimeneminen eivät jää rajoille ja jäännös pienenee
    selvästi. Muuten pidetään pelkkä pulssisovitus ja vaimenevan värähtelyn tulos."""
    if not rs.get("ok") or fit is None or fit["r2"] < FIT_R2_MIN:
        return fit, rs
    ring = rs.get("ring")
    ring_ok = ring is not None and ring["r2"] >= RING_R2_MIN
    f0 = ring["hz"] if ring_ok else rs["hz_spec"]
    tau0 = float(np.clip(ring["decay_s"] if ring_ok else 0.02, 0.002, 0.4))
    f_lim = (max(0.7 * band[0], 10.0), min(1.2 * band[1], 0.49 * fs))
    best = None
    # Syklitahtinen soiminen näkyy spektrissä vain syklitaajuuden monikertoina,
    # joten spektripiikki voi olla jopa harmonisten välin sivussa: ilman
    # onnistunutta vaimenevan värähtelyn sovitusta aloitetaan puolen välin askelin.
    ks = (0, -1, 1) if ring_ok else (0, -1, 1, -2, 2, -3, 3)
    for fs0 in (f0 + k * 0.5 / P_s for k in ks):
        if not f_lim[0] < fs0 < f_lim[1]:
            continue
        j = fit_joint(prof, mask, fit, fs0, tau0, P_s, f_lim)
        if j is not None and (best is None or j["sse"] < best["sse"]):
            best = j
    if best is None or best["at_bound"] or best["sse_gain"] > 0.8:
        if best is not None:
            rs["joint_rejected"] = {"sse_gain": best["sse_gain"], "at_bound": best["at_bound"],
                                    "hz": best["res_hz"], "amp": best["res_amp"]}
        return fit, rs
    rs["joint"] = {k: best[k] for k in ("res_amp", "res_hz", "res_decay_s", "res_theta_deg",
                                        "sse_gain", "r2")}
    rs["hz"] = best["res_hz"]
    rs["amp"] = best["res_amp"]
    rs["decay_s"] = best["res_decay_s"]
    if best["depth"] > 0:
        rs["rel_depth"] = best["res_amp"] / best["depth"]
    if P_s < 3.0 * best["res_decay_s"]:
        # Edellisen syklin soiminen ei ehdi vaimeta: amplitudi ja vaimeneminen
        # sekoittuvat (sovitus säilyttää lähinnä niiden tulon). Taajuus pätee.
        rs["amp_ok"] = False
        rs["amp_why"] = (f"sykli {P_s * 1000:.0f} ms < 3 × vaimeneminen "
                         f"{best['res_decay_s'] * 1000:.0f} ms: amplitudi ja vaimeneminen eivät "
                         "erotu, arvioi res tyhjäkäynniltä")
    else:
        rs["amp_ok"] = True
        rs.pop("amp_why", None)
    return best, rs


NOISE_BINS = 24        # syklin vaihelokeroa kohinan arvioon


def noise_hf(mv: np.ndarray, fs: float, lo_hz: float, cycles: list[dict]) -> dict:
    """Valkoisen kohinan σ (mV) kaistalta lo_hz…0,49·fs syklin hiljaisista vaiheista.

    Imuventtiilin isku ja letkun soimisen alku ovat jyrkkiä, joten ne näkyvät
    ylipäästetyssä signaalissa joka syklissä samassa vaiheessa. Siksi σ arvioidaan
    vaihelokeroittain (mediaanipohjaisesti) ja otetaan hiljaisimman kolmanneksen
    mediaani: venttiili kiinni -vaiheessa kanavalla on vain ADC-kohina.
    Piikit (> 8σ) lasketaan samoista hiljaisista vaiheista."""
    hi_hz = NOISE_HI_FRAC * fs
    lo_hz = min(lo_hz, 0.88 * hi_hz)
    sos = signal.butter(6, lo_hz, btype="highpass", fs=fs, output="sos")
    hp = signal.sosfiltfilt(sos, mv - np.mean(mv))
    w, h = signal.sosfreqz(sos, worN=4096, fs=fs)
    g4 = np.abs(h) ** 4  # filtfilt = |H|², tehona |H|⁴
    frac = float(np.sum(0.5 * (g4[1:] + g4[:-1]) * np.diff(w)) / (fs / 2.0))
    idx, bins = [], []
    for cy in cycles:
        i0, i1 = int(math.ceil(cy["t0"])), int(math.ceil(cy["t1"]))
        i = np.arange(max(i0, 0), min(i1, len(mv)))
        idx.append(i)
        bins.append(np.minimum(((i - cy["t0"]) / cy["P"] * NOISE_BINS).astype(int), NOISE_BINS - 1))
    if idx:
        idx_a, bins_a = np.concatenate(idx), np.concatenate(bins)
        sig_b = np.array([robust_sigma(hp[idx_a[bins_a == b]]) if np.sum(bins_a == b) >= 30
                          else np.nan for b in range(NOISE_BINS)])
        order = np.argsort(np.where(np.isfinite(sig_b), sig_b, np.inf))
        ref = float(np.median(sig_b[order[:max(1, NOISE_BINS // 3)]]))
        # Hiljaisiksi kaikki lokerot lähellä hiljaisinta kolmannesta, ja σ yhdistetyistä
        # näytteistä: pelkkä hiljaisimpien lokeroiden valinta vinouttaisi alaspäin.
        quiet = np.flatnonzero(np.isfinite(sig_b) & (sig_b <= 1.25 * ref))
        q_idx = idx_a[np.isin(bins_a, quiet)]
        sb = robust_sigma(hp[q_idx])
    else:
        sb = robust_sigma(hp)
        q_idx = np.arange(len(mv))
    sigma = sb / math.sqrt(frac) if frac > 0 else float("nan")
    big = q_idx[np.abs(hp[q_idx]) > 8.0 * sb]
    spikes = int(np.sum(np.diff(big) > int(0.005 * fs))) + (1 if big.size else 0)
    noise = math.sqrt(max(sigma * sigma - QUANT_VAR, 0.0))
    return {"sigma_mv": sigma, "noise_mv": noise, "band_hz": [lo_hz, hi_hz],
            "spikes_per_s": spikes / max(len(q_idx) / fs, 1e-9)}


def h1_channel(mv: np.ndarray, r: dict, fs: float, sat_mv: float, sigma_mv: float) -> dict:
    """H1: saturoituuko huippu (≥ sat_mv) ja litistyykö se."""
    good = r["good"]
    i0 = int(math.ceil(good[0]["t0"]))
    i1 = int(math.floor(good[-1]["t1"]))
    span = mv[i0:i1 + 1]
    out = {"sat_frac": float(np.mean(span >= sat_mv)), "max_mv": float(np.max(span))}
    cmax, flats, phases = [], [], []
    thr_band = max(3.0 * sigma_mv, 3.0)
    for cy in good:
        a, b = int(math.ceil(cy["t0"])), int(math.floor(cy["t1"])) + 1
        seg = mv[a:b]
        if seg.size == 0:
            continue
        cmax.append(float(seg.max()))
    cmax = np.array(cmax)
    sat_cyc = cmax >= sat_mv
    out["sat_cycles"] = float(np.mean(sat_cyc)) if cmax.size else 0.0
    out["cycle_max_mv_p50"] = float(np.median(cmax)) if cmax.size else None
    if sat_cyc.any():
        ceiling = float(np.median(cmax[sat_cyc]))
        out["ceiling_mv"] = ceiling
        for cy in good:
            a, b = int(math.ceil(cy["t0"])), int(math.floor(cy["t1"])) + 1
            seg = mv[a:b]
            if seg.size == 0 or seg.max() < sat_mv:
                continue
            near = seg >= ceiling - thr_band
            rl = max((e - s for s, e in runs(near)), default=0)
            flats.append(rl / fs * 1000.0)
            sat_i = np.flatnonzero(seg >= sat_mv)
            phases.extend(((a + sat_i - cy["t0"]) / cy["P"] * 720.0).tolist())
        out["flat_ms_p50"] = float(np.median(flats)) if flats else 0.0
        out["sat_phase_deg_p50"] = float(np.median(phases)) if phases else None
    return out


def analyze_running(rec: dict, cal: dict, res_list: list, sat_mv: float,
                    band: tuple[float, float], master: int) -> dict:
    fs = rec["fs"]
    chans: list[dict] = []
    warnings: list[str] = []
    for c in range(CH):
        r = res_list[c]
        mv = rec["mv"][c]
        info: dict = {"name": NAMES[c], "pulse": r is not None}
        med_mv = float(np.median(mv))
        info["median_mv"] = med_mv
        if med_mv < FAULT_LO_MV:
            info["fault"] = 2
        elif med_mv > FAULT_HI_MV:
            info["fault"] = 3
        if r is None:
            lvl = float(np.mean(to_kpa(cal, c, mv)))
            info["level_kpa"] = lvl
            if "fault" not in info and abs(lvl) < OFF_KPA:
                info["fault"] = 1
            chans.append(info)
            continue
        x = to_kpa(cal, c, mv)
        good = r["good"]
        info["cycles"] = len(good)
        info["cycles_rejected"] = len(r["cycles"]) - len(good)
        info["level_kpa"] = float(np.mean([cy["mean"] for cy in good]))
        info["p2p_cycle_kpa"] = float(np.median([cy["max"] - cy["min"] for cy in good]))
        prof = np.asarray(r["prof"]["mean"], dtype=float)
        info["p2p_profile_kpa"] = float(prof.max() - prof.min())
        info["min_kpa"] = float(np.median([cy["min"] for cy in good]))
        # Kohina ensin: H1:n litteän huipun kynnys ja sensuroinnin marginaali
        nz = noise_hf(mv, fs, NOISE_LO_HZ, good)
        info["noise"] = nz
        h1 = h1_channel(mv, r, fs, sat_mv, nz["sigma_mv"])
        info["h1"] = h1
        # Sensuroitu sovitus: vaiheet, joissa yksikin sykli (> 2 %) on saturoitunut,
        # jätetään pois, jolloin paluuaallon huippu arvioidaan näkyvistä reunoista.
        mask = None
        if h1["sat_cycles"] > 0:
            sat_kpa = float(to_kpa(cal, c, sat_mv)) - 2.0 * nz["sigma_mv"] * kpa_per_mv(cal, c)
            profs = np.array([cy["prof"] for cy in good])
            info["_sat_phase"] = np.mean(profs >= sat_kpa, axis=0) > 0.02
            bad = np.convolve(info["_sat_phase"].astype(float), np.ones(5), mode="same") > 0
            mask = ~bad
            h1["censored_points"] = int(bad.sum())
        ceil_kpa = float(to_kpa(cal, c, h1["ceiling_mv"])) if "ceiling_mv" in h1 else None
        fit = fit_profile(prof, mask)
        rs = resonance(x, r, fs, band, fit, ceil_kpa)
        fit, rs = joint_resonance(rs, prof, mask, fit, r["pmed"] / fs, band, fs)
        info["fit"] = fit
        info["resonance"] = rs
        if fit is not None and mask is not None and ceil_kpa is not None:
            h1["clip_excess_kpa"] = fit["top"] - ceil_kpa
        # H1-tulkinta: litteä huippu tai näkyvien reunojen ennustama huippu katon yli
        sig_kpa = nz["sigma_mv"] * kpa_per_mv(cal, c)
        if h1["sat_cycles"] == 0:
            h1["verdict"] = "ei"
        elif (h1.get("flat_ms_p50", 0.0) >= FLAT_MS_CLIP
              or h1.get("clip_excess_kpa", -1.0) > max(3.0 * sig_kpa, 0.3)):
            h1["verdict"] = "leikkautuu"
        else:
            h1["verdict"] = "koskettaa"
        chans.append(info)

    pulse_ch = [c for c in range(CH) if res_list[c] is not None]
    # --- rpm ja jitter (peräkkäisten jaksojen erotus: hidas ajautuminen ei näy) ---
    periods, vn_num, vn_den, t_rpm, rpm_k = [], 0.0, 0, [], []
    for c in pulse_ch:
        g = res_list[c]["good"]
        periods += [cy["P"] for cy in g]
        for a, b in zip(g, g[1:]):
            if abs(b["t0"] - a["t1"]) < 1e-6:
                vn_num += (b["P"] - a["P"]) ** 2
                vn_den += 1
        t_rpm += [cy["t0"] / fs for cy in g]
        rpm_k += [cy["rpm"] for cy in g]
    pmean = float(np.mean(periods))
    rpm = 120.0 * fs / pmean
    jitter = math.sqrt(vn_num / (2.0 * vn_den)) / pmean if vn_den >= 10 else None
    drift = float(np.polyfit(t_rpm, rpm_k, 1)[0]) if len(rpm_k) >= 10 else None
    rpm_sd = float(np.std(rpm_k, ddof=1)) if len(rpm_k) > 1 else None

    # --- kanavaerot (D2/D3): Δ = master − kanava, positiivinen = imee enemmän ---
    delta = None
    if master in pulse_ch and len(pulse_ch) >= 2:
        gm = res_list[master]["good"]
        tm = np.array([0.5 * (cy["t0"] + cy["t1"]) for cy in gm])
        mm = np.array([cy["mean"] for cy in gm])
        d_all = {master: np.zeros_like(mm)}
        delta = {"master": master, "channels": {}}
        for c in pulse_ch:
            if c == master:
                continue
            g = res_list[c]["good"]
            tc = np.array([0.5 * (cy["t0"] + cy["t1"]) for cy in g])
            mc = np.array([cy["mean"] for cy in g])
            d = mm - np.interp(tm, tc, mc)
            d_all[c] = d
            stats = {"mean_kpa": float(np.mean(d)), "sd_cycle_kpa": float(np.std(d))}
            if len(d) >= DELTA_AVG:
                d8 = np.convolve(d, np.ones(DELTA_AVG) / DELTA_AVG, mode="valid")
                stats["sd_8_kpa"] = float(np.std(d8))
                stats["p95_dev_8_kpa"] = float(np.percentile(np.abs(d8 - np.mean(d8)), 95))
            delta["channels"][NAMES[c]] = stats
        if len(gm) >= DELTA_AVG:
            mat = np.array([np.convolve(d_all[c], np.ones(DELTA_AVG) / DELTA_AVG, mode="valid")
                            for c in sorted(d_all)])
            spread = mat.max(axis=0) - mat.min(axis=0)
            delta["spread_8_p50_kpa"] = float(np.median(spread))
            delta["spread_8_p95_kpa"] = float(np.percentile(spread, 95))

    # --- ajoitus (kampiasteina SYL 1:stä) ---
    offs = cp.phase_offsets(res_list, fs) if res_list[0] else [None] * CH

    # --- EngineSim-parametrit ---
    params: dict[str, float] = {"engine_on": 1.0}
    omitted: dict[str, str] = {}
    extra: dict = {}
    params["rpm"] = rpm
    fits = {c: chans[c]["fit"] for c in pulse_ch if chans[c]["fit"] is not None}
    good_fits = {c: f for c, f in fits.items() if f["r2"] >= FIT_R2_MIN}
    if jitter is not None:
        # Syklin raja (laskevan reunan puoliväli) on imutahdin sisällä, a = osuus
        # EngineSimin 240°:n välistä imun alusta rajaan. Rajojen väli jakaa kaksi
        # väliä naapurisyklin kanssa, joten mitattu hajonta on pienempi kuin
        # parametrin (720°:n jakson) hajonta: suhde √(((1−a)² + 2 + a² − a(1−a))/3).
        a = 0.0
        if good_fits:
            a = float(np.clip(-3.0 * np.median([f["phi0"] for f in good_fits.values()]), 0.0, 1.0))
        ratio = math.sqrt(((1 - a) ** 2 + 2 + a * a - a * (1 - a)) / 3.0)
        extra["jitter_measured"] = jitter
        extra["jitter_ratio"] = ratio
        params["jitter"] = jitter / ratio
    else:
        omitted["jitter"] = "alle 10 peräkkäistä hyväksyttyä syklien paria"
    lv = {c: chans[c]["level_kpa"] for c in pulse_ch}
    base = float(np.mean(list(lv.values())))
    params["base"] = base
    for c in pulse_ch:
        params[f"err{c + 1}"] = lv[c] - base
    clipped = [NAMES[c] for c in pulse_ch if chans[c]["h1"]["verdict"] == "leikkautuu"]
    touch = [NAMES[c] for c in pulse_ch if chans[c]["h1"]["verdict"] == "koskettaa"]
    if clipped:
        warnings.append(f"H1: huiput leikkautuvat ({', '.join(clipped)}): base, err ja huippu–huippu "
                        "ovat vinoutuneita (D4, rauta.md §1.3). Jos tämä on tyhjäkäynniltä, ks. "
                        "docs/moottoriajo.md päätöspiste H1.")
    elif touch:
        warnings.append(f"H1: huiput käyvät sat_mv:n yläpuolella ({', '.join(touch)}), mutta eivät "
                        "litisty: positiivinen paine näkyy.")
    for c in range(CH):
        if "fault" in chans[c]:
            params[f"fault{c + 1}"] = float(chans[c]["fault"])
            if c not in pulse_ch:
                omitted[f"err{c + 1}"] = "kanavalla ei pulssia (vika), keskiarvoa ei verrata"
        elif c not in pulse_ch:
            omitted[f"err{c + 1}"] = "kanavalla ei tunnistettua pulssia"

    if good_fits:
        params["depth"] = float(np.median([f["depth"] for f in good_fits.values()]))
        xs = [0.5 * (f["x1"] + f["x2"]) for f in good_fits.values()]
        xt = float(np.median(xs))
        if xt < -0.03:
            warnings.append(f"Ristivuoto {xt:+.3f} < 0: naapurisylinterin imun aikana paine nousee. "
                            "EngineSim ei mallinna tätä (xtalk ≥ 0), lomakkeessa 0.")
        params["xtalk"] = min(max(xt, 0.0), 1.0)
        extra["intake_frac"] = float(np.median([f["w"] for f in good_fits.values()]))
        # Paluuaalto: sensuroidulla sovituksella arvioitavissa, jos reunoja jää näkyviin.
        peaks, bad_peak = [], []
        for c, f in good_fits.items():
            # Paluuaallon vaiheet: [w, 1,5 w] imun alusta; jos saturoituneet vaiheet
            # kattavat yli puolet niistä, huipun korkeutta ei näe.
            u0 = np.mod(PSI - f["phi0"], 1.0)
            bump_pts = (u0 >= f["w"]) & (u0 < (1 + K_BUMP) * f["w"])
            sat_ph = chans[c].get("_sat_phase")
            if sat_ph is not None and bump_pts.any() and np.mean(sat_ph[bump_pts]) > 0.5:
                bad_peak.append(c)
                continue
            peaks.append(f["peak"])
        if peaks and not bad_peak:
            params["peak"] = float(np.median(peaks))
        elif bad_peak:
            omitted["peak"] = ("paluuaallon huippu leikkautuu (H1): yli puolet sen vaiheista on "
                               f"saturoitunut kanavilla {', '.join(NAMES[c] for c in bad_peak)}; "
                               "sensuroitu sovitus antaa vain alarajan")
            lb = [fits[c]["peak"] for c in bad_peak]
            extra["peak_lower_bound"] = float(np.median(lb))
        xr = [(f["x1"], f["x2"]) for f in good_fits.values()]
        extra["xtalk_pairs"] = {NAMES[c]: [f["x1"], f["x2"]] for c, f in good_fits.items()}
        if any(abs(a - b) > 0.1 for a, b in xr):
            warnings.append("Ristivuoto eroaa 240°:n ja 480°:n naapurin välillä: EngineSimin "
                            "yksi xtalk ei kuvaa sitä tarkasti (tasauskanavat vain osan välillä?).")
    else:
        why = ("pulssimallin sovitus ei selitä profiilia (R² < "
               f"{FIT_R2_MIN:g}): muoto poikkeaa EngineSimin sin²-mallista")
        if not fits:
            why = "pulssimallin sovitus epäonnistui"
        for key in ("depth", "xtalk", "peak"):
            omitted[key] = why

    res_ok = {c: chans[c]["resonance"] for c in pulse_ch if chans[c]["resonance"]["ok"]}
    amp_ok = {c: v for c, v in res_ok.items() if v["amp_ok"]}
    if amp_ok:
        params["res"] = float(np.median([v["amp"] for v in amp_ok.values()]))
        extra["res_decay_s"] = float(np.median([v["decay_s"] for v in amp_ok.values()]))
    else:
        reasons = []
        for c in pulse_ch:
            v = chans[c]["resonance"]
            reasons.append(f"{NAMES[c]}: {v.get('why') or v.get('amp_why') or '?'}")
        omitted["res"] = "; ".join(reasons)
    extra["res_hz"] = [res_ok[c]["hz"] if c in res_ok else None for c in range(CH)]
    if len(res_ok) >= 2:
        hz = [v["hz"] for v in res_ok.values()]
        if max(hz) / min(hz) - 1.0 > 0.05:
            warnings.append("Letkuresonanssit eroavat kanavien välillä yli 5 %: letkut eivät "
                            "ole identtiset (R1, rauta.md §4.1).")

    nz = [chans[c]["noise"]["noise_mv"] for c in pulse_ch]
    params["noise"] = float(np.median(nz))
    spikes = max(chans[c]["noise"]["spikes_per_s"] for c in pulse_ch)
    if spikes > 0.2:
        warnings.append(f"ADC-piikkejä {spikes:.1f}/s (> 8σ): noise kuvaa piikittömän kohinan, "
                        "EngineSim ei mallinna piikkejä.")
    if drift is not None:
        extra["drift_rpm_s"] = drift
    extra["rpm_sd"] = rpm_sd
    extra["phase_deg"] = offs
    return {"mode": "running", "params": params, "omitted": omitted, "extra": extra,
            "channels": chans, "delta": delta, "warnings": warnings}


# ---------------------------------------------------------------------------
# Lepotila (moottori seis)
# ---------------------------------------------------------------------------

def analyze_rest(rec: dict, cal: dict, sat_mv: float) -> dict:
    fs = rec["fs"]
    n = rec["mv"].shape[1]
    t = np.arange(n) / fs
    chans, warnings = [], []
    win = int(round(0.1 * fs))
    nb = n // win
    nominal = []
    at_zero = []
    for c in range(CH):
        mv = rec["mv"][c]
        mean_mv = float(np.mean(mv))
        coef = np.polyfit(t, mv, 1)
        det = mv - np.polyval(coef, t)
        sd = float(np.std(det))
        blk = mv[:nb * win].reshape(nb, win)
        # 100 ms:n lohkojen keskihajontojen mediaani: piikit osuvat harvoihin
        # lohkoihin, eikä kokonaislukuinen mV kvantisoi tulosta (vrt. MAD).
        sig = float(np.median(np.std(blk, axis=1, ddof=1)))
        pp = blk.max(axis=1) - blk.min(axis=1)
        med9 = signal.medfilt(mv, 9)
        big = np.abs(mv - med9) > max(6.0 * sig, 15.0)
        spikes = len(runs(big))
        hf = noise_hf(mv, fs, NOISE_LO_HZ, [])
        off_mv = mean_mv - cal["vz"]
        off_nom = off_mv / cal["k"]
        kpa_cal = float(np.mean(to_kpa(cal, c, mv)))
        info = {
            "name": NAMES[c], "mean_mv": mean_mv, "offset_mv": off_mv,
            "offset_kpa_nominal": off_nom, "level_kpa_cal": kpa_cal,
            "sd_mv": sd, "sigma_mv": sig, "noise_mv": math.sqrt(max(sig * sig - QUANT_VAR, 0.0)),
            "sigma_hf_mv": hf["sigma_mv"],
            "sd_kpa": sd / cal["k"], "pp100_p50_mv": float(np.median(pp)),
            "pp100_max_mv": float(pp.max()), "spikes_per_s": spikes / (n / fs),
            "drift_mv_min": float(coef[0] * 60.0), "sat_frac": float(np.mean(mv >= sat_mv)),
            "min_mv": float(mv.min()), "max_mv": float(mv.max()),
        }
        if FAULT_LO_MV <= mean_mv <= FAULT_HI_MV and off_nom >= -0.4:
            at_zero.append(NAMES[c])
        nominal.append(mv / cal["k"])
        if mean_mv < FAULT_LO_MV or mean_mv > FAULT_HI_MV:
            info["fault"] = 2 if mean_mv < FAULT_LO_MV else 3
            warnings.append(f"{NAMES[c]}: {mean_mv:.0f} mV on rivin 7 vikarajojen ulkopuolella "
                            f"({FAULT_LO_MV}…{FAULT_HI_MV} mV): anturi irti tai oikosulussa.")
        elif abs(off_nom) > OFF_KPA:
            warnings.append(f"{NAMES[c]}: offset {off_nom:+.2f} kPa > {OFF_KPA:g} kPa (D10): laite ei "
                            "tunnista lepotilaa ennen nollausta. Letku kiinni alipaineessa tai anturi vialla?")
        elif abs(off_nom) > SENSOR_ACC_KPA:
            warnings.append(f"{NAMES[c]}: offset {off_nom:+.2f} kPa on anturin ±{SENSOR_ACC_KPA:g} kPa:n "
                            "tarkkuuden ulkopuolella (R4).")
        if sd == 0.0:
            warnings.append(f"{NAMES[c]}: lukema ei muutu lainkaan (jumittunut kanava?).")
        if abs(coef[0] * 60.0) / cal["k"] > 0.2:
            warnings.append(f"{NAMES[c]}: lepotaso ajautuu {coef[0] * 60.0:+.1f} mV/min: lämpeneminen "
                            "tai tiivis kotelo (R5)?")
        chans.append(info)
    if at_zero:
        warnings.append(f"{', '.join(at_zero)}: lepotaso on anturin nollapisteen (v_zero_mv) "
                        "tuntumassa. GN-anturi leikkaa ilmanpaineessa, joten mahdollinen positiivinen "
                        "offset ei näy tässä eikä nollaus poista sitä (H1/R4, jatko.md Avoimet 3); "
                        "vahvistuskalibrointi T-haarassa noin −50 kPa:ssa kompensoi.")
    diffs = {}
    blocks = [np.asarray(v[:nb * win]).reshape(nb, win).mean(axis=1) for v in nominal]
    for c in range(1, CH):
        d = blocks[0] - blocks[c]
        diffs[f"SYL 1 − {NAMES[c]}"] = {"mean_kpa": float(np.mean(d)), "sd_100ms_kpa": float(np.std(d))}
    if all(ch["sat_frac"] > 0.5 for ch in chans if "fault" not in ch):
        warnings.append(f"Lepotasot ovat ≥ sat_mv ({sat_mv:g} mV): GN-anturin tavallinen tila "
                        "ilmanpaineessa, ei vika (status.h laskee saturaation vain pulssin kanssa).")
    noise = float(np.median([ch["noise_mv"] for ch in chans if "fault" not in ch] or [0.0]))
    params = {"engine_on": 0.0, "noise": noise}
    for c in range(CH):
        if "fault" in chans[c]:
            params[f"fault{c + 1}"] = float(chans[c]["fault"])
    omitted = {k: "moottori seis: ei pulssia" for k in
               ("rpm", "jitter", "base", "depth", "res", "xtalk", "peak")}
    return {"mode": "rest", "params": params, "omitted": omitted, "extra": {},
            "channels": chans, "diffs": diffs, "warnings": warnings}


# ---------------------------------------------------------------------------
# Kokonaisuus ja tulostus
# ---------------------------------------------------------------------------

def form_string(params: dict) -> tuple[str, list[str]]:
    parts, clamped = [], []
    for key, nd in FORM_ORDER:
        if key not in params:
            continue
        v = float(params[key])
        lo, hi = SIM_KEYS[key]
        if v < lo or v > hi:
            clamped.append(f"{key}={v:.4g} rajattu alueelle {lo:g}…{hi:g}")
            v = min(max(v, lo), hi)
        txt = f"{v:.{nd}f}"
        if float(txt) == 0.0:
            txt = f"{0.0:.{nd}f}"  # ei "-0.00"
        parts.append(f"{key}={txt}")
    return "&".join(parts), clamped


def fit_recording(path: str | Path, start: float | None = None, end: float | None = None,
                  cal_mode: str = "header", zero_from: str | Path | None = None,
                  sat_mv: float = SAT_MV, force_rest: bool = False,
                  band: tuple[float, float] = RES_BAND, master: int = 0) -> dict:
    rec = load_recording(path, start, end)
    zrec = load_recording(zero_from) if zero_from else None
    cal = calibration(rec, cal_mode, zrec)
    fs = rec["fs"]
    meta = rec["meta"]
    head = {
        "file": rec["name"], "source": meta.get("source", "?"), "fs_hz": fs,
        "samples": int(rec["mv"].shape[1]), "seconds": rec["mv"].shape[1] / fs,
        "window_start_s": rec["t0_s"], "gaps_filled": len(rec["gaps"]),
        "calibration": {"mode": cal["mode"], "zero_valid": meta.get("zero_valid"),
                        "gain_valid": meta.get("gain_valid"), "offset_kpa": cal["off"],
                        "gain": cal["gain"], "v_zero_mv": cal["vz"], "mv_per_kpa": cal["k"]},
        "sat_mv": sat_mv,
    }
    res_list: list = [None] * CH
    if not force_rest:
        for c in range(CH):
            res_list[c] = detect_cycles(to_kpa(cal, c, rec["mv"][c]), fs)
    if any(r is not None for r in res_list):
        out = analyze_running(rec, cal, res_list, sat_mv, band, master)
    else:
        out = analyze_rest(rec, cal, sat_mv)
    out.update(head)
    form, clamped = form_string(out["params"])
    out["form"] = form
    out["warnings"] = out["warnings"] + clamped
    if meta.get("aborted"):
        out["warnings"].append("CSV-lataus keskeytyi (aborted): data on katkennut.")
    if rec["gaps"]:
        out["warnings"].append(f"{len(rec['gaps'])} seq-aukkoa täytetty lineaarisesti.")
    if cal_mode == "header" and meta.get("zero_valid") != "1" and zero_from is None \
            and out["mode"] == "running":
        out["warnings"].append("Nauhoituksessa ei ole nollausta (zero_valid=0): base ja err "
                               "sisältävät anturien offsetit (±2 kPa, R4). Käytä --zero-from lepo.csv.")
    truth = compare_truth(meta, out)
    if truth:
        out["truth"] = truth
    return out


# SimParams-oletukset, joita /api/sim ei aseta (engine_sim.h): tools/replay --sim
# -viennissä ne ovat aina nämä.
SIM_FIXED = {"res_hz": 112.0, "res_decay_s": 0.02, "intake_frac": 0.25}


def compare_truth(meta: dict, out: dict) -> list[dict] | None:
    """tools/replay --sim kirjoittaa otsakkeeseen sim=<GET /api/sim -JSON>.
    Vertailu kanavatasoina (base + errN), koska jako base/err on sopimus."""
    if "sim" not in meta:
        return None
    try:
        t = json.loads(meta["sim"])
    except ValueError:
        return None
    p = out["params"]
    rows = []

    def add(key, est, true):
        if est is None or true is None:
            return
        rows.append({"key": key, "est": float(est), "true": float(true),
                     "diff": float(est) - float(true)})

    if out["mode"] == "running":
        for k in ("rpm", "jitter", "depth", "res", "xtalk", "peak", "noise"):
            add(k, p.get(k), t.get(k))
        for c in range(CH):
            if f"err{c + 1}" in p and "base" in t:
                add(f"taso{c + 1}", p["base"] + p[f"err{c + 1}"], t["base"] + t.get(f"err{c + 1}", 0.0))
        if meta.get("generator", "").startswith("tools/replay"):
            ex = out["extra"]
            for c, hz in enumerate(ex.get("res_hz") or []):
                add(f"res_hz{c + 1}", hz, SIM_FIXED["res_hz"] if t.get("res", 0) > 0 else None)
            add("res_decay_s", ex.get("res_decay_s"), SIM_FIXED["res_decay_s"])
            add("intake_frac", ex.get("intake_frac"), SIM_FIXED["intake_frac"])
    else:
        add("noise", p.get("noise"), t.get("noise"))
    for c in range(CH):
        add(f"fault{c + 1}", p.get(f"fault{c + 1}", 0.0), t.get(f"fault{c + 1}"))
    return rows


def fi(v, nd=2, sign=False) -> str:
    if v is None or (isinstance(v, float) and not math.isfinite(v)):
        return "–"
    s = f"{v:+.{nd}f}" if sign else f"{v:.{nd}f}"
    return s.replace(".", ",").replace("-", "−")


def text_report(o: dict) -> str:
    L = []
    cal = o["calibration"]
    L.append(f"Suckboxx sim_fit: {o['file']}")
    L.append(f"lähde {o['source']} · {fi(o['seconds'])} s · {o['fs_hz']:.0f} Hz · "
             f"aukkoja {o['gaps_filled']} · kalibrointi: {cal['mode']} "
             f"(nollaus {'kyllä' if cal['zero_valid'] == '1' else 'ei'}, "
             f"vahvistus {'kyllä' if cal['gain_valid'] == '1' else 'ei'})")
    if o["mode"] == "rest":
        L.append("Tila: moottori seis (ei imupulssia) -> lähtötason tarkistus")
    else:
        cyc = ", ".join(f"{ch['name']} {ch.get('cycles', 0)}" for ch in o["channels"])
        L.append(f"Tila: moottori käy · hyväksytyt syklit: {cyc}")
    L.append("")
    L.append("POST /api/sim:")
    L.append(o["form"])
    if o["omitted"]:
        L.append("")
        L.append("Pois jätetyt:")
        for k, v in o["omitted"].items():
            L.append(f"  {k}: {v}")
    L.append("")
    ch = o["channels"]

    def row(label, vals):
        return f"  {label:<30}" + "".join(f"{v:>12}" for v in vals)

    L.append(row("", [c["name"] for c in ch]))
    if o["mode"] == "rest":
        L.append(row("lepotaso mV", [fi(c["mean_mv"], 1) for c in ch]))
        L.append(row("offset mV (− v_zero)", [fi(c["offset_mv"], 1, True) for c in ch]))
        L.append(row("offset kPa (nimellinen)", [fi(c["offset_kpa_nominal"], 2, True) for c in ch]))
        L.append(row("taso kPa (kalibroitu)", [fi(c["level_kpa_cal"], 2, True) for c in ch]))
        L.append(row("kohina σ mV (100 ms, mediaani)", [fi(c["sigma_mv"], 2) for c in ch]))
        L.append(row("kohina σ mV (350–490 Hz)", [fi(c["sigma_hf_mv"], 2) for c in ch]))
        L.append(row("keskihajonta mV (piikit mukana)", [fi(c["sd_mv"], 2) for c in ch]))
        L.append(row("pp 100 ms mV (p50 / max)",
                     [f"{fi(c['pp100_p50_mv'], 0)}/{fi(c['pp100_max_mv'], 0)}" for c in ch]))
        L.append(row("piikit /s", [fi(c["spikes_per_s"], 2) for c in ch]))
        L.append(row("ajautuminen mV/min", [fi(c["drift_mv_min"], 2, True) for c in ch]))
        L.append(row("≥ sat_mv osuus", [fi(100 * c["sat_frac"], 0) + " %" for c in ch]))
        L.append("")
        for k, v in o["diffs"].items():
            L.append(f"  {k}: {fi(v['mean_kpa'], 3, True)} kPa (100 ms:n hajonta {fi(v['sd_100ms_kpa'], 3)})")
    else:
        def g(c, *keys, nd=2, sign=False):
            v = c
            for k in keys:
                if not isinstance(v, dict) or v.get(k) is None:
                    return "–"
                v = v[k]
            return fi(v, nd, sign)

        L.append(row("taso kPa (syklin ka, D1)", [g(c, "level_kpa", sign=True) for c in ch]))
        L.append(row("huippu–huippu kPa (sykli p50)", [g(c, "p2p_cycle_kpa", nd=1) for c in ch]))
        L.append(row("syvyys kPa (malli)", [g(c, "fit", "depth", nd=1) for c in ch]))
        L.append(row("paluuaalto kPa (malli)", [g(c, "fit", "peak", nd=1) for c in ch]))
        L.append(row("ristivuoto +240° / +480°",
                     [f"{g(c, 'fit', 'x1', nd=2)}/{g(c, 'fit', 'x2', nd=2)}" for c in ch]))
        L.append(row("imutahti (osuus syklistä)", [g(c, "fit", "w", nd=3) for c in ch]))
        L.append(row("mallin R²", [g(c, "fit", "r2", nd=3) for c in ch]))
        L.append(row("resonanssi Hz", [g(c, "resonance", "hz", nd=1) for c in ch]))
        L.append(row("  piikki dB kaistan mediaanista", [g(c, "resonance", "prom_db", nd=1) for c in ch]))
        L.append(row("  heräte kPa (res)", [g(c, "resonance", "amp", nd=2) for c in ch]))
        L.append(row("  vaimeneminen ms", [fi(c["resonance"]["decay_s"] * 1000, 1)
                                          if c.get("resonance", {}).get("decay_s") else "–" for c in ch]))
        L.append(row("  RMS kPa (0,7–1,4 × f)", [g(c, "resonance", "rms_kpa", nd=2) for c in ch]))
        L.append(row("kohina σ mV (350–490 Hz)", [g(c, "noise", "noise_mv", nd=2) for c in ch]))
        L.append(row("H1: tulkinta", [c["h1"]["verdict"] if c.get("h1") else "–" for c in ch]))
        L.append(row("H1: ≥ sat_mv näytteistä", [fi(100 * c["h1"]["sat_frac"], 2) + " %"
                                                if c.get("h1") else "–" for c in ch]))
        L.append(row("H1: saturoituvat syklit", [fi(100 * c["h1"]["sat_cycles"], 1) + " %"
                                               if c.get("h1") else "–" for c in ch]))
        L.append(row("H1: syklin huippu mV (p50)", [g(c, "h1", "cycle_max_mv_p50", nd=0) for c in ch]))
        L.append(row("H1: litteä huippu ms", [g(c, "h1", "flat_ms_p50", nd=1) for c in ch]))
        L.append(row("H1: leikkautuu yli kPa", [g(c, "h1", "clip_excess_kpa", nd=2) for c in ch]))
        L.append(row("H1: saturaation vaihe °", [g(c, "h1", "sat_phase_deg_p50", nd=0) for c in ch]))
        ex = o["extra"]
        L.append("")
        L.append(f"rpm {fi(o['params']['rpm'], 0)} (syklien hajonta {fi(ex.get('rpm_sd'), 1)} rpm), "
                 f"ajautuminen {fi(ex.get('drift_rpm_s'), 1, True)} rpm/s (ei lomakkeessa)")
        if ex.get("jitter_measured") is not None:
            L.append(f"jitter: syklirajojen väleistä {fi(ex['jitter_measured'], 4)}, EngineSimin "
                     f"720°:n jaksoksi korjattuna (÷ {fi(ex['jitter_ratio'], 3)}) "
                     f"{fi(o['params']['jitter'], 4)}")
        ph = ex.get("phase_deg") or []
        if any(v is not None for v in ph[1:]):
            L.append("ajoitus SYL 1:stä: " + ", ".join(
                f"{NAMES[c]} {fi(ph[c], 0)}°" for c in range(1, CH) if ph[c] is not None))
        L.append("EngineSimin muut kentät (eivät /api/sim-avaimia): "
                 f"res_hz {' / '.join(fi(v, 1) for v in ex.get('res_hz', []))} Hz, "
                 f"res_decay_s {fi(ex.get('res_decay_s'), 3)}, "
                 f"intake_frac {fi(ex.get('intake_frac'), 3)}")
        if ex.get("peak_lower_bound") is not None:
            L.append(f"paluuaallon alaraja (sensuroitu sovitus): {fi(ex['peak_lower_bound'], 1)} kPa")
        dl = o.get("delta")
        if dl:
            L.append("")
            L.append(f"Kanavaerot (Δ = {NAMES[dl['master']]} − kanava, D3; {DELTA_AVG} syklin ka kuten laite):")
            for k, v in dl["channels"].items():
                L.append(f"  {k}: Δ {fi(v['mean_kpa'], 2, True)} kPa, syklin hajonta "
                         f"{fi(v['sd_cycle_kpa'], 2)}, {DELTA_AVG} syklin ka hajonta {fi(v.get('sd_8_kpa'), 2)}, "
                         f"p95-poikkeama {fi(v.get('p95_dev_8_kpa'), 2)} kPa")
            if "spread_8_p50_kpa" in dl:
                L.append(f"  spread (D2): mediaani {fi(dl['spread_8_p50_kpa'], 2)}, "
                         f"p95 {fi(dl['spread_8_p95_kpa'], 2)} kPa (rajat 1,5 / 3,0)")
    if o.get("truth"):
        L.append("")
        L.append("Otsakkeen totuus (sim=, tools/replay --sim): arvio / totuus / ero")
        for t in o["truth"]:
            nd = 4 if t["key"] in ("jitter", "res_decay_s") else (3 if t["key"] in ("xtalk", "intake_frac") else 2)
            L.append(f"  {t['key']:<12}{fi(t['est'], nd):>10}{fi(t['true'], nd):>10}"
                     f"{fi(t['diff'], nd, True):>10}")
    if o["warnings"]:
        L.append("")
        L.append("Huomiot:")
        for w in o["warnings"]:
            L.append(f"  - {w}")
    return "\n".join(L)


def to_json(o) -> object:
    if isinstance(o, dict):
        return {str(k): to_json(v) for k, v in o.items()
                if k not in ("p", "q") and not str(k).startswith("_")}
    if isinstance(o, (list, tuple)):
        return [to_json(v) for v in o]
    if isinstance(o, np.ndarray):
        return [to_json(v) for v in o.tolist()]
    if isinstance(o, (float, int, np.floating, np.integer, np.bool_, bool)) or o is None:
        return nice(o, 6)
    return o


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="EngineSimin parametrit Suckboxxin raakanauhoituksesta (/api/sim-lomake)")
    ap.add_argument("csv", help="/api/raw.csv tai tools/serial_record.py:n tallenne")
    ap.add_argument("--json", action="store_true", help="koko tulos JSON:na stdoutiin")
    ap.add_argument("--form", action="store_true",
                    help="vain /api/sim-lomake (esim. replay --sim $(... --form | tr '&' ' '))")
    ap.add_argument("--start", type=float, metavar="S", help="analyysi-ikkunan alku sekunteina")
    ap.add_argument("--end", type=float, metavar="S", help="analyysi-ikkunan loppu sekunteina")
    ap.add_argument("--cal", choices=["header", "nominal"], default="header",
                    help="kPa otsakkeen kalibroinnilla (oletus, = CSV:n kPa) tai nimellisellä "
                         "siirtofunktiolla")
    ap.add_argument("--zero-from", metavar="CSV",
                    help="nollaus jälkikäteen: offsetit lepotilan nauhoituksesta (moottori seis)")
    ap.add_argument("--sat-mv", type=float, default=SAT_MV,
                    help=f"saturaatioraja mV (oletus {SAT_MV}, types.h sat_mv)")
    ap.add_argument("--rest", action="store_true",
                    help="analysoi lepotilana, vaikka pulssia näyttäisi olevan")
    ap.add_argument("--res-band", type=float, nargs=2, metavar=("LO", "HI"),
                    default=list(RES_BAND), help="resonanssin hakukaista Hz (oletus 50 300)")
    ap.add_argument("--master", type=int, choices=[1, 2, 3], default=1,
                    help="kanavaerojen viite (D7, oletus SYL 1)")
    args = ap.parse_args(argv)
    lo, hi = args.res_band
    if not 0 < lo < hi < 0.5 * 1000:
        ap.error("--res-band: 0 < LO < HI < fs/2")
    out = fit_recording(args.csv, args.start, args.end, args.cal, args.zero_from, args.sat_mv,
                        args.rest, (lo, hi), args.master - 1)
    if args.json:
        print(json.dumps(to_json(out), ensure_ascii=False, indent=1))
    elif args.form:
        print(out["form"])
    else:
        print(text_report(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
