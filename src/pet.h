#pragma once
#include <Preferences.h>

#include "face.h"

// "Vida" do bichinho: humor que sobe com atencao e cai com o tempo, e sono
// quando fica muito tempo sem ninguem por perto. O humor fica salvo mesmo desligando.
namespace pet {
void begin(Preferences& prefs);
// dozedOff = ficou tanto tempo sem atividade que pegou no sono
void update(uint32_t now, bool dozedOff, bool faceDown);

int mood();  // 0..100
void addMood(int delta);
void save();  // grava o humor ja, sem esperar a gravacao periodica
bool asleep();
void forceSleep(int v);  // teste: 1 dorme, 0 acorda, -1 automatico
// Expressao de fundo conforme humor e sono
Expr baseExpr();
}  // namespace pet
