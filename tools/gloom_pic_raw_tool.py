#!/usr/bin/env python3
"""
ZGloomEditor Campaign Phase 1 helper
Raw Gloom picture <-> PNG converter.

Important:
- This tool does NOT repack CrM2.
- CrM2 input must be decrunched first with the existing Gloom/ZGloom decruncher.
- Raw picture data is treated as one byte per pixel: palette index 0..255.
- .pal output is written as RGB0 entries, 4 bytes per color.

Usage examples:
  Export raw Gloom picture to PNG:
    python tools/gloom_pic_raw_tool.py export --raw pics/Title --pal pics/Title.pal --png out/Title.png --width 320 --height 256

  Import PNG to raw Gloom picture + .pal:
    python tools/gloom_pic_raw_tool.py import --png my_title.png --raw pics/Title --pal pics/Title.pal --width 320 --height 256 --colors 256
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import List, Tuple

try:
    from PIL import Image
except Exception as exc:  # pragma: no cover - user environment helper
    print("Pillow is required for PNG import/export.", file=sys.stderr)
    print("Install it with: python -m pip install pillow", file=sys.stderr)
    raise

RGB = Tuple[int, int, int]


def is_crm2(path: Path) -> bool:
    with path.open("rb") as f:
        return f.read(4) == b"CrM2"


def expand_amiga_nibble(value: int) -> int:
    """Convert 0..15 to 0..255, keep regular 8-bit values untouched."""
    if 0 <= value <= 15:
        return value * 17
    return max(0, min(255, value))


def read_palette(path: Path) -> List[RGB]:
    data = path.read_bytes()
    colors: List[RGB] = []

    if len(data) % 4 == 0:
        for i in range(0, len(data), 4):
            r, g, b, _unused = data[i:i + 4]
            colors.append((expand_amiga_nibble(r), expand_amiga_nibble(g), expand_amiga_nibble(b)))
        return colors

    if len(data) % 3 == 0:
        for i in range(0, len(data), 3):
            r, g, b = data[i:i + 3]
            colors.append((expand_amiga_nibble(r), expand_amiga_nibble(g), expand_amiga_nibble(b)))
        return colors

    raise ValueError(f"Unsupported palette size: {len(data)} bytes")


def write_palette(path: Path, colors: List[RGB], entries: int) -> None:
    out = bytearray()
    padded = list(colors[:entries])
    while len(padded) < entries:
        padded.append((0, 0, 0))

    for r, g, b in padded:
        out.extend((r & 0xFF, g & 0xFF, b & 0xFF, 0))

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(out))


def make_pil_palette(colors: List[RGB]) -> List[int]:
    pal: List[int] = []
    for r, g, b in colors[:256]:
        pal.extend([r, g, b])
    while len(pal) < 256 * 3:
        pal.extend([0, 0, 0])
    return pal[:256 * 3]


def export_raw_to_png(raw_path: Path, pal_path: Path, png_path: Path, width: int, height: int) -> None:
    if is_crm2(raw_path):
        raise ValueError("Input is CrM2 compressed. Decrunch it first; Phase 1 does not decode CrM2 here.")

    raw = raw_path.read_bytes()
    expected = width * height
    if len(raw) < expected:
        raise ValueError(f"Raw picture is too small: {len(raw)} bytes, expected {expected}")
    if len(raw) > expected:
        print(f"Warning: raw picture has {len(raw)} bytes; only first {expected} bytes are used.", file=sys.stderr)
        raw = raw[:expected]

    colors = read_palette(pal_path)
    img = Image.frombytes("P", (width, height), raw)
    img.putpalette(make_pil_palette(colors))
    png_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(png_path)


def import_png_to_raw(png_path: Path, raw_path: Path, pal_path: Path, width: int, height: int, colors: int) -> None:
    if colors < 2 or colors > 256:
        raise ValueError("--colors must be between 2 and 256")

    img = Image.open(png_path).convert("RGBA")
    if img.size != (width, height):
        img = img.resize((width, height), Image.Resampling.LANCZOS)

    # Quantize to an adaptive indexed palette.
    # Convert through RGB because Pillow cannot always quantize RGBA directly with all backends.
    rgb = Image.new("RGB", img.size, (0, 0, 0))
    rgb.paste(img, mask=img.getchannel("A"))
    indexed = rgb.quantize(colors=colors, method=Image.Quantize.MEDIANCUT)

    raw = indexed.tobytes()
    palette = indexed.getpalette() or []
    out_colors: List[RGB] = []
    for i in range(colors):
        base = i * 3
        if base + 2 < len(palette):
            out_colors.append((palette[base], palette[base + 1], palette[base + 2]))
        else:
            out_colors.append((0, 0, 0))

    raw_path.parent.mkdir(parents=True, exist_ok=True)
    raw_path.write_bytes(raw)
    write_palette(pal_path, out_colors, colors)


def guess_dimensions_from_size(size: int) -> Tuple[int, int] | None:
    candidates = [(320, 256), (320, 240), (320, 200), (320, 128), (320, 64), (640, 256), (160, 128), (160, 100)]
    for w, h in candidates:
        if size == w * h:
            return w, h
    if size % 320 == 0:
        return 320, size // 320
    return None


def cmd_info(args: argparse.Namespace) -> None:
    path = Path(args.raw)
    pal = Path(args.pal) if args.pal else Path(str(path) + ".pal")
    print(f"Image: {path}")
    print(f"Exists: {path.exists()}")
    if path.exists():
        size = path.stat().st_size
        print(f"Bytes: {size}")
        print(f"CrM2: {is_crm2(path)}")
        dims = guess_dimensions_from_size(size)
        if dims:
            print(f"Guessed size: {dims[0]}x{dims[1]}")
    print(f"Palette: {pal}")
    print(f"Palette exists: {pal.exists()}")
    if pal.exists():
        colors = read_palette(pal)
        print(f"Palette entries/colors: {len(colors)}")


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="Raw Gloom picture <-> PNG converter, no CrM2 repacking")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_info = sub.add_parser("info", help="show raw picture and palette info")
    p_info.add_argument("--raw", required=True)
    p_info.add_argument("--pal")

    p_export = sub.add_parser("export", help="export raw indexed picture + .pal to PNG")
    p_export.add_argument("--raw", required=True)
    p_export.add_argument("--pal", required=True)
    p_export.add_argument("--png", required=True)
    p_export.add_argument("--width", type=int, default=320)
    p_export.add_argument("--height", type=int, default=256)

    p_import = sub.add_parser("import", help="import PNG to raw indexed picture + .pal")
    p_import.add_argument("--png", required=True)
    p_import.add_argument("--raw", required=True)
    p_import.add_argument("--pal", required=True)
    p_import.add_argument("--width", type=int, default=320)
    p_import.add_argument("--height", type=int, default=256)
    p_import.add_argument("--colors", type=int, default=256)

    args = parser.parse_args(argv)

    if args.cmd == "info":
        cmd_info(args)
    elif args.cmd == "export":
        export_raw_to_png(Path(args.raw), Path(args.pal), Path(args.png), args.width, args.height)
    elif args.cmd == "import":
        import_png_to_raw(Path(args.png), Path(args.raw), Path(args.pal), args.width, args.height, args.colors)
    else:
        parser.error("unknown command")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
