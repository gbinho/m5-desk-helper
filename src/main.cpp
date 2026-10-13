// Bichinho de mesa: um rostinho que reage a voce e alterna entre relogio,
// temperatura, previsao, PC e musica.
//   Botao A (frente)  clique : proxima tela        2 cliques: carinho
//                     segurar: menu de telas (inclinar escolhe, soltar abre)
//   Botao B (lateral) clique : brilho (degraus)    2 cliques: liga/desliga o som
//                     segurar: ajuste fino do brilho inclinando o aparelho
//   Ligar segurando o botao B: configurar o Wi-Fi de novo

#include <M5Unified.h>
#include <Preferences.h>

#include "config.h"
#include "ears.h"
#include "face.h"
#include "gifs.h"
#include "icons.h"
#include "lines.h"
#include "motion.h"
#include "net.h"
#include "pclink.h"
#include "pet.h"
#include "phone.h"
#include "scenes.h"
#include "sound.h"
#include "speech.h"
#include "text.h"
#include "util.h"

static const uint32_t FACE_MS = 8000, INFO_MS = 6000, MINUTE = 60000;
static const uint8_t BRIGHT[] = {30, 70, 120, 180, 255};  // degraus do clique no botao B
static const int BRIGHT_MIN = 12;  // abaixo disso a tela apaga de vez
static const Scene ROTA[] = {CLOCK, TEMP, TEMPCHART, FORECAST, RAINCHART, PC, MUSIC};
static const size_t ROTA_COUNT = sizeof(ROTA) / sizeof(ROTA[0]);

static M5Canvas canvas(&M5.Display);
static Preferences prefs;
static Face face;

static uint32_t now;           // instante do quadro atual
static struct tm clk;          // hora local do quadro atual
static bool haveTime = false;

static Scene scene = FACE;     // tela do rodizio
static Scene content = FACE;   // o que esta desenhado abaixo do rosto pequeno
static size_t rotaIdx = ROTA_COUNT - 1;
static uint32_t sceneEnd = FACE_MS;
static int prevX0 = 0, prevY0 = 0, prevX1 = 239, prevY1 = 134;  // retangulo desenhado no quadro anterior
static float hideFace = 0;  // 0..1: rosto fora da tela (tela de musica com GIF)
static float dock = 0;  // 0..1: quanto da faixa "tocando agora" esta na tela
static float frameDt = 0.016f;  // duracao do ultimo quadro, s
static float k = 0;            // 0 = rosto em tela cheia, 1 = rosto pequeno + informacao
static uint8_t rotation = 1;
static float userBright = 120;   // brilho escolhido (BRIGHT_MIN..255)
static bool sliding = false;     // ajustando o brilho: botao B segurado + inclinar
static bool slideTest = false;

// Menu de telas: botao A segurado + inclinar para escolher, soltar para abrir
struct MenuItem {
  Scene scene;
  const char* name;  // so para quem le o codigo; na tela aparece o icone
};
static const MenuItem MENU[] = {{FACE, "Rosto"},        {CLOCK, "Relógio"},   {TEMP, "Tempo"},
                                {TEMPCHART, "Temperatura"}, {FORECAST, "Previsão"}, {RAINCHART, "Chuva"},
                                {PC, "PC"},              {MUSIC, "Música"},     {NEWS, "Notícias"},     {NOISE, "Barulho"}};
static const int MENU_MAX = sizeof(MENU) / sizeof(MENU[0]);
static const MenuItem* menuItems[MENU_MAX];  // so as telas que tem o que mostrar agora
static int menuCount = 0;
static float menuPos = 0, menuTilt0 = 0;    // posicao (fracionaria) na lista
static bool menuOpen = false, menuTest = false;
static float slideTilt0 = 0;     // inclinacao no momento em que o botao foi segurado
static int appliedBright = -1;
static float bright = 130;        // brilho atual, suavizado
static uint32_t lastActivity = 0;  // ultima vez que houve musica, movimento, barulho ou botao
static bool resting = false;       // tela escurecida por falta de atividade

static uint32_t nextWater, nextStretch, nextChat, nextIdle = 12000;
static uint32_t lastLoud = 0, lastDance = 0, lastHotTalk = 0, cpuHighSince = 0;
static bool greeted = false, cpuHigh = false;
static int lastChimeHour = -1, lastTimed = -1;
static int battery = -1;       // % de bateria quando esta fora do cabo; -1 = ligado no USB
static int batteryTest = -2;   // comando de teste: -2 = usa a leitura de verdade
static bool batteryWarned = false;
// Manchetes mandadas pelo PC; o plantao mostra as tres primeiras
static const int NEWS_SLOTS = 5;
static String newsSource[NEWS_SLOTS], newsTitle[NEWS_SLOTS];
static int voiceTest = -1;  // comando de teste: 0..100 fixa a abertura da boca
static int timbreTest = 0;  // comando de teste: -100 (grave) .. 100 (agudo), vale junto com o "voice"
static int ledTest = -1;  // comando de teste: 0..255 fixa o brilho do LED
static bool nightQuiet = false;  // modo noite: mudo e escuro, mas ainda mostra mensagens
static int nightTest = -1;       // comando de teste: 1 forca o modo noite, 0 impede, -1 automatico
static bool inCall = false;  // o PC avisou que tem chamada rolando: nada de som nem conversa
// Notificacoes de mensagem que ainda estao sem ler no iPhone
static const int UNREAD_MAX = 16;
static uint32_t unreadUids[UNREAD_MAX];
static int unreadCount = 0;
#define unread unreadCount

static bool daytime() { return !haveTime || (clk.tm_hour >= DAY_FROM_HOUR && clk.tm_hour < DAY_TO_HOUR); }

static void show(Scene s, uint32_t ms) {
  scene = s;
  sceneEnd = now + ms;
}

static void nextScene() {
  face.blink(now);
  if (scene == FACE) {
    for (size_t i = 0; i < ROTA_COUNT; i++) {
      rotaIdx = (rotaIdx + 1) % ROTA_COUNT;
      if (scenes::available(ROTA[rotaIdx], now)) return show(ROTA[rotaIdx], INFO_MS);
    }
  }
  show(FACE, FACE_MS);
}

// Reacao em tela cheia, com som opcional
static void react(Expr e, uint32_t ms, int sfx = -1) {
  face.play(e, now, ms);
  if (sfx >= 0) sound::play((sound::Sfx)sfx);
  scene = FACE;
  if (!due(sceneEnd, now + ms + 2000)) sceneEnd = now + ms + 2000;
}

// ---- Coisas que acontecem com o bichinho ----

static void wakeUp() {
  lastActivity = now;
  react(Expr::Surprised, 1200, sound::Wake);
  speech::say(lines::woken(), Expr::Sleepy);
  speech::holdUntil(now + 1200);
}

