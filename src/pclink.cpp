#include "pclink.h"

#include "net.h"

namespace pclink {

static String line, title_, artist_;
static float cpu_ = 0, ram_ = 0, gpu_ = 0, gpuTemp_ = 0;
static bool hasGpu_ = false;
static int bpm_ = 0;
static char kind_ = '1';  // '1' musica, '2' video, '3' YouTube
static float fanPct_ = 30;
static int fanRpm_ = 0;
static int keys_ = 0, clicks_ = 0;
static uint32_t lastKey = 0;
static bool everKey = false, gaming_ = false;
static String game_;
static long userIdle = 0;  // segundos sem teclado/mouse no PC
static bool playing_ = false, changed_ = false, shot_ = false, everPc = false;
static uint32_t lastPc = 0, lastNp = 0;
static uint8_t coverPx[COVER * COVER * 2];
static uint8_t thumbPx[THUMB * THUMB * 2];
static bool coverReady = false, linked = false;

// Miniatura: media dos pixels de cada bloquinho da capa (os pixels ficam
// guardados com os bytes na ordem que a tela espera: alto, baixo)
static void makeThumb() {
  for (int ty = 0; ty < THUMB; ty++) {
    for (int tx = 0; tx < THUMB; tx++) {
      int x0 = tx * COVER / THUMB, x1 = (tx + 1) * COVER / THUMB;
      int y0 = ty * COVER / THUMB, y1 = (ty + 1) * COVER / THUMB;
      int r = 0, g = 0, b = 0, n = 0;
      for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++, n++) {
          const uint8_t* p = &coverPx[(y * COVER + x) * 2];
          uint16_t v = p[0] << 8 | p[1];
          r += v >> 11, g += (v >> 5) & 63, b += v & 31;
        }
      }
      uint16_t v = (r / n) << 11 | (g / n) << 5 | (b / n);
      thumbPx[(ty * THUMB + tx) * 2] = v >> 8;
      thumbPx[(ty * THUMB + tx) * 2 + 1] = v & 0xFF;
    }
  }
}

static int hexVal(char ch) { return ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10; }

static void coverRow(const String& arg) {
  int sp = arg.indexOf(' ');
  int row = arg.toInt();
  if (sp < 0 || row < 0 || row >= COVER || (int)arg.length() - sp - 1 < COVER * 4) return;
  const char* hex = arg.c_str() + sp + 1;
  uint8_t* out = &coverPx[row * COVER * 2];
  for (int i = 0; i < COVER * 2; i++) out[i] = hexVal(hex[i * 2]) << 4 | hexVal(hex[i * 2 + 1]);
  if (row == COVER - 1) {
    makeThumb();
    coverReady = true;
  }
}

static void handle(uint32_t now) {
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);

  if (cmd == "pc") {
    hasGpu_ = sscanf(arg.c_str(), "%f %f %f %f", &cpu_, &ram_, &gpu_, &gpuTemp_) == 4;
    lastPc = now;
    everPc = true;
  } else if (cmd == "np") {
    int t1 = arg.indexOf('\t'), t2 = arg.indexOf('\t', t1 + 1);
    if (t1 < 0 || t2 < 0) return;
    char kind = arg.charAt(0);
    bool nowPlaying = kind >= '1' && kind <= '3';
    if (nowPlaying) kind_ = kind;
    String newTitle = arg.substring(t1 + 1, t2);
    if (nowPlaying && (newTitle != title_ || !playing_)) changed_ = true;
    if (newTitle != title_) {  // a capa e o andamento da nova vem logo em seguida
      coverReady = false;
      bpm_ = 0;
    }
    playing_ = nowPlaying;
    title_ = newTitle;
    artist_ = arg.substring(t2 + 1);
    lastNp = now;
  } else if (cmd == "clock") {
    long utc = 0, offset = 0;
    if (sscanf(arg.c_str(), "%ld %ld", &utc, &offset) == 2 && utc > 1700000000) setClock(utc, offset);
  } else if (cmd == "weather") {
    float temp, hi, lo, mm = 0;
    int wmo, day, rain;
    int tab = arg.indexOf('\t');
    if (sscanf(arg.c_str(), "%f %d %d %f %f %d %f", &temp, &wmo, &day, &hi, &lo, &rain, &mm) >= 6) {
      net.rainingMm = mm;
      net.tempC = temp, net.wmo = wmo, net.isDay = day, net.maxC = hi, net.minC = lo, net.rainPct = rain;
      if (tab > 0) strlcpy(net.city, arg.c_str() + tab + 1, sizeof(net.city));
      net.hasWeather = true;
    }
  } else if (cmd == "hourly") {
    // 24 temperaturas separadas por virgula, ponto e virgula, 24 chances de chuva
    const char* p = arg.c_str();
    int n = 0;
    for (; n < 72 && *p; n++) {
      char* end;
      float v = strtof(p, &end);
      if (end == p) break;
      if (n < 24) net.hourTemp[n] = v;
      else if (n < 48) net.hourRain[n - 24] = constrain((int)v, 0, 100);
      else net.hourMm[n - 48] = max(v, 0.0f);
      p = *end ? end + 1 : end;
    }
    if (n >= 48) net.hasHourly = true;
  } else if (cmd == "type") {
    int k = 0, c = 0;
    sscanf(arg.c_str(), "%d %d", &k, &c);
    keys_ = min(keys_ + k, 12), clicks_ = min(clicks_ + c, 6);
    if (k) lastKey = now, everKey = true;
  } else if (cmd == "game") {
    gaming_ = arg.charAt(0) == '1';
    int tab = arg.indexOf('\t');
    if (gaming_ && tab > 0) game_ = arg.substring(tab + 1);
  } else if (cmd == "fan") {
    sscanf(arg.c_str(), "%f %d", &fanPct_, &fanRpm_);
  } else if (cmd == "user") {
    userIdle = arg.toInt();
  } else if (cmd == "bpm") {
    bpm_ = arg.toInt();
  } else if (cmd == "img") {
    coverRow(arg);
  } else if (cmd == "shot") {
    shot_ = true;
  } else if (cmd.length()) {
    onDebugCommand(cmd, arg);
  }
}

