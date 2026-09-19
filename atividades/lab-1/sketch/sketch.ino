/*  ============================================================================
    Aula 04 - Laboratorio 01: o primeiro sistema funcional
    Topicos em Sistemas Computacionais I (IoT/AIoT) - UFPI - 2026.2
    Docente responsavel: Prof. Dr. Ricardo de Andrade Lira Rabelo
    Ministrante: Atos Apollo Silva Borges

    VERSAO GABARITO (docente). A versao do aluno tem os trechos marcados
    com // TODO em branco.

    UM SENSOR, DUAS SAIDAS. A cadeia completa da camada de percepcao:

        LDR  ->  leitura (ADC)  ->  decisao  ->  LED + buzzer  ->  telemetria

    Sensor     LDR analogico, lido pelo ADC1
    Saidas     LED em PWM   - atuacao continua e proporcional
               Buzzer passivo - atuacao discreta, sonora, com cadencia propria
    Telemetria linha CSV no monitor serial a 115200 bps

    Nenhuma biblioteca externa: analogRead(), analogWrite(), tone() e noTone()
    ja vem no core do ESP32. O projeto no Wokwi nao precisa de libraries.txt.

    Mapa de pinos - Wokwi (ESP32 DevKit C v4) x placa WeMos D1 R32
    -------------------------------------------------------------------------
      funcao          GPIO   rotulo na D1 R32   observacao
      LDR (no)          34         A3           ADC1, so entrada, sem pull
      LED (anodo)       26         D2           mesmo pino da Aula 03
      Buzzer (+)        25         D3           buzzer PASSIVO, tocado por tone()
    -------------------------------------------------------------------------
    Por que ADC1 e nao ADC2: o ADC2 fica indisponivel com o radio WiFi ativo.
    O projeto vai usar WiFi no Modulo 2, entao a escolha ja e feita agora.

    O DIVISOR DO LDR - leia antes de mexer nos limiares.
    O LDR nao mede nada sozinho: ele so muda de resistencia. Quem transforma
    resistencia em tensao e o divisor da Aula 03:

        3V3 ---[ 10k fixo ]---+--- GPIO34 (leitura)
                              |
                            [ LDR ]
                              |
                             GND

    Com o LDR no ramo de BAIXO, a tensao no no CAI quando a luz aumenta.
    Logo, neste projeto: CONTAGEM ALTA = ESCURO, CONTAGEM BAIXA = CLARO.
    Inverter os dois componentes inverte todo o codigo abaixo - e a sua
    tabela de medidas denuncia isso na primeira linha.

    No Wokwi o modulo wokwi-photoresistor-sensor JA traz esse divisor pronto
    (10k fixo + LDR), e o pino AO e o no do meio. Na bancada, com LDR nu,
    voce monta o divisor com as proprias maos - mesma topologia, mesmo codigo.

    Por que buzzer PASSIVO: o passivo e um transdutor "burro" - a frequencia
    do som e a frequencia que voce manda no pino, e por isso tone() funciona.
    O buzzer ATIVO tem oscilador proprio: so aceita ligar e desligar, e com
    ele tone() nao muda nota nenhuma.
    ============================================================================ */

// ------------------------------ pinos --------------------------------------
const int PINO_LDR    = 34;
const int PINO_LED    = 26;
const int PINO_BUZZER = 25;

// --------------------------- constantes de projeto -------------------------
const int   ADC_BITS = 12;
const int   ADC_MAX  = 4095;            // 2^12 - 1
const float V_REF    = 3.3;             // fundo de escala nominal do ADC

// Histerese do modo noturno. Dois limiares, nao um: com um unico limiar o
// sistema oscila quando a leitura fica em cima do valor de corte.
// Lembre da topologia: CONTAGEM ALTA = ESCURO. Por isso o limiar de escurecer
// e o MAIOR dos dois. Valores medidos com o modulo padrao do Wokwi
// (rl10 = 50 kohm, gamma = 0,7): 10 lux ~ 3412 e 100 lux ~ 2045.
const int LIMIAR_ESCURECER = 2500;      // ACIMA disso, entra em modo noturno
const int LIMIAR_CLAREAR   = 1800;      // ABAIXO disso, sai do modo noturno

