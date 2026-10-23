#include "medicao.h"

#include <WiFi.h>

#include "calculo.h"
#include "config.h"
#include "http_manual.h"
#include "payload.h"
#include "rede.h"

static const char* NOME_FASE[N_FASES] = {
  "associar + autenticar", "obter IP (DHCP)", "resolver nome (DNS)",
  "conexao (TCP ou TCP+TLS)", "requisicao HTTP", "desconectar",
};
static const char* CHAVE_FASE[N_FASES] = {"assoc", "dhcp", "dns", "conexao", "http", "desc"};

static float corrente_mA = CORRENTE_RADIO_ESTIMADA_mA;
static bool correnteMedida = false;
static uint32_t contadorSeries = 0;

static bool sucesso(int status) { return classifica(status) == ClasseStatus::Sucesso; }

TemposCiclo cicloCompleto(bool https, const char* origem, uint32_t serie, uint32_t n) {
  TemposCiclo r = {};
  r.status = -1;

  // A leitura do sensor é a linha "acorda e lê" do orçamento, não a do rádio:
  // fica fora do cronômetro.
  String corpo = montaPayload(true);

  redeDesliga();   // começa como um nó que acorda do deep sleep
  delay(50);
  uint32_t t0 = millis();

  if (redeConecta(&r.fase[FASE_ASSOC], &r.fase[FASE_DHCP])) {
    uint32_t t = millis();
    IPAddress ip;
    bool dnsOk = WiFi.hostByName(ECO_HOST, ip) == 1;
    r.fase[FASE_DNS] = millis() - t;

    if (dnsOk) {
      // a conexão usa o nome; o lwIP responde do cache, sem nova consulta
      RespostaHttp resp = enviaPost(https, corpo, false, &r.fase[FASE_CONEXAO], &r.fase[FASE_HTTP]);
      r.status = resp.status;
    } else {
      Serial.printf("  DNS falhou para %s\n", ECO_HOST);
    }

    t = millis();
    redeDesliga();
    r.fase[FASE_DESC] = millis() - t;
  }
  r.total = millis() - t0;

  // linha para o gravador
  Serial.printf("@FASES origem=%s serie=%u n=%u tipo=%s", origem, (unsigned)serie,
                (unsigned)n, https ? "https" : "http");
  for (int f = 0; f < N_FASES; f++) Serial.printf(" %s=%u", CHAVE_FASE[f], (unsigned)r.fase[f]);
  Serial.printf(" total=%u status=%d mais_cara=%s\n", (unsigned)r.total, r.status,
                CHAVE_FASE[indiceDoMaior(r.fase, N_FASES)]);
  // o corpo enviado, para virar linha do conjunto de dados
  Serial.printf("@ENVIO status=%d %s\n", r.status, corpo.c_str());
  return r;
}

void teclaCiclo() {
  Serial.println("[1] cronometro de fases - um ciclo completo em HTTP\n");
  TemposCiclo r = cicloCompleto(false, "t1", 0, 1);
  Serial.println();
  for (int f = 0; f < N_FASES; f++) {
    Serial.printf("  %-28s %6u ms", NOME_FASE[f], (unsigned)r.fase[f]);
    if (f == FASE_HTTP) Serial.printf("   -> status %d", r.status);
    Serial.println();
  }
  Serial.printf("  %-28s %6u ms\n", "  (TCP + HTTP)",
                (unsigned)(r.fase[FASE_CONEXAO] + r.fase[FASE_HTTP]));
  Serial.println("  ------------------------------------------");
  Serial.printf("  %-28s %6u ms\n\n", "CICLO COMPLETO", (unsigned)r.total);
  Serial.printf("  fase mais cara: %s\n", NOME_FASE[indiceDoMaior(r.fase, N_FASES)]);
}

static void imprimeLinhaOrcamento(const char* tipo, uint32_t maximo_ms) {
  float s = maximo_ms / 1000.0f;
  float carga = cargaDiaria_mAh(corrente_mA, s, CICLOS_POR_DIA);
  const char* origem = correnteMedida ? "bancada" : "estimativa";
  Serial.printf("\n  linha do orcamento (%s):\n", tipo);
  Serial.println("  estado                 | mA      | s (max) | ciclos/dia | mA.h/dia");
  Serial.printf("  associa WiFi e publica | %7.1f | %7.2f | %10u | %8.2f\n",
                corrente_mA, s, (unsigned)CICLOS_POR_DIA, carga);
  Serial.printf("  origem: corrente = %s, tempo = bancada da equipe (maximo de %d ciclos)\n",
                origem, CICLOS_POR_SERIE);
  Serial.printf("@ORCAMENTO tipo=%s mA=%.1f origem_mA=%s s=%.3f ciclos_dia=%u mAh_dia=%.3f\n",
                tipo, corrente_mA, origem, s, (unsigned)CICLOS_POR_DIA, carga);
}

