/*
  ============================================================================
  Nó IoT de Temperatura e Umidade — ESP32 + DHT22
  ----------------------------------------------------------------------------
  Disciplina: Tópicos em Sistemas Computacionais I — UFPI
  Construído sobre os conceitos de:
    Aula 03 — eletrônica de prototipagem, limites do GPIO, divisor resistivo
    Aula 05 — ruído, filtros (mediana/média) e o erro que média nenhuma corrige
    Aula 06 — barramentos: o DHT22 já traz o conversor A/D embutido
    Aula 08 — atuadores e energia: deep sleep, memória RTC, orçamento em mA·h
  ============================================================================

  O QUE ESTE FIRMWARE FAZ
  - Lê temperatura e umidade de um DHT22 (sensor digital de fábrica —
    Aula 06: "o conversor A/D agora é do sensor". Não há sinal bruto para
    calibrar; há apenas a incerteza declarada na folha de dados).
  - Faz N leituras por ciclo e aplica um filtro de MEDIANA para descartar
    outliers de comunicação (erro de timing, ruído no fio de dado).
  - Aplica uma MÉDIA EXPONENCIAL (EMA) entre ciclos de sono, guardada em
    memória RTC, para suavizar a variação natural da grandeza física
    sem herdar o atraso de uma média longa (Aula 05: "cada filtro, seu
    preço em latência").
  - Declara a origem de cada valor de corrente usado no cálculo de energia
    (datasheet / bancada / estimativa), como pede o orçamento da Aula 08.
  - Dorme em deep sleep entre leituras. Como o loop() nunca é retomado
    nesse modo, todo o programa mora no setup() (Aula 08, Demonstração 03).
  - Lê opcionalmente a tensão da bateria por um divisor resistivo, seguindo
    a fórmula e o cuidado descritos na Aula 03.

  HARDWARE
  - ESP32 (referência da disciplina: WeMos D1 R32)
  - DHT22 / AM2302, ALIMENTADO EM 3V3 (ver README.md, seção 2 — "Por que
    3V3 e não 5V")
  - Resistor de pull-up de 10 kΩ entre VCC (3V3) e o pino de dado do DHT22
  - (Opcional) Divisor resistivo 100 kΩ / 100 kΩ da bateria até GPIO34,
    para monitorar a tensão da fonte (ver README.md, seção 2.3)

  DEPENDÊNCIAS (Arduino IDE → Gerenciador de Bibliotecas)
  - "DHT sensor library" (Adafruit)
  - "Adafruit Unified Sensor"

  MAPA DE PINOS USADO NESTE SKETCH (compatível com a tabela da Aula 03)
  - GPIO4  (rótulo A1) — dado do DHT22 (uso digital; não entra em conflito
    com leitura analógica porque não usamos A1 como ADC neste projeto)
  - GPIO25 (rótulo D3) — LED de status de bancada (evita os pinos de
    strapping 0, 2, 5, 12, 15 citados na Aula 03)
  - GPIO34 (rótulo A2) — leitura de VBAT (ADC1, só entrada, confiável com
    o WiFi ligado, conforme a Aula 03)
  ============================================================================
*/

#include <DHT.h>
#include <esp_sleep.h>

// ---------------------------------------------------------------------------
// 1) CONFIGURAÇÃO DE HARDWARE — ver README.md, seção "Mapa de pinos"
// ---------------------------------------------------------------------------
#define DHT_PIN         4        // GPIO4 — dado do DHT22, digital
#define DHT_TYPE        DHT22
#define LED_STATUS_PIN  25       // GPIO25 — só para diagnóstico em bancada
#define VBAT_ADC_PIN    34       // GPIO34 — ADC1, só entrada

// Divisor resistivo para VBAT (Aula 03, Unidade 1):
//   Vout = Vin * R2 / (R1 + R2)   =>   Vin = Vout * (R1 + R2) / R2
#define VBAT_R1_OHMS    100000.0f   // do positivo da bateria até o pino
#define VBAT_R2_OHMS    100000.0f   // do pino até o GND
#define VBAT_ADC_MAXV   3.1f        // faixa útil do ADC do ESP32 (atenuação padrão)
#define VBAT_ADC_MAXRAW 4095.0f     // ADC de 12 bits: 0 a 4095

// ---------------------------------------------------------------------------
// 2) FILTRAGEM (Aula 05)
//    - Mediana: robusta a outlier isolado (um pulso mal decodificado).
//    - EMA entre ciclos: suaviza sem herdar o atraso de uma janela longa.
// ---------------------------------------------------------------------------
#define N_AMOSTRAS            5      // ímpar, para mediana bem definida
#define EMA_ALPHA             0.30f  // peso da amostra nova (0 a 1)
#define DHT_MIN_INTERVAL_MS   2100   // datasheet: nunca ler mais rápido que 2 s

