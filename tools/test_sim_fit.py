#!/usr/bin/env python3
"""tools/sim_fit.py:n testit synteettisellä datalla (WP-12).

Generaattori on EngineSimin (firmware/src/core/engine_sim.h) Python-käännös:
sama vaihemalli (240°:n välit, lognormaali jitter per väli), sin²-imukuoppa ja
paluuaalto, ristivuoto, vaimeneva letkuresonanssi (heräte vaiheessa arg(1 − ρ)),
anturin siirtofunktio ja GN-leikkaus 200…2700 mV:iin, kohina mV:nä ja
pyöristys. PRNG on eri, joten data ei ole näytteittäin sama kuin laitteella,
mutta parametrien merkitys on sama. Tunnetuilla parametreilla tuotetun
nauhoituksen estimaattien on osuttava toleranssiin.

Ajo:  python tools/test_sim_fit.py      (tai python -m unittest discover -s tools -p "test_sim_fit.py")
"""

from __future__ import annotations

import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

try:
    import numpy as np
    import scipy  # noqa: F401
    HAVE_NP = True
except ImportError:  # pragma: no cover
    HAVE_NP = False

if HAVE_NP:
    import sim_fit

FS = 1000
SEG_START = (0.0, 1.0 / 3.0, 2.0 / 3.0)
SEG_END = (1.0 / 3.0, 2.0 / 3.0, 1.0)


def _clamp(v, lo, hi):
    if not v >= lo:
        return lo
    return hi if v > hi else v


def simulate(seconds: float = 20.0, *, engine_on: bool = True, rpm: float = 850.0,
             jitter: float = 0.01, drift: float = 0.0, base: float = -55.0,
             err=(0.0, 0.0, 0.0), depth: float = 20.0, intake_frac: float = 0.25,
             res_hz=(112.0, 112.0, 112.0), res_kpa: float = 3.0, res_decay_s: float = 0.02,
             xtalk: float = 0.0, peak: float = 0.0, noise_mv: float = 2.0,
             sensor_offset_kpa=(0.0, 0.0, 0.0), sensor_gain_err=(0.0, 0.0, 0.0),
             hose_off=(False, False, False), v_zero_mv: float = 2700.0,
             mv_per_kpa: float = 25.0, seed: int = 1) -> np.ndarray:
    """EngineSim.next() n kertaa. Palauttaa n × 3 -taulukon kokonaisia millivoltteja."""
    rng = np.random.default_rng(seed)
    n = int(round(seconds * FS))
    T = 1.0 / FS
    w = _clamp(intake_frac, 0.05, 0.6)
    wb = 0.5 * w
    dep = _clamp(depth, 0.0, 200.0)
    pk = _clamp(peak, 0.0, 200.0)
    shape_mean = 0.5 * (pk * wb - dep * w)
    xt = _clamp(xtalk, 0.0, 1.0)
    j = _clamp(jitter, 0.0, 0.4)
    seg_sigma = math.sqrt(math.log1p(3.0 * j * j))
    half_var = 0.5 * seg_sigma ** 2
    amp = _clamp(res_kpa, 0.0, 50.0)
    decay = _clamp(res_decay_s, 0.0, 1.0)
    rot = [0j] * 3
    exc = [0j] * 3
    for c in range(3):
        hz = _clamp(res_hz[c], 0.0, 490.0)
        if hz > 0 and decay > 0 and amp > 0:
            rho = math.exp(-T / decay) * complex(math.cos(2 * math.pi * hz * T),
                                                 math.sin(2 * math.pi * hz * T))
            d = 1 - rho
            rot[c] = rho
            exc[c] = amp * d / abs(d)
    lo_mv = min(v_zero_mv - 100.0 * mv_per_kpa, v_zero_mv)
    hi_mv = max(v_zero_mv - 100.0 * mv_per_kpa, v_zero_mv)

    def gauss():  # Irwin-Hall(4) kuten EngineSim
        return (float(rng.random(4).sum()) - 2.0) * 1.7320508

    def seg_factor():
        return _clamp(math.exp(seg_sigma * gauss() - half_var), 0.1, 10.0)

    def pulse(psi):
        if psi < w:
            s = math.sin(math.pi * psi / w)
            v = -dep * s * s
        elif pk > 0 and psi < w + wb:
            s = math.sin(math.pi * (psi - w) / wb)
            v = pk * s * s
        else:
            v = 0.0
        return v - shape_mean

    z = [0j, 0j, 0j]
    phase, seg = 0.0, 0
    sf = seg_factor()
    z[0] += exc[0]
    noise = (rng.random((n, 3, 4)).sum(axis=2) - 2.0) * 1.7320508
    out = np.empty((n, 3))
    for i in range(n):
        r_now = _clamp(rpm + drift * i * T, 60.0, 12000.0)
        if engine_on:
            sh = []
            for c in range(3):
                psi = phase - SEG_START[c]
                if psi < 0:
                    psi += 1.0
                sh.append(pulse(psi))
            ssum = sum(sh)
        for c in range(3):
            kpa = 0.0
            if engine_on and not hose_off[c]:
                kpa = base + err[c] + sh[c] + xt * (ssum - sh[c]) + z[c].imag
            meas = (1.0 + sensor_gain_err[c]) * kpa + sensor_offset_kpa[c]
            mv = _clamp(v_zero_mv + mv_per_kpa * meas, lo_mv, hi_mv)
            mv = _clamp(mv + noise_mv * noise[i, c], 0.0, 3300.0)
            out[i, c] = math.floor(mv + 0.5)
        for c in range(3):
            z[c] = z[c] * rot[c]
            if abs(z[c].real) + abs(z[c].imag) < 1e-6:
                z[c] = 0j
        if engine_on:
            left = T * r_now / 120.0
            for _ in range(8):
                step = left / sf
                if phase + step < SEG_END[seg]:
                    phase += step
                    break
                left = max(left - (SEG_END[seg] - phase) * sf, 0.0)
                seg = (seg + 1) % 3
                phase = SEG_START[seg]
                sf = seg_factor()
                z[seg] += exc[seg]
    return out