void update(uint32_t now) {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') {
      handle(now);
      line = "";
    } else if (ch != '\r' && line.length() < 420) {
      line += ch;
    }
  }
  linked = everPc && now - lastPc < 10000;
}

bool alive(uint32_t now) { return everPc && now - lastPc < 10000; }
bool userAway(uint32_t now) { return alive(now) && userIdle > 300; }
bool everSeen() { return everPc; }
uint32_t staleMs(uint32_t now) { return now - lastPc; }
float cpu() { return cpu_; }
float ram() { return ram_; }
bool hasGpu() { return hasGpu_; }
float gpu() { return gpu_; }
float gpuTemp() { return gpuTemp_; }
float fanPct() { return fanPct_; }
int fanRpm() { return fanRpm_; }
int takeKeys() {
  int k = keys_;
  keys_ = 0;
  return k;
}
int takeClicks() {
  int c = clicks_;
  clicks_ = 0;
  return c;
}
bool typing(uint32_t now) { return everKey && linked && now - lastKey < 1500; }
bool gaming() { return gaming_ && linked; }
const String& game() { return game_; }
bool playing() { return playing_ && linked; }
bool video() { return kind_ >= '2'; }
bool youtube() { return kind_ == '3'; }
bool hasTrack() { return title_.length() > 0; }
const String& title() { return title_; }
const String& artist() { return artist_; }
const uint16_t* cover() { return coverReady ? (const uint16_t*)coverPx : nullptr; }
const uint16_t* thumb() { return coverReady ? (const uint16_t*)thumbPx : nullptr; }

int bpm() { return bpm_; }

bool takeTrackChange() {
  bool c = changed_;
  changed_ = false;
  return c;
}
bool takeShotRequest() {
  bool s = shot_;
  shot_ = false;
  return s;
}

// Tela comprimida em sequencias [repeticoes][pixel de 2 bytes]
void sendShot(M5Canvas& c) {
  const uint16_t* px = (const uint16_t*)c.getBuffer();
  const size_t total = (size_t)c.width() * c.height();

  size_t runs = 0;
  for (size_t i = 0; i < total;) {
    size_t n = 1;
    while (i + n < total && n < 255 && px[i + n] == px[i]) n++;
    i += n;
    runs++;
  }
  Serial.printf("\nSHOT %d %d %u\n", (int)c.width(), (int)c.height(), (unsigned)(runs * 3));

  uint8_t out[240];
  size_t o = 0;
  for (size_t i = 0; i < total;) {
    size_t n = 1;
    while (i + n < total && n < 255 && px[i + n] == px[i]) n++;
    out[o++] = n;
    memcpy(&out[o], &px[i], 2);
    o += 2;
    if (o == sizeof(out)) {
      Serial.write(out, o);
      o = 0;
    }
    i += n;
  }
  Serial.write(out, o);
  Serial.flush();
}

}  // namespace pclink
