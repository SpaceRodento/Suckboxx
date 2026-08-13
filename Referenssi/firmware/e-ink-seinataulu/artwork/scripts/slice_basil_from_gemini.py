#!/usr/bin/env python3
"""
Slice the Gemini 'BASIL GROWTH VARIATIONS' 4x2 grid into 6 stage frames
for the growth animation pipeline.

Input:  firmware/e-ink-seinataulu/imported_images/Gemini_Generated_Image_402tan402tan402t.png
Output: firmware/e-ink-seinataulu/artwork/growth/plants/basil/stage_0N.png  (N=0..5)

Grid layout in source (cells indexed left-to-right, top-to-bottom):
  Row 1: [0]Stage0 Seedling | [1]Stage1 Spurt   | [2]Stage2 Tall    | [3]Stage2 Tall alt
  Row 2: [4]Stage3 Bushing  | [5]Stage4 Bushial | [6]Stage4 Lateral | [7]Stage5 Mature

Picked cells -> stages: 0->S0, 1->S1, 2->S2, 4->S3, 5->S4, 7->S5
(Skips redundant Stage2-alt and Stage4-Lateral duplicates.)

The script auto-detects the inner content box (drops the title banner + outer
white margins) by looking for the bounding box of non-white pixels.
"""
from pathlib import Path
from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[4]
SRC = REPO / "firmware/e-ink-seinataulu/imported_images/Gemini_Generated_Image_402tan402tan402t.png"
DST = REPO / "firmware/e-ink-seinataulu/artwork/growth/plants/basil"

COLS = 4
ROWS = 2
CELL_PICKS = [0, 1, 2, 4, 5, 7]  # source cell index per output stage 0..5

# Inner crop ratio: drop the top title banner and small outer margins.
# Tuned visually for this specific Gemini layout.
TOP_BANNER_FRAC = 0.08
SIDE_MARGIN_FRAC = 0.005
BOTTOM_MARGIN_FRAC = 0.005

# Each tile has a thin border + caption text strip at the TOP. Trim them so
# the resulting frame is mostly silhouette.
TILE_BORDER_PX = 6
TILE_CAPTION_TOP_FRAC = 0.18
TILE_CAPTION_BOTTOM_FRAC = 0.16  # next row's caption strip leaks here


def autocrop_white(img: Image.Image, threshold: int = 240) -> Image.Image:
    gray = img.convert("L")
    # invert so white -> 0; bbox finds non-zero region
    inv = gray.point(lambda p: 0 if p >= threshold else 255)
    bbox = inv.getbbox()
    return img.crop(bbox) if bbox else img


def main() -> None:
    if not SRC.exists():
        raise SystemExit(f"Source not found: {SRC}")

    DST.mkdir(parents=True, exist_ok=True)

    img = Image.open(SRC).convert("RGB")
    W, H = img.size
    print(f"Source size: {W}x{H}")

    # Crop title banner + outer margins
    left = int(W * SIDE_MARGIN_FRAC)
    right = W - int(W * SIDE_MARGIN_FRAC)
    top = int(H * TOP_BANNER_FRAC)
    bottom = H - int(H * BOTTOM_MARGIN_FRAC)
    inner = img.crop((left, top, right, bottom))
    iw, ih = inner.size
    print(f"Inner grid size: {iw}x{ih}")

    cell_w = iw // COLS
    cell_h = ih // ROWS

    for out_idx, src_idx in enumerate(CELL_PICKS):
        r = src_idx // COLS
        c = src_idx % COLS
        x0 = c * cell_w
        y0 = r * cell_h
        tile = inner.crop((x0, y0, x0 + cell_w, y0 + cell_h))

        # Trim border + caption strip (caption is at the TOP of each tile)
        tw, th = tile.size
        tile = tile.crop((
            TILE_BORDER_PX,
            int(th * TILE_CAPTION_TOP_FRAC),
            tw - TILE_BORDER_PX,
            th - int(th * TILE_CAPTION_BOTTOM_FRAC),
        ))

        # Final: autocrop residual whitespace to focus on the plant silhouette
        tile = autocrop_white(tile, threshold=240)

        # Crop 5 px off each edge to kill thin frame lines that bled in from
        # neighbouring tiles. Simple and effective; if the silhouette loses a
        # few pixels at the edge, fit_to_canvas restores margin around it.
        tw, th = tile.size
        edge = 5
        if tw > 2 * edge and th > 2 * edge:
            tile = tile.crop((edge, edge, tw - edge, th - edge))

        out_path = DST / f"stage_{out_idx:02d}.png"
        tile.save(out_path)
        print(f"  stage_{out_idx:02d}.png  src cell {src_idx} (r={r},c={c}) -> {tile.size}")

    print(f"OK: wrote 6 frames to {DST}")


if __name__ == "__main__":
    main()
