#!/usr/bin/env python3
"""Suckboxx PC-mock-palvelin (WP-5, docs/firmware.md §5).

Sama HTTP-rajapinta kuin laitteella, jotta web-UI:n (web/index.html) voi
kehittää ja testata PC:llä ilman ESP32:ta. Pelkkä Pythonin standardikirjasto
(D9: SSE on mockattavissa ilman riippuvuuksia).

    python3 tools/mock_server.py                 # http://localhost:8000
    python3 tools/mock_server.py --port 8080 --sensor-err
    python3 tools/mock_server.py --check-config  # rekisteri vs. config.h/types.h
    python3 tools/mock_server.py --check-snapshot  # snapshotin muoto vs. laite (test_snapshot)

Mock ajaa pienen moottorisimulaation 1 kHz:llä (sama malli kuin
src/core/engine_sim.h), kevyen DSP:n (syklin keskiarvo D1, rpm, vikamonitorit)
ja käytösmatriisin suoran portin (src/core/status.h), joten tilat, tekstit ja
liput syntyvät samoista syistä kuin laitteella - eivät käsin asetettuina.
Laitteen tavoin:
  - nollaus (POST /api/cal/zero) onnistuu vain ilmanpaineessa: engine_on=0,
    tai automaattisesti kun "ei signaalia" on kestänyt off_hold_s (rivi 1)
  - vahvistuskalibrointi mittaa 2 s ja vaatii tasaisen alipaineen kaikissa
    kanavissa (T-haara). Simulaattorissa: depth=0, res=0, err1..3=0.
  - letkuvika (0,5 s) ja saturaatiovaroitus (1,2 s) pitoajoilla, ja kaikkien
    kanavien yhteinen alipaineen muutos (kaasu) on "odota" (WP-2c)
  - snapshotissa ruuvineuvo rakenteisena (act) ja kanavaluokitus (ch[i].cond)
  - lähteen (source) vaihto käynnistää "laitteen" uudelleen: SSE katkeaa,
    nollaus unohtuu (D11). ADC/HX710B-lähde ilman antureita = kelluvat pinnit
    ~10 mV -> anturivika kaikissa kanavissa.
Mockin omat lisät (eivät ole laitteella): --sensor-err (anturien R4-virheet,
jotta kalibroinnin vaikutus näkyy) ja --calibrated (nollaus + vahvistus valmiina).
"""

import argparse
import json
import math
import os
import queue
import random
import re
import sys
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qsl, urlsplit

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INDEX_HTML = os.path.join(ROOT, "web", "index.html")
CONFIG_H = os.path.join(ROOT, "firmware", "src", "core", "config.h")
TYPES_H = os.path.join(ROOT, "firmware", "src", "core", "types.h")

KCH = 3                 # types.h kChannels
FS = 1000               # kSampleRateHz
DT = 1.0 / FS
DECIM = 16              # wave.s: 16 näytteen lohkokeskiarvo -> 62,5 Hz (§5)
WAVE_FS = FS / DECIM
WAVE_MAX_ROWS = 16      # enintään 16 riviä per viesti (§5)
RING_CAP = 8192         # ESP32:n rengas ≈ 8,2 s (§4.5)
TICK_SAMPLES = 50       # status ~20 Hz
GAIN_CAPTURE_S = 2.0    # §5: /api/cal/gain mittaa 2 s
REBOOT_S = 1.5          # lähteen vaihdon "uudelleenkäynnistys"

# ---------------------------------------------------------------------------
# Konfiguraatiorekisteri: config.h:n SBX_CONFIG_FIELDS samassa järjestyksessä,
# oletukset types.h:sta. --check-config vertaa tätä lähdekoodiin, jotta mock ei
# jää jälkeen kun rekisteriin lisätään kenttä.
# (key, type, min, max, default, group, unit, label)
# ---------------------------------------------------------------------------
G_SYNC, G_SIG, G_SENS, G_SYS = "Synkronointi", "Signaali", "Anturi", "Järjestelmä"
CONFIG_FIELDS = [
    ("master", "u8", 0, 2, 0, G_SYNC, "", "Master-sylinteri (0 = SYL 1, 1 = SYL 2, 2 = SYL 3)"),
    ("tol_green_kpa", "float", 0.1, 10.0, 1.5, G_SYNC, "kPa", "Synkronoitu, kun ero enintään"),
    ("tol_yellow_kpa", "float", 0.2, 20.0, 3.0, G_SYNC, "kPa", "Lähellä, kun ero enintään"),
    ("off_kpa", "float", 0.5, 20.0, 5.0, G_SYNC, "kPa", "Ei signaalia: kaikki kanavat alle"),
    ("off_hold_s", "float", 0.5, 60.0, 3.0, G_SYNC, "s", "Ei signaalia: kesto ennen nollausta"),
    ("hose_off_kpa", "float", 0.2, 20.0, 2.0, G_SYNC, "kPa", "Letkuvika: kanavan taso alle"),
    ("unstable_cv", "float", 0.01, 1.0, 0.15, G_SYNC, "", "Epävakaa käynti: rpm-hajonta yli (osuus)"),
    ("stable_hold_s", "float", 0.0, 30.0, 3.0, G_SYNC, "s", "Vakaata käyntiä ennen arviota"),
    ("sync_rpm_min", "float", 100.0, 3000.0, 500.0, G_SYNC, "rpm", "Arvio alkaen kierrosluvusta"),
    ("sync_rpm_max", "float", 500.0, 12000.0, 3000.0, G_SYNC, "rpm", "Arvio enintään kierroslukuun"),
    ("drift_rpm", "float", 5.0, 1000.0, 50.0, G_SYNC, "rpm", "Kierrosluku ajautuu: muutos yli"),
    ("drift_window_s", "float", 0.5, 30.0, 2.0, G_SYNC, "s", "Kierrosluku ajautuu: aikaikkuna"),
    ("auto_zero", "bool", 0, 1, True, G_SYNC, "", "Automaattinen nollaus moottorin ollessa sammuksissa"),
    ("lp_cutoff_hz", "float", 5.0, 250.0, 40.0, G_SIG, "Hz", "Alipäästösuodattimen rajataajuus"),
    ("level_tau_s", "float", 0.05, 10.0, 0.5, G_SIG, "s", "Tasolukeman aikavakio"),
    ("hyst_frac", "float", 0.05, 0.9, 0.3, G_SIG, "", "Syklirajan hystereesi (osuus huippu–huipusta)"),
    ("hyst_min_kpa", "float", 0.05, 10.0, 0.3, G_SIG, "kPa", "Syklirajan hystereesi vähintään"),
    ("avg_cycles", "u8", 1, 32, 8, G_SIG, "", "Keskiarvoistettavat syklit"),
    ("rpm_min", "float", 60.0, 2000.0, 300.0, G_SIG, "rpm", "Syklintunnistus: alin kierrosluku"),
    ("rpm_max", "float", 1000.0, 12000.0, 6000.0, G_SIG, "rpm", "Syklintunnistus: ylin kierrosluku"),
    ("pulse_timeout_s", "float", 0.2, 10.0, 1.0, G_SIG, "s", "Pulssi kadonnut, kun ei sykliä"),
    ("rpm_hist", "u8", 2, 64, 16, G_SIG, "", "Jaksoja kierroslukuhajontaan"),
    ("v_zero_mv", "float", 1000.0, 3300.0, 2700.0, G_SENS, "mV", "Anturin jännite 0 kPa:ssa"),
    ("mv_per_kpa", "float", 5.0, 50.0, 25.0, G_SENS, "mV/kPa", "Anturin herkkyys"),
    ("fault_lo_mv", "u16", 50, 500, 100, G_SENS, "mV", "Anturivika: jännite alle"),
    ("fault_hi_mv", "u16", 2500, 3250, 3150, G_SENS, "mV", "Anturivika: jännite yli"),
    ("fault_hold_s", "float", 0.1, 10.0, 1.0, G_SENS, "s", "Anturivika: kesto ennen hälytystä"),
    ("sat_mv", "u16", 1000, 3300, 2680, G_SENS, "mV", "Saturaatiovaroitus: jännite vähintään"),
    ("rail_comp", "bool", 0, 1, False, G_SENS, "", "Kiskojännitteen korjaus (R6)"),
    ("rail_nominal_mv", "float", 2500.0, 5500.0, 3300.0, G_SENS, "mV", "Anturikiskon nimellisjännite"),
    ("source", "u8", 0, 2, 0, G_SYS, "", "Signaalilähde (0 = simulaattori, 1 = ADC, 2 = HX710B)"),
    ("push_rate_hz", "u8", 4, 30, 15, G_SYS, "Hz", "Näytön päivitystaajuus"),
]
FIELD_INDEX = {f[0]: i for i, f in enumerate(CONFIG_FIELDS)}
SOURCE_NAMES = {0: "sim", 1: "adc", 2: "hx710b"}

SET_RESULT_TEXT = {
    "UnknownKey": "Tuntematon asetus",
    "OutOfRange": "Arvo sallitun alueen ulkopuolella",
    "Invalid": "Virheellinen arvo – anna luku (desimaalierotin pilkku tai piste)",
}

