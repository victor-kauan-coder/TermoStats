/* =======================================================================
   Laboratorio 03 - telemetria dos sensores do projeto via MQTT
   30/09/2026 · Topicos em Sistemas Computacionais I (IoT/AIoT) · UFPI 2026.2

   Um no MQTT com o minimo necessario, e nada mais:
     1. entra no WiFi e acerta o relogio (NTP)
     2. conecta ao broker com TLS e deixa um TESTAMENTO: "offline" (retido)
     3. publica "online" (retido) no topico de status
     4. ASSINA o proprio topico de config (o canal de volta)
     5. publica a telemetria (o JSON do contrato de dados) a cada periodo_s

   Topicos deste no:
     ufpi-iot26/<EQUIPE>/<NO>/telemetria   o no publica   QoS 1, nao retido
     ufpi-iot26/<EQUIPE>/<NO>/status       o no publica   QoS 1, RETIDO (e testamento)
     ufpi-iot26/<EQUIPE>/<NO>/config       o no ASSINA    ex.: {"periodo_s":2}

   O QUE A EQUIPE EDITA:
     - secrets.h ....... rede WiFi e usuario/senha da EQUIPE no broker
     - EQUIPE e NO ..... quem e este no (vira topico e client ID)
     - leSensores() .... os sensores do projeto (AP1)

   Monitor serial em 115200.
   ======================================================================= */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTT.h>             // biblioteca "MQTT", de Joel Gaehwiler (arduino-mqtt)
#include <DHTesp.h>           // biblioteca "DHT sensor library for ESPx" (beegee_tokyo)
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"          // WIFI_*, MQTT_HOST, MQTT_PORTA, MQTT_USUARIO, MQTT_SENHA

// ------------------------------------------------------------ EDITE AQUI
const char *EQUIPE = "eq01";          // IGUAL ao usuario da equipe no broker
const char *NO     = "no02";           // placa: no02 · Wokwi: no01 (nunca os dois iguais)

// ------------------------------------------------------------ MQTT
WiFiClientSecure net;
MQTTClient mqtt(512);                  // 512 bytes: cabe o payload e a config

char idNo[32], clientId[48];
char tTele[64], tStatus[64], tConfig[64];

uint32_t periodo_s = 5;                // a config pode mudar isto em tempo real
uint32_t seq = 0;                      // numero da leitura: denuncia perdas e repeticoes

// ======================================================================
//  OS SENSORES DO PROJETO - aqui entra o codigo da AP1
//  - uma chave por grandeza, com a unidade no nome: "temp_c", "umid_pct"
//  - nada de delay() longo aqui dentro
//  - sensor analogico com WiFi ligado: so GPIO 32 a 39 (ADC1)
// ======================================================================
const uint8_t PINO_DHT = 4;           // pino de dados do DHT22 (troquem se ligarem em outro)
DHTesp dht;

void iniciaSensores() {
  dht.setup(PINO_DHT, DHTesp::DHT22);
}

void leSensores(JsonDocument &doc) {
  TempAndHumidity th = dht.getTempAndHumidity();   // o DHT22 so mede de novo apos ~2 s
  if (dht.getStatus() != DHTesp::ERROR_NONE || isnan(th.temperature) || isnan(th.humidity)) {
    doc["rssi_dbm"] = WiFi.RSSI();     // sem leitura valida: um valor do proprio no
    return;
  }
  doc["temp_c"]   = serialized(String(th.temperature, 1));
  doc["umid_pct"] = serialized(String(th.humidity, 1));
}

// ======================================================================
//  PUBLICAR: o contrato de dados de 23/09 (id, seq, ts e as grandezas)
// ======================================================================
void publicaTelemetria() {
  JsonDocument doc;
  doc["id"]  = idNo;
  doc["seq"] = ++seq;
  if (time(nullptr) > 1700000000) doc["ts"] = (uint32_t)time(nullptr);  // hora da MEDICAO
  else                            doc["ts"] = nullptr;                   // relogio ainda nao acertou
  leSensores(doc);

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));
  bool ok = mqtt.publish(tTele, buf, false, 1);       // topico, payload, retido?, QoS
  Serial.printf("[pub] %s  %s  %s\n", tTele, buf, ok ? "" : "<- FALHOU");
}