// ---------------------------------------------------------------------------
// 3) ENERGIA (Aula 08) — perfil de corrente por estado.
//    Valores marcados como estimativa/datasheet DEVEM ser confirmados com
//    multímetro na bancada antes de fechar o orçamento real do projeto.
//    Ver README.md, seção 5.
// ---------------------------------------------------------------------------
#define TEMPO_ENTRE_LEITURAS_S    (15UL * 60UL)  // acorda a cada 15 min — ajuste ao projeto
#define I_ACORDADO_SEM_RADIO_mA   45.0f    // origem: folha de dados do ESP32
#define I_DHT22_ATIVO_mA          1.5f     // origem: folha de dados do DHT22
#define I_DEEP_SLEEP_PLACA_mA     2.0f     // origem: bancada (placa de desenvolvimento, não o chip)
#define T_ACORDADO_S              12.0f    // origem: bancada (N_AMOSTRAS leituras + overhead)

// ---------------------------------------------------------------------------
// 4) MEMÓRIA RTC — sobrevive ao deep sleep (Aula 08, Demonstração 03).
//    Qualquer variável em RAM comum volta ao valor inicial a cada despertar;
//    o que precisa atravessar o sono tem de estar aqui.
// ---------------------------------------------------------------------------
RTC_DATA_ATTR uint32_t bootCount          = 0;
RTC_DATA_ATTR float    emaTemp            = NAN;
RTC_DATA_ATTR float    emaHum             = NAN;
RTC_DATA_ATTR bool     emaInicializada    = false;
RTC_DATA_ATTR uint32_t falhasConsecutivas = 0;

DHT dht(DHT_PIN, DHT_TYPE);

// ---------------------------------------------------------------------------
// Protótipos
// ---------------------------------------------------------------------------
bool  lerDHTFiltrado(float &temperaturaC, float &umidadeRH);
void  ordenar(float amostras[], int n);
float mediana(float amostras[], int n);
float lerTensaoBateria();
void  imprimirOrcamentoEnergia();
void  dormir();

// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(50); // só para o monitor serial conectar; não é hábito de produção
  bootCount++;

  Serial.println();
  Serial.printf("== No DHT22 - despertar #%lu ==\n", (unsigned long)bootCount);
  Serial.printf("Motivo do despertar: %d\n", esp_sleep_get_wakeup_cause());

  pinMode(LED_STATUS_PIN, OUTPUT);
  digitalWrite(LED_STATUS_PIN, HIGH); // aceso = no acordado, referencia de bancada

  dht.begin();

  float temperaturaC, umidadeRH;
  bool leituraOk = lerDHTFiltrado(temperaturaC, umidadeRH);

  if (leituraOk) {
    falhasConsecutivas = 0;

    // Atualiza a média exponencial entre ciclos de sono (Aula 05).
    if (!emaInicializada) {
      emaTemp = temperaturaC;
      emaHum  = umidadeRH;
      emaInicializada = true;
    } else {
      emaTemp = EMA_ALPHA * temperaturaC + (1.0f - EMA_ALPHA) * emaTemp;
      emaHum  = EMA_ALPHA * umidadeRH  + (1.0f - EMA_ALPHA) * emaHum;
    }

    float vbat = lerTensaoBateria();

    Serial.println("---- Leitura ----");
    Serial.printf("Temperatura (mediana da amostra): %.2f C\n", temperaturaC);
    Serial.printf("Umidade     (mediana da amostra): %.2f %%RH\n", umidadeRH);
    Serial.printf("Temperatura (media exponencial):  %.2f C\n", emaTemp);
    Serial.printf("Umidade     (media exponencial):  %.2f %%RH\n", emaHum);
    Serial.println("Incerteza declarada (datasheet DHT22): +-0.5 C / +-2 a 5% RH");
    if (!isnan(vbat)) {
      Serial.printf("Tensao da bateria: %.2f V\n", vbat);
    }

    // Ponto de extensão do projeto: publicar via WiFi/MQTT/HTTP ou gravar em
    // cartão SD (barramento SPI, Aula 06) ANTES de dormir. Deixado fora deste
    // firmware-base para não acoplar a um backend específico.

  } else {
    falhasConsecutivas++;
    Serial.printf("Falha de leitura do DHT22. Falhas consecutivas: %lu\n",
                  (unsigned long)falhasConsecutivas);
    Serial.println("Causas mais comuns: pull-up ausente, fio solto, intervalo");
    Serial.println("menor que 2 s entre leituras, ou GND nao comum com a fonte.");
  }

  if (bootCount == 1) {
    imprimirOrcamentoEnergia();
  }

  digitalWrite(LED_STATUS_PIN, LOW);
  dormir();
}

void loop() {
  // Em deep sleep o programa nunca "continua": ele recomeça do setup().
  // Este loop() nunca executa (Aula 08, Demonstração 03).
}

