#include "face.h"

#include "icons.h"
#include "util.h"

static const float TAU = 6.2831853f;
static const float DEFAULT_BPM = 112;  // ritmo do balancinho quando o andamento da musica e desconhecido

void Face::blink(uint32_t now) {
  blinkEnd_ = now + 110;
  // 1 em 5 vira piscada dupla
  nextBlink_ = now + (random(5) == 0 ? 320 : random(2000, 6000));
}

void Face::setBase(Expr e, uint32_t now) {
  if (e == base_) return;
  base_ = e;
  baseStart_ = now;
}

void Face::play(Expr e, uint32_t now, uint32_t ms) {
  // repetir a mesma expressao so estende, sem reiniciar a animacao
  if (!(tempOn_ && temp_ == e)) tempStart_ = now;
  if (e == Expr::Ball && !(tempOn_ && temp_ == e)) {  // solta a bolinha do alto
    ballX_ = 120, ballY_ = 10, ballVy_ = 0;
    ballVx_ = random(-90, 91);
  }
  temp_ = e;
  tempOn_ = true;
  tempEnd_ = now + ms;
}

// Posicao da mosquinha na tela cheia
static void flyPos(float t, float& x, float& y) {
  x = 120 + 100 * sinf(t * 1.3f);
  y = 52 + 36 * sinf(t * 2.1f + 1);
}

void Face::update(uint32_t now) {
  if (tempOn_ && due(now, tempEnd_)) tempOn_ = false;
  cur_ = tempOn_ ? temp_ : base_;

  if (due(now, nextBlink_)) blink(now);
  if (due(now, nextGaze_)) {
    gxT_ = random(-14, 15);
    gyT_ = random(-6, 7);
    gazeEnd_ = now + random(900, 2500);
    nextGaze_ = gazeEnd_ + random(2500, 7000);
  }
  if (due(now, gazeEnd_)) gxT_ = gyT_ = 0;

  float dt = constrain((now - last_) / 1000.0f, 0.001f, 0.05f);
  last_ = now;

  // Para onde o rosto quer olhar...
  float tx = gxT_, ty = gyT_;
  if (cur_ == Expr::Fly) {
    float fx, fy;
    flyPos((now - tempStart_) / 1000.0f, fx, fy);
    tx = (fx - 120) * 0.16f;
    ty = (fy - 64) * 0.2f;
  } else if (cur_ == Expr::Ball) {
    // a bolinha cai, rola para o lado em que o aparelho inclina e quica nas bordas
    ballVx_ = (ballVx_ + forceX_ * 700 * dt) * 0.997f;
    ballVy_ += (420 + forceY_ * 500) * dt;
    ballX_ += ballVx_ * dt;
    ballY_ += ballVy_ * dt;
    if (ballX_ < 8) ballX_ = 8, ballVx_ = fabsf(ballVx_) * 0.78f;
    if (ballX_ > 232) ballX_ = 232, ballVx_ = -fabsf(ballVx_) * 0.78f;
    if (ballY_ < 8) ballY_ = 8, ballVy_ = fabsf(ballVy_) * 0.78f;
    if (ballY_ > 127) ballY_ = 127, ballVy_ = -fabsf(ballVy_) * 0.78f;
    tx = (ballX_ - 120) * 0.16f;
    ty = (ballY_ - 64) * 0.2f;
  } else if (cur_ == Expr::Whistle) {
    tx = 0, ty = -5;
  } else if (cur_ == Expr::Curious) {
    tx = 7;
  } else if (cur_ == Expr::Rainy) {
    ty = -6;
  } else if (cur_ == Expr::Bored) {
    tx = 11, ty = -4;
  } else if (cur_ == Expr::Sleepy) {
    tx *= 0.3f, ty = 3;
  }
  // Balancinho de quem esta curtindo a musica: vai de um lado para o outro,
  // da um pulinho a cada batida e inclina junto
  float sway = 0;
  bool calm = cur_ != Expr::Asleep && cur_ != Expr::Dizzy && cur_ != Expr::Surprised &&
              cur_ != Expr::Dance && cur_ != Expr::Fly && cur_ != Expr::Ball && cur_ != Expr::Sneeze &&
              cur_ != Expr::Laugh;
  if (groove_ && calm) {
    // Cabeca nao acompanha musica rapida demais nem lenta demais: dobra ou
    // divide o andamento ate cair numa faixa confortavel
    float bpm = bpm_ > 0 ? bpm_ : DEFAULT_BPM;
    while (bpm > 140) bpm /= 2;
    while (bpm < 70) bpm *= 2;
    beat_ = fmodf(beat_ + dt * bpm / 60 * 3.1416f, TAU);  // acumula, para nao dar tranco quando o andamento muda
    float beat = beat_;
    sway = sinf(beat);
    tx += sway * 7;
    ty -= fabsf(sinf(beat)) * 4 - 2;
  }
  // ...mais o empurrao da gravidade e dos trancos
  float px = constrain(forceX_, -2.0f, 2.0f) * 22, py = constrain(forceY_, -2.0f, 2.0f) * 12;

  // Cada parte tem uma mola um pouco diferente, entao nao balancam juntas
  eye_[0].step(tx + px, ty + py, 115, 8.5f, dt);
  eye_[1].step(tx + px, ty + py, 140, 9.5f, dt);
  mouth_.step(tx * 0.6f + px * 0.8f, ty * 0.6f + py * 0.8f, 75, 7, dt);

  // A boca segue o volume com uma mola: solta o bastante para passar um pouco do
  // ponto a cada silaba ou batida (como gelatina), firme o bastante para nao tremer
  mouthVel_ += ((voice_ - mouthOpen_) * 170 - mouthVel_ * 15) * dt;
  mouthOpen_ = constrain(mouthOpen_ + mouthVel_ * dt, 0.0f, 1.2f);
  // O formato segue o timbre com outra mola, mais lenta: as duas nao andam juntas
  mouthShapeVel_ += ((timbre_ - mouthShape_) * 90 - mouthShapeVel_ * 11) * dt;
  mouthShape_ = constrain(mouthShape_ + mouthShapeVel_ * dt, -1.2f, 1.2f);

  float ease = 1 - expf(-dt * 18);
  open_ += ((due(now, blinkEnd_) ? 1 : 0) - open_) * ease;
  leanS_ += (constrain(lean_ + sway * 0.4f, -1.0f, 1.0f) - leanS_) * (1 - expf(-dt * 9));
}

