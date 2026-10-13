"""O que voce esta fazendo no PC, para o bichinho acompanhar: digitando,
clicando ou jogando.

Do teclado ele so CONTA as teclas apertadas, para bater a patinha junto. Nunca
olha nem guarda qual tecla foi.
"""
import ctypes
import threading
import time
import winreg
from ctypes import wintypes

user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32

# Programas que ficam em tela cheia e usam a placa de video sem serem jogos
NOT_GAMES = {"chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", "opera.exe", "vlc.exe", "mpc-hc64.exe",
             "explorer.exe", "obs64.exe", "powerpnt.exe", "photos.exe", "netflix.exe", "code.exe", "claude.exe"}
GPU_GAMING = 40          # % de uso da placa de video que, em tela cheia, indica jogo
START_AFTER, STOP_AFTER = 12, 25  # segundos para confirmar que comecou / que acabou


class Keys:
    """Conta teclas e cliques numa linha separada, olhando o estado do teclado 50 vezes por segundo."""

    MOUSE = (0x01, 0x02)           # botoes esquerdo e direito
    KEYBOARD = range(0x08, 0xDF)   # todas as teclas de verdade

    def __init__(self):
        self.keys = self.clicks = 0
        self.lock = threading.Lock()
        threading.Thread(target=self._watch, daemon=True).start()

    def _watch(self):
        down = set()
        while True:
            time.sleep(0.02)
            now_down = {vk for vk in (*self.MOUSE, *self.KEYBOARD) if user32.GetAsyncKeyState(vk) & 0x8000}
            fresh = now_down - down
            down = now_down
            if fresh:
                with self.lock:
                    self.clicks += sum(1 for vk in fresh if vk in self.MOUSE)
                    self.keys += sum(1 for vk in fresh if vk not in self.MOUSE)

    def take(self):
        """(teclas, cliques) desde a ultima chamada."""
        with self.lock:
            k, c, self.keys, self.clicks = self.keys, self.clicks, 0, 0
        return k, c


class RECT(ctypes.Structure):
    _fields_ = [("left", wintypes.LONG), ("top", wintypes.LONG), ("right", wintypes.LONG), ("bottom", wintypes.LONG)]


class MONITORINFO(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("rcMonitor", RECT), ("rcWork", RECT), ("dwFlags", wintypes.DWORD)]


def foreground():
    """(programa.exe, titulo da janela, esta em tela cheia?) da janela em uso agora."""
    user32.GetForegroundWindow.restype = wintypes.HWND
    hwnd = user32.GetForegroundWindow()
    if not hwnd:
        return "", "", False
    title = ctypes.create_unicode_buffer(200)
    user32.GetWindowTextW(hwnd, title, 200)
    cls = ctypes.create_unicode_buffer(80)
    user32.GetClassNameW(hwnd, cls, 80)

    rect, info = RECT(), MONITORINFO()
    info.cbSize = ctypes.sizeof(info)
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    user32.MonitorFromWindow.restype = wintypes.HANDLE
    monitor = user32.MonitorFromWindow(hwnd, 2)
    user32.GetMonitorInfoW(monitor, ctypes.byref(info))
    m = info.rcMonitor
    # janela so maximizada tem barra de titulo; jogo em tela cheia nao tem
    has_caption = (user32.GetWindowLongW(hwnd, -16) & 0x00C00000) == 0x00C00000
    full = (rect.left <= m.left and rect.top <= m.top and rect.right >= m.right and rect.bottom >= m.bottom
            and not has_caption and cls.value not in ("Progman", "WorkerW"))  # a area de trabalho nao conta

    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    exe = ""
    kernel32.OpenProcess.restype = wintypes.HANDLE
    handle = kernel32.OpenProcess(0x1000, False, pid.value)  # so o direito de perguntar o nome
    if handle:
        path = ctypes.create_unicode_buffer(520)
        size = wintypes.DWORD(520)
        if kernel32.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(size)):
            exe = path.value.rsplit("\\", 1)[-1].lower()
        kernel32.CloseHandle(handle)
    return exe, title.value, full


def steam_game():
    """Nome do jogo aberto pela Steam, '' se nenhum."""
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            app = winreg.QueryValueEx(key, "RunningAppID")[0]
        if not app:
            return ""
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, rf"Software\Valve\Steam\Apps\{app}") as key:
                return winreg.QueryValueEx(key, "Name")[0] or "jogo"
        except OSError:
            return "jogo"
    except OSError:
        return ""


class Gaming:
    """Decide se ha um jogo rodando: a Steam avisa os dela; para os outros, vale
    janela em tela cheia + placa de video trabalhando, por alguns segundos seguidos."""

    def __init__(self):
        self.playing, self.name = False, ""
        self.since_yes = self.since_no = None

    def poll(self, gpu_use):
        now = time.time()
        name = steam_game()
        if not name:
            exe, title, full = foreground()
            if full and gpu_use >= GPU_GAMING and exe and exe not in NOT_GAMES:
                name = title or exe.removesuffix(".exe")
        if name:
            self.since_no = None
            self.since_yes = self.since_yes or now
            if not self.playing and now - self.since_yes >= START_AFTER:
                self.playing, self.name = True, name
        else:
            self.since_yes = None
            self.since_no = self.since_no or now
            if self.playing and now - self.since_no >= STOP_AFTER:
                self.playing = False
        return self.playing, self.name


if __name__ == "__main__":
    keys = Keys()
    print("janela em uso:", foreground())
    print("Steam:", repr(steam_game()))
    time.sleep(3)
    print("teclas e cliques em 3 s:", keys.take())
