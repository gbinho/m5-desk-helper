#include "motion.h"

#include <M5Unified.h>

namespace motion {

// Eixos do M5StickC Plus (em pe, USB para baixo): X = direita, Y = cima, Z = saindo da tela.
// O acelerometro marca +1 g no eixo que aponta para o ceu.
static float ax, ay, az;             // leitura crua
static float fx, fy, fz = 1;         // filtrada (gravidade)
static float sx, sy;                 // quase crua: guarda os trancos
static float spinZ;                  // giro em torno da tela, graus/s
static float gyro;                   // modulo da rotacao, graus/s
static float bx, by, bz;             // desvio do giroscopio parado
static bool biasInit;
static float peakGyro, peakJerk;     // maiores valores desde o ultimo debugPrint
static bool shake_, pickup_, tap_, down_, activity_;

static uint32_t shakeStart, lastHit, calmStart, lastCalm, downSince, flipSince;
static uint8_t hits;
static bool calm, wasDown, flipping;

void update(uint32_t now) {
  if (!M5.Imu.update()) return;
  auto d = M5.Imu.getImuData();
  float jerk = fabsf(d.accel.x - ax) + fabsf(d.accel.y - ay) + fabsf(d.accel.z - az);
  ax = d.accel.x;
  ay = d.accel.y;
  az = d.accel.z;
  fx += (ax - fx) * 0.15f;
  fy += (ay - fy) * 0.15f;
  fz += (az - fz) * 0.15f;
  sx += (ax - sx) * 0.5f;
  sy += (ay - sy) * 0.5f;

  // O giroscopio vem com um desvio grande de fabrica: aprende o "zero" enquanto
  // o aparelho esta quieto e desconta
  if (!biasInit) {
    bx = d.gyro.x, by = d.gyro.y, bz = d.gyro.z;
    biasInit = true;
  } else if (jerk < 0.05f || now < 4000) {
    float rate = now < 4000 ? 0.2f : 0.02f;  // logo ao ligar aprende depressa
    bx += (d.gyro.x - bx) * rate;
    by += (d.gyro.y - by) * rate;
    bz += (d.gyro.z - bz) * rate;
  }
  float gx = d.gyro.x - bx, gy = d.gyro.y - by, gz = d.gyro.z - bz;
  gyro = sqrtf(gx * gx + gy * gy + gz * gz);
  spinZ += (gz - spinZ) * 0.3f;
  peakGyro = max(peakGyro, gyro);
  peakJerk = max(peakJerk, jerk);
  if (gyro > 10 || jerk > 0.14f) activity_ = true;  // parado, o sensor oscila ate uns 5 e 0.08

  // Chacoalhao: varios trancos fortes em menos de 1 s
  float dev = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1);
  if (dev > 0.8f && now - lastHit > 60) {
    if (now - shakeStart > 1000) {
      shakeStart = now;
      hits = 0;
    }
    lastHit = now;
    if (++hits >= 4) {
      shake_ = true;
      hits = 0;
    }
  }

  // Pegar da mesa: movimento depois de um bom tempo parado.
  // Batidinha: tranco pequeno enquanto esta parado.
  if (gyro < 20) {
    if (!calm) {
      calm = true;
      calmStart = now;
    }
    lastCalm = now;
    if (jerk > 0.2f && now - calmStart > 1000) tap_ = true;
  } else if (calm) {
    if (gyro > 60) {
      if (lastCalm - calmStart > 4000) pickup_ = true;
      calm = false;
    } else if (now - lastCalm > 500) {
      calm = false;
    }
  }

  // Tela virada para a mesa
  bool isDown = fz < -0.85f;
  if (isDown && !wasDown) downSince = now;
  wasDown = isDown;
  down_ = isDown && now - downSince > 1500;
}

// Na rotacao 1 a direita da tela e o -Y do aparelho e o "para baixo" e o -X
static float gravRight(uint8_t rot) { return rot == 1 ? fy : -fy; }
static float gravDown(uint8_t rot) { return rot == 1 ? fx : -fx; }

float tiltX(uint8_t rot) { return constrain(gravRight(rot), -1.0f, 1.0f); }
float tiltY(uint8_t rot) {
  // em pe a gravidade ja aponta para baixo o tempo todo; so conta quando esta deitado
  return constrain(gravDown(rot) * fabsf(fz), -1.0f, 1.0f);
}

float forceX(uint8_t rot) { return rot == 1 ? sy : -sy; }
float forceY(uint8_t rot) {
  // tranco vertical + a inclinacao de quando esta deitado
  float jolt = (rot == 1 ? sx - fx : fx - sx);
  return jolt + tiltY(rot);
}
float spin() { return spinZ; }

bool wantsFlip(uint8_t rot, uint32_t now) {
  bool upsideDown = gravDown(rot) < -0.55f;
  if (upsideDown && !flipping) flipSince = now;
  flipping = upsideDown;
  if (flipping && now - flipSince > 700) {
    flipping = false;
    return true;
  }
  return false;
}

static bool take(bool& flag) {
  bool v = flag;
  flag = false;
  return v;
}
bool takeShake() { return take(shake_); }
bool takePickup() { return take(pickup_); }
bool takeTap() { return take(tap_); }
bool takeActivity() { return take(activity_); }
bool faceDown() { return down_; }

void debugPrint() {
  Serial.printf("[imu] a=%.2f %.2f %.2f gyro=%.0f pico: giro=%.1f tranco=%.3f\n", fx, fy, fz, gyro, peakGyro, peakJerk);
  peakGyro = peakJerk = 0;
}

}  // namespace motion
