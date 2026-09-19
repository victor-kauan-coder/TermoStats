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
  HISTÓRICO DESTA REVISÃO (ver README.md, seção 10, para o detalhe completo)
  ============================================================================
  - O intervalo entre leituras deixou de ser um #define fixo e passou a ser
    CONFIGURÁVEL em tempo de execução, gravado na memória não-volátil (NVS)
    via a biblioteca Preferences. Motivo: o mesmo firmware atende cenários
    de criticidade diferente (uma câmara fria de farmácia pode exigir
    checagem mais frequente que uma sala de TI), e recompilar para cada
    instalação não é uma solução aceitável de produto.
  - Os limiares de temperatura e umidade (RF-02 do relatório) também
    passaram a ser configuráveis pelo mesmo mecanismo, em vez de fixos em
    firmware — cada ambiente monitorado tem sua própria faixa aceitável.
  - Foi implementado o alarme local (RF-03/RF-04): buzzer ativo + LED
    dedicado, acionados quando a leitura sai da faixa configurada e
    desligados quando ela retorna.
  - Enquanto o alarme está ativo, o nó reduz o intervalo entre despertares
    (para "checar de novo logo"), em vez de manter o mesmo intervalo de
    rotina — ver seção 5 do README para a justificativa energética dessa
    escolha.

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
  - Compara a leitura filtrada contra uma faixa configurável de
    temperatura e umidade e aciona um alarme local (buzzer + LED) quando
    o valor sai da faixa, desligando o alarme quando o valor volta.
  - Declara a origem de cada valor de corrente usado no cálculo de energia
    (datasheet / bancada / estimativa), como pede o orçamento da Aula 08.
  - Dorme em deep sleep entre leituras. Como o loop() nunca é retomado
    nesse modo, todo o programa mora no setup() (Aula 08, Demonstração 03).
  - Lê opcionalmente a tensão da bateria por um divisor resistivo, seguindo
    a fórmula e o cuidado descritos na Aula 03.
  - Aceita configuração pela porta serial nos primeiros segundos após
    energizar (ver seção "MODO DE CONFIGURAÇÃO" abaixo e README.md,
    seção 10).

  HARDWARE
  - ESP32 (referência da disciplina: WeMos D1 R32)
  - DHT22 / AM2302, ALIMENTADO EM 3V3 (ver README.md, seção 2 — "Por que
    3V3 e não 5V")
  - Resistor de pull-up de 10 kΩ entre VCC (3V3) e o pino de dado do DHT22
  - Buzzer ativo (2 terminais, já oscila sozinho) entre GPIO26 e GND
  - LED de alarme (com resistor limitador, ~330 Ω) em GPIO27
  - (Opcional) Divisor resistivo 100 kΩ / 100 kΩ da bateria até GPIO34,
    para monitorar a tensão da fonte (ver README.md, seção 2.3)

  DEPENDÊNCIAS (Arduino IDE → Gerenciador de Bibliotecas)
  - "DHT sensor library" (Adafruit)
  - "Adafruit Unified Sensor"
  - "Preferences" (já inclusa no core ESP32 — não precisa instalar)

  MAPA DE PINOS USADO NESTE SKETCH (compatível com a tabela da Aula 03)
  - GPIO4  (rótulo A1) — dado do DHT22 (uso digital; não entra em conflito
    com leitura analógica porque não usamos A1 como ADC neste projeto)
  - GPIO25 (rótulo D3) — LED de status de bancada (nó acordado)
  - GPIO26 (rótulo D4) — buzzer ativo do alarme local
  - GPIO27 (rótulo D5) — LED do alarme local (distinto do LED de status)
  - GPIO34 (rótulo A2) — leitura de VBAT (ADC1, só entrada, confiável com
    o WiFi ligado, conforme a Aula 03)

  MODO DE CONFIGURAÇÃO
  Nos primeiros ~4 segundos após energizar (Serial já em 115200 baud),
  o firmware aguarda um comando de configuração. Se nada chegar, segue o
  ciclo normal com a última configuração salva (ou os padrões de fábrica,
  no primeiro boot). Comandos aceitos, um por linha, finalizados por
  Enter:

    CONFIG                 entra no modo interativo (pede cada valor)
    SET INTERVALO=<s>      intervalo normal entre leituras, em segundos
    SET TEMP_MIN=<c>       limite inferior de temperatura, em °C
    SET TEMP_MAX=<c>       limite superior de temperatura, em °C
    SET UMID_MIN=<pct>     limite inferior de umidade relativa, em %
    SET UMID_MAX=<pct>     limite superior de umidade relativa, em %
    SHOW                   mostra a configuração atual gravada
    RESET                  restaura os padrões de fábrica

  Todo valor aceito é gravado na NVS (Preferences) e sobrevive a
  desligamentos e ao deep sleep — não é a mesma coisa que "gravar dado
  de leitura", que este ciclo continua sem fazer (ver Capítulo 1 do
  relatório): aqui só se persiste PARÂMETRO DE CONFIGURAÇÃO, não série
  temporal de medição.
  ============================================================================
