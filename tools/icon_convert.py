#!/usr/bin/env python3
"""PNG/SVG → MJXI icon blob (see docs/mja-format.md).

MJXI little-endian:
  16B header: magic "MJXI", ver=1, w, h, cf, reserved, data_size
  payload: I1 packed MSB-first / A8 / RGB565 LE / RGB565 then A8

Default cf is A8 (line-art mask, tinted at runtime from json `color`).
"""
from __future__ import annotations

import argparse
import io
import struct
import sys
from pathlib import Path

MAGIC = b"MJXI"
CF_I1, CF_A8, CF_RGB565, CF_RGB565A8 = 1, 2, 3, 4
CF_NAME = {
    "i1": CF_I1,
    "a8": CF_A8,
    "rgb565": CF_RGB565,
    "rgb565a8": CF_RGB565A8,
}


def load_rgba(src: Path, size: int | None, stroke: float):
    from PIL import Image

    if src.suffix.lower() == ".svg":
        try:
            import cairosvg
        except ImportError:
            sys.exit("error: SVG input needs cairosvg (pip install cairosvg pillow)")
        text = src.read_text(encoding="utf-8")
        text = text.replace("currentColor", "#ffffff")
        if stroke != 2.0:
            text = text.replace('stroke-width="2"', f'stroke-width="{stroke}"')
        wh = size or 24
        png = cairosvg.svg2png(
            bytestring=text.encode(),
            output_width=wh,
            output_height=wh,
            background_color="black",
        )
        im = Image.open(io.BytesIO(png)).convert("RGBA")
        # SVG path used a black backdrop; coverage is the light channel.
        rgb = im.convert("RGB")
        a = im.convert("L")
        im = Image.merge("RGBA", (*rgb.split(), a))
        return im

    im = Image.open(src).convert("RGBA")
    if size:
        im = im.resize((size, size), Image.Resampling.LANCZOS)
    return im


def pack_i1(a8: bytes, w: int, h: int) -> bytes:
    stride = (w + 7) // 8
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if a8[y * w + x] >= 128:
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def pack_rgb565(im) -> bytes:
    w, h = im.size
    rgb = im.convert("RGB")
    px = rgb.tobytes()
    out = bytearray(w * h * 2)
    for i in range(w * h):
        r, g, b = px[3 * i], px[3 * i + 1], px[3 * i + 2]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[2 * i] = v & 0xFF
        out[2 * i + 1] = v >> 8
    return bytes(out)


def payload(im, cf: int) -> bytes:
    w, h = im.size
    if cf == CF_A8:
        # Prefer alpha; fall back to luma (SVG coverage).
        a = im.getchannel("A").tobytes()
        if max(a) == 0:
            a = im.convert("L").tobytes()
        return a
    if cf == CF_I1:
        a8 = payload(im, CF_A8)
        return pack_i1(a8, w, h)
    if cf == CF_RGB565:
        return pack_rgb565(im)
    if cf == CF_RGB565A8:
        return pack_rgb565(im) + im.getchannel("A").tobytes()
    raise ValueError(cf)


def encode(im, cf: int) -> bytes:
    w, h = im.size
    if w < 1 or h < 1 or w > 512 or h > 512:
        sys.exit(f"error: unsupported size {w}x{h}")
    data = payload(im, cf)
    hdr = struct.pack("<4sHHHBBI", MAGIC, 1, w, h, cf, 0, len(data))
    return hdr + data


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", type=Path)
    ap.add_argument("-o", "--out", required=True, type=Path)
    ap.add_argument("--format", choices=sorted(CF_NAME), default="a8")
    ap.add_argument("--size", type=int, default=None, help="force square size (SVG default 24)")
    ap.add_argument("--stroke", type=float, default=2.0, help="SVG stroke-width override")
    args = ap.parse_args(argv)

    cf = CF_NAME[args.format]
    size = args.size
    if args.src.suffix.lower() == ".svg" and not size:
        size = 24
    im = load_rgba(args.src, size, args.stroke)
    blob = encode(im, cf)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    print(f"MJXI {args.out}: {im.size[0]}x{im.size[1]} {args.format} "
          f"{len(blob)} bytes ({len(blob) - 16} payload)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
