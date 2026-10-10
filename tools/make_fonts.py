"""Gera as fontes da tela (formato VLW, com acentos e suavizadas) em assets/.

uso: python tools/make_fonts.py
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONTS = Path("C:/Windows/Fonts")
OUT = Path(__file__).resolve().parent.parent / "assets"

LATIN = "".join(chr(c) for c in range(0x21, 0x7F)) + "".join(chr(c) for c in range(0xA1, 0x100))
NUMS = "0123456789:%-.,°"


def build(name, ttf, px, chars, top_ref, bottom_ref):
    font = ImageFont.truetype(str(FONTS / ttf), px)
    glyphs = []
    for ch in sorted(set(chars)):
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = x1 - x0, y1 - y0
        if w <= 0 or h <= 0:
            continue
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        glyphs.append((ord(ch), h, w, round(font.getlength(ch)), -y0, x0, img.tobytes()))

    ascent = -font.getbbox(top_ref, anchor="ls")[1]
    descent = max(font.getbbox(bottom_ref, anchor="ls")[3], 0)
    data = struct.pack(">6I", len(glyphs), 11, px, 0, ascent, descent)
    for code, h, w, adv, dy, dx, _ in glyphs:
        data += struct.pack(">7i", code, h, w, adv, dy, dx, 0)
    for g in glyphs:
        data += g[6]
    OUT.mkdir(exist_ok=True)
    (OUT / f"{name}.vlw").write_bytes(data)
    print(f"{name}: {len(glyphs)} glifos, {len(data)} bytes, ascent={ascent} descent={descent}")


build("small", "seguisb.ttf", 16, LATIN, "d", "p")
build("medium", "seguisb.ttf", 22, LATIN, "d", "p")
build("num", "segoeuib.ttf", 34, NUMS, "0", "0")
build("big", "segoeuib.ttf", 62, NUMS, "0", "0")
