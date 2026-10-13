"""Programinha do PC: manda para o bichinho, pelo cabo USB, o uso de CPU e
memoria e a musica que esta tocando, com a capa (Spotify no aplicativo ou no
navegador, ou qualquer player que o Windows reconheca).

uso: pythonw pc/companion.py [porta]
Sem porta, procura o M5Stick sozinho. Se a porta estiver ocupada (por exemplo,
durante uma gravacao de firmware), fica tentando de novo. Mostra um icone perto
do relogio do Windows (pausar, religar, sair) e anota erros em pc/companion.log.
"""
import asyncio
import ctypes
import io
import logging
import logging.handlers
import subprocess
import sys
import threading
import time
import unicodedata
from ctypes import wintypes
from pathlib import Path

import serial
from PIL import Image
from serial.tools import list_ports
import activity
import events
import news
import routine
from lastfm import LastFm
from spotify import Spotify
import tempo
from weather import Weather
from winrt.windows.media.control import (
    GlobalSystemMediaTransportControlsSessionManager as MediaManager,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
)
from winrt.windows.storage.streams import Buffer, DataReader, InputStreamOptions
from winrt.windows.ui.notifications import KnownNotificationBindings, NotificationKinds
from winrt.windows.ui.notifications.management import UserNotificationListener

# Notificacoes que viram aviso no bichinho: nome do aplicativo ou site contendo isto
NOTIFY_FROM = ("whatsapp",)
# False = mostra so quem mandou, sem o texto da mensagem
SHOW_MESSAGE_TEXT = True

TICK = 0.1   # segundos por volta: curto, para as patinhas baterem junto com as suas teclas
COVER = 72  # lado da capa na tela; igual a pclink::COVER no firmware
USB_IDS = {(0x0403, 0x6001), (0x1A86, 0x55D4), (0x1A86, 0x7523), (0x10C4, 0xEA60)}


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for p in list_ports.comports():
        if (p.vid, p.pid) in USB_IDS:
            return p.device
    return None


# ---- CPU e memoria, direto da API do Windows ----

class FILETIME(ctypes.Structure):
    _fields_ = [("lo", wintypes.DWORD), ("hi", wintypes.DWORD)]

    def value(self):
        return (self.hi << 32) | self.lo


class MEMORYSTATUSEX(ctypes.Structure):
    _fields_ = [("dwLength", wintypes.DWORD), ("dwMemoryLoad", wintypes.DWORD)] + [
        (n, ctypes.c_ulonglong)
        for n in ("total", "avail", "totalPage", "availPage", "totalVirt", "availVirt", "availExt")
    ]


_last_cpu = None


def cpu_percent():
    global _last_cpu
    idle, kernel, user = FILETIME(), FILETIME(), FILETIME()
    ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user))
    now = (idle.value(), kernel.value() + user.value())  # o tempo de kernel ja inclui o ocioso
    prev, _last_cpu = _last_cpu, now
    if not prev or now[1] == prev[1]:
        return 0.0
    return 100.0 * (1 - (now[0] - prev[0]) / (now[1] - prev[1]))


def ram_percent():
    m = MEMORYSTATUSEX()
    m.dwLength = ctypes.sizeof(m)
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
    return float(m.dwMemoryLoad)


