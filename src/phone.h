#pragma once
#include <Arduino.h>

// Ligacao Bluetooth com o iPhone do dono. Depois de pareado uma vez, o celular
// se reconecta sozinho sempre que chega perto. Com isso o bichinho sabe se o
// dono esta por perto (presenca) e recebe as notificacoes do telefone.
namespace phone {
struct Note {
  uint32_t uid;      // numero que o iPhone da a essa notificacao
  uint8_t category;  // categoria da Apple: 1 = ligacao, 2 = chamada perdida, 4 = social (mensagens)...
  String app;        // identificador do aplicativo, ex.: net.whatsapp.WhatsApp
  String title;      // quem mandou
  String text;
};

void begin();
// Desliga o Bluetooth por um instante (para o Wi-Fi poder usar a memoria) e religa.
// O iPhone reconecta sozinho em poucos segundos.
void pause();
void resume();
bool connected();
bool justPaired();        // true uma vez, logo depois de um pareamento novo dar certo
int rssi();               // forca do sinal em dBm (perto de 0 = colado); 0 se desconectado
bool takeNote(Note& out); // proxima notificacao nova, se houver
// Proxima notificacao que sumiu do iPhone (voce leu ou dispensou la), se houver
bool takeRemoved(uint32_t& uid);
// Proxima mensagem que ja estava sem ler no iPhone quando ele conectou, se houver
bool takeExisting(uint32_t& uid);
void forget();            // esquece o celular pareado (para parear de novo)
void debugPrint();
}  // namespace phone
