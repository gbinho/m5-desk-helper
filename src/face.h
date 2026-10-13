#pragma once
#include <M5Unified.h>

enum class Expr : uint8_t {
  Neutral,
  Happy,      // olhos sorrindo
  Love,       // olhos de coracao
  Dizzy,      // espirais
  Surprised,  // olhos arregalados
  Sleepy,     // palpebras pesadas
  Asleep,     // dormindo, com z z z
  Bored,
  Sad,
  Wink,
  Yawn,
  Hot,   // suando
  Cold,  // tremendo
  Talk,  // boca mexendo
  Dance, // balanca com notas musicais
  Fly,   // persegue uma mosquinha com os olhos
  Laugh,    // gargalhada: sacode e chora de rir
  Sneeze,   // prepara... atchim!
  Whistle,  // assobia, com notinhas
  Curious,  // um olho maior que o outro e um ponto de interrogacao
  Chew,     // mastigando, com migalhas
  Ball,     // bolinha que quica conforme o aparelho inclina
  Rainy,    // chuva caindo e guarda-chuva
  Coffee,   // xicara fumegando ao lado
  Wind,     // estao soprando nele: aperta os olhos contra o vento
  COUNT
};

// Rosto do bichinho. Tem uma expressao de base (o humor do momento) e pode
// tocar uma expressao temporaria por cima.
class Face {
 public:
  void update(uint32_t now);
  // cx/cy = centro dos olhos; scale 1.0 = rosto em tela cheia (240x135)
  void draw(M5Canvas& c, float cx, float cy, float scale, uint32_t now) const;

  void setBase(Expr e, uint32_t now);
  void play(Expr e, uint32_t now, uint32_t ms);
  bool busy() const { return tempOn_; }
  Expr current() const { return cur_; }
  // Movimento do aparelho: forca (em g) que empurra o rosto, que balanca como
  // gelatina; lean = quanto esta tombado para o lado (-1..1)
  void setMotion(float forceX, float forceY, float lean) {
    forceX_ = forceX, forceY_ = forceY, lean_ = lean;
  }
  // Musica tocando: os olhos ficam balancando no ritmo, mesmo fora da danca
  // bpm = andamento da musica (0 = desconhecido, usa um ritmo medio)
  void setGroove(bool on, int bpm) { groove_ = on, bpm_ = bpm; }
  // Som no ambiente, 0..1: a boca abre e fecha junto, como se ele falasse
  void setVoice(float v, float timbre = 0) { voice_ = v, timbre_ = timbre; }
  void blink(uint32_t now);
  // Para onde a "cabeca" esta agora, em relacao ao centro (em pixels na escala 1),
  // e quanto esta inclinada: o que estiver preso nela (o fone) acompanha a danca
  void headPose(uint32_t now, float& dx, float& dy, float& lean) const;

 private:
  // deslocamento do rosto inteiro por causa da expressao (danca, risada, vento...)
  void sway(uint32_t now, float& dx, float& dy) const;
  Expr base_ = Expr::Neutral, temp_ = Expr::Neutral, cur_ = Expr::Neutral;
  bool tempOn_ = false;
  uint32_t baseStart_ = 0, tempStart_ = 0, tempEnd_ = 0;

  // Ponto preso por uma mola ao lugar onde deveria estar: passa do ponto e volta
  struct Spring {
    float x = 0, y = 0, vx = 0, vy = 0;
    void step(float tx, float ty, float stiffness, float damping, float dt);
  };
  Spring eye_[2], mouth_;  // olho esquerdo, olho direito, boca
  float open_ = 1, gxT_ = 0, gyT_ = 0, forceX_ = 0, forceY_ = 0, lean_ = 0, leanS_ = 0;
  uint32_t last_ = 0;
  bool groove_ = false;
  float timbre_ = 0, mouthShape_ = 0, mouthShapeVel_ = 0;  // som grave/agudo; formato da boca (larga/alta) e sua velocidade
  float voice_ = 0, mouthOpen_ = 0, mouthVel_ = 0;  // som agora; abertura da boca (suavizada) e sua velocidade
  int bpm_ = 0;
  float ballX_ = 120, ballY_ = 10, ballVx_ = 0, ballVy_ = 0;  // bolinha, em pixels da tela
  float beat_ = 0;  // fase do balanco, avanca meia volta por batida
  uint32_t blinkEnd_ = 0, nextBlink_ = 2000;
  uint32_t gazeEnd_ = 0, nextGaze_ = 4000;
};
