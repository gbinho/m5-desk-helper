#include "text.h"

#define ASSET(name)                                                              \
  extern const uint8_t name##_start[] asm("_binary_assets_" #name "_vlw_start"); \
  extern const uint8_t name##_end[] asm("_binary_assets_" #name "_vlw_end");     \
  static lgfx::PointerWrapper name##_data;
ASSET(small)
ASSET(medium)
ASSET(num)
ASSET(big)

namespace text {

lgfx::VLWfont small, medium, num, big;

void begin() {
  small_data.set(small_start, small_end - small_start);
  small.loadFont(&small_data);
  medium_data.set(medium_start, medium_end - medium_start);
  medium.loadFont(&medium_data);
  num_data.set(num_start, num_end - num_start);
  num.loadFont(&num_data);
  big_data.set(big_start, big_end - big_start);
  big.loadFont(&big_data);
}

static const int MAX_LINES = 14;

static int wrap(M5Canvas& c, const char* s, int maxW, String* lines) {
  int n = 0;
  String line;
  while (*s && n < MAX_LINES) {
    const char* sp = strchr(s, ' ');
    size_t len = sp ? (size_t)(sp - s) : strlen(s);
    String word;
    word.concat(s, len);
    String trial = line.length() ? line + " " + word : word;
    if (line.length() && c.textWidth(trial.c_str()) > maxW) {
      lines[n++] = line;
      line = word;
    } else {
      line = trial;
    }
    s += len;
    while (*s == ' ') s++;
  }
  if (line.length() && n < MAX_LINES) lines[n++] = line;
  return n;
}

void drawWrapped(M5Canvas& c, const char* s, int cx, int cy, int maxW, int maxH) {
  String lines[MAX_LINES];
  c.setFont(&medium);
  int lh = 25;
  int n = wrap(c, s, maxW, lines);
  if (n * lh > maxH) {
    c.setFont(&small);
    lh = 18;
    n = wrap(c, s, maxW, lines);
  }
  n = min(n, 5);
  c.setTextDatum(middle_center);
  for (int i = 0; i < n; i++) c.drawString(lines[i].c_str(), cx, cy + (i * 2 - (n - 1)) * lh / 2);
}

int drawLeft(M5Canvas& c, const char* s, int x, int top, int maxW, int maxH, bool draw) {
  String lines[MAX_LINES];
  c.setFont(&medium);
  int lh = 24;
  int n = wrap(c, s, maxW, lines);
  if (n * lh > maxH) {
    c.setFont(&small);
    lh = 18;
    n = wrap(c, s, maxW, lines);
    n = min(n, max(maxH / lh, 1));  // o que nao couber fica de fora
  }
  if (draw) {
    c.setTextDatum(middle_left);
    for (int i = 0; i < n; i++) c.drawString(lines[i].c_str(), x, top + lh / 2 + i * lh);
  }
  return n * lh;
}

int smallLines(M5Canvas& c, const char* s, int maxW) {
  String lines[MAX_LINES];
  c.setFont(&small);
  return wrap(c, s, maxW, lines);
}

void drawScrolled(M5Canvas& c, const char* s, int x, int top, int maxW, int viewH, int offset) {
  String lines[MAX_LINES];
  c.setFont(&small);
  int n = wrap(c, s, maxW, lines);
  c.setClipRect(x, top, maxW, viewH);
  c.setTextDatum(middle_left);
  for (int i = 0; i < n; i++) {
    int y = top - offset + SMALL_LINE / 2 + i * SMALL_LINE;
    if (y > top - SMALL_LINE && y < top + viewH + SMALL_LINE) c.drawString(lines[i].c_str(), x, y);
  }
  c.clearClipRect();
}

void drawFit(M5Canvas& c, const char* s, int cx, int cy, int maxW) {
  c.setFont(&medium);
  if (c.textWidth(s) > maxW) c.setFont(&small);
  c.setTextDatum(middle_center);
  c.drawString(s, cx, cy);
}

}  // namespace text