CAL_TEXT = {  # calibration.h calResultText
    "Ok": "Kalibrointi onnistui",
    "NotAtmosphere": "Nollaus hylätty: kanava ei näytä ilmanpainetta. Sammuta moottori ja "
                     "päästä alipaine letkuista; jos se ei auta, tarkista v_zero_mv ja mv_per_kpa",
    "NotStable": "Paine vaihteli mittauksen aikana. Tarkista ettei pumppu tai T-haara vuoda ja "
                 "pidä alipaine tasaisena",
    "TooSmall": "Alipaine liian pieni. Pumppaa vähintään 15 kPa (mieluiten 40–60 kPa) ja "
                "yritä uudelleen",
    "Mismatch": "Kanavat näkevät eri paineen. Tarkista että kaikki kolme letkua ovat samassa "
                "T-haarassa eikä mikään vuoda",
    "GainOutOfRange": "Anturin herkkyys poikkeaa yli 15 %. Tarkista anturityyppi (GN/GPN, "
                      "3,3 V / 5 V) ja anturiasetukset v_zero_mv ja mv_per_kpa",
    "NoZero": "Nollaus puuttuu. Päästä alipaine pois (letkut ilmanpaineeseen), tee vaihe 1 "
              "Nollaa ja toista sitten vahvistuskalibrointi",
}


def default_config():
    return {f[0]: f[4] for f in CONFIG_FIELDS}


def _json_float(v):
    """config.h jsonNumber: 6 merkitsevää numeroa, loppunollat pois ("2700", "0.15")."""
    a = abs(v)
    digits = 1
    while a >= 10.0 and digits < 6:
        a /= 10.0
        digits += 1
    s = "%.*f" % (6 - digits, v)
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    if s in ("-0", ""):
        s = "0"
    return s


def config_to_json(cfg):
    """configToJson: {"v":1,"fields":[{key,label,unit,group,type,min,max,def,value}]}.

    Numerot muotoillaan kuten laitteen JsonWriter: u8/u16 kokonaislukuina,
    float trimmattuna, bool-kentän min/max lukuina 0/1 ja def/value true/false."""
    def num(t, v):
        if t == "bool":
            return "true" if v else "false"
        if t in ("u8", "u16"):
            return str(int(v))
        return _json_float(float(v))

    parts = []
    for key, t, mn, mx, dv, group, unit, label in CONFIG_FIELDS:
        rt = "u8" if t == "bool" else t
        parts.append(
            '{"key":%s,"label":%s,"unit":%s,"group":%s,"type":"%s","min":%s,"max":%s,'
            '"def":%s,"value":%s}' % (
                json.dumps(key), json.dumps(label, ensure_ascii=False),
                json.dumps(unit, ensure_ascii=False), json.dumps(group, ensure_ascii=False),
                t, num(rt, mn), num(rt, mx), num(t, dv), num(t, cfg[key])))
    return '{"v":1,"fields":[' + ",".join(parts) + "]}"


_NUM_CHARS = re.compile(r"^[0-9.+\-eE]+$")


def config_set_text(cfg, key, text):
    """configSetText-portti. Palauttaa (tulos, None) tai (None, virheavain)."""
    i = FIELD_INDEX.get(key)
    if i is None:
        return "UnknownKey"
    _, t, mn, mx = CONFIG_FIELDS[i][:4]
    s = (text or "").strip(" \t\r\n")
    if not s or len(s) >= 32:
        return "Invalid"
    if t == "bool":
        if s.lower() in ("true", "on"):
            s = "1"
        elif s.lower() in ("false", "off"):
            s = "0"
    s = s.replace(",", ".")
    if not _NUM_CHARS.match(s):
        return "Invalid"
    try:
        v = float(s)
    except ValueError:
        return "Invalid"
    if not math.isfinite(v):
        return "Invalid"
    if t != "float" and math.floor(v) != v:
        return "Invalid"  # "15,5" sykliksi on kirjoitusvirhe, ei pyöristetä hiljaa
    if v < mn or v > mx:
        return "OutOfRange"
    if t == "bool":
        cfg[key] = v != 0.0
    elif t in ("u8", "u16"):
        cfg[key] = int(v)
    else:
        cfg[key] = v
    return "Ok"


def _fi(v):
    s = "%.3f" % v
    s = s.rstrip("0").rstrip(".") if "." in s else s
    return s.replace(".", ",")


def config_validate(c):
    """configValidate-portti: palauttaa None tai ensimmäisen suomenkielisen virheen."""
    if not c["tol_green_kpa"] < c["tol_yellow_kpa"]:
        return ("Vihreän rajan (tol_green_kpa) on oltava pienempi kuin keltaisen rajan "
                "(tol_yellow_kpa)")
    if not c["rpm_min"] < c["rpm_max"]:
        return ("Syklintunnistuksen alimman kierrosluvun (rpm_min) on oltava pienempi kuin "
                "ylimmän (rpm_max)")
    if not c["sync_rpm_min"] < c["sync_rpm_max"]:
        return "Arvion alarajan (sync_rpm_min) on oltava pienempi kuin ylärajan (sync_rpm_max)"
    if not (c["rpm_min"] <= c["sync_rpm_min"] and c["sync_rpm_max"] <= c["rpm_max"]):
        return ("Arvion kierroslukuikkunan (sync_rpm_min…sync_rpm_max) on oltava "
                "syklintunnistuksen alueen (rpm_min…rpm_max) sisällä")
    max_period = 120.0 / c["rpm_min"]
    if not c["pulse_timeout_s"] > max_period:
        return ("Pulssin aikakatkaisun (pulse_timeout_s) on oltava pidempi kuin yksi sykli "
                "alimmalla kierrosluvulla: 120 / rpm_min = %s s" % _fi(max_period))
    if not (c["fault_lo_mv"] < c["sat_mv"] < c["fault_hi_mv"]):
        return ("Jänniterajojen järjestyksen on oltava fault_lo_mv < sat_mv < fault_hi_mv "
                "(nyt %d / %d / %d mV)" % (c["fault_lo_mv"], c["sat_mv"], c["fault_hi_mv"]))
    if not (c["fault_lo_mv"] < c["v_zero_mv"] < c["fault_hi_mv"]):
        return ("Anturin nollapisteen (v_zero_mv) on oltava anturivikarajojen "
                "(fault_lo_mv…fault_hi_mv) välissä – muuten ilmanpaine näkyisi anturivikana")
    if not c["hose_off_kpa"] <= c["off_kpa"]:
        return "Letkuvian raja (hose_off_kpa) saa olla enintään ei signaalia -raja (off_kpa)"
    return None


# ---------------------------------------------------------------------------
# Simulaattorin parametrit (§5 sim-avaimet -> engine_sim.h SimParams)
# ---------------------------------------------------------------------------
SIM_KEYS = [  # (avain, oletus, min, max, kokonaisluku)
    ("engine_on", 1, 0, 1, True),
    ("rpm", 850.0, 60.0, 12000.0, False),     # EngineSim::kRpmMin/kRpmMax
    ("jitter", 0.01, 0.0, 0.4, False),        # kJitterMax
    ("drift", 0.0, -2000.0, 2000.0, False),   # rpm/s
    ("base", -55.0, -100.0, 0.0, False),
    ("err1", 0.0, -30.0, 30.0, False),
    ("err2", 0.0, -30.0, 30.0, False),
    ("err3", 0.0, -30.0, 30.0, False),
    ("depth", 20.0, 0.0, 60.0, False),
    ("res", 3.0, 0.0, 50.0, False),           # res_kpa (kResKpaMax)
    ("xtalk", 0.0, 0.0, 1.0, False),
    ("peak", 0.0, 0.0, 60.0, False),
    ("noise", 2.0, 0.0, 1000.0, False),       # kNoiseMvMax
    ("fault1", 0, 0, 3, True),
    ("fault2", 0, 0, 3, True),
    ("fault3", 0, 0, 3, True),
]
SIM_INDEX = {k[0]: k for k in SIM_KEYS}


def default_sim():
    return {k: d for k, d, _, _, _ in SIM_KEYS}


def sim_apply(sim, pairs):
    """Kaikki tai ei mitään: virheellinen avain tai arvo hylkää koko pyynnön."""
    new = dict(sim)
    for k, text in pairs:
        spec = SIM_INDEX.get(k)
        if spec is None:
            return "Tuntematon simulaattoriparametri: %s" % k
        try:
            v = float(text.replace(",", "."))
        except ValueError:
            return "Virheellinen arvo: %s=%s" % (k, text)
        if not math.isfinite(v):
            return "Virheellinen arvo: %s=%s" % (k, text)
        _, _, mn, mx, is_int = spec
        if is_int and math.floor(v) != v:
            return "Virheellinen arvo: %s=%s" % (k, text)
        v = min(max(v, mn), mx)  # EngineSim rajaa itse, ei hylkää
        new[k] = int(v) if is_int else v
    sim.clear()
    sim.update(new)
    return None


