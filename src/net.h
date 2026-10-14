#pragma once
#include <Arduino.h>
#include <time.h>

#define AP_NAME "Bichinho-Setup"  // rede criada pelo aparelho para configurar o Wi-Fi
#define HOSTNAME "m5boladao"      // nome do aparelho na rede

// Escrito pela tarefa de rede, lido pelo desenho da tela
struct NetState {
  volatile bool portal = false;      // esperando o usuario configurar o Wi-Fi
  volatile bool online = false;
  // Escrito pelo programa principal: false = desligar o radio (o PC ja manda tudo pelo cabo)
  volatile bool wifiWanted = false;
  volatile uint32_t fetches = 0;     // quantas vezes ja buscou o tempo com sucesso pelo Wi-Fi
  volatile bool setupDone = false;   // a configuracao do Wi-Fi (se foi pedida) ja terminou
  volatile bool radioOff = true;     // radio do Wi-Fi desligado de fato
  volatile bool hasWeather = false;
  volatile float tempC = 0;
  volatile int wmo = 0;              // codigo de tempo WMO
  volatile bool isDay = true;
  volatile float maxC = 0, minC = 0; // previsao de hoje
  volatile int rainPct = 0;
  volatile float rainingMm = 0;      // chuva caindo agora, em mm (0 = nao esta chovendo)
  volatile bool hasHourly = false;   // curvas de hoje, hora a hora (0h..23h)
  float hourTemp[24] = {};
  uint8_t hourRain[24] = {};         // chance de chuva, %
  float hourMm[24] = {};             // chuva estimada em cada hora, mm
  char city[40] = "";
};
extern NetState net;

// Sobe a tarefa que cuida de Wi-Fi, hora (NTP) e temperatura.
// configure = true abre a tela de configuracao do Wi-Fi mesmo com rede salva
void netBegin(bool configure);
const char* weatherText(int wmo);
// Hora local; false enquanto o relogio nao foi acertado
bool clockNow(struct tm& t);
// Acerta o relogio com a hora mandada pelo PC (hora universal + diferenca do fuso, em segundos)
void setClock(time_t utc, int offsetSec);
// Ao ligar: recupera a hora do relogio a pilha do aparelho, se ja foi acertado alguma vez
void restoreClock();