static void onPet() {
  pet::addMood(12);
  // Dias seguidos com carinho: conta uma vez por dia e comemora a sequencia
  if (haveTime) {
    long y = clk.tm_year + 1899;  // numero do dia, contando do ano 1 (so importa que seja corrido)
    long day = y * 365 + y / 4 - y / 100 + y / 400 + clk.tm_yday;
    long last = prefs.getLong("petday", 0);
    if (day != last) {
      int streak = day == last + 1 ? prefs.getInt("petstreak", 0) + 1 : 1;
      prefs.putLong("petday", day);
      prefs.putInt("petstreak", streak);
      if (streak >= 2) {
        speech::say(String(streak) + " dias seguidos de carinho! Valeu, " OWNER "!", Expr::Love);
        speech::holdUntil(now + 2400);
      }
    }
  }
  react(Expr::Love, 2400, sound::Love);
  if (random(3) == 0) {
    speech::say(lines::petted(), Expr::Happy);
    speech::holdUntil(now + 2400);
  }
}

static void onShake() {
  pet::addMood(-3);
  react(Expr::Dizzy, 3500, sound::Dizzy);
  if (random(2) == 0) {
    speech::say(lines::shaken(), Expr::Dizzy);
    speech::holdUntil(now + 3500);
  }
}

static void onPickup() {
  if (pet::asleep()) return wakeUp();
  if (!face.busy()) react(Expr::Surprised, 1400, sound::Startle);
}

static void onLoud() {
  lastActivity = now;
  if (now - lastLoud < 6000) return;
  lastLoud = now;
  if (inCall) return;  // em chamada o barulho e a sua voz: nada de susto
  if (pet::asleep()) return wakeUp();
  react(Expr::Surprised, 1300);
  if (random(4) == 0) {
    speech::say(lines::startled(), Expr::Surprised);
    speech::holdUntil(now + 1300);
  }
}

// Notificacao de mensagem vinda do PC (WhatsApp)
// uid = numero da notificacao no iPhone (0 quando a mensagem veio pelo PC)
static void onMessage(const String& sender, const String& body, uint32_t uid = 0) {
  // a mesma mensagem pode chegar pelo PC e pelo celular: mostra uma vez so
  static String lastKey;
  static uint32_t lastAt = 0;
  String key = sender + '|' + body;
  bool repeated = key == lastKey && now - lastAt < 20000;
  // Conta como nao lida enquanto a notificacao existir no iPhone; quando voce le
  // ou dispensa la, o telefone avisa e ela sai da conta (ver handlePhone)
  if (uid && unreadCount < UNREAD_MAX) unreadUids[unreadCount++] = uid;
  if (repeated) return;
  lastKey = key, lastAt = now;
  lastActivity = now;
  // mensagem comprida rola dentro do balao; o tempo na tela cobre duas passadas
  speech::say(body, Expr::Curious, pet::asleep() ? -1 : sound::Message, speech::Chat, sender,
              scenes::chatDuration(canvas, body.c_str()));
}

// Mini noticiario: vinheta e tres manchetes, cada uma com a fonte
static void newsBulletin() {
  if (!newsTitle[0].length()) return;
  lastActivity = now;
  speech::say("Plantão do bichinho!", Expr::Happy, sound::Hour);
  for (int i = 0; i < 3 && newsTitle[i].length(); i++)
    speech::say(newsTitle[i], Expr::Talk, -1, speech::News, newsSource[i]);
}

static void tellJoke() {
  const lines::Joke& j = lines::joke();
  speech::say(j.setup, Expr::Talk, sound::Chirp);
  speech::say(j.punch, Expr::Laugh, sound::Laugh);
}

static void remindWater() { speech::say(lines::water(), Expr::Talk, sound::Alert, speech::Water); }
static void remindStretch() { speech::say(lines::stretch(), Expr::Talk, sound::Alert); }
static void greet() {
  // sem relogio ainda, nao arrisca "bom dia" ou "boa noite"
  speech::say(haveTime ? lines::greeting(clk.tm_hour) : "Oi, " OWNER "!", Expr::Happy, sound::Happy);
}

static void pcHot() {
  react(Expr::Hot, 3000);
  speech::say("Ufa! O PC tá fritando!", Expr::Hot);
  speech::holdUntil(now + 3000);
}

// Gracinha aleatoria quando nao esta acontecendo nada
static void idleAct() {
  int hour = haveTime ? clk.tm_hour : 12;
  bool weather = net.hasWeather;
  bool raining = weather && net.rainingMm > 0.05f;  // chovendo agora, nao so previsao
  struct {
    Expr e;
    int weight;
    uint32_t ms;
  } options[] = {
      {Expr::Happy, pclink::gaming() ? 6 : 3, 2000},
      {Expr::Surprised, pclink::gaming() ? 4 : 0, 1100},  // torcendo junto
      {Expr::Wink, 3, 1300},
      {Expr::Fly, 2, 6000},
      {Expr::Yawn, hour >= 21 || hour < 9 ? 4 : 1, 2600},
      {Expr::Hot, weather && net.tempC >= 29 ? 4 : 0, 4000},
      {Expr::Cold, weather && net.tempC <= 15 ? 4 : 0, 4000},
      {Expr::Sad, raining ? 2 : 0, 3500},
      {Expr::Bored, pet::mood() < 45 ? 2 : 0, 4000},
      {Expr::Love, pet::mood() > 85 ? 2 : 0, 2000},
      {Expr::Dance, pclink::playing() && !pclink::video() ? 5 : 0, 5000},
      {Expr::Chew, pclink::playing() && pclink::video() ? 5 : 0, 5000},  // pipoca vendo video
      {Expr::Ball, 2, 9000},
      {Expr::Whistle, 2, 4000},
      {Expr::Sneeze, weather && net.tempC <= 17 ? 3 : 1, 1700},
      {Expr::Curious, 1, 2500},
      {Expr::Chew, hour >= 12 && hour < 14 ? 4 : 0, 4500},
      {Expr::Rainy, raining ? 5 : 0, 7000},  // guarda-chuva so com agua caindo de verdade
      {Expr::Coffee, hour >= 6 && hour < 10 ? 5 : 0, 6000},
  };
  int total = 0;
  for (auto& o : options) total += o.weight;
  int roll = random(total);
  for (auto& o : options) {
    if ((roll -= o.weight) < 0) return face.play(o.e, now, o.ms);
  }
}

