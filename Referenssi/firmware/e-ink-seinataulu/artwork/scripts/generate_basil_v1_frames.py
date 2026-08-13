#!/usr/bin/env python3
"""
Generate v1 source frames for e-ink growth animation.

Creates:
  artwork/growth/generic/stage_00..05.png
    artwork/growth/plants/<species>/stage_00..05.png

Style: monochrome-friendly silhouette with retro game feel.
Includes a basil realism pass and additional common herb packs.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, Dict

from PIL import Image, ImageDraw


FRAME_COUNT = 6
W = 200
H = 360


def canvas() -> Image.Image:
    return Image.new("L", (W, H), 255)


def draw_ground(draw: ImageDraw.ImageDraw) -> None:
    draw.rectangle((20, H - 36, W - 20, H - 28), fill=220)
    draw.line((20, H - 36, W - 20, H - 36), fill=180, width=1)


def draw_stem(draw: ImageDraw.ImageDraw, x: int, y0: int, y1: int, width: int = 4) -> None:
    draw.line((x, y0, x, y1), fill=40, width=width)


def draw_leaf(draw: ImageDraw.ImageDraw, x: int, y: int, rx: int, ry: int, tilt: int = 0, fill: int = 40) -> None:
    draw.ellipse((x - rx, y - ry, x + rx, y + ry), outline=fill, fill=fill)
    if tilt != 0:
        draw.line((x, y, x + tilt, y - ry + 2), fill=255, width=1)


def draw_leaf_teardrop(
    draw: ImageDraw.ImageDraw,
    cx: int,
    cy: int,
    length: int,
    width: int,
    side: int,
    fill: int = 35,
) -> None:
    tip_x = cx + side * (length + 4)
    base_x = cx - side * (length // 3)
    points = [
        (base_x, cy - width),
        (cx + side * (length // 2), cy - width // 2),
        (tip_x, cy),
        (cx + side * (length // 2), cy + width // 2),
        (base_x, cy + width),
    ]
    draw.polygon(points, fill=fill, outline=fill)
    draw.line((base_x, cy, tip_x - side * 2, cy), fill=255, width=1)


def draw_leaf_lobed(
    draw: ImageDraw.ImageDraw,
    cx: int,
    cy: int,
    r: int,
    petals: int,
    fill: int = 35,
) -> None:
    for i in range(petals):
        dx = (i - (petals - 1) / 2.0) * max(4, r // 2)
        draw.ellipse((cx + dx - r, cy - r, cx + dx + r, cy + r), fill=fill, outline=fill)


def draw_basil_stage(stage: int) -> Image.Image:
    img = canvas()
    d = ImageDraw.Draw(img)

    draw_ground(d)

    base_x = W // 2
    base_y = H - 40

    stem_height = 24 + stage * 34
    stem_top = base_y - stem_height
    draw_stem(d, base_x, base_y, stem_top, width=4)

    # Subtle side stems appear on later stages for a more natural basil shape.
    if stage >= 2:
        branch_y = base_y - (26 + stage * 8)
        d.line((base_x, branch_y, base_x - (10 + stage * 2), branch_y - 10), fill=35, width=2)
        d.line((base_x, branch_y + 2, base_x + (9 + stage * 2), branch_y - 9), fill=35, width=2)

    leaf_pairs = [0, 1, 2, 3, 4, 5][stage]
    for i in range(leaf_pairs):
        yy = base_y - 20 - i * 18
        length = max(10, 14 + stage * 4 - i * 2)
        width = max(4, 7 + stage * 2 - i)
        spread = 8 + i * 4
        draw_leaf_teardrop(d, base_x - spread, yy, length, width, side=-1)
        draw_leaf_teardrop(d, base_x + spread, yy - 1, length - 1, width, side=1)

        if stage >= 3:
            # Midrib accents improve readability after dithering.
            d.line((base_x - spread + 2, yy, base_x - spread - max(3, length // 3), yy), fill=255, width=1)
            d.line((base_x + spread - 2, yy - 1, base_x + spread + max(3, length // 3), yy - 1), fill=255, width=1)

    if stage >= 3:
        crown_size = 6 + (stage - 3) * 3
        d.ellipse((base_x - crown_size, stem_top - crown_size,
                   base_x + crown_size, stem_top + crown_size),
                  fill=30, outline=30)

        # Flower spike hint for mature basil (kept subtle for culinary stage feel).
        if stage >= 5:
            d.line((base_x, stem_top - crown_size - 2, base_x, stem_top - crown_size - 14), fill=30, width=2)
            d.ellipse((base_x - 3, stem_top - crown_size - 18,
                       base_x + 3, stem_top - crown_size - 12), fill=30, outline=30)

    if stage >= 4:
        draw_leaf_teardrop(d, base_x - 4, stem_top + 6, 8, 4, side=-1)
        draw_leaf_teardrop(d, base_x + 4, stem_top + 7, 7, 4, side=1)

    return img


def draw_generic_stage(stage: int) -> Image.Image:
    img = canvas()
    d = ImageDraw.Draw(img)
    draw_ground(d)

    base_x = W // 2
    base_y = H - 40
    stem_top = base_y - (14 + stage * 30)
    draw_stem(d, base_x, base_y, stem_top, width=3)

    # Neutral herb silhouette.
    for i in range(1 + stage):
        yy = base_y - 18 - i * 20
        span = 8 + i * 2
        d.line((base_x, yy, base_x - span, yy - 8), fill=35, width=3)
        d.line((base_x, yy + 1, base_x + span, yy - 7), fill=35, width=3)
        d.ellipse((base_x - span - 6, yy - 14, base_x - span + 3, yy - 5), fill=35)
        d.ellipse((base_x + span - 3, yy - 14, base_x + span + 6, yy - 5), fill=35)

    return img


def draw_mint_stage(stage: int) -> Image.Image:
    img = canvas()
    d = ImageDraw.Draw(img)
    draw_ground(d)

    base_x = W // 2
    base_y = H - 40
    stem_top = base_y - (24 + stage * 30)
    draw_stem(d, base_x, base_y, stem_top, width=4)

    for i in range(1 + stage):
        yy = base_y - 20 - i * 18
        spread = 10 + i * 4
        rr = max(6, 10 + stage - i)
        draw_leaf_lobed(d, base_x - spread, yy, rr, petals=3)
        draw_leaf_lobed(d, base_x + spread, yy - 1, rr, petals=3)
        d.line((base_x - spread + 2, yy, base_x - spread - rr + 2, yy), fill=255, width=1)
        d.line((base_x + spread - 2, yy - 1, base_x + spread + rr - 2, yy - 1), fill=255, width=1)

    if stage >= 4:
        draw_leaf_lobed(d, base_x, stem_top + 6, 8, petals=3)

    return img


def draw_dill_stage(stage: int) -> Image.Image:
    img = canvas()
    d = ImageDraw.Draw(img)
    draw_ground(d)

    base_x = W // 2
    base_y = H - 40
    stem_top = base_y - (20 + stage * 34)
    draw_stem(d, base_x, base_y, stem_top, width=3)

    branches = 2 + stage * 2
    for i in range(branches):
        yy = base_y - 18 - i * 14
        span = 8 + i * 2
        for side in (-1, 1):
            x2 = base_x + side * span
            y2 = yy - 7
            d.line((base_x, yy, x2, y2), fill=35, width=2)
            for k in range(3):
                fx = x2 + side * (3 + k * 2)
                fy = y2 - (k * 2)
                d.line((x2, y2, fx, fy), fill=35, width=1)

    return img


def draw_parsley_stage(stage: int) -> Image.Image:
    img = canvas()
    d = ImageDraw.Draw(img)
    draw_ground(d)

    base_x = W // 2
    base_y = H - 40
    stem_top = base_y - (18 + stage * 28)
    draw_stem(d, base_x, base_y, stem_top, width=3)

    for i in range(1 + stage):
        yy = base_y - 18 - i * 16
        span = 9 + i * 3
        r = max(4, 7 + stage - i)
        draw_leaf_lobed(d, base_x - span, yy, r, petals=5)
        draw_leaf_lobed(d, base_x + span, yy - 1, r, petals=5)

    if stage >= 3:
        draw_leaf_lobed(d, base_x, stem_top + 8, 6 + stage, petals=5)

    return img


def draw_chives_stage(stage: int) -> Image.Image:
    img = canvas()
    d = ImageDraw.Draw(img)
    draw_ground(d)

    base_x = W // 2
    base_y = H - 40
    blade_count = 4 + stage * 2

    for i in range(blade_count):
        offset = (i - blade_count // 2) * 4
        height = 40 + stage * 30 + ((i % 3) * 8)
        x0 = base_x + offset
        y1 = base_y - height
        x1 = x0 + (1 if i % 2 == 0 else -1) * (3 + (i % 4))
        d.line((x0, base_y, x1, y1), fill=35, width=2)

    return img


def draw_cilantro_stage(stage: int) -> Image.Image:
    img = canvas()
    d = ImageDraw.Draw(img)
    draw_ground(d)

    base_x = W // 2
    base_y = H - 40
    stem_top = base_y - (20 + stage * 30)
    draw_stem(d, base_x, base_y, stem_top, width=3)

    for i in range(1 + stage):
        yy = base_y - 20 - i * 17
        span = 10 + i * 3
        draw_leaf_lobed(d, base_x - span, yy, 5 + stage - (i // 2), petals=4)
        draw_leaf_lobed(d, base_x + span, yy - 1, 5 + stage - (i // 2), petals=4)
        draw_leaf_lobed(d, base_x, yy - 5, 4 + (stage // 2), petals=4)

    return img


def save_frames(root: Path, generator: Callable[[int], Image.Image]) -> None:
    root.mkdir(parents=True, exist_ok=True)
    for i in range(FRAME_COUNT):
        frame = generator(i)
        frame.save(root / f"stage_{i:02d}.png")


def main() -> None:
    here = Path(__file__).resolve().parent
    growth_root = here.parent / "growth"

    generic_root = growth_root / "generic"
    plants_root = growth_root / "plants"

    generators: Dict[str, Callable[[int], Image.Image]] = {
        "basil": draw_basil_stage,
        "mint": draw_mint_stage,
        "dill": draw_dill_stage,
        "parsley": draw_parsley_stage,
        "chives": draw_chives_stage,
        "cilantro": draw_cilantro_stage,
    }

    save_frames(generic_root, draw_generic_stage)
    print(f"OK: wrote {FRAME_COUNT} generic frames to {generic_root}")

    for plant_id, fn in generators.items():
        out_root = plants_root / plant_id
        save_frames(out_root, fn)
        print(f"OK: wrote {FRAME_COUNT} {plant_id} frames to {out_root}")


if __name__ == "__main__":
    main()
