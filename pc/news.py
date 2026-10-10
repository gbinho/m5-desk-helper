"""Mini noticiario do bichinho: pega as manchetes principais do Google Noticias
(Brasil) e de tempos em tempos manda um plantao com tres delas.
"""
import time
import urllib.request
import xml.etree.ElementTree as ET

FEED = "https://news.google.com/rss?hl=pt-BR&gl=BR&ceid=BR:pt-419"
FETCH_EVERY = 30 * 60      # busca manchetes novas
BULLETIN_EVERY = 45 * 60   # intervalo entre um plantao e outro
FIRST_BULLETIN = 10 * 60   # o primeiro, depois que o programa liga
SLOTS = 5                  # manchetes guardadas no aparelho (o plantao mostra as 3 primeiras)
MAX_TITLE = 110            # o que cabe na tela


def fetch():
    """Lista de (fonte, manchete), da mais para a menos importante."""
    req = urllib.request.Request(FEED, headers={"User-Agent": "Mozilla/5.0 bichinho"})
    with urllib.request.urlopen(req, timeout=10) as r:
        root = ET.fromstring(r.read())
    out = []
    for item in root.iter("item"):
        title = (item.findtext("title") or "").strip()
        source = (item.findtext("source") or "").strip()
        # o titulo vem como "Manchete - Fonte"
        if source and title.endswith(" - " + source):
            title = title[: -len(source) - 3]
        elif " - " in title and not source:
            title, source = title.rsplit(" - ", 1)
        if len(title) > MAX_TITLE:
            title = title[: MAX_TITLE - 3].rsplit(" ", 1)[0] + "..."
        if title:
            out.append((source[:24], title))
    return out


class News:
    def __init__(self):
        self.headlines = []
        self.shown = set()      # manchetes que ja passaram num plantao
        self.next_fetch = 0.0
        self.next_bulletin = time.time() + FIRST_BULLETIN
        self.dirty = False      # ha manchetes novas para mandar ao aparelho

    def resend(self):
        self.dirty = bool(self.headlines)

    def poll(self, can_talk):
        """Linhas para o aparelho. can_talk = o dono esta por perto e nao esta em chamada."""
        now = time.time()
        if now >= self.next_fetch:
            try:
                self.headlines = fetch()
                self.next_fetch = now + FETCH_EVERY
                self.dirty = True
            except Exception:
                self.next_fetch = now + 5 * 60  # sem internet agora: tenta de novo em 5 min
        out = []
        # as ainda nao mostradas vem primeiro; a ordem do site (importancia) se mantem
        ordered = sorted(self.headlines, key=lambda h: h[1] in self.shown)[:SLOTS]
        if self.dirty and ordered:
            self.dirty = False
            out += [f"news {i} {src}\t{title}\n" for i, (src, title) in enumerate(ordered)]
        if can_talk and now >= self.next_bulletin and ordered and ordered[0][1] not in self.shown:
            self.next_bulletin = now + BULLETIN_EVERY
            out.append("bulletin\n")
            self.shown.update(title for _, title in ordered[:3])
            self.dirty = True  # da proxima vez o aparelho recebe as seguintes
        return out


if __name__ == "__main__":
    for source, title in fetch()[:8]:
        print(f"[{source}] {title}")