static void openMenu() {
  menuCount = 0;
  menuPos = 0;
  for (const MenuItem& item : MENU) {
    // a musica entra sempre que houver alguma guardada, tocando ou nao
    bool ok = menuTest || (item.scene == MUSIC  ? pclink::hasTrack()
                           : item.scene == NEWS ? newsTitle[0].length() > 0
                                                : scenes::available(item.scene, now));
    if (!ok) continue;
    if (item.scene == scene) menuPos = menuCount;  // comeca na tela atual
    menuItems[menuCount++] = &item;
  }
  menuTilt0 = motion::tiltX(rotation);  // a posicao em que esta agora e o "parado"
  menuOpen = true;
}

// Fora do cabo ele roda na bateria interna, que e pequena: mostra quanto resta
static void checkBattery() {
  static uint32_t next = 0;
  if (!due(now, next)) return;
  next = now + 2000;
  bool plugged = M5.Power.getVBUSVoltage() > 4000;  // milivolts chegando pelo USB
  battery = batteryTest > -2 ? batteryTest : plugged ? -1 : constrain((int)M5.Power.getBatteryLevel(), 0, 100);
  if (battery < 0 || battery > 25) batteryWarned = false;
  if (battery >= 0 && battery <= 15 && !batteryWarned) {
    batteryWarned = true;
    speech::say("Minha bateria tá acabando, " OWNER "!", Expr::Sad, sound::Alert);
  }
}

static void drawBattery() {
  const int x = 214, y = 5;
  uint16_t color = battery > 50 ? col::green : battery > 20 ? col::yellow : col::red;
  canvas.drawRoundRect(x, y, 21, 11, 2, col::white);
  canvas.fillRect(x + 21, y + 3, 2, 5, col::white);
  canvas.fillRect(x + 2, y + 2, max(17 * battery / 100, 1), 7, color);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", battery);
  canvas.setFont(&text::small);
  canvas.setTextColor(col::white);
  canvas.setTextDatum(middle_right);
  canvas.drawString(buf, x - 4, y + 6);
}

// ---- Celular: presenca e notificacoes ----

static void handlePhone() {
  static bool was = false, seenGone = false, leftHandled = true;
  static uint32_t changedAt = 0;
  bool is = phone::connected();
  if (is != was) {
    // Voltou depois de um bom tempo longe: chegou em casa
    if (is && seenGone && now - changedAt > 3 * MINUTE) {
      lastActivity = now;
      if (!inCall && !speech::pending()) speech::say("Olha quem chegou! Oi, " OWNER "!", Expr::Happy, sound::Happy);
    }
    if (!is) seenGone = true, leftHandled = false;
    was = is, changedAt = now;
  }
  // Sumiu ha um minuto e meio e ninguem mexe no PC: saiu. Vai descansar ja, sem
  // esperar os minutos de inatividade.
  if (!is && !leftHandled && now - changedAt > 90000) {
    leftHandled = true;
    bool atPc = pclink::alive(now) && !pclink::userAway(now);
    if (!atPc && !pclink::playing() && now - lastActivity < REST_AFTER_MIN * MINUTE)
      lastActivity = now - REST_AFTER_MIN * MINUTE;
  }
  if (phone::justPaired()) speech::say("Celular conectado! Agora eu sei quando você chega.", Expr::Happy, sound::Happy);

  // leu ou dispensou no celular: sai da conta
  uint32_t gone;
  while (phone::takeRemoved(gone)) {
    for (int i = 0; i < unreadCount; i++) {
      if (unreadUids[i] != gone) continue;
      unreadUids[i] = unreadUids[--unreadCount];
      break;
    }
  }
  if (!phone::connected()) unreadCount = 0;  // sem o celular nao ha como saber
  // O que ja estava na central de notificacoes do iPhone ao conectar NAO entra na
  // conta: sao avisos antigos, que la nem aparecem mais como novidade. A bolinha
  // conta so o que chegou com o bichinho ligado e ainda nao foi lido.
  uint32_t old;
  while (phone::takeExisting(old)) {
  }
  phone::Note note;
  while (phone::takeNote(note)) {
    Serial.printf("[celular] categoria=%d app=%s\n", note.category, note.app.c_str());
    if (note.category == 1) onMessage(note.title, "está te ligando!");
    else if (note.category == 2) onMessage(note.title, "ligação perdida", note.uid);
    else if (note.category == 4 && note.title.length()) onMessage(note.title, note.text, note.uid);  // mensagens
  }
}

// LED vermelho da lateral: da para ver de longe e com a tela apagada.
//  - mensagem sem ler no iPhone: uma piscada curta a cada 4 s
//  - dormindo: "respira" bem devagar, como notebook em repouso
//  - de bruços na mesa: apagado, para nao incomodar
static void updateLed() {
  int v = 0;
  if (ledTest >= 0) {
    v = ledTest;
  } else if (motion::faceDown()) {
    v = 0;
  } else if (unreadCount > 0) {
    v = now % 4000 < 90 ? (nightQuiet ? 25 : 110) : 0;  // de noite, piscada bem fraca
  } else if (pet::asleep() && !nightQuiet) {  // de noite o LED nao fica respirando no escuro
    float wave = sinf(now / 5000.0f * 3.1416f);  // uma respiracao a cada 5 s
    v = 1 + 34 * wave * wave;
  }
  static int last = -1;
  if (v != last) M5.Power.setLed(last = v);
}

// Sem o PC, o tempo vem do Wi-Fi do proprio aparelho. Wi-Fi e Bluetooth nao cabem
// juntos na memoria, entao de tempos em tempos o Bluetooth sai de cena por alguns
// segundos, o Wi-Fi busca o tempo e desliga, e o Bluetooth volta.
static bool phoneWaiting = false;  // Bluetooth esperando a configuracao do Wi-Fi terminar
static bool wifiFailed = false;
static bool sessionNow = false;  // comando de teste: busca o tempo ja  // a ultima tentativa nao conseguiu buscar

static void weatherSession() {
  if (phoneWaiting) {
    if (!net.setupDone) return;
    phoneWaiting = false;
    phone::begin();
  }
  enum { IDLE, FETCHING, STOPPING };
  static int state = IDLE;
  static uint32_t since = 0, nextAt = 60000, before = 0;
  bool pc = pclink::alive(now);
  switch (state) {
    case IDLE:
      if (pc) nextAt = now + 60000;  // com o PC mandando tudo, nada a fazer; sem ele, espera 1 min
      if (sessionNow) nextAt = now, sessionNow = false;
      if (!USE_WIFI || pc || !due(now, nextAt) || speech::pending() || gifs::active()) return;
      Serial.printf("[sessao] antes: %u\n", ESP.getFreeHeap());
      phone::pause();
      Serial.printf("[sessao] sem Bluetooth: %u\n", ESP.getFreeHeap());
      before = net.fetches;
      net.wifiWanted = true;
      since = now;
      state = FETCHING;
      break;
    case FETCHING:
      if (net.fetches != before || pc || now - since > 50000) {
        Serial.printf("[sessao] com Wi-Fi: %u\n", ESP.getFreeHeap());
        wifiFailed = net.fetches == before && !pc;
        net.wifiWanted = false;
        since = now;
        state = STOPPING;
      }
      break;
    case STOPPING:
      if (net.radioOff) {  // so religa o Bluetooth com o radio do Wi-Fi desligado de fato: os dois juntos nao cabem
        Serial.printf("[sessao] sem Wi-Fi: %u\n", ESP.getFreeHeap());
        phone::resume();
        Serial.printf("[sessao] Bluetooth de volta: %u\n", ESP.getFreeHeap());
        nextAt = now + (wifiFailed ? 15 : WIFI_EVERY_MIN) * MINUTE;  // falhou (sinal fraco?): tenta de novo em 15 min
        state = IDLE;
      }
      break;
  }
}

