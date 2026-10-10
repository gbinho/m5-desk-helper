#include "icons.h"

#include "util.h"

namespace icons {

void drop(M5Canvas& c, float x, float y, float r, uint16_t color) {
  c.fillCircle(x, y, r, color);
  c.fillTriangle(x - r * 0.9f, y - r * 0.4f, x + r * 0.9f, y - r * 0.4f, x, y - r * 2.1f, color);
}

void arrow(M5Canvas& c, int x, int y, bool up, uint16_t color) {
  int d = up ? -1 : 1;
  c.fillTriangle(x - 8, y, x + 8, y, x, y + d * 9, color);
  c.fillRect(x - 3, up ? y : y - 8, 6, 8, color);
}

void note(M5Canvas& c, float x, float y, float size, uint16_t color) {
  float stem = max(size * 0.2f, 1.5f);
  c.fillEllipse(x, y, size * 0.6f, size * 0.45f, color);
  c.fillRect(x + size * 0.6f - stem, y - size * 1.8f, stem, size * 1.8f, color);
  c.fillTriangle(x + size * 0.6f, y - size * 1.8f, x + size * 1.4f, y - size * 1.1f, x + size * 0.6f,
                 y - size * 1.0f, color);
}

void wifiOff(M5Canvas& c, int x, int y) {
  c.fillArc(x, y + 6, 9, 11, 225, 315, col::grey);
  c.fillArc(x, y + 6, 4, 6, 225, 315, col::grey);
  c.fillCircle(x, y + 6, 1, col::grey);
  c.drawLine(x - 7, y - 6, x + 7, y + 8, col::red);
  c.drawLine(x - 6, y - 6, x + 8, y + 8, col::red);
}

uint16_t tempColor(float t) {
  if (t < 12) return col::blue;
  if (t < 20) return mix(col::blue, col::white, (t - 12) / 8);
  if (t < 26) return mix(col::white, col::yellow, (t - 20) / 6);
  if (t < 32) return mix(col::yellow, col::orange, (t - 26) / 6);
  return mix(col::orange, col::red, (t - 32) / 5);
}

static void sun(M5Canvas& c, int x, int y, int r, uint32_t now) {
  float spin = now / 4000.0f;
  for (int i = 0; i < 8; i++) {
    float a = spin + i * 0.7854f, ca = cosf(a), sa = sinf(a);
    float bx = x + ca * (r + 3), by = y + sa * (r + 3);
    c.fillTriangle(bx - sa * 2.5f, by + ca * 2.5f, bx + sa * 2.5f, by - ca * 2.5f, x + ca * (r + 10),
                   y + sa * (r + 10), col::yellow);
  }
  c.fillCircle(x, y, r, col::yellow);
}

static void moon(M5Canvas& c, int x, int y, int r) {
  c.fillCircle(x, y, r, col::moon);
  c.fillCircle(x + r / 2, y - r / 3, r - 1, col::black);
}

static void cloud(M5Canvas& c, int x, int y, uint16_t color) {
  c.fillCircle(x - 9, y + 2, 8, color);
  c.fillCircle(x + 1, y - 4, 11, color);
  c.fillCircle(x + 11, y + 3, 7, color);
  c.fillRoundRect(x - 17, y + 2, 36, 9, 4, color);
}

static void falling(M5Canvas& c, int x, int y, uint32_t now, bool snow) {
  for (int i = 0; i < 3; i++) {
    float p = fmodf(now / (snow ? 1400.0f : 600.0f) + i * 0.37f, 1);
    int px = x + (i - 1) * 10, py = y + p * 13;
    if (snow) {
      c.fillCircle(px + sinf(p * 6.28f) * 2, py, 2, mix(col::white, col::black, p * p));
    } else {
      uint16_t color = mix(col::blue, col::black, p * p);
      c.drawLine(px, py, px - 2, py + 5, color);
      c.drawLine(px + 1, py, px - 1, py + 5, color);
    }
  }
}

void weather(M5Canvas& c, int x, int y, int wmo, bool day, uint32_t now) {
  if (wmo == 0) {
    if (day) sun(c, x, y, 12, now);
    else moon(c, x, y, 14);
  } else if (wmo <= 2) {
    if (day) sun(c, x + 8, y - 8, 9, now);
    else moon(c, x + 9, y - 9, 10);
    cloud(c, x - 3, y + 6, col::cloud);
  } else if (wmo == 3) {
    cloud(c, x + 7, y - 7, col::cloudDark);
    cloud(c, x - 4, y + 4, col::cloud);
  } else if (wmo <= 48) {  // neblina
    for (int i = 0; i < 3; i++) {
      int shift = sinf(now / 900.0f + i * 2) * 4;
      c.fillRoundRect(x - 18 + shift + (i == 1 ? 5 : 0), y - 11 + i * 10, 32, 5, 2,
                      i == 1 ? col::cloud : col::grey);
    }
  } else if (wmo >= 95) {  // tempestade
    cloud(c, x, y - 8, col::cloudDark);
    bool flash = (now % 1800) < 220;
    uint16_t bolt = flash ? col::white : col::yellow;
    c.fillTriangle(x + 4, y + 3, x - 6, y + 15, x + 1, y + 14, bolt);
    c.fillTriangle(x + 1, y + 12, x + 8, y + 11, x - 3, y + 25, bolt);
  } else {
    bool snow = (wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86;
    cloud(c, x, y - 8, snow ? col::cloud : rgb(185, 195, 212));
    falling(c, x, y + 7, now, snow);
  }
}

}  // namespace icons
