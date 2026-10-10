#pragma once
#include <Arduino.h>

// Acelerometro/giroscopio: inclinacao, chacoalhao, pegar da mesa, virar
namespace motion {
void update(uint32_t now);

// Para onde a gravidade puxa na tela, -1..1 (rotation = rotacao atual da tela)
float tiltX(uint8_t rotation);
float tiltY(uint8_t rotation);

// Forca que uma gelatina presa na tela sentiria, em g: a gravidade puxando
// para o lado mais baixo mais os trancos de quando o aparelho e mexido
float forceX(uint8_t rotation);
float forceY(uint8_t rotation);
// Giro em torno do centro da tela, graus/s
float spin();

bool takeShake();
bool takePickup();
bool takeTap();
bool takeActivity();  // qualquer mexidinha, por menor que seja
bool faceDown();
// A tela esta de ponta-cabeca ha um tempo: hora de girar
bool wantsFlip(uint8_t rotation, uint32_t now);

void debugPrint();
}  // namespace motion
