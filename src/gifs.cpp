#include "gifs.h"

#include <AnimatedGIF.h>
#include <new>

#include "util.h"

// assets/gifs.pack: quantidade, depois (posicao, tamanho) de cada GIF, depois os GIFs
extern const uint8_t pack_start[] asm("_binary_assets_gifs_pack_start");

namespace gifs {

static AnimatedGIF* decoder = nullptr;  // criado so enquanto um GIF toca: parado, ocuparia 24 KB a toa
static M5Canvas frame;  // quadro atual; os GIFs so redesenham o que mudou
static bool active_ = false;
static uint32_t nextFrame = 0;
static int last = -1;

static uint32_t word(size_t at) {
  uint32_t v;
  memcpy(&v, pack_start + at, 4);
  return v;
}

// Chamado pelo decodificador a cada linha de um quadro
static void drawLine(GIFDRAW* d) {
  int y = d->iY + d->y;
  if (y < 0 || y >= SIZE) return;
  uint16_t* out = (uint16_t*)frame.getBuffer() + y * SIZE;
  for (int x = 0; x < d->iWidth; x++) {
    int px = d->iX + x;
    uint8_t idx = d->pPixels[x];
    if (px < 0 || px >= SIZE || (d->ucHasTransparency && idx == d->ucTransparent)) continue;
    out[px] = d->pPalette[idx];
  }
}

void begin() {
  frame.setColorDepth(16);
}

int count() { return word(0); }

void stop() {
  if (decoder) {
    if (active_) decoder->close();
    delete decoder;
    decoder = nullptr;
  }
  active_ = false;
  frame.deleteSprite();  // devolve os 20 KB do quadro: a memoria e disputada com o Bluetooth e o Wi-Fi
}

void start(uint32_t now) {
  stop();
  int n = count();
  if (n == 0 || !frame.createSprite(SIZE, SIZE)) return;  // sem memoria agora: fica sem GIF
  decoder = new (std::nothrow) AnimatedGIF();
  if (!decoder) return stop();
  decoder->begin(BIG_ENDIAN_PIXELS);  // mesma ordem de bytes do desenho na memoria
  int pick = random(n);
  if (n > 1 && pick == last) pick = (pick + 1) % n;  // nao repete o anterior
  last = pick;
  frame.fillSprite(col::black);
  if (!decoder->open((uint8_t*)pack_start + word(4 + pick * 8), word(8 + pick * 8), drawLine)) return;
  active_ = true;
  nextFrame = now;
}

bool active() { return active_; }

void update(uint32_t now) {
  if (!active_ || !due(now, nextFrame)) return;
  int wait = 0;
  if (decoder->playFrame(false, &wait) <= 0) decoder->reset();  // acabou: volta ao comeco
  nextFrame = now + max(wait, 20);
}

void draw(M5Canvas& c, int x, int y) {
  if (active_) frame.pushSprite(&c, x, y);
}

}  // namespace gifs
