#include "pet.h"

#include "config.h"
#include "util.h"

namespace pet {

static const uint32_t DECAY_EVERY_MS = 15 * 60 * 1000, SAVE_EVERY_MS = 15 * 60 * 1000;
// O tempo sozinho so derruba o humor ate aqui: ficar sem carinho deixa ele "normal",
// nunca entediado. Abaixo disso so por maus-tratos (chacoalhar).
static const int CALM_MOOD = 60;

static Preferences* prefs_;
static int mood_ = 70, savedMood = 70, forced = -1;
static bool asleep_ = false;
static uint32_t nextDecay = DECAY_EVERY_MS, nextSave = SAVE_EVERY_MS;

void begin(Preferences& prefs) {
  prefs_ = &prefs;
  mood_ = savedMood = constrain((int)prefs.getUChar("mood", 70), 0, 100);
}

void update(uint32_t now, bool dozedOff, bool faceDown) {
  if (due(now, nextDecay)) {
    nextDecay = now + DECAY_EVERY_MS;
    if (!asleep_ && mood_ > CALM_MOOD) addMood(-1);
    if (!asleep_ && mood_ < CALM_MOOD) addMood(2);  // e o mau humor passa sozinho, aos poucos
  }
  if (due(now, nextSave)) {
    nextSave = now + SAVE_EVERY_MS;
    save();
  }
  asleep_ = forced >= 0 ? forced : (faceDown || dozedOff);
}

void save() {
  if (mood_ != savedMood) prefs_->putUChar("mood", savedMood = mood_);
}

int mood() { return mood_; }
void addMood(int delta) { mood_ = constrain(mood_ + delta, 0, 100); }
bool asleep() { return asleep_; }
void forceSleep(int v) { forced = v; }

Expr baseExpr() {
  if (asleep_) return Expr::Asleep;
  if (mood_ < 15) return Expr::Sad;
  if (mood_ < 30) return Expr::Bored;
  return Expr::Neutral;
}

}  // namespace pet
