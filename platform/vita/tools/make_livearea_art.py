#!/usr/bin/env python3
# Original LiveArea artwork for the Mystery Dungeon Vita port (no game
# assets), in the flat style of the Minish Cap port: a rescue-team style
# badge (gold ring, blue gem, wings, ribbon) on a warm red gradient with
# sparkles, plus the port's name and version.
# Usage: make_livearea_art.py <outdir> <version>
import math, random, sys
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
CREAM = (252, 240, 200)
PALE = (255, 226, 170)


def background(w, h, seed):
    img = Image.new("RGB", (w, h))
    px = img.load()
    top, bottom = (120, 26, 30), (206, 78, 44)
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    d = ImageDraw.Draw(img)
    rnd = random.Random(seed)
    for _ in range(w * h // 2600):
        x, y = rnd.randrange(w), rnd.randrange(h)
        r = rnd.choice((1, 1, 2, 2, 3))
        d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 214, 150))
    return img


def badge(img, cx, cy, r):
    """A rescue badge: wings, gold ring, blue gem, ribbon tails."""
    d = ImageDraw.Draw(img)
    gold, gold_dark, gold_light = (238, 186, 60), (166, 112, 28), (255, 232, 150)
    blue, blue_dark, blue_light = (70, 140, 230), (30, 70, 160), (190, 220, 255)
    red, red_dark = (214, 50, 50), (140, 20, 30)
    # ribbon tails
    for sgn in (-1, 1):
        pts = [(cx + sgn * r * 0.25, cy + r * 0.55), (cx + sgn * r * 0.95, cy + r * 1.45),
               (cx + sgn * r * 0.55, cy + r * 1.35), (cx + sgn * r * 0.75, cy + r * 1.75),
               (cx + sgn * r * 0.05, cy + r * 0.95)]
        d.polygon(pts, fill=red, outline=red_dark)
    # wings
    for sgn in (-1, 1):
        for i, (dy, ln) in enumerate(((-0.55, 1.9), (-0.2, 1.7), (0.15, 1.4))):
            pts = [(cx + sgn * r * 0.7, cy + r * (dy + 0.2)),
                   (cx + sgn * r * ln, cy + r * (dy - 0.35)),
                   (cx + sgn * r * (ln - 0.1), cy + r * (dy + 0.25)),
                   (cx + sgn * r * 0.75, cy + r * (dy + 0.45))]
            d.polygon(pts, fill=gold_light if i == 0 else gold, outline=gold_dark)
    # ring and gem
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=gold, outline=gold_dark, width=max(2, r // 18))
    d.ellipse((cx - r * 0.78, cy - r * 0.78, cx + r * 0.78, cy + r * 0.78), fill=gold_dark)
    d.ellipse((cx - r * 0.68, cy - r * 0.68, cx + r * 0.68, cy + r * 0.68), fill=blue, outline=blue_dark, width=max(1, r // 30))
    d.ellipse((cx - r * 0.38, cy - r * 0.52, cx - r * 0.02, cy - r * 0.18), fill=blue_light)
    # small star in the gem
    pts = []
    for k in range(10):
        a = -math.pi / 2 + k * math.pi / 5
        rr = r * (0.34 if k % 2 == 0 else 0.15)
        pts.append((cx + rr * math.cos(a), cy + r * 0.08 + rr * math.sin(a)))
    d.polygon(pts, fill=CREAM)


def text(img, center, s, size, color, shadow=(60, 10, 10)):
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT, size)
    box = d.textbbox((0, 0), s, font=f)
    tw, th = box[2] - box[0], box[3] - box[1]
    x, y = center[0] - tw // 2 - box[0], center[1] - th // 2 - box[1]
    d.text((x + 3, y + 3), s, font=f, fill=shadow)
    d.text((x, y), s, font=f, fill=color)


out, version = sys.argv[1], sys.argv[2]
bg = background(840, 500, 7)
badge(bg, 420, 175, 95)
text(bg, (420, 365), "Mystery Dungeon", 62, CREAM)
text(bg, (420, 425), "Red Rescue Team  ·  Vita Port " + version, 28, PALE)
bg.quantize(256).save(f"{out}/bg.png", optimize=True)

icon = background(128, 128, 3)
badge(icon, 64, 50, 30)
text(icon, (64, 108), "MD", 30, CREAM)
icon.quantize(256).save(f"{out}/icon0.png", optimize=True)

st = background(280, 158, 5)
badge(st, 44, 72, 27)
text(st, (178, 50), "Mystery Dungeon", 18, CREAM)
text(st, (178, 84), "Start", 20, PALE)
text(st, (178, 124), "v" + version, 14, PALE)
st.quantize(256).save(f"{out}/startup.png", optimize=True)
print("livearea art written to", out)
