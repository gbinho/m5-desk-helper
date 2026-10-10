#pragma once
#include <M5Unified.h>

// Fontes suavizadas com acentos (geradas por tools/make_fonts.py)
namespace text {
extern lgfx::VLWfont small, medium;  // texto
extern lgfx::VLWfont num, big;       // so numeros e : % - . , °

void begin();
// Texto centralizado em (cx, cy) com quebra de linha; usa a fonte media e cai
// para a pequena se nao couber em maxH
void drawWrapped(M5Canvas& c, const char* s, int cx, int cy, int maxW, int maxH);
// Texto alinhado a esquerda a partir de (x, top), com quebra de linha. Escolhe a
// fonte media ou a pequena conforme couber em maxH e devolve a altura ocupada.
// Com draw = false so mede, sem desenhar.
int drawLeft(M5Canvas& c, const char* s, int x, int top, int maxW, int maxH, bool draw = true);
// Quantas linhas o texto ocupa na fonte pequena
int smallLines(M5Canvas& c, const char* s, int maxW);
// Texto comprido na fonte pequena, visto por uma janela de altura viewH que
// comeca em top; offset = quantos pixels ja rolou para cima
void drawScrolled(M5Canvas& c, const char* s, int x, int top, int maxW, int viewH, int offset);
const int SMALL_LINE = 18;  // altura de linha da fonte pequena
// Uma linha centralizada; encolhe a fonte se passar de maxW
void drawFit(M5Canvas& c, const char* s, int cx, int cy, int maxW);
}  // namespace text