// ---- Entradas ----

static void handleButtons() {
  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) lastActivity = now;
  // Botao A: clique passa para a proxima tela; dois cliques fazem carinho;
  // segurado, abre o menu de telas (inclinar escolhe, soltar abre).
  if (M5.BtnA.wasSingleClicked()) {
    unreadCount = 0;  // saida de emergencia: um clique tambem zera a bolinha
    if (pet::asleep()) {
      wakeUp();
    } else if (speech::showing()) {
      speech::skip();
    } else {
      sound::play(sound::Chirp);
      nextScene();
    }
  }
  if (M5.BtnA.wasDoubleClicked()) onPet();
  if (M5.BtnA.wasHold()) {
    if (pet::asleep()) wakeUp();
    else openMenu();
  }
  if (menuOpen) {
    lastActivity = now;
    float tilt = motion::tiltX(rotation) - menuTilt0;
    float push = fabsf(tilt) < 0.07f ? 0 : tilt - copysignf(0.07f, tilt);
    menuPos = constrain(menuPos + push * 7 * frameDt, 0.0f, menuCount - 1.0f);
    if (!M5.BtnA.isPressed() && !menuTest) {
      menuOpen = false;
      Scene chosen = menuItems[lroundf(menuPos)]->scene;
      sound::play(sound::Chirp);
      face.blink(now);
      if (chosen == NEWS) newsBulletin();
      else show(chosen, chosen == FACE ? FACE_MS : 20000);  // a tela escolhida fica mais tempo
    }
  }

  // Botao B: clique sobe um degrau de brilho; dois cliques ligam/desligam o som;
  // segurado, vira um controle deslizante: inclinar para a direita clareia, para a
  // esquerda escurece, e soltar confirma.
  if (M5.BtnB.wasSingleClicked()) {
    size_t i = 0;
    while (i < sizeof(BRIGHT) && BRIGHT[i] <= userBright + 4) i++;
    userBright = BRIGHT[i % sizeof(BRIGHT)];
    prefs.putUChar("brightv", (uint8_t)userBright);
  }
  if (M5.BtnB.wasDoubleClicked()) {
    bool mute = !sound::muted();
    sound::setMuted(mute);
    prefs.putBool("mute", mute);
    sound::play(sound::Happy);
    speech::say(mute ? "Som desligado" : "Som ligado!", Expr::Wink);
  }
  if (M5.BtnB.wasHold()) {
    sliding = true;
    slideTilt0 = motion::tiltX(rotation);  // a posicao em que esta agora e o "parado"
  }
  if (sliding) {
    lastActivity = now;
    // quanto mais inclinado, mais depressa desliza; perto do ponto de partida, fica parado
    float tilt = motion::tiltX(rotation) - slideTilt0;
    float push = fabsf(tilt) < 0.07f ? 0 : tilt - copysignf(0.07f, tilt);
    userBright = constrain(userBright + push * 520 * frameDt, (float)BRIGHT_MIN, 255.0f);
    if (!M5.BtnB.isPressed() && !slideTest) {
      sliding = false;
      prefs.putUChar("brightv", (uint8_t)userBright);
      sound::play(sound::Chirp);
    }
  }
}

static void handleSenses() {
  face.setMotion(motion::forceX(rotation), motion::forceY(rotation),
                 motion::tiltX(rotation) + motion::spin() * 0.004f);
  if (motion::wantsFlip(rotation, now)) {
    rotation = rotation == 1 ? 3 : 1;
    M5.Display.setRotation(rotation);
    prevX0 = prevY0 = 0, prevX1 = 239, prevY1 = 134;  // redesenha a tela toda
    prefs.putUChar("rot", rotation);
  }
  if (motion::takeActivity() || pclink::playing()) lastActivity = now;
  if (motion::takeShake()) onShake();
  if (motion::takePickup()) onPickup();
  if (motion::takeTap() && pet::asleep()) wakeUp();
  if (ears::takeLoud()) onLoud();

  // Boca acompanha a voz (ou o som que houver no ambiente)
  face.setVoice(voiceTest >= 0 ? voiceTest / 100.0f : ears::voice(),
                voiceTest >= 0 ? timbreTest / 100.0f : ears::timbre());

  // Sopro: aperta os olhos contra o vento; soprando forte e comprido, manda embora o que esta na tela
  if (ears::blowing()) {
    lastActivity = now;
    face.play(Expr::Wind, now, 500);
  }
  if (ears::takeBlow()) {
    if (speech::showing()) speech::skip();
    else if (scene != FACE) show(FACE, FACE_MS);
  }

  // Assobio: escuta com cara de curioso e, quando a pessoa termina, repete a melodia no buzzer.
  // Com musica tocando nao vale: flauta e sintetizador soam como assobio.
  if (!pclink::playing() && !inCall && !nightQuiet) {  // de noite, passarinho cantando nao e convite
    if (ears::whistling()) {
      lastActivity = now;
      face.play(Expr::Curious, now, 500);
    }
    static sound::Note tune[16];
    int notes = ears::takePhrase(tune, 16);
    if (notes > 0 && !pet::asleep()) {
      uint32_t total = 0;
      for (int i = 0; i < notes; i++) total += tune[i].ms;
      face.play(Expr::Whistle, now, total + 400);
      sound::playNotes(tune, notes);
    }
  }

  // Som continuo no ambiente: danca um pouco (o Spotify do PC tem tela propria)
  if (ears::music() && !pet::asleep() && !pclink::playing() && !face.busy() && !speech::pending() &&
      now - lastDance > 45000) {
    lastDance = now;
    react(Expr::Dance, 6000);
  }
}

// ---- Iniciativa propria: cumprimentos, lembretes, piadas ----

