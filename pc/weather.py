"""Hora e tempo para o bichinho, mandados pelo cabo: assim ele nao depende do
proprio Wi-Fi (que e fraco) para ter relogio, temperatura, previsao e graficos.
"""
import json
import time
import urllib.parse
import urllib.request

WEATHER_EVERY = 10 * 60
CLOCK_EVERY = 30


def _get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 bichinho"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r)


class Weather:
    def __init__(self):
        self.place = None  # (cidade, latitude, longitude), descoberta pelo IP
        mms = ",".join(f"{m or 0:.1f}" for m in hour["precipitation"][:24])
        self.lines = []    # ultimo tempo ja formatado para o aparelho
        self.next_weather = self.next_clock = 0.0

    def resend(self):
        """O aparelho reiniciou: manda tudo de novo na proxima volta."""
        self.next_clock = 0.0
        self.next_weather = min(self.next_weather, time.time())
        self.pending = list(self.lines)

    pending = []

    def _fetch(self):
        if not self.place:
            p = _get("http://ip-api.com/json/?fields=status,city,lat,lon")
            if p.get("status") != "success":
                return
            self.place = (p["city"], p["lat"], p["lon"])
        city, lat, lon = self.place
        query = urllib.parse.urlencode(
            {
                "latitude": lat,
                "longitude": lon,
                "current": "temperature_2m,weather_code,is_day,precipitation",
                "daily": "temperature_2m_max,temperature_2m_min,precipitation_probability_max",
                "hourly": "temperature_2m,precipitation_probability,precipitation",
                "timezone": "auto",
                "forecast_days": 1,
            }
        )
        d = _get("https://api.open-meteo.com/v1/forecast?" + query)
        cur, day, hour = d["current"], d["daily"], d["hourly"]
        city = "".join(ch for ch in city if 0x20 <= ord(ch) <= 0xFF)[:38]
        temps = ",".join(f"{t:.1f}" for t in hour["temperature_2m"][:24])
        rains = ",".join(str(int(r or 0)) for r in hour["precipitation_probability"][:24])
        self.lines = [
            f"weather {cur['temperature_2m']:.1f} {cur['weather_code']} {cur['is_day']} "
            f"{day['temperature_2m_max'][0]:.1f} {day['temperature_2m_min'][0]:.1f} "
            f"{int(day['precipitation_probability_max'][0] or 0)} {cur.get('precipitation') or 0:.1f}\t{city}\n".encode("utf-8"),
            f"hourly {temps};{rains};{mms}\n".encode(),
        ]
        self.pending = list(self.lines)

    def poll(self):
        """Linhas para mandar agora ao aparelho (pode ser nenhuma)."""
        now = time.time()
        out = []
        if now >= self.next_clock:
            self.next_clock = now + CLOCK_EVERY
            # hora universal + diferenca do fuso deste PC, em segundos
            out.append(f"clock {int(now)} {time.localtime(now).tm_gmtoff}\n".encode())
        if now >= self.next_weather:
            try:
                self._fetch()
                self.next_weather = now + WEATHER_EVERY
            except Exception:
                self.next_weather = now + 60  # sem internet agora: tenta de novo em 1 min
        out += self.pending
        self.pending = []
        return out


if __name__ == "__main__":
    for line in Weather().poll():
        print(line.decode("utf-8").rstrip()[:150])
