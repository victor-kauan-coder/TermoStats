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
    ============================================================================

    ----------------------------------------------------------------------------
    ATUALIZACAO - Aula 05 "Da leitura a medida": filtro + calibracao
    ----------------------------------------------------------------------------
    O circuito e o diagram.json NAO MUDAM - Aula 05 e so codigo. O que muda:

      1. FILTRO (Parte 3 da analise). Media movel de N = 8 sobre a tensao ja
         calibrada. Em 'ambiente.csv' esse N derruba sigma de 6,08 para 2,32
         contagens (-62%) por so 8 ms de atraso - irrelevante para um sensor
         de luz ambiente. N maior nao ajuda: a curva trava perto de N ~ 8
         porque o que sobra dali pra frente e deriva lenta da lampada/sala,
         nao ruido aleatorio - e filtro nao remove deriva.

      2. CALIBRACAO (Parte 4 da analise). analogRead() + regra de tres vira
         analogReadMilliVolts(), que aplica a curva de fabrica gravada na
         eFuse do chip - o erro sistematico medido em calibracao.csv chegou
         a 46 contagens, MAIOR que todo o ruido aleatorio que o filtro reduz.
         A tensao calibrada e filtrada e entao convertida para LUX invertendo
         o divisor e aplicando a lei de potencia do LDR ajustada em log-log:

             R_LDR = R10 * (lux/10)^(-gamma)   =>   lux = 10*(R_LDR/R10)^(-1/gamma)

         gamma = 0,700 +/- 0,002 e R10 = 50,0 +/- 0,3 kOhm, recuperados de
         calibracao.csv - e batem com os parametros do proprio componente
         simulado (wokwi-photoresistor-sensor: rl10=50, gamma=0.7 no
         diagram.json), entao a curva vale tanto no Wokwi quanto na bancada
         com o mesmo modulo fisico.

      3. TELEMETRIA EM GRANDEZA FISICA. O CSV passa a sair em lux, nao em
         volt nem em contagem crua - e leva a incerteza declarada (4%, do
         luximetro de referencia usado para calibrar; e ela, nao o sigma do
         canal, que domina o orcamento de erro depois do filtro).

      4. HISTERESE DO MODO NOTURNO EM LUX. Os limiares antigos (2500 / 1800
         contagens) ja miravam ~52 lux e ~141 lux por baixo do pano - so que
         sem o numero explicito. Publicar a decisao na grandeza fisica e o
         proprio ponto da aula: "nao e o numero que voce imprime, e o numero
         que voce consegue defender".

    Analise completa, com os graficos e a curva ajustada: notebook/ e
    README.md deste mesmo repositorio (pasta aula05).
    ============================================================================ */

// ------------------------------ pinos --------------------------------------
const int PINO_LDR    = 34;
const int PINO_LED    = 26;
const int PINO_BUZZER = 25;

// --------------------------- constantes de projeto -------------------------
const int   ADC_BITS = 12;
const int   ADC_MAX  = 4095;            // 2^12 - 1
const float V_REF    = 3.3;             // fundo de escala nominal do ADC

// Faixa util do tom do buzzer: tres oitavas entre o claro e o escuro total.
const int FREQ_MIN = 220;               // La3
const int FREQ_MAX = 1760;              // La6

// Cadencia do bipe, em milissegundos. Alarme continuo cansa e some do
// ouvido; alarme intermitente continua sendo percebido.
const unsigned long BIPE_CICLO  = 1000;
const unsigned long BIPE_LIGADO = 150;

const unsigned long PERIODO_TELEMETRIA = 500;

// --------------------- Aula 05: filtro e calibracao -------------------------
// Curva do LDR recuperada em Parte 4 (calibracao.csv, ajuste log-log) e
// confirmada pelos atributos do proprio componente simulado no diagram.json.
const float GAMA       = 0.700;     // expoente da lei de potencia do LDR
const float R10_OHM    = 50000.0;   // resistencia do LDR a 10 lux
const float R_FIXO_OHM = 10000.0;   // resistor fixo do divisor (mesmo da Aula 03/04)

// Filtro escolhido na Parte 3: media movel de N = 8 (ver justificativa no
// bloco de comentarios acima e no README). Filtra-se a tensao JA calibrada,
// nao a contagem crua - senao o filtro reintroduziria o erro sistematico
// que a calibracao acabou de remover.
const int N_FILTRO = 8;
uint32_t  janelaMv[N_FILTRO];
int       idxJanela   = 0;
bool      janelaCheia = false;

// Histerese do modo noturno, agora em LUX (nao mais em contagens cruas).
// CONTAGEM ALTA = ESCURO ainda vale para o sensor, mas LUX BAIXO = ESCURO -
// a comparacao abaixo esta invertida em relacao a versao da Aula 04.
const float LIMIAR_ESCURECER_LUX = 52.4;   // ABAIXO disso, entra em modo noturno
const float LIMIAR_CLAREAR_LUX   = 141.0;  // ACIMA disso, sai do modo noturno

// Incerteza da medida publicada (Parte 4): depois do filtro e da calibracao
// o ruido do proprio canal cai para ~0,34% em 'ambiente' - mas a medida foi
// calibrada contra um luximetro de referencia que declara 4%. E esse numero,
// nao o sigma do canal, que domina o orcamento de erro e que precisa
// acompanhar todo valor publicado.
const float INCERTEZA_PCT = 4.0;

// ------------------------------ estado -------------------------------------
bool modoNoturno = false;
bool tocando     = false;

unsigned long tUltimaLinha = 0;

// ------------------------------ funcoes ------------------------------------