*/

#include <DHT.h>
#include <esp_sleep.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// 1) CONFIGURAÇÃO DE HARDWARE — ver README.md, seção "Mapa de pinos"
// ---------------------------------------------------------------------------
#define DHT_PIN         4        // GPIO4 — dado do DHT22, digital
#define DHT_TYPE        DHT22
#define LED_STATUS_PIN  25       // GPIO25 — só para diagnóstico em bancada
#define BUZZER_PIN      26       // GPIO26 — buzzer ativo do alarme local
#define ALARME_LED_PIN  27       // GPIO27 — LED do alarme local
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
// 3) CONFIGURAÇÃO PADRÃO DE FÁBRICA — usada apenas no primeiro boot, ou
//    depois de um RESET. Depois disso, os valores em uso vêm da NVS
//    (Preferences) e podem ser trocados sem recompilar (ver "MODO DE
//    CONFIGURAÇÃO" no cabeçalho). O padrão de intervalo é 10 min por ser o
//    caso de teste fechado no Capítulo 2 do relatório (RF-01); os limiares
//    de exemplo são os da farmácia de manipulação usada como cenário de
//    validação (Capítulo 1) e DEVEM ser ajustados por ambiente na
//    instalação real.
// ---------------------------------------------------------------------------
#define PADRAO_INTERVALO_NORMAL_S   (10UL * 60UL)  // 10 min — RF-01
#define PADRAO_INTERVALO_ALARME_S   (1UL * 60UL)   // 1 min — checa de novo mais rápido em alarme
#define PADRAO_TEMP_MIN_C           15.0f          // exemplo: farmácia de manipulação
#define PADRAO_TEMP_MAX_C           25.0f
#define PADRAO_UMID_MIN_PCT         40.0f          // exemplo — ajustar por ambiente
#define PADRAO_UMID_MAX_PCT         70.0f

#define CONFIG_JANELA_MS            4000  // tempo de espera por comando serial no boot

// ---------------------------------------------------------------------------
// 4) ALARME LOCAL (RF-03/RF-04)
// ---------------------------------------------------------------------------
#define ALARME_DURACAO_MS      3000  // por quanto tempo buzzer+LED ficam ativos por ciclo em alarme

// ---------------------------------------------------------------------------
// 5) ENERGIA (Aula 08) — perfil de corrente por estado.
//    Valores marcados como estimativa/datasheet DEVEM ser confirmados com
//    multímetro na bancada antes de fechar o orçamento real do projeto.
//    Ver README.md, seção 5.
// ---------------------------------------------------------------------------
#define I_ACORDADO_SEM_RADIO_mA   45.0f    // origem: folha de dados do ESP32
#define I_DHT22_ATIVO_mA          1.5f     // origem: folha de dados do DHT22
#define I_DEEP_SLEEP_PLACA_mA     2.0f     // origem: bancada (placa de desenvolvimento, não o chip)
#define I_ALARME_ATIVO_mA         35.0f    // origem: ESTIMATIVA — meça buzzer+LED juntos na bancada
#define T_ACORDADO_S              12.0f    // origem: bancada (N_AMOSTRAS leituras + overhead)

// ---------------------------------------------------------------------------
// 6) MEMÓRIA RTC — sobrevive ao deep sleep (Aula 08, Demonstração 03).
//    Qualquer variável em RAM comum volta ao valor inicial a cada despertar;
//    o que precisa atravessar o sono tem de estar aqui.
// ---------------------------------------------------------------------------
RTC_DATA_ATTR uint32_t bootCount          = 0;
RTC_DATA_ATTR float    emaTemp            = NAN;
RTC_DATA_ATTR float    emaHum             = NAN;
RTC_DATA_ATTR bool     emaInicializada    = false;
RTC_DATA_ATTR uint32_t falhasConsecutivas = 0;

DHT dht(DHT_PIN, DHT_TYPE);
Preferences preferencias;

