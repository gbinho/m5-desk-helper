#include "speech.h"

#include "util.h"

namespace speech {

static const int QUEUE = 4;
static Page queue[QUEUE], current;
static int count = 0;
static bool showing_ = false, held = false;
static uint32_t pageEnd = 0, holdTime = 0, pageStart_ = 0;

void say(const String& text, Expr expr, int sfx, Icon icon, const String& title, uint32_t wanted) {
  if (count >= QUEUE) return;
  uint32_t ms = max(2600u, 1500u + 60u * text.length());
  // mensagem de alguem merece mais calma para ler: 4 s e mais 1 s a cada 10 letras
  if (icon == Chat) ms = min(4000u + 100u * text.length(), 24000u);
  if (wanted) ms = wanted;
  queue[count++] = {text, expr, (int8_t)sfx, icon, ms, title};
}

void holdUntil(uint32_t t) {
  held = true;
  holdTime = t;
}

bool update(uint32_t now) {
  if (held && due(now, holdTime)) held = false;
  if (showing_ && due(now, pageEnd)) showing_ = false;
  if (showing_ || held || count == 0) return false;

  current = queue[0];
  for (int i = 1; i < count; i++) queue[i - 1] = queue[i];
  count--;
  showing_ = true;
  pageStart_ = now;
  pageEnd = now + current.ms;
  return true;
}

bool showing() { return showing_; }
uint32_t pageStart() { return pageStart_; }
bool pending() { return showing_ || count > 0; }
const Page& page() { return current; }
void skip() { showing_ = false; }

}  // namespace speech
