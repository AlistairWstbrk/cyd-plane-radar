#!/usr/bin/env python3
"""Generate the UI fonts as VLW files at their native pixel sizes.

The first version scaled one 15 px VLW font to every size, which blurs it.
Each UI size now gets its own hinted rasterisation (Noto Sans, OFL), so
glyph stems land on whole pixels and the text stays sharp.

Usage: python scripts/build_fonts.py [--preview out.png]
Needs Pillow. Writes data/font_<name>.vlw.
"""
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "fonts_src")
OUT = os.path.join(ROOT, "data")

# name, weight file, pixel size, alpha gamma (<1 darkens edge pixels)
FONTS = [
    ("xs", "NotoSans-SemiBold.ttf", 11, 0.80),
    ("s", "NotoSans-SemiBold.ttf", 13, 0.85),
    ("m", "NotoSans-Bold.ttf", 15, 0.90),
    ("l", "NotoSans-Bold.ttf", 22, 0.95),
    ("xl", "NotoSans-Bold.ttf", 32, 1.0),
]

CHARS = [chr(c) for c in range(0x20, 0x7F)] + ["°", "·"]


def glyph(font, ch, gamma):
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    adv = int(round(font.getlength(ch)))
    w, h = max(0, x1 - x0), max(0, y1 - y0)
    if ch == " " or w == 0 or h == 0:
        return dict(code=ord(ch), w=0, h=0, adv=adv, top=0, left=0, bmp=b"")
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-x0, -y0), ch, fill=255, font=font, anchor="ls")
    if gamma != 1.0:
        lut = [min(255, int(round(255 * (i / 255) ** gamma))) for i in range(256)]
        img = img.point(lut)
    return dict(code=ord(ch), w=w, h=h, adv=adv, top=-y0, left=x0, bmp=img.tobytes())


def build(name, ttf, px, gamma):
    font = ImageFont.truetype(os.path.join(SRC, ttf), px)
    glyphs = [glyph(font, ch, gamma) for ch in CHARS]
    glyphs.sort(key=lambda g: g["code"])
    ascent = -font.getbbox("d", anchor="ls")[1]
    descent = font.getbbox("p", anchor="ls")[3]
    out = bytearray(struct.pack(">6i", len(glyphs), 11, px, 0, ascent, descent))
    for g in glyphs:
        out += struct.pack(">7i", g["code"], g["h"], g["w"], g["adv"], g["top"], g["left"], 0)
    for g in glyphs:
        out += g["bmp"]
    fam = f"NotoSans-{px}".encode()
    out += struct.pack(">B", len(fam)) + fam + struct.pack(">B", len(fam)) + fam + b"\x01"
    path = os.path.join(OUT, f"font_{name}.vlw")
    with open(path, "wb") as f:
        f.write(out)
    return path, glyphs, font


def preview(path_png, fonts):
    sample = "UAL1234 B738 FL350 12.5km 438kt KTPA Tampa Intl 13:45 -+"
    rows = []
    for name, (font, gamma) in fonts.items():
        img = Image.new("L", (330, font.size + 8), 0)
        d = ImageDraw.Draw(img)
        d.text((4, 4 + font.size), sample, fill=255, font=font, anchor="ls")
        if gamma != 1.0:
            img = img.point([min(255, int(round(255 * (i / 255) ** gamma))) for i in range(256)])
        rows.append(img)
    hh = sum(r.height for r in rows)
    sheet = Image.new("L", (330, hh))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y))
        y += r.height
    sheet = sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST)
    sheet.save(path_png)


def main():
    made = {}
    for name, ttf, px, gamma in FONTS:
        path, glyphs, font = build(name, ttf, px, gamma)
        made[name] = (font, gamma)
        print(f"{os.path.relpath(path, ROOT)}: {len(glyphs)} glyphs, {os.path.getsize(path)} bytes")
    if "--preview" in sys.argv:
        preview(sys.argv[sys.argv.index("--preview") + 1], made)


if __name__ == "__main__":
    main()
