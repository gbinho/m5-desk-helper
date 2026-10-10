#include "phone.h"

#include <NimBLEDevice.h>
#include <nvs.h>

namespace phone {

// Servico de notificacoes da Apple (ANCS), que todo iPhone oferece a acessorios pareados
static const NimBLEUUID ANCS("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static const NimBLEUUID NOTIFICATION_SOURCE("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static const NimBLEUUID CONTROL_POINT("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static const NimBLEUUID DATA_SOURCE("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

static const int TITLE_MAX = 40, TEXT_MAX = 240;

static NimBLEServer* server = nullptr;
static NimBLEClient* client = nullptr;
static const NimBLERemoteCharacteristic* control = nullptr;
static SemaphoreHandle_t lock;

static volatile bool connected_ = false, secured = false, ready = false, newBond = false, paired_ = false;
static volatile int rssi_ = 0;
static volatile bool running = false;  // Bluetooth no ar

// Notificacoes avisadas pelo iPhone, esperando eu pedir os detalhes
struct Pending {
  uint32_t uid;
  uint8_t category;
};
static Pending pending[8];
static int pendingCount = 0;

// Resposta em andamento (chega em pedacos) e notificacoes prontas para o programa principal
static std::string rx;
static uint8_t rxCategory = 0;
static uint32_t rxUid = 0;
static uint32_t removed[16];
static int removedCount = 0;
static uint32_t existing[16];
static int existingCount = 0;
static volatile bool awaiting = false;
static uint32_t askedAt = 0;
static Note notes[4];
static int noteCount = 0;

// Tira emoji e o que mais a fonte da tela nao tem; troca aspas curvas e afins
static String latin(const std::string& s) {
  String out;
  for (size_t i = 0; i < s.size();) {
    uint8_t b = s[i];
    uint32_t cp = b;
    int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
    if (extra) cp = b & (0x3F >> extra);
    for (int k = 1; k <= extra && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += extra + 1;
    if (cp == 0x2018 || cp == 0x2019) cp = '\'';
    else if (cp == 0x201C || cp == 0x201D) cp = '"';
    else if (cp == 0x2013 || cp == 0x2014) cp = '-';
    else if (cp == '\n' || cp == '\r' || cp == '\t') cp = ' ';
    if (cp < 0x20 || cp > 0xFF) continue;
    if (cp < 0x80) out += (char)cp;
    else {
      out += (char)(0xC0 | (cp >> 6));
      out += (char)(0x80 | (cp & 0x3F));
    }
  }
  out.trim();
  return out;
}

// Resposta do iPhone: comando, id, e para cada atributo: codigo, tamanho (2 bytes), texto
static void onData(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  rx.append((const char*)data, len);
  if (rx.size() < 5) return;
  std::string field[3];
  size_t at = 5;
  for (int i = 0; i < 3; i++) {
    if (rx.size() < at + 3) return;  // ainda falta pedaco
    size_t n = (uint8_t)rx[at + 1] | ((uint8_t)rx[at + 2] << 8);
    if (rx.size() < at + 3 + n) return;
    field[i] = rx.substr(at + 3, n);
    at += 3 + n;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  if (noteCount < 4)
    notes[noteCount++] = {rxUid, rxCategory, String(field[0].c_str()), latin(field[1]), latin(field[2])};
  xSemaphoreGive(lock);
  rx.clear();
  awaiting = false;
}

// Aviso do iPhone: evento, marcas, categoria, quantidade, id (4 bytes)
static void onSource(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) {
  if (len < 8) return;
  uint32_t uid;
  memcpy(&uid, d + 4, 4);
  if (d[0] == 2) {  // sumiu do telefone: foi lida ou dispensada la
    xSemaphoreTake(lock, portMAX_DELAY);
    if (removedCount < 16) removed[removedCount++] = uid;
    xSemaphoreGive(lock);
    return;
  }
  if (d[0] != 0) return;    // so interessa notificacao nova
  if (d[1] & 0x04) {  // ja existia antes de conectar: nao aparece na tela, mas conta como nao lida
    if (d[2] == 4 || d[2] == 2) {
      xSemaphoreTake(lock, portMAX_DELAY);
      if (existingCount < 16) existing[existingCount++] = uid;
      xSemaphoreGive(lock);
    }
    return;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  if (pendingCount < 8) pending[pendingCount++] = {uid, d[2]};
  xSemaphoreGive(lock);
}

// Diario curto do que aconteceu na ligacao (conectou, pareou, caiu e por que),
// para investigar depois pelo comando "phone" sem precisar estar olhando na hora
static char diary[10][72];
static int diaryCount = 0;
static void note(const char* fmt, ...) {
  if (diaryCount == 10) {
    memmove(diary[0], diary[1], sizeof(diary[0]) * 9);
    diaryCount = 9;
  }
  int at = snprintf(diary[diaryCount], sizeof(diary[0]), "%lus ", (unsigned long)(millis() / 1000));
  va_list args;
  va_start(args, fmt);
  vsnprintf(diary[diaryCount] + at, sizeof(diary[0]) - at, fmt, args);
  va_end(args);
  diaryCount++;
}

class Callbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    connected_ = true;
    newBond = !info.isBonded();
    note("conectou, ja pareado=%d, guardados=%d", (int)info.isBonded(), NimBLEDevice::getNumBonds());
    client = s->getClient(info);
    // O servico da Apple so aparece com a ligacao cifrada. Celular novo: eu peco o
    // pareamento. Celular ja pareado: o proprio iPhone cifra ao reconectar, e pedir
    // de novo ao mesmo tempo pode embolar a negociacao.
    if (!info.isBonded()) NimBLEDevice::startSecurity(info.getConnHandle());
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    secured = info.isEncrypted();
    note("seguranca: cifrado=%d pareado=%d guardados=%d", (int)info.isEncrypted(), (int)info.isBonded(),
         NimBLEDevice::getNumBonds());
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    note("desconectou, motivo 0x%x, guardados=%d", reason, NimBLEDevice::getNumBonds());
    connected_ = secured = ready = awaiting = false;
    control = nullptr;
    client = nullptr;
    rssi_ = 0;
    pendingCount = 0;
    rx.clear();
  }
} callbacks;

// Acha o servico de notificacoes no iPhone e se inscreve
static bool subscribe() {
  if (!client) return false;
  auto service = client->getService(ANCS);
  if (!service) return false;
  auto source = service->getCharacteristic(NOTIFICATION_SOURCE);
  auto data = service->getCharacteristic(DATA_SOURCE);
  control = service->getCharacteristic(CONTROL_POINT);
  if (!source || !data || !control) return false;
  return data->subscribe(true, onData) && source->subscribe(true, onSource);
}

static void task(void*) {
  uint32_t nextRssi = 0, nextTry = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t now = millis();
    if (!running || !connected_) continue;

    if (secured && !ready && (int32_t)(now - nextTry) >= 0) {
      nextTry = now + 4000;
      ready = subscribe();
      if (ready && newBond) paired_ = true;
      Serial.printf("[celular] notificacoes: %s\n", ready ? "ok" : "ainda nao");
    }
    if ((int32_t)(now - nextRssi) >= 0 && client) {
      nextRssi = now + 2000;
      int r = client->getRssi();
      if (r < 0) rssi_ = rssi_ ? (rssi_ * 3 + r) / 4 : r;  // suavizado: o sinal oscila muito
    }
    if (awaiting && now - askedAt > 3000) {  // resposta que nao veio: desiste dessa
      awaiting = false;
      rx.clear();
    }
    if (ready && !awaiting && control) {
      Pending p;
      bool have = false;
      xSemaphoreTake(lock, portMAX_DELAY);
      if (pendingCount > 0) {
        p = pending[0];
        memmove(pending, pending + 1, --pendingCount * sizeof(Pending));
        have = true;
      }
      xSemaphoreGive(lock);
      if (have) {
        // pede: aplicativo, titulo (ate TITLE_MAX letras) e mensagem (ate TEXT_MAX)
        uint8_t ask[12] = {0, 0, 0, 0, 0, 0, 1, TITLE_MAX, 0, 3, TEXT_MAX & 0xFF, TEXT_MAX >> 8};
        memcpy(ask + 1, &p.uid, 4);
        rxCategory = p.category;
        rxUid = p.uid;
        rx.clear();
        awaiting = true;
        askedAt = now;
        if (!control->writeValue(ask, sizeof(ask), true)) awaiting = false;
      }
    }
  }
}

// Sobe o Bluetooth e comeca a se anunciar para o iPhone
static void start() {
  NimBLEDevice::init("Bichinho");
  NimBLEDevice::setSecurityAuth(true, false, true);          // guarda o pareamento; sem codigo para digitar
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  note("ligou com %d celular(es) guardado(s)", NimBLEDevice::getNumBonds());
  server = NimBLEDevice::createServer();
  server->setCallbacks(&callbacks, false);  // false: o objeto e fixo, a biblioteca nao deve apaga-lo
  server->advertiseOnDisconnect(true);

  // O anuncio "procura" o servico de notificacoes da Apple: e isso que faz o
  // iPhone listar o aparelho em Ajustes > Bluetooth e reconectar sozinho depois
  static const uint8_t SOLICIT[] = {17,   0x15, 0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F,
                                    0xA4, 0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79};
  NimBLEAdvertisementData adv, scan;
  adv.setFlags(0x06);
  adv.addData(SOLICIT, sizeof(SOLICIT));
  scan.setName("Bichinho");
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setAdvertisementData(adv);
  advertising->setScanResponseData(scan);
  advertising->start();
  running = true;
}

void begin() {
  lock = xSemaphoreCreateMutex();
  start();
  xTaskCreatePinnedToCore(task, "phone", 6144, nullptr, 1, nullptr, 0);
}

void pause() {
  if (!running) return;
  running = false;
  vTaskDelay(pdMS_TO_TICKS(250));  // deixa a tarefa terminar o que estiver fazendo
  note("Bluetooth desligado para o Wi-Fi buscar o tempo");
  NimBLEDevice::deinit(true);
  server = nullptr;
  client = nullptr;
  control = nullptr;
  connected_ = secured = ready = awaiting = false;
  rssi_ = 0;
  pendingCount = 0;
  rx.clear();
}

void resume() {
  if (!running) start();
}

bool connected() { return connected_; }
int rssi() { return rssi_; }

bool justPaired() {
  bool p = paired_;
  paired_ = false;
  return p;
}

bool takeNote(Note& out) {
  bool have = false;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (noteCount > 0) {
    out = notes[0];
    for (int i = 1; i < noteCount; i++) notes[i - 1] = notes[i];
    notes[--noteCount] = Note();
    have = true;
  }
  xSemaphoreGive(lock);
  return have;
}

bool takeRemoved(uint32_t& uid) {
  bool have = false;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (removedCount > 0) {
    uid = removed[0];
    memmove(removed, removed + 1, --removedCount * sizeof(uint32_t));
    have = true;
  }
  xSemaphoreGive(lock);
  return have;
}

bool takeExisting(uint32_t& uid) {
  bool have = false;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (existingCount > 0) {
    uid = existing[0];
    memmove(existing, existing + 1, --existingCount * sizeof(uint32_t));
    have = true;
  }
  xSemaphoreGive(lock);
  return have;
}

void forget() {
  NimBLEDevice::deleteAllBonds();
  if (server && connected_) server->disconnect(server->getPeerInfo(0));
}

void debugPrint() {
  nvs_stats_t st;
  if (nvs_get_stats(nullptr, &st) == ESP_OK)
    Serial.printf("[celular] memoria de ajustes: %u usados, %u livres de %u\n", (unsigned)st.used_entries,
                  (unsigned)st.free_entries, (unsigned)st.total_entries);
  for (int i = 0; i < diaryCount; i++) Serial.printf("[celular] . %s\n", diary[i]);
  Serial.printf("[celular] conectado=%d cifrado=%d notificacoes=%d sinal=%d pareados=%d heap=%u\n", (int)connected_,
                (int)secured, (int)ready, (int)rssi_, NimBLEDevice::getNumBonds(), ESP.getFreeHeap());
}

}  // namespace phone