class Engine:
    """engine_sim.h:n malli Pythonina (float64, ei bittitarkka).

    Kampivaihe 0..1 = 720°, sylinterin c imu alkaa vaiheessa c/3. Pulssi on
    sin²-muotoinen, ja muodon keskiarvo syklin yli vähennetään, joten syklin
    keskiarvo on tasan base + err (D1:n totuus). Jitter arvotaan jokaiselle
    240°:n sytytysvälille, resonanssi on vaimeneva värähtely imun alussa."""

    SEG_START = (0.0, 1.0 / 3.0, 2.0 / 3.0)
    SEG_END = (1.0 / 3.0, 2.0 / 3.0, 1.0)
    RES_HZ = 112.0   # docs/rauta.md §4.1 (ei sim-avainta)
    RES_DECAY_S = 0.02

    def __init__(self, seed, sensor_err):
        self.rng = random.Random(seed)
        self.p = default_sim()
        if sensor_err:
            # R4: anturin oma virhe ±2 kPa / muutama %, jotta nollauksen ja
            # vahvistuskalibroinnin vaikutus näkyy UI:ssa.
            # Valittu niin, että kalibroimaton spread on ~2,8 kPa (keltainen),
            # nollauksen jälkeen ~1,9 kPa (gain-ero jää) ja vahvistuksen jälkeen ~0.
            self.offset = [0.8, -0.6, 0.2]
            self.gain_err = [0.0, 0.025, -0.01]
        else:
            self.offset = [0.0] * KCH   # SimParams-oletus
            self.gain_err = [0.0] * KCH
        rot = 2.0 * math.pi * self.RES_HZ * DT
        r = math.exp(-DT / self.RES_DECAY_S)
        self.rot = complex(r * math.cos(rot), r * math.sin(rot))
        self.reset()

    def reset(self):
        self.phase = 0.0
        self.seg = 0
        self.anchor = self._clamp_rpm(self.p["rpm"])
        self.n = 0
        self.res = [0j] * KCH
        self.seg_factor = self._draw_factor()
        self._excite(0)

    def configure(self, p, rpm_posted=False):
        rpm_now = self.rpm()
        # EngineSim ankkuroi kierrosluvun uudelleen vain, jos rpm muuttuu. UI:ssa
        # driftin jälkeen "rpm = 850" (sama arvo) on kuitenkin selvä pyyntö palata
        # 850:een, joten POSTissa mukana oleva rpm ankkuroi aina.
        rpm_changed = rpm_posted or p["rpm"] != self.p["rpm"]
        drift_changed = p["drift"] != self.p["drift"]
        self.p = dict(p)
        if rpm_changed:
            self.anchor = self._clamp_rpm(p["rpm"])
            self.n = 0
        elif drift_changed:
            # engine_sim.h configure(): uusi asetusarvo = kierrosluku, jossa ollaan nyt
            # (p_.rpm = rpm_now). Device.api_sim_set vie saman arvon dev.sim['rpm']:ään.
            self.anchor = rpm_now
            self.p["rpm"] = rpm_now
            self.n = 0

    @staticmethod
    def _clamp_rpm(r):
        return min(max(r, 60.0), 12000.0)

    def rpm(self):
        return self._clamp_rpm(self.anchor + self.p["drift"] * self.n * DT)

    def _gauss(self):
        u = self.rng.random
        return (u() + u() + u() + u() - 2.0) * 1.7320508  # Irwin-Hall, rajattu ±3,46σ

    def _draw_factor(self):
        sigma = math.sqrt(3.0) * min(max(self.p["jitter"], 0.0), 0.4)
        f = math.exp(sigma * self._gauss() - 0.5 * sigma * sigma)
        return min(max(f, 0.1), 10.0)

    def _excite(self, c):
        self.res[c] += self.p["res"]

    def _shape(self, psi):
        w = 0.25
        wb = 0.5 * w
        depth, peak = self.p["depth"], self.p["peak"]
        mean = -depth * w * 0.5 + peak * wb * 0.5
        v = 0.0
        if psi < w:
            s = math.sin(math.pi * psi / w)
            v = -depth * s * s
        elif peak > 0.0 and psi < w + wb:
            s = math.sin(math.pi * (psi - w) / wb)
            v = peak * s * s
        return v - mean

    def next(self, sensor, source):
        """Yksi 1 kHz näyte: (mv[3], imun alut [kanavat]) - jälkimmäinen on DSP:n syklioraakkeli."""
        p = self.p
        on = bool(p["engine_on"])
        shape = [0.0] * KCH
        if on:
            for c in range(KCH):
                psi = self.phase - self.SEG_START[c]
                if psi < 0.0:
                    psi += 1.0
                shape[c] = self._shape(psi)
        ssum = sum(shape)
        vz, k = sensor["v_zero_mv"], sensor["mv_per_kpa"]
        lo_mv, hi_mv = vz - 100.0 * k, vz   # GN: −100…0 kPa, leikkautuu 0 kPa:ssa (D4/H1)
        mv = [0] * KCH
        for c in range(KCH):
            g = self._gauss()
            fault = int(p["fault%d" % (c + 1)])
            if source != 0:
                # ADC/HX710B ilman antureita: kelluva pinni lähellä nollaa (rivi 7).
                v = min(max(10.0 + p["noise"] * g, 0.0), 30.0)
            elif fault == 2:
                v = min(max(10.0 + p["noise"] * g, 0.0), 30.0)
            elif fault == 3:
                v = min(max(3290.0 + p["noise"] * g, 3270.0), 3300.0)
            else:
                kpa = 0.0
                if on and fault != 1:
                    kpa = (p["base"] + p["err%d" % (c + 1)] + shape[c] +
                           p["xtalk"] * (ssum - shape[c]) + self.res[c].imag)
                meas = (1.0 + self.gain_err[c]) * kpa + self.offset[c]
                v = min(max(vz + k * meas, lo_mv), hi_mv)
                v = min(max(v + p["noise"] * g, 0.0), 3300.0)
            mv[c] = int(v + 0.5)
        for c in range(KCH):
            z = self.res[c] * self.rot
            self.res[c] = z if abs(z) > 1e-6 else 0j
        starts = []
        if on:
            left = DT * self.rpm() / 120.0
            for _ in range(8):
                end = self.SEG_END[self.seg]
                step = left / self.seg_factor
                if self.phase + step < end:
                    self.phase += step
                    break
                left -= (end - self.phase) * self.seg_factor
                self.seg = (self.seg + 1) % KCH
                self.phase = self.SEG_START[self.seg]
                self.seg_factor = self._draw_factor()
                self._excite(self.seg)
                starts.append(self.seg)
            self.n += 1
        return mv, starts


