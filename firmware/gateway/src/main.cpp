/*
  Gateway BLE (central GATT).

  Procura o anúncio do serviço do sensor, conecta, assina as notificações e
  devolve um ACK para cada pacote. Se a conexão cair, volta a procurar.

  Cada pacote vai para uma fila (o callback roda na tarefa do Bluetooth e não
  deve imprimir nem demorar). O loop() tira da fila, devolve o ACK na hora e
  imprime uma linha para o gravador. Toda linha termina em *XX, o XOR dos
  caracteres entre '@' e '*' (como no NMEA): o gravador descarta a linha que
  a serial corromper.

    @PKT mac=.. seq=.. rx=.. bytes=.. rssi=.. rtt_us=.. seq_rtt=.. tx_falhas=.. temp_c=.. umid_pct=.. descartados_fila=..*XX
    @EVT tipo=conectado|desconectado|falha_conexao mac=.. t_ms=.. [mtu=..]*XX

  rx é o contador de pacotes recebidos pelo gateway desde o boot. A perda no
  gravador é calculada por ele, e não pelas linhas que chegaram ao notebook:
  uma linha descartada pela serial não vira perda falsa.
*/
#include <Arduino.h>
#include <BLEDevice.h>
#include <stdarg.h>

#include "pacote.h"

#define RSSI_INTERVALO_MS 1000   // getRssi() é uma consulta à pilha: uma vez por segundo basta

struct Recebido {
  uint32_t rx;
  int len;
  PacoteDado dado;
};

static QueueHandle_t fila;
static volatile uint32_t rx = 0;
static volatile uint32_t descartadosFila = 0;
static volatile bool caiu = false;

static BLEClient* cliente;
static BLEAdvertisedDevice* alvo = nullptr;
static BLERemoteCharacteristic* ackRemoto = nullptr;
static bool conectado = false;
static char mac[13] = "000000000000";
static int rssi = 0;

// Imprime "@..." com o checksum no fim.
static void linha(const char* formato, ...) {
  char buf[256];
  va_list args;
  va_start(args, formato);
  vsnprintf(buf, sizeof buf, formato, args);
  va_end(args);
  uint8_t soma = 0;
  for (const char* c = buf + 1; *c; c++) soma ^= (uint8_t)*c;
  Serial.printf("%s*%02X\n", buf, soma);
}

class AoAchar : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dispositivo) override {
    if (alvo || !dispositivo.haveServiceUUID() ||
        !dispositivo.isAdvertisingService(BLEUUID(UUID_SERVICO))) return;
    alvo = new BLEAdvertisedDevice(dispositivo);
    BLEDevice::getScan()->stop();
  }
};

class AoCair : public BLEClientCallbacks {
  void onConnect(BLEClient*) override {}
  void onDisconnect(BLEClient*) override { caiu = true; }
};

static void aoNotificar(BLERemoteCharacteristic*, uint8_t* dados, size_t len, bool) {
  if (len != sizeof(PacoteDado) || dados[0] != TIPO_DADO) return;
  Recebido r;
  rx = rx + 1;                   // só esta tarefa escreve
  r.rx = rx;
  r.len = (int)len;
  memcpy(&r.dado, dados, sizeof r.dado);
  if (xQueueSend(fila, &r, 0) != pdTRUE) descartadosFila = descartadosFila + 1;
}

static void guardaMac(BLEAddress endereco) {
  const uint8_t* b = endereco.getNative();
  snprintf(mac, sizeof mac, "%02X%02X%02X%02X%02X%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}

static void esqueceAlvo() {
  delete alvo;
  alvo = nullptr;
  ackRemoto = nullptr;
}

static void conecta() {
  guardaMac(alvo->getAddress());
  if (!cliente->connect(alvo)) {
    linha("@EVT tipo=falha_conexao mac=%s t_ms=%lu", mac, millis());
    esqueceAlvo();
    return;
  }
  cliente->setMTU(MTU_DESEJADO);
  BLERemoteService* servico = cliente->getService(UUID_SERVICO);
  BLERemoteCharacteristic* dados = servico ? servico->getCharacteristic(UUID_DADOS) : nullptr;
  ackRemoto = servico ? servico->getCharacteristic(UUID_ACK) : nullptr;
  if (!dados || !ackRemoto || !dados->canNotify()) {
    linha("@EVT tipo=falha_conexao mac=%s t_ms=%lu motivo=servico", mac, millis());
    cliente->disconnect();
    esqueceAlvo();
    return;
  }
  dados->registerForNotify(aoNotificar);
  conectado = true;
  caiu = false;
  rssi = cliente->getRssi();
  linha("@EVT tipo=conectado mac=%s t_ms=%lu mtu=%u", mac, millis(), cliente->getMTU());
}

void setup() {
  Serial.begin(115200);
  fila = xQueueCreate(64, sizeof(Recebido));

  BLEDevice::init("TermoStats-gateway");
  BLEDevice::setMTU(MTU_DESEJADO);
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  cliente = BLEDevice::createClient();
  cliente->setClientCallbacks(new AoCair());

  BLEScan* busca = BLEDevice::getScan();
  busca->setAdvertisedDeviceCallbacks(new AoAchar());
  busca->setActiveScan(true);
  busca->setInterval(100);
  busca->setWindow(99);

  Serial.printf("[gateway] BLE %s · procurando %s\n",
                BLEDevice::getAddress().toString().c_str(), NOME_SENSOR);
}

void loop() {
  static uint32_t tRssi = 0;

  if (conectado && caiu) {
    conectado = false;
    linha("@EVT tipo=desconectado mac=%s t_ms=%lu", mac, millis());
    esqueceAlvo();
  }

  if (!conectado) {
    if (!alvo) {
      BLEDevice::getScan()->start(2, false);   // bloqueia até 2 s ou até achar o sensor
      BLEDevice::getScan()->clearResults();
    }
    if (alvo) conecta();
    return;
  }

  if (millis() - tRssi >= RSSI_INTERVALO_MS) {
    tRssi = millis();
    rssi = cliente->getRssi();
  }

  Recebido r;
  if (xQueueReceive(fila, &r, pdMS_TO_TICKS(50)) != pdTRUE) return;

  // ACK primeiro: tudo o que vem antes dele entra no RTT
  PacoteAck ack = {TIPO_ACK, r.dado.seq, r.dado.t_envio_us};
  if (ackRemoto) ackRemoto->writeValue((uint8_t*)&ack, sizeof ack, false);

  const PacoteDado& d = r.dado;
  char temp[12], umid[12];
  if (d.temp_c_x10 == TEMP_INVALIDA) strcpy(temp, "null");
  else snprintf(temp, sizeof temp, "%.1f", d.temp_c_x10 / 10.0);
  if (d.umid_pct_x10 == UMID_INVALIDA) strcpy(umid, "null");
  else snprintf(umid, sizeof umid, "%.1f", d.umid_pct_x10 / 10.0);

  linha("@PKT mac=%s seq=%u rx=%u bytes=%d rssi=%d rtt_us=%u seq_rtt=%u tx_falhas=%u "
        "temp_c=%s umid_pct=%s descartados_fila=%u",
        mac, (unsigned)d.seq, (unsigned)r.rx, r.len, rssi, (unsigned)d.rtt_us,
        (unsigned)d.seq_rtt, (unsigned)d.tx_falhas, temp, umid, (unsigned)descartadosFila);
}