static void think() {
  if (pet::asleep()) return;
  // Em chamada, ou com o dono longe do PC, nao adianta falar: guarda para depois
  bool gaming = pclink::gaming();
  bool quiet = inCall || gaming || pclink::userAway(now);  // jogando, nada de interromper com piada

  // Comecou ou terminou de jogar
  static bool wasGaming = false;
  static uint32_t gameStart = 0;
  if (gaming != wasGaming) {
    wasGaming = gaming;
    lastActivity = now;
    if (gaming) {
      gameStart = now;
      speech::say("Bora jogar! " + pclink::game(), Expr::Happy, sound::Happy);
    } else if (now - gameStart > 5 * MINUTE) {
      uint32_t min = (now - gameStart) / MINUTE;
      String played = min >= 60 ? String(min / 60) + "h" + (min % 60 < 10 ? "0" : "") + String(min % 60)
                               : String(min) + " min";
      speech::say("GG, " OWNER "! Foram " + played + " de jogo.", Expr::Happy, sound::Happy);
    }
  }

  // Digitando ha um tempo: ele senta ao computador e digita junto, enquanto voce continuar
  static uint32_t typingSince = 0, lastTyped = 0, lastTypeShow = 0, typeShowStart = 0;
  if (pclink::typing(now)) lastTyped = now;
  if (now - lastTyped > 4000) typingSince = now;  // pausa longa: recomeca a contagem
  bool sustained = now - typingSince > 12000;
  if (sustained && scene == FACE && !gaming && !speech::pending() &&
      (lastTypeShow == 0 || now - lastTypeShow > 4 * MINUTE)) {
    lastTypeShow = typeShowStart = now;
    show(PC, 9000);
  }
  if (scene == PC && sustained && now - typeShowStart < 45000 && due(now + 3000, sceneEnd))
    sceneEnd = now + 3000;  // segue na cena enquanto as teclas continuam (ate 45 s)

  if (!greeted && ((haveTime && now > 4000) || now > 20000)) {
    greeted = true;
    greet();
  }

  if (haveTime) {
    if (clk.tm_min == 0 && lastChimeHour != clk.tm_hour) {
      lastChimeHour = clk.tm_hour;
      if (daytime() && now > 30000) {
        if (!quiet) sound::play(sound::Hour);
        show(CLOCK, INFO_MS);
      }
    }
    for (size_t i = 0; i < lines::TIMED_COUNT; i++) {
      const lines::Timed& t = lines::TIMED[i];
      int key = clk.tm_yday * 16 + i;
      if (t.hour == clk.tm_hour && t.minute == clk.tm_min && lastTimed != key) {
        lastTimed = key;
        // na hora do almoco, de boca cheia
        speech::say(t.text, t.hour == 12 ? Expr::Chew : Expr::Talk, sound::Chirp);
      }
    }
  }

  if (daytime() && !speech::pending() && !quiet) {
    if (due(now, nextWater)) {
      nextWater = now + WATER_EVERY_MIN * MINUTE;
      remindWater();
    } else if (due(now, nextStretch)) {
      nextStretch = now + STRETCH_EVERY_MIN * MINUTE;
      remindStretch();
    } else if (due(now, nextChat)) {
      nextChat = now + random(CHAT_MIN_MIN, CHAT_MAX_MIN + 1) * MINUTE;
      if (random(2)) tellJoke();
      else speech::say(lines::name(), Expr::Talk, sound::Chirp);
    }
  }

  // Ambiente barulhento ha um tempo: comenta, no maximo a cada meia hora
  static uint32_t noisySince = 0, lastNoiseTalk = 0;
  if (ears::noiseNow() < 68) noisySince = now;
  if (now - noisySince > 3 * MINUTE && !quiet && (lastNoiseTalk == 0 || now - lastNoiseTalk > 30 * MINUTE)) {
    lastNoiseTalk = now;
    noisySince = now;
    speech::say("Tá barulhento aqui, hein, " OWNER "?", Expr::Surprised);
  }

  // PC no talo por um tempo
  bool high = pclink::alive(now) && pclink::cpu() > 90;
  if (high && !cpuHigh) cpuHighSince = now;
  cpuHigh = high;
  if (high && now - cpuHighSince > 20000 && (lastHotTalk == 0 || now - lastHotTalk > 10 * MINUTE)) {
    lastHotTalk = now;
    pcHot();
  }

  if (pclink::takeTrackChange()) {  // deu play ou trocou de musica: tela cheia com GIF
    show(MUSIC, INFO_MS + 3000);
    if (!pclink::video()) gifs::start(now);  // GIF de danca so para musica
  }

  if (due(now, nextIdle)) {
    nextIdle = now + random(9000, 20000);
    if (scene == FACE && !face.busy() && !speech::pending() && face.current() == Expr::Neutral) idleAct();
  }
}

// ---- Comandos de teste pela serial (ver tools/shot.py) ----

