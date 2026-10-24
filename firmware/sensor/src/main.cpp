/*
  Sensor BLE — DHT22 no GPIO4, um pacote a cada INTERVALO_MS.

  Periférico GATT: anuncia o serviço, aceita a conexão do gateway e envia
  cada pacote por notify. O gateway devolve um ACK escrevendo na
  característica UUID_ACK; o sensor mede a ida e volta (RTT) e a manda no
  pacote seguinte, como no teste de ESP-NOW.

  Na conexão BLE a camada de enlace retransmite até conseguir: o BLE não
  perde pacotes avulsos, ele perde a CONEXÃO. Por isso o seq avança mesmo
  desconectado — os pacotes desse intervalo viram o buraco que o gravador
  conta como perda, e tx_falhas registra quantos não puderam sair.
*/
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <DHT.h>

#include "pacote.h"

#define DHT_PIN          4
#define DHT_TYPE         DHT22
#define DHT_INTERVALO_MS 2100    // folha de dados: nunca ler mais rápido que 2 s
#define INTERVALO_MS     100     // 10 pacotes/s, o mesmo do teste de ESP-NOW

static DHT dht(DHT_PIN, DHT_TYPE);
static BLECharacteristic* dados;
static volatile bool conectado = false;
static volatile bool reanunciar = false;

// Escritos nos callbacks (tarefa do Bluetooth), lidos no loop().
static portMUX_TYPE trava = portMUX_INITIALIZER_UNLOCKED;
static uint32_t ultimoRtt = 0, ultimoSeqRtt = 0;
static volatile uint32_t txFalhas = 0;

static uint32_t seq = 0;
static int16_t temp_x10 = TEMP_INVALIDA;
static uint16_t umid_x10 = UMID_INVALIDA;

class AoConectar : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { conectado = true; }
  void onDisconnect(BLEServer*) override {
    conectado = false;
    reanunciar = true;   // o loop() volta a anunciar: aqui é a tarefa do Bluetooth
  }
};

class AoReceberAck : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    uint32_t agora = micros();
    if (c->getLength() != sizeof(PacoteAck)) return;
    PacoteAck ack;
    memcpy(&ack, c->getData(), sizeof ack);
    if (ack.tipo != TIPO_ACK) return;
    portENTER_CRITICAL(&trava);
    ultimoRtt = agora - ack.t_envio_us;
    ultimoSeqRtt = ack.seq;
    portEXIT_CRITICAL(&trava);
  }
};

class AoNotificar : public BLECharacteristicCallbacks {
  void onStatus(BLECharacteristic*, Status s, uint32_t) override {
    if (s != SUCCESS_NOTIFY && s != SUCCESS_INDICATE) txFalhas = txFalhas + 1;
  }
};

static void leSensor() {
  static uint32_t tUltima = 0;
  if (tUltima != 0 && millis() - tUltima < DHT_INTERVALO_MS) return;
  tUltima = millis();
  float t = dht.readTemperature();
  float u = dht.readHumidity();
  temp_x10 = isnan(t) ? TEMP_INVALIDA : (int16_t)lroundf(t * 10);
  umid_x10 = isnan(u) ? UMID_INVALIDA : (uint16_t)lroundf(u * 10);
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  BLEDevice::init(NOME_SENSOR);
  BLEDevice::setMTU(MTU_DESEJADO);
  BLEDevice::setPower(ESP_PWR_LVL_P9);                           // +9 dBm: o máximo do BLE no ESP32
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);

  BLEServer* servidor = BLEDevice::createServer();
  servidor->setCallbacks(new AoConectar());
  BLEService* servico = servidor->createService(UUID_SERVICO);

  dados = servico->createCharacteristic(UUID_DADOS, BLECharacteristic::PROPERTY_NOTIFY);
  dados->addDescriptor(new BLE2902());
  dados->setCallbacks(new AoNotificar());

  BLECharacteristic* ack = servico->createCharacteristic(
      UUID_ACK, BLECharacteristic::PROPERTY_WRITE_NR);
  ack->setCallbacks(new AoReceberAck());

  servico->start();
  BLEAdvertising* anuncio = BLEDevice::getAdvertising();
  anuncio->addServiceUUID(UUID_SERVICO);
  anuncio->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.printf("[sensor] BLE %s · %s · %d ms entre pacotes · %u bytes por pacote\n",
                BLEDevice::getAddress().toString().c_str(), NOME_SENSOR, INTERVALO_MS,
                (unsigned)sizeof(PacoteDado));
  Serial.println("[sensor] anunciando, esperando o gateway...");
}

void loop() {
  static uint32_t tUltimoEnvio = 0;
  static bool estavaConectado = false;

  if (reanunciar) {
    reanunciar = false;
    delay(50);                    // a pilha termina de fechar a conexão antes
    BLEDevice::startAdvertising();
  }
  if (conectado != estavaConectado) {
    estavaConectado = conectado;
    Serial.printf("[sensor] %s (seq %u)\n", estavaConectado ? "gateway conectado" :
                  "conexao perdida, anunciando de novo", (unsigned)seq);
  }

  leSensor();

  if (millis() - tUltimoEnvio < INTERVALO_MS) return;
  tUltimoEnvio += INTERVALO_MS;
  if (millis() - tUltimoEnvio > INTERVALO_MS) tUltimoEnvio = millis();   // não acumula atraso

  PacoteDado p = {};
  p.tipo = TIPO_DADO;
  p.seq = seq++;                  // avança sempre: sem conexão, este seq vira perda

  if (!conectado) {
    txFalhas = txFalhas + 1;
    return;
  }

  portENTER_CRITICAL(&trava);
  p.rtt_us = ultimoRtt;
  p.seq_rtt = ultimoSeqRtt;
  portEXIT_CRITICAL(&trava);
  p.tx_falhas = txFalhas;
  p.temp_c_x10 = temp_x10;
  p.umid_pct_x10 = umid_x10;
  p.t_envio_us = micros();        // por último: o mais perto possível do envio

  dados->setValue((uint8_t*)&p, sizeof p);
  dados->notify();

  if (p.seq % 50 == 0) {
    Serial.printf("[sensor] seq %u · rtt %u us · falhas %u · %.1f C %.1f %%\n",
                  (unsigned)p.seq, (unsigned)p.rtt_us, (unsigned)p.tx_falhas,
                  temp_x10 / 10.0, umid_x10 / 10.0);
  }
}