def gpu_stats():
    """' <uso%> <graus>' da placa NVIDIA, ou '' se nao houver nvidia-smi."""
    try:
        out = subprocess.run(
            ["nvidia-smi", "--query-gpu=utilization.gpu,temperature.gpu", "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=3, creationflags=subprocess.CREATE_NO_WINDOW,
        ).stdout
        use, temp = (float(v) for v in out.splitlines()[0].split(","))
        return f" {use:.0f} {temp:.0f}"
    except Exception:
        return ""


class Cooler:
    """Velocidade do cooler da CPU. O Windows nao informa isso sozinho: se o
    LibreHardwareMonitor estiver aberto com o "Remote Web Server" ligado, le a
    rotacao de verdade; sem ele, estima pela carga do processador (o cooler
    acelera e desacelera com um certo atraso em relacao ao uso)."""

    URL = "http://localhost:8085/data.json"

    def __init__(self):
        self.estimate = 30.0
        self.retry_at = 0.0
        self.max_rpm = 1500.0

    def _real_rpm(self):
        if time.time() < self.retry_at:
            return 0
        try:
            import json
            import urllib.request
            with urllib.request.urlopen(self.URL, timeout=0.4) as r:
                tree = json.load(r)
        except Exception:
            self.retry_at = time.time() + 60  # nao esta aberto: confere de novo daqui a 1 min
            return 0
        fans = []

        def walk(node):
            if node.get("Type") == "Fan" or "RPM" in str(node.get("Value", "")):
                try:
                    fans.append((node.get("Text", ""), float(str(node["Value"]).split()[0].replace(",", "."))))
                except (ValueError, IndexError):
                    pass
            for child in node.get("Children", []):
                walk(child)

        walk(tree)
        cpu = [rpm for name, rpm in fans if "cpu" in name.lower() and rpm > 0]
        spinning = cpu or [rpm for _, rpm in fans if rpm > 0]
        return spinning[0] if spinning else 0

    def line(self, cpu):
        rpm = self._real_rpm()
        if rpm:
            self.max_rpm = max(self.max_rpm, rpm)
            return f"fan {100 * rpm / self.max_rpm:.0f} {rpm:.0f}\n".encode()
        self.estimate += (25 + 0.75 * cpu - self.estimate) * 0.08
        return f"fan {self.estimate:.0f} 0\n".encode()


# ---- Musica ----

def clean(s, limit=80):
    """A tela so tem letras latinas: troca o que der e descarta o resto."""
    s = unicodedata.normalize("NFKC", s or "").replace("\t", " ").replace("\n", " ")
    for fancy, plain in (("‘", "'"), ("’", "'"), ("“", '"'), ("”", '"'), ("–", "-"), ("—", "-"), ("…", "...")):
        s = s.replace(fancy, plain)
    return "".join(ch for ch in s if 0x20 <= ord(ch) <= 0xFF).strip()[:limit]


MUSIC, VIDEO, YOUTUBE = 1, 2, 3  # tipo de midia, como vai na linha "np" para o aparelho


def window_titles():
    """Titulos das janelas abertas: e onde o navegador diz em que site esta."""
    titles = []
    proc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def each(hwnd, _):
        n = ctypes.windll.user32.GetWindowTextLengthW(hwnd)
        if n and ctypes.windll.user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(n + 1)
            ctypes.windll.user32.GetWindowTextW(hwnd, buf, n + 1)
            titles.append(buf.value)
        return True

    ctypes.windll.user32.EnumWindows(proc(each), 0)
    return titles


def media_kind(title, thumb):
    """O Windows chama tudo de "musica". Da para separar por dois sinais: a aba
    do YouTube aparece no titulo da janela, e miniatura de video e larga (16:9),
    enquanto capa de disco e quadrada."""
    if title and any(w.startswith(title) and " - YouTube" in w for w in window_titles()):
        return YOUTUBE
    if thumb:
        try:
            w, h = Image.open(io.BytesIO(thumb)).size
            if w >= h * 1.3:
                return YOUTUBE if any(" - YouTube" in t for t in window_titles()) else VIDEO
        except Exception:
            pass
    return MUSIC


async def now_playing():
    """(tocando, titulo, artista, bytes da imagem da capa ou None, tipo de midia)"""
    manager = await MediaManager.request_async()
    sessions = list(manager.get_sessions())
    if not sessions:
        return False, "", "", None, MUSIC

    def rank(s):  # tocando primeiro; entre os que tocam, Spotify primeiro
        playing = s.get_playback_info().playback_status == PlaybackStatus.PLAYING
        return (playing, "spotify" in s.source_app_user_model_id.lower())

    session = max(sessions, key=rank)
    playing = session.get_playback_info().playback_status == PlaybackStatus.PLAYING
    props = await session.try_get_media_properties_async()
    thumb = None
    if props.thumbnail:
        stream = await props.thumbnail.open_read_async()
        buf = Buffer(stream.size)
        await stream.read_async(buf, stream.size, InputStreamOptions.READ_AHEAD)
        thumb = bytearray(buf.length)
        DataReader.from_buffer(buf).read_bytes(thumb)
        thumb = bytes(thumb)
    return playing, clean(props.title), clean(props.artist), thumb, media_kind(props.title, thumb)


# ---- Notificacoes de mensagem ----

class Notifications:
    """Le os avisos que o Windows mostra no canto da tela e separa os de mensagem."""

    def __init__(self):
        self.listener = UserNotificationListener.current
        self.seen = None  # ids ja tratados; None ate a primeira leitura

    async def _read(self):
        found = []
        for n in await self.listener.get_notifications_async(NotificationKinds.TOAST):
            try:
                app = n.app_info.display_info.display_name
                binding = n.notification.visual.get_binding(KnownNotificationBindings.toast_generic)
                texts = [t.text for t in binding.get_text_elements()] if binding else []
            except Exception:
                continue
            found.append((n.id, app, texts))
        return found

    def poll(self):
        """Lista de (quem, texto) das mensagens novas desde a ultima chamada."""
        try:
            found = asyncio.run(self._read())
        except Exception:
            return []
        first, self.seen = self.seen is None, self.seen or set()
        new = [f for f in found if f[0] not in self.seen]
        self.seen = {f[0] for f in found}
        if first:
            return []  # o que ja estava la antes de ligar nao e novidade
        out = []
        for _, app, texts in new:
            blob = " ".join([app] + texts).lower()
            if not any(word in blob for word in NOTIFY_FROM) or len(texts) < 2:
                continue
            # no navegador, o endereco do site vem como um dos textos: tira
            texts = [t for t in texts if not any(word in t.lower() for word in NOTIFY_FROM)]
            if texts:
                body = " ".join(texts[1:]) if SHOW_MESSAGE_TEXT else "mandou mensagem"
                out.append((clean(texts[0]), clean(body or "mandou mensagem", 240)))
        return out


def cover_lines(thumb, video=False):
    """Capa recortada em quadrado, reduzida e convertida para as linhas 'img' do aparelho."""
    img = Image.open(io.BytesIO(thumb)).convert("RGB")
    # alguns players entregam a capa com uma moldura preta em volta: corta fora
    box = img.convert("L").point(lambda v: 255 if v > 24 else 0).getbbox()
    if box and (box[2] - box[0]) > img.width // 2 and (box[3] - box[1]) > img.height // 2:
        img = img.crop(box)
    if video:
        # miniatura de video inteira, com faixas pretas em cima e embaixo
        img.thumbnail((COVER, COVER), Image.LANCZOS)
        framed = Image.new("RGB", (COVER, COVER))
        framed.paste(img, ((COVER - img.width) // 2, (COVER - img.height) // 2))
        img = framed
    else:
        side = min(img.size)
        left, top = (img.width - side) // 2, (img.height - side) // 2
        img = img.crop((left, top, left + side, top + side)).resize((COVER, COVER), Image.LANCZOS)
    px = img.load()
    for y in range(COVER):
        row = "".join(
            f"{((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3):04x}"
            for r, g, b in (px[x, y] for x in range(COVER))
        )
        yield f"img {y} {row}\n".encode()


# ---- Robustez: registro de erros, uma parte com defeito nao derruba as outras ----

LOG_FILE = Path(__file__).with_name("companion.log")
log = logging.getLogger("bichinho")
_warned = {}


def setup_log():
    handler = logging.handlers.RotatingFileHandler(LOG_FILE, maxBytes=200_000, backupCount=1, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s", "%d/%m %H:%M:%S"))
    log.addHandler(handler)
    log.setLevel(logging.INFO)


def guarded(name, fn, default):
    """Roda uma parte do programa (tempo, noticias, rotina...). Se ela falhar,
    anota no registro (no maximo uma vez a cada 10 min por parte) e segue com o resto."""
    try:
        return fn()
    except Exception:
        if time.time() - _warned.get(name, 0) > 600:
            _warned[name] = time.time()
            log.exception("falha em '%s'", name)
        return default


class State:
    """O que o icone perto do relogio mostra e comanda."""
    status = "Procurando o aparelho"
    connected = False
    paused = False     # porta USB liberada (para gravar firmware, por exemplo)
    restart = False    # pedido de refazer a ligacao
    quit = False


def run(port, state):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout, s.write_timeout = port, 115200, 0, 5
    s.dtr = s.rts = False  # nao reinicia o aparelho ao conectar
    s.open()
    try:
        state.connected, state.status = True, f"Ligado ao aparelho ({port})"
        log.info("ligado ao aparelho em %s", port)
        serve(s, state)
    finally:
        state.connected = False
        s.close()


def serve(s, state):
    cpu_percent()
    # Conta de musica para ver o que toca em outros aparelhos: Spotify (so com
    # Premium) ou Last.fm. None enquanto nenhuma das duas foi configurada.
    account = Spotify.load() or LastFm.load()
    notifications = Notifications()
    weather, memory, happenings = Weather(), routine.Routine(), events.Events()
    in_call = None
    cooler = Cooler()
    headlines = news.News()
    keys, gaming = activity.Keys(), activity.Gaming()
    in_game, next_notes = None, 0.0
    last_np, last_np_time, last_thumb = None, 0.0, None
    next_slow, cover = 0.0, None  # cover = linhas da capa ainda por mandar
    while not (state.paused or state.restart or state.quit):
        time.sleep(TICK)
        if b"[boot]" in s.read(4096):  # o aparelho reiniciou: manda tudo de novo
            last_np = last_thumb = in_call = in_game = None
            weather.resend()
            headlines.resend()

        # Teclas e cliques (so a contagem): as patinhas batem junto
        typed, clicked = keys.take()
        if typed or clicked:
            s.write(f"type {min(typed, 9)} {min(clicked, 9)}\n".encode())

        # Mensagens: conferidas tres vezes por segundo, para chegarem quase na hora
        fresh = []
        if time.time() >= next_notes:
            next_notes = time.time() + 0.3
            fresh = guarded("mensagens", notifications.poll, [])
        for sender, body in fresh:
            s.write(f"msg {sender}\t{body[:240]}\n".encode("utf-8"))

        # Capa: poucas linhas por volta, para nao segurar as mensagens
        if cover:
            for line in cover[:6]:
                s.write(line)
                time.sleep(0.02)  # da tempo de o aparelho ler cada linha
            cover = cover[6:]

        if time.time() < next_slow:
            continue
        next_slow = time.time() + 1  # o resto, uma vez por segundo

        cpu = guarded("cpu", cpu_percent, 0.0)
        gpu = gpu_stats()
        s.write(f"pc {cpu:.0f} {guarded('memoria', ram_percent, 0.0):.0f}{gpu}\n".encode())

        # Jogando? (a Steam avisa; fora dela, tela cheia + placa de video trabalhando)
        gpu_use = float(gpu.split()[0]) if gpu else 0.0
        game = guarded("jogo", lambda: gaming.poll(gpu_use), (False, ""))
        if game != in_game:
            in_game = game
            s.write(f"game {int(game[0])}\t{clean(game[1], 40)}\n".encode("utf-8"))
        s.write(guarded("cooler", lambda: cooler.line(cpu), b""))

        # Hora e tempo pelo cabo: o aparelho nao depende do Wi-Fi dele
        for line in guarded("tempo", weather.poll, []):
            s.write(line)

        # Chamada em andamento (algum programa com o microfone aberto): ele fica quieto
        calling = guarded("chamada", events.in_call, False)
        if calling != in_call:
            in_call = calling
            s.write(f"call {int(calling)}\n".encode())

        # Rotina e acontecimentos do PC viram falas; em chamada, ficam sem dizer
        idle, says = guarded("rotina", memory.poll, (0, []))
        s.write(f"user {idle:.0f}\n".encode())
        says = says + guarded("eventos", lambda: happenings.poll(cpu), [])
        if not calling:
            for expr, sfx, text in says[:3]:
                s.write(f"tell {expr} {sfx} {clean(text, 120)}\n".encode("utf-8"))

        # Noticiario: manchetes guardadas no aparelho e, de vez em quando, um plantao
        for line in guarded("noticias", lambda: headlines.poll(can_talk=not calling and idle < 300), []):
            head, _, rest = line.partition("\t")
            s.write((head + ("\t" + clean(rest, 115) + "\n" if rest else "")).encode("utf-8"))

        *np, thumb, kind = guarded("midia", lambda: asyncio.run(now_playing()), (False, "", "", None, MUSIC))
        # Conta de musica (celular, TV...): vale quando esta tocando, ou quando
        # nada toca neste PC
        remote = guarded("conta de musica", account.now_playing, None) if account else None
        if remote and (remote[0] or not np[0]):
            np, thumb, kind = [remote[0], clean(remote[1]), clean(remote[2])], remote[3], MUSIC
        np[0] = kind if np[0] else 0  # 0 parado, 1 musica, 2 video, 3 YouTube
        if np != last_np or time.time() - last_np_time > 10:
            if last_np is None or np[1:] != last_np[1:]:
                last_thumb = None  # musica nova: o aparelho descartou a capa antiga
            last_np, last_np_time = np, time.time()
            s.write(f"np {int(np[0])}\t{np[1]}\t{np[2]}\n".encode("utf-8"))
            if np[0] == MUSIC and np[1]:  # andamento da musica, para o balanco no ritmo (0 = nao achou)
                s.write(f"bpm {guarded('andamento', lambda: tempo.bpm(np[2], np[1]), 0)}\n".encode())
        if thumb and thumb != last_thumb:
            last_thumb = thumb
            cover = guarded("capa", lambda: list(cover_lines(thumb, video=kind != MUSIC)), None)


def worker(state):
    """Procura o aparelho, conversa com ele e, aconteca o que acontecer, tenta de novo."""
    while not state.quit:
        state.restart = False
        port = None if state.paused else find_port()
        if state.paused:
            state.status = "Pausado (porta USB liberada)"
        elif not port:
            state.status = "Procurando o aparelho"
        else:
            try:
                run(port, state)
            except (serial.SerialException, OSError) as e:
                # cabo solto, porta ocupada por uma gravacao de firmware...
                state.status = "Sem contato com o aparelho; tentando de novo"
                log.info("sem contato com %s: %s", port, e)
            except Exception:
                state.status = "Erro inesperado; reiniciando a ligacao"
                log.exception("erro inesperado na ligacao")
        for _ in range(10):  # 5 s entre tentativas, mas atende logo a um comando do icone
            if state.quit or state.restart:
                break
            time.sleep(0.5)


def only_instance():
    """Dois programas disputando a mesma porta USB nao funcionam: fica so o primeiro."""
    ctypes.windll.kernel32.CreateMutexW(None, False, "bichinho-m5-companion")
    return ctypes.windll.kernel32.GetLastError() != 183  # ERROR_ALREADY_EXISTS


def tray(state):
    """Icone perto do relogio do Windows: mostra se esta ligado e deixa pausar, religar e sair."""
    import pystray
    from PIL import ImageDraw

    def face(color):
        img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle((2, 2, 62, 62), 14, fill=(20, 20, 24, 255))
        d.rounded_rectangle((14, 16, 26, 40), 5, fill=color)
        d.rounded_rectangle((38, 16, 50, 40), 5, fill=color)
        d.arc((22, 34, 42, 50), 20, 160, fill=color, width=4)
        return img

    icons = {"on": face((255, 255, 255)), "off": face((120, 120, 130)), "paused": face((255, 205, 50))}

    def toggle_pause(icon, item):
        state.paused = not state.paused

    def reconnect(icon, item):
        state.restart = True

    def open_log(icon, item):
        LOG_FILE.touch()
        subprocess.Popen(["notepad.exe", str(LOG_FILE)])

    def leave(icon, item):
        state.quit = True
        icon.stop()

    icon = pystray.Icon("bichinho", icons["off"], "Bichinho", menu=pystray.Menu(
        pystray.MenuItem(lambda item: state.status, None, enabled=False),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Pausar (libera a porta USB)", toggle_pause, checked=lambda item: state.paused),
        pystray.MenuItem("Refazer a ligação", reconnect),
        pystray.MenuItem("Abrir registro de erros", open_log),
        pystray.MenuItem("Sair", leave),
    ))

    def refresh():  # mantem a cor do icone e o texto em dia
        while not state.quit:
            icon.icon = icons["paused" if state.paused else "on" if state.connected else "off"]
            icon.title = "Bichinho: " + state.status
            time.sleep(1)

    threading.Thread(target=refresh, daemon=True).start()
    icon.run()


def main():
    setup_log()
    if not only_instance():
        return
    log.info("programa iniciado")
    state = State()
    thread = threading.Thread(target=worker, args=(state,), daemon=True)
    thread.start()
    try:
        tray(state)
    except Exception:
        log.exception("icone da bandeja indisponivel; seguindo sem ele")
        thread.join()
    state.quit = True
    thread.join(timeout=3)
    log.info("programa encerrado")


if __name__ == "__main__":
    main()
