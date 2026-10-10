#pragma once
#include <M5Unified.h>

// Icones desenhados no codigo
namespace icons {
// Tempo (codigo WMO), numa caixa de ~50 px centrada em (x, y)
void weather(M5Canvas& c, int x, int y, int wmo, bool day, uint32_t now);
void drop(M5Canvas& c, float x, float y, float r, uint16_t color);
void arrow(M5Canvas& c, int x, int y, bool up, uint16_t color);
void note(M5Canvas& c, float x, float y, float size, uint16_t color);
void wifiOff(M5Canvas& c, int x, int y);
// Azul no frio, branco no ameno, laranja e vermelho no calor
uint16_t tempColor(float celsius);
}  // namespace icons
