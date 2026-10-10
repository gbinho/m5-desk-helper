#pragma once
#include <Arduino.h>

// Tudo o que o bichinho fala
namespace lines {
struct Joke {
  const char* setup;
  const char* punch;
};
// Avisos em hora marcada
struct Timed {
  uint8_t hour, minute;
  const char* text;
};

extern const Timed TIMED[];
extern const size_t TIMED_COUNT;

const char* greeting(int hour);
const Joke& joke();
const char* name();      // frase solta chamando o dono
const char* petted();
const char* shaken();
const char* startled();
const char* woken();
const char* water();
const char* stretch();
}  // namespace lines
