#pragma once
#include <M5Unified.h>

// GIFs de danca que tocam quando uma musica comeca. Vem embutidos no firmware,
// preparados por tools/make_gifs.py a partir da pasta gifs/.
namespace gifs {
const int SIZE = 100;  // lado em pixels

void begin();
int count();
void start(uint32_t now);  // sorteia um e comeca a tocar
void stop();
bool active();
void update(uint32_t now);  // avanca os quadros no tempo certo
void draw(M5Canvas& c, int x, int y);
}  // namespace gifs