// Configuração em uso neste ciclo (carregada da NVS em carregarConfiguracao()).
struct Configuracao {
  uint32_t intervaloNormalS;
  uint32_t intervaloAlarmeS;
  float    tempMinC;
  float    tempMaxC;
  float    umidMinPct;
  float    umidMaxPct;
};
Configuracao cfg;

// ---------------------------------------------------------------------------
// Protótipos
// ---------------------------------------------------------------------------
bool  lerDHTFiltrado(float &temperaturaC, float &umidadeRH);
void  ordenar(float amostras[], int n);
float mediana(float amostras[], int n);
float lerTensaoBateria();
void  imprimirOrcamentoEnergia(bool emAlarme);
void  dormir(uint32_t segundos);
void  carregarConfiguracao();
void  salvarConfiguracao();
void  imprimirConfiguracao();
void  talvezEntrarModoConfig();
void  processarComandoConfig(String linha);
bool  avaliarAlarme(float temperaturaC, float umidadeRH);
void  ligarAlarme();
void  desligarAlarme();

// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(50); // só para o monitor serial conectar; não é hábito de produção
  bootCount++;

  pinMode(LED_STATUS_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(ALARME_LED_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(ALARME_LED_PIN, LOW);
  digitalWrite(LED_STATUS_PIN, HIGH); // aceso = no acordado, referencia de bancada

  carregarConfiguracao();

  Serial.println();
  Serial.printf("== No DHT22 - despertar #%lu ==\n", (unsigned long)bootCount);
  Serial.printf("Motivo do despertar: %d\n", esp_sleep_get_wakeup_cause());
  imprimirConfiguracao();

  // Janela curta para aceitar configuração via serial (ver cabeçalho do
  // arquivo e README.md, secao 10). Só bloqueia se alguem estiver
  // digitando; caso contrário segue o ciclo normal sem atraso perceptível
  // no orçamento de energia (a janela conta como parte do tempo acordado
  // já orçado para leitura + overhead).
  talvezEntrarModoConfig();

  dht.begin();

  float temperaturaC, umidadeRH;
  bool leituraOk = lerDHTFiltrado(temperaturaC, umidadeRH);
  bool emAlarme = false;

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

    // RF-02/RF-03/RF-04: compara contra a faixa configurada e aciona (ou
    // desliga) o alarme local. Usa a mediana da amostra, não a EMA, porque
    // o alarme precisa reagir ao ciclo atual, e não a uma média que
    // suaviza justamente o tipo de desvio que o alarme existe para pegar.
    emAlarme = avaliarAlarme(temperaturaC, umidadeRH);

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
    imprimirOrcamentoEnergia(emAlarme);
  }

  digitalWrite(LED_STATUS_PIN, LOW);

  uint32_t intervalo = emAlarme ? cfg.intervaloAlarmeS : cfg.intervaloNormalS;
  dormir(intervalo);
}

void loop() {
  // Em deep sleep o programa nunca "continua": ele recomeça do setup().
  // Este loop() nunca executa (Aula 08, Demonstração 03).
}

// =============================================================================
// Configuração (RF-01/RF-02 como parâmetros, não constantes de firmware).
// Gravada na NVS via Preferences — sobrevive a deep sleep e a desligamentos,
// mas é PARÂMETRO, não SÉRIE DE DADOS: o relatório (Capítulo 1) continua
// sem persistir leitura alguma neste ciclo.
// =============================================================================
void carregarConfiguracao() {
  preferencias.begin("termolog", false);

  bool primeiraVez = !preferencias.isKey("intervalo_s");

  cfg.intervaloNormalS = preferencias.getUInt("intervalo_s", PADRAO_INTERVALO_NORMAL_S);
  cfg.intervaloAlarmeS = preferencias.getUInt("interv_alrm_s", PADRAO_INTERVALO_ALARME_S);
  cfg.tempMinC         = preferencias.getFloat("temp_min_c", PADRAO_TEMP_MIN_C);
  cfg.tempMaxC         = preferencias.getFloat("temp_max_c", PADRAO_TEMP_MAX_C);
  cfg.umidMinPct       = preferencias.getFloat("umid_min_pc", PADRAO_UMID_MIN_PCT);
  cfg.umidMaxPct       = preferencias.getFloat("umid_max_pc", PADRAO_UMID_MAX_PCT);

  preferencias.end();

  if (primeiraVez) {
    Serial.println("Primeiro boot: gravando configuracao padrao de fabrica na NVS.");
    salvarConfiguracao();
  }
}

