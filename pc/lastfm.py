"""Le no Last.fm o que a conta esta ouvindo agora, em qualquer aparelho
(celular, TV, outro PC). Serve para Spotify sem Premium: basta ligar o Spotify
ao Last.fm e preencher pc/lastfm.json com o usuario e a chave da API.
"""
import json
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

CONFIG = Path(__file__).with_name("lastfm.json")
API = "https://ws.audioscrobbler.com/2.0/"
POLL_SECONDS = 5
PLACEHOLDER = "2a96cbd8b46e442fc41c2b86b821562f"  # imagem que o Last.fm manda quando nao tem capa


def _download(url):
    """Bytes da imagem, ou None se nao existir (sem capa nao e motivo para perder a musica)."""
    if not url:
        return None
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 bichinho"})
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.read()
    except OSError:
        return None


def _itunes_cover(artist, title):
    """O Last.fm muitas vezes nao tem a capa: procura na loja da Apple, que nao pede chave."""
    # "Musica (feat. X) - Remaster" atrapalha a busca: fica so o nome principal
    plain = re.split(r"[(\[]| - ", title)[0].strip()
    query = urllib.parse.urlencode({"term": f"{artist} {plain}", "entity": "song", "limit": 1})
    try:
        with urllib.request.urlopen("https://itunes.apple.com/search?" + query, timeout=10) as r:
            results = json.load(r)["results"]
        return results[0]["artworkUrl100"].replace("100x100", "300x300") if results else None
    except (OSError, ValueError, KeyError):
        return None


class LastFm:
    def __init__(self, user, api_key):
        self.url = API + "?" + urllib.parse.urlencode(
            {"method": "user.getrecenttracks", "user": user, "api_key": api_key, "format": "json", "limit": 1}
        )
        self.next_poll, self.last = 0.0, None
        self.cover_url, self.cover = None, None

    @staticmethod
    def load():
        """Conta configurada, ou None se o arquivo ainda nao foi preenchido."""
        try:
            cfg = json.loads(CONFIG.read_text(encoding="utf-8"))
            user, key = cfg["user"].strip(), cfg["api_key"].strip()
        except (OSError, ValueError, KeyError, AttributeError):
            return None
        if not user or not key or "COLE" in user or "COLE" in key:
            return None
        return LastFm(user, key)

    def _cover(self, url, artist, title):
        key = (url, artist, title)
        if key != self.cover_url:
            self.cover_url = key
            if url and PLACEHOLDER in url:
                url = None  # estrela cinza generica do Last.fm: nao e capa de verdade
            self.cover = _download(url) or _download(_itunes_cover(artist, title))
        return self.cover

    def now_playing(self):
        """(tocando, titulo, artista, bytes da capa ou None); None se nao deu para consultar."""
        if time.time() < self.next_poll:
            return self.last
        self.next_poll = time.time() + POLL_SECONDS
        try:
            with urllib.request.urlopen(self.url, timeout=10) as r:
                tracks = json.load(r)["recenttracks"]["track"]
            track = tracks[0] if isinstance(tracks, list) and tracks else tracks
            if not track:
                self.last = (False, "", "", None)
            else:
                playing = track.get("@attr", {}).get("nowplaying") == "true"
                images = [i.get("#text") for i in track.get("image", []) if i.get("#text")]
                title, artist = track.get("name", ""), track.get("artist", {}).get("#text", "")
                cover = self._cover(images[-1] if images else None, artist, title) if playing else None
                self.last = (playing, title, artist, cover)
        except urllib.error.HTTPError as e:
            if e.code == 429:
                self.next_poll = time.time() + 60
            self.last = None
        except (OSError, ValueError, KeyError, IndexError, AttributeError):
            self.last = None
        return self.last


if __name__ == "__main__":
    account = LastFm.load()
    if not account:
        raise SystemExit("Preencha pc/lastfm.json com o usuario e a chave da API do Last.fm.")
    result = account.now_playing()
    if result is None:
        raise SystemExit("Nao consegui consultar o Last.fm: confira o usuario e a chave.")
    print("Tocando agora:" if result[0] else "Nada tocando; ultima musica:", result[1], "-", result[2])
    print("Capa:", f"{len(result[3])} bytes" if result[3] else "sem capa")