void Face::Spring::step(float tx, float ty, float stiffness, float damping, float dt) {
  vx += ((tx - x) * stiffness - vx * damping) * dt;
  vy += ((ty - y) * stiffness - vy * damping) * dt;
  x = constrain(x + vx * dt, -34.0f, 34.0f);
  y = constrain(y + vy * dt, -16.0f, 16.0f);
}

static void heart(M5Canvas& c, float x, float y, float q, uint16_t color) {
  c.fillCircle(x - q, y - q * 0.4f, q * 1.08f, color);
  c.fillCircle(x + q, y - q * 0.4f, q * 1.08f, color);
  c.fillTriangle(x - q * 2, y + q * 0.1f, x + q * 2, y + q * 0.1f, x, y + q * 2.3f, color);
}

static void spiral(M5Canvas& c, float x, float y, float radius, float phase, float thick) {
  for (int i = 8; i <= 130; i++) {
    float f = i / 130.0f, a = phase + f * 2.4f * TAU;
    c.fillCircle(x + cosf(a) * radius * f, y + sinf(a) * radius * f, thick, col::white);
  }
}

static void letterZ(M5Canvas& c, float x, float y, float sz, uint16_t color) {
  for (int o = 0; o < 2; o++) {
    c.drawLine(x, y + o, x + sz, y + o, color);
    c.drawLine(x + sz, y + o, x, y + sz + o, color);
    c.drawLine(x, y + sz + o, x + sz, y + sz + o, color);
  }
}

