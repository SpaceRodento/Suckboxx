#!/usr/bin/env python3
"""
PlantMeister OTA upload helper.

Usage:
    python scripts/ota_upload.py [ip] [--env <name>] [--bin path/to/firmware.bin]
                                      [--user admin] [--password admin]

Target address, in order of precedence:
    1. positional <ip>      one-off
    2. env PM_IP            persistent for the session (same var as pm.py,
                            health_check.py, sensor_log.py)
    3. built-in default     scripts/pm_device.py DEFAULT_IP

Examples:
    # Default: production main firmware to the default/PM_IP address
    python scripts/ota_upload.py

    # Explicit address (another device, another network)
    python scripts/ota_upload.py 192.168.0.50

    # Smoke target — built from firmware/hw_smoke
    python scripts/ota_upload.py --env motor_dead_man

Endpoint contract (ElegantOTA v3):
    GET  /ota/start?mode=fr&hash=<md5>   - prepare Update.begin()
    POST /ota/upload (multipart "file")  - stream firmware
Authentication: HTTP Digest (not Basic). Credentials from secrets.h
or CLI flags.

Reboot verification (the important part):
    ElegantOTA's POST returns 200 and the /update page keeps answering
    even when the chip never actually restarted (known quirk — the old
    firmware is still running). So "device responds" is NOT proof the new
    image booted. Instead this script reads /api/status uptime_ms before
    and after the upload: a genuine reboot resets millis() toward zero.
    If no reset is observed, it sends an explicit /api/command {cmd:REBOOT}
    and re-checks; if that still fails, it tells you to press RST by hand.
"""

import argparse
import hashlib
import sys
import time
from pathlib import Path

try:
    import requests
    from requests.auth import HTTPDigestAuth
except ImportError:
    print("ERROR: 'requests' package missing. Install with: pip install requests")
    sys.exit(2)

REPO_ROOT = Path(__file__).resolve().parents[3]


def _default_ip() -> str:
    """Laitteen osoite: PM_IP-env tai repon jaettu oletus.

    Osoite luetaan scripts/pm_device.py:sta jotta oletus on yhdessa paikassa
    kaikille tyokaluille. Jos importti ei onnistu (tiedosto kopioitu muualle),
    kaytetaan samaa vakiota paikallisesti - flashaus ei saa kaatua tahan.
    """
    import os

    env_ip = (os.environ.get("PM_IP") or "").strip()
    if env_ip:
        return env_ip
    try:
        sys.path.insert(0, str(REPO_ROOT / "scripts"))
        from pm_device import DEFAULT_IP  # noqa: PLC0415

        return DEFAULT_IP
    except ImportError:
        return "192.168.0.196"


# Tuotantolaitteen env. Sama vakio kuin scripts/pm.py EXPECTED_PIO_ENV ja
# platformio.ini default_envs - jos vaihdat, vaihda kaikki kolme.
PRODUCTION_ENV = "xiao_esp32s3_v2"

# Paafirmwaren envit. Nama kaantavat SAMAN sovelluksen eri feature-joukolla,
# joten vaara valinta ei kaadu eika valita - se vain sammuttaa laitteesta
# osia HILJAA (nappi, valo, anturit). Siksi kaikki muut kuin PRODUCTION_ENV
# vaativat --allow-non-production. hw_smoke-envit eivat ole talla listalla:
# niita ei valita vahingossa, koska niiden nimi on eksplisiittinen.
MAIN_FW_ENVS = {
    "xiao_esp32s3",
    "xiao_esp32s3_v2",
    "xiao_esp32s3_fan",
    "xiao_esp32s3_ebbflow",
    "xiao_esp32s3_v3",
}