// ======================================================================
//  RECEBER: chamado pela biblioteca a cada mensagem num topico assinado
// ======================================================================
void aoReceber(String &topico, String &carga) {
  Serial.printf("[sub] %s  %s\n", topico.c_str(), carga.length() ? carga.c_str() : "(vazia)");
  if (carga.length() == 0) { periodo_s = 5; return; }   // retido apagado: volta ao padrao
  JsonDocument cfg;
  if (deserializeJson(cfg, carga)) { Serial.println("      JSON invalido: ignorado"); return; }
  if (cfg["periodo_s"].is<int>()) {
    periodo_s = constrain(cfg["periodo_s"].as<int>(), 1, 3600);
    Serial.printf("      periodo_s = %lu\n", (unsigned long)periodo_s);
  }
}

// ======================================================================
//  CONECTAR ao broker: testamento, status e assinatura
// ======================================================================
void conectaBroker() {
  mqtt.setWill(tStatus, "offline", true, 1);   // se eu cair sem avisar, o broker publica isto
  mqtt.setCleanSession(false);                 // sessao persistente
  mqtt.setKeepAlive(30);                       // sem noticias por 45 s = o broker me da por morto
  Serial.printf("[mqtt] conectando como %s ...\n", clientId);
  if (!mqtt.connect(clientId, MQTT_USUARIO, MQTT_SENHA)) {
    Serial.printf("[mqtt] falhou (erro %d, retorno %d): confira secrets.h e EQUIPE\n",
                  (int)mqtt.lastError(), (int)mqtt.returnCode());
    return;
  }
  Serial.println("[mqtt] conectado");
  mqtt.publish(tStatus, "online", true, 1);    // retido: quem chegar depois ve o estado
  mqtt.subscribe(tConfig, 1);                  // o canal de volta
  Serial.printf("[mqtt] status = online (retido) · assinando %s\n", tConfig);
}

// ======================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  iniciaSensores();

  snprintf(idNo,     sizeof(idNo),     "%s-%s", EQUIPE, NO);
  snprintf(clientId, sizeof(clientId), "ufpi-iot26-%s-%s", EQUIPE, NO);
  snprintf(tTele,    sizeof(tTele),    "ufpi-iot26/%s/%s/telemetria", EQUIPE, NO);
  snprintf(tStatus,  sizeof(tStatus),  "ufpi-iot26/%s/%s/status", EQUIPE, NO);
  snprintf(tConfig,  sizeof(tConfig),  "ufpi-iot26/%s/%s/config", EQUIPE, NO);

  Serial.printf("\n[wifi] entrando em \"%s\"...\n", WIFI_SSID);
  if (WIFI_CANAL > 0) WiFi.begin(WIFI_SSID, WIFI_SENHA, WIFI_CANAL);
  else                WiFi.begin(WIFI_SSID, WIFI_SENHA);
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.google.com");   // acerta o relogio quando houver rede

  net.setInsecure();                   // atalho de sala: cifra, mas nao confere o servidor (30/11)
  mqtt.begin(MQTT_HOST, MQTT_PORTA, net);
  mqtt.onMessage(aoReceber);
}

void loop() {
  static uint32_t ultimaTentativa = 0, ultimaLeitura = 0;
  static bool avisouWiFi = false;

  if (WiFi.status() != WL_CONNECTED) {             // sem rede: espera (o ESP32 reconecta sozinho)
    avisouWiFi = false;
    return;
  }
  if (!avisouWiFi) { Serial.printf("[wifi] conectado · IP %s\n", WiFi.localIP().toString().c_str()); avisouWiFi = true; }

  if (!mqtt.connected() && millis() - ultimaTentativa > 5000) {   // sem broker: tenta a cada 5 s
    ultimaTentativa = millis();
    conectaBroker();
  }
  mqtt.loop();                                     // entrega o que chegou e mantem a conexao viva

  if (mqtt.connected() && millis() - ultimaLeitura >= periodo_s * 1000UL) {
    ultimaLeitura = millis();
    publicaTelemetria();
  }
}
