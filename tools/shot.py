"""Tira capturas da tela do bichinho pela serial e monta uma folha com todas.

uso: python tools/shot.py saida.png "rotulo=cmd1;cmd2@segundos" ...
  ex: python tools/shot.py tmp/telas.png "relogio=scene 1@1.5" "tonto=expr 3@0.5"
Cada passo manda os comandos, espera e captura. Feche o companion antes.
"""
import sys
import time

import serial
from PIL import Image, ImageDraw

PORT = "COM3"
SCALE = 2
COLS = 3


def capture(s):
    s.reset_input_buffer()
    s.write(b"shot\n")
    deadline = time.time() + 5
    while time.time() < deadline:
        line = s.readline()
        if line.startswith(b"SHOT "):
            w, h, n = map(int, line.split()[1:4])
            s.timeout = 2 + n / 8000  # tela cheia de detalhes demora mais para chegar
            data = s.read(n)
            s.timeout = 2
            if len(data) != n:
                raise RuntimeError("captura incompleta")
            px = bytearray()
            for i in range(0, n, 3):
                v = (data[i + 1] << 8) | data[i + 2]
                rgb = bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
                px += rgb * data[i]
            return Image.frombytes("RGB", (w, h), bytes(px[: w * h * 3]))
    raise RuntimeError("o aparelho nao respondeu")


def main():
    out, steps = sys.argv[1], sys.argv[2:]
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = PORT, 115200, 2
    s.dtr = s.rts = False  # nao reinicia o aparelho
    s.open()
    time.sleep(0.3)

    shots = []
    for step in steps:
        label, _, rest = step.partition("=")
        cmds, _, wait = rest.partition("@")
        for cmd in filter(None, cmds.split(";")):
            s.write(cmd.strip().encode("utf-8") + b"\n")
            time.sleep(0.05)
        time.sleep(float(wait or 1))
        shots.append((label, capture(s)))
    s.close()

    w, h = shots[0][1].size
    cw, ch = w * SCALE + 8, h * SCALE + 22
    rows = (len(shots) + COLS - 1) // COLS
    sheet = Image.new("RGB", (cw * min(COLS, len(shots)), ch * rows), (40, 40, 48))
    draw = ImageDraw.Draw(sheet)
    for i, (label, img) in enumerate(shots):
        x, y = (i % COLS) * cw + 4, (i // COLS) * ch
        draw.text((x, y + 4), label, fill=(255, 255, 255))
        sheet.paste(img.resize((w * SCALE, h * SCALE), Image.NEAREST), (x, y + 18))
    sheet.save(out)
    print("ok", out)


if __name__ == "__main__":
    main()
