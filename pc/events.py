"""Coisas que acontecem no PC e que o bichinho comenta: chamada em andamento,
download que terminou, trabalho pesado que acabou, bateria do PC no fim.
"""
import ctypes
import time
import winreg
from pathlib import Path

# Expressoes e sons do aparelho (mesma ordem de face.h e sound.h)
TALK, HAPPY, SURPRISED, HOT = 13, 1, 4, 11
CHIRP, HAPPY_SOUND, ALERT = 0, 1, 5

MIC_KEY = r"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone"
UNFINISHED = (".crdownload", ".part", ".tmp", ".download", ".opdownload", ".partial")
BUSY_ABOVE, CALM_BELOW = 70, 30  # % de CPU
BUSY_FOR, CALM_FOR = 60, 10      # segundos


def _boot_filetime():
    """Instante em que o Windows ligou, na unidade que o registro usa (100 ns desde 1601)."""
    ctypes.windll.kernel32.GetTickCount64.restype = ctypes.c_ulonglong
    now = int((time.time() + 11644473600) * 10_000_000)
    return now - ctypes.windll.kernel32.GetTickCount64() * 10_000


def _mic_users(key):
    """Programas com o microfone aberto agora: o Windows marca o fim do uso como 0.
    Programa que fechou de mau jeito deixa esse 0 para sempre (ja vi um de meses
    atras), entao so vale uso que comecou depois que o Windows ligou."""
    count = 0
    boot = _boot_filetime()
    i = 0
    while True:
        try:
            name = winreg.EnumKey(key, i)
        except OSError:
            return count
        i += 1
        try:
            with winreg.OpenKey(key, name) as sub:
                if name == "NonPackaged":
                    count += _mic_users(sub)
                elif (winreg.QueryValueEx(sub, "LastUsedTimeStop")[0] == 0
                      and winreg.QueryValueEx(sub, "LastUsedTimeStart")[0] > boot):
                    count += 1
        except OSError:
            pass


def in_call():
    """Algum programa esta usando o microfone (Discord, Meet, Teams, Zoom...)."""
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, MIC_KEY) as key:
            return _mic_users(key) > 0
    except OSError:
        return False


class SYSTEM_POWER_STATUS(ctypes.Structure):
    _fields_ = [("ACLineStatus", ctypes.c_byte), ("BatteryFlag", ctypes.c_ubyte),
                ("BatteryLifePercent", ctypes.c_ubyte), ("SystemStatusFlag", ctypes.c_byte),
                ("BatteryLifeTime", ctypes.c_ulong), ("BatteryFullLifeTime", ctypes.c_ulong)]


def battery():
    """(% de carga, na tomada?) ou None em PC sem bateria."""
    st = SYSTEM_POWER_STATUS()
    if not ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(st)):
        return None
    if st.BatteryFlag & 128 or st.BatteryLifePercent > 100:
        return None
    return st.BatteryLifePercent, st.ACLineStatus == 1


class Events:
    def __init__(self):
        self.downloads = Path.home() / "Downloads"
        self.seen = self._finished()
        self.busy_since = self.calm_since = None
        self.was_busy = False
        self.battery_warned = False

    def _finished(self):
        try:
            return {p.name for p in self.downloads.iterdir()
                    if p.is_file() and not p.name.lower().endswith(UNFINISHED)}
        except OSError:
            return set()

    def poll(self, cpu):
        """Falas novas: lista de (expressao, som, texto)."""
        now = time.time()
        says = []

        # Download concluido: arquivo novo na pasta, ja sem extensao de "baixando"
        current = self._finished()
        for name in sorted(current - self.seen):
            try:
                if now - (self.downloads / name).stat().st_mtime < 1.5:
                    current.discard(name)  # ainda sendo escrito: confere na proxima
                    continue
            except OSError:
                continue
            short = name if len(name) <= 34 else name[:20] + "..." + name[-11:]
            says.append((HAPPY, HAPPY_SOUND, f"Download pronto: {short}"))
        self.seen = current

        # Trabalho pesado: CPU no alto por um bom tempo e depois sossega
        if cpu >= BUSY_ABOVE:
            self.busy_since = self.busy_since or now
            self.calm_since = None
            if now - self.busy_since >= BUSY_FOR:
                self.was_busy = True
        elif cpu <= CALM_BELOW:
            self.calm_since = self.calm_since or now
            if self.was_busy and now - self.calm_since >= CALM_FOR:
                says.append((HAPPY, HAPPY_SOUND, "Ufa! O PC terminou o trabalho pesado."))
                self.was_busy = False
            if now - self.calm_since >= CALM_FOR:
                self.busy_since = None

        # Bateria do PC acabando (so em notebook)
        b = battery()
        if b:
            pct, plugged = b
            if not plugged and pct <= 20 and not self.battery_warned:
                self.battery_warned = True
                says.append((SURPRISED, ALERT, f"Bateria do PC em {pct}%! Liga na tomada."))
            elif plugged or pct > 25:
                self.battery_warned = False
        return says


if __name__ == "__main__":
    print("em chamada:", in_call(), "| bateria:", battery())
    e = Events()
    print("downloads conhecidos:", len(e.seen))
