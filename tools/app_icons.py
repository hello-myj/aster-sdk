#!/usr/bin/env python3
"""Generate the app-drawer icons in apps/<app>/icon.bin.

Each icon is a full-colour 96x96 tile: a rounded gradient square in the app's
own colour with the app's Lucide glyph stroked white on top. The drawer
launcher blits it as-is -- no theme tint, no well drawn by the firmware -- so
an app's identity survives every theme (see firmware launcher/flux.c).

Two things live here and nowhere else:

  * the colour table, one hand-picked pair of stops per app;
  * the composition recipe (tile size, corner radius, glyph box, stroke).

Changing either only means re-running this script: the firmware takes whatever
RGB565A8 blob it finds on disk and scales it onto the ribbon.

Rasterising is cairosvg straight to RGBA. Do not route this through
icon_convert.load_rgba(): its SVG branch renders on a black backdrop and uses
the luminance channel as alpha, which is right for the tinted line-art masks
it was written for and silently wrong for colour art (every dark pixel would
come out transparent). The ASTI encoder below that -- icon_convert.encode --
is format-generic and is reused as-is.

    python3 tools/app_icons.py             # all apps
    python3 tools/app_icons.py player      # one or more app dirs
    python3 tools/app_icons.py --sheet /tmp/icons.png

Output is byte-reproducible for a given cairosvg/Pillow pair: same sources and
same numbers in, same 27664 bytes out. That is the contract the README states.
"""
from __future__ import annotations

import argparse
import io
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
LUCIDE = REPO / "assets" / "icons" / "lucide"
APPS = REPO / "apps"

sys.path.insert(0, str(HERE))
from icon_convert import CF_RGB565A8, encode  # noqa: E402

# Tile geometry. TILE must match LAUNCHER_WELL_PX in the launcher: a mismatch
# shows up as a shrunken or padded icon on the panel, not as an error.
#
# The first three are in the tile's own pixel space, so they scale together.
# STROKE is NOT one of them: it is written inside `scale(GLYPH_BOX/24)`, i.e.
# in Lucide's 24-unit space, where the SVG scales it for us. Scaling the
# stroke here as well would stroke the glyph thin.
TILE = 96
RADIUS = 24          # corner radius; ~25% of the tile, the iOS-ish proportion
GLYPH_BOX = 54       # the 24x24 Lucide viewBox is scaled into this square
STROKE = 2.4         # in Lucide's 24-unit space -> ~5.4 px once scaled up

# Per-app art: directory under apps/, Lucide source, gradient stops.
# Hues are spread around the wheel so that no two tiles on a page read alike;
# 2048/amber sit closest together (~20 deg) but differ sharply in lightness.
ART = [
    ("apitest",   "cpu",         "#0891B2", "#22D3EE"),  # cyan    - system probe
    ("gallery",   "image",       "#7C3AED", "#A855F7"),  # violet
    ("game.2048", "layout-grid", "#EA580C", "#F59E0B"),  # orange  - the classic
    ("gfxshow",   "palette",     "#C026D3", "#E879F9"),  # fuchsia - paint
    ("permdeny",  "lock",        "#DC2626", "#EF4444"),  # red     - refused
    ("player",    "music",       "#4F46E5", "#818CF8"),  # indigo
    ("reader",    "book-open",   "#047857", "#10B981"),  # emerald - paper
    ("snake",     "gamepad-2",   "#4D7C0F", "#84CC16"),  # lime
    ("trap",      "alert",       "#CA8A04", "#FACC15"),  # amber   - the trap
]


def glyph_body(src: Path) -> str:
    """The drawing elements of a Lucide SVG, without its <svg> wrapper.

    Lucide paints with `stroke="currentColor"` on the root and a few marks
    (the palette's dots, for instance) with `fill="currentColor"`. Resolving
    that to white here lets the composed group set its own colours without
    every child having to be rewritten.
    """
    text = src.read_text(encoding="utf-8")
    open_tag = text.find("<svg")
    if open_tag < 0:
        sys.exit(f"error: {src} has no <svg> element")
    body = text[text.index(">", open_tag) + 1:]
    body = body[: body.rindex("</svg>")]
    return body.replace("currentColor", "#FFFFFF").strip()


def compose(body: str, top: str, bottom: str) -> str:
    """One app icon as a standalone SVG, in the tile's own pixel space."""
    s = GLYPH_BOX / 24.0
    off = (TILE - GLYPH_BOX) / 2.0
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{TILE}" height="{TILE}" '
        f'viewBox="0 0 {TILE} {TILE}">\n'
        f'  <defs><linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">'
        f'<stop offset="0" stop-color="{top}"/>'
        f'<stop offset="1" stop-color="{bottom}"/></linearGradient></defs>\n'
        f'  <rect x="0" y="0" width="{TILE}" height="{TILE}" '
        f'rx="{RADIUS}" ry="{RADIUS}" fill="url(#bg)"/>\n'
        f'  <g transform="translate({off:g} {off:g}) scale({s:.6f})" fill="none" '
        f'stroke="#FFFFFF" stroke-width="{STROKE}" stroke-linecap="round" '
        f'stroke-linejoin="round">\n{body}\n  </g>\n</svg>\n'
    )


def rasterize(svg: str):
    from PIL import Image

    try:
        import cairosvg
    except ImportError:
        sys.exit("error: needs cairosvg (pip install cairosvg pillow)")

    # No background_color: the corners outside the rounded rect have to stay
    # transparent so the wallpaper shows through on the panel.
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=TILE,
                           output_height=TILE)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def build(want: list[str] | None) -> list[tuple[str, Path, bytes, object]]:
    out = []
    for app, glyph, top, bottom in ART:
        if want and app not in want:
            continue
        src = LUCIDE / f"{glyph}.svg"
        if not src.exists():
            sys.exit(f"error: {src} missing (see assets/icons/lucide/NOTICE)")
        im = rasterize(compose(glyph_body(src), top, bottom))
        out.append((app, APPS / app / "icon.bin", encode(im, CF_RGB565A8), im))
    if not out:
        sys.exit(f"error: no app matched {want} (known: {', '.join(a for a, *_ in ART)})")
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("apps", nargs="*", help="app dirs to rebuild (default: all)")
    ap.add_argument("--sheet", type=Path, help="write a contact sheet PNG here")
    ap.add_argument("--check", action="store_true",
                    help="fail if any icon.bin differs from what is on disk")
    args = ap.parse_args(argv)

    built = build(args.apps)
    stale = 0
    for app, path, blob, _ in built:
        old = path.read_bytes() if path.exists() else None
        stale += old != blob
        if args.check:
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(blob)
    if args.check:
        for app, path, blob, _ in built:
            print(f"{app}: {'OK' if path.exists() and path.read_bytes() == blob else 'STALE'}")
        return 1 if stale else 0

    print(f"{len(built)} icons, {len(built[0][2])} bytes each, "
          f"{stale} changed")
    if args.sheet:
        from PIL import Image

        cell = TILE + 8
        sheet = Image.new("RGB", (cell * 5, cell * 2), (24, 26, 34))
        for i, (_, _, _, im) in enumerate(built):
            sheet.paste(im, (cell * (i % 5) + 4, cell * (i // 5) + 4), im)
        sheet.save(args.sheet)
        print(f"contact sheet -> {args.sheet}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
