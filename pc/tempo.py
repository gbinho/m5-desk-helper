"""Descobre o andamento (batidas por minuto) de uma musica, para o bichinho
balancar no ritmo. Usa o catalogo aberto da Deezer, que nao pede cadastro: se
ela ja souber o andamento, usa; se nao, baixa a amostra de 30 s e mede.
"""
import json
import re
import urllib.parse
import urllib.request

import miniaudio
import numpy as np

RATE, FRAME, HOP = 11025, 512, 128
_cache = {}


def _get(url, raw=False):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 bichinho"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read() if raw else json.load(r)


def measure(mp3):
    """Andamento medido no audio: acha as batidas e o intervalo que mais se repete entre elas."""
    pcm = miniaudio.decode(mp3, output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=RATE)
    x = np.frombuffer(pcm.samples, dtype=np.int16).astype(np.float32) / 32768
    if len(x) < RATE * 8:
        return 0
    # "novidade" a cada instante: quanto o som ficou mais forte em cada faixa de frequencia
    frames = np.lib.stride_tricks.sliding_window_view(x, FRAME)[::HOP] * np.hanning(FRAME)
    mag = np.log1p(40 * np.abs(np.fft.rfft(frames, axis=1)))
    onset = np.maximum(mag[1:] - mag[:-1], 0).sum(axis=1)
    onset -= onset.mean()
    # intervalo que mais se repete, entre 70 e 180 bpm, com leve preferencia por ~120
    ac = np.correlate(onset, onset, "full")[len(onset) - 1 :]
    fps = RATE / HOP
    lags = np.arange(int(fps * 60 / 180), int(fps * 60 / 70) + 1)
    bpms = 60 * fps / lags
    score = ac[lags] * np.exp(-0.5 * (np.log2(bpms / 120) / 0.9) ** 2)
    return round(float(bpms[int(np.argmax(score))]))


def bpm(artist, title):
    """Batidas por minuto, ou 0 se a musica nao foi achada."""
    key = (artist, title)
    if key in _cache:
        return _cache[key]
    # "Musica (feat. X) - Remaster" atrapalha a busca: fica so o nome principal
    plain = re.split(r"[(\[]| - ", title)[0].strip()
    first_artist = re.split(r",|&| feat", artist)[0].strip()
    result = 0
    try:
        query = urllib.parse.urlencode({"q": f"{first_artist} {plain}", "limit": 3})
        found = _get("https://api.deezer.com/search?" + query).get("data", [])
        # so vale resultado do mesmo artista
        found = [t for t in found if first_artist.lower() in t["artist"]["name"].lower()] or found[:1]
        for track in found:
            result = round(float(_get(f"https://api.deezer.com/track/{track['id']}").get("bpm") or 0))
            if result:
                break
        if not result and found and found[0].get("preview"):
            result = measure(_get(found[0]["preview"], raw=True))
    except Exception:
        return 0  # falha de rede ou de audio: tenta de novo na proxima
    _cache[key] = result
    return result


if __name__ == "__main__":
    import sys

    tests = [tuple(sys.argv[1:3])] if len(sys.argv) == 3 else [
        ("Daft Punk", "Get Lucky"), ("Michael Jackson", "Billie Jean"), ("The Weeknd", "Blinding Lights"),
        ("Bee Gees", "Stayin' Alive"), ("Queen", "Bohemian Rhapsody - Remastered 2011"),
        ("El Zombi15", "Activo"), ("Kerri Chandler", "You Are In My System (feat. Troy Denari) - Original Deluxe"),
    ]
    for a, t in tests:
        print(f"{a} - {t}: {bpm(a, t)} bpm")
