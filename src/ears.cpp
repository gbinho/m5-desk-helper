#include "ears.h"

#include <M5Unified.h>

namespace ears {

static const size_t BLOCK = 256;  // 16 ms a 16 kHz
static const float RATE = 16000;
static int16_t buf[2][BLOCK];
static uint8_t idx = 0;
static float level_ = 0, quiet_ = 200, musicScore = 0;
static bool loud_ = false;
static uint32_t lastBlock = 0;

// medidas do ultimo bloco
static float freq_ = 0;      // tom dominante, Hz (0 = som sem tom definido)
static bool pure_ = false;   // o som e um tom limpo, como um assobio
static float lowRatio_ = 0;  // quanto do som e grave/abafado: sopro e quase todo assim
static float voice_ = 0;
static float timbre_ = 0;  // -1 som grave ... +1 som agudo

// sopro
static bool blowing_ = false, blowEvent = false, blowFired = false;
static uint32_t blowSince = 0, lastBlowBlock = 0;

// assobio: notas da melodia em andamento
static const int MAX_NOTES = 16;
static sound::Note notes[MAX_NOTES], phrase[MAX_NOTES];
static int noteCount = 0, phraseCount = 0;
static bool whistling_ = false;
static float noteFreq = 0;
static uint32_t noteStart = 0, lastTone = 0, toneSince = 0;

// barulho ao longo do tempo
static uint8_t history[NOISE_POINTS];
static int historyCount = 0;
static float minuteSum = 0;
static uint32_t minuteBlocks = 0, minuteStart = 0;
static float noiseSmooth = 0;

void begin() { M5.Mic.begin(); }

static int toNoise(float level) {
  // escala de decibeis: cada vez que o som dobra, sobe a mesma quantidade
  float db = 20 * log10f(max(level, 1.0f));
  return constrain((int)((db - 30) / 45 * 100), 0, 100);
}

static void endNote(uint32_t now) {
  if (noteFreq <= 0) return;
  uint32_t ms = now - noteStart;
  if (ms >= 90 && noteCount < MAX_NOTES) notes[noteCount++] = {(uint16_t)noteFreq, (uint16_t)min(ms, 1500u)};
  noteFreq = 0;
}

void update(uint32_t now) {
  if (!M5.Mic.isEnabled()) return;
  if (!M5.Mic.record(buf[idx], BLOCK, 16000)) return;
  idx ^= 1;  // o outro bloco ja terminou de gravar

  const int16_t* b = buf[idx];
  int32_t mean = 0;
  for (size_t i = 0; i < BLOCK; i++) mean += b[i];
  mean /= (int32_t)BLOCK;

  // Uma passada so pelo bloco mede tres coisas:
  //  - volume (media do desvio)
  //  - parte grave do som (media movel de 16 amostras deixa passar so abaixo de ~400 Hz)
  //  - tom: tempo entre as passagens por zero; num tom limpo esse tempo quase nao varia
  uint32_t sum = 0, lowSum = 0;
  int32_t window = 0;
  int crossings = 0;
  float firstT = 0, lastT = -1, minGap = 1e9f, maxGap = 0;
  int prev = b[0] - mean;
  for (size_t i = 0; i < BLOCK; i++) {
    int v = b[i] - mean;
    sum += abs(v);
    window += v;
    if (i >= 16) window -= b[i - 16] - mean;
    lowSum += abs(window) >> 4;
    if (i > 0 && prev < 0 && v >= 0) {
      float t = (i - 1) + (float)(-prev) / (v - prev);
      if (lastT >= 0) {
        float gap = t - lastT;
        minGap = min(minGap, gap), maxGap = max(maxGap, gap);
      } else {
        firstT = t;
      }
      lastT = t;
      crossings++;
    }
    prev = v;
  }
  level_ = (float)sum / BLOCK;
  lowRatio_ = sum ? (float)lowSum / sum : 0;
  freq_ = 0;
  pure_ = false;
  if (crossings >= 6) {
    float period = (lastT - firstT) / (crossings - 1);
    freq_ = RATE / period;
    pure_ = (maxGap - minGap) < period * 0.3f;
  }

  float dt = (now - lastBlock) / 1000.0f;
  lastBlock = now;
  if (sound::recent(now) || dt > 0.5f) {  // nao escuta o proprio bipe
    voice_ = 0, timbre_ = 0;
    blowing_ = whistling_ = false;
    noteFreq = 0, noteCount = 0;
    return;
  }

  // Fundo: desce rapido, sobe bem devagar
  quiet_ += (level_ - quiet_) * (level_ < quiet_ ? 0.05f : 0.0005f);
  if (quiet_ < 20) quiet_ = 20;

  // Sopro: bem forte e quase todo grave, por um tempinho. Na calibracao o sopro
  // passou de 1400 e a fala nao chegou a 550, entao o corte fica entre os dois.
  bool windy = lowRatio_ > 0.7f && level_ > quiet_ * 10 + 800;
  if (windy) {
    if (now - lastBlowBlock > 250) blowSince = now, blowFired = false;
    lastBlowBlock = now;
    if (now - blowSince > 150) blowing_ = true;
    if (now - blowSince > 800 && !blowFired) blowEvent = blowFired = true;
  } else if (now - lastBlowBlock > 250) {
    blowing_ = false;
  }

  // Assobio: tom limpo entre 600 e 4000 Hz, bem acima do fundo
  bool tone = pure_ && freq_ > 600 && freq_ < 4000 && level_ > quiet_ * 3 + 80 && !windy;
  if (tone) {
    if (now - lastTone > 120) toneSince = now;
    lastTone = now;
    if (now - toneSince > 60) {  // confirmou: nao foi um estalo com tom por acaso
      whistling_ = true;
      // nota nova quando o tom muda mais que meio tom
      if (noteFreq <= 0 || fabsf(freq_ - noteFreq) > noteFreq * 0.045f) {
        endNote(now);
        noteFreq = freq_;
        noteStart = now;
      } else {
        noteFreq += (freq_ - noteFreq) * 0.3f;
      }
    }
  } else if (now - lastTone > 110) {
    endNote(lastTone);
    whistling_ = false;
    // silencio depois da melodia: fecha a frase
    if (noteCount > 0 && now - lastTone > 700) {
      memcpy(phrase, notes, sizeof(sound::Note) * noteCount);
      phraseCount = noteCount;
      noteCount = 0;
    }
  }

  // Barulho forte e repentino (palmas, batida). Espera um instante antes de
  // contar: se virar sopro ou assobio, nao era susto.
  static uint32_t spikeAt = 0;
  static bool spike = false;
  if (level_ > quiet_ * 8 + 600 && !spike && !blowing_ && !whistling_) spike = true, spikeAt = now;
  if (spike && now - spikeAt > 220) {
    spike = false;
    if (!blowing_ && !whistling_ && !windy) loud_ = true;
  }

  if (level_ > quiet_ * 2.2f + 60) musicScore += dt;
  else musicScore -= dt * 1.5f;
  musicScore = constrain(musicScore, 0.0f, 6.0f);

  // Boca: abre depressa com o som e fecha um pouco mais devagar
  // (a fala chega fraca nesse microfone: com 350 acima do corte a boca ja abre toda)
  float over = (level_ - (quiet_ * 2 + 40)) / 350.0f;
  float target = constrain(over, 0.0f, 1.0f);
  voice_ += (target - voice_) * (target > voice_ ? 0.6f : 0.25f);
  // Timbre do som: grave e abafado ("o", "u", grave da musica) ou agudo e chiado ("i", "s", palmas).
  // So atualiza com som de verdade, senao o chiado do fundo ficaria mandando na boca.
  if (target > 0.05f) timbre_ += (constrain((0.45f - lowRatio_) / 0.22f, -1.0f, 1.0f) - timbre_) * 0.45f;
  else timbre_ *= 0.8f;

  // Historico de barulho: media de cada minuto
  noiseSmooth += (level_ - noiseSmooth) * 0.02f;
  minuteSum += level_;
  minuteBlocks++;
  if (now - minuteStart >= 60000) {
    uint8_t value = toNoise(minuteSum / max(minuteBlocks, 1u));
    if (historyCount < NOISE_POINTS) {
      history[historyCount++] = value;
    } else {
      memmove(history, history + 1, NOISE_POINTS - 1);
      history[NOISE_POINTS - 1] = value;
    }
    minuteSum = 0, minuteBlocks = 0, minuteStart = now;
  }
}

static bool take(bool& flag) {
  bool v = flag;
  flag = false;
  return v;
}

bool takeLoud() { return take(loud_); }
bool music() { return musicScore > 3.5f; }
float level() { return level_; }
float quiet() { return quiet_; }
float voice() { return voice_; }
float timbre() { return timbre_; }
bool blowing() { return blowing_; }
bool takeBlow() { return take(blowEvent); }
bool whistling() { return whistling_; }

int takePhrase(sound::Note* out, int max) {
  int n = min(phraseCount, max);
  memcpy(out, phrase, sizeof(sound::Note) * n);
  phraseCount = 0;
  return n;
}

int noiseNow() { return toNoise(noiseSmooth); }
int noiseHistory(const uint8_t*& points) {
  points = history;
  return historyCount;
}

void debugPrint() {
  Serial.printf("[mic] nivel=%.0f fundo=%.0f tom=%.0fHz limpo=%d grave=%.2f voz=%.2f sopro=%d assobio=%d barulho=%d\n",
                level_, quiet_, freq_, (int)pure_, lowRatio_, voice_, (int)blowing_, (int)whistling_, noiseNow());
}

}  // namespace ears