// =============================================================================
// Leitura filtrada: N amostras, mediana como filtro robusto a outlier de
// comunicação (Aula 05: "cada filtro com seu preço em latência").
// =============================================================================
bool lerDHTFiltrado(float &temperaturaC, float &umidadeRH) {
  float amostrasTemp[N_AMOSTRAS];
  float amostrasHum[N_AMOSTRAS];
  int validas = 0;

  for (int i = 0; i < N_AMOSTRAS; i++) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      amostrasTemp[validas] = t;
      amostrasHum[validas]  = h;
      validas++;
    } else {
      Serial.printf("  amostra %d/%d: leitura invalida (checksum ou timing)\n",
                    i + 1, N_AMOSTRAS);
    }

    if (i < N_AMOSTRAS - 1) {
      delay(DHT_MIN_INTERVAL_MS); // datasheet: nunca ler mais rapido que isso
    }
  }

  if (validas < (N_AMOSTRAS / 2 + 1)) {
    // Menos que a maioria das amostras foi válida: sem base para uma
    // mediana confiável. Melhor declarar falha do que inventar um número.
    return false;
  }

  temperaturaC = mediana(amostrasTemp, validas);
  umidadeRH    = mediana(amostrasHum, validas);
  return true;
}

void ordenar(float amostras[], int n) {
  for (int i = 1; i < n; i++) {
    float chave = amostras[i];
    int j = i - 1;
    while (j >= 0 && amostras[j] > chave) {
      amostras[j + 1] = amostras[j];
      j--;
    }
    amostras[j + 1] = chave;
  }
}

float mediana(float amostras[], int n) {
  ordenar(amostras, n);
  if (n % 2 == 1) {
    return amostras[n / 2];
  }
  return (amostras[n / 2 - 1] + amostras[n / 2]) / 2.0f;
}

// =============================================================================
// Tensão de bateria via divisor resistivo (Aula 03, Unidade 1).
//   Vout = Vin * R2 / (R1 + R2)  =>  Vin = Vout * (R1 + R2) / R2
// =============================================================================
float lerTensaoBateria() {
  int leituraBruta = analogRead(VBAT_ADC_PIN);
  float vOut = (leituraBruta / VBAT_ADC_MAXRAW) * VBAT_ADC_MAXV;
  float vBat = vOut * (VBAT_R1_OHMS + VBAT_R2_OHMS) / VBAT_R2_OHMS;
  return vBat;
}

// =============================================================================
// Orçamento de energia (Aula 08, "Bateria e painel em quatro passos").
// Impresso uma vez, no primeiro boot, para conferência em bancada.
// Os valores de corrente aqui são estimativa/datasheet — MEÇA com o
// multímetro antes de fechar o orçamento real do seu projeto.
// =============================================================================
void imprimirOrcamentoEnergia() {
  float cargaAcordadoMah = (I_ACORDADO_SEM_RADIO_mA + I_DHT22_ATIVO_mA)
                            * (T_ACORDADO_S / 3600.0f);
  float tempoDormindo_s = (float)TEMPO_ENTRE_LEITURAS_S - T_ACORDADO_S;
  float cargaDormindoMah = I_DEEP_SLEEP_PLACA_mA * (tempoDormindo_s / 3600.0f);
  float ciclosPorDia = 86400.0f / (float)TEMPO_ENTRE_LEITURAS_S;
  float cargaDiariaMah = (cargaAcordadoMah + cargaDormindoMah) * ciclosPorDia;

  Serial.println();
  Serial.println("==== Orcamento de energia (estimativa inicial) ====");
  Serial.printf("Ciclo: acorda a cada %lu s (%.1f vezes/dia)\n",
                (unsigned long)TEMPO_ENTRE_LEITURAS_S, ciclosPorDia);
  Serial.printf("Carga por ciclo acordado:  %.4f mA.h\n", cargaAcordadoMah);
  Serial.printf("Carga por ciclo dormindo:  %.4f mA.h\n", cargaDormindoMah);
  Serial.printf("Consumo diario estimado:   %.2f mA.h/dia\n", cargaDiariaMah);
  Serial.println("Origem dos numeros: ver comentarios de #define no topo do arquivo.");
  Serial.println("Meca o consumo real na bancada antes de dimensionar bateria/painel");
  Serial.println("(README.md, secao 5).");
  Serial.println("=====================================================");
}

// =============================================================================
// Deep sleep: única fonte de despertar é o temporizador (Aula 08). Ao voltar,
// o programa recomeça do setup() — nada em RAM comum atravessa o sono.
// =============================================================================
void dormir() {
  Serial.printf("Dormindo por %lu segundos...\n\n", (unsigned long)TEMPO_ENTRE_LEITURAS_S);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)TEMPO_ENTRE_LEITURAS_S * 1000000ULL);
  esp_deep_sleep_start();
}
