#!/usr/bin/env python3
"""Draw the chess app's launcher tile and encode it as ASTI RGB565A8."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tools"))
from icon_convert import CF_RGB565A8, encode


def main():
    size = 96
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)
    draw.rounded_rectangle((0, 0, 95, 95), radius=24, fill="#29463A")
    for row in range(4):
        for col in range(4):
            color = "#E9EBD7" if (row + col) % 2 == 0 else "#79956D"
            x, y = 12 + col * 18, 12 + row * 18
            draw.rectangle((x, y, x + 17, y + 17), fill=color)
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 56)
    draw.text((48, 47), "♔", fill="#14241D", stroke_width=3,
              stroke_fill="#F6F6E9", font=font, anchor="mm")
    (HERE / "icon.bin").write_bytes(encode(im, CF_RGB565A8))


if __name__ == "__main__":
    main()
