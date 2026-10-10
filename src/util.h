#pragma once
#include <Arduino.h>

// Comparacao que sobrevive ao estouro do millis() (~49 dias ligado)
inline bool due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

constexpr uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline uint16_t mix(uint16_t a, uint16_t b, float t) {
  t = constrain(t, 0.0f, 1.0f);
  int r = (a >> 11) + ((int)(b >> 11) - (int)(a >> 11)) * t;
  int g = ((a >> 5) & 63) + ((int)((b >> 5) & 63) - (int)((a >> 5) & 63)) * t;
  int bl = (a & 31) + ((int)(b & 31) - (int)(a & 31)) * t;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

namespace col {
constexpr uint16_t black = 0, white = 0xFFFF;
constexpr uint16_t grey = rgb(160, 165, 175), dim = rgb(50, 55, 65);
constexpr uint16_t yellow = rgb(255, 205, 50), orange = rgb(255, 165, 80), red = rgb(255, 95, 80);
constexpr uint16_t blue = rgb(80, 170, 255), ice = rgb(190, 225, 255), pink = rgb(255, 120, 160);
constexpr uint16_t green = rgb(90, 220, 120), spotify = rgb(30, 215, 96);
constexpr uint16_t cloud = rgb(225, 230, 240), cloudDark = rgb(125, 135, 155);
constexpr uint16_t moon = rgb(255, 235, 150);
}  // namespace col
