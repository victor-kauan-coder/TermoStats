/*
  Gateway ESP-NOW.

  Cada pacote recebido vai para uma fila (o callback roda na tarefa do
  WiFi e não deve imprimir nem demorar). O loop() tira da fila, devolve o
  ACK ao sensor na hora e imprime uma linha para o gravador:

    @PKT mac=.. seq=.. bytes=.. rssi=.. rtt_us=.. seq_rtt=.. tx_falhas=.. temp_c=.. umid_pct=..
*/
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "pacote.h"

struct Recebido {
  uint8_t mac[6];
  int8_t rssi;
  int len;
  PacoteDado dado;
};

static QueueHandle_t fila;
static volatile uint32_t descartadosFila = 0;

static void aoReceber(const esp_now_recv_info_t* info, const uint8_t* dados, int len) {
  if (len != sizeof(PacoteDado) || dados[0] != TIPO_DADO) return;
  Recebido r;
  memcpy(r.mac, info->src_addr, 6);
  r.rssi = info->rx_ctrl->rssi;
  r.len = len;
  memcpy(&r.dado, dados, sizeof r.dado);
  if (xQueueSend(fila, &r, 0) != pdTRUE) descartadosFila = descartadosFila + 1;
}

static void garantePeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = CANAL_ESPNOW;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void setup() {
  Serial.begin(115200);
  fila = xQueueCreate(64, sizeof(Recebido));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(CANAL_ESPNOW, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[gateway] esp_now_init falhou");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(aoReceber);

  Serial.printf("[gateway] MAC %s · canal %d · esperando o sensor\n",
                WiFi.macAddress().c_str(), CANAL_ESPNOW);
}

void loop() {
  Recebido r;
  if (xQueueReceive(fila, &r, pdMS_TO_TICKS(1000)) != pdTRUE) return;

  // ACK primeiro: tudo o que vem antes dele entra no RTT
  garantePeer(r.mac);
  PacoteAck ack = {TIPO_ACK, r.dado.seq, r.dado.t_envio_us};
  esp_now_send(r.mac, (const uint8_t*)&ack, sizeof ack);

  const PacoteDado& d = r.dado;
  Serial.printf("@PKT mac=%02X%02X%02X%02X%02X%02X seq=%u bytes=%d rssi=%d rtt_us=%u "
                "seq_rtt=%u tx_falhas=%u",
                r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5], (unsigned)d.seq,
                r.len, r.rssi, (unsigned)d.rtt_us, (unsigned)d.seq_rtt,
                (unsigned)d.tx_falhas);
  if (d.temp_c_x10 == TEMP_INVALIDA) Serial.print(" temp_c=null");
  else Serial.printf(" temp_c=%.1f", d.temp_c_x10 / 10.0);
  if (d.umid_pct_x10 == UMID_INVALIDA) Serial.print(" umid_pct=null");
  else Serial.printf(" umid_pct=%.1f", d.umid_pct_x10 / 10.0);
  Serial.printf(" descartados_fila=%u\n", (unsigned)descartadosFila);
}
