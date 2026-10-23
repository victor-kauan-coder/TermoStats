/*
  TermoStats — Aula 10 (23/09): WiFi, HTTP/REST e JSON no nó DHT22.

  Ferramenta de medição para a prática guiada e para o Apêndice B
  (orçamento de energia v2). Monitor serial em 115200; tecle m para o menu.

  Cada resultado sai duas vezes: em tabela, para ler, e numa linha que
  começa com "@", para o gravador (pasta gravador/) montar as tabelas.

  As teclas bloqueiam o laço enquanto rodam (o LED de status para). Isso é
  aceitável numa ferramenta de medição; no firmware do projeto a rede vira
  uma tarefa não bloqueante (slide 10).
*/
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

#include "calculo.h"
#include "config.h"
#include "http_manual.h"
#include "medicao.h"
#include "payload.h"
#include "politica.h"
#include "rede.h"
#include "relogio.h"
#include "sensor.h"

static void imprimeMenu() {
  float t, u;
  Serial.println();
  if (sensorLe(t, u)) {
    Serial.printf("sensor: DHT22 no GPIO%d respondeu (%.1f C, %.1f %%UR)\n", DHT_PIN, t, u);
  } else {
    Serial.printf("sensor: DHT22 no GPIO%d NAO respondeu - temp_c e umid_pct irao como null\n",
                  DHT_PIN);
  }
  Serial.printf("no: %s | eco: %s | ciclos/dia no orcamento: %d\n", ID_EQUIPE, ECO_HOST,
                CICLOS_POR_DIA);
  Serial.println("  1  um ciclo completo com o tempo de cada fase (E1)");
  Serial.println("  p  payload atual e tamanho em bytes (E2)");
  Serial.println("  n  sincroniza o relogio por NTP (E2)");
  Serial.println("  2  POST escrito a mao, com os bytes de cada linha (E3)");
  Serial.println("  3  o mesmo POST com uma virgula a mais no JSON (E3)");
  Serial.println("  e  forca 404 (nao retenta) e 503 (retenta) (E3)");
  Serial.println("  i  corrente do radio ativo medida na bancada: i135 = 135 mA (E4)");
  Serial.println("  5  dez ciclos em HTTP + linha do orcamento (E4)");
  Serial.println("  6  dez ciclos em HTTPS + linha do orcamento (E4)");
  Serial.println("  r  rajada de 30 s para o medidor USB (bancada)");
  Serial.println("  m  este menu");
}

static void teclaPayload() {
  String json = montaPayload(false);
  Serial.printf("[p] payload atual (%u bytes):\n  %s\n", (unsigned)json.length(), json.c_str());
  if (!relogioSincronizado()) Serial.println("  ts = null: tecle n para sincronizar o relogio");
  Serial.printf("@PAYLOAD bytes=%u %s\n", (unsigned)json.length(), json.c_str());
}

static void teclaNtp() {
  Serial.println("[n] antes do NTP:");
  Serial.printf("  time() = %ld", (long)time(nullptr));
  Serial.println(relogioSincronizado() ? "" : "  -> o no NAO sabe que horas sao (1970)");
  long antes = (long)time(nullptr);

  if (!redeGaranteConectada()) return;
  Serial.printf("  configTime(%d, 0, \"%s\");\n", FUSO_S, NTP_SERVIDOR);
  if (!relogioSincroniza(10000)) {
    Serial.println("  NTP nao respondeu em 10 s");
    return;
  }
  time_t agora = time(nullptr);
  struct tm local;
  localtime_r(&agora, &local);
  char quando[32];
  strftime(quando, sizeof quando, "%d/%m/%Y %H:%M:%S", &local);
  Serial.printf("  depois do NTP: %s (Teresina)\n", quando);
  Serial.printf("  time() = %ld  <- vai no campo \"ts\"\n", (long)agora);
  Serial.printf("@NTP antes=%ld depois=%ld\n", antes, (long)agora);
}

// Compara campo a campo: o eco do servidor pode reordenar as chaves.
static bool mesmoConteudo(JsonObjectConst enviado, JsonObjectConst entendido) {
  if (enviado.size() != entendido.size()) return false;
  for (JsonPairConst campo : enviado) {
    JsonVariantConst outro = entendido[campo.key()];
    String a, b;
    serializeJson(campo.value(), a);
    serializeJson(outro, b);
    if (a != b) return false;
  }
  return true;
}

