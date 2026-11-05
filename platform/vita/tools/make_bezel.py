#!/usr/bin/env python3
"""Generates platform/vita/assets/bezel.png, the 9-slice bezel the Vita port
draws around the game image when it has borders ("Rahmen: Bezel").

Layout (BORDER = 48 px, CENTER = 16 px, image 112x112): the centre square is
the screen opening (transparent, never drawn); the eight pieces around it are
the bezel. The port draws the corners at 1:1 at the corners of the game
rectangle and stretches the edge pieces along the game's edges, so the ring
is BORDER pixels wide on screen whatever the view mode. The rest of the screen
is filled with SHELL, the flat colour the ring fades into.

From the opening outwards: a black glass margin with rounded outer corners
(the LCD sits in a larger glass window, as on a GBA SP), a lit lip, a bevel
sloping into the shell. Light comes from the top left: the bottom and right
lips are brighter, the top and left ones darker. Own drawing, no artwork.

    python3 platform/vita/tools/make_bezel.py [out.png]
"""
import math
import sys

from PIL import Image

BORDER = 48
CENTER = 16
SIZE = 2 * BORDER + CENTER

SHELL = (40, 41, 46)        # flat shell colour (vita2d fills the screen with it)
GLASS = (6, 6, 8)           # black glass around the LCD
GLASS_W = 8                 # width of the glass margin
GLASS_R = 14                # corner radius of the glass window
LIP_W = 3.0                 # bright edge of the bevel
BEVEL_W = 24.0              # slope from the lip into the shell
LIGHT = (-0.6, -0.8)        # light direction (towards top left)


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def rect_distance(px, py, x0, y0, x1, y1):
    """Euclidean distance from a pixel centre to the (closed) rectangle; the
    outward normal of the nearest point. Contours are rounded rectangles."""
    dx = x0 - px if px < x0 else (px - x1 if px > x1 else 0.0)
    dy = y0 - py if py < y0 else (py - y1 if py > y1 else 0.0)
    d = math.hypot(dx, dy)
    if d == 0:
        return 0.0, 0.0, 0.0
    sx = -1.0 if px < x0 else (1.0 if px > x1 else 0.0)
    sy = -1.0 if py < y0 else (1.0 if py > y1 else 0.0)
    return d, sx * abs(dx) / d, sy * abs(dy) / d


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "platform/vita/assets/bezel.png"
    img = Image.new("RGBA", (SIZE, SIZE), SHELL + (255,))
    px = img.load()
    # the glass window: rounded rectangle of radius GLASS_R, GLASS_W outside
    # the opening -> distance <= GLASS_R to the opening shrunk by GLASS_R - GLASS_W
    shrink = GLASS_R - GLASS_W
    x0 = BORDER + shrink - 0.5
    y0 = x0
    x1 = BORDER + CENTER - 1 - shrink + 0.5
    y1 = x1
    for y in range(SIZE):
        for x in range(SIZE):
            if BORDER <= x < BORDER + CENTER and BORDER <= y < BORDER + CENTER:
                px[x, y] = (0, 0, 0, 0)  # the opening (never drawn)
                continue
            d, nx, ny = rect_distance(x + 0.5, y + 0.5, x0, y0, x1, y1)
            e = d - GLASS_R  # 0 at the glass edge
            if e <= 0:
                col = GLASS
            else:
                # lit amount of a lip sloping towards the opening (normal -n)
                lit = -(nx * LIGHT[0] + ny * LIGHT[1])  # -1 top/left .. +1 bottom/right
                lip = lerp((150, 152, 160), (225, 227, 235), (lit + 1) / 2)
                bevel = lerp((78, 79, 86), (122, 124, 132), (lit + 1) / 2)
                if e < LIP_W:
                    t = smoothstep(e / LIP_W)
                    col = lerp(lip, bevel, t)
                    # anti-aliased edge against the glass
                    col = lerp(GLASS, col, min(1.0, e))
                else:
                    t = smoothstep((e - LIP_W) / BEVEL_W)
                    col = lerp(bevel, SHELL, t)
            px[x, y] = col + (255,)
    img.save(out, optimize=True)
    print("wrote", out, img.size)


if __name__ == "__main__":
    main()
