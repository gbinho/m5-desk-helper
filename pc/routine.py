"""Memoria do bichinho sobre a sua rotina no PC: a que horas voce costuma
chegar, quanto tempo ficou hoje, ha quanto tempo esta sem pausa. Com isso ele
comenta as coisas na hora certa e fica quieto quando voce nao esta.

Guarda so horarios e minutos, em pc/rotina.json. Nada sobre o que voce faz.
"""
import ctypes
import json
import random
import re
import statistics
import time
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STATS = ROOT / "rotina.json"

# Expressoes e sons do aparelho (mesma ordem de face.h e sound.h)
TALK, HAPPY, SURPRISED, SLEEPY = 13, 1, 4, 5
NO_SOUND, CHIRP, HAPPY_SOUND, ALERT = -1, 0, 1, 5

PRESENT_BELOW = 60     # segundos sem teclado/mouse para ainda contar como "no PC"
BREAK_AFTER = 5 * 60   # pausa de verdade: zera o tempo "direto"
AWAY_AFTER = 15 * 60   # a partir daqui ele comenta a volta
STREAK_FIRST, STREAK_EVERY = 90, 60  # minutos direto: primeiro aviso e os seguintes
DAY_MARKS = (4, 6, 8, 10)            # horas de PC no dia que merecem comentario
DAY_STARTS = 4                       # hora em que o dia vira: antes disso ainda e a noite anterior
EARLY_BEFORE = 6 * 60 + 30           # chegar antes das 6h30 e "muito cedo"
OFFICE_WEEKDAY = 1                   # terca-feira (segunda = 0): dia de trabalho presencial


def owner():
    """O nome fica num lugar so: src/config.h."""
    try:
        text = (ROOT.parent / "src" / "config.h").read_text(encoding="utf-8")
        return re.search(r'#define OWNER "([^"]+)"', text).group(1)
    except Exception:
        return "você"


class LASTINPUTINFO(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.UINT), ("dwTime", wintypes.DWORD)]


def idle_seconds():
    """Ha quanto tempo ninguem mexe no teclado nem no mouse."""
    info = LASTINPUTINFO()
    info.cbSize = ctypes.sizeof(info)
    ctypes.windll.user32.GetLastInputInfo(ctypes.byref(info))
    return ((ctypes.windll.kernel32.GetTickCount() - info.dwTime) & 0xFFFFFFFF) / 1000


def _hm(minutes):
    h, m = divmod(int(minutes), 60)
    return f"{h}h{m:02d}" if h else f"{m} min"


class Routine:
    def __init__(self):
        self.name = owner()
        try:
            self.days = json.loads(STATS.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            self.days = {}  # "2026-10-09": {"first": minutos desde 0h, "active": segundos}
        self.last = time.time()
        self.next_save = self.last + 60
        self.streak_start = None   # inicio do periodo sem pausa
        self.next_streak = STREAK_FIRST
        self.away_since = None
        self.marks_done = set()

    def _today(self):
        # ficar acordado ate depois da meia-noite nao conta como "chegar" no dia seguinte
        return time.strftime("%Y-%m-%d", time.localtime(time.time() - DAY_STARTS * 3600))

    def _usual_arrival(self):
        """Mediana da hora de chegada dos outros dias; None com menos de 3 dias de historia."""
        firsts = [d["first"] for day, d in self.days.items() if day != self._today() and "first" in d]
        return statistics.median(firsts[-14:]) if len(firsts) >= 3 else None

    def _arrival_line(self, minute_of_day):
        hour = minute_of_day // 60
        hello = "Bom dia" if hour < 12 else "Boa tarde" if hour < 18 else "Boa noite"
        # Muito cedo: nas tercas e o trabalho presencial; nos outros dias, academia
        if DAY_STARTS * 60 <= minute_of_day < EARLY_BEFORE:
            if time.localtime().tm_wday == OFFICE_WEEKDAY:
                return f"Bom dia, {self.name}! Terça de presencial. Bom trabalho!"
            return f"Bom dia, {self.name}! Dia de academia? Bom treino!"
        usual = self._usual_arrival()
        yesterday = sorted(d for d in self.days if d != self._today())[-1:]
        extra = ""
        if usual is not None and minute_of_day < usual - 30:
            extra = " Chegou cedo hoje!"
        elif usual is not None and minute_of_day > usual + 45:
            extra = " Hoje começou mais tarde, hein?"
        elif yesterday and self.days[yesterday[0]].get("active", 0) > 1800:
            extra = f" Da última vez foram {_hm(self.days[yesterday[0]]['active'] / 60)} de PC."
        return f"{hello}, {self.name}!{extra}"

    def poll(self):
        """Devolve (segundos parado, falas novas). Cada fala e (expressao, som, texto)."""
        now = time.time()
        dt, self.last = min(now - self.last, 5), now
        idle = idle_seconds()
        present = idle < PRESENT_BELOW
        says = []

        today = self.days.setdefault(self._today(), {"active": 0})
        if present:
            lt = time.localtime(now)
            if "first" not in today:  # primeira mexida do dia
                today["first"] = lt.tm_hour * 60 + lt.tm_min
                self.marks_done = set()
                says.append((HAPPY, HAPPY_SOUND, self._arrival_line(today["first"])))
                self.away_since = None
            today["active"] += dt

            if self.away_since is not None:  # voltou
                gone = (now - self.away_since) / 60
                self.away_since = None
                if gone >= 180:
                    says.append((HAPPY, HAPPY_SOUND, f"Olha quem voltou! Senti sua falta, {self.name}."))
                else:
                    says.append((HAPPY, CHIRP, random.choice([
                        f"Voltou, {self.name}!",
                        "Opa, olha quem voltou!",
                        f"{_hm(gone)} fora... o café tava bom?",
                    ])))

            if self.streak_start is None:
                self.streak_start, self.next_streak = now, STREAK_FIRST
            streak = (now - self.streak_start) / 60
            if streak >= self.next_streak:
                self.next_streak += STREAK_EVERY
                says.append((TALK, ALERT, f"Já faz {_hm(streak)} direto, {self.name}. Levanta um pouco!"))

            hours = today["active"] / 3600
            for mark in DAY_MARKS:
                if hours >= mark and mark not in self.marks_done:
                    self.marks_done.add(mark)
                    if hours < mark + 0.1:  # so comenta na hora em que cruza a marca
                        says.append((TALK, CHIRP, f"Já são {mark} horas de PC hoje, {self.name}."))
        else:
            if idle >= BREAK_AFTER:
                self.streak_start = None
            if idle >= AWAY_AFTER and self.away_since is None:
                self.away_since = now - idle

        if now >= self.next_save:
            self.next_save = now + 60
            for day in sorted(self.days)[:-60]:  # guarda no maximo dois meses
                del self.days[day]
            try:
                STATS.write_text(json.dumps(self.days, indent=1), encoding="utf-8")
            except OSError:
                pass
        return idle, says
