#!/usr/bin/env python3
"""
Build growth animation assets for e-ink firmware.

Input layout:
  artwork/growth/generic/stage_00.png ... stage_05.png
  artwork/growth/plants/<plant_id>/stage_00.png ... stage_05.png

Output:
  artwork/generated/growth_anim_assets.h
  artwork/generated/preview/<plant_id>_preview.png
  artwork/generated/preview/<plant_id>_preview_invert.png (optional)
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import List

from PIL import Image, ImageOps


BAYER4 = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
]

BAYER8 = [
    [0, 32, 8, 40, 2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
]


@dataclass
class AssetPack:
    plant_id: str
    frames: List[Image.Image]


def fit_to_canvas(img: Image.Image, width: int, height: int) -> Image.Image:
    src = img.convert("L")
    src.thumbnail((width, height), Image.Resampling.LANCZOS)

    canvas = Image.new("L", (width, height), 255)
    x = (width - src.width) // 2
    y = (height - src.height) // 2
    canvas.paste(src, (x, y))
    return canvas


def dither_ordered(gray: Image.Image, matrix: List[List[int]]) -> Image.Image:
    w, h = gray.size
    out = Image.new("1", (w, h), 1)

    m_size = len(matrix)
    max_v = m_size * m_size

    for y in range(h):
        for x in range(w):
            p = gray.getpixel((x, y))
            threshold = (matrix[y % m_size][x % m_size] + 0.5) * (255.0 / max_v)
            out.putpixel((x, y), 0 if p < threshold else 255)

    return out


def to_bw(gray: Image.Image, mode: str) -> Image.Image:
    if mode == "none":
        return gray.point(lambda p: 0 if p < 128 else 255, mode="1")
    if mode == "floyd":
        return gray.convert("1", dither=Image.Dither.FLOYDSTEINBERG)
    if mode == "bayer4":
        return dither_ordered(gray, BAYER4)
    if mode == "bayer8":
        return dither_ordered(gray, BAYER8)
    raise ValueError(f"Unsupported dither mode: {mode}")


def pack_msb_row_major(bw: Image.Image) -> bytes:
    width, height = bw.size
    packed = bytearray()

    for y in range(height):
        byte_val = 0
        bit_count = 0
        for x in range(width):
            px = bw.getpixel((x, y))
            bit = 1 if px == 0 else 0
            byte_val = (byte_val << 1) | bit
            bit_count += 1
            if bit_count == 8:
                packed.append(byte_val)
                byte_val = 0
                bit_count = 0

        if bit_count:
            byte_val <<= (8 - bit_count)
            packed.append(byte_val)

    return bytes(packed)


def bytes_literal(data: bytes, per_line: int = 16) -> str:
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i : i + per_line]
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    return "\n".join(lines)


def find_frames(folder: Path, frame_count: int) -> List[Path]:
    frames: List[Path] = []
    last_existing: Path | None = None

    for i in range(frame_count):
        p = folder / f"stage_{i:02d}.png"
        if p.exists():
            frames.append(p)
            last_existing = p
        elif last_existing is not None:
            frames.append(last_existing)
        else:
            raise FileNotFoundError(f"Missing first frame: {p}")

    return frames


def load_pack(folder: Path, plant_id: str, frame_count: int, width: int, height: int, dither: str) -> AssetPack:
    frame_paths = find_frames(folder, frame_count)
    frames: List[Image.Image] = []

    for p in frame_paths:
        fitted = fit_to_canvas(Image.open(p), width, height)
        frames.append(to_bw(fitted, dither))

    return AssetPack(plant_id=plant_id, frames=frames)


def save_preview(pack: AssetPack, out_dir: Path, invert: bool, both: bool) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)

    width, height = pack.frames[0].size
    sheet = Image.new("1", (width * len(pack.frames), height), 255)
    for idx, frame in enumerate(pack.frames):
        sheet.paste(frame, (idx * width, 0))

    normal_path = out_dir / f"{pack.plant_id}_preview.png"
    sheet.save(normal_path)

    if invert or both:
        inv = ImageOps.invert(sheet.convert("L")).convert("1")
        inv.save(out_dir / f"{pack.plant_id}_preview_invert.png")


def save_preview_html(out_dir: Path, plant_ids: List[str], title: str = "E-ink Growth Preview") -> None:
        items = "\n".join(
                f"""
            <section class=\"card\" data-plant=\"{pid}\">
                <h2>{pid}</h2>
                <img id=\"img-{pid}\" src=\"{pid}_preview.png\" alt=\"{pid} preview\" />
            </section>
"""
                for pid in plant_ids
        )

        html = f"""<!doctype html>