void Face::sway(uint32_t now, float& dx, float& dy) const {
  const Expr e = cur_;
  const float t = (now - (tempOn_ ? tempStart_ : baseStart_)) / 1000.0f;
  const float progress = tempOn_ && tempEnd_ != tempStart_
                             ? constrain((float)(now - tempStart_) / (tempEnd_ - tempStart_), 0.0f, 1.0f)
                             : 0;
  dx = dy = 0;
  if (e == Expr::Cold) dx = (now / 45) % 2 ? 1.5f : -1.5f;
  if (e == Expr::Dizzy) dx = sinf(t * 5) * 4;
  if (e == Expr::Dance) {
    dx = sinf(t * 6) * 9;
    dy = -fabsf(sinf(t * 6)) * 5;
  }
  if (e == Expr::Laugh) dy = sinf(t * 24) * 2.5f;
  if (e == Expr::Wind) dx = 9 + sinf(t * 40) * 2;  // empurrado pelo vento, tremendo
  if (e == Expr::Sneeze) {  // sobe devagar tomando folego e despenca no atchim
    if (progress < 0.55f) dy = -progress / 0.55f * 7;
    else if (progress < 0.72f) dy = 9, dx = sinf(t * 60) * 3;
  }
}

void Face::headPose(uint32_t now, float& dx, float& dy, float& lean) const {
  sway(now, dx, dy);
  // os olhos tambem balancam (ritmo da musica, inclinacao do aparelho): a cabeca vai junto
  dx += (eye_[0].x + eye_[1].x) * 0.5f * 0.85f;
  dy += (eye_[0].y + eye_[1].y) * 0.5f * 0.85f;
  lean = leanS_;
}