# Build-output kartta per env. PIO oletus on <project>/.pio/build/<env>/firmware.bin,
# mutta firmware/plantmeister/platformio.ini yliajaa build_dir = C:/pio/...
ENV_BINS = {
    # PERUS-ENV, EI TUOTANTOA. Ei antureita, ei MCP23017:aa (= ei nappia eika
    # valoa), ei tuuletinta. Olemassa vain _v2:n ja muiden vanhempana
    # (extends) + CI:n kaannostarkistuksena "ilman antureita" -polulle.
    "xiao_esp32s3":         Path("C:/pio/build/plantmeister/xiao_esp32s3/firmware.bin"),
    # PCB v2 -laite (PE-anturit + MCP23017-nappi/valo + fan): TUOTANTOLAITTEEN env.
    "xiao_esp32s3_v2":      Path("C:/pio/build/plantmeister/xiao_esp32s3_v2/firmware.bin"),
    "xiao_esp32s3_fan":     Path("C:/pio/build/plantmeister/xiao_esp32s3_fan/firmware.bin"),
    "xiao_esp32s3_ebbflow": Path("C:/pio/build/plantmeister/xiao_esp32s3_ebbflow/firmware.bin"),
    # V3-sensoripaketti: ei moottoria/pumppua/valorelettä/laajenninta. Sama
    # sovellus, eri feature-joukko kuin PRODUCTION_ENV -> vaatii
    # --allow-non-production, kuten muutkin MAIN_FW_ENVS-jäsenet.
    "xiao_esp32s3_v3":      Path("C:/pio/build/plantmeister/xiao_esp32s3_v3/firmware.bin"),
    "motor_dead_man":   REPO_ROOT / "firmware/hw_smoke/.pio/build/motor_dead_man/firmware.bin",
    "light_dead_man":   REPO_ROOT / "firmware/hw_smoke/.pio/build/light_dead_man/firmware.bin",
    "pump_dead_man":    REPO_ROOT / "firmware/hw_smoke/.pio/build/pump_dead_man/firmware.bin",
    "blink":            REPO_ROOT / "firmware/hw_smoke/.pio/build/blink/firmware.bin",
    "button_led":       REPO_ROOT / "firmware/hw_smoke/.pio/build/button_led/firmware.bin",
    "i2c_scan":         REPO_ROOT / "firmware/hw_smoke/.pio/build/i2c_scan/firmware.bin",
    "dht20_read":       REPO_ROOT / "firmware/hw_smoke/.pio/build/dht20_read/firmware.bin",
    "mcp23017_read":    REPO_ROOT / "firmware/hw_smoke/.pio/build/mcp23017_read/firmware.bin",
}
# Kartan taydellisyyden vartioi scripts/check_docs_vs_code.py (kategoria ENVKARTTA):
# jokainen platformio.ini:n env pitaa loytya talta - juuri tama ajautuminen
# ("skripti ei seurannut uutta enviä") on osunut nelja kertaa.


# A fresh boot has a small uptime. Used only when no pre-upload baseline
# was available (e.g. /api/status auth-gated): below this we assume a reboot.
FRESH_BOOT_MS = 60_000


def fetch_uptime_ms(base, timeout=3):
    """Return device uptime_ms from /api/status, or None if unavailable.

    /api/status uses the portal's cookie session auth, which is disabled
    when no admin PIN is set (the normal case). No Digest auth here — that
    is only for the /ota/* + /update endpoints. When a PIN *is* set and we
    have no session, /api/status returns only auth fields (no uptime_ms),
    so .get() yields None and the caller falls back gracefully.
    """
    try:
        r = requests.get(f"{base}/api/status", timeout=timeout)
        if r.status_code == 200:
            return r.json().get("uptime_ms")
    except (requests.RequestException, ValueError):
        pass
    return None


def fetch_build_id(base, timeout=3):
    """Return device build_id from /api/status, or None if unavailable.

    build_id contains the compile timestamp (__DATE__ __TIME__) and changes
    on every rebuild. A successful OTA activation is provable by comparing
    the build_id before and after the upload: if they differ, the new
    firmware booted. If they match, either a rollback occurred or the new
    image never activated.
    """
    try:
        r = requests.get(f"{base}/api/status", timeout=timeout)
        if r.status_code == 200:
            return r.json().get("build_id")
    except (requests.RequestException, ValueError):
        pass
    return None


def wait_for_reboot(base, baseline_ms, label="", window=30):
    """Poll uptime_ms until a reboot (millis() reset) is observed.

    Returns True if a reset is seen within `window` seconds. A genuine
    reboot drops uptime_ms below the pre-upload baseline; if there was no
    baseline we accept any uptime under FRESH_BOOT_MS.
    """
    deadline = time.time() + window
    while time.time() < deadline:
        time.sleep(2)
        up = fetch_uptime_ms(base)
        if up is None:
            continue  # device offline mid-reboot or auth-gated — keep trying
        if baseline_ms is None:
            if up < FRESH_BOOT_MS:
                print(f"[OTA] reboot CONFIRMED{label}: uptime {up / 1000:.1f} s (fresh boot)")
                return True
        elif up < baseline_ms:
            print(f"[OTA] reboot CONFIRMED{label}: uptime {up / 1000:.1f} s "
                  f"(was {baseline_ms / 1000:.1f} s)")
            return True
    return False


