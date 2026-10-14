#pragma once
#include <M5Unified.h>

// Conversa com o PC pelo cabo USB (pc/companion.py). Uma linha por comando:
//   pc <cpu%> <ram%> [gpu%] [gpu graus]
//   np <0 parado|1 musica|2 video|3 YouTube>\t<titulo>\t<artista>
//   clock <utc> <fuso em s>   -> acerta o relogio
//   weather <t> <wmo> <dia> <max> <min> <chuva%> <mm agora>\t<cidade>
//   hourly <24 temperaturas>;<24 chances de chuva>;<24 mm de chuva previstos>
//   type <teclas> <cliques>   -> quantas teclas e cliques desde o ultimo aviso (so a contagem)
//   game <0|1>\t<nome>        -> jogo rodando
//   fan <%> <rpm>             -> velocidade do cooler (rpm 0 = estimada)
//   user <s>                  -> ha quantos segundos ninguem mexe no PC
//   bpm <n>                   -> andamento da musica atual (0 = desconhecido)
//   img <linha> <hex>         -> uma linha da capa do disco (COVER pixels, 4 hex cada)
//   tell <expressao> <som> <texto>  -> fala pronta, montada pelo PC (rotina, eventos)
//   news <i> <fonte>\t<manchete> -> guarda uma manchete; bulletin -> mostra o plantao
//   call <0|1>                -> chamada em andamento: fica em silencio
//   msg <quem>\t<texto>       -> notificacao de mensagem (WhatsApp)
//   shot                      -> devolve a tela (usado por tools/shot.py)
// Qualquer outra linha vai para onDebugCommand().
namespace pclink {
const int COVER = 72;  // lado da capa do disco, em pixels
const int THUMB = 22;  // lado da miniatura

void update(uint32_t now);

bool alive(uint32_t now);  // o PC mandou dados ha pouco
// O dono saiu de perto: PC em contato, mas teclado e mouse parados ha um tempo
bool userAway(uint32_t now);
// Sem contato (cabo solto, PC desligado) os ultimos dados continuam valendo:
bool everSeen();                // ja recebeu algo do PC desde que ligou
uint32_t staleMs(uint32_t now); // ha quanto tempo nao chega nada
float cpu();
float ram();
bool hasGpu();
float gpu();
float gpuTemp();
float fanPct();  // velocidade do cooler, 0..100
int fanRpm();    // rotacao real, ou 0 quando a velocidade e so uma estimativa
// Teclas e cliques novos desde a ultima chamada (para as patinhas baterem junto)
int takeKeys();
int takeClicks();
bool typing(uint32_t now);  // digitando agora (alguma tecla no ultimo segundo e meio)
bool gaming();              // tem um jogo rodando no PC
const String& game();
bool playing();   // tocando agora, com o PC em contato
bool video();     // o que esta tocando e um video, nao uma musica
bool youtube();   // ...e e do YouTube
bool hasTrack();  // tem uma musica guardada (mesmo que antiga)
const String& title();
const String& artist();
bool takeTrackChange();
int bpm();  // andamento da musica atual; 0 se o PC nao descobriu
// Capa da musica atual (pixels prontos para pushImage) ou nullptr se ainda nao chegou
const uint16_t* cover();
// A mesma capa reduzida a THUMB x THUMB, para a faixa "tocando agora"
const uint16_t* thumb();

bool takeShotRequest();
void sendShot(M5Canvas& c);
}  // namespace pclink

void onDebugCommand(const String& cmd, const String& arg);
