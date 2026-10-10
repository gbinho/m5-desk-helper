#pragma once
#include "face.h"

// Falas do bichinho: uma fila de "paginas" de texto mostradas abaixo do rosto
namespace speech {
enum Icon : uint8_t { NoIcon, Water, Chat, News };

struct Page {
  String text;
  Expr expr;
  int8_t sfx;  // sound::Sfx ou -1
  Icon icon;
  uint32_t ms;
  String title;  // quem mandou, nas notificacoes de mensagem
};

// ms = 0 calcula o tempo na tela pelo tamanho do texto
void say(const String& text, Expr expr = Expr::Talk, int sfx = -1, Icon icon = NoIcon,
         const String& title = "", uint32_t ms = 0);
uint32_t pageStart();  // instante em que a pagina atual entrou na tela
// Nao comeca a falar antes desse instante (deixa uma reacao terminar)
void holdUntil(uint32_t t);
// Devolve true no quadro em que uma pagina nova comeca
bool update(uint32_t now);
bool showing();  // tem pagina na tela
bool pending();  // tem algo na tela ou na fila
const Page& page();
void skip();
}  // namespace speech