void Face::draw(M5Canvas& c, float cx, float cy, float s, uint32_t now) const {
  const Expr e = cur_;
  const uint32_t start = tempOn_ ? tempStart_ : baseStart_;
  const float t = (now - start) / 1000.0f;
  // 0..1 ao longo de uma expressao temporaria
  const float progress = tempOn_ && tempEnd_ != tempStart_
                             ? constrain((float)(now - tempStart_) / (tempEnd_ - tempStart_), 0.0f, 1.0f)
                             : 0;

  float swayX, swayY;
  sway(now, swayX, swayY);
  cx += swayX * s;
  cy += swayY * s;

  const float eyeDist = 40 * s, baseW = 30 * s, baseH = 46 * s;
  const float ox = mouth_.x * s, oy = mouth_.y * s;
  const uint16_t eyeColor = e == Expr::Cold ? col::ice : col::white;

  auto rectEye = [&](float x, float y, float w, float h, float lid, bool blinks) {
    if (blinks) h = max(h * open_, 4 * s);
    c.fillRoundRect(x - w / 2, y - h / 2, w, h, min(w, h) * 0.4f, eyeColor);
    if (lid > 0) c.fillRect(x - w / 2 - 1, y - h / 2 - 1, w + 3, h * lid + 1, col::black);
  };
  auto arcUp = [&](float x, float y) {  // "^" sorrindo
    float r1 = 17 * s;
    c.fillArc(x, y + 8 * s, r1 - max(7 * s, 2.0f), r1, 200, 340, col::white);
  };
  auto arcDown = [&](float x, float y) {  // fechado, dormindo
    float r1 = 17 * s;
    c.fillArc(x, y - 8 * s, r1 - max(5 * s, 2.0f), r1, 25, 155, col::white);
  };

  if (e == Expr::Rainy && s > 0.8f) {
    // guarda-chuva: meia elipse por cima do rosto, com a ponta
    c.fillEllipse(cx, cy - 34 * s, 58 * s, 17 * s, col::red);
    c.fillRect(cx - 59 * s, cy - 34 * s, 118 * s + 2, 18 * s + 1, col::black);
    for (int i = -1; i <= 1; i++) c.fillEllipse(cx + i * 38 * s, cy - 34 * s, 19 * s, 5 * s, col::black);
    c.fillRect(cx - 1, cy - 55 * s, 3, 5 * s, col::red);
  }

  for (int side = -1; side <= 1; side += 2) {
    const Spring& sp = eye_[side > 0];
    // tombado, o olho do lado de baixo desce e incha, como liquido escorrendo
    float x = cx + side * eyeDist + sp.x * s, y = cy + sp.y * s + side * leanS_ * 8 * s;
    float pool = 1 + side * leanS_ * 0.13f;
    // estica na direcao em que esta se movendo e afina na outra
    float stretch = constrain((fabsf(sp.vx) - fabsf(sp.vy)) / 420, -0.24f, 0.24f);
    const float eyeW = baseW * pool * (1 + stretch), eyeH = baseH * pool * (1 - stretch);
    switch (e) {
      case Expr::Happy:
      case Expr::Dance:
      case Expr::Laugh:
      case Expr::Coffee:
        arcUp(x, y);
        break;
      case Expr::Love:
        heart(c, x, y - 4 * s, 8.5f * s * (1 + 0.12f * sinf(t * 9)), col::pink);
        break;
      case Expr::Dizzy:
        spiral(c, x, y, 20 * s, side * t * 7, max(1.7f * s, 0.8f));
        break;
      case Expr::Surprised:
        rectEye(x, y, eyeW * 1.13f, eyeH * 1.17f, 0, false);
        break;
      case Expr::Sleepy:
        // as palpebras vao caindo aos poucos
        rectEye(x, y, eyeW, eyeH, 0.2f + min(t * 0.06f, 0.3f) + 0.1f * sinf(t * 0.9f), true);
        break;
      case Expr::Asleep:
        arcDown(x, y);
        break;
      case Expr::Bored:
        rectEye(x, y, eyeW, eyeH, 0.5f, true);
        break;
      case Expr::Hot:
        rectEye(x, y, eyeW, eyeH, 0.35f, true);
        break;
      case Expr::Sad: {
        rectEye(x, y, eyeW, eyeH, 0, true);
        // sobrancelha caida: corta o canto de fora
        float h = max(eyeH * open_, 4 * s), top = y - h / 2 - 1;
        float outer = x + side * (eyeW / 2 + 1), inner = x - side * (eyeW / 2 + 1);
        c.fillTriangle(outer, top, inner, top, outer, top + h * 0.42f, col::black);
        break;
      }
      case Expr::Wink:
        if (side > 0) arcUp(x, y);
        else rectEye(x, y, eyeW, eyeH, 0, true);
        break;
      case Expr::Yawn:
        if (progress > 0.15f && progress < 0.85f) arcUp(x, y);
        else rectEye(x, y, eyeW, eyeH, 0.4f, false);
        break;
      case Expr::Sneeze:
        if (progress < 0.55f) rectEye(x, y, eyeW, eyeH, 0.15f + 0.5f * progress / 0.55f, false);
        else if (progress < 0.72f) arcUp(x, y);
        else rectEye(x, y, eyeW, eyeH, 0.45f, false);
        break;
      case Expr::Curious:  // um olho arregala, o outro aperta
        if (side < 0) rectEye(x, y, eyeW * 1.22f, eyeH * 1.22f, 0, true);
        else rectEye(x, y + 4 * s, eyeW * 0.82f, eyeH * 0.68f, 0.2f, true);
        break;
      case Expr::Wind:  // olhos apertados
        rectEye(x, y, eyeW * 1.1f, eyeH * 0.3f, 0, false);
        break;
      case Expr::Chew:
        rectEye(x, y, eyeW, eyeH * 0.78f, 0.25f, true);
        break;
      default:
        // falando alto, os olhos arregalam um pouco junto
        rectEye(x, y - (e == Expr::Neutral ? 2 * mouthOpen_ * s : 0), eyeW,
                eyeH * (e == Expr::Neutral ? 1 + 0.1f * mouthOpen_ : 1), 0, true);
    }
  }

  // Boca
  const float mx = cx + ox, my = cy + oy;
  auto smile = [&](float r, float spread) {
    c.fillArc(mx, my + 12 * s, r * s - max(3.5f * s, 2.0f), r * s, spread, 180 - spread, col::white);
  };
  auto openMouth = [&](float rx, float ry) {
    c.fillEllipse(mx, my + 21 * s, max(rx * s, 1.5f), max(ry * s, 1.0f), col::white);
  };
  auto flat = [&]() {
    c.fillRoundRect(mx - 8 * s, my + 19 * s, 16 * s, max(3.5f * s, 2.0f), max(1.5f * s, 1.0f), col::white);
  };
  switch (e) {
    case Expr::Happy:
    case Expr::Love:
    case Expr::Dance:
    case Expr::Coffee:
      smile(15, 10);
      break;
    case Expr::Dizzy:
      openMouth(4 + sinf(t * 6), 4);
      break;
    case Expr::Surprised:
      openMouth(5, 6.5f);
      break;
    case Expr::Sleepy:
    case Expr::Bored:
    case Expr::Cold:
      flat();
      break;
    case Expr::Asleep:  // boquinha aberta, respirando devagar
      openMouth(5, 3 + 1.5f * sinf(t * 1.4f));
      break;
    case Expr::Sad:
      c.fillArc(mx, my + 31 * s, 13 * s - max(3.5f * s, 2.0f), 13 * s, 215, 325, col::white);
      break;
    case Expr::Yawn:
      openMouth(8, 1.5f + 11 * sinf(progress * 3.1416f));
      break;
    case Expr::Hot:
      openMouth(6, 3.5f + 1.5f * sinf(t * 9));
      break;
    case Expr::Talk:
      openMouth(6, 1.5f + 4.5f * fabsf(sinf(t * 9)));
      break;
    case Expr::Laugh:
      openMouth(9, 6 + 2.5f * sinf(t * 24));
      break;
    case Expr::Sneeze:
      if (progress < 0.55f) openMouth(3 + 3 * progress, 3 + 5 * progress);
      else if (progress < 0.72f) openMouth(9, 10);
      else flat();
      break;
    case Expr::Whistle:  // biquinho
      c.fillCircle(mx, my + 20 * s, max(4.5f * s, 2.0f), col::white);
      c.fillCircle(mx, my + 20 * s, max(2 * s, 1.0f), col::black);
      break;
    case Expr::Wind: {  // boca tremida
      float w = (14 + 3 * sinf(t * 30)) * s;
      c.fillRoundRect(mx - w / 2, my + 19 * s, w, max(3.5f * s, 2.0f), max(1.5f * s, 1.0f), col::white);
      break;
    }
    case Expr::Curious:
      smile(10, 28);
      break;
    case Expr::Chew: {
      float w = (12 + 4 * sinf(t * 10)) * s, h = max((4 + 1.5f * cosf(t * 10)) * s, 2.0f);
      c.fillRoundRect(mx - w / 2, my + 19 * s, w, h, h / 2, col::white);
      break;
    }
    default:
      // Tem alguem falando (ou som no ambiente): a boca vira uma bolinha que
      // cresce com o volume. O tamanho vem de mouthOpen_, que segue o som com
      // uma mola (ver update), entao enche e murcha sem trancos.
      if (mouthOpen_ > 0.04f && e == Expr::Neutral) {
        // O formato vem do tipo de som: grave deixa a boca larga ("ooo"), agudo
        // deixa alta e estreita ("iii"). Abrindo ela estica para cima, fechando
        // achata para os lados, sem mudar de tamanho: e o que da o ar de gelatina.
        float r = 2 + 15 * mouthOpen_;
        float stretch = constrain(mouthShape_ * 0.34f + mouthVel_ * 0.035f, -0.5f, 0.5f);
        float rx = r * (1 - stretch) * s, ry = r * (1 + stretch) * s;
        c.fillEllipse(mx, my + (19 + 5 * mouthOpen_) * s, max(rx, 1.5f), max(ry, 1.5f), col::white);
      } else {
        smile(13, 20);
      }
  }

  // Detalhes coloridos
  if (e == Expr::Wind && s > 0.8f) {  // riscos de vento atravessando a tela
    for (int i = 0; i < 6; i++) {
      float p = fmodf(t * 2.2f + i * 0.31f, 1);
      int wy = 14 + i * 21, wx = -40 + p * 300, len = 26 + (i % 3) * 12;
      c.drawFastHLine(wx, wy, len, col::grey);
      c.drawFastHLine(wx + 6, wy + 1, len - 12, col::grey);
    }
  }
  if (e == Expr::Laugh) {  // lagrimas de riso voando para os lados
    for (int side = -1; side <= 1; side += 2) {
      float p = fmodf(t * 1.6f + (side > 0 ? 0.4f : 0), 1);
      icons::drop(c, cx + side * (62 + p * 18) * s, cy - (8 - p * p * 30) * s, max(3.5f * s, 1.5f),
                  mix(col::blue, col::black, p * p));
    }
  }
  if (e == Expr::Sneeze && progress > 0.55f && progress < 0.9f) {  // perdigotos
    float q = (progress - 0.55f) / 0.35f;
    for (int i = 0; i < 6; i++) {
      float a = (20 + i * 28) * 0.01745f;
      c.fillCircle(mx + cosf(a) * q * 44 * s, my + 21 * s + sinf(a) * q * 30 * s, max(2.2f * s * (1 - q), 1.0f),
                   col::ice);
    }
  }
  if (e == Expr::Whistle) {
    static const uint16_t NOTES[] = {col::yellow, col::blue};
    for (int i = 0; i < 2; i++) {
      float p = fmodf(t * 0.6f + i * 0.5f, 1);
      icons::note(c, mx + (sinf(p * 9 + i * 3) * 7 - 3) * s, my + (8 - p * 62) * s, max(6 * s, 3.0f),
                  mix(NOTES[i], col::black, p * p));
    }
  }
  if (e == Expr::Curious && s > 0.8f) {  // ponto de interrogacao
    int qx = cx + 88, qy = cy - 44;
    c.fillArc(qx, qy, 4, 8, 200, 450, col::yellow);
    c.fillRect(qx - 2, qy + 7, 4, 6, col::yellow);
    c.fillCircle(qx, qy + 19, 3, col::yellow);
  }
  if (e == Expr::Chew) {
    for (int side = -1; side <= 1; side += 2)  // bochechas cheias
      c.fillEllipse(cx + side * 60 * s + ox, cy + 26 * s + oy, max((8 + 2 * sinf(t * 10 + side)) * s, 2.0f),
                    max(5 * s, 1.5f), col::pink);
    for (int i = 0; i < 3; i++) {  // migalhas
      float p = fmodf(t * 1.2f + i * 0.33f, 1);
      c.fillCircle(mx + (i - 1) * 8 * s, my + (27 + p * 22) * s, max(1.6f * s, 1.0f), mix(col::orange, col::black, p));
    }
  }
  if (e == Expr::Ball && s > 0.8f) {
    c.fillCircle(ballX_, ballY_, 7, col::orange);
    c.fillCircle(ballX_ - 2, ballY_ - 2, 2, col::yellow);
  }
  if (e == Expr::Rainy && s > 0.8f) {
    for (int i = 0; i < 15; i++) {  // gotas, menos embaixo do guarda-chuva
      float p = fmodf(now / 700.0f + i * 0.37f, 1);
      int dx = 6 + i * 16, dy = p * 135;
      if (fabsf(dx - cx) < 58 * s && dy > cy - 50 * s) continue;
      c.drawLine(dx, dy, dx - 2, dy + 7, col::blue);
      c.drawLine(dx + 1, dy, dx - 1, dy + 7, col::blue);
    }
  }
  if (e == Expr::Coffee && s > 0.8f) {
    int kx = cx + 92, ky = cy + 2;
    c.fillRoundRect(kx - 13, ky, 26, 23, 5, col::white);
    c.fillArc(kx + 13, ky + 11, 5, 8, 270, 450, col::white);  // alca
    c.fillEllipse(kx, ky + 4, 10, 3, rgb(120, 70, 30));
    for (int i = 0; i < 2; i++)  // fumaca
      for (int k = 0; k < 5; k++)
        c.fillCircle(kx - 5 + i * 10 + sinf(t * 3 + k * 0.8f + i * 2) * 3, ky - 5 - k * 4, 1,
                     mix(col::grey, col::black, k / 5.0f));
  }
  if (e == Expr::Happy || e == Expr::Love || e == Expr::Wink || e == Expr::Laugh || e == Expr::Coffee) {
    for (int side = -1; side <= 1; side += 2)
      c.fillEllipse(cx + side * 63 * s + ox, cy + 27 * s + oy, max(7 * s, 2.0f), max(3.5f * s, 1.0f),
                    col::pink);
  }
  if (e == Expr::Asleep) {
    // bochechas coradas
    for (int side = -1; side <= 1; side += 2)
      c.fillEllipse(cx + side * 62 * s + ox, cy + 20 * s + oy, max(8 * s, 2.0f), max(4 * s, 1.0f), col::pink);
    // Bolha no nariz: enche quando ele solta o ar e murcha quando puxa. A cada
    // quatro respiracoes ela cresce demais e estoura.
    const float breath = 4.5f;
    int cycle = (int)(t / breath);
    float p = fmodf(t, breath) / breath;
    bool big = cycle % 4 == 3;
    float r = (2.5f + (big ? 12 : 8) * powf(sinf(p * 3.1416f), 2)) * s;
    // pendurada para o lado, saindo do nariz: no meio do rosto parecia um terceiro olho
    float bx = cx + ox + 9 * s + r * 0.75f, by = cy + oy + 9 * s + r * 0.55f;
    if (big && p > 0.5f) {
      if (p < 0.58f) {  // ploc!
        for (int i = 0; i < 8; i++) {
          float a = i * 0.7854f;
          c.drawLine(bx + cosf(a) * r * 0.7f, by + sinf(a) * r * 0.7f, bx + cosf(a) * r * 1.3f,
                     by + sinf(a) * r * 1.3f, col::ice);
        }
      }
    } else {
      c.fillCircle(bx, by, r, col::ice);
      c.fillCircle(bx, by, max(r - max(1.3f * s, 1.0f), 0.0f), col::black);
      c.fillCircle(bx - r * 0.38f, by - r * 0.38f, max(r * 0.2f, 1.0f), col::white);
    }
    for (int i = 0; i < 3; i++) {
      float p = fmodf(t * 0.3f + i / 3.0f, 1);
      letterZ(c, cx + (58 + p * 34) * s, cy - (8 + p * 44) * s, (5 + p * 8) * s,
              mix(col::blue, col::black, p * p));
    }
  }
  if (e == Expr::Hot) {
    float p = fmodf(t * 0.7f, 1);
    icons::drop(c, cx + 66 * s, cy - (26 - p * 22) * s, max(5 * s, 2.0f), mix(col::blue, col::black, p * p));
  }
  if (e == Expr::Sad) {
    float p = fmodf(t * 0.5f, 1);
    icons::drop(c, cx - eyeDist + eye_[0].x * s, cy + (30 + p * 16) * s, max(4 * s, 1.5f),
                mix(col::blue, col::black, p * p));
  }
  if (e == Expr::Dizzy) {
    for (int i = 0; i < 3; i++) {
      float a = t * 4 + i * TAU / 3;
      c.fillCircle(cx + cosf(a) * 48 * s, cy - 38 * s + sinf(a) * 6 * s, max(3 * s, 1.0f), col::yellow);
    }
  }
  if (e == Expr::Surprised && s > 0.8f) {
    c.fillRoundRect(cx + 84, cy - 46, 6, 20, 3, col::yellow);
    c.fillCircle(cx + 87, cy - 18, 3, col::yellow);
  }
  if (e == Expr::Dance) {
    static const uint16_t NOTE_COLORS[] = {col::yellow, col::pink, col::blue};
    for (int i = 0; i < 3; i++) {
      float p = fmodf(t * 0.45f + i / 3.0f, 1);
      float side = i % 2 ? 1 : -1;
      icons::note(c, cx + side * (70 + 12 * sinf(p * TAU)) * s - sinf(t * 6) * 9 * s,
                  cy + (10 - p * 50) * s, max(7 * s, 3.0f), mix(NOTE_COLORS[i], col::black, p * p));
    }
  }
  if (e == Expr::Fly && s > 0.8f) {
    float fx, fy;
    flyPos(t, fx, fy);
    bool flap = (now / 50) % 2;
    c.fillCircle(fx - 3, fy - (flap ? 4 : 2), 2, col::grey);
    c.fillCircle(fx + 3, fy - (flap ? 4 : 2), 2, col::grey);
    c.fillCircle(fx, fy, 2, col::orange);
  }
}