<html lang=\"fi\">
<head>
    <meta charset=\"utf-8\" />
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />
    <title>{title}</title>
    <style>
        body {{ font-family: Segoe UI, Arial, sans-serif; margin: 20px; background: #f6f7f8; color: #111; }}
        .top {{ display: flex; gap: 16px; align-items: center; flex-wrap: wrap; margin-bottom: 18px; }}
        .grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(320px, 1fr)); gap: 14px; }}
        .card {{ background: #fff; border: 1px solid #ddd; border-radius: 10px; padding: 10px; }}
        h1 {{ margin: 0 0 6px 0; font-size: 22px; }}
        h2 {{ margin: 0 0 8px 0; font-size: 16px; text-transform: lowercase; }}
        img {{ width: 100%; image-rendering: pixelated; border: 1px solid #ddd; background: #fff; }}
        .note {{ font-size: 13px; color: #444; }}
        .mono {{ font-family: Consolas, monospace; }}
    </style>
</head>
<body>
    <h1>{title}</h1>
    <div class=\"note\">Preview-stripit generoitu build_growth_assets.py:lla. Invert vaihtaa normal/invert kuvan.</div>
    <div class=\"top\">
        <label><input type=\"checkbox\" id=\"invert\" /> Invert preview</label>
        <span class=\"mono\">*_preview.png / *_preview_invert.png</span>
    </div>
    <div class=\"grid\">{items}
    </div>
    <script>
        const invert = document.getElementById('invert');
        invert.addEventListener('change', () => {{
            const on = invert.checked;
            document.querySelectorAll('img[id^="img-"]').forEach(img => {{
                const base = img.id.replace('img-', '');
                img.src = on ? `${{base}}_preview_invert.png` : `${{base}}_preview.png`;
            }});
        }});
    </script>
</body>
</html>
"""

        (out_dir / "index.html").write_text(html, encoding="utf-8")


def render_header(packs: List[AssetPack], width: int, height: int) -> str:
    lines: List[str] = []

    lines.append("/* Auto-generated by build_growth_assets.py */")
    lines.append("#ifndef GROWTH_ANIM_ASSETS_H")
    lines.append("#define GROWTH_ANIM_ASSETS_H")
    lines.append("")
    lines.append("#include <Arduino.h>")
    lines.append("")
    lines.append("struct GrowthAnimAsset {")
    lines.append("  const char* plantId;")
    lines.append("  uint8_t frameCount;")
    lines.append("  uint16_t width;")
    lines.append("  uint16_t height;")
    lines.append("  const unsigned char* const* frames;")
    lines.append("};")
    lines.append("")

    for pack in packs:
        for idx, frame in enumerate(pack.frames):
            data = pack_msb_row_major(frame)
            symbol = f"GROWTH_{pack.plant_id.upper()}_{idx:02d}"
            lines.append(f"static const unsigned char {symbol}[] PROGMEM = {{")
            lines.append(bytes_literal(data))
            lines.append("};")
            lines.append("")

        ptr_name = f"FRAMES_{pack.plant_id.upper()}"
        lines.append(f"static const unsigned char* const {ptr_name}[] = {{")
        for idx, _ in enumerate(pack.frames):
            lines.append(f"  GROWTH_{pack.plant_id.upper()}_{idx:02d},")
        lines.append("};")
        lines.append("")

    lines.append("static const GrowthAnimAsset GROWTH_ANIM_ASSETS[] = {")
    for pack in packs:
        ptr_name = f"FRAMES_{pack.plant_id.upper()}"
        lines.append(f'  {{"{pack.plant_id}", {len(pack.frames)}, {width}, {height}, {ptr_name}}},')
    lines.append("};")
    lines.append("")
    lines.append(f"static const uint8_t GROWTH_ANIM_ASSET_COUNT = {len(packs)};")
    lines.append("")
    lines.append("#endif // GROWTH_ANIM_ASSETS_H")

    return "\n".join(lines) + "\n"


def load_plant_list(plants_root: Path) -> List[str]:
    plant_ids = []
    if not plants_root.exists():
        return plant_ids
    for child in sorted(plants_root.iterdir()):
        if child.is_dir():
            plant_ids.append(child.name)
    return plant_ids


def main() -> None:
    parser = argparse.ArgumentParser(description="Build e-ink growth animation asset header")
    parser.add_argument("--root", default="artwork/growth", help="Growth asset root folder")
    parser.add_argument("--output", default="artwork/generated/growth_anim_assets.h", help="Generated header output")
    parser.add_argument("--preview-dir", default="artwork/generated/preview", help="Preview output folder")
    parser.add_argument("--frames", type=int, default=6, help="Frame count per plant")
    parser.add_argument("--width", type=int, default=152, help="Target frame width")
    parser.add_argument("--height", type=int, default=288, help="Target frame height")
    parser.add_argument("--dither", choices=["none", "floyd", "bayer4", "bayer8"], default="bayer8", help="Dither mode")
    parser.add_argument("--invert-preview", action="store_true", help="Generate inverted preview")
    parser.add_argument("--preview-both", action="store_true", help="Generate both normal and inverted previews")
    args = parser.parse_args()

    root = Path(args.root)
    generic_root = root / "generic"
    plants_root = root / "plants"

    if not generic_root.exists():
        raise FileNotFoundError(f"Missing generic folder: {generic_root}")

    packs: List[AssetPack] = []

    generic_pack = load_pack(generic_root, "generic", args.frames, args.width, args.height, args.dither)
    packs.append(generic_pack)

    plant_ids = load_plant_list(plants_root)
    for pid in plant_ids:
        p_root = plants_root / pid
        try:
            pack = load_pack(p_root, pid, args.frames, args.width, args.height, args.dither)
            packs.append(pack)
        except FileNotFoundError:
            # Skip incomplete plant pack, runtime will fallback to generic.
            continue

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(render_header(packs, args.width, args.height), encoding="utf-8")

    preview_dir = Path(args.preview_dir)
    for pack in packs:
        save_preview(pack, preview_dir, args.invert_preview, args.preview_both)

    summary = {
        "assets": [p.plant_id for p in packs],
        "frame_count": args.frames,
        "width": args.width,
        "height": args.height,
        "dither": args.dither,
    }
    (preview_dir / "build_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    save_preview_html(preview_dir, summary["assets"])

    print(f"OK: generated {out_path}")
    print(f"OK: assets = {', '.join(summary['assets'])}")
    print(f"OK: preview = {preview_dir / 'index.html'}")


if __name__ == "__main__":
    main()
