#pragma once
#include <Arduino.h>

#include "sound.h"

// "Ouvido" do bichinho. O microfone nao entende fala, mas mede bem volume e
// tom, e disso saem: susto com barulho, danca com som continuo, boca que
// acompanha a voz, sopro, assobio (que ele repete) e o historico de barulho.
namespace ears {
void begin();
void update(uint32_t now);

bool takeLoud();  // estalo/palmas/barulho forte (consome o evento)
bool music();     // som continuo no ambiente ha alguns segundos
float level();    // volume atual
float quiet();    // volume de fundo estimado

float voice();    // 0..1: quanto som tem agora acima do fundo (para a boca acompanhar)
float timbre();   // -1 som grave/abafado ... +1 som agudo/chiado (muda o formato da boca)

bool blowing();   // estao soprando nele agora
bool takeBlow();  // sopro forte e comprido (consome o evento)

bool whistling();  // ouvindo um assobio agora
// Melodia assobiada, quando a pessoa termina: copia as notas e devolve quantas (0 = nada novo)
int takePhrase(sound::Note* out, int max);

// Barulho do ambiente, de 0 (silencio) a 100 (muito alto)
int noiseNow();
const int NOISE_POINTS = 106;  // um ponto por minuto
// Historico por minuto, do mais antigo ao mais novo; devolve quantos pontos ja existem
int noiseHistory(const uint8_t*& points);

void debugPrint();
}  // namespace ears