void salvarConfiguracao() {
  preferencias.begin("termolog", false);
  preferencias.putUInt("intervalo_s", cfg.intervaloNormalS);
  preferencias.putUInt("interv_alrm_s", cfg.intervaloAlarmeS);
  preferencias.putFloat("temp_min_c", cfg.tempMinC);
  preferencias.putFloat("temp_max_c", cfg.tempMaxC);
  preferencias.putFloat("umid_min_pc", cfg.umidMinPct);
  preferencias.putFloat("umid_max_pc", cfg.umidMaxPct);
  preferencias.end();
}

void imprimirConfiguracao() {
  Serial.println("---- Configuracao em uso (NVS) ----");
  Serial.printf("Intervalo normal:  %lu s\n", (unsigned long)cfg.intervaloNormalS);
  Serial.printf("Intervalo em alarme: %lu s\n", (unsigned long)cfg.intervaloAlarmeS);
  Serial.printf("Faixa de temperatura: %.1f a %.1f C\n", cfg.tempMinC, cfg.tempMaxC);
  Serial.printf("Faixa de umidade:     %.1f a %.1f %%RH\n", cfg.umidMinPct, cfg.umidMaxPct);
  Serial.println("Digite CONFIG nos proximos segundos para alterar (ver README.md, secao 10).");
}

// Espera uma janela curta por comando serial. Não bloqueia indefinidamente:
// se nada chegar dentro de CONFIG_JANELA_MS, segue o ciclo normal.
void talvezEntrarModoConfig() {
  unsigned long inicio = millis();
  String linha = "";

  while (millis() - inicio < CONFIG_JANELA_MS) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (linha.length() > 0) {
          processarComandoConfig(linha);
          linha = "";
          inicio = millis(); // renova a janela após cada comando aceito
        }
      } else {
        linha += c;
      }
    }
  }
}

void processarComandoConfig(String linha) {
  linha.trim();
  linha.toUpperCase();

  if (linha == "SHOW") {
    imprimirConfiguracao();
    return;
  }

  if (linha == "RESET") {
    cfg.intervaloNormalS = PADRAO_INTERVALO_NORMAL_S;
    cfg.intervaloAlarmeS = PADRAO_INTERVALO_ALARME_S;
    cfg.tempMinC         = PADRAO_TEMP_MIN_C;
    cfg.tempMaxC         = PADRAO_TEMP_MAX_C;
    cfg.umidMinPct       = PADRAO_UMID_MIN_PCT;
    cfg.umidMaxPct       = PADRAO_UMID_MAX_PCT;
    salvarConfiguracao();
    Serial.println("Configuracao restaurada para o padrao de fabrica.");
    imprimirConfiguracao();
    return;
  }

  if (linha == "CONFIG") {
    Serial.println("Modo interativo: envie os comandos SET um de cada vez, ex.:");
    Serial.println("  SET INTERVALO=300");
    Serial.println("  SET TEMP_MIN=10");
    Serial.println("Digite SHOW para conferir o resultado.");
    return;
  }

  if (linha.startsWith("SET ")) {
    int posIgual = linha.indexOf('=');
    if (posIgual < 0) {
      Serial.println("Comando invalido. Formato: SET CHAVE=VALOR");
      return;
    }
    String chave = linha.substring(4, posIgual);
    float valor  = linha.substring(posIgual + 1).toFloat();
    chave.trim();

    bool reconhecido = true;
    if (chave == "INTERVALO") {
      cfg.intervaloNormalS = (uint32_t)valor;
    } else if (chave == "INTERVALO_ALARME") {
      cfg.intervaloAlarmeS = (uint32_t)valor;
    } else if (chave == "TEMP_MIN") {
      cfg.tempMinC = valor;
    } else if (chave == "TEMP_MAX") {
      cfg.tempMaxC = valor;
    } else if (chave == "UMID_MIN") {
      cfg.umidMinPct = valor;
    } else if (chave == "UMID_MAX") {
      cfg.umidMaxPct = valor;
    } else {
      reconhecido = false;
    }

    if (reconhecido) {
      salvarConfiguracao();
      Serial.printf("OK: %s = %.2f (gravado na NVS)\n", chave.c_str(), valor);
    } else {
      Serial.println("Chave desconhecida. Use INTERVALO, INTERVALO_ALARME, TEMP_MIN,");
      Serial.println("TEMP_MAX, UMID_MIN ou UMID_MAX.");
    }
    return;
  }

  Serial.println("Comando nao reconhecido. Comandos: CONFIG, SET <CHAVE>=<VALOR>, SHOW, RESET.");
}

