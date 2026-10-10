#include "sound.h"

#include "util.h"

namespace sound {

static const uint8_t PIN = 2, CHANNEL = 4;
static const uint16_t DUTY = 70;  // de 1023; baixo de proposito, o buzzer e estridente

struct Melody {
  const Note* notes;
  uint8_t count;
};

static const Note CHIRP[] = {{1760, 40}, {2349, 60}};
static const Note HAPPY[] = {{1319, 70}, {1568, 70}, {2093, 110}};
static const Note LOVE[] = {{1568, 80}, {0, 30}, {1568, 60}, {2093, 160}};
static const Note STARTLE[] = {{2637, 50}, {3136, 90}};
static const Note DIZZY[] = {{988, 70}, {880, 70}, {784, 70}, {698, 70}, {784, 70}, {880, 70}, {988, 90}};
static const Note ALERT[] = {{1760, 90}, {0, 60}, {1760, 90}, {0, 60}, {2093, 160}};
static const Note LAUGH[] = {{1568, 55}, {0, 40}, {1760, 55}, {0, 40}, {1568, 55},
                             {0, 40},    {1760, 55}, {0, 40}, {2093, 90}};
static const Note YAWN[] = {{880, 120}, {784, 120}, {659, 200}};
static const Note SAD[] = {{659, 140}, {523, 220}};
static const Note WAKE[] = {{1047, 70}, {1319, 70}, {1568, 100}};
static const Note HOUR[] = {{1568, 120}, {0, 60}, {2093, 200}};
// Mensagem de alguem: de proposito bem diferente dos outros (que sao bipes agudos,
// curtos e subindo). Este e um "ding-dong" grave e lento, tocado duas vezes.
static const Note MESSAGE[] = {{988, 150}, {0, 30}, {740, 260}, {0, 220}, {988, 150}, {0, 30}, {740, 260}};

#define MELODY(x) {x, sizeof(x) / sizeof(x[0])}
static const Melody MELODIES[SFX_COUNT] = {
    MELODY(CHIRP), MELODY(HAPPY), MELODY(LOVE), MELODY(STARTLE), MELODY(DIZZY), MELODY(ALERT),
    MELODY(LAUGH), MELODY(YAWN),  MELODY(SAD),  MELODY(WAKE),    MELODY(HOUR), MELODY(MESSAGE)};

static bool muted_ = false, quiet_ = false;
static const Melody* cur = nullptr;
static uint8_t idx = 0;
static uint32_t noteEnd = 0, lastActive = 0;
static bool everPlayed = false;

static void startNote() {
  const Note& n = cur->notes[idx];
  if (n.hz) {
    ledcWriteTone(CHANNEL, n.hz);
    ledcWrite(CHANNEL, DUTY);
  } else {
    ledcWrite(CHANNEL, 0);
  }
  noteEnd = millis() + n.ms;
}

void begin(bool muted) {
  muted_ = muted;
  ledcSetup(CHANNEL, 2000, 10);
  ledcAttachPin(PIN, CHANNEL);
  ledcWrite(CHANNEL, 0);
}

void play(Sfx s) {
  if (muted_ || quiet_ || s >= SFX_COUNT) return;
  cur = &MELODIES[s];
  idx = 0;
  everPlayed = true;
  startNote();
}

void playNotes(const Note* notes, int count) {
  static Note custom[16];
  static Melody melody = {custom, 0};
  if (muted_ || quiet_ || count <= 0) return;
  melody.count = min(count, 16);
  memcpy(custom, notes, sizeof(Note) * melody.count);
  cur = &melody;
  idx = 0;
  everPlayed = true;
  startNote();
}

void update(uint32_t now) {
  if (!cur) return;
  lastActive = now;
  if (!due(now, noteEnd)) return;
  if (++idx < cur->count) {
    startNote();
  } else {
    ledcWrite(CHANNEL, 0);
    cur = nullptr;
  }
}

void setMuted(bool m) {
  muted_ = m;
  if (m && cur) {
    ledcWrite(CHANNEL, 0);
    cur = nullptr;
  }
}

bool muted() { return muted_; }
void setQuiet(bool q) { quiet_ = q; }
bool recent(uint32_t now) { return cur || (everPlayed && now - lastActive < 500); }

}  // namespace sound