// Faixa util do tom do buzzer: tres oitavas entre o claro e o escuro total.
const int FREQ_MIN = 220;               // La3
const int FREQ_MAX = 1760;              // La6

// Cadencia do bipe, em milissegundos. Alarme continuo cansa e some do
// ouvido; alarme intermitente continua sendo percebido.
const unsigned long BIPE_CICLO  = 1000;
const unsigned long BIPE_LIGADO = 150;

const unsigned long PERIODO_TELEMETRIA = 500;

// ------------------------------ estado -------------------------------------
bool modoNoturno = false;
bool tocando     = false;

unsigned long tUltimaLinha = 0;

// ------------------------------ funcoes ------------------------------------

// Converte a contagem bruta do ADC em tensao. E aqui que o numero deixa de
// ser "um inteiro qualquer" e vira uma grandeza fisica.
float contagemParaTensao(int contagem) {
  return (contagem * V_REF) / ADC_MAX;
}

// Decisao 1 - saida CONTINUA. Brilho do LED proporcional a escuridao.
// A contagem JA cresce com a escuridao (LDR no ramo de baixo do divisor),
// entao o mapeamento aqui e direto, nao invertido.
int brilhoPorLuminosidade(int contagem) {
  int brilho = map(contagem, 0, ADC_MAX, 0, 255);
  return constrain(brilho, 0, 255);
}

// Decisao 2 - saida DISCRETA. Modo noturno com histerese: e ele que decide
// se o buzzer tem o direito de tocar. Retorna o estado novo.
bool atualizaModoNoturno(int contagem, bool estadoAtual) {
  if (!estadoAtual && contagem > LIMIAR_ESCURECER) return true;
  if (estadoAtual  && contagem < LIMIAR_CLAREAR)   return false;
  return estadoAtual;                   // zona morta: nao muda nada
}

// Decisao 3 - o tom do alarme sobe conforme escurece.
int frequenciaPorEscuridao(int contagem) {
  int f = map(contagem, 0, ADC_MAX, FREQ_MIN, FREQ_MAX);
  return constrain(f, FREQ_MIN, FREQ_MAX);
}

// ------------------------------ setup --------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(ADC_BITS);       // fixa 12 bits: 0 a 4095
  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BUZZER, OUTPUT);
  noTone(PINO_BUZZER);                  // comeca em silencio

  Serial.println();
  Serial.println(F("# Lab 01 - primeiro sistema funcional"));
  Serial.println(F("t_ms;adc;tensao_V;pwm;modo;freq_hz;bipe"));
}

// ------------------------------ loop ---------------------------------------
void loop() {
  unsigned long agora = millis();

  // ---- 1. leitura ---------------------------------------------------------
  int   contagem = analogRead(PINO_LDR);
  float tensao   = contagemParaTensao(contagem);

  // ---- 2. decisao ---------------------------------------------------------
  int  brilho     = brilhoPorLuminosidade(contagem);
  modoNoturno     = atualizaModoNoturno(contagem, modoNoturno);
  int  frequencia = frequenciaPorEscuridao(contagem);

  // ---- 3. atuacao ---------------------------------------------------------
  analogWrite(PINO_LED, brilho);        // saida continua: 0 a 255

  // Saida discreta. O bipe sai da propria linha do tempo: nenhum delay()
  // trava o laco, e por isso o LED continua respondendo enquanto o buzzer
  // esta na parte silenciosa do ciclo.
  bool devoTocar = modoNoturno && ((agora % BIPE_CICLO) < BIPE_LIGADO);
  if (devoTocar != tocando) {           // so mexe no buzzer quando muda
    tocando = devoTocar;
    if (tocando) tone(PINO_BUZZER, frequencia);
    else         noTone(PINO_BUZZER);
  }

  // ---- 4. telemetria ------------------------------------------------------
  if (agora - tUltimaLinha >= PERIODO_TELEMETRIA) {
    tUltimaLinha = agora;
    Serial.print(agora);       Serial.print(';');
    Serial.print(contagem);    Serial.print(';');
    Serial.print(tensao, 3);   Serial.print(';');
    Serial.print(brilho);      Serial.print(';');
    Serial.print(modoNoturno ? F("noturno") : F("diurno"));
    Serial.print(';');
    Serial.print(frequencia);  Serial.print(';');
    Serial.println(tocando ? F("1") : F("0"));
  }
}
