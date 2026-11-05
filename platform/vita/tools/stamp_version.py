#!/usr/bin/env python3
"""Writes the version number onto the LiveArea images (startup.png and
bg.png) so that it is visible on the Vita's LiveArea, as in the TMC port.
Usage: stamp_version.py <in.png> <out.png> <version> <corner>
corner: 'br' (bottom right) or 'bl'."""
import sys
from PIL import Image, ImageDraw, ImageFont

src, dst, version, corner = sys.argv[1:5]
im = Image.open(src).convert("RGBA")
w, h = im.size
size = max(14, h // 11)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", size)
except OSError:
    font = ImageFont.load_default()
text = "v" + version
d = ImageDraw.Draw(im)
tw = d.textlength(text, font=font)
x = w - tw - size // 2 if corner == "br" else size // 2
y = h - size - size // 2
for ox, oy in ((-2, 0), (2, 0), (0, -2), (0, 2), (-1, -1), (1, 1), (-1, 1), (1, -1)):
    d.text((x + ox, y + oy), text, font=font, fill=(0, 0, 0, 230))
d.text((x, y), text, font=font, fill=(255, 240, 200, 255))
# the Vita's LiveArea shows only 8-bit palette PNGs (a 32-bit bg.png comes
# out as a grey page): back to 256 colours
im.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(dst, optimize=True)
