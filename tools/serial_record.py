#!/usr/bin/env python3
"""Suckboxx - raakadatan tallennus USB-sarjaportin kautta (firmware: hal/serial_stream.*).

Wi-Fin /api/raw.csv antaa vain viimeiset ~8 s. Tämä tallentaa jokaisen 1 kHz:n
näytteen niin pitkään kuin halutaan, ilman Wi-Fi-yhteyttä laitteeseen, ja
kirjoittaa saman CSV-muodon kuin /api/raw.csv (tools/cycle_profile.py lukee sen).

Käyttö:
  python tools/serial_record.py --seconds 60
  python tools/serial_record.py --port COM15 --seconds 30 --profile --open
  python tools/serial_record.py --seconds 20 --out puhallus.csv --profile

Tallennuksen aikana laite mittaa ja palvelee puhelinta normaalisti; vain
sarjaloki on tauolla. Vaatii pyserialin (pip install pyserial, tai
PlatformIOn python: ~/.platformio/penv/Scripts/python.exe).
"""

from __future__ import annotations

import argparse
import datetime as dt
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # pragma: no cover
    raise SystemExit("pyserial puuttuu: pip install pyserial")

LOG_BAUD = 115200
CH = 3


def find_port() -> str:
    # ESP32-devkitien USB-UART-sillat: CP210x (10C4), CH340 (1A86), FTDI (0403).
    for p in serial.tools.list_ports.comports():
        if p.vid in (0x10C4, 0x1A86, 0x0403):
            return p.device
    raise SystemExit("laitetta ei löytynyt, anna --port (esim. COM15)")


def record(port: str, seconds: int, baud: int) -> tuple[dict, list[tuple[int, int, int, int]], str]:
    # DTR ja RTS pois ennen avausta: devkitin automaattinen reset-piiri (EN, IO0)
    # käynnistää ESP32:n uudelleen, jos Windows heilauttaa linjoja avauksessa.
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, LOG_BAUD, 0.5
    s.dtr = False
    s.rts = False
    s.open()
    try:
        s.reset_input_buffer()
        # Komento uudelleen, jos laite ei kuittaa (käynnistyi silti uudelleen ja
        # komento hukkui käynnistyslokiin).
        t_end = time.time() + 8
        next_send = 0.0
        while True:
            if time.time() >= next_send:
                s.write(f"stream {seconds} {baud}\n".encode())
                next_send = time.time() + 2.0
            line = s.readline().decode(errors="replace").strip()
            if line.startswith("# stream ok"):
                break
            if line.startswith("# stream virhe"):
                raise SystemExit(line)
            if time.time() > t_end:
                raise SystemExit("laite ei vastannut (vanha firmware ilman sarjavirtaa?)")
        s.baudrate = baud
        meta: dict[str, str] = {}
        rows: list[tuple[int, int, int, int]] = []
        bad = 0
        end_line = ""
        t_end = time.time() + seconds + 10
        last_print = 0.0
        while time.time() < t_end:
            raw = s.readline()
            if not raw:
                continue
            line = raw.decode(errors="replace").strip()
            if line.startswith("# end"):
                end_line = line
                break
            if line.startswith("#"):
                body = line[1:].strip()
                if "=" in body:
                    k, v = body.split("=", 1)
                    meta[k.strip()] = v.strip()
                continue
            parts = line.split(",")
            if len(parts) != 1 + CH:
                continue
            try:
                rows.append(tuple(int(p) for p in parts))  # type: ignore[arg-type]
            except ValueError:
                bad += 1
            now = time.time()
            if now - last_print > 1.0:
                last_print = now
                done = len(rows) / 1000.0
                print(f"\r  {done:5.1f} / {seconds} s", end="", flush=True)
        print()
        if bad:
            meta["bad_lines"] = str(bad)
        return meta, rows, end_line
    finally:
        try:
            s.baudrate = LOG_BAUD
        except Exception:
            pass
        s.close()


def write_csv(path: Path, meta: dict, rows: list, end_line: str) -> None:
    fs = int(meta.get("fs_hz", "1000"))
    vz = float(meta.get("v_zero_mv", "2700"))
    k = float(meta.get("mv_per_kpa", "25"))
    off = [float(v) for v in meta.get("offset_kpa", "0,0,0").split(",")]
    gain = [float(v) for v in meta.get("gain", "1,1,1").split(",")]
    gaps = sum(1 for a, b in zip(rows, rows[1:]) if b[0] - a[0] != 1)
    out = [
        "# Suckboxx raakadata (sarjavirta, tools/serial_record.py)",
        "# device=suckboxx", "# format=1",
        f"# source={meta.get('source', '?')}", f"# fs_hz={fs}",
        f"# v_zero_mv={vz:g}", f"# mv_per_kpa={k:g}",
        f"# zero_valid={meta.get('zero_valid', '0')}", f"# gain_valid={meta.get('gain_valid', '0')}",
        f"# offset_kpa={','.join(f'{v:g}' for v in off)}", f"# gain={','.join(f'{v:g}' for v in gain)}",
        f"# samples={len(rows)}", f"# gaps={gaps}",
        f"# recorded={dt.datetime.now().isoformat(timespec='seconds')}",
        f"# stream_end={end_line[1:].strip() if end_line else 'puuttuu'}",
        "# kpa=gain*(mv-v_zero_mv)/mv_per_kpa+offset (ei kiskokorjausta)",
        "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa",
    ]
    for seq, a, b, c in rows:
        kp = [gain[i] * (m - vz) / k + off[i] for i, m in enumerate((a, b, c))]
        out.append(f"{seq},{seq * 1000 // fs},{a},{b},{c},{kp[0]:.3f},{kp[1]:.3f},{kp[2]:.3f}")
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description="Suckboxxin raakadatan tallennus USB:n kautta")
    ap.add_argument("--port", help="sarjaportti (oletus: ensimmäinen ESP32-silta)")
    ap.add_argument("--seconds", type=int, default=30, help="tallennuksen pituus (oletus 30)")
    ap.add_argument("--baud", type=int, default=921600, help="virran nopeus (oletus 921600)")
    ap.add_argument("--out", help="CSV-tiedosto (oletus suckboxx_serial_<aika>.csv)")
    ap.add_argument("--profile", action="store_true", help="aja tools/cycle_profile.py perään")
    ap.add_argument("--open", action="store_true", help="avaa raportti selaimeen (--profile)")
    args = ap.parse_args()

    port = args.port or find_port()
    out = Path(args.out) if args.out else Path(
        f"suckboxx_serial_{dt.datetime.now().strftime('%Y%m%d_%H%M%S')}.csv")
    print(f"tallennetaan {args.seconds} s portista {port} ({args.baud} baud) ...")
    meta, rows, end_line = record(port, args.seconds, args.baud)
    if not rows:
        raise SystemExit("ei näytteitä")
    write_csv(out, meta, rows, end_line)
    gaps = sum(b[0] - a[0] - 1 for a, b in zip(rows, rows[1:]) if b[0] - a[0] != 1)
    print(f"{len(rows)} näytettä ({len(rows) / 1000:.1f} s), puuttuvia {gaps}; {end_line or 'end-rivi puuttuu'}")
    print(f"tallennettu: {out}")
    if args.profile:
        cmd = [sys.executable, str(Path(__file__).with_name("cycle_profile.py")), str(out)]
        if args.open:
            cmd.append("--open")
        return subprocess.call(cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
