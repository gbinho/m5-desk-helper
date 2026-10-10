"""Prepara os GIFs da pasta gifs/ para o aparelho: recorta em quadrado, reduz,
limita quadros e cores, e junta tudo em assets/gifs.pack (vai dentro do firmware).

uso: python tools/make_gifs.py     (depois grave o firmware de novo)
"""
import io
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageSequence

ROOT = Path(__file__).resolve().parent.parent
SIZE = 100          # lado em pixels; igual a gifs::SIZE no firmware
STILL_BELOW = 22    # diferenca de cor (0-255) abaixo da qual o pixel conta como parado
BUDGET = 1_300_000  # bytes que sobram no firmware com folga
# (quadros, cores): se nao couber tudo, tenta de novo com menos. A ordem
# sacrifica cor antes de quadro, porque e a quantidade de quadros que da fluidez.
QUALITY = [(q, c) for q in (160, 120, 96, 80, 64, 56, 48, 40, 32, 28, 24, 20, 16, 12) for c in (96, 64, 48, 32, 24)]
QUALITY.sort(key=lambda qc: (-qc[0], -qc[1]))


def convert(path, max_frames, colors):
    src = Image.open(path)
    frames, durations = [], []
    for frame in ImageSequence.Iterator(src):
        rgba = frame.convert("RGBA")
        full = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
        full.alpha_composite(rgba)  # fundo transparente vira preto
        side = min(full.size)
        left, top = (full.width - side) // 2, (full.height - side) // 2
        full = full.crop((left, top, left + side, top + side)).resize((SIZE, SIZE), Image.LANCZOS)
        frames.append(full.convert("RGB"))
        durations.append(frame.info.get("duration", 80) or 80)

    # GIF comprido: escolhe quadros espalhados por igual e soma os tempos dos pulados
    n = min(len(frames), max_frames)
    cuts = [round(i * len(frames) / n) for i in range(n + 1)]
    kept = [frames[cuts[i]] for i in range(n)]
    times = [max(sum(durations[cuts[i] : cuts[i + 1]]), 20) for i in range(n)]

    # Video tem um chuvisco que muda todo pixel a cada quadro e faz o arquivo
    # inchar. Onde a mudanca e pequena, repete o pixel do quadro anterior: o GIF
    # passa a guardar so o que se mexeu de verdade, e sobra espaco para mais quadros.
    still = np.asarray(kept[0], dtype=np.int16)
    for i in range(1, len(kept)):
        cur = np.asarray(kept[i], dtype=np.int16)
        same = np.abs(cur - still).max(axis=2) < STILL_BELOW
        still = np.where(same[..., None], still, cur)
        kept[i] = Image.fromarray(still.astype(np.uint8), "RGB")

    # uma paleta so para o GIF inteiro, tirada de todos os quadros juntos
    strip = Image.new("RGB", (SIZE * len(kept), SIZE))
    for i, f in enumerate(kept):
        strip.paste(f, (i * SIZE, 0))
    palette = strip.quantize(colors, method=Image.MEDIANCUT)
    kept = [f.quantize(palette=palette, dither=Image.NONE) for f in kept]

    out = io.BytesIO()
    kept[0].save(out, "GIF", save_all=True, append_images=kept[1:], duration=times, loop=0, disposal=1)
    return out.getvalue(), len(kept)


def main():
    paths = sorted((ROOT / "gifs").glob("*.gif"))
    for max_frames, colors in QUALITY:
        packed, names = [], []
        for path in paths:
            try:
                data, n = convert(path, max_frames, colors)
            except Exception as e:
                print(f"pulei {path.name}: {e}")
                continue
            packed.append(data)
            names.append(f"{path.name}: {n} quadros, {len(data)} bytes")
        if sum(len(d) for d in packed) <= BUDGET:
            break
    else:
        print("nem na menor qualidade coube tudo: tire alguns GIFs da pasta")
        while sum(len(d) for d in packed) > BUDGET:
            packed.pop()
            print("ficou de fora:", names.pop())
    print(f"qualidade usada: ate {max_frames} quadros, {colors} cores")
    for line in names:
        print(line)

    # cabecalho: quantidade, depois (posicao, tamanho) de cada um
    header = struct.pack("<I", len(packed))
    offset = 4 + 8 * len(packed)
    for data in packed:
        header += struct.pack("<II", offset, len(data))
        offset += len(data)
    (ROOT / "assets" / "gifs.pack").write_bytes(header + b"".join(packed))
    print(f"total: {len(packed)} GIFs, {offset} bytes")


if __name__ == "__main__":
    main()