// Media movel de N = 8 sobre a tensao calibrada (mV). Guarda as ultimas N
// leituras e devolve a media das validas - o inicio (antes de encher a
// janela) usa so as amostras que ja chegaram, sem gerar NaN nem lixo.
uint32_t filtraMediaMovel(uint32_t novaLeituraMv) {
  janelaMv[idxJanela] = novaLeituraMv;
  idxJanela = (idxJanela + 1) % N_FILTRO;
  if (idxJanela == 0) janelaCheia = true;

  int nValidas = janelaCheia ? N_FILTRO : idxJanela;
  uint32_t soma = 0;
  for (int i = 0; i < nValidas; i++) soma += janelaMv[i];
  return soma / (uint32_t)nValidas;
}

// Inverte o divisor (V_no = V_alim*R_LDR/(R_FIXO+R_LDR)) para recuperar a
// resistencia do LDR, depois aplica a lei de potencia ajustada na Parte 4:
// R = R10*(lux/10)^(-gamma)  =>  lux = 10*(R/R10)^(-1/gamma).
float tensaoParaLux(uint32_t mv) {
  float mv_f  = constrain((float)mv, 1.0, V_REF * 1000.0 - 1.0);
  float r_ldr = R_FIXO_OHM * mv_f / (V_REF * 1000.0 - mv_f);
  return 10.0 * pow(r_ldr / R10_OHM, -1.0 / GAMA);
}

// Decisao 1 - saida CONTINUA. Brilho do LED proporcional a escuridao.
// Opera sobre a tensao ja filtrada e calibrada (mV), nao mais sobre a
// contagem crua - mesmo sentido de antes: mV alto = escuro = LED mais aceso.
int brilhoPorLuminosidade(uint32_t mvFiltrado) {
  int brilho = map(mvFiltrado, 0, (uint32_t)(V_REF * 1000.0), 0, 255);
  return constrain(brilho, 0, 255);
}

// Decisao 2 - saida DISCRETA. Modo noturno com histerese, agora em LUX.
// Lux baixo = escuro, entao a comparacao e o espelho da versao em contagens.
bool atualizaModoNoturno(float luxAtual, bool estadoAtual) {
  if (!estadoAtual && luxAtual < LIMIAR_ESCURECER_LUX) return true;
  if (estadoAtual  && luxAtual > LIMIAR_CLAREAR_LUX)   return false;
  return estadoAtual;                   // zona morta: nao muda nada
}

// Decisao 3 - o tom do alarme sobe conforme escurece (mV alto = escuro).
int frequenciaPorEscuridao(uint32_t mvFiltrado) {
  int f = map(mvFiltrado, 0, (uint32_t)(V_REF * 1000.0), FREQ_MIN, FREQ_MAX);
  return constrain(f, FREQ_MIN, FREQ_MAX);
}

// ------------------------------ setup --------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(ADC_BITS);           // fixa 12 bits: 0 a 4095
  analogSetPinAttenuation(PINO_LDR, ADC_11db); // mesma faixa da calibracao (11 dB)
  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BUZZER, OUTPUT);
  noTone(PINO_BUZZER);                      // comeca em silencio

  Serial.println();
  Serial.println(F("# Lab 01.5 - filtro (N=8) + calibracao (Aula 05)"));
  Serial.println(F("t_ms;adc_bruto;mv_filtrado;lux;incerteza_pct;pwm;modo;freq_hz;bipe"));
}

// ------------------------------ loop ---------------------------------------
void loop() {
  unsigned long agora = millis();

  // ---- 1. leitura + filtro + calibracao ------------------------------------
  int      contagemBruta = analogRead(PINO_LDR);          // so para comparar na telemetria
  uint32_t mvCalibrado   = analogReadMilliVolts(PINO_LDR); // corrige ganho/offset/nao linearidade de fabrica
  uint32_t mvFiltrado    = filtraMediaMovel(mvCalibrado);  // media movel N=8 (Parte 3)
  float    lux           = tensaoParaLux(mvFiltrado);      // grandeza fisica (Parte 4)

  // ---- 2. decisao -----------------------------------------------------------
  int  brilho     = brilhoPorLuminosidade(mvFiltrado);
  modoNoturno     = atualizaModoNoturno(lux, modoNoturno);
  int  frequencia = frequenciaPorEscuridao(mvFiltrado);

  // ---- 3. atuacao -----------------------------------------------------------
  analogWrite(PINO_LED, brilho);            // saida continua: 0 a 255

  // Saida discreta. O bipe sai da propria linha do tempo: nenhum delay()
  // trava o laco, e por isso o LED continua respondendo enquanto o buzzer
  // esta na parte silenciosa do ciclo.
  bool devoTocar = modoNoturno && ((agora % BIPE_CICLO) < BIPE_LIGADO);
  if (devoTocar != tocando) {               // so mexe no buzzer quando muda
    tocando = devoTocar;
    if (tocando) tone(PINO_BUZZER, frequencia);
    else         noTone(PINO_BUZZER);
  }

  // ---- 4. telemetria, ja em grandeza fisica e com incerteza declarada ------
  if (agora - tUltimaLinha >= PERIODO_TELEMETRIA) {
    tUltimaLinha = agora;
    Serial.print(agora);            Serial.print(';');
    Serial.print(contagemBruta);    Serial.print(';');
    Serial.print(mvFiltrado);       Serial.print(';');
    Serial.print(lux, 2);           Serial.print(';');
    Serial.print(INCERTEZA_PCT, 1); Serial.print(';');
    Serial.print(brilho);           Serial.print(';');
    Serial.print(modoNoturno ? F("noturno") : F("diurno"));
    Serial.print(';');
    Serial.print(frequencia);       Serial.print(';');
    Serial.println(tocando ? F("1") : F("0"));
  }
}
