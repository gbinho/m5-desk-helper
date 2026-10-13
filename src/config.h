#pragma once

// Ajustes pessoais do bichinho

#define OWNER "Gabriel"

// Wi-Fi do proprio aparelho, para buscar o tempo quando o PC esta desligado.
// Ele nao cabe na memoria junto com o Bluetooth do celular, entao os dois se
// revezam: a cada WIFI_EVERY_MIN o Bluetooth sai por uns 15 s, o Wi-Fi busca o
// tempo e desliga, e o Bluetooth volta (o iPhone reconecta sozinho). Com 0 o
// aparelho nunca usa o Wi-Fi.
#define USE_WIFI 1
#define WIFI_EVERY_MIN 30  // sem o PC, de quanto em quanto tempo ele busca o tempo sozinho

// Lembretes (minutos)
#define WATER_EVERY_MIN 60
#define STRETCH_EVERY_MIN 100

// Intervalo entre piadas / frases soltas (minutos, sorteado entre os dois)
#define CHAT_MIN_MIN 15
#define CHAT_MAX_MIN 28

// So fala e lembra coisas nesse horario
#define DAY_FROM_HOUR 8
#define DAY_TO_HOUR 23

// Modo noite: nesse horario, se ninguem estiver usando o PC, ele fica mudo (nem
// bipe de mensagem, nem repetir assobio) e com a tela e o LED bem fracos. As
// mensagens continuam aparecendo, so que em silencio. Mexeu no PC, volta ao normal.
#define NIGHT_FROM_MIN (22 * 60 + 30)  // 22h30
#define NIGHT_TO_MIN (8 * 60)          // 8h

// Sem musica, movimento nem barulho por esse tempo, a tela escurece devagar
// (esquenta menos). Qualquer mexidinha, barulho alto ou botao acende de novo.
#define REST_AFTER_MIN 4
#define REST_BRIGHTNESS 50  // 0-255; bem escuro, mas ainda da para ver o rosto

// Depois de escurecer, se continuar sem atividade por mais esse tempo, ele dorme
// de vez (para de falar e de trocar de tela). Nao depende da hora do dia.
#define DOZE_AFTER_SEC 45
