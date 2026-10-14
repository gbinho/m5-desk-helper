#include "net.h"

#include <ArduinoJson.h>
#include <M5Unified.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <esp_wifi.h>

NetState net;

static const uint32_t WEATHER_EVERY_MS = 10 * 60 * 1000;
static const uint32_t RETRY_MS = 30 * 1000;

static float lat, lon;

const char* weatherText(int wmo) {
  if (wmo == 0) return "Céu limpo";
  if (wmo <= 2) return "Poucas nuvens";
  if (wmo == 3) return "Nublado";
  if (wmo <= 48) return "Neblina";
  if (wmo <= 57) return "Garoa";
  if (wmo <= 67) return "Chuva";
  if (wmo <= 77) return "Neve";
  if (wmo <= 82) return "Pancadas";
  if (wmo <= 86) return "Neve";
  return "Tempestade";
}

bool clockNow(struct tm& t) {
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return t.tm_year > 2016 - 1900;
}

void restoreClock() {
  Preferences prefs;
  prefs.begin("bichinho", true);
  int offset = prefs.getInt("tz", 1);
  prefs.end();
  auto dt = M5.Rtc.getDateTime();
  if (offset == 1 || dt.date.year < 2025) return;  // nunca foi acertado
  struct tm t = {};
  t.tm_year = dt.date.year - 1900, t.tm_mon = dt.date.month - 1, t.tm_mday = dt.date.date;
  t.tm_hour = dt.time.hours, t.tm_min = dt.time.minutes, t.tm_sec = dt.time.seconds;
  setenv("TZ", "UTC0", 1);
  tzset();
  setClock(mktime(&t), offset);
}

void setClock(time_t utc, int offsetSec) {
  // fuso no formato do sistema, que e ao contrario do usual: Brasilia (-3 h) vira "UTC+3:00"
  char tz[20];
  int a = abs(offsetSec);
  snprintf(tz, sizeof(tz), "UTC%c%d:%02d", offsetSec <= 0 ? '+' : '-', a / 3600, a % 3600 / 60);
  setenv("TZ", tz, 1);
  tzset();
  struct timeval tv = {utc, 0};
  settimeofday(&tv, nullptr);
  // guarda no relogio a pilha do aparelho (hora universal) e o fuso, para a proxima vez que ligar
  static time_t saved = 0;
  if (llabs((long long)utc - saved) > 3600) {
    saved = utc;
    M5.Rtc.setDateTime(gmtime(&utc));
    Preferences prefs;
    prefs.begin("bichinho");
    if (prefs.getInt("tz", 1) != offsetSec) prefs.putInt("tz", offsetSec);
    prefs.end();
  }
}

static bool httpGetJson(const char* url, JsonDocument& doc) {
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(url)) return false;
  int code = http.GET();
  bool ok = code == HTTP_CODE_OK && !deserializeJson(doc, http.getString());
  http.end();
  if (!ok) Serial.printf("[net] falhou (%d): %s\n", code, url);
  return ok;
}

// Descobre cidade, coordenadas e fuso pelo IP, e acerta o relogio
static bool fetchLocation() {
  JsonDocument doc;
  if (!httpGetJson("http://ip-api.com/json/?fields=status,city,lat,lon,offset", doc)) return false;
  if (doc["status"] != "success") return false;
  lat = doc["lat"];
  lon = doc["lon"];
  strlcpy(net.city, doc["city"] | "", sizeof(net.city));
  configTime(doc["offset"] | -10800, 0, "pool.ntp.org", "time.google.com");
  Serial.printf("[net] local: %s (%.3f, %.3f)\n", net.city, lat, lon);
  return true;
}

