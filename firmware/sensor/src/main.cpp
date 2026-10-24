/*
  Sensor ESP-NOW — DHT22 no GPIO4, um pacote a cada INTERVALO_MS.

  Descoberta do gateway: o sensor começa em broadcast. Quando chega o
  primeiro ACK, ele aprende o MAC do gateway e passa a enviar em unicast
  (com confirmação e retransmissão da camada MAC).

  Latência: o gateway devolve o t_envio_us de cada pacote. O sensor mede a
  ida e volta (RTT) e a manda no pacote seguinte, porque o gravador só
  enxerga a serial do gateway. Latência de um sentido ~ RTT / 2.
*/
#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "pacote.h"

#define DHT_PIN          4
#define DHT_TYPE         DHT22
#define DHT_INTERVALO_MS 2100    // folha de dados: nunca ler mais rápido que 2 s
#define INTERVALO_MS     100     // 10 pacotes/s

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static DHT dht(DHT_PIN, DHT_TYPE);
static uint8_t macGateway[6];
static volatile bool conheceGateway = false;

// Escritos nos callbacks (tarefa do WiFi), lidos no loop().
static portMUX_TYPE trava = portMUX_INITIALIZER_UNLOCKED;
static uint32_t ultimoRtt = 0, ultimoSeqRtt = 0;
static volatile uint32_t txFalhas = 0;

static uint32_t seq = 0;
static int16_t temp_x10 = TEMP_INVALIDA;
static uint16_t umid_x10 = UMID_INVALIDA;

static void aoEnviar(const esp_now_send_info_t*, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) txFalhas = txFalhas + 1;   // só esta tarefa escreve
}

static void aoReceber(const esp_now_recv_info_t* info, const uint8_t* dados, int len) {
  uint32_t agora = micros();
  if (len != sizeof(PacoteAck) || dados[0] != TIPO_ACK) return;
  PacoteAck ack;
  memcpy(&ack, dados, sizeof ack);

  portENTER_CRITICAL(&trava);
  ultimoRtt = agora - ack.t_envio_us;
  ultimoSeqRtt = ack.seq;
  portEXIT_CRITICAL(&trava);

  if (!conheceGateway) {
    memcpy(macGateway, info->src_addr, 6);
    conheceGateway = true;   // o loop() registra o peer: aqui é a tarefa do WiFi
  }
}

static bool adicionaPeer(const uint8_t* mac) {
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = CANAL_ESPNOW;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

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

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(CANAL_ESPNOW, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[sensor] esp_now_init falhou");
    while (true) delay(1000);
  }
  esp_now_register_send_cb(aoEnviar);
  esp_now_register_recv_cb(aoReceber);
  adicionaPeer(BROADCAST);

  Serial.printf("[sensor] MAC %s · canal %d · %d ms entre pacotes · %u bytes por pacote\n",
                WiFi.macAddress().c_str(), CANAL_ESPNOW, INTERVALO_MS,
                (unsigned)sizeof(PacoteDado));
  Serial.println("[sensor] procurando o gateway (broadcast)...");
}

void loop() {
  static bool emUnicast = false;
  static uint32_t tUltimoEnvio = 0;

  if (conheceGateway && !emUnicast) {
    emUnicast = adicionaPeer(macGateway);
    Serial.printf("[sensor] gateway %02X:%02X:%02X:%02X:%02X:%02X -> unicast\n",
                  macGateway[0], macGateway[1], macGateway[2], macGateway[3], macGateway[4],
                  macGateway[5]);
  }

  leSensor();

  if (millis() - tUltimoEnvio < INTERVALO_MS) return;
  tUltimoEnvio += INTERVALO_MS;
  if (millis() - tUltimoEnvio > INTERVALO_MS) tUltimoEnvio = millis();   // não acumula atraso

  PacoteDado p = {};
  p.tipo = TIPO_DADO;
  p.seq = seq++;
  portENTER_CRITICAL(&trava);
  p.rtt_us = ultimoRtt;
  p.seq_rtt = ultimoSeqRtt;
  portEXIT_CRITICAL(&trava);
  p.tx_falhas = txFalhas;
  p.temp_c_x10 = temp_x10;
  p.umid_pct_x10 = umid_x10;
  p.t_envio_us = micros();   // por último: o mais perto possível do envio

  esp_now_send(emUnicast ? macGateway : BROADCAST, (const uint8_t*)&p, sizeof p);

  if (p.seq % 50 == 0) {
    Serial.printf("[sensor] seq %u · rtt %u us · falhas MAC %u · %.1f C %.1f %%\n",
                  (unsigned)p.seq, (unsigned)p.rtt_us, (unsigned)p.tx_falhas,
                  temp_x10 / 10.0, umid_x10 / 10.0);
  }
}
