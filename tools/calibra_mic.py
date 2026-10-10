"""Calibracao do microfone: o aparelho mostra na tela o que fazer em cada etapa
(silencio, falar, palmas, soprar, assobiar) enquanto este programa anota o que
o microfone mediu. O resumo no final serve para acertar os limites em ears.cpp.

uso: python tools/calibra_mic.py     (feche o programinha do PC antes)
"""
import re
import statistics
import time

import serial

PORT = "COM3"
STEPS = [
    ("silencio", "Fica em silêncio", 6),
    ("fala", "Fala normalmente", 7),
    ("palmas", "Bate palmas 3 vezes", 7),
    ("sopro", "Sopra em mim!", 7),
    ("assobio", "Assobia uma melodia", 9),
]
FIELDS = re.compile(r"nivel=(\d+) fundo=(\d+) tom=(\d+)Hz limpo=(\d) grave=([\d.]+) voz=([\d.]+) sopro=(\d) assobio=(\d)")


def main():
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = PORT, 115200, 0.05
    s.dtr = s.rts = False
    s.open()
    time.sleep(0.3)
    s.write(b"scene 0\n")
    results = {}
    for key, prompt, seconds in STEPS:
        s.write(f"say {prompt}\n".encode("utf-8"))
        time.sleep(1.5)  # tempo de ler
        s.reset_input_buffer()
        rows, end = [], time.time() + seconds
        while time.time() < end:
            s.write(b"mic\n")
            time.sleep(0.07)
            for line in s.read(600).decode("ascii", "replace").splitlines():
                m = FIELDS.search(line)
                if m:
                    rows.append([float(v) for v in m.groups()])
        results[key] = rows
    s.write(b"say Pronto, valeu!\n")
    s.close()

    for key, rows in results.items():
        if not rows:
            print(key, "sem leituras")
            continue
        level = sorted(r[0] for r in rows)
        loud = [r for r in rows if r[0] >= level[int(len(level) * 0.75)]]  # o quarto mais forte da etapa
        print(f"{key:9s} n={len(rows):3d} nivel med={statistics.median(level):6.0f} p90={level[int(len(level) * 0.9)]:6.0f} "
              f"max={level[-1]:6.0f} | fundo={rows[-1][1]:4.0f} | nos fortes: grave={statistics.mean(r[4] for r in loud):.2f} "
              f"limpo={statistics.mean(r[3] for r in loud):.2f} tom={statistics.median(r[2] for r in loud):5.0f}Hz "
              f"| sopro={sum(r[6] for r in rows):3.0f} assobio={sum(r[7] for r in rows):3.0f}")


if __name__ == "__main__":
    main()
