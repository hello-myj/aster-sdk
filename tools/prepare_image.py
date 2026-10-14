#!/usr/bin/env python3
"""Prepare one native-size RGB image for APP assets (optional Pillow tool)."""
import argparse
from pathlib import Path


def fit_size(sw, sh, width, height):
    if min(sw, sh, width, height) < 1 or max(width, height) > 4096:
        raise ValueError('target must be 1..4096 pixels on each axis')
    if sw * height >= sh * width:
        return width, max(1, (sh * width + sw // 2) // sw)
    return max(1, (sw * height + sh // 2) // sh), height


def prepare(source, destination, width, height, kind, background=None, allow_upscale=False):
    from PIL import Image, ImageOps, ImageColor
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve():
        raise ValueError('output must differ from source')
    if kind not in ('pixel', 'photo'):
        raise ValueError('kind must be pixel or photo')
    if destination.suffix.lower() not in ('.png', '.bmp'):
        raise ValueError('output must be PNG or 24-bit BMP')
    with Image.open(source) as original:
        if original.width * original.height > 16 * 1024 * 1024:
            raise ValueError('source exceeds 16 million pixels')
        image = ImageOps.exif_transpose(original).convert('RGBA')
    target = fit_size(*image.size, width, height)
    if not allow_upscale and (target[0] > image.width or target[1] > image.height):
        raise ValueError('upscaling requires --allow-upscale; prefer a larger original')
    has_alpha = image.getchannel('A').getextrema()[0] < 255
    if has_alpha and background is None:
        raise ValueError('GFX images do not retain independent alpha; choose --background, or use an ASTI RGB565A8 launcher icon')
    if image.size != target:
        resample = Image.Resampling.NEAREST if kind == 'pixel' else Image.Resampling.LANCZOS
        # Filter premultiplied colors, so invisible RGB never bleeds into edges.
        image = image.convert('RGBa').resize(target, resample).convert('RGBA')
    if has_alpha:
        base = Image.new('RGBA', target, ImageColor.getrgb(background) + (255,))
        image = Image.alpha_composite(base, image)
    image = image.convert('RGB')
    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(destination, format='BMP' if destination.suffix.lower() == '.bmp' else 'PNG')
    return target


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('destination', type=Path)
    p.add_argument('--width', type=int, required=True)
    p.add_argument('--height', type=int, required=True)
    p.add_argument('--kind', choices=('pixel', 'photo'), required=True)
    p.add_argument('--background')
    p.add_argument('--allow-upscale', action='store_true')
    a = p.parse_args(argv)
    try:
        w, h = prepare(a.source, a.destination, a.width, a.height, a.kind, a.background, a.allow_upscale)
    except ImportError:
        p.exit(1, 'Image preparation needs optional Pillow; APP compilation does not.\n')
    except (ValueError, OSError) as e:
        p.exit(1, str(e) + '\n')
    print(f'{a.destination}: {w}x{h}, RGB; GFX still treats RGB565 0x0000 as a transparent key')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
