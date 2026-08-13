#!/usr/bin/env python3
"""
slice_grid_from_gemini.py - Viipaloi Gemini-kasvuruudukko vaihe-frameiksi.

Parametrinen seuraaja slice_basil_from_gemini.py:lle: mika tahansa rows×cols
-ruudukko -> stage_00..0(N-1).png annetulle kasville.

Oletuskaytto (basilika, 3x2 = 6 vaihetta, lukujarjestys vas->oik, ylh->alas):
    python artwork/scripts/slice_grid_from_gemini.py \
        --source imported_images/Gemini_Generated_Image_5y6m575y6m575y6m.png \
        --plant basil --rows 2 --cols 3

RUUDUKON VALIT tunnistetaan tummuusprofiilista (ei tasajako): Gemini piirtaa
usein rivit ERI korkuisiksi (pienet taimet ylhaalla ahtaammin, iso kasvi alempi
rivi korkeampi). Tasajako leikkaisi naapurisolun latvan mukaan (itu-frame nayttaisi
tuuhean kasvin ylaosan). Gutter-haku etsii kunkin jaon syvimman valkoisen kaytavan
odotetun jakokohdan ymparilta. Sitten kukin solu autocropataan sisaltoon.
--equal pakottaa vanhan tasajaon.
"""
import argparse
from pathlib import Path
from PIL import Image

REPO = Path(__file__).resolve().parents[4]
EINK = REPO / "firmware/e-ink-seinataulu"


def autocrop_white(img: Image.Image, threshold: int = 240) -> Image.Image:
    gray = img.convert("L")
    inv = gray.point(lambda p: 0 if p >= threshold else 255)
    bbox = inv.getbbox()
    return img.crop(bbox) if bbox else img


def ink_profiles(gray_small: Image.Image):
    """Palauttaa (col_ink, row_ink): tummien pikselien maara per sarake/rivi."""
    w, h = gray_small.size
    px = gray_small.load()
    col = [0] * w
    row = [0] * h
    for y in range(h):
        for x in range(w):
            if px[x, y] < 128:
                col[x] += 1
                row[y] += 1
    return col, row


def find_gutters(profile, n_cells, span, win_frac=0.28):
    """Etsi n_cells-1 jakokohtaa: kunkin odotetun rajan (i*span/n_cells)
    ymparilta se kohta jossa VAHITEN tummuutta (valkoisin kaytava). Leveahko
    ikkuna, koska Gemini piirtaa rivit eri korkuisiksi -> todellinen valkoinen
    kaytava voi olla kaukana geometrisesta keskikohdasta. Tasatilanteessa
    (usea yhta valkoinen rivi) valitaan keskimmainen -> kaytavan keskelta."""
    splits = []
    L = len(profile)
    for i in range(1, n_cells):
        center = int(L * i / n_cells)
        win = max(3, int(L * win_frac))
        lo, hi = max(0, center - win), min(L, center + win)
        best_val = min(profile[k] for k in range(lo, hi))
        band = [k for k in range(lo, hi) if profile[k] <= best_val + max(1, int(0.02 * max(profile) if max(profile) else 1))]
        best = band[len(band) // 2] if band else (lo + hi) // 2
        splits.append(int(best * span / L))
    return splits


def main() -> None:
    ap = argparse.ArgumentParser(description="Viipaloi Gemini-kasvuruudukko frameiksi")
    ap.add_argument("--source", required=True)
    ap.add_argument("--plant", required=True)
    ap.add_argument("--rows", type=int, default=2)
    ap.add_argument("--cols", type=int, default=3)
    ap.add_argument("--pad-frac", type=float, default=0.06, help="reunus autocropin ymparille")
    ap.add_argument("--equal", action="store_true", help="tasajako gutter-haun sijaan")
    ap.add_argument("--no-autocrop", action="store_true", help="ala autocroppaa soluja")
    ap.add_argument("--picks", default=None,
                    help="pilkkuerotellut solu-indeksit (rivijarjestys r*cols+c) -> stage_00.. "
                         "esim. 3x3-ruudukosta jossa 1 tyhja: 0,1,2,4,7,8. Oletus: kaikki jarjestyksessa.")
    args = ap.parse_args()

    src = Path(args.source)
    if not src.is_absolute():
        src = EINK / args.source if (EINK / args.source).exists() else REPO / args.source
    if not src.exists():
        raise SystemExit(f"Lahdekuvaa ei loydy: {src}")

    dst = EINK / "artwork/growth/plants" / args.plant
    dst.mkdir(parents=True, exist_ok=True)

    img = Image.open(src).convert("RGB")
    W, H = img.size
    print(f"Lahde: {src.name}  {W}x{H}  ruudukko {args.cols}x{args.rows}")

    if args.equal:
        xs = [i * W // args.cols for i in range(args.cols)] + [W]
        ys = [i * H // args.rows for i in range(args.rows)] + [H]
    else:
        # Gutter-haku pienennetylla harmaakuvalla (nopeus).
        scale_w = 240
        small = img.convert("L").resize((scale_w, int(H * scale_w / W)))
        col, row = ink_profiles(small)
        vx = find_gutters(col, args.cols, W)
        hy = find_gutters(row, args.rows, H)
        xs = [0] + vx + [W]
        ys = [0] + hy + [H]
        print(f"  gutterit: pystyt={vx}  vaaka={hy}")

    if args.picks:
        picks = [int(p) for p in args.picks.split(",")]
    else:
        picks = list(range(args.rows * args.cols))

    for n, cell in enumerate(picks):
        ri, ci = cell // args.cols, cell % args.cols
        tile = img.crop((xs[ci], ys[ri], xs[ci + 1], ys[ri + 1]))
        if not args.no_autocrop:
            tile = autocrop_white(tile, 240)
            # Lisaa tasainen valkoreunus jotta kasvi ei liimaudu reunaan.
            tw, th = tile.size
            pad = int(max(tw, th) * args.pad_frac)
            padded = Image.new("RGB", (tw + 2 * pad, th + 2 * pad), (255, 255, 255))
            padded.paste(tile, (pad, pad))
            tile = padded
        out = dst / f"stage_{n:02d}.png"
        tile.save(out)
        print(f"  stage_{n:02d}.png  cell {cell} (r={ri},c={ci}) -> {tile.size}")

    print(f"OK: {len(picks)} framea -> {dst}")


if __name__ == "__main__":
    main()