class ChanDsp:
    """Kevyt DSP yhdelle kanavalle. Kaikki tallennetaan nimellis-kPa:na ja
    muunnetaan kalibroiduksi luettaessa (lineaarinen), jolloin kalibroinnin
    muutos näkyy heti eikä vasta EMA:n asetuttua.

    Syklirajat tulevat simulaattorilta (oraakkeli) - laitteen dsp.h tunnistaa
    ne hystereesillä suodatetusta signaalista. Sykli hyväksytään vain, jos
    suodatettu huippu–huippu ylittää hystereesin selvästi, joten irti oleva
    letku tai anturivika ei tuota syklejä."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.level = None
        self.lp = None
        self.acc_sum = 0.0
        self.acc_n = 0
        self.acc_min = math.inf
        self.acc_max = -math.inf
        self.started = False
        self.means, self.mins, self.p2ps, self.periods = deque(), deque(), deque(), deque()
        self.rpm_hist = deque()
        self.cycles = 0
        self.last_cycle_n = None
        self.fault_since = None
        self.sat_last = None
        self.last_nom = 0.0
        self.last_mv = 0

    def push(self, n, mv, nom, cfg):
        self.last_mv = mv
        self.last_nom = nom
        a_lvl = min(1.0, DT / cfg["level_tau_s"])
        self.level = nom if self.level is None else self.level + a_lvl * (nom - self.level)
        a_lp = 1.0 - math.exp(-2.0 * math.pi * cfg["lp_cutoff_hz"] * DT)
        self.lp = nom if self.lp is None else self.lp + a_lp * (nom - self.lp)
        self.acc_sum += nom
        self.acc_n += 1
        self.acc_min = min(self.acc_min, self.lp)
        self.acc_max = max(self.acc_max, self.lp)
        # Rivi 7: jännite ulkona rajoista yhtäjaksoisesti fault_hold_s.
        if mv < cfg["fault_lo_mv"] or mv > cfg["fault_hi_mv"]:
            if self.fault_since is None:
                self.fault_since = n
        else:
            self.fault_since = None
        if mv >= cfg["sat_mv"]:
            self.sat_last = n
        # Pulssi kadonnut: vanhat syklit eivät saa sekoittua uusiin (letku takaisin).
        if self.last_cycle_n is not None and n - self.last_cycle_n > cfg["pulse_timeout_s"] * FS:
            self.last_cycle_n = None
            for d in (self.means, self.mins, self.p2ps, self.periods, self.rpm_hist):
                d.clear()

    def cycle_boundary(self, n, cfg):
        if self.started and self.acc_n > 0:
            period = self.acc_n * DT
            p2p = self.acc_max - self.acc_min
            gate = max(1.0, 4.0 * cfg["hyst_min_kpa"])
            rpm = 120.0 / period
            if p2p >= gate and cfg["rpm_min"] <= rpm <= cfg["rpm_max"]:
                navg = int(cfg["avg_cycles"])
                for d, v in ((self.means, self.acc_sum / self.acc_n), (self.mins, self.acc_min),
                             (self.p2ps, p2p), (self.periods, period)):
                    d.append(v)
                    while len(d) > navg:
                        d.popleft()
                self.rpm_hist.append(rpm)
                while len(self.rpm_hist) > int(cfg["rpm_hist"]):
                    self.rpm_hist.popleft()
                self.cycles += 1
                self.last_cycle_n = n
        self.started = True
        self.acc_sum, self.acc_n = 0.0, 0
        self.acc_min, self.acc_max = math.inf, -math.inf

    def state(self, n, cfg, gain, offset):
        has_pulse = self.last_cycle_n is not None and len(self.means) > 0
        cal = lambda v: gain * v + offset  # noqa: E731
        st = {
            "level": cal(self.level if self.level is not None else self.last_nom),
            "pulse": has_pulse,
            "mean": cal(sum(self.means) / len(self.means)) if has_pulse else None,
            "min": cal(sum(self.mins) / len(self.mins)) if has_pulse else None,
            "p2p": gain * sum(self.p2ps) / len(self.p2ps) if has_pulse else None,
            "rpm": 120.0 / (sum(self.periods) / len(self.periods)) if has_pulse else None,
            "mv": self.last_mv,
            "fault": self.fault_since is not None and n - self.fault_since >= cfg["fault_hold_s"] * FS,
            "sat": self.sat_last is not None and n - self.sat_last < FS,
        }
        return st


def rpm_estimate(chs, dsps):
    """RpmEstimate: kanavien rpm:ien mediaani, hajonta rpm_hist jakson yli."""
    cand = [(c["rpm"], i) for i, c in enumerate(chs) if c["pulse"] and c["rpm"] is not None]
    if not cand:
        return {"valid": False, "rpm": 0.0, "sd": 0.0, "cv": 0.0}
    cand.sort()
    rpm, idx = cand[len(cand) // 2]
    hist = list(dsps[idx].rpm_hist)
    if len(hist) < 4:  # "jaksoja riittävästi" (§4.2): hajonta ei kerro 1-3 jaksosta mitään
        return {"valid": False, "rpm": rpm, "sd": 0.0, "cv": 0.0}
    mu = sum(hist) / len(hist)
    sd = math.sqrt(sum((h - mu) ** 2 for h in hist) / (len(hist) - 1))
    return {"valid": True, "rpm": rpm, "sd": sd, "cv": sd / rpm if rpm > 0 else 0.0}


# ---------------------------------------------------------------------------
# Käytösmatriisi: src/core/status.h StatusMachine::update() rivi riviltä.
# Tekstit ovat samat kuin laitteella, jotta UI:n asettelu testautuu oikeilla
# pituuksilla.
# ---------------------------------------------------------------------------
def fmt1(v):
    if v is None or not math.isfinite(v):
        return "?"
    v = min(max(v, -999.9), 999.9)
    s = ("%.1f" % v).replace(".", ",")
    return "0,0" if s == "-0,0" else s


def rpm_int(r):
    if r is None or not math.isfinite(r) or r < 0:
        return 0
    return min(int(r + 0.5), 99999)


def channel_list(idx):
    names = ["SYL %d" % (i + 1) for i in idx]
    if len(names) <= 1:
        return "".join(names)
    return ", ".join(names[:-1]) + " ja " + names[-1]


def all_beyond(v, k):
    """status_detail::allBeyond: kaikki arvot samaan suuntaan yli rajan k."""
    return all(x > k for x in v) or all(x < -k for x in v)


# Snapshotin state- ja ch[i].cond-arvot (status.h syncStateKey, snapshot.h channelCondKey).
# --check-snapshot vertaa näitä lähdekoodiin.
STATE_KEYS = ("no_signal", "unstable", "rpm_too_high", "synced", "near", "out_of_sync",
              "fault_sensor", "fault_hose", "fault_master")
# Sisäinen luokitus (status.h Cond) -> ch[i].cond (types.h ChannelCond). "ok" voi tarkentua
# "sat":ksi, ks. StatusMachine.update().
COND_KEY = {"ok": "ok", "idle": "idle", "lost": "no_pulse", "hose": "hose", "blocked": "blocked",
            "sensor": "sensor"}
COND_KEYS = ("ok", "sat", "idle", "no_pulse", "hose", "blocked", "sensor")


class StatusMachine:
    AUTO_ZERO_BAND = 0.3
    HOSE_HOLD_S = 0.5        # kHoseHoldS: rivi 6 vasta näin pitkän pulssittomuuden jälkeen
    SAT_HOLD_S = 1.2         # kSatHoldS: rivi 10 vasta kun leikkautuminen on jatkunut näin kauan
    COMMON_MOVE_KPA = 1.0    # kCommonMoveKpa: rivi 5, yhteismuotoinen alipaineen muutos
    COMMON_WINDOW_S = 0.3    # kCommonWindowS
    DRIFT_SLOTS = 32
    DRIFT_CAP = 48

    def __init__(self, cfg):
        self.configure(cfg)

    def configure(self, cfg):
        self.p = dict(cfg)
        self.master = int(cfg["master"]) if int(cfg["master"]) < KCH else 0
        self.reset()

    def reset(self):
        self.last_t = None
        self.stable_since = None
        self.sat_since = None
        self.cm_count = 0
        self.cm_t = [0.0, 0.0]
        self.cm_mean = [[0.0] * KCH, [0.0] * KCH]
        self.lost_since = [None] * KCH
        self.zero_since = None
        self.zero_lo = [0.0] * KCH
        self.zero_hi = [0.0] * KCH
        self.zero_armed = True
        self.drift = deque()

    def _update_drift(self, e, now):
        win = self.p["drift_window_s"]
        if not e["valid"] or not math.isfinite(e["rpm"]) or not win > 0:
            self.drift.clear()
            return False
        target = now - win
        drift = False
        newer = None
        for t, r in reversed(self.drift):
            if t <= target:
                then = r
                if newer is not None and newer[0] - t > 0:
                    then = r + (newer[1] - r) * ((target - t) / (newer[0] - t))
                drift = abs(e["rpm"] - then) > self.p["drift_rpm"]
                break
            newer = (t, r)
        if not self.drift or now - self.drift[-1][0] >= win / self.DRIFT_SLOTS:
            self.drift.append((now, e["rpm"]))
            while len(self.drift) > self.DRIFT_CAP:
                self.drift.popleft()
        return drift

    def update(self, chs, e, zero_valid, gain_valid, now):
        p = self.p
        r = {"state": "no_signal", "fault_ch": -1, "d": [0.0] * KCH, "spread": 0.0, "worst": -1,
             "sat": False, "uncal_zero": not zero_valid, "uncal_gain": not gain_valid,
             "drift": False, "auto_zero": False, "head": "", "advice": "", "act": None,
             "cond": ["idle"] * KCH}
        if self.last_t is not None and now < self.last_t:
            self.reset()
        self.last_t = now

        pulse = [c["pulse"] and c["mean"] is not None for c in chs]
        pulsing = sum(pulse)
        cond = [None] * KCH
        for i, c in enumerate(chs):
            others = pulsing - (1 if pulse[i] else 0) > 0
            lost = not c["fault"] and not pulse[i] and others
            if not lost:
                self.lost_since[i] = None
            elif self.lost_since[i] is None:
                self.lost_since[i] = now
            if c["fault"]:
                cond[i] = "sensor"
            elif pulse[i]:
                cond[i] = "ok"
            elif not others:
                cond[i] = "idle"
            elif abs(c["level"]) < p["hose_off_kpa"] and now - self.lost_since[i] >= self.HOSE_HOLD_S:
                # Rivi 6 pitoajan jälkeen: käynnistyksessä ja sammutuksessa kanavat saavat ja
                # menettävät pulssin eri sykleillä (WP-2c).
                cond[i] = "hose"
            elif now - self.lost_since[i] >= p["off_hold_s"]:
                cond[i] = "blocked"
            else:
                cond[i] = "lost"

        # Rivi 10 vain pulssin aikana ja vasta SAT_HOLD_S:n jälkeen (DSP:n saturated- ja
        # has_pulse-muistit ovat 1 s, joten käynnistys ja sammutus eivät välähdä).
        if not any(c["sat"] and pulse[i] for i, c in enumerate(chs)):
            self.sat_since = None
        elif self.sat_since is None:
            self.sat_since = now
        r["sat"] = self.sat_since is not None and now - self.sat_since >= self.SAT_HOLD_S
        r["drift"] = self._update_drift(e, now)

        m = self.master
        if cond[m] == "ok":
            ref = chs[m]["mean"]
            lo = hi = 0.0
            worst_abs = -1.0
            for i in range(KCH):
                if cond[i] != "ok" or i == m:
                    continue
                d = ref - chs[i]["mean"]  # positiivinen = imee enemmän kuin master
                r["d"][i] = d
                lo, hi = min(lo, d), max(hi, d)
                if abs(d) > worst_abs:
                    worst_abs, r["worst"] = abs(d), i
            r["spread"] = hi - lo

        sensor = [i for i in range(KCH) if cond[i] == "sensor"]
        hose = [i for i in range(KCH) if cond[i] == "hose"]
        blocked = [i for i in range(KCH) if cond[i] == "blocked"]
        hoseish = [i for i in range(KCH) if cond[i] in ("hose", "blocked")]
        lost = [i for i in range(KCH) if cond[i] == "lost"]

        cv_ok = e["valid"] and e["cv"] <= p["unstable_cv"]
        rpm_low = not (e["rpm"] >= p["sync_rpm_min"])
        rpm_high = e["rpm"] > p["sync_rpm_max"]
        master_bad = cond[m] in ("sensor", "hose", "blocked")
        measurable = (not sensor and not hoseish and not lost and pulsing == KCH and cv_ok and
                      not rpm_low and not rpm_high)
        if not measurable:
            self.stable_since = None
        elif self.stable_since is None:
            self.stable_since = now
        stable_for = now - self.stable_since if self.stable_since is not None else 0.0
        stable = self.stable_since is not None and stable_for >= p["stable_hold_s"]

        # Rivi 5, yhteismuotoinen alipaineen muutos (kaasu, WP-2c): kanavien keskiarvot
        # päivittyvät eri hetkinä, joten kaikkien yhteinen muutos näkyisi hetken spreadinä.
        # Taso-EMA karkaa keskiarvosta, tai keskiarvot ovat siirtyneet COMMON_WINDOW_S...2x
        # sitten, kaikissa kanavissa samaan suuntaan -> odota. Vakausajastin ei nollaudu.
        common_move = False
        if pulsing == KCH:
            common_move = all_beyond([c["level"] - c["mean"] for c in chs], self.COMMON_MOVE_KPA)
            if self.cm_count == 0 or now - self.cm_t[1] >= self.COMMON_WINDOW_S:
                self.cm_t = [self.cm_t[1], now]
                self.cm_mean = [self.cm_mean[1], [c["mean"] for c in chs]]
                self.cm_count = min(self.cm_count + 1, 2)
            if self.cm_count >= 2:
                moved = [chs[i]["mean"] - self.cm_mean[0][i] for i in range(KCH)]
                if all_beyond(moved, self.COMMON_MOVE_KPA):
                    common_move = True
        else:
            self.cm_count = 0  # keskiarvot eivät ole vertailukelpoisia pulssikatkon yli

        # Kanavakohtainen luokitus snapshotiin (ch[i].cond, WP-10). "sat" vain, kun rivin 10
        # varoitus on päällä (pitoaika) ja juuri tämä kanava leikkautuu.
        r["cond"] = ["sat" if cond[i] == "ok" and r["sat"] and chs[i]["sat"] else COND_KEY[cond[i]]
                     for i in range(KCH)]

        def clear():
            r["d"] = [0.0] * KCH
            r["spread"] = 0.0
            r["worst"] = -1

        if master_bad:
            r["state"], r["fault_ch"] = "fault_master", m
            clear()
            r["head"] = "Master (SYL %d) ei kelpaa viitteeksi – valitse toinen" % (m + 1)
            if cond[m] == "sensor":
                r["advice"] = "SYL %d: anturivika (jännite %d mV)" % (m + 1, chs[m]["mv"])
            elif cond[m] == "hose":
                r["advice"] = "SYL %d: ei alipainetta – tarkista letku" % (m + 1)
            else:
                r["advice"] = "SYL %d: ei pulssia (taso %s kPa) – tarkista letku" % (
                    m + 1, fmt1(chs[m]["level"]))
        elif sensor:
            r["state"], r["fault_ch"] = "fault_sensor", sensor[0]
            r["head"] = "VIKA: %s – anturivika" % channel_list(sensor)
            if len(sensor) == 1:
                r["advice"] = "Jännite %d mV – tarkista anturin johdot tai vaihda" % chs[sensor[0]]["mv"]
            else:
                r["advice"] = "Useita anturivikoja – tarkista anturien käyttöjännite"
        elif hoseish:
            r["state"], r["fault_ch"] = "fault_hose", hoseish[0]
            lst = channel_list(hoseish)
            plural = "letkut" if len(hoseish) > 1 else "letku"
            if not blocked:
                r["head"] = "VIKA: %s – ei alipainetta, tarkista %s" % (lst, plural)
                r["advice"] = "Letku irti tai vuotaa – kytke ja tarkista liitokset"
            elif not hose:
                r["head"] = "VIKA: %s – ei pulssia, tarkista %s" % (lst, plural)
                r["advice"] = "Taso %s kPa ilman pulssia – letku irti, tukossa tai taittunut?" % (
                    fmt1(chs[hoseish[0]]["level"]))
            else:
                r["head"] = "VIKA: %s – tarkista letkut" % lst
                r["advice"] = "Letku irti, vuotaa tai tukossa – tarkista liitokset"
        elif pulsing == 0:
            r["state"] = "no_signal"
            clear()
            r["head"] = "Ei alipainesignaalia"
            r["advice"] = "Moottori sammuksissa tai letkut irti"
        elif lost or not e["valid"] or not cv_ok or rpm_low:
            r["state"] = "unstable"
            r["head"] = "Odota, käynti tasaantuu"
            if lost:
                r["advice"] = "SYL %d: ei pulssia (taso %s kPa)" % (lost[0] + 1, fmt1(chs[lost[0]]["level"]))
            elif not e["valid"]:
                r["advice"] = "Kierroslukua ei vielä tunnistettu"
            elif not cv_ok:
                r["advice"] = "Käynti epätasainen: hajonta %s %% (raja %s %%)" % (
                    fmt1(e["cv"] * 100.0), fmt1(p["unstable_cv"] * 100.0))
            else:
                r["advice"] = "Kierrosluku liian matala: %d rpm (raja %d)" % (
                    rpm_int(math.floor(e["rpm"])), rpm_int(p["sync_rpm_min"]))
        elif rpm_high:
            r["state"] = "rpm_too_high"
            r["head"] = "Säädä joutokäynnillä"
            r["advice"] = "Kierrosluku %d rpm (raja %d) – laske kaasua" % (
                rpm_int(math.ceil(e["rpm"])), rpm_int(p["sync_rpm_max"]))
        elif common_move:
            r["state"] = "unstable"
            r["head"] = "Odota, käynti tasaantuu"
            r["advice"] = "Alipaine muuttuu kaikissa sylintereissä"
        elif not stable:
            r["state"] = "unstable"
            r["head"] = "Odota, käynti tasaantuu"
            r["advice"] = "Vakautta odotetaan: %s / %s s" % (fmt1(stable_for), fmt1(p["stable_hold_s"]))
        else:
            self._verdict(r, zero_valid)

        verdict = r["state"] in ("synced", "near", "out_of_sync")
        r["bars"] = r["verdict"] = verdict

        # Rivi 1: automaattinen nollaus, reunaliipattu kerran per NoSignal-jakso.
        if pulsing > 0:
            self.zero_armed = True
        near_zero = r["state"] == "no_signal" and all(abs(c["level"]) < p["off_kpa"] for c in chs)
        if not near_zero:
            self.zero_since = None
        else:
            restart = self.zero_since is None
            if not restart:
                for i, c in enumerate(chs):
                    lv = c["level"]
                    if max(self.zero_hi[i], lv) - min(self.zero_lo[i], lv) > self.AUTO_ZERO_BAND:
                        restart = True
            if restart:
                self.zero_since = now
                self.zero_lo = [c["level"] for c in chs]
                self.zero_hi = [c["level"] for c in chs]
            else:
                for i, c in enumerate(chs):
                    self.zero_lo[i] = min(self.zero_lo[i], c["level"])
                    self.zero_hi[i] = max(self.zero_hi[i], c["level"])
            if self.zero_armed and p["auto_zero"] and now - self.zero_since >= p["off_hold_s"]:
                r["auto_zero"] = True
                self.zero_armed = False
        return r

    def _verdict(self, r, zero_valid):
        p = self.p
        s = r["spread"]
        if s <= p["tol_green_kpa"]:
            if zero_valid:
                r["state"] = "synced"
                r["head"] = "Synkronoitu"
                r["advice"] = "Valmis: ero %s kPa (raja %s kPa)" % (fmt1(s), fmt1(p["tol_green_kpa"]))
            else:
                r["state"] = "near"  # D11: ilman nollausta ei koskaan vihreää
                r["head"] = "Lähellä – kalibroimaton"
                r["advice"] = ("Nollaus puuttuu – sammuta moottori hetkeksi, laite nollaa itse"
                               if p["auto_zero"] else
                               "Nollaus puuttuu – irrota letkut ja tee nollaus")
            return
        r["state"] = "near" if s <= p["tol_yellow_kpa"] else "out_of_sync"
        head = "Lähellä" if r["state"] == "near" else "Ei synkronissa"
        r["head"] = head + ("" if zero_valid else " – kalibroimaton")
        w = r["worst"]
        if w < 0:
            r["advice"] = "Ero %s kPa" % fmt1(s)
            return
        d = r["d"][w]
        more = d > 0.0
        r["advice"] = "SYL %d: %s läppää – imee %s kPa %s kuin SYL %d" % (
            w + 1, "avaa" if more else "sulje", fmt1(abs(d)), "enemmän" if more else "vähemmän",
            self.master + 1)
        # Sama neuvo rakenteisena (snapshotin act, WP-10): ainoa paikka, jossa act asetetaan.
        r["act"] = {"ch": w, "open": more}


# ---------------------------------------------------------------------------
# "Laite": simulaattori + DSP + status + kalibrointi + rengas + SSE-jakelu
# ---------------------------------------------------------------------------
class Device:
    def __init__(self, seed, sensor_err, calibrated):
        self.lock = threading.RLock()
        self.cfg = default_config()
        self.sim = default_sim()
        self.engine = Engine(seed, sensor_err)
        self.rng = random.Random(seed + 1)
        self.clients = set()
        self.boot_wall = time.monotonic()
        self.reboot_until = 0.0
        self.offset = [0.0] * KCH
        self.gain = [1.0] * KCH
        self.zero_valid = False
        self.gain_valid = False
        self.gaps = 0
        self.frozen = False
        self.seq = 0
        self.heap = 182000
        self._boot(first=True)
        if calibrated:
            self.offset = [-o for o in self.engine.offset]
            self.gain = [1.0 / (1.0 + g) for g in self.engine.gain_err]
            self.offset = [self.gain[i] * self.offset[i] for i in range(KCH)]
            self.zero_valid = self.gain_valid = True

    # --- elinkaari ----------------------------------------------------------
    def _boot(self, first=False):
        """Käynnistys: aika, DSP, status ja rengas alusta. Vahvistus säilyy
        (NVS), nollaus ei (D11: vain tämän käynnistyksen nollaus kelpaa)."""
        self.t0 = time.monotonic()
        self.n = 0
        self.boot_wall = self.t0
        self.dsps = [ChanDsp() for _ in range(KCH)]
        self.status = StatusMachine(self.cfg)
        self.ring = deque(maxlen=RING_CAP)
        self.wave_rows = deque(maxlen=WAVE_MAX_ROWS)
        self.decim_acc = [0.0] * KCH
        self.decim_n = 0
        self.result = None
        self.chs = None
        self.est = {"valid": False, "rpm": 0.0, "sd": 0.0, "cv": 0.0}
        self.fs_meas = float(FS)
        self.fs_count = 0
        self.fs_t = self.t0
        self.capture = None
        self.cal_busy_until = 0.0
        self.cal_msg = ""
        if not first:
            self.zero_valid = False
            self.offset = [0.0] * KCH
        self.engine.reset()

    def rebooting(self):
        return time.monotonic() < self.reboot_until

    def start_reboot(self):
        self.reboot_until = time.monotonic() + REBOOT_S
        with self.lock:
            clients = list(self.clients)
        for q in clients:
            q.put(None)  # katkaise SSE-yhteydet kuten laitteen uudelleenkäynnistys

    # --- näytepolku (sim-säie) ----------------------------------------------
    def run(self):
        was_rebooting = False
        while True:
            time.sleep(0.01)
            with self.lock:
                if self.rebooting():
                    was_rebooting = True
                    continue
                if was_rebooting:
                    was_rebooting = False
                    self._boot()
                target = int((time.monotonic() - self.t0) * FS)
                todo = target - self.n
                if todo > 2000:
                    # PC jäi jälkeen (esim. debuggeri): pudotetaan näytteet kuten
                    # laitteen sampler, ja aukko näkyy diag.gaps:issa.
                    skip = todo - 200
                    self.seq += skip
                    self.n += skip
                    self.gaps += 1
                    todo = 200
                for _ in range(max(0, todo)):
                    self._sample()

    def _sample(self):
        cfg = self.cfg
        mv, starts = self.engine.next(cfg, int(cfg["source"]))
        n = self.n
        if not self.frozen:
            self.ring.append((self.seq, mv[0], mv[1], mv[2]))
        self.seq += 1
        nom = [(mv[c] - cfg["v_zero_mv"]) / cfg["mv_per_kpa"] for c in range(KCH)]
        for c in range(KCH):
            self.dsps[c].push(n, mv[c], nom[c], cfg)
        for c in starts:
            self.dsps[c].cycle_boundary(n, cfg)
        for c in range(KCH):
            self.decim_acc[c] += self.gain[c] * nom[c] + self.offset[c]
        self.decim_n += 1
        if self.decim_n == DECIM:
            self.wave_rows.append([self.decim_acc[c] / DECIM for c in range(KCH)])
            self.decim_acc = [0.0] * KCH
            self.decim_n = 0
        if self.capture is not None:
            self._capture_add(nom)
        self.n += 1
        self.fs_count += 1
        if self.n % TICK_SAMPLES == 0:
            self._tick()

    def _tick(self):
        now_s = self.n * DT
        self.chs = [self.dsps[c].state(self.n, self.cfg, self.gain[c], self.offset[c])
                    for c in range(KCH)]
        self.est = rpm_estimate(self.chs, self.dsps)
        self.result = self.status.update(self.chs, self.est, self.zero_valid, self.gain_valid, now_s)
        if self.result["auto_zero"] and self.capture is None:
            res = self._zero()
            self.cal_msg = ("Automaattinen nollaus tehty (moottori sammuksissa)" if res == "Ok"
                            else CAL_TEXT[res])
        wall = time.monotonic()
        if wall - self.fs_t >= 1.0:
            self.fs_meas = self.fs_count / (wall - self.fs_t)
            self.fs_count = 0
            self.fs_t = wall
            self.heap = max(150000, min(200000, self.heap + self.rng.randint(-1500, 1500)))

    # --- kalibrointi (calibration.h:n portti) --------------------------------
    def _nominal_levels(self):
        return [d.level if d.level is not None else d.last_nom for d in self.dsps]

    def _zero(self):
        lv = self._nominal_levels()
        if not all(abs(v) <= self.cfg["off_kpa"] for v in lv):
            return "NotAtmosphere"
        self.offset = [-self.gain[i] * lv[i] for i in range(KCH)]
        self.zero_valid = True
        return "Ok"

    def _capture_add(self, nom):
        cap = self.capture
        cap["k"] += 1
        for c in range(KCH):  # Welford
            d = nom[c] - cap["mean"][c]
            cap["mean"][c] += d / cap["k"]
            cap["m2"][c] += d * (nom[c] - cap["mean"][c])
        if cap["k"] >= int(GAIN_CAPTURE_S * FS):
            self.capture = None
            sd = [math.sqrt(cap["m2"][c] / (cap["k"] - 1)) for c in range(KCH)]
            self.cal_msg = CAL_TEXT[self._compute_gain(cap["mean"], sd)]

    def _compute_gain(self, mean, sd):
        # Sama järjestys kuin calibration.h computeGain: NoZero, TooSmall, NotStable,
        # Mismatch, GainOutOfRange.
        if not self.zero_valid:
            return "NoZero"
        z = []
        cc = []
        for i in range(KCH):
            g = self.gain[i] if self.gain[i] > 0 else 1.0
            z.append(-self.offset[i] / g)
            cc.append(mean[i] - z[i])
        ref = sum(cc) / KCH
        if not abs(ref) >= 15.0:
            return "TooSmall"
        if not all(s < 0.5 for s in sd):
            return "NotStable"
        # Pareittainen ehto: max − min ≤ 15 % · min |c|.
        if not max(cc) - min(cc) <= 0.15 * min(abs(c) for c in cc):
            return "Mismatch"
        g_new = [ref / c for c in cc]
        if not all(0.85 <= g <= 1.15 for g in g_new):
            return "GainOutOfRange"
        self.gain = g_new
        self.offset = [-g_new[i] * z[i] for i in range(KCH)]
        self.gain_valid = True
        return "Ok"

    # --- REST-komennot --------------------------------------------------------
    def api_zero(self):
        with self.lock:
            if self.capture is not None:
                return {"ok": False, "msg": "Vahvistuskalibrointi on käynnissä – odota"}
            res = self._zero()
            self.cal_msg = CAL_TEXT[res]
            return {"ok": res == "Ok", "msg": CAL_TEXT[res]}

    def api_gain(self):
        with self.lock:
            if self.capture is not None:
                return {"ok": False, "msg": "Vahvistuskalibrointi on jo käynnissä"}
            self.capture = {"k": 0, "mean": [0.0] * KCH, "m2": [0.0] * KCH}
            self.cal_msg = "Mitataan vahvistusta (2 s) – pidä alipaine tasaisena"
            return {"ok": True, "msg": self.cal_msg}

    def api_cal_reset(self):
        with self.lock:
            # Nollapiste säilyy: z = -offset/gain pysyy, gain -> 1.
            self.offset = [self.offset[i] / self.gain[i] for i in range(KCH)]
            self.gain = [1.0] * KCH
            self.gain_valid = False
            self.cal_msg = "Vahvistuskalibrointi poistettu"
            return {"ok": True, "msg": self.cal_msg}

    def api_master(self, pairs):
        d = dict(pairs)
        v = d.get("ch", "")
        if v not in ("0", "1", "2"):
            return {"ok": False, "err": "Virheellinen kanava (ch = 0, 1 tai 2)"}
        return self.api_config_set([("master", v)])

    def api_config_set(self, pairs):
        if not pairs:
            return {"ok": False, "err": "Ei muutettavia asetuksia"}
        with self.lock:
            new = dict(self.cfg)
            for k, v in pairs:
                res = config_set_text(new, k, v)
                if res == "UnknownKey":
                    return {"ok": False, "err": "%s: %s" % (SET_RESULT_TEXT[res], k)}
                if res != "Ok":
                    label = CONFIG_FIELDS[FIELD_INDEX[k]][7]
                    return {"ok": False, "err": "%s (%s): %s" % (label, k, SET_RESULT_TEXT[res])}
            err = config_validate(new)
            if err:
                return {"ok": False, "err": err}
            self._commit_config(new)
            return {"ok": True}

    def api_config_reset(self):
        with self.lock:
            self._commit_config(default_config())
            return {"ok": True}

    def _commit_config(self, new):
        old = self.cfg
        self.cfg = new
        dsp_keys = ("lp_cutoff_hz", "level_tau_s", "hyst_frac", "hyst_min_kpa", "avg_cycles",
                    "rpm_min", "rpm_max", "pulse_timeout_s", "rpm_hist", "v_zero_mv", "mv_per_kpa")
        if any(old[k] != new[k] for k in dsp_keys):
            for d in self.dsps:
                d.reset()
        self.status.configure(new)  # uusi master/kynnykset = uusi vakausjakso (D7)
        if old["source"] != new["source"]:
            self.start_reboot()

    def api_sim_set(self, pairs):
        with self.lock:
            err = sim_apply(self.sim, pairs)
            if err:
                return {"ok": False, "err": err}
            self.engine.configure(self.sim, rpm_posted=any(k == "rpm" for k, _ in pairs))
            # Moottorisimulaattori on parametrien omistaja (laitteella: /api/sim yhdistää POSTin
            # sim.params():iin). Driftin muutos kirjaa nykyisen rpm:n engine.p['rpm']:ään, joten se
            # vie dev.sim:iin: muuten GET /api/sim näyttäisi vanhan arvon ja seuraava POST hyppäisi
            # takaisin siihen (docs/firmware.md §4.1).
            self.sim["rpm"] = self.engine.p["rpm"]
            return {"ok": True}

    # --- snapshot ------------------------------------------------------------
    def snapshot(self):
        with self.lock:
            if self.result is None or self.chs is None:
                return None
            r, cfg, e = self.result, self.cfg, self.est

            def rnd(v, d):
                return None if v is None or not math.isfinite(v) else round(v, d)

            chs = []
            for i, c in enumerate(self.chs):
                chs.append({
                    "d": rnd(r["d"][i], 2), "mean": rnd(c["mean"], 2), "level": rnd(c["level"], 2),
                    "min": rnd(c["min"], 2), "p2p": rnd(c["p2p"], 2), "rpm": rnd(c["rpm"], 1),
                    "mv": c["mv"], "pulse": c["pulse"], "fault": c["fault"], "sat": c["sat"],
                    "cond": r["cond"][i]})
            rows = [[round(v, 2) for v in row] for row in self.wave_rows]
            self.wave_rows.clear()
            return {
                "v": 1, "t": int((time.monotonic() - self.boot_wall) * 1000),
                "src": SOURCE_NAMES.get(int(cfg["source"]), "sim"),
                "state": r["state"], "fault_ch": r["fault_ch"], "bars": r["bars"],
                "verdict": r["verdict"], "head": r["head"], "advice": r["advice"],
                "act": dict(r["act"]) if r["act"] is not None else None,
                "flags": {"sat": r["sat"], "uncal_zero": r["uncal_zero"],
                          "uncal_gain": r["uncal_gain"], "drift": r["drift"]},
                "master": self.status.master,
                "tol": [cfg["tol_green_kpa"], cfg["tol_yellow_kpa"]],
                "rpm": rnd(e["rpm"], 1) if e["valid"] else None,
                "rpm_sd": rnd(e["sd"], 1) if e["valid"] else None,
                "rpm_cv": rnd(e["cv"], 4) if e["valid"] else None,
                "spread": rnd(r["spread"], 2), "worst": r["worst"],
                "ch": chs,
                "wave": {"fs": WAVE_FS, "s": rows},
                "cal": {"busy": self.capture is not None, "zero": self.zero_valid,
                        "gain": self.gain_valid, "msg": self.cal_msg},
                "diag": {"fs": round(self.fs_meas, 1), "gaps": self.gaps, "heap": self.heap,
                         "frozen": self.frozen},
            }

    def pusher(self):
        """Yksi snapshot per push-jakso kaikille asiakkaille (wave.s kertyy
        edellisestä viestistä, joten sitä ei voi rakentaa asiakaskohtaisesti)."""
        while True:
            rate = max(1, int(self.cfg["push_rate_hz"]))
            time.sleep(1.0 / rate)
            if self.rebooting():
                continue
            snap = self.snapshot()
            if snap is None:
                continue
            line = json.dumps(snap, ensure_ascii=False, separators=(",", ":"))
            with self.lock:
                clients = list(self.clients)
            for q in clients:
                if q.qsize() < 64:  # hidas asiakas ei kasvata muistia (laite pudottaa samoin)
                    q.put(line)

    # --- CSV (raw_ring.h CsvWriter) -------------------------------------------
    def csv(self):
        with self.lock:
            self.frozen = True
            rows = list(self.ring)
            cfg = dict(self.cfg)
            off, gain = list(self.offset), list(self.gain)
            zv, gv = self.zero_valid, self.gain_valid
        try:
            def ff(v, d):
                s = "%.*f" % (d, v)
                if s.startswith("-") and not any(ch in "123456789" for ch in s):
                    s = s[1:]
                return s

            def trim(s):
                return s.rstrip("0").rstrip(".") if "." in s else s

            first = rows[0][0] if rows else 0
            gaps_win = sum(1 for a, b in zip(rows, rows[1:]) if b[0] - a[0] != 1)
            out = [
                "# Suckboxx raakadata (S5b)",
                "# device=suckboxx", "# format=1",
                "# source=%s" % SOURCE_NAMES.get(int(cfg["source"]), "sim"), "# fs_hz=%d" % FS,
                "# v_zero_mv=%s" % trim(ff(cfg["v_zero_mv"], 3)),
                "# mv_per_kpa=%s" % trim(ff(cfg["mv_per_kpa"], 4)),
                "# rail_comp=%d" % (1 if cfg["rail_comp"] else 0),
                "# rail_nominal_mv=%s" % trim(ff(cfg["rail_nominal_mv"], 1)),
                "# zero_valid=%d" % (1 if zv else 0), "# gain_valid=%d" % (1 if gv else 0),
                "# offset_kpa=%s" % ",".join(trim(ff(o, 4)) for o in off),
                "# gain=%s" % ",".join(trim(ff(g, 6)) for g in gain),
                "# samples=%d" % len(rows), "# first_seq=%d" % first,
                "# gaps=%d" % gaps_win, "# gaps_total=%d" % self.gaps,
                "# kpa=gain*(mv-v_zero_mv)/mv_per_kpa+offset (ei kiskokorjausta)",
                "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa",
            ]
            vz, k = cfg["v_zero_mv"], cfg["mv_per_kpa"]
            for seq, a, b, c in rows:
                kp = [ff(gain[i] * ((m - vz) / k) + off[i], 3) for i, m in enumerate((a, b, c))]
                out.append("%d,%d,%d,%d,%d,%s,%s,%s" % (seq, (seq - first) * 1000 // FS, a, b, c,
                                                        kp[0], kp[1], kp[2]))
            return ("\n".join(out) + "\n").encode("ascii")
        finally:
            with self.lock:
                self.frozen = False


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    server_version = "SuckboxxMock/1"
    dev = None  # asetetaan main():ssa

    def log_message(self, fmt, *args):
        if self.server.verbose:
            sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

    def _send(self, code, body, ctype="application/json; charset=utf-8", extra=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj, ensure_ascii=False, separators=(",", ":")))

    def _form(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n).decode("utf-8", "replace") if n > 0 else ""
        q = urlsplit(self.path).query
        return parse_qsl(raw, keep_blank_values=True) + parse_qsl(q, keep_blank_values=True)

    def do_GET(self):
        path = urlsplit(self.path).path
        dev = self.dev
        if dev.rebooting() and path != "/":
            return self._send(503, "laite käynnistyy uudelleen", "text/plain; charset=utf-8")
        if path in ("/", "/index.html"):
            try:
                with open(INDEX_HTML, "rb") as f:  # luetaan joka kerta: muokkaa + päivitä selain
                    body = f.read()
            except OSError as e:
                return self._send(500, "web/index.html puuttuu: %s" % e, "text/plain; charset=utf-8")
            return self._send(200, body, "text/html; charset=utf-8")
        if path == "/events":
            return self._sse()
        if path == "/api/config":
            with dev.lock:
                return self._send(200, config_to_json(dev.cfg))
        if path == "/api/sim":
            with dev.lock:
                return self._json(dev.sim)
        if path == "/api/raw.csv":
            body = dev.csv()
            return self._send(200, body, "text/csv; charset=us-ascii",
                              {"Content-Disposition": 'attachment; filename="suckboxx_raw.csv"'})
        return self._send(404, "ei löydy", "text/plain; charset=utf-8")

    def do_POST(self):
        path = urlsplit(self.path).path
        dev = self.dev
        if dev.rebooting():
            return self._send(503, "laite käynnistyy uudelleen", "text/plain; charset=utf-8")
        pairs = self._form()
        routes = {
            "/api/config": lambda: dev.api_config_set(pairs),
            "/api/config/reset": dev.api_config_reset,
            "/api/master": lambda: dev.api_master(pairs),
            "/api/cal/zero": dev.api_zero,
            "/api/cal/gain": dev.api_gain,
            "/api/cal/reset": dev.api_cal_reset,
            "/api/sim": lambda: dev.api_sim_set(pairs),
        }
        fn = routes.get(path)
        if fn is None:
            return self._send(404, "ei löydy", "text/plain; charset=utf-8")
        return self._json(fn())

    def _sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        q = queue.Queue()
        with self.dev.lock:
            self.dev.clients.add(q)
        try:
            self.wfile.write(b"retry: 1000\n\n")
            self.wfile.flush()
            while True:
                try:
                    line = q.get(timeout=5.0)
                except queue.Empty:
                    self.wfile.write(b": ka\n\n")  # keep-alive-kommentti
                    self.wfile.flush()
                    continue
                if line is None:
                    break  # "uudelleenkäynnistys"
                self.wfile.write(("event: snap\ndata: %s\n\n" % line).encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            pass
        finally:
            with self.dev.lock:
                self.dev.clients.discard(q)
            self.close_connection = True


# ---------------------------------------------------------------------------
# --check-config: rekisteri vs. firmware-lähde
# ---------------------------------------------------------------------------
def check_config():
    try:
        with open(CONFIG_H, encoding="utf-8") as f:
            ch = f.read()
        with open(TYPES_H, encoding="utf-8") as f:
            th = f.read()
    except OSError as e:
        print("lähde puuttuu: %s" % e)
        return 2
    groups = dict(re.findall(r'constexpr char (\w+)\[\] = "([^"]*)";', ch))
    body = ch.replace("\\\n", " ")
    entries = re.findall(
        r'F\(\s*(\w+)\s*,\s*([\w.]+)\s*,\s*"([^"]*)"\s*,\s*(\w+)\s*,\s*([-\d.]+)f\s*,\s*([-\d.]+)f'
        r'\s*,\s*(\w+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)', body)
    structs = {}
    for name, sbody in re.findall(r"struct (\w+) \{(.*?)\n\};", th + "\n" + ch, re.S):
        mem = {}
        for m, v in re.findall(r"^\s*[\w:]+\s+(\w+)\s*=\s*([^;]+);", sbody, re.M):
            mem[m] = v.strip()
        structs[name] = mem
    owner = {"sensor": "SensorParams", "dsp": "DspParams", "status": "StatusParams"}

    def parse_def(v):
        if v in ("true", "false"):
            return v == "true"
        if v.startswith("SourceKind::"):
            return {"Sim": 0, "Adc": 1, "Hx710b": 2}[v.split("::")[1]]
        return float(v.rstrip("f"))

    errors = []
    if len(entries) != len(CONFIG_FIELDS):
        errors.append("kenttiä config.h:ssa %d, mockissa %d" % (len(entries), len(CONFIG_FIELDS)))
    for i, (eid, member, key, typ, mn, mx, grp, unit, label) in enumerate(entries):
        if i >= len(CONFIG_FIELDS):
            errors.append("puuttuu mockista: %s" % key)
            continue
        mk, mt, mmn, mmx, mdef, mgrp, munit, mlabel = CONFIG_FIELDS[i]
        parts = member.split(".")
        st = owner.get(parts[0], "Config") if len(parts) == 2 else "Config"
        d = parse_def(structs.get(st, {}).get(parts[-1], "nan"))
        want = (key, typ.lower(), float(mn), float(mx), float(d), groups.get(grp, grp), unit, label)
        have = (mk, mt, float(mmn), float(mmx), float(mdef), mgrp, munit, mlabel)
        if want != have:
            errors.append("kenttä %d eroaa:\n  config.h: %r\n  mock:     %r" % (i, want, have))
    if errors:
        print("\n".join(errors))
        return 1
    print("OK: %d kenttää vastaa config.h:ta ja types.h:ta" % len(entries))
    return 0


# ---------------------------------------------------------------------------
# --check-snapshot: snapshotin rakenne vs. laite (WP-10)
#
# Laitteen snapshotin muoto on kirjattu test_snapshot:iin muotomerkkijonoina
# (avaimet järjestyksessä, tyypit n/s/b/z), ja native-testi vertaa laitteen
# viestiä niihin. Tämä ajaa mockin samoissa tilanteissa simuloidulla ajalla ja
# vertaa mockin viestiä samoihin merkkijonoihin, joten kumpikin puoli on
# sidottu samaan lähteeseen. Lisäksi state- ja cond-arvojoukot luetaan
# status.h:sta ja snapshot.h:sta.
# ---------------------------------------------------------------------------
TEST_SNAPSHOT = os.path.join(ROOT, "firmware", "test", "test_snapshot", "test_main.cpp")
STATUS_H = os.path.join(ROOT, "firmware", "src", "core", "status.h")
SNAPSHOT_H = os.path.join(ROOT, "firmware", "src", "core", "snapshot.h")

# (muoto test_snapshot:ssa, sim-asetukset, kesto s, odotettu tila) - samat tilanteet kuin
# test_shape_engine_off_matches_mock, test_shape_running_matches_mock ja
# test_delta_sign_and_worst_follow_status.
SNAPSHOT_CASES = [
    ("kShapeOff", [("engine_on", "0")], 1.0, "no_signal"),
    ("kShapeRunning", [], 5.0, "near"),
    ("kShapeAdvice", [("err3", "-4")], 6.0, "out_of_sync"),
]


def shape_of(v):
    """test_snapshot.cpp:n muotomerkkijono: {avain:muoto,...}, [..], n/s/b/z."""
    if isinstance(v, dict):
        return "{" + ",".join("%s:%s" % (k, shape_of(x)) for k, x in v.items()) + "}"
    if isinstance(v, list):
        return "[" + ",".join(shape_of(x) for x in v) + "]"
    if isinstance(v, bool):
        return "b"
    if v is None:
        return "z"
    if isinstance(v, str):
        return "s"
    if isinstance(v, (int, float)):
        return "n"
    raise TypeError("tuntematon tyyppi %r" % type(v))


def cpp_shapes(src):
    """kShape*-vakiot test_snapshot.cpp:stä: merkkijonoliteraalit ja #define-makrot auki."""
    macros = {m: b.replace("\\\n", " ")
              for m, b in re.findall(r"^#define (\w+) ((?:.*\\\n)*.*)$", src, re.M)}

    def expand(expr, depth=0):
        out = []
        for lit, name in re.findall(r'"((?:[^"\\]|\\.)*)"|\b([A-Z][A-Z0-9_]*)\b', expr):
            if not name:
                out.append(lit)
            elif name in macros and depth < 4:
                out.append(expand(macros[name], depth + 1))
            else:
                raise ValueError("tuntematon makro %s" % name)
        return "".join(out)

    return {n: expand(e) for n, e in re.findall(r"const char\* const (kShape\w+) =(.*?);", src, re.S)}


