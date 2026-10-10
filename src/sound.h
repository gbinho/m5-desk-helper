#pragma once
#include <Arduino.h>

// Bipes no buzzer do M5StickC Plus
namespace sound {
enum Sfx : uint8_t { Chirp, Happy, Love, Startle, Dizzy, Alert, Laugh, Yawn, Sad, Wake, Hour, Message, SFX_COUNT };

struct Note {
  uint16_t hz, ms;  // hz 0 = pausa
};

void begin(bool muted);
// Toca uma sequencia de notas montada na hora (copia as notas; ate 16)
void playNotes(const Note* notes, int count);
void update(uint32_t now);
void play(Sfx s);
void setMuted(bool m);
bool muted();
// Silencio temporario (chamada em andamento), sem mexer na escolha de mudo do dono
void setQuiet(bool q);
// Tocando agora ou acabou de tocar (o microfone ignora esse periodo)
bool recent(uint32_t now);
}  // namespace sound