def write_csv(path: Path, mv: np.ndarray, *, style: str = "raw", zero_valid: int = 1,
              offset=(0.0, 0.0, 0.0), gain=(1.0, 1.0, 1.0), drop=(), v_zero_mv: float = 2700.0,
              mv_per_kpa: float = 25.0) -> None:
    """/api/raw.csv (raw_ring.h CsvWriter) tai serial_record.py -muoto."""
    off_s = ",".join(f"{v:.4f}" for v in offset)
    gain_s = ",".join(f"{v:.6f}" for v in gain)
    if style == "raw":
        head = ["# Suckboxx raakadata (S5b)", "# device=suckboxx", "# format=1", "# source=sim",
                f"# fs_hz={FS}", f"# v_zero_mv={v_zero_mv:.3f}", f"# mv_per_kpa={mv_per_kpa:.4f}",
                "# rail_comp=0", "# rail_nominal_mv=3300.0", f"# zero_valid={zero_valid}",
                "# gain_valid=0", f"# offset_kpa={off_s}", f"# gain={gain_s}",
                f"# samples={len(mv) - len(drop)}", "# first_seq=0", "# gaps=0", "# gaps_total=0",
                "# kpa=gain*(mv-v_zero_mv)/mv_per_kpa+offset (ei kiskokorjausta)"]
    else:
        head = ["# Suckboxx raakadata (sarjavirta, tools/serial_record.py)", "# device=suckboxx",
                "# format=1", "# source=adc", f"# fs_hz={FS}", f"# v_zero_mv={v_zero_mv:g}",
                f"# mv_per_kpa={mv_per_kpa:g}", f"# zero_valid={zero_valid}", "# gain_valid=0",
                f"# offset_kpa={off_s}", f"# gain={gain_s}", f"# samples={len(mv)}", "# gaps=0",
                "# recorded=2026-11-07T10:00:00", "# stream_end=end samples=1 dropped=0",
                "# kpa=gain*(mv-v_zero_mv)/mv_per_kpa+offset (ei kiskokorjausta)"]
    lines = head + ["seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa"]
    dropset = set(drop)
    for i, row in enumerate(mv):
        if i in dropset:
            continue
        kp = [gain[c] * (row[c] - v_zero_mv) / mv_per_kpa + offset[c] for c in range(3)]
        lines.append(f"{i},{i},{int(row[0])},{int(row[1])},{int(row[2])},"
                     f"{kp[0]:.3f},{kp[1]:.3f},{kp[2]:.3f}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


@unittest.skipUnless(HAVE_NP, "numpy/scipy puuttuu")
class SimFitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_fit(self, name, mv, csv_kw=None, **fit_kw):
        p = self.dir / f"{name}.csv"
        write_csv(p, mv, **(csv_kw or {}))
        return sim_fit.fit_recording(p, **fit_kw)

    def near(self, got, want, tol, what):
        self.assertIsNotNone(got, what)
        self.assertLessEqual(abs(got - want), tol, f"{what}: {got:.4f}, odotettiin {want} ± {tol}")

    # --- moottori käy ---------------------------------------------------

    def test_idle_default(self):
        """EngineSimin oletukset (850 rpm, syvyys 20, resonanssi 3 kPa @ 112 Hz)."""
        err = (0.0, 0.8, -1.2)
        mv = simulate(30, err=err, seed=3)
        o = self.run_fit("idle", mv)
        p = o["params"]
        self.assertEqual(o["mode"], "running")
        self.near(p["rpm"], 850, 850 * 0.005, "rpm")
        self.near(p["jitter"], 0.01, 0.002, "jitter")
        self.assertLess(o["extra"]["jitter_ratio"], 1.0)
        self.near(p["base"], -55 + sum(err) / 3, 0.3, "base")
        for c in range(3):
            self.near(p[f"err{c + 1}"], err[c] - sum(err) / 3, 0.15, f"err{c + 1}")
        self.near(p["depth"], 20, 1.0, "depth")
        self.near(p["xtalk"], 0, 0.02, "xtalk")
        self.near(p["peak"], 0, 0.5, "peak")
        self.near(p["res"], 3.0, 0.6, "res")
        self.near(p["noise"], 2.0, 0.2, "noise")
        for c in range(3):
            self.near(o["extra"]["res_hz"][c], 112, 112 * 0.03, f"res_hz {c}")
        self.near(o["extra"]["res_decay_s"], 0.02, 0.006, "res_decay_s")
        self.near(o["extra"]["intake_frac"], 0.25, 0.02, "intake_frac")
        for ch in o["channels"]:
            self.assertEqual(ch["h1"]["sat_frac"], 0.0)
            self.assertEqual(ch["h1"]["verdict"], "ei")
        self.assertEqual(o["omitted"], {})
        d = o["delta"]
        self.near(d["channels"]["SYL 2"]["mean_kpa"], err[0] - err[1], 0.1, "Δ SYL 2")
        self.near(d["spread_8_p50_kpa"], max(err) - min(err), 0.15, "spread")

    def test_xtalk_peak_per_channel_resonance(self):
        """Ristivuoto ja paluuaalto, kanavakohtaiset letkuresonanssit (R1-ero)."""
        mv = simulate(30, rpm=1200, jitter=0.02, base=-45, depth=16, xtalk=0.15, peak=5.0,
                      res_hz=(95.0, 118.0, 140.0), res_kpa=2.0, noise_mv=3.0, seed=5)
        o = self.run_fit("xtalk", mv)
        p = o["params"]
        self.near(p["rpm"], 1200, 1200 * 0.005, "rpm")
        self.near(p["jitter"], 0.02, 0.004, "jitter")  # ±20 %: 30 s:ssa ~300 sykliä
        self.near(p["depth"], 16, 1.0, "depth")
        self.near(p["xtalk"], 0.15, 0.025, "xtalk")
        self.near(p["peak"], 5.0, 1.0, "peak")
        self.near(p["res"], 2.0, 0.5, "res")
        self.near(p["noise"], 3.0, 0.3, "noise")
        for c, hz in enumerate((95.0, 118.0, 140.0)):
            self.near(o["extra"]["res_hz"][c], hz, hz * 0.03, f"res_hz {c}")
        self.assertTrue(any("R1" in w for w in o["warnings"]), o["warnings"])

    def test_3000_rpm(self):
        """Rivin 9 raja: 3000 rpm, 40 näytettä sykliin."""
        mv = simulate(20, rpm=3000, base=-30, depth=10, res_kpa=2.0, seed=7)
        o = self.run_fit("r3000", mv)
        p = o["params"]
        self.near(p["rpm"], 3000, 3000 * 0.005, "rpm")
        self.near(p["depth"], 10, 1.0, "depth")
        self.near(p["base"], -30, 0.3, "base")
        self.near(p["jitter"], 0.01, 0.002, "jitter")
        # Syklitahtinen soiminen: spektrissä vain 25 Hz:n monikerrat, yhteissovitus
        # löytää silti taajuuden. Amplitudi jätetään pois (sykli 40 ms < 3τ).
        for c in range(3):
            self.near(o["extra"]["res_hz"][c], 112, 112 * 0.03, f"res_hz {c}")
        self.assertIn("res", o["omitted"])
        self.assertIn("3 × vaimeneminen", o["omitted"]["res"])

    def test_no_resonance_omitted_with_reason(self):
        mv = simulate(20, res_kpa=0.0, seed=11)
        o = self.run_fit("nores", mv)
        self.assertNotIn("res=", o["form"])
        self.assertIn("res", o["omitted"])
        self.assertTrue(o["omitted"]["res"].startswith("SYL 1: ei resonanssipiikkiä"),
                        o["omitted"]["res"])
        self.assertTrue(all(v is None for v in o["extra"]["res_hz"]))
        self.near(o["params"]["depth"], 20, 1.0, "depth")

    def test_short_hose_resonance_above_band(self):
        """Lyhyt letku soi yli 300 Hz:n (rauta.md §4.1): oletuskaista neuvoo
        laajentamaan, --res-band 50 490 löytää piikin."""
        mv = simulate(20, res_hz=(380.0, 380.0, 380.0), seed=73)
        o = self.run_fit("lyhyt", mv)
        self.assertIn("--res-band", o["omitted"]["res"])
        o = sim_fit.fit_recording(self.dir / "lyhyt.csv", band=(50.0, 490.0))
        for c in range(3):
            self.near(o["extra"]["res_hz"][c], 380, 380 * 0.03, f"res_hz {c}")
        self.near(o["params"]["noise"], 2.0, 0.3, "noise")

    def test_clipping_h1(self):
        """Paluuaalto ylittää ilmanpaineen: GN-anturi leikkaa 2700 mV:iin (H1)."""
        mv = simulate(20, base=-12, depth=10, peak=20.0, res_kpa=1.0, seed=13)
        self.assertGreater(np.mean(mv >= 2700 - 8), 0.01)
        o = self.run_fit("clip", mv)
        for ch in o["channels"]:
            h1 = ch["h1"]
            self.assertEqual(h1["verdict"], "leikkautuu")
            self.assertGreater(h1["sat_frac"], 0.01)
            self.assertGreater(h1["sat_cycles"], 0.9)
            self.assertGreaterEqual(h1["flat_ms_p50"], sim_fit.FLAT_MS_CLIP)
            self.near(h1["ceiling_mv"], 2700, 8, "katto")
            # Paluuaalto: −12 − (shape_mean) + 20 ≈ +9 kPa, katto 0 kPa
            self.assertGreater(h1["clip_excess_kpa"], 5.0)
        self.assertTrue(any(w.startswith("H1: huiput leikkautuvat") for w in o["warnings"]))
        # Huippu jää osin piiloon: joko pois jätetty syyn kanssa tai sensuroitu arvio
        if "peak" in o["params"]:
            self.near(o["params"]["peak"], 20.0, 4.0, "peak (sensuroitu)")
        else:
            self.assertIn("H1", o["omitted"]["peak"])

    def test_peak_below_saturation(self):
        mv = simulate(20, base=-40, depth=15, peak=6.0, seed=17)
        o = self.run_fit("nosat", mv)
        for ch in o["channels"]:
            self.assertEqual(ch["h1"]["sat_frac"], 0.0)
            self.assertEqual(ch["h1"]["sat_cycles"], 0.0)
            self.assertEqual(ch["h1"]["verdict"], "ei")
        self.near(o["params"]["peak"], 6.0, 1.0, "peak")

    def test_touches_sat_without_clipping(self):
        """Huippu käy sat_mv:n (2680) yli mutta jää anturin katon alle: anturin
        nollataso 2750 mV, huippu noin −1 kPa = 2725 mV. Ei litisty."""
        mv = simulate(20, base=-12, depth=10, peak=10.4, res_kpa=0.5, v_zero_mv=2750.0, seed=61)
        o = self.run_fit("touch", mv, {"v_zero_mv": 2750.0})
        for ch in o["channels"]:
            self.assertGreater(ch["h1"]["sat_cycles"], 0.9)
            self.assertEqual(ch["h1"]["verdict"], "koskettaa")
        self.assertTrue(any("eivät litisty" in w for w in o["warnings"]), o["warnings"])

    def test_hose_off_channel(self):
        """Rivi 6: SYL 3:n letku irti -> fault3=1, base kahdesta kanavasta."""
        mv = simulate(20, hose_off=(False, False, True), err=(0.5, -0.5, 0.0), seed=19)
        o = self.run_fit("hose", mv)
        p = o["params"]
        self.assertEqual(p.get("fault3"), 1.0)
        self.near(p["base"], -55.0, 0.3, "base")
        self.near(p["err1"], 0.5, 0.15, "err1")
        self.assertIn("fault3=1", o["form"])

    def test_sensor_offsets_header_and_zero_from(self):
        """R4: anturien offsetit. Otsakkeen nollaus tai --zero-from poistaa ne."""
        off = (-1.2, -0.4, -0.9)
        err = (0.0, 1.0, -1.0)
        run = simulate(20, err=err, sensor_offset_kpa=off, seed=23)
        rest = simulate(6, engine_on=False, sensor_offset_kpa=off, seed=29)
        write_csv(self.dir / "lepo_off.csv", rest, zero_valid=0)
        # 1) laite nollattu: otsakkeessa −offset
        o = self.run_fit("hdr", run, {"offset": tuple(-v for v in off)})
        for c in range(3):
            self.near(o["params"][f"err{c + 1}"], err[c], 0.15, f"err{c + 1} otsake")
        # 2) ei nollausta: virhe näkyy ja varoitetaan
        o = self.run_fit("raw0", run, {"zero_valid": 0})
        # offsetit keskiarvostaan: −1,2 − (−0,83) = −0,37 kPa SYL 1:ssä
        self.near(o["params"]["err1"], err[0] - 1.2 + (1.2 + 0.4 + 0.9) / 3, 0.15, "err1 nollaamaton")
        self.assertTrue(any("zero_valid=0" in w for w in o["warnings"]))
        # 3) --zero-from lepotallenteesta
        o = sim_fit.fit_recording(self.dir / "raw0.csv", zero_from=self.dir / "lepo_off.csv")
        for c in range(3):
            self.near(o["params"][f"err{c + 1}"], err[c], 0.15, f"err{c + 1} zero-from")

    def test_positive_offset_invisible_at_rest(self):
        """H1/R4 (jatko.md Avoimet 3): GN-anturi leikkaa ilmanpaineessa, joten
        positiivinen offset ei näy lepotallenteessa eikä nollaus poista sitä.
        Tämä testi kirjaa rajoituksen: --zero-from ei korjaa SYL 1:tä."""
        off = (1.2, 0.0, 0.0)
        run = simulate(20, sensor_offset_kpa=off, seed=67)
        rest = simulate(6, engine_on=False, sensor_offset_kpa=off, seed=71)
        write_csv(self.dir / "lepo_pos.csv", rest, zero_valid=0)
        r = sim_fit.fit_recording(self.dir / "lepo_pos.csv")
        self.near(r["channels"][0]["offset_mv"], 0.0, 0.5, "SYL 1 lepo (leikattu)")
        self.assertTrue(any("positiivinen offset" in w for w in r["warnings"]), r["warnings"])
        write_csv(self.dir / "run_pos.csv", run, zero_valid=0)
        o = sim_fit.fit_recording(self.dir / "run_pos.csv", zero_from=self.dir / "lepo_pos.csv")
        self.near(o["params"]["err1"] - o["params"]["err2"], 1.2, 0.15, "SYL 1 − SYL 2 jää")

    def test_serial_format_and_gaps(self):
        """serial_record.py:n otsake ja pudonneet näytteet (seq-aukot)."""
        mv = simulate(20, seed=31)
        drop = list(range(5000, 5004)) + [12000]
        o = self.run_fit("serial", mv, {"style": "serial", "drop": drop})
        self.assertEqual(o["gaps_filled"], 2)
        self.near(o["params"]["rpm"], 850, 850 * 0.005, "rpm")
        self.near(o["params"]["depth"], 20, 1.0, "depth")

    def test_window(self):
        mv = np.vstack([simulate(10, engine_on=False, seed=37), simulate(15, seed=41)])
        o = self.run_fit("ikkuna", mv, start=11.0)
        self.assertEqual(o["mode"], "running")
        self.near(o["params"]["rpm"], 850, 850 * 0.005, "rpm")
        o = self.run_fit("ikkuna", mv, end=9.0)
        self.assertEqual(o["mode"], "rest")

    # --- lepotila -------------------------------------------------------

    def test_rest(self):
        # Negatiiviset: GN-anturi leikkaa ilmanpaineessa, joten positiivinen ei näy
        # (test_positive_offset_invisible_at_rest).
        off = (-0.28, -0.6, -1.0)      # kPa -> −7, −15, −25 mV
        mv = simulate(10, engine_on=False, sensor_offset_kpa=off, noise_mv=3.5, seed=43)
        o = self.run_fit("lepo", mv, {"zero_valid": 0})
        self.assertEqual(o["mode"], "rest")
        self.assertEqual(o["form"], f"engine_on=0&noise={o['params']['noise']:.2f}")
        self.near(o["params"]["noise"], 3.5, 0.3, "noise")
        for c, ch in enumerate(o["channels"]):
            self.near(ch["offset_mv"], off[c] * 25, 0.3, f"offset_mv {c}")
            self.near(ch["offset_kpa_nominal"], off[c], 0.012, f"offset_kpa {c}")
            self.near(ch["sigma_mv"], 3.5, 0.3, f"sigma {c}")
            self.assertGreater(ch["pp100_p50_mv"], 3.5 * 3)
        d = o["diffs"]["SYL 1 − SYL 2"]
        self.near(d["mean_kpa"], off[0] - off[1], 0.02, "SYL 1 − SYL 2")
        # 1,0 kPa ei ylitä anturin tarkkuutta, mutta 2,5 kPa ylittää
        self.assertFalse(any("tarkkuuden ulkopuolella" in w for w in o["warnings"]))
        mv2 = simulate(6, engine_on=False, sensor_offset_kpa=(-0.5, -0.5, -2.5), seed=47)
        o2 = self.run_fit("lepo2", mv2)
        self.assertTrue(any(w.startswith("SYL 3: offset") and "R4" in w for w in o2["warnings"]),
                        o2["warnings"])

    def test_rest_sensor_fault(self):
        mv = simulate(5, engine_on=False, seed=53)
        mv[:, 1] = 12  # rivi 7: anturi alas
        o = self.run_fit("vika", mv)
        self.assertEqual(o["params"].get("fault2"), 2.0)
        self.assertIn("fault2=2", o["form"])

    # --- C++ EngineSim (tools/replay --sim) -----------------------------

    def test_engine_sim_export_golden(self):
        """Ristiintodennus oikealla EngineSimillä ilman käännöstä: WP-8:n
        golden-nauhoitukset on kirjoitettu `tools/replay --sim`:llä, ja otsakkeen
        sim= on totuus (GET /api/sim). 8 s ≈ 60 sykliä/kanava, joten toleranssit
        ovat väljempiä kuin 30 s:n synteettisissä testeissä."""
        rec_dir = HERE.parent / "firmware" / "test" / "data" / "recordings"
        files = sorted(rec_dir.glob("sim_*.csv"))
        if not files:
            self.skipTest("golden-nauhoitukset puuttuvat (WP-8)")
        tol = {"rpm": 9.0, "jitter": 0.003, "depth": 1.0, "res": 0.75, "xtalk": 0.02,
               "peak": 0.5, "noise": 0.4, "res_decay_s": 0.006, "intake_frac": 0.02}
        for f in files:
            with self.subTest(f.name):
                o = sim_fit.fit_recording(f)
                self.assertTrue(o.get("truth"), "otsakkeesta puuttuu sim=")
                seen = set()
                for t in o["truth"]:
                    k = t["key"]
                    seen.add(k)
                    if k.startswith("taso"):
                        lim = 0.15                  # D1: kanavan keskiarvo
                    elif k.startswith("res_hz"):
                        lim = 0.03 * t["true"]
                    elif k.startswith("fault"):
                        lim = 0.0
                    else:
                        lim = tol[k]
                    self.assertLessEqual(abs(t["diff"]), lim,
                                         f"{f.name} {k}: {t['est']:.4f} vs {t['true']:.4f}")
                self.assertTrue({"rpm", "depth", "noise", "taso1"} <= seen, seen)

    # --- lomake ja CLI --------------------------------------------------

    def test_form_keys_and_ranges(self):
        params = {"engine_on": 1, "rpm": 851.26, "jitter": 0.0123456, "base": -120.0,
                  "err1": -0.001, "depth": 19.876, "xtalk": 0.0, "noise": 2.0}
        form, clamped = sim_fit.form_string(params)
        self.assertEqual(form, "engine_on=1&rpm=851&jitter=0.0123&base=-100.00&err1=0.00"
                               "&depth=19.88&xtalk=0.000&noise=2.00")
        self.assertEqual(len(clamped), 1)
        for kv in form.split("&"):
            k, v = kv.split("=")
            self.assertIn(k, sim_fit.SIM_KEYS)
            lo, hi = sim_fit.SIM_KEYS[k]
            self.assertTrue(lo <= float(v) <= hi)

    def test_cli_json(self):
        p = self.dir / "cli.csv"
        write_csv(p, simulate(12, seed=59))
        r = subprocess.run([sys.executable, str(HERE / "sim_fit.py"), str(p), "--json"],
                           capture_output=True, text=True, encoding="utf-8", timeout=300)
        self.assertEqual(r.returncode, 0, r.stderr)
        o = json.loads(r.stdout)
        self.assertEqual(o["mode"], "running")
        self.assertTrue(o["form"].startswith("engine_on=1&rpm="))
        r = subprocess.run([sys.executable, str(HERE / "sim_fit.py"), str(p)],
                           capture_output=True, text=True, encoding="utf-8", timeout=300)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("POST /api/sim:", r.stdout)
        r = subprocess.run([sys.executable, str(HERE / "sim_fit.py"), str(p), "--form"],
                           capture_output=True, text=True, encoding="utf-8", timeout=300)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.strip(), o["form"])


if __name__ == "__main__":
    unittest.main()