def switch_keys(src, func):
    """Funktion func paluuarvot ('return "x";') lähdekoodista arvojoukkona."""
    m = re.search(r"inline const char\* %s\([^)]*\) \{(.*?)\n\}" % func, src, re.S)
    return set(re.findall(r'return "(\w+)";', m.group(1))) if m else set()


def headless_snapshot(sim_pairs, seconds, seed=1):
    """Mock ilman säikeitä ja seinäkelloa: näytteet ajetaan suoraan (deterministinen)."""
    dev = Device(seed, False, False)
    if sim_pairs:
        res = dev.api_sim_set(sim_pairs)
        if not res["ok"]:
            raise ValueError(res["err"])
    with dev.lock:
        for _ in range(int(seconds * FS + 0.5)):
            dev._sample()
        dev.wave_rows.clear()  # kuten test_snapshot: dropWave() ennen viestiä
    return dev.snapshot()


def check_snapshot():
    try:
        with open(TEST_SNAPSHOT, encoding="utf-8") as f:
            shapes = cpp_shapes(f.read())
        with open(STATUS_H, encoding="utf-8") as f:
            states = switch_keys(f.read(), "syncStateKey")
        with open(SNAPSHOT_H, encoding="utf-8") as f:
            conds = switch_keys(f.read(), "channelCondKey")
    except (OSError, ValueError) as e:
        print("lähde puuttuu tai ei jäsenny: %s" % e)
        return 2
    errors = []
    if states != set(STATE_KEYS):
        errors.append("state-arvot eroavat: status.h %s, mock %s" % (sorted(states), sorted(STATE_KEYS)))
    if conds != set(COND_KEYS):
        errors.append("cond-arvot eroavat: snapshot.h %s, mock %s" % (sorted(conds), sorted(COND_KEYS)))
    for name, sim, seconds, state in SNAPSHOT_CASES:
        want = shapes.get(name)
        if want is None:
            errors.append("%s puuttuu test_snapshot:sta" % name)
            continue
        snap = headless_snapshot(sim, seconds)
        try:
            # Laite ei koskaan lähetä NaN/Inf:iä (JsonWriter -> null); sama mockilta.
            snap = json.loads(json.dumps(snap, ensure_ascii=False, allow_nan=False))
        except ValueError as e:
            errors.append("%s: ei kelvollista JSONia (%s)" % (name, e))
            continue
        have = shape_of(snap)
        if have != want:
            errors.append("%s eroaa:\n  laite: %s\n  mock:  %s" % (name, want, have))
            continue
        if snap["state"] != state:
            errors.append("%s: tila %s, laitteella %s" % (name, snap["state"], state))
        bad = [c["cond"] for c in snap["ch"] if c["cond"] not in COND_KEYS]
        if bad:
            errors.append("%s: tuntematon cond %s" % (name, bad))
        # test_delta_sign_and_worst_follow_status: SYL 3 imee 4 kPa enemmän -> avaa (§4.7).
        if name == "kShapeAdvice" and snap["act"] != {"ch": 2, "open": True}:
            errors.append("%s: act %r, laitteella {'ch': 2, 'open': True}" % (name, snap["act"]))
    if errors:
        print("\n".join(errors))
        return 1
    print("OK: %d snapshot-muotoa, %d tilaa ja %d cond-arvoa vastaavat laitetta" % (
        len(SNAPSHOT_CASES), len(states), len(conds)))
    return 0


