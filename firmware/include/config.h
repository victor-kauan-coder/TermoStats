#pragma once
// Configuração do nó. A equipe edita: ID_EQUIPE, ECO_HOST (plano B) e,
// depois da bancada, nada mais — a corrente medida entra pela tecla i<mA>.

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Falta include/secrets.h: copie include/secrets.example.h para include/secrets.h e preencha."
#endif

// ---------------------------------------------------------------------------
// Identidade e servidor de eco
// ---------------------------------------------------------------------------
#define ID_EQUIPE    "termo-no01"     // campo "id" do contrato de dados
#define ECO_HOST     "httpbin.org"    // plano B: "postman-echo.com"
#define ECO_CAMINHO  "/post"
#define USER_AGENT   "ESP32-TermoStats"

// ---------------------------------------------------------------------------
// Hardware (mesmo mapa de pinos da AP1)
// ---------------------------------------------------------------------------
#define DHT_PIN            4      // GPIO4 — uso digital, não sofre com o ADC2
#define DHT_TYPE           DHT22
#define DHT_INTERVALO_MS   2100   // folha de dados: nunca ler mais rápido que 2 s
#define LED_STATUS_PIN     25     // batimento: pisca enquanto o laço está livre

// ---------------------------------------------------------------------------
// Medição
// ---------------------------------------------------------------------------
#define TIMEOUT_CONEXAO_MS   15000   // desiste da associação+DHCP (slide 10)
#define TIMEOUT_RESPOSTA_MS  10000
#define CICLOS_POR_SERIE     10      // teclas 5 e 6
#define RAJADA_MS            30000   // tecla r: tempo para o medidor USB estabilizar
#define MAX_TENTATIVAS       3       // tecla e: 503 retenta com 1 s, 2 s

// ---------------------------------------------------------------------------
// Orçamento de energia (Apêndice B)
// ---------------------------------------------------------------------------
#define CICLOS_POR_DIA              144      // RF-01: uma leitura a cada 10 min
#define CORRENTE_RADIO_ESTIMADA_mA  120.0f   // origem: estimativa de 09/09

// ---------------------------------------------------------------------------
// Relógio
// ---------------------------------------------------------------------------
#define NTP_SERVIDOR   "pool.ntp.org"
#define FUSO_S         (-3 * 3600)    // Teresina, UTC-3 (só para exibir; ts é UTC)
#define EPOCH_VALIDO   1700000000UL   // abaixo disso o relógio ainda acha que é 1970
