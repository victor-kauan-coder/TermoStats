#include "rede.h"

#include <WiFi.h>

#include "config.h"

// Escritos pela tarefa de eventos do WiFi, lidos pelo laço principal.
static volatile uint32_t tConectado = 0;
static volatile uint32_t tGotIp = 0;

static void aoEventoWifi(arduino_event_id_t evento, arduino_event_info_t) {
  if (evento == ARDUINO_EVENT_WIFI_STA_CONNECTED) tConectado = millis();
  if (evento == ARDUINO_EVENT_WIFI_STA_GOT_IP) tGotIp = millis();
}

void redeInicia() {
  WiFi.persistent(false);   // não grava credenciais na flash a cada ciclo
  WiFi.onEvent(aoEventoWifi);
  WiFi.mode(WIFI_OFF);
}

bool redeConecta(uint32_t* tAssoc, uint32_t* tDhcp) {
  tConectado = 0;
  tGotIp = 0;
  uint32_t t0 = millis();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);   // quem decide reconectar é o firmware
  WiFi.begin(WIFI_SSID, WIFI_SENHA, WIFI_CANAL);

  while (tGotIp == 0 && millis() - t0 < TIMEOUT_CONEXAO_MS) delay(2);

  if (tGotIp == 0) {
    Serial.printf("  nao conectou em %u s (status %d). A rede e 2,4 GHz? "
                  "SSID exato? WIFI_CANAL em 0?\n",
                  TIMEOUT_CONEXAO_MS / 1000, (int)WiFi.status());
    redeDesliga();
    return false;
  }
  if (tAssoc) *tAssoc = tConectado - t0;
  if (tDhcp) *tDhcp = tGotIp - tConectado;
  return true;
}

bool redeGaranteConectada() {
  if (WiFi.isConnected()) return true;
  Serial.println("  conectando...");
  redeDesliga();
  return redeConecta(nullptr, nullptr);
}

void redeDesliga() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