void onDebugCommand(const String& cmd, const String& arg) {
  int n = arg.toInt();
  if (cmd == "scene") show(n == NOISE ? NOISE : (Scene)constrain(n, 0, (int)RAINCHART), 60000);
  else if (cmd == "expr") {
    int ms = 60000;
    sscanf(arg.c_str(), "%*d %d", &ms);
    face.play((Expr)constrain(n, 0, (int)Expr::COUNT - 1), now, ms);
  }
  else if (cmd == "say") speech::say(arg);
  else if (cmd == "mood") {
    pet::addMood(n - pet::mood());
    pet::save();
  }
  else if (cmd == "tell") {  // tell <expressao> <som> <texto>
    int expr = 13, sfx = -1, used = 0;
    if (sscanf(arg.c_str(), "%d %d %n", &expr, &sfx, &used) >= 2 && used > 0 && !pet::asleep()) {
      lastActivity = now;
      speech::say(arg.substring(used), (Expr)constrain(expr, 0, (int)Expr::COUNT - 1), sfx);
    }
  } else if (cmd == "news") {  // news <posicao> <fonte>\t<manchete>
    int sp = arg.indexOf(' '), tab = arg.indexOf('\t');
    if (n >= 0 && n < NEWS_SLOTS && sp > 0 && tab > sp) {
      newsSource[n] = arg.substring(sp + 1, tab);
      newsTitle[n] = arg.substring(tab + 1);
    }
  } else if (cmd == "bulletin") {
    if (!pet::asleep() && !inCall) newsBulletin();
  } else if (cmd == "call") {
    inCall = n;
    sound::setQuiet(inCall || nightQuiet);
  } else if (cmd == "msg") {  // msg <quem>\t<texto>
    int tab = arg.indexOf('\t');
    if (tab > 0) onMessage(arg.substring(0, tab), arg.substring(tab + 1));
  } else if (cmd == "sleep") pet::forceSleep(n);
  else if (cmd == "menu") {
    menuTest = n;
    if (n) openMenu(), menuPos = min(n - 1, menuCount - 1);  // no teste entram todas as telas
    else menuOpen = false;
  } else if (cmd == "slider") sliding = slideTest = n, slideTilt0 = motion::tiltX(rotation);
  else if (cmd == "bat") batteryTest = n;  // -2 volta para a leitura de verdade
  else if (cmd == "rest") lastActivity = now - REST_AFTER_MIN * MINUTE - 1;  // testa o escurecer
  else if (cmd == "phone") {
    if (arg == "forget") phone::forget();
    phone::debugPrint();
    Serial.printf("[celular] nao lidas na bolinha: %d\n", unreadCount);
  } else if (cmd == "wifi") Serial.printf("[wifi] pedido=%d conectado=%d\n", (int)net.wifiWanted, (int)net.online);
  else if (cmd == "bright") Serial.printf("[brilho] %d descansando=%d\n", appliedBright, resting);
  else if (cmd == "noportal") net.portal = false;  // esconde o aviso de Wi-Fi para testar telas
  else if (cmd == "gif") {
    show(MUSIC, 60000);
    gifs::start(now);
  } else if (cmd == "fps") Serial.printf("[fps] %.0f bpm=%d\n", 1 / frameDt, pclink::bpm());
  else if (cmd == "imu") {
    motion::debugPrint();
    Serial.printf("[imu] rotacao=%d\n", rotation);
  }
  else if (cmd == "mic") ears::debugPrint();
  else if (cmd == "voice") voiceTest = n;
  else if (cmd == "timbre") timbreTest = n;
  else if (cmd == "night") nightTest = n;
  else if (cmd == "sfx") sound::play((sound::Sfx)constrain(n, 0, (int)sound::SFX_COUNT - 1));
  else if (cmd == "session") sessionNow = true;
  else if (cmd == "led") ledTest = n;
  else if (cmd == "wx") {
    int wmo = 0, day = 1;
    float temp = 22;
    sscanf(arg.c_str(), "%d %f %d", &wmo, &temp, &day);
    net.wmo = wmo, net.tempC = temp, net.isDay = day;
    net.rainingMm = wmo >= 51 ? 1 : 0;
    net.hasWeather = true;
  } else if (cmd == "ev") {
    if (arg == "shake") onShake();
    else if (arg == "pickup") onPickup();
    else if (arg == "loud") lastLoud = now - 7000, onLoud();
    else if (arg == "pet") onPet();
    else if (arg == "joke") tellJoke();
    else if (arg == "water") remindWater();
    else if (arg == "stretch") remindStretch();
    else if (arg == "greet") greet();
    else if (arg == "hot") pcHot();
    else if (arg == "wake") wakeUp();
  }
}

// ---- Tela ----

// Brilho desejado agora. Muda devagar para escurecer e rapido para acender.
static void applyBrightness() {
  resting = now - lastActivity > REST_AFTER_MIN * MINUTE;
  int target = lroundf(userBright);
  if (sliding) {  // ajustando: mostra na hora o brilho escolhido
    bright = target;
  } else if (motion::faceDown()) {
    target = 0;
  } else if (resting) {
    target = min(target, REST_BRIGHTNESS);
  } else if (nightQuiet) {  // de noite uma mensagem nao acende o quarto
    target = min(target, REST_BRIGHTNESS);
  } else if (pet::asleep()) {
    target = min(target, 45);
  }

  float rate = target < bright ? 0.2f : 6;  // por segundo
  bright += (target - bright) * (1 - expf(-frameDt * rate));
  if (fabsf(target - bright) < 0.6f) bright = target;
  int b = lroundf(bright);
  if (b != appliedBright) M5.Display.setBrightness(appliedBright = b);
}

// Manda para a tela so o retangulo que mudou (o que tem desenho agora mais o
// que tinha no quadro anterior). Com o rosto sozinho isso e menos da metade da
// tela, e a animacao roda bem mais lisa.

static void pushRect(int x0, int y0, int x1, int y1) {
  if (x1 < x0 || y1 < y0) return;
  M5.Display.setClipRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
  canvas.pushSprite(0, 0);
  M5.Display.clearClipRect();
}

// Acima de splitY manda o retangulo desenhado (unido ao do quadro anterior,
// para apagar o que saiu); de splitY para baixo fica a faixa da musica, que
// muda sempre e vai inteira.
static void present(int splitY) {
  const uint16_t* px = (const uint16_t*)canvas.getBuffer();
  int x0 = 240, y0 = 135, x1 = -1, y1 = -1;
  for (int y = 0; y < splitY; y++) {
    const uint16_t* row = px + y * 240;
    int l = 0;
    while (l < 240 && !row[l]) l++;
    if (l == 240) continue;
    int r = 239;
    while (!row[r]) r--;
    x0 = min(x0, l), x1 = max(x1, r);
    if (y0 > y) y0 = y;
    y1 = y;
  }
  pushRect(min(x0, prevX0), min(y0, prevY0), max(x1, prevX1), max(y1, prevY1));
  prevX0 = x0, prevY0 = y0, prevX1 = x1, prevY1 = y1;

  // inclui as linhas que a faixa ocupava no quadro anterior, se ela estiver descendo
  static int prevSplit = 135;
  pushRect(0, min(splitY, prevSplit), 239, 134);
  prevSplit = splitY;
}

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static void sunIcon(int x, int y, int r) {
  canvas.fillCircle(x, y, r, col::yellow);
  for (int i = 0; i < 8; i++) {
    float a = i * 0.7854f;
    canvas.drawLine(x + cosf(a) * (r + 2), y + sinf(a) * (r + 2), x + cosf(a) * (r + 5), y + sinf(a) * (r + 5),
                    col::yellow);
  }
}

