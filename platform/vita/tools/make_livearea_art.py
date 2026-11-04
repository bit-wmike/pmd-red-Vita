#!/usr/bin/env python3
# Original LiveArea artwork for the Mystery Dungeon Vita port (no game
# assets): a dark cave with a torch-lit stone floor grid and a staircase
# glow, plus the port's name and version. Usage: make_livearea_art.py <outdir> <version>
import math, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
GOLD = (250, 196, 70)
PALE = (230, 220, 200)


def cave(w, h, seed, cell):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        c = (int(18 + 30 * t), int(12 + 22 * t), int(28 + 24 * t))
        for x in range(w):
            px[x, y] = c
    d = ImageDraw.Draw(img)
    rnd = random.Random(seed)
    # stone floor: perspective grid in the lower half
    horizon = int(h * 0.42)
    for i in range(0, 40):
        y = horizon + int((i / 40.0) ** 2 * (h - horizon))
        shade = int(40 + 70 * i / 40)
        d.line((0, y, w, y), fill=(shade, shade - 10, shade - 20))
    for i in range(-12, 13):
        x0 = w // 2 + i * cell
        x1 = w // 2 + i * cell * 4
        d.line((x0, horizon, x1, h), fill=(70, 58, 48))
    # torch glow
    glow = Image.new("RGB", (w, h), (0, 0, 0))
    g = ImageDraw.Draw(glow)
    r = int(min(w, h) * 0.55)
    cx, cy = int(w * 0.5), int(h * 0.62)
    for k in range(r, 0, -4):
        a = int(90 * (1 - k / r) ** 2)
        g.ellipse((cx - k, cy - k // 2, cx + k, cy + k // 2), fill=(a, int(a * 0.6), int(a * 0.2)))
    glow = glow.filter(ImageFilter.GaussianBlur(w // 40))
    img = Image.blend(img, Image.eval(glow, lambda v: v), 0.0)
    img = Image.composite(img, img, Image.new("L", (w, h), 255))
    px = img.load()
    gp = glow.load()
    for y in range(h):
        for x in range(w):
            p = px[x, y]
            q = gp[x, y]
            px[x, y] = (min(255, p[0] + q[0]), min(255, p[1] + q[1]), min(255, p[2] + q[2]))
    # stars of dust
    d = ImageDraw.Draw(img)
    for _ in range(w * h // 1500):
        x, y = rnd.randrange(w), rnd.randrange(horizon)
        b = rnd.randrange(60, 160)
        d.point((x, y), fill=(b, b - 20, b - 40))
    return img


def text(img, center, s, size, color):
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT, size)
    box = d.textbbox((0, 0), s, font=f)
    tw, th = box[2] - box[0], box[3] - box[1]
    x, y = center[0] - tw // 2 - box[0], center[1] - th // 2 - box[1]
    d.text((x + 2, y + 2), s, font=f, fill=(0, 0, 0))
    d.text((x, y), s, font=f, fill=color)


out, version = sys.argv[1], sys.argv[2]
bg = cave(840, 500, 7, 60)
text(bg, (420, 120), "Mystery Dungeon", 62, GOLD)
text(bg, (420, 175), "Red Rescue Team", 34, PALE)
text(bg, (420, 455), "PS Vita Port " + version, 26, PALE)
bg.quantize(256).save(f"{out}/bg.png", optimize=True)

icon = cave(128, 128, 3, 10)
text(icon, (64, 50), "MD", 40, GOLD)
text(icon, (64, 100), "Vita", 22, PALE)
icon.quantize(256).save(f"{out}/icon0.png", optimize=True)

st = cave(280, 158, 5, 24)
text(st, (140, 55), "Mystery Dungeon", 26, GOLD)
text(st, (140, 90), "Start", 20, PALE)
text(st, (140, 130), "v" + version, 16, PALE)
st.quantize(256).save(f"{out}/startup.png", optimize=True)
print("livearea art written to", out)
