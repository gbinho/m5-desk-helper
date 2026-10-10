"""Ligacao direta com a conta do Spotify, para o bichinho mostrar o que toca em
qualquer aparelho (celular, TV, outro PC).

Configurar (uma vez so):  python pc/spotify.py <Client ID do app criado em developer.spotify.com>
Abre o navegador para voce autorizar e guarda a chave de acesso em pc/spotify.json.
Depois disso o companion.py usa sozinho.
"""
import base64
import hashlib
import json
import secrets
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

CONFIG = Path(__file__).with_name("spotify.json")
PORT = 8888
REDIRECT = f"http://127.0.0.1:{PORT}/callback"
SCOPE = "user-read-currently-playing user-read-playback-state"  # so leitura
TOKEN_URL = "https://accounts.spotify.com/api/token"
PLAYING_URL = "https://api.spotify.com/v1/me/player/currently-playing?additional_types=episode"
POLL_SECONDS = 3


def _post_token(fields):
    req = urllib.request.Request(TOKEN_URL, data=urllib.parse.urlencode(fields).encode())
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r)


class Spotify:
    def __init__(self, cfg):
        self.client_id, self.refresh = cfg["client_id"], cfg["refresh_token"]
        self.access, self.expires = None, 0.0
        self.next_poll, self.last = 0.0, None
        self.cover_url, self.cover = None, None

    @staticmethod
    def load():
        """Conta configurada, ou None se o login ainda nao foi feito."""
        try:
            return Spotify(json.loads(CONFIG.read_text()))
        except (OSError, ValueError, KeyError):
            return None

    def _token(self):
        if self.access and time.time() < self.expires - 30:
            return self.access
        tok = _post_token(
            {"grant_type": "refresh_token", "refresh_token": self.refresh, "client_id": self.client_id}
        )
        self.access, self.expires = tok["access_token"], time.time() + tok.get("expires_in", 3600)
        if tok.get("refresh_token") and tok["refresh_token"] != self.refresh:
            # o Spotify troca a chave de vez em quando: guarda a nova
            self.refresh = tok["refresh_token"]
            CONFIG.write_text(json.dumps({"client_id": self.client_id, "refresh_token": self.refresh}))
        return self.access

    def _cover(self, url):
        if url != self.cover_url:
            self.cover_url, self.cover = url, None
            if url:
                with urllib.request.urlopen(url, timeout=10) as r:
                    self.cover = r.read()
        return self.cover

    def now_playing(self):
        """(tocando, titulo, artista, bytes da capa ou None); None se nao deu para consultar."""
        if time.time() < self.next_poll:
            return self.last
        self.next_poll = time.time() + POLL_SECONDS
        try:
            req = urllib.request.Request(PLAYING_URL, headers={"Authorization": f"Bearer {self._token()}"})
            with urllib.request.urlopen(req, timeout=10) as r:
                data = json.load(r) if r.status == 200 else None
            item = data and data.get("item")
            if not item:
                self.last = (False, "", "", None)
            else:
                if item.get("type") == "episode":
                    artist, images = item.get("show", {}).get("name", ""), item.get("images", [])
                else:
                    artist = ", ".join(a["name"] for a in item.get("artists", []))
                    images = item.get("album", {}).get("images", [])
                # a menor imagem que ainda seja maior que a tela
                images = sorted(images, key=lambda i: i.get("width") or 0)
                url = next((i["url"] for i in images if (i.get("width") or 0) >= 150), None)
                url = url or (images[-1]["url"] if images else None)
                self.last = (bool(data.get("is_playing")), item.get("name", ""), artist, self._cover(url))
        except urllib.error.HTTPError as e:
            if e.code == 429:  # pediu demais: espera o que o Spotify mandar
                self.next_poll = time.time() + int(e.headers.get("Retry-After", 30))
            elif e.code in (400, 401):
                self.access = None
            self.last = None
        except (OSError, ValueError, KeyError):
            self.last = None
        return self.last


def login(client_id):
    verifier = secrets.token_urlsafe(64)
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
    state = secrets.token_urlsafe(16)
    result = {}

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            if query.get("state", [""])[0] == state:
                result.update(code=query.get("code", [None])[0], error=query.get("error", [None])[0])
            ok = bool(result.get("code"))
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(
                ("<h2>Pronto! Pode fechar esta aba.</h2>" if ok else "<h2>Não autorizado.</h2>").encode()
            )

        def log_message(self, *args):
            pass

    server = HTTPServer(("127.0.0.1", PORT), Handler)
    server.timeout = 300
    url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode(
        {
            "client_id": client_id,
            "response_type": "code",
            "redirect_uri": REDIRECT,
            "scope": SCOPE,
            "code_challenge_method": "S256",
            "code_challenge": challenge,
            "state": state,
        }
    )
    print("Abrindo o navegador para voce autorizar...")
    webbrowser.open(url)
    while "code" not in result:
        server.handle_request()
        if not result:
            sys.exit("Tempo esgotado esperando a autorizacao.")
    server.server_close()
    if not result["code"]:
        sys.exit(f"Autorizacao recusada: {result['error']}")

    tok = _post_token(
        {
            "grant_type": "authorization_code",
            "code": result["code"],
            "redirect_uri": REDIRECT,
            "client_id": client_id,
            "code_verifier": verifier,
        }
    )
    CONFIG.write_text(json.dumps({"client_id": client_id, "refresh_token": tok["refresh_token"]}))
    print("Conta ligada. Testando...")
    print("Tocando agora:", Spotify.load().now_playing()[:3])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    login(sys.argv[1])