// Desenho que representa cada tela no menu, numa caixa de ~30 px centrada em (x, y)
static void menuIcon(Scene which, int x, int y) {
  switch (which) {
    case FACE:  // o rostinho
      canvas.fillRoundRect(x - 12, y - 9, 8, 13, 3, col::white);
      canvas.fillRoundRect(x + 4, y - 9, 8, 13, 3, col::white);
      canvas.fillArc(x, y + 5, 4, 6, 20, 160, col::white);
      break;
    case CLOCK:
      canvas.fillCircle(x, y, 14, col::white);
      canvas.fillCircle(x, y, 11, col::black);
      for (int o = -1; o <= 0; o++) {
        canvas.drawLine(x + o, y, x + o, y - 8, col::white);
        canvas.drawLine(x, y + o, x + 6, y + 3 + o, col::white);
      }
      canvas.fillCircle(x, y, 2, col::orange);
      break;
    case TEMP:  // o tempo de agora
      icons::weather(canvas, x, y, net.wmo, net.isDay, now);
      break;
    case TEMPCHART:  // termometro
      canvas.fillRoundRect(x - 4, y - 14, 9, 20, 4, col::white);
      canvas.fillCircle(x, y + 8, 8, col::white);
      canvas.fillRect(x - 1, y - 6, 3, 12, col::red);
      canvas.fillCircle(x, y + 8, 5, col::red);
      break;
    case FORECAST:  // maxima e minima
      icons::arrow(canvas, x - 9, y + 2, true, col::red);
      icons::arrow(canvas, x + 9, y - 2, false, col::blue);
      break;
    case RAINCHART:
      icons::drop(canvas, x, y + 5, 9, col::blue);
      break;
    case PC:  // monitor
      canvas.fillRoundRect(x - 15, y - 13, 30, 21, 3, col::white);
      canvas.fillRect(x - 12, y - 10, 24, 15, col::black);
      for (int i = 0; i < 4; i++) {
        static const int8_t bars[] = {5, 9, 6, 11};
        canvas.fillRect(x - 9 + i * 5, y + 3 - bars[i], 3, bars[i], col::green);
      }
      canvas.fillRect(x - 2, y + 8, 4, 4, col::white);
      canvas.fillRoundRect(x - 8, y + 11, 16, 3, 1, col::white);
      break;
    case NOISE:  // ondas de som
      canvas.fillTriangle(x - 12, y, x - 4, y - 8, x - 4, y + 8, col::white);
      canvas.fillRect(x - 14, y - 4, 6, 8, col::white);
      canvas.fillArc(x - 4, y, 7, 9, 310, 410, col::green);
      canvas.fillArc(x - 4, y, 12, 14, 310, 410, col::yellow);
      canvas.fillArc(x - 4, y, 17, 19, 310, 410, col::red);
      break;
    case NEWS:  // jornal
      canvas.fillRoundRect(x - 14, y - 12, 28, 24, 3, col::white);
      canvas.fillRect(x - 10, y - 8, 20, 4, col::black);
      canvas.fillRect(x - 10, y - 1, 9, 9, col::black);
      for (int i = 0; i < 3; i++) canvas.fillRect(x + 2, y - 1 + i * 4, 8, 2, col::black);
      break;
    case MUSIC:
      icons::note(canvas, x - 5, y + 9, 12, col::spotify);
      break;
    default:
      break;
  }
}

// Menu de telas: os icones numa fita que desliza; o que para na moldura do meio e o escolhido
static void drawMenu() {
  const int top = 38, h = 62, cy = top + 26;
  canvas.fillRoundRect(8, top, 224, h, 12, col::black);
  canvas.drawRoundRect(8, top, 224, h, 12, col::dim);
  canvas.setClipRect(12, top + 2, 216, h - 4);
  for (int i = 0; i < menuCount; i++) {
    int x = 120 + lroundf((i - menuPos) * 62);
    if (x > -30 && x < 270) menuIcon(menuItems[i]->scene, x, cy);
  }
  canvas.clearClipRect();
  canvas.drawRoundRect(120 - 26, cy - 22, 52, 44, 10, col::yellow);
  canvas.drawRoundRect(120 - 25, cy - 21, 50, 42, 9, col::yellow);
  // bolinhas: onde voce esta na lista
  int chosen = lroundf(menuPos);
  for (int i = 0; i < menuCount; i++)
    canvas.fillCircle(120 + (i * 2 - (menuCount - 1)) * 6, top + h - 7, i == chosen ? 3 : 2,
                      i == chosen ? col::yellow : col::dim);
}

// Controle deslizante de brilho, por cima de tudo, enquanto o botao B esta segurado
static void drawBrightSlider() {
  const int x0 = 48, x1 = 192, y = 106;
  canvas.fillRoundRect(10, y - 20, 220, 40, 12, col::black);
  canvas.drawRoundRect(10, y - 20, 220, 40, 12, col::dim);
  sunIcon(28, y, 3);
  sunIcon(212, y, 6);
  float t = (userBright - BRIGHT_MIN) / (255.0f - BRIGHT_MIN);
  int knob = x0 + lroundf(t * (x1 - x0));
  canvas.fillRoundRect(x0, y - 3, x1 - x0, 6, 3, col::dim);
  canvas.fillRoundRect(x0, y - 3, knob - x0, 6, 3, col::yellow);
  canvas.fillCircle(knob, y, 8, col::white);
}

