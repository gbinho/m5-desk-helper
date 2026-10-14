#include "scenes.h"

#include "ears.h"
#include "gifs.h"
#include "icons.h"
#include "net.h"
#include "pclink.h"
#include "speech.h"
#include "text.h"
#include "util.h"

namespace scenes {

static const char* DIAS[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sáb"};
static const char* MESES[] = {"janeiro", "fevereiro", "março",    "abril",   "maio",     "junho",
                              "julho",   "agosto",    "setembro", "outubro", "novembro", "dezembro"};

bool available(Scene s, uint32_t now) {
  struct tm t;
  switch (s) {
    case CLOCK: return clockNow(t);
    case TEMP:
    case FORECAST: return net.hasWeather;
    // sem contato com o PC, as duas continuam mostrando o ultimo estado
    case PC: return pclink::everSeen();
    // tocando, a musica mora na faixa de baixo; a tela cheia fica para a ultima
    // musica de quando o PC sumiu
    case MUSIC: return !pclink::alive(now) && pclink::hasTrack();
    case TEMPCHART: return net.hasHourly;
    case RAINCHART: {  // so aparece em dia com chance de chuva
      if (!net.hasHourly) return false;
      for (int i = 0; i < 24; i++)
        if (net.hourRain[i] >= 25) return true;
      return false;
    }
    default: return true;
  }
}

static int L(float v);
static const int DESK_X = 54, DESK_Y = 33;  // lugar do rosto na tela do PC: a esquerda, ao computador
static const float DESK_SCALE = 0.5f;
static float squeeze = 0;  // 0 = tela inteira livre; 1 = faixa da musica ocupando o pe da tela
static int slide = 0;      // deslocamento da tela que esta entrando por baixo

void setDock(float amount) { squeeze = amount; }

bool faceSpot(Scene s, float& x, float& y, float& scale) {
  if (s == SPEECH && speech::page().icon == speech::Chat) {  // mensagem: rostinho no canto de cima
    x = 210, y = L(19) - slide, scale = 0.3f;
    return true;
  }
  if (s != PC) return false;
  x = DESK_X, y = L(DESK_Y) - slide, scale = DESK_SCALE * (1 - 0.14f * squeeze);
  return true;
}

// Converte uma altura do desenho de tela cheia (135 px) para o espaco que sobra
// agora: com a faixa da musica, tudo sobe e aperta um pouco
static int L(float v) {
  float compact = v < 40 ? v * 0.75f : 26 + (v - 40) * 0.84f;
  return lroundf(v + (compact - v) * squeeze) + slide;
}

static void clockScene(M5Canvas& c, uint32_t now) {
  struct tm t;
  if (!clockNow(t)) return;
  char buf[48];
  c.setFont(&text::big);
  c.setTextColor(col::white);
  c.setTextDatum(middle_right);
  snprintf(buf, sizeof(buf), "%02d", t.tm_hour);
  c.drawString(buf, 111, L(78));
  c.setTextDatum(middle_left);
  snprintf(buf, sizeof(buf), "%02d", t.tm_min);
  c.drawString(buf, 129, L(78));
  if (now % 1000 < 500) {
    c.setTextDatum(middle_center);
    c.drawString(":", 120, L(76));
  }
  snprintf(buf, sizeof(buf), "%s, %d de %s", DIAS[t.tm_wday], t.tm_mday, MESES[t.tm_mon]);
  c.setTextColor(col::orange);
  text::drawFit(c, buf, 120, L(119), 232);
}

static void tempScene(M5Canvas& c, uint32_t now) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%d°", (int)lroundf(net.tempC));
  c.setFont(&text::big);
  int w = c.textWidth(buf);
  // icone + numero centralizados juntos
  int left = (240 - (54 + 10 + w)) / 2;
  icons::weather(c, left + 27, L(78), net.wmo, net.isDay, now);
  c.setTextColor(icons::tempColor(net.tempC));
  c.setTextDatum(middle_left);
  c.drawString(buf, left + 64, L(78));

  snprintf(buf, sizeof(buf), "%s · %s", weatherText(net.wmo), net.city);
  c.setTextColor(col::grey);
  text::drawFit(c, buf, 120, L(119), 232);
}

static int currentHour();

// Como vai estar a chuva numa hora, em palavras: junta a chance com quanto deve
// cair. Cada nivel tem alguns jeitos de dizer, que vao se revezando; so entram
// os que cabem no espaco (o ultimo de cada lista e o mais curto).
struct RainLevel {
  const char* words[4];
  uint16_t color;
};
static const RainLevel RAIN_LEVELS[] = {
    {{"céu seco", "sem chuva", "tudo seco", "seco"}, col::grey},
    {{"pode chover", "talvez chova", "nuvem suspeita", "talvez"}, col::ice},
    {{"garoinha", "chuvisco", "garoa", "garoa"}, col::ice},
    {{"chuva leve", "chuvinha", "leve", "leve"}, col::blue},
    {{"chuva firme", "chuva", "molhado", "chuva"}, col::blue},
    {{"chuva forte", "chuvarada", "toró", "forte"}, col::orange},
    {{"MUITA CHUVA", "TEMPORAL", "DILÚVIO", "TORÓ!"}, col::red},
};

static const RainLevel& rainLevel(float mm, int pct) {
  int i = mm >= 10 ? 6 : mm >= 4 ? 5 : mm >= 1.5f ? 4 : mm >= 0.5f ? 3 : mm >= 0.05f ? 2 : pct >= 30 ? 1 : 0;
  return RAIN_LEVELS[i];
}

// Escreve a palavra da chuva em (x, y) com o alinhamento atual, sem passar de maxW
static void drawRainWord(M5Canvas& c, float mm, int pct, int x, int y, int maxW) {
  const RainLevel& lv = rainLevel(mm, pct);
  c.setFont(&text::small);
  const char* fits[4];
  int n = 0;
  for (const char* w : lv.words)
    if (c.textWidth(w) <= maxW) {
      bool dup = false;
      for (int k = 0; k < n; k++) dup |= strcmp(fits[k], w) == 0;
      if (!dup) fits[n++] = w;
    }
  const char* word = n ? fits[(millis() / 5000) % n] : lv.words[3];
  // nivel mais forte pulsa de leve, para chamar atencao
  uint16_t color = lv.color;
  if (&lv == &RAIN_LEVELS[6]) color = mix(col::red, col::white, 0.5f + 0.5f * sinf(millis() / 250.0f));
  c.setTextColor(color);
  if (c.getTextDatum() == middle_center) {  // centralizada: encosta na borda em vez de sair da tela
    int half = c.textWidth(word) / 2;
    x = constrain(x, half + 2, 238 - half);
  }
  c.drawString(word, x, y);
}

static void forecastScene(M5Canvas& c) {
  struct Column {
    int x;
    const char* label;
    char value[8];
  } cols[3] = {{32, "máx", ""}, {102, "mín", ""}, {180, "chuva", ""}};
  // Chuva: a chance desta hora (nao a maior do dia) e quanto deve cair nela
  int hour = currentHour();
  bool perHour = net.hasHourly && hour >= 0;
  if (perHour) cols[2].label = "";  // a palavra da chuva e desenhada a parte, mais abaixo
  snprintf(cols[0].value, 8, "%d°", (int)lroundf(net.maxC));
  snprintf(cols[1].value, 8, "%d°", (int)lroundf(net.minC));
  snprintf(cols[2].value, 8, "%d%%", perHour ? (int)net.hourRain[hour] : (int)net.rainPct);

  icons::arrow(c, cols[0].x, L(60), true, col::red);
  icons::arrow(c, cols[1].x, L(52), false, col::blue);
  icons::drop(c, cols[2].x, L(60), 6, col::blue);

  c.setTextDatum(middle_center);
  for (auto& column : cols) {
    c.setFont(&text::num);
    c.setTextColor(col::white);
    c.drawString(column.value, column.x, L(90));
    c.setFont(&text::small);
    c.setTextColor(col::grey);
    int half = c.textWidth(column.label) / 2;  // texto comprido nao sai pela borda
    c.drawString(column.label, constrain(column.x, half + 2, 238 - half), L(121));
  }
  if (perHour) drawRainWord(c, net.hourMm[hour], net.hourRain[hour], cols[2].x + 4, L(121), 150);
}

// "sem PC há 5 min": avisa que a informacao na tela e antiga
static void staleLabel(M5Canvas& c, const char* prefix, int x, int y, uint32_t now) {
  uint32_t min = pclink::staleMs(now) / 60000;
  char buf[40];
  if (min < 1) snprintf(buf, sizeof(buf), "%shá instantes", prefix);
  else if (min < 60) snprintf(buf, sizeof(buf), "%shá %u min", prefix, (unsigned)min);
  else snprintf(buf, sizeof(buf), "%shá %u h", prefix, (unsigned)(min / 60));
  c.setFont(&text::small);
  c.setTextColor(col::orange);
  c.setTextDatum(middle_center);
  c.drawString(buf, x, y);
}

static void gauge(M5Canvas& c, int x, int y, float pct, const char* label, bool stale) {
  pct = constrain(pct, 0.0f, 100.0f);
  uint16_t color = stale ? col::grey : pct < 60 ? col::green : pct < 85 ? col::yellow : col::red;
  c.fillArc(x, y, 14, 19, 135, 405, col::dim);
  if (pct >= 1) c.fillArc(x, y, 14, 19, 135, 135 + 2.7f * pct, color);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", (int)lroundf(pct));
  c.setTextDatum(middle_center);
  c.setFont(&text::small);
  c.setTextColor(col::white);
  c.drawString(buf, x, y);
  c.setTextColor(col::grey);
  c.drawString(label, x, y + 27);
}

// Bracinho: uma capsula branca de (x0, y0) ate a patinha em (x1, y1)
static void arm(M5Canvas& c, float x0, float y0, float x1, float y1) {
  for (int i = 0; i <= 6; i++) c.fillCircle(x0 + (x1 - x0) * i / 6, y0 + (y1 - y0) * i / 6, 4, col::white);
  c.fillCircle(x1, y1, 6, col::white);
}

// Fone de ouvido em volta do rosto (x, y = centro dos olhos; scale = o mesmo do rosto)
static float headDx = 0, headDy = 0, headLean = 0;
void setHeadPose(float dx, float dy, float lean) { headDx = dx, headDy = dy, headLean = lean; }

void drawHeadset(M5Canvas& c, int x, int y, float s) {
  x += lroundf(headDx * s);  // acompanha a cabeca: dancando, o fone danca junto
  y += lroundf(headDy * s);
  float tilt = headLean * 8 * s;  // inclinado, uma concha desce e a outra sobe, como os olhos
  c.fillArc(x, y + 8 * s, 68 * s, 74 * s, 205, 335, col::grey);  // arco por cima
  c.fillRoundRect(x - 84 * s, y - 18 * s - tilt, 20 * s, 44 * s, 8 * s, col::blue);  // conchas
  c.fillRoundRect(x + 64 * s, y - 18 * s + tilt, 20 * s, 44 * s, 8 * s, col::blue);
  y -= lroundf(tilt);  // o microfone sai da concha esquerda
  for (int o = 0; o < 2; o++)  // haste do microfone
    c.drawLine(x - 74 * s, y + 26 * s + o, x - 52 * s, y + 42 * s + o, col::grey);
  c.fillCircle(x - 48 * s, y + 44 * s, max(6 * s, 2.0f), col::blue);
}

// Ele tambem esta no computador: de fone, debrucado sobre a mesa, com o mouse de
// um lado e o teclado do outro, dando tapinhas alternados nos dois
static void deskScene(M5Canvas& c, uint32_t now, bool stale) {
  const int fx = DESK_X, fy = L(DESK_Y);
  drawHeadset(c, fx, fy, DESK_SCALE);

  // Patinhas: cada tecla que voce aperta no PC e uma batida da direita no teclado;
  // cada clique, uma da esquerda no mouse. Sem digitar, as duas ficam no ar; sem o
  // PC, descansam na mesa.
  static int keyQueue = 0, clickQueue = 0, litKey = 0;
  static uint32_t keyAt = 0, clickAt = 0;
  keyQueue = min(keyQueue + pclink::takeKeys(), 4);
  clickQueue = min(clickQueue + pclink::takeClicks(), 2);
  if (keyQueue && now - keyAt > 120) keyQueue--, keyAt = now, litKey = random(24);
  if (clickQueue && now - clickAt > 160) clickQueue--, clickAt = now;
  bool rightDown = stale || (keyAt && now - keyAt < 70), leftDown = stale || (clickAt && now - clickAt < 90);

  // mesa vista de cima e meio de lado: tapete do mouse a esquerda, teclado a direita
  const int top = L(84), h = L(108) - L(84);  // a mesa encolhe junto com a tela
  auto dy = [&](float f) { return top + lroundf(h * f); };
  // tapete (paralelogramo) e mouse
  c.fillTriangle(4, dy(0.30f), 40, dy(0.02f), 46, dy(0.70f), col::dim);
  c.fillTriangle(4, dy(0.30f), 46, dy(0.70f), 10, dy(1.0f), col::dim);
  int mouseX = 25 + (stale ? 0 : lroundf(sinf(now / 600.0f) * 3));
  c.fillEllipse(mouseX, dy(0.50f), 8, 6, col::cloud);
  c.drawLine(mouseX - 1, dy(0.50f) - 5, mouseX + 2, dy(0.50f) - 1, col::dim);
  // teclado inclinado para o outro lado, com as fileiras de teclas
  c.fillTriangle(58, dy(0.02f), 106, dy(0.30f), 102, dy(1.0f), col::dim);
  c.fillTriangle(58, dy(0.02f), 102, dy(1.0f), 54, dy(0.72f), col::dim);
  for (int row = 0; row < 3; row++) {
    for (int k = 0; k < 8; k++) {
      int kx = 60 + k * 5 - row, ky = dy(0.10f + row * 0.22f + k * 0.034f);
      bool hit = rightDown && !stale && row * 8 + k == litKey;
      c.fillRect(kx, ky, 4, 3, hit ? col::green : col::grey);
    }
  }

  // bracinhos: no alto quando levantados, em cima do mouse / do teclado quando batem
  float lx = leftDown ? mouseX : 30, ly = leftDown ? dy(0.42f) : dy(-0.45f);
  float rx = rightDown ? 78 : 76, ry = rightDown ? dy(0.42f) : dy(-0.45f);
  arm(c, fx - 14, fy + 22, lx, ly);
  arm(c, fx + 14, fy + 22, rx, ry);
}

// Ventoinha que gira na velocidade do cooler
static void fan(M5Canvas& c, int x, int y, float pct, bool stale) {
  static float angle = 0;
  static uint32_t last = 0;
  uint32_t now = millis();
  float dt = min((now - last) / 1000.0f, 0.1f);
  last = now;
  if (!stale) angle = fmodf(angle + dt * (40 + pct * 9), 360);  // graus por segundo
  c.drawCircle(x, y, 12, col::dim);
  for (int i = 0; i < 3; i++) c.fillArc(x, y, 3, 10, angle + i * 120, angle + i * 120 + 62, stale ? col::grey : col::ice);
  c.fillCircle(x, y, 3, col::white);
}

// Texto numa faixa [x, x+w]: centralizado se couber; se nao, vai e volta
static void marquee(M5Canvas& c, const char* s, int x, int w, int y, uint32_t now) {
  int tw = c.textWidth(s);
  if (tw <= w) {
    c.setTextDatum(middle_center);
    c.drawString(s, x + w / 2, y);
    return;
  }
  int range = tw - w, period = range * 2 + 100;
  int p = (now / 28) % period;
  int off = p < 50 ? 0 : p < 50 + range ? p - 50 : p < 100 + range ? range : period - p;
  c.setClipRect(x, y - 16, w, 32);
  c.setTextDatum(middle_left);
  c.drawString(s, x - off, y);
  c.clearClipRect();
}

// Selo de video: retangulo vermelho arredondado com o triangulo branco de "play"
static void playBadge(M5Canvas& c, int x, int y, float scale) {
  int w = lroundf(24 * scale), h = lroundf(17 * scale);
  c.fillRoundRect(x - w / 2, y - h / 2, w, h, 5, col::red);
  c.fillTriangle(x - 4, y - 5, x - 4, y + 5, x + 6, y, col::white);
}

static void musicScene(M5Canvas& c, uint32_t now) {
  if (gifs::active()) {
    // musica comecando: GIF de um lado; capa, nome e artista do outro
    gifs::draw(c, 8, 17 + slide);
    const int x = 116, w = 118;
    if (const uint16_t* cover = pclink::cover())
      c.pushImage(x + (w - pclink::COVER) / 2, 8 + slide, pclink::COVER, pclink::COVER, cover);
    else
      icons::note(c, x + w / 2 - 6, 62 + slide, 22, col::spotify);
    c.setFont(&text::medium);
    c.setTextColor(col::white);
    marquee(c, pclink::title().c_str(), x, w, 97 + slide, now);
    c.setFont(&text::small);
    c.setTextColor(col::grey);
    marquee(c, pclink::artist().c_str(), x, w, 120 + slide, now);
    return;
  }
  // com capa: capa a esquerda e texto a direita; sem capa: tudo centralizado
  const uint16_t* cover = pclink::cover();
  const int x = cover ? 92 : 8, w = cover ? 140 : 224;
  if (cover) c.pushImage(10, L(46), pclink::COVER, pclink::COVER, cover);

  c.setFont(&text::medium);
  c.setTextColor(col::white);
  marquee(c, pclink::title().c_str(), x, w, L(60), now);
  c.setFont(&text::small);
  c.setTextColor(col::grey);
  marquee(c, pclink::artist().c_str(), x, w, L(84), now);

  if (!pclink::playing()) {  // musica antiga, de antes de perder o PC
    staleLabel(c, "", x + w / 2, L(112), now);
    return;
  }
  if (pclink::video()) {  // video: selo vermelho de "play" em vez das barrinhas de musica
    const char* label = pclink::youtube() ? "YouTube" : "vídeo";
    c.setFont(&text::small);
    int tw = c.textWidth(label), left = x + w / 2 - (tw + 30) / 2;
    playBadge(c, left + 12, L(112), 1 + 0.08f * sinf(now / 300.0f));
    c.setTextColor(col::white);
    c.setTextDatum(middle_left);
    c.drawString(label, left + 30, L(112));
    return;
  }
  // barrinhas de equalizador
  for (int i = 0; i < 9; i++) {
    int h = 3 + 16 * fabsf(sinf(now / (170.0f + i * 31) + i * 1.7f));
    c.fillRoundRect(x + w / 2 - 43 + i * 10, L(122) - h, 6, h, 2, col::spotify);
  }
}

// Graficos de hoje, hora a hora: area util x 14..226, y 50..108 na tela cheia
static const int CHART_X = 14, CHART_W = 212;
static int chartTop() { return L(50); }
static int chartBottom() { return L(108); }

static void chartAxis(M5Canvas& c) {
  c.drawFastHLine(CHART_X, chartBottom() + 2, CHART_W + 1, col::dim);
  c.setFont(&text::small);
  c.setTextColor(col::grey);
  c.setTextDatum(middle_center);
  static const char* LABELS[] = {"0h", "6h", "12h", "18h"};
  for (int i = 0; i < 4; i++) c.drawString(LABELS[i], CHART_X + CHART_W * i * 6 / 23 + 4, L(123));
}

static int currentHour() {
  struct tm t;
  return clockNow(t) ? t.tm_hour : -1;
}

static void tempChart(M5Canvas& c) {
  float lo = net.hourTemp[0], hi = lo;
  for (int i = 1; i < 24; i++) {
    lo = min(lo, net.hourTemp[i]);
    hi = max(hi, net.hourTemp[i]);
  }
  float span = max(hi - lo, 1.0f);
  auto px = [](int i) { return CHART_X + CHART_W * i / 23; };
  auto py = [&](float t) { return chartBottom() - (int)((t - lo) / span * (chartBottom() - chartTop())); };

  char buf[16];
  c.setFont(&text::small);
  c.setTextDatum(middle_left);
  c.setTextColor(col::red);
  snprintf(buf, sizeof(buf), "máx %d°", (int)lroundf(hi));
  c.drawString(buf, 8, L(20));
  c.setTextDatum(middle_right);
  c.setTextColor(col::blue);
  snprintf(buf, sizeof(buf), "mín %d°", (int)lroundf(lo));
  c.drawString(buf, 232, L(20));

  chartAxis(c);
  int hour = currentHour();
  if (hour >= 0) c.drawFastVLine(px(hour), chartTop() - 4, chartBottom() - chartTop() + 6, col::dim);
  for (int i = 0; i < 23; i++) {
    uint16_t color = icons::tempColor((net.hourTemp[i] + net.hourTemp[i + 1]) / 2);
    int x0 = px(i), y0 = py(net.hourTemp[i]), x1 = px(i + 1), y1 = py(net.hourTemp[i + 1]);
    for (int o = -1; o <= 1; o++) c.drawLine(x0, y0 + o, x1, y1 + o, color);
  }
  if (hour >= 0) {
    c.fillCircle(px(hour), py(net.hourTemp[hour]), 5, col::black);
    c.fillCircle(px(hour), py(net.hourTemp[hour]), 4, col::white);
  }
}

static void rainChart(M5Canvas& c) {
  int peak = 0;
  for (int i = 1; i < 24; i++)
    if (net.hourRain[i] > net.hourRain[peak]) peak = i;

  char buf[40];
  icons::drop(c, 14, L(23), 5, col::blue);
  c.setFont(&text::small);
  // no topo: esta hora (chance e quanto cai); sem hora certa, o pico do dia
  int hour = currentHour();
  int at = hour >= 0 ? hour : peak;
  // as pontas do topo ficam livres do rostinho, que mora no meio
  snprintf(buf, sizeof(buf), "%dh · %d%%", at, net.hourRain[at]);
  c.setTextDatum(middle_left);
  c.setTextColor(col::blue);
  c.drawString(buf, 24, L(20));
  c.setTextDatum(middle_right);
  drawRainWord(c, net.hourMm[at], net.hourRain[at], 237, L(38), 108);  // numa linha propria: nao cabe ao lado do rostinho

  chartAxis(c);
  // marquinha com o horario do pico, em cima da barra mais alta
  int peakX = CHART_X + CHART_W * peak / 23;
  int peakH = net.hourRain[peak] * (chartBottom() - chartTop()) / 100;
  bool underWord = peakX > 105 && chartBottom() - peakH - 14 < L(46);  // cairia em cima do texto de cima
  if (net.hourRain[peak] > 0 && peak != hour && !underWord) {
    char tag[8];
    snprintf(tag, sizeof(tag), "%dh", peak);
    int h = peakH;
    c.setTextDatum(bottom_center);
    c.setTextColor(col::grey);
    c.drawString(tag, constrain(CHART_X + CHART_W * peak / 23, 12, 228), chartBottom() - h - 2);
  }
  for (int i = 0; i < 24; i++) {
    int h = max(net.hourRain[i] * (chartBottom() - chartTop()) / 100, 1);
    // horas que ja passaram ficam apagadas; a hora atual, mais clara
    uint16_t color = i == hour ? col::ice : i < hour ? mix(col::blue, col::black, 0.6f) : col::blue;
    c.fillRoundRect(CHART_X + CHART_W * i / 23 - 3, chartBottom() - h, 7, h + 1, 2, color);
  }
}

// Faixa "tocando agora" no pe da tela: capa pequena, titulo e artista rolando, barrinhas
void drawDock(M5Canvas& c, float amount, uint32_t now) {
  const int top = 135 - lroundf(DOCK_H * amount);
  c.fillRect(0, top, 240, DOCK_H, rgb(20, 24, 30));
  c.drawFastHLine(0, top, 240, col::dim);

  int x = 6;
  if (const uint16_t* thumb = pclink::thumb()) {
    c.pushImage(3, top + 2, pclink::THUMB, pclink::THUMB, thumb);
    x = pclink::THUMB + 9;
  }
  // titulo em branco e artista em cinza, rolando juntos se nao couberem
  const int w = 200 - x;
  c.setFont(&text::small);
  String sep = pclink::artist().length() ? "  ·  " : "";
  int tw = c.textWidth(pclink::title().c_str()), sw = c.textWidth(sep.c_str());
  int total = tw + sw + c.textWidth(pclink::artist().c_str());
  int off = 0;
  if (total > w) {
    int range = total - w, period = range * 2 + 160;
    int p = (now / 30) % period;
    off = p < 80 ? 0 : p < 80 + range ? p - 80 : p < 160 + range ? range : period - p;
  }
  c.setClipRect(x, top + 1, w, DOCK_H - 1);
  c.setTextDatum(middle_left);
  c.setTextColor(col::white);
  c.drawString(pclink::title().c_str(), x - off, top + 14);
  c.setTextColor(col::grey);
  c.drawString((sep + pclink::artist()).c_str(), x - off + tw, top + 14);
  c.clearClipRect();

  if (pclink::video()) {
    playBadge(c, 220, top + 14, 1);
    return;
  }
  for (int i = 0; i < 5; i++) {
    int h = 3 + 15 * fabsf(sinf(now / (170.0f + i * 37) + i * 1.7f));
    c.fillRoundRect(206 + i * 6, top + 23 - h, 4, h, 1, col::spotify);
  }
}

static const uint32_t CHAT_HOLD_MS = 1800;  // parada no comeco e no fim da rolagem
static const int CHAT_SCROLL_SPEED = 13;    // pixels por segundo: da para ler sem correr
static uint32_t chatScrollCycle(int dist) { return CHAT_HOLD_MS * 2 + dist * 1000 / CHAT_SCROLL_SPEED; }

// Mensagem recebida, com cara de conversa: bolinha com a inicial de quem mandou,
// o nome ao lado e o texto num balao que cresce conforme o tamanho da mensagem.
// O rostinho vai para o canto de cima, curioso (ver faceSpot).
static void chatCard(M5Canvas& c, const speech::Page& p, uint32_t now) {
  const uint16_t bubble = rgb(30, 44, 52);
  // cabecalho: inicial num circulo verde + nome
  const int ay = L(19);
  c.fillCircle(21, ay, 14, col::spotify);
  char initial[5] = "?";
  const char* name = p.title.c_str();
  int bytes = (uint8_t)name[0] >= 0xC0 ? 2 : 1;  // letra acentuada ocupa dois bytes
  if (name[0]) {
    memcpy(initial, name, bytes);
    initial[bytes] = 0;
  }
  c.setFont(&text::medium);
  c.setTextColor(col::black);
  c.setTextDatum(middle_center);
  c.drawString(initial, 21, ay);
  c.setTextColor(col::white);
  int tw = min((int)c.textWidth(name), 136);
  c.setClipRect(42, ay - 14, 138, 28);
  c.setTextDatum(middle_left);
  if (c.textWidth(name) <= 136) {
    c.drawString(name, 42, ay);
  } else {  // nome comprido (grupos): vai e volta
    int range = c.textWidth(name) - 136, period = range * 2 + 120;
    int q = (now / 30) % period;
    c.drawString(name, 42 - (q < 60 ? 0 : q < 60 + range ? q - 60 : q < 120 + range ? range : period - q), ay);
  }
  c.clearClipRect();
  (void)tw;

  // balao: altura conforme o texto, com a pontinha virada para quem mandou
  const int top = L(40), room = max(L(132) - top - 14, 20);
  const char* body = p.text.c_str();
  int lines = text::smallLines(c, body, 208);
  bool scrolls = lines * text::SMALL_LINE > room;
  int h = scrolls ? room + 14 : text::drawLeft(c, body, 0, 0, 208, room, false) + 14;
  c.fillRoundRect(8, top, 224, h, 10, bubble);
  c.fillTriangle(14, top + 1, 30, top + 1, 16, top - 7, bubble);
  c.setTextColor(col::white);
  if (!scrolls) {
    text::drawLeft(c, body, 16, top + 7, 208, room);
    return;
  }
  // Mensagem comprida: para um instante no comeco, rola ate o fim, para de novo
  // e recomeca (a duracao da pagina, calculada em chatDuration, da duas passadas)
  int dist = lines * text::SMALL_LINE - room;
  uint32_t cycle = chatScrollCycle(dist);
  uint32_t at = (now - speech::pageStart()) % cycle;
  int offset = at < CHAT_HOLD_MS ? 0 : min(dist, (int)((at - CHAT_HOLD_MS) * CHAT_SCROLL_SPEED / 1000));
  text::drawScrolled(c, body, 16, top + 7, 204, room, offset);
  // barrinha de rolagem
  int track = room - 4, thumb = max(track * room / (lines * text::SMALL_LINE), 8);
  c.fillRoundRect(225, top + 9 + (track - thumb) * offset / dist, 3, thumb, 1, col::grey);
}

uint32_t chatDuration(M5Canvas& c, const char* body) {
  int lines = text::smallLines(c, body, 208);
  const int room = 78;  // altura do balao sem a faixa de musica
  if (lines * text::SMALL_LINE <= room) return 0;  // cabe: usa o tempo normal
  return 2 * chatScrollCycle(lines * text::SMALL_LINE - room);
}

// Barulho do ambiente: uma barra por minuto, do mais antigo (esquerda) ao de agora
static void noiseScene(M5Canvas& c) {
  auto shade = [](int v) { return v < 45 ? col::green : v < 68 ? col::yellow : col::red; };
  int nowV = ears::noiseNow();
  const char* word = nowV < 25 ? "silêncio" : nowV < 45 ? "calmo" : nowV < 68 ? "normal" : "barulhento";

  c.setFont(&text::small);
  c.setTextDatum(middle_left);
  c.setTextColor(col::grey);
  c.drawString("barulho", 8, L(20));
  c.setTextDatum(middle_right);
  c.setTextColor(shade(nowV));
  c.drawString(word, 232, L(20));

  const uint8_t* points;
  int n = ears::noiseHistory(points);
  const int base = chartBottom(), height = chartBottom() - chartTop();
  c.drawFastHLine(CHART_X, base + 2, CHART_W + 1, col::dim);
  for (int i = 0; i < n; i++) {
    int x = CHART_X + CHART_W - (n - i) * 2, h = max(points[i] * height / 100, 1);
    c.fillRect(x, base - h, 2, h + 1, shade(points[i]));
  }
  // a barra de agora, mais larga, na ponta direita
  int h = max(nowV * height / 100, 1);
  c.fillRect(CHART_X + CHART_W + 2, base - h, 5, h + 1, col::white);

  c.setTextColor(col::grey);
  c.setTextDatum(middle_left);
  c.drawString(n < 10 ? "medindo..." : n < 60 ? "última hora" : "últimas 2 horas", CHART_X, L(123));
  c.setTextDatum(middle_right);
  c.drawString("agora", CHART_X + CHART_W + 8, L(123));
}

void draw(M5Canvas& c, Scene s, int y, uint32_t now) {
  slide = y;
  switch (s) {
    case CLOCK:
      clockScene(c, now);
      break;
    case TEMP:
      tempScene(c, now);
      break;
    case FORECAST:
      forecastScene(c);
      break;
    case PC: {
      bool stale = !pclink::alive(now);
      deskScene(c, now, stale);
      // medidores a direita
      if (pclink::hasGpu()) {
        gauge(c, 126, L(56), pclink::cpu(), "CPU", stale);
        gauge(c, 170, L(56), pclink::ram(), "RAM", stale);
        gauge(c, 214, L(56), pclink::gpu(), "GPU", stale);
      } else {
        gauge(c, 140, L(56), pclink::cpu(), "CPU", stale);
        gauge(c, 200, L(56), pclink::ram(), "RAM", stale);
      }
      // cooler: ventoinha girando + velocidade; temperatura da placa de video ao lado
      char buf[24];
      fan(c, 120, L(114), pclink::fanPct(), stale);
      if (pclink::fanRpm() > 0) snprintf(buf, sizeof(buf), "%d rpm", pclink::fanRpm());
      else snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(pclink::fanPct()));
      c.setFont(&text::small);
      c.setTextDatum(middle_left);
      c.setTextColor(col::white);
      if (!stale) c.drawString(buf, 137, L(114));
      if (stale) {  // ventoinha parada e ha quanto tempo o PC sumiu
        staleLabel(c, "", 188, L(114), now);
      } else if (pclink::hasGpu()) {
        snprintf(buf, sizeof(buf), "GPU %d°", (int)lroundf(pclink::gpuTemp()));
        c.setTextDatum(middle_right);
        c.setTextColor(icons::tempColor(pclink::gpuTemp() / 2.4f));  // 40 graus = fria, 80 = quente
        c.drawString(buf, 236, L(114));
      }
      break;
    }
    case MUSIC:
      musicScene(c, now);
      break;
    case TEMPCHART:
      tempChart(c);
      break;
    case NOISE:
      noiseScene(c);
      break;
    case RAINCHART:
      rainChart(c);
      break;
    case SPEECH: {
      const speech::Page& p = speech::page();
      if (p.icon == speech::Water) {
        icons::drop(c, 52, L(24), 7, col::blue);
        icons::drop(c, 188, L(24), 7, col::blue);
      }
      if (p.icon == speech::News) {  // manchete: jornalzinho + fonte em amarelo, texto embaixo
        c.setFont(&text::small);
        int tw = min((int)c.textWidth(p.title.c_str()), 190);
        int left = 120 - (tw + 24) / 2;
        c.fillRoundRect(left, L(45), 17, 13, 2, col::white);
        c.fillRect(left + 3, L(45) + 3, 11, 2, col::black);
        c.fillRect(left + 3, L(45) + 7, 5, 4, col::black);
        c.fillRect(left + 10, L(45) + 7, 4, 1, col::black);
        c.fillRect(left + 10, L(45) + 10, 4, 1, col::black);
        c.setTextColor(col::yellow);
        marquee(c, p.title.c_str(), left + 24, tw, L(52), now);
        c.setTextColor(col::white);
        text::drawWrapped(c, p.text.c_str(), 120, L(99), 232, 66 - 12 * squeeze);
        break;
      }
      if (p.icon == speech::Chat) {
        chatCard(c, p, now);
        break;
      }
      c.setTextColor(col::white);
      text::drawWrapped(c, p.text.c_str(), 120, L(87), 228, 92 - 20 * squeeze);
      break;
    }
    case SETUP:
      c.setTextColor(col::white);
      text::drawFit(c, "Me conecta no Wi-Fi!", 120, L(62), 232);
      c.setFont(&text::small);
      c.setTextColor(col::grey);
      c.drawString("pelo celular, entre na rede", 120, L(90));
      c.setTextColor(col::yellow);
      text::drawFit(c, AP_NAME, 120, L(116), 232);
      break;
    default:
      break;
  }
}

}  // namespace scenes