static bool fetchWeather() {
  char url[400];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,is_day,precipitation"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&hourly=temperature_2m,precipitation_probability,precipitation"
           "&timezone=auto&forecast_days=1",
           lat, lon);
  JsonDocument doc;
  if (!httpGetJson(url, doc) || doc["current"]["temperature_2m"].isNull()) return false;
  net.tempC = doc["current"]["temperature_2m"];
  net.wmo = doc["current"]["weather_code"];
  net.isDay = (doc["current"]["is_day"] | 1) != 0;
  net.rainingMm = doc["current"]["precipitation"] | 0.0f;
  net.maxC = doc["daily"]["temperature_2m_max"][0] | (float)net.tempC;
  net.minC = doc["daily"]["temperature_2m_min"][0] | (float)net.tempC;
  net.rainPct = doc["daily"]["precipitation_probability_max"][0] | 0;
  JsonArray temps = doc["hourly"]["temperature_2m"], rains = doc["hourly"]["precipitation_probability"],
            mms = doc["hourly"]["precipitation"];
  if (temps.size() >= 24 && rains.size() >= 24) {
    for (int i = 0; i < 24; i++) {
      net.hourTemp[i] = temps[i] | 0.0f;
      net.hourRain[i] = constrain(rains[i] | 0, 0, 100);
      net.hourMm[i] = mms[i] | 0.0f;
    }
    net.hasHourly = true;
  }
  net.hasWeather = true;
  Serial.printf("[net] %.1f C, %s, max %.0f min %.0f chuva %d%%\n", (float)net.tempC,
                weatherText(net.wmo), (float)net.maxC, (float)net.minC, (int)net.rainPct);
  return true;
}

// Explica no log por que nao conectou: a rede aparece? com que sinal?
static void logSignal(const String& ssid, int status) {
  WiFi.disconnect();  // a busca falha se ainda estiver tentando conectar
  vTaskDelay(pdMS_TO_TICKS(500));
  int n = WiFi.scanNetworks(), rssi = 0;
  for (int i = 0; i < n; i++)
    if (WiFi.SSID(i) == ssid) rssi = WiFi.RSSI(i);
  WiFi.scanDelete();
  if (rssi) Serial.printf("[net] sem conexao (status %d): '%s' com sinal %d dBm\n", status, ssid.c_str(), rssi);
  else Serial.printf("[net] sem conexao (status %d): '%s' nao aparece entre %d redes\n", status, ssid.c_str(), n);
}

static bool forcePortal = false;

static void wifiOff() {
  if (WiFi.getMode() == WIFI_OFF) return;
  WiFi.disconnect(true);  // desliga o radio; a rede salva continua guardada
  WiFi.mode(WIFI_OFF);
  net.online = false;
  Serial.println("[net] Wi-Fi desligado");
}

static void netTask(void*) {
  WiFi.setHostname(HOSTNAME);
  // Tela de configuracao do Wi-Fi: so quando pedida (ligar segurando o botao B).
  // O Bluetooth fica desligado ate ela terminar, porque os dois nao cabem juntos.
  if (forcePortal) {
    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.setAPCallback([](WiFiManager*) { net.portal = true; });
    wm.setConnectTimeout(20);
    while (!wm.startConfigPortal(AP_NAME)) net.portal = false;
    net.portal = false;
    wifiOff();
  }
  net.setupDone = true;
  // Fora isso o radio nem e ligado aqui: fica para quando o programa principal pedir

  // Dai em diante: radio ligado so enquanto for preciso. Com rede salva ele insiste
  // para sempre, sem pedir o Wi-Fi de novo por causa de sinal fraco.
  bool located = false, announced = false;
  uint32_t nextFetch = millis();
  for (int tries = 0;;) {
    if (!net.wifiWanted) {
      wifiOff();
      net.radioOff = true;
      announced = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      net.online = false;
      announced = false;
      net.radioOff = false;
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);  // ligado na tomada: prefere sinal firme a economizar energia
      WiFi.begin();
      int status = WiFi.waitForConnectResult(15000);
      if (status != WL_CONNECTED) {
        if (tries++ % 4 == 3) {  // de vez em quando anota por que nao conectou
          wifi_config_t conf = {};
          esp_wifi_get_config(WIFI_IF_STA, &conf);  // nome da rede salva
          logSignal(String((const char*)conf.sta.ssid), status);
        }
        // espera antes de tentar de novo, mas larga na hora se o PC voltar
        for (int i = 0; i < 4 && net.wifiWanted; i++) vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
    }
    tries = 0;
    net.online = true;
    if (!announced) {
      announced = true;
      Serial.printf("[net] conectado: %s, sinal %d dBm\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    }
    if ((int32_t)(millis() - nextFetch) >= 0) {
      if (!located) located = fetchLocation();
      bool ok = located && fetchWeather();
      if (ok) net.fetches = net.fetches + 1;
      nextFetch = millis() + (ok ? WEATHER_EVERY_MS : RETRY_MS);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void netBegin(bool configure) {
  forcePortal = configure;
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
}