def trigger_reboot(base, timeout=5):
    """Ask the device to reboot via /api/command. Returns True if accepted.

    Open when no admin PIN is set; otherwise returns False (gated) and the
    caller falls back to the manual-RST prompt.
    """
    try:
        r = requests.post(f"{base}/api/command",
                          json={"cmd": "REBOOT", "value": ""}, timeout=timeout)
        return r.status_code == 200
    except requests.RequestException:
        return False


def main():
    ap = argparse.ArgumentParser(description="OTA upload to PlantMeister XIAO.")
    default_ip = _default_ip()
    ap.add_argument("ip", nargs="?", default=default_ip,
                    help=f"Device IP (default: {default_ip}; set env PM_IP to change)")
    ap.add_argument("--env", default=PRODUCTION_ENV,
                    choices=sorted(ENV_BINS.keys()),
                    help=f"PIO env name. Default: {PRODUCTION_ENV} (tuotantolaite).")
    ap.add_argument("--allow-non-production", action="store_true",
                    help="salli muun paafirmware-envin kuin "
                         f"{PRODUCTION_ENV} flashaus (sammuttaa featureita)")
    ap.add_argument("--bin", default=None,
                    help="firmware.bin path (overrides --env mapping)")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--password", default="admin")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--allow-same-build", action="store_true",
                    help="älä failaa vaikka build_id ei vaihdu (tarkoituksellinen saman binäärin uudelleenflash)")
    args = ap.parse_args()

    # Vaara paafirmware-env ei kaadu eika valita - se sammuttaa laitteesta osia
    # hiljaa. Siksi portti on tassa, ei saantotekstissa: sama moka on osunut
    # kolmesti (viimeksi 16.7.2026: nappi mykistyi ja valo jaatyi).
    if (args.env in MAIN_FW_ENVS and args.env != PRODUCTION_ENV
            and not args.allow_non_production):
        print(f"ERROR: {args.env} EI ole tuotantolaitteen env ({PRODUCTION_ENV}).")
        print("       Talla flashaus sammuttaa HILJAA: MCP23017 (nappi + vihrea")
        print("       valo), PE-anturit (SCD41/MLX90614/AS7341), tuuletin, INA228.")
        print("       Laite bootaa normaalisti eika valita mitaan - siksi tama esto.")
        print(f"       Tuotanto:    python {Path(__file__).name} {args.ip}")
        print(f"       Tahallinen:  ... --env {args.env} --allow-non-production")
        sys.exit(2)

    bin_path = Path(args.bin) if args.bin else ENV_BINS[args.env]
    if not bin_path.is_file():
        print(f"ERROR: firmware not found: {bin_path}")
        # hw_smoke-envit kaannetaan eri projektikansiossa kuin paafirmware.
        # Ennen 17.7.2026 tama vihje neuvoi hw_smoke-kansioon KAIKILLE
        # paitsi xiao_esp32s3:lle - eli myos _v2:lle, joka on paafirmwarea.
        where = "" if args.env in MAIN_FW_ENVS else "  (in firmware/hw_smoke/)"
        print(f"       Build first: pio run -e {args.env}{where}")
        sys.exit(2)

    fw = bin_path.read_bytes()
    md5 = hashlib.md5(fw).hexdigest()
    auth = HTTPDigestAuth(args.user, args.password)
    base = f"http://{args.ip}"

    print(f"[OTA] target  : {base}")
    print(f"[OTA] env     : {args.env}")
    print(f"[OTA] firmware: {bin_path} ({len(fw):,} bytes)")
    print(f"[OTA] md5     : {md5}")

    # Capture uptime BEFORE the upload so we can prove a reboot afterwards.
    baseline_ms = fetch_uptime_ms(base)
    if baseline_ms is not None:
        print(f"[OTA] baseline uptime: {baseline_ms / 1000:.1f} s")
    else:
        print("[OTA] baseline uptime: unavailable (auth-gated?) — using fresh-boot heuristic")

    baseline_build = fetch_build_id(base)
    if baseline_build is not None:
        print(f"[OTA] baseline build_id: {baseline_build}")
    else:
        print("[OTA] baseline build_id: unavailable (auth-gated or old firmware?)")

    try:
        r = requests.get(f"{base}/ota/start", params={"mode": "fr", "hash": md5},
                         auth=auth, timeout=10)
    except requests.RequestException as e:
        print(f"[OTA] /ota/start failed: {e}")
        sys.exit(1)
    if r.status_code != 200:
        print(f"[OTA] /ota/start HTTP {r.status_code}: {r.text[:200]}")
        sys.exit(1)
    print("[OTA] /ota/start: OK")

    t0 = time.time()
    try:
        r = requests.post(f"{base}/ota/upload", auth=auth,
                          files={"file": (bin_path.name, fw, "application/octet-stream")},
                          timeout=args.timeout)
    except requests.RequestException as e:
        print(f"[OTA] /ota/upload failed: {e}")
        sys.exit(1)
    if r.status_code != 200:
        print(f"[OTA] /ota/upload HTTP {r.status_code}: {r.text[:200]}")
        sys.exit(1)
    print(f"[OTA] /ota/upload: OK ({time.time() - t0:.1f} s)")

    # Verify an ACTUAL reboot — not just that the HTTP server answers.
    print("[OTA] verifying reboot via /api/status uptime_ms...")
    reboot_confirmed = wait_for_reboot(base, baseline_ms, window=30)

    if reboot_confirmed:
        # Reboot was confirmed (uptime reset). Now verify build_id changed.
        print("[OTA] reboot confirmed, checking build_id activation...")
        new_build = None
        deadline = time.time() + 10
        while time.time() < deadline:
            new_build = fetch_build_id(base)
            if new_build is not None:
                break
            time.sleep(2)

        if new_build is None:
            print("[OTA] build_id ei saatavilla (vanha firmware ilman kenttää?) — fallback uptime-tulokseen")
            return 0
        elif new_build == baseline_build and baseline_build is not None:
            if args.allow_same_build:
                print("[OTA] build_id ennallaan (sallittu --allow-same-build)")
                return 0
            else:
                print(f"[OTA] VIRHE: build_id ei vaihtunut ({baseline_build}) — joko flashasit saman binäärin tai rollback palautti vanhan firmwaren")
                sys.exit(1)
        else:
            old_or_na = baseline_build if baseline_build is not None else "n/a"
            print(f"[OTA] ACTIVATED: build_id {old_or_na} -> {new_build}")
            return 0

    # No reset seen: ElegantOTA's known quirk — server answers but
    # ESP.restart() never fired, so the old firmware is still live.
    print("[OTA] no reboot detected — old firmware likely still running.")
    print("[OTA] sending explicit reboot (/api/command cmd=REBOOT)...")
    if trigger_reboot(base):
        if wait_for_reboot(base, baseline_ms,
                           label=" (after explicit REBOOT)", window=25):
            # Same activation proof as on the primary path: a reboot that
            # comes back with the old build_id means rollback, not success.
            print("[OTA] explicit reboot confirmed, verifying build_id...")
            new_build = None
            deadline = time.time() + 10
            while time.time() < deadline:
                new_build = fetch_build_id(base)
                if new_build is not None:
                    break
                time.sleep(2)

            if new_build is None:
                print("[OTA] build_id ei saatavilla — fallback uptime-tulokseen")
            elif new_build == baseline_build and baseline_build is not None:
                if args.allow_same_build:
                    print("[OTA] build_id ennallaan (sallittu --allow-same-build)")
                else:
                    print(f"[OTA] VIRHE: build_id ei vaihtunut ({baseline_build}) — joko flashasit saman binäärin tai rollback palautti vanhan firmwaren")
                    sys.exit(1)
            else:
                old_or_na = baseline_build if baseline_build is not None else "n/a"
                print(f"[OTA] ACTIVATED: build_id {old_or_na} -> {new_build}")
            return 0
    else:
        print("[OTA] /api/command REBOOT not accepted (admin PIN set?).")

    print("")
    print("=" * 60)
    print("  WARNING: reboot NOT confirmed.")
    print("  -> Press the XIAO RST button MANUALLY now.")
    print("  -> Confirm boot: python scripts/wireless_log_listener.py")
    print("=" * 60)
    return 1


if __name__ == "__main__":
    sys.exit(main())