void teclaSerie(bool https) {
  const char* tipo = https ? "https" : "http";
  uint32_t serie = ++contadorSeries;
  Serial.printf("[%c] %d ciclos em %s (serie %u). Cada ciclo comeca com WIFI_OFF.\n\n",
                https ? '6' : '5', CICLOS_POR_SERIE, https ? "HTTPS" : "HTTP", (unsigned)serie);
  Serial.println("   n  assoc   dhcp    dns  conex   http   desc |  total  status");

  uint32_t porFase[N_FASES][CICLOS_POR_SERIE];
  uint32_t totais[CICLOS_POR_SERIE];
  size_t ok = 0;

  for (int i = 0; i < CICLOS_POR_SERIE; i++) {
    TemposCiclo r = cicloCompleto(https, https ? "t6" : "t5", serie, i + 1);
    Serial.printf("  %2d", i + 1);
    for (int f = 0; f < N_FASES; f++) Serial.printf(" %6u", (unsigned)r.fase[f]);
    Serial.printf(" | %6u  %d%s\n", (unsigned)r.total, r.status,
                  sucesso(r.status) ? "" : "  (fora da estatistica)");
    if (sucesso(r.status)) {
      for (int f = 0; f < N_FASES; f++) porFase[f][ok] = r.fase[f];
      totais[ok] = r.total;
      ok++;
    }
  }

  if (ok == 0) {
    Serial.println("\n  nenhum ciclo com 2xx: sem estatistica. Confira rede e ECO_HOST.");
    return;
  }

  Resumo rt = resume(totais, ok);
  uint32_t medianas[N_FASES];
  for (int f = 0; f < N_FASES; f++) medianas[f] = resume(porFase[f], ok).mediana;
  int maisCara = (int)indiceDoMaior(medianas, N_FASES);

  Serial.printf("\n  %u de %d ciclos com 2xx\n", (unsigned)ok, CICLOS_POR_SERIE);
  Serial.printf("  minimo %u ms | mediana %u ms | maximo %u ms\n",
                (unsigned)rt.minimo, (unsigned)rt.mediana, (unsigned)rt.maximo);
  Serial.printf("  fase mais cara (pela mediana): %s\n", NOME_FASE[maisCara]);
  Serial.printf("@SERIE serie=%u tipo=%s ok=%u n=%d min=%u med=%u max=%u mais_cara=%s\n",
                (unsigned)serie, tipo, (unsigned)ok, CICLOS_POR_SERIE, (unsigned)rt.minimo,
                (unsigned)rt.mediana, (unsigned)rt.maximo, CHAVE_FASE[maisCara]);

  imprimeLinhaOrcamento(tipo, rt.maximo);
}

void teclaRajada() {
  Serial.printf("[r] rajada: ciclos HTTP sem pausa por %d s - leia o medidor USB agora\n",
                RAJADA_MS / 1000);
  uint32_t t0 = millis();
  uint32_t n = 0;
  while (millis() - t0 < RAJADA_MS) {
    TemposCiclo r = cicloCompleto(false, "r", 0, ++n);
    Serial.printf("  ciclo %u: %u ms, status %d\n", (unsigned)n, (unsigned)r.total, r.status);
  }
  Serial.printf("  fim da rajada: %u ciclos. Anote a corrente media e tecle i<mA>.\n", (unsigned)n);
}

void defineCorrente(float mA) {
  if (mA <= 0.0f || mA > 1000.0f) {
    Serial.printf("[i] valor invalido (%.1f). Exemplo: i135\n", mA);
    return;
  }
  corrente_mA = mA;
  correnteMedida = true;
  Serial.printf("[i] corrente do radio ativo: %.1f mA (origem: bancada). "
                "As teclas 5 e 6 passam a usa-la.\n", corrente_mA);
  Serial.printf("@CORRENTE mA=%.1f origem=bancada\n", corrente_mA);
}
