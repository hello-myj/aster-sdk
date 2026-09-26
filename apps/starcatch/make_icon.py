#!/usr/bin/env python3
"""Generate the Star Catch launcher icon as ASTI RGB565A8."""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tools"))
from icon_convert import CF_RGB565A8, encode


def main():
    scale = 4
    side = 96 * scale
    tile = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    draw = ImageDraw.Draw(tile)
    for y in range(side):
        t = y / (side - 1)
        color = (int(12 + 17 * t), int(38 + 25 * t), int(75 + 38 * t), 255)
        draw.line((0, y, side, y), fill=color)
    mask = Image.new("L", (side, side), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, side - 1, side - 1),
                                            radius=24 * scale, fill=255)
    tile.putalpha(mask)
    draw = ImageDraw.Draw(tile)
    def box(rect):
        return tuple(int(v * scale) for v in rect)
    draw.line(box((66, 12, 57, 27)), fill="#FB7185", width=5 * scale)
    draw.ellipse(box((63, 7, 76, 20)), fill="#FB7185")
    points = []
    for i in range(10):
        angle = -math.pi / 2 + i * math.pi / 5
        radius = 27 if i % 2 == 0 else 11
        points.append(((48 + radius * math.cos(angle)) * scale,
                       (43 + radius * math.sin(angle)) * scale))
    draw.polygon(points, fill="#FCD34D")
    draw.ellipse(box((42, 36, 51, 45)), fill="#FFF7CD")
    draw.rounded_rectangle(box((17, 71, 79, 83)), radius=6 * scale,
                           fill="#4DD4E5")
    draw.rounded_rectangle(box((25, 68, 71, 74)), radius=2 * scale,
                           fill="#C7F9FF")
    tile = tile.resize((96, 96), Image.Resampling.LANCZOS)
    (HERE / "icon.bin").write_bytes(encode(tile, CF_RGB565A8))


if __name__ == "__main__":
    main()