def main():
    ap = argparse.ArgumentParser(description="Suckboxx PC-mock (sama API kuin laitteella)")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--sensor-err", action="store_true",
                    help="anturien R4-virheet (offset < 1 kPa, gain ±2,5 %%) simulaattoriin")
    ap.add_argument("--calibrated", action="store_true",
                    help="käynnisty nollattuna ja vahvistuskalibroituna")
    ap.add_argument("--verbose", action="store_true", help="HTTP-loki")
    ap.add_argument("--check-config", action="store_true",
                    help="vertaa rekisteriä config.h/types.h:hon ja lopeta")
    ap.add_argument("--check-snapshot", action="store_true",
                    help="vertaa snapshotin rakennetta laitteen muotoon (test_snapshot) ja lopeta")
    args = ap.parse_args()
    if args.check_config:
        sys.exit(check_config())
    if args.check_snapshot:
        sys.exit(check_snapshot())

    dev = Device(args.seed, args.sensor_err, args.calibrated)
    Handler.dev = dev
    threading.Thread(target=dev.run, daemon=True, name="sim").start()
    threading.Thread(target=dev.pusher, daemon=True, name="push").start()
    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    srv.daemon_threads = True
    srv.verbose = args.verbose
    print("Suckboxx mock: http://localhost:%d  (Ctrl+C lopettaa)" % args.port, flush=True)
    try:
        srv.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()


if __name__ == "__main__":
    main()
