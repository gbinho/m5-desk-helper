"""Desenha dois GIFs de exemplo (originais, feitos aqui) em gifs/, so para a
tela de "comecou a tocar" ter o que mostrar. Troque pelos seus quando quiser.

uso: python tools/draw_sample_gifs.py
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parent.parent / "gifs"
S, FRAMES = 200, 16
TAU = math.tau


def save(name, frames, ms):
    frames[0].save(OUT / name, save_all=True, append_images=frames[1:], duration=ms, loop=0)
    print("ok", name)


def dancer():
    frames = []
    for f in range(FRAMES):
        p = f / FRAMES * TAU
        img = Image.new("RGB", (S, S), (0, 0, 0))
        d = ImageDraw.Draw(img)
        for i, color in enumerate([(255, 120, 160), (80, 170, 255), (255, 205, 50)]):  # luzes no chao
            x = 40 + i * 60 + 10 * math.sin(p + i)
            d.ellipse((x - 26, 172, x + 26, 186), fill=tuple(c // 3 for c in color))
        hip = (100 + 14 * math.sin(p), 118 - 5 * abs(math.sin(p)))
        neck = (100 + 6 * math.sin(p + 0.6), 70 - 7 * abs(math.sin(p)))
        head = (neck[0] + 3 * math.sin(p), neck[1] - 20)
        body = (255, 205, 50)

        def limb(a, length, angle, bend):
            mid = (a[0] + length * math.sin(angle), a[1] + length * math.cos(angle))
            end = (mid[0] + length * math.sin(angle + bend), mid[1] + length * math.cos(angle + bend))
            d.line([a, mid, end], fill=body, width=9, joint="curve")
            d.ellipse((end[0] - 6, end[1] - 6, end[0] + 6, end[1] + 6), fill=body)

        limb(hip, 26, 0.35 + 0.3 * math.sin(p), -0.5 - 0.3 * math.sin(p))       # pernas
        limb(hip, 26, -0.35 + 0.3 * math.sin(p), 0.5 - 0.3 * math.sin(p))
        limb(neck, 22, 2.2 + 0.7 * math.sin(2 * p), 0.9 * math.cos(2 * p))      # bracos
        limb(neck, 22, -2.2 + 0.7 * math.sin(2 * p), 0.9 * math.cos(2 * p))
        d.line([hip, neck], fill=body, width=11)
        d.ellipse((head[0] - 15, head[1] - 15, head[0] + 15, head[1] + 15), fill=body)
        frames.append(img)
    save("exemplo-dancarino.gif", frames, 70)


def vinyl():
    frames = []
    for f in range(FRAMES):
        a = f / FRAMES * TAU
        img = Image.new("RGB", (S, S), (0, 0, 0))
        d = ImageDraw.Draw(img)
        c = S / 2
        d.ellipse((c - 88, c - 88, c + 88, c + 88), fill=(28, 28, 34))
        for r in range(44, 86, 7):
            d.ellipse((c - r, c - r, c + r, c + r), outline=(52, 52, 62))
        for k in range(2):  # brilho girando
            start = math.degrees(a) + k * 180
            d.pieslice((c - 86, c - 86, c + 86, c + 86), start, start + 22, fill=(70, 70, 84))
        d.ellipse((c - 34, c - 34, c + 34, c + 34), fill=(255, 120, 160))
        mx, my = c + 20 * math.cos(a), c + 20 * math.sin(a)
        d.ellipse((mx - 6, my - 6, mx + 6, my + 6), fill=(255, 235, 150))
        d.ellipse((c - 5, c - 5, c + 5, c + 5), fill=(0, 0, 0))
        frames.append(img)
    save("exemplo-vinil.gif", frames, 60)


OUT.mkdir(exist_ok=True)
dancer()
vinyl()
