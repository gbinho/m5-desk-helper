#pragma once
#include <M5Unified.h>

// Telas mostradas abaixo do rosto pequeno
enum Scene : uint8_t { FACE, CLOCK, TEMP, FORECAST, PC, MUSIC, TEMPCHART, RAINCHART, SPEECH, SETUP, NEWS, NOISE };  // NEWS so existe no menu: abre o noticiario

namespace scenes {
const int DOCK_H = 26;  // altura da faixa "tocando agora"

// Quanto da faixa da musica esta na tela (0..1): as telas se apertam para caber
void setDock(float amount);
void drawDock(M5Canvas& c, float amount, uint32_t now);
// Fone de ouvido em volta do rosto (x, y = centro dos olhos; scale = o mesmo do rosto)
void drawHeadset(M5Canvas& c, int x, int y, float scale);
// Para onde a cabeca foi neste quadro (danca, balanco, inclinacao): o fone vai junto
void setHeadPose(float dx, float dy, float lean);
// Algumas telas poem o rosto num lugar proprio (na do PC ele senta ao computador).
// Devolve false quando a tela usa o lugar padrao, no alto ao centro.
bool faceSpot(Scene s, float& x, float& y, float& scale);
// Tempo na tela para uma mensagem que precisa rolar (duas passadas completas);
// 0 se ela cabe inteira no balao
uint32_t chatDuration(M5Canvas& c, const char* body);
// Ja tem dados para mostrar essa tela?
bool available(Scene s, uint32_t now);
// y = deslocamento vertical (a tela entra deslizando por baixo)
void draw(M5Canvas& c, Scene s, int y, uint32_t now);
}  // namespace scenes