static void teclaPost(bool quebrado) {
  Serial.printf("[%c] POST %s%s escrito a mao%s\n", quebrado ? '3' : '2', ECO_HOST, ECO_CAMINHO,
                quebrado ? ", com uma virgula a mais" : "");
  if (!redeGaranteConectada()) return;

  String corpo = montaPayload(true);
  if (quebrado) corpo = comVirgulaAMais(corpo.c_str()).c_str();

  RespostaHttp r = enviaPost(false, corpo, true, nullptr, nullptr);
  if (r.status < 0) {
    Serial.println("  sem resposta do servidor");
    Serial.printf("@ENVIO status=%d %s\n", r.status, corpo.c_str());
    return;
  }

  JsonDocument resposta;
  bool respostaOk = !deserializeJson(resposta, r.corpo);
  JsonVariantConst entendido = resposta["json"];
  JsonDocument enviado;
  bool enviadoValido = !deserializeJson(enviado, corpo);

  Serial.printf("  o servidor recebeu (\"data\"):  %s\n", resposta["data"] | "(sem campo data)");
  Serial.print("  o servidor ENTENDEU (\"json\"): ");
  if (respostaOk) serializeJson(entendido, Serial);
  else Serial.print("(resposta nao e JSON)");
  Serial.println();

  bool igual = respostaOk && enviadoValido && !entendido.isNull() &&
               mesmoConteudo(enviado.as<JsonObjectConst>(), entendido.as<JsonObjectConst>());
  Serial.printf("  o campo json e igual ao enviado? %s\n", igual ? "sim" : "NAO");
  if (quebrado && classifica(r.status) == ClasseStatus::Sucesso && entendido.isNull()) {
    Serial.println("  O HTTP transportou; ninguem entendeu. Codigo de sucesso nao valida o dado.");
  }
  Serial.printf("@POST quebrado=%d cabecalho=%u corpo=%u status=%d json_igual=%d\n", quebrado,
                (unsigned)r.bytesCabecalho, (unsigned)r.bytesCorpo, r.status, igual);
  Serial.printf("@ENVIO status=%d %s\n", r.status, corpo.c_str());
}

static void teclaErros() {
  Serial.println("[e] 404 (erro do no) e 503 (erro do caminho), via postComPolitica()");
  if (!redeGaranteConectada()) return;
  String corpo = montaPayload(false);
  Serial.println(" 404:");
  postComPolitica("http://" ECO_HOST "/status/404", corpo);
  Serial.println(" 503:");
  postComPolitica("http://" ECO_HOST "/status/503", corpo);
  Serial.println("  o laco voltou: nada travou.");
}

static void teclaCorrente() {
  String resto = Serial.readStringUntil('\n');   // "i135" -> "135"
  defineCorrente(resto.toFloat());
}

static void batimento() {
  static uint32_t tUltimo = 0;
  if (millis() - tUltimo >= 500) {
    tUltimo = millis();
    digitalWrite(LED_STATUS_PIN, !digitalRead(LED_STATUS_PIN));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(5000);
  pinMode(LED_STATUS_PIN, OUTPUT);
  sensorInicia();
  redeInicia();
  delay(DHT_INTERVALO_MS);   // o DHT22 precisa de ~2 s depois de energizar
  imprimeMenu();
}

void loop() {
  batimento();
  if (!Serial.available()) return;

  char tecla = (char)Serial.read();
  switch (tecla) {
    case '1': teclaCiclo(); break;
    case '2': teclaPost(false); break;
    case '3': teclaPost(true); break;
    case '5': teclaSerie(false); break;
    case '6': teclaSerie(true); break;
    case 'e': teclaErros(); break;
    case 'n': teclaNtp(); break;
    case 'p': teclaPayload(); break;
    case 'r': teclaRajada(); break;
    case 'i': teclaCorrente(); break;
    case 'm': imprimeMenu(); break;
    case '\r': case '\n': case ' ': return;
    default: Serial.printf("tecla '%c' desconhecida; m para o menu\n", tecla); return;
  }
  Serial.println("\n(m para o menu)");
}