static void render() {
  // Avisos e falas passam na frente do rodizio; dormindo, so o rosto
  Scene target = net.portal ? SETUP : speech::showing() ? SPEECH : pet::asleep() ? FACE : scene;
  if (target != FACE) content = target;
  // na tela cheia: danca com musica, come pipoca com video
  if (target == MUSIC && pclink::playing() && !gifs::active())
    face.play(pclink::video() ? Expr::Chew : Expr::Dance, now, 400);
  k += ((target != FACE ? 1 : 0) - k) * (1 - expf(-frameDt * 6.5f));

  // Com musica tocando, uma faixa no pe da tela mostra a musica o tempo todo e o
  // resto se ajusta ao espaco que sobra. A tela cheia de musica dispensa a faixa.
  bool wantDock = pclink::playing() && target != MUSIC && target != SETUP;
  dock += ((wantDock ? 1 : 0) - dock) * (1 - expf(-frameDt * 7));
  if (dock < 0.01f && !wantDock) dock = 0;
  scenes::setDock(dock);

  // O GIF so vale enquanto a tela de musica esta no ar
  if (gifs::active() && target != MUSIC && k < 0.05f) gifs::stop();
  gifs::update(now);
  bool intro = gifs::active() && target == MUSIC;
  hideFace += ((intro ? 1 : 0) - hideFace) * (1 - expf(-frameDt * 8));

  face.setGroove(pclink::playing() && !pclink::video() && !pet::asleep(), pclink::bpm());
  // Antes de descansar ele boceja, as palpebras vao caindo e por fim cochila
  static bool wasResting = false;
  if (resting && !wasResting && !pet::asleep()) face.play(Expr::Yawn, now, 2600);
  wasResting = resting;
  Expr base = pet::baseExpr();
  if (resting && base != Expr::Asleep) base = Expr::Sleepy;
  face.setBase(base, now);
  face.update(now);

  canvas.fillScreen(col::black);
  // Sozinho, o rosto ocupa a tela; para mostrar informacao ele encolhe e sobe,
  // e ela entra por baixo
  float bob = sinf(now / 900.0f) * 1.5f * (1 - k);
  // lugar do rosto quando ha informacao na tela: no alto ao centro, salvo telas com lugar proprio
  float spotX = 120, spotY = lerp(20, 15, dock), spotScale = lerp(0.42f, 0.36f, dock);
  scenes::faceSpot(content, spotX, spotY, spotScale);
  float faceX = lerp(120, spotX, k);
  // Jogando: o rosto encolhe um pouco para caber o fone de ouvido em volta
  static float gameK = 0;
  gameK += ((pclink::gaming() && !pet::asleep() ? 1 : 0) - gameK) * (1 - expf(-frameDt * 5));
  float bigY = lerp(lerp(66, 52, dock), lerp(72, 60, dock), gameK);
  float bigScale = lerp(lerp(1.25f, 1.0f, dock), lerp(1.0f, 0.8f, dock), gameK);
  float faceY = lerp(bigY, spotY, k) + bob;
  float faceScale = lerp(bigScale, spotScale, k);
  {
    float dx, dy, lean;
    face.headPose(now, dx, dy, lean);
    scenes::setHeadPose(dx, dy, lean);
  }
  if (gameK > 0.6f && k < 0.3f && hideFace < 0.5f) scenes::drawHeadset(canvas, faceX, faceY, faceScale);
  if (hideFace < 0.98f) face.draw(canvas, faceX, faceY - 70 * hideFace, faceScale, now);  // sai por cima para dar lugar ao GIF
  if (k > 0.02f) scenes::draw(canvas, content, (1 - k) * 90, now);
  if (dock > 0) scenes::drawDock(canvas, dock, now);
  // Sem Wi-Fi so e problema se o PC tambem nao estiver mandando hora e tempo pelo cabo
  // (com a bateria na tela, o aviso de Wi-Fi vai para a esquerda dela)
  if (wifiFailed && !pclink::alive(now))  // sem PC e a ultima busca pelo Wi-Fi falhou
    icons::wifiOff(canvas, battery >= 0 ? 164 : 224, 10);
  if (battery >= 0) drawBattery();
  // luazinha no canto: modo noite, em silencio (some enquanto uma mensagem ocupa esse canto)
  if (nightQuiet && !inCall && !(speech::showing() && speech::page().icon == speech::Chat)) {
    int x = unread > 0 ? 32 : 9;
    canvas.fillCircle(x, 9, 6, col::moon);
    canvas.fillCircle(x + 3, 7, 5, col::black);
  }
  if (inCall) {  // pontinho vermelho no canto: em chamada, bichinho em silencio
    int x = unread > 0 ? 32 : 7;
    canvas.fillCircle(x, 7, 5, col::red);
    canvas.fillRoundRect(x - 1, 4, 3, 6, 1, col::white);
  }
  bool chatOnScreen = speech::showing() && speech::page().icon == speech::Chat;
  if (unread > 0 && !chatOnScreen) {  // bolinha verde com o numero de mensagens nao vistas
    canvas.fillCircle(14, 13, 10, col::spotify);
    canvas.setFont(&text::small);
    canvas.setTextColor(col::black);
    canvas.setTextDatum(middle_center);
    canvas.drawString(String(min(unread, 99)).c_str(), 14, 13);
  }
  if (sliding) drawBrightSlider();
  if (menuOpen) drawMenu();
  present(135 - lroundf(scenes::DOCK_H * dock));
}

void setup() {
  Serial.setRxBufferSize(4096);  // a capa do disco chega em rajada
  Serial.begin(115200);
  auto cfg = M5.config();
  cfg.internal_spk = false;  // o buzzer e tocado direto em sound.cpp
  M5.begin(cfg);

  prefs.begin("bichinho");
  userBright = max((int)prefs.getUChar("brightv", 120), BRIGHT_MIN);
  rotation = prefs.getUChar("rot", 1) == 3 ? 3 : 1;
  M5.Display.setRotation(rotation);
  canvas.setColorDepth(16);
  bool sprite = canvas.createSprite(240, 135);

  restoreClock();
  text::begin();
  gifs::begin();
  sound::begin(prefs.getBool("mute", false));
  ears::begin();
  pet::begin(prefs);
  nextWater = WATER_EVERY_MIN * MINUTE;
  nextStretch = STRETCH_EVERY_MIN * MINUTE;
  nextChat = random(CHAT_MIN_MIN, CHAT_MAX_MIN + 1) * MINUTE;

  Serial.printf("[boot] placa=%d sprite=%d heap=%u humor=%d\n", (int)M5.getBoard(), sprite,
                ESP.getFreeHeap(), pet::mood());
  // Ligar segurando o botao B (lateral) abre a configuracao do Wi-Fi
  M5.update();
  bool configure = M5.BtnB.isPressed();
  if (USE_WIFI) netBegin(configure);
  // configurando o Wi-Fi, o Bluetooth espera (ver loop); senao, sobe ja
  if (USE_WIFI && configure) phoneWaiting = true;
  else phone::begin();
}

void loop() {
  frameDt = constrain((millis() - now) / 1000.0f, 0.001f, 0.05f);
  now = millis();
  M5.update();
  haveTime = clockNow(clk);

  sound::update(now);
  motion::update(now);
  ears::update(now);
  pclink::update(now);
  // Pega no sono so depois de um bom tempo sem musica, movimento, barulho nem botao
  bool dozedOff = now - lastActivity > REST_AFTER_MIN * MINUTE + DOZE_AFTER_SEC * 1000;
  bool wasAsleep = pet::asleep();
  pet::update(now, dozedOff, motion::faceDown());
  if (wasAsleep && !pet::asleep() && !face.busy()) face.play(Expr::Yawn, now, 2200);  // acordou: boceja

  checkBattery();
  // Modo noite: horario de dormir e ninguem mexendo no PC
  {
    int minute = clk.tm_hour * 60 + clk.tm_min;
    bool night = haveTime && (minute >= NIGHT_FROM_MIN || minute < NIGHT_TO_MIN);
    bool atPc = pclink::alive(now) && !pclink::userAway(now);
    bool quiet = nightTest >= 0 ? nightTest : (night && !atPc);
    if (quiet != nightQuiet) {
      nightQuiet = quiet;
      sound::setQuiet(inCall || nightQuiet);
    }
  }
  handlePhone();
  updateLed();
  weatherSession();
  handleButtons();
  handleSenses();
  think();

  if (speech::update(now)) {
    const speech::Page& p = speech::page();
    face.play(p.expr, now, p.ms);
    if (p.sfx >= 0) sound::play((sound::Sfx)p.sfx);
  }
  if (due(now, sceneEnd)) {
    if (speech::pending() || pet::asleep()) sceneEnd = now + 1000;
    else nextScene();
  }

  applyBrightness();
  // Descansando, redesenha so 8 vezes por segundo: menos trabalho, menos calor
  static uint32_t lastRender = 0;
  if (!resting || now - lastRender >= 125) {
    lastRender = now;
    render();
  }
  if (pclink::takeShotRequest()) pclink::sendShot(canvas);

  int32_t spare = 16 - (int32_t)(millis() - now);
  delay(spare > 1 ? spare : 1);
}