// =============================================================================
// Alarme local (RF-03/RF-04). Retorna true se o alarme foi acionado neste
// ciclo (usado para decidir o intervalo de sono seguinte).
// =============================================================================
bool avaliarAlarme(float temperaturaC, float umidadeRH) {
  bool foraDaFaixa = (temperaturaC < cfg.tempMinC) || (temperaturaC > cfg.tempMaxC) ||
                      (umidadeRH   < cfg.umidMinPct) || (umidadeRH   > cfg.umidMaxPct);

  if (foraDaFaixa) {
    Serial.printf("ALARME: leitura fora da faixa configurada (T: %.1f-%.1f C, U: %.1f-%.1f %%RH)\n",
                  cfg.tempMinC, cfg.tempMaxC, cfg.umidMinPct, cfg.umidMaxPct);
    ligarAlarme();
    delay(ALARME_DURACAO_MS);
    desligarAlarme();
    return true;
  }

  // RF-04: dentro da faixa, o alarme fica desligado (não há necessidade de
  // desligar explicitamente aqui porque ele só é ligado dentro deste `if`
  // e a chamada a desligarAlarme() acima já cobre o próprio ciclo em que
  // ele soou; este comentário existe para deixar o requisito rastreável).
  return false;
}

void ligarAlarme() {
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(ALARME_LED_PIN, HIGH);
}

void desligarAlarme() {
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(ALARME_LED_PIN, LOW);
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
// Impresso uma vez, no primeiro boot, para conferência em bancada. Agora
// depende da configuração em uso (intervalo pode mudar em tempo de
// execução), então o cálculo lê `cfg` em vez de uma constante fixa.
// Os valores de corrente aqui são estimativa/datasheet — MEÇA com o
// multímetro antes de fechar o orçamento real do seu projeto.
// =============================================================================
void imprimirOrcamentoEnergia(bool emAlarme) {
  uint32_t intervaloS = emAlarme ? cfg.intervaloAlarmeS : cfg.intervaloNormalS;

  float cargaAcordadoMah = (I_ACORDADO_SEM_RADIO_mA + I_DHT22_ATIVO_mA)
                            * (T_ACORDADO_S / 3600.0f);
  float cargaAlarmeMah = emAlarme
      ? I_ALARME_ATIVO_mA * ((ALARME_DURACAO_MS / 1000.0f) / 3600.0f)
      : 0.0f;
  float tempoDormindo_s = (float)intervaloS - T_ACORDADO_S
                           - (emAlarme ? (ALARME_DURACAO_MS / 1000.0f) : 0.0f);
  float cargaDormindoMah = I_DEEP_SLEEP_PLACA_mA * (tempoDormindo_s / 3600.0f);
  float ciclosPorDia = 86400.0f / (float)intervaloS;
  float cargaDiariaMah = (cargaAcordadoMah + cargaAlarmeMah + cargaDormindoMah) * ciclosPorDia;

  Serial.println();
  Serial.println("==== Orcamento de energia (estimativa inicial) ====");
  Serial.printf("Ciclo atual: acorda a cada %lu s (%.1f vezes/dia) [%s]\n",
                (unsigned long)intervaloS, ciclosPorDia,
                emAlarme ? "intervalo de alarme" : "intervalo normal");
  Serial.printf("Carga por ciclo acordado:  %.4f mA.h\n", cargaAcordadoMah);
  Serial.printf("Carga do alarme no ciclo:  %.4f mA.h (0 se nao disparou)\n", cargaAlarmeMah);
  Serial.printf("Carga por ciclo dormindo:  %.4f mA.h\n", cargaDormindoMah);
  Serial.printf("Consumo diario estimado:   %.2f mA.h/dia (neste cenario)\n", cargaDiariaMah);
  Serial.println("Origem dos numeros: ver comentarios de #define no topo do arquivo.");
  Serial.println("I_ALARME_ATIVO_mA e ESTIMATIVA - meca buzzer+LED juntos na bancada.");
  Serial.println("Meca o consumo real na bancada antes de dimensionar bateria/painel");
  Serial.println("(README.md, secao 5).");
  Serial.println("=====================================================");
}

// =============================================================================
// Deep sleep: única fonte de despertar é o temporizador (Aula 08). Ao voltar,
// o programa recomeça do setup() — nada em RAM comum atravessa o sono.
// O intervalo agora é um parâmetro (normal ou de alarme), não uma constante.
// =============================================================================
void dormir(uint32_t segundos) {
  Serial.printf("Dormindo por %lu segundos...\n\n", (unsigned long)segundos);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)segundos * 1000000ULL);
  esp_deep_sleep_start();
}
