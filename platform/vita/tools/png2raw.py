#!/usr/bin/env python3
"""Converts a PNG into the raw RGBA texture the Vita port loads at runtime
(the bootstrapped VitaSDK has no libpng, so vita2d cannot read PNGs there).

Format: 'PRAW', u32 width, u32 height (little endian), then width*height
pixels of R, G, B, A bytes, row by row - the layout of a vita2d
U8U8U8U8_ABGR texture, so the rows are copied straight into it.

    python3 png2raw.py in.png out.raw [WIDTHxHEIGHT]

An optional size scales the image (the LiveArea background is stored at half
the screen size: it is drawn darkened behind the game anyway).
"""
import struct
import sys

from PIL import Image


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    img = Image.open(sys.argv[1]).convert("RGBA")
    if len(sys.argv) > 3:
        w, h = (int(v) for v in sys.argv[3].lower().split("x"))
        img = img.resize((w, h), Image.LANCZOS)
    with open(sys.argv[2], "wb") as f:
        f.write(b"PRAW" + struct.pack("<II", img.width, img.height))
        f.write(img.tobytes("raw", "RGBA"))
    print("wrote %s (%dx%d)" % (sys.argv[2], img.width, img.height))


if __name__ == "__main__":
    main()
