# Nó IoT de Temperatura e Umidade — ESP32 + DHT22

Firmware e documentação de um nó de sensoriamento com ESP32 e DHT22, construído
sobre os conceitos da disciplina **Tópicos em Sistemas Computacionais I** (UFPI):
eletrônica de prototipagem (Aula 03), ruído e calibração (Aula 05), barramentos
(Aula 06) e gerenciamento de energia (Aula 08).

O arquivo de firmware é `esp32_dht22.ino`. Este README documenta as decisões
de projeto por trás dele — o que a disciplina chama de "origem de cada número".

---

## Sumário

1. [Visão geral da arquitetura](#1-visão-geral-da-arquitetura)
2. [Hardware e ligação](#2-hardware-e-ligação)
3. [Mapa de pinos](#3-mapa-de-pinos)
4. [Filtragem de ruído e calibração](#4-filtragem-de-ruído-e-calibração)
5. [Gerenciamento de energia](#5-gerenciamento-de-energia)
6. [Instalação e dependências](#6-instalação-e-dependências)
7. [Diagnóstico de falhas](#7-diagnóstico-de-falhas)
8. [Extensões sugeridas](#8-extensões-sugeridas)
9. [Configuração em tempo de execução](#9-configuração-em-tempo-de-execução)
10. [Alarme local](#10-alarme-local)
11. [Referências](#11-referências)

---

## 1. Visão geral da arquitetura

O nó segue o ciclo **acordar → medir → filtrar → avaliar alarme →
(publicar) → dormir**, descrito na Aula 08 como o ciclo básico de um
sistema IoT alimentado por bateria:

```
┌─────────┐   timer    ┌──────────┐   5 leituras   ┌──────────┐   mediana +   ┌─────────┐   fora da    ┌─────────┐
│  deep   │ ─────────▶ │  acorda  │ ─────────────▶ │  DHT22   │ ────────────▶ │  dado   │ ───faixa?──▶ │ alarme  │
│  sleep  │            │ (setup)  │   (>=2 s entre) │  amostra │   EMA (RTC)   │  final  │              │ local   │
└─────────┘            └──────────┘                └──────────┘               └────┬────┘              └────┬────┘
     ▲                                                                              │                        │
     │                                     intervalo normal ou de alarme            │                        │
     └───────────────────────────────── esp_deep_sleep_start() ◀────────────────────┴────────────────────────┘
```

Duas coisas mudaram em relação à primeira versão deste firmware, e
ambas vêm de uma mesma constatação: o mesmo nó precisa atender
ambientes com criticidade diferente, sem recompilar para cada
instalação.

- O **intervalo entre leituras** e os **limiares de alarme** deixaram
  de ser `#define` fixos e passaram a ser configuráveis em tempo de
  execução, gravados na NVS via `Preferences` (Seção 9).
- O nó agora **compara a leitura contra a faixa configurada e aciona
  um alarme local** (buzzer + LED) quando ela sai da faixa (Seção 10).

Um ponto central da Aula 08 continua se aplicando diretamente aqui:
**em deep sleep o `loop()` nunca é retomado — o programa recomeça do
`setup()`**. Por isso todo o firmware mora no `setup()`, e o que
precisa sobreviver ao sono (contador de despertares, média
exponencial) é declarado com `RTC_DATA_ATTR`, a única memória que
atravessa o deep sleep. A configuração (intervalo, limiares), por sua
vez, sobrevive de outra forma — pela NVS, que persiste mesmo com o nó
totalmente desligado (não só dormindo).

---

## 2. Hardware e ligação

### 2.1 Lista de materiais

- ESP32 (referência da disciplina: WeMos D1 R32, ou qualquer placa equivalente)
- DHT22 / AM2302 (o AM2302 é o mesmo chip DHT22 em um módulo encapsulado)
- 1 resistor de 10 kΩ (pull-up do pino de dado)
- 1 buzzer ativo (2 terminais, já oscila sozinho — não confundir com o passivo, que precisa de PWM)
- 1 LED de alarme + 1 resistor limitador (~330 Ω)
- 2 resistores de 100 kΩ (opcional — divisor para leitura de bateria)
- Protoboard e jumpers

### 2.2 Por que alimentar o DHT22 em 3V3, e não em 5V

O DHT22 aceita de 3,3 V a 5,5 V, então tecnicamente funcionaria nas duas
tensões. A escolha deste projeto é **alimentar em 3V3**, pelo mesmo motivo
que a Aula 03 lista como uma das "quatro maneiras de perder a placa":

> *"Sensor de 5 V no pino — o ESP32 não é tolerante a 5 V: a tensão máxima
> admitida em um pino fica em torno de 3,6 V. O dano costuma ser silencioso e
> cumulativo."*

Se o DHT22 for alimentado em 3V3, o nível alto do seu pino de dado também sai
em ~3,3 V, compatível diretamente com o GPIO do ESP32 — **sem necessidade de
divisor resistivo ou conversor de nível** no fio de dado.

**Se o seu projeto exigir alimentar o DHT22 em 5V** (por exemplo, para um
cabo mais longo até o sensor), o pino de dado passa a chavear em ~5 V e **aí
sim é obrigatório** proteger a entrada do ESP32. Duas opções, na ordem de
preferência da disciplina:

1. **Conversor de nível lógico bidirecional** (poucos reais, Aula 03: "é o
   que a bancada terá disponível" para barramentos de dado).
2. **Divisor resistivo simples**, só se a comunicação for lenta o bastante
   para tolerar o arredondamento de borda que o divisor introduz (Aula 03,
   "Onde o divisor não serve": *"não serve para sinal rápido — a resistência
   somada à capacitância do fio arredonda as bordas do sinal"*). O
   protocolo do DHT22 é lento (dezenas de kHz equivalentes), então um
   divisor **funciona na prática**, mas o conversor de nível é a opção mais
   robusta.

   Fórmula do divisor (Aula 03, Unidade 1):

   ```
   V_out = V_in × R2 / (R1 + R2)
   ```

   Para levar 5 V a algo próximo de 3,3 V: `R1 = 10 kΩ`, `R2 = 20 kΩ` →
   `V_out = 5 × 20/30 = 3,33 V` (o mesmo exemplo numérico da Aula 03).

Este projeto **não usa divisor no fio de dado** porque optou por alimentar o
DHT22 em 3V3. O divisor que o firmware implementa é para outra finalidade:
medir a tensão da própria bateria (seção 2.3).

### 2.3 Pull-up do pino de dado

O DHT22 se comunica por um único fio de dado (protocolo próprio, parecido em
espírito com o 1-Wire visto na Aula 06, mas não compatível com ele). Como
todo barramento de dreno aberto, **o pull-up é parte do circuito, não
acessório de montagem** — a mesma lição que a Aula 06 deixa para o DS18B20:

> *"O DS18B20 não aparece: falta o resistor de 4,7 kΩ entre o fio de dados e
> o 3V3. O pull-up é parte do circuito, não acessório."*

Para o DHT22, o valor recomendado pelo fabricante é **10 kΩ** entre o pino
de dado e VCC (3V3). Muitos módulos DHT22 já vêm com esse resistor
embutido na placa do módulo — confira o seu antes de adicionar outro em
série, o que só empobreceria o sinal.

### 2.4 Divisor resistivo para leitura de bateria (opcional)

Para projetos alimentados por bateria, é útil saber a tensão restante sem
gastar um pino dedicado caro. O firmware lê `VBAT` em `GPIO34` (rótulo `A2`
na D1 R32), que a Aula 03 recomenda justamente por ser **ADC1, entrada
confiável mesmo com o WiFi ligado** (o ADC2, ao qual pertence a maioria dos
pinos "A0/A1", para de funcionar quando o rádio está ativo).

```
   VBAT (ex.: Li-ion até 4,2 V)
     │
    [R1 = 100 kΩ]
     │
     ├──────────────▶ GPIO34 (ADC1, só entrada)
     │
    [R2 = 100 kΩ]
     │
    GND
```

Com R1 = R2 = 100 kΩ, `V_out = V_in / 2`. Para uma bateria de 4,2 V no
máximo, `V_out = 2,1 V`, dentro da faixa útil do ADC do ESP32 (até ~3,1 V na
atenuação padrão — Aula 03, "O conversor A/D, em três avisos"). O firmware
desfaz a conta (`V_in = V_out × (R1+R2)/R2`) para devolver a tensão real.

Se o seu projeto não usa bateria (alimentação por USB/fonte fixa), pode
remover o divisor e ignorar a leitura de `VBAT` — o firmware simplesmente vai
reportar um valor sem sentido físico, que você pode descartar.

### 2.5 Diagrama de ligação (resumo)

| DHT22        | ESP32 (D1 R32)         |
|---------------|--------------------------|
| VCC           | 3V3                      |
| GND           | GND                      |
| DATA          | GPIO4, **com pull-up de 10 kΩ até o 3V3** |

| Alarme local | ESP32 (D1 R32) |
|---|---|
| Buzzer ativo (+) | GPIO26 |
| Buzzer ativo (−) | GND |
| LED de alarme (ânodo, via resistor ~330 Ω) | GPIO27 |
| LED de alarme (catodo) | GND |

| Divisor VBAT (opcional) | ESP32 (D1 R32) |
|---|---|
| Nó central do divisor (100k/100k) | GPIO34 |
| Extremos do divisor | + da bateria e GND |

**Terra comum antes de tudo** (Aula 03, "Protoboard e as regras da
bancada"): se algum dia você alimentar o DHT22 por uma fonte externa em vez
do 3V3 da própria placa, o GND dessa fonte precisa estar ligado ao GND do
ESP32. Sem isso, o nível lógico do pino de dado deixa de ter uma referência
— o mesmo erro silencioso discutido na Aula 08 para atuadores externos.

---

## 3. Mapa de pinos

Seguindo o formato de mapa de barramento pedido nas Aulas 06 e 08:

| Sinal | GPIO | Rótulo D1 R32 | Barramento/tipo | Observação |
|---|---|---|---|---|
| DHT22 DATA | 4 | A1 | digital, dreno aberto + pull-up | não usado como ADC neste projeto, sem conflito |
| LED de status | 25 | D3 | saída digital | só para bancada; remover/desligar em campo |
| Buzzer do alarme | 26 | D4 | saída digital | ativo em nível alto enquanto o alarme dispara |
| LED do alarme | 27 | D5 | saída digital | distinto do LED de status (Seção 10) |
| VBAT (divisor) | 34 | A2 | ADC1, só entrada | confiável com WiFi ligado (Aula 03) |
| UART0 | 1 / 3 | D0 / D1 | reservado | usado pelo upload e monitor serial — **não ocupar** |

Pinos evitados de propósito, seguindo as "cinco armadilhas de pino" da
Aula 03: `GPIO2` (LED_BUILTIN + strapping de boot), `GPIO12` (strapping
ligado à tensão da flash), e os pinos `A2–A5` que são só entrada (não
serviriam para o pino de dado, que precisa também escrever).

---

## 4. Filtragem de ruído e calibração

### 4.1 Por que o DHT22 muda o problema da Aula 05

A Aula 05 tratou ruído em um **sinal analógico bruto**, lido por um ADC de
propriedade da equipe: resistor, taxa, filtro e conversão eram todos
escolhidos por vocês. O DHT22 é o caso oposto, descrito na Aula 06:

> *"O conversor A/D agora é do sensor. [...] O que se ganha: calibração de
> fábrica, grandeza física direta, sinal que viaja digital. O que se perde:
> o sinal bruto, a taxa de amostragem, o direito de auditar."*

Isso significa duas coisas práticas para este firmware:

- **Não existe "contagem crua" para filtrar.** O DHT22 já entrega
  temperatura em °C e umidade em %RH, calibrados de fábrica.
- **O erro sistemático declarado na folha de dados não é corrigível por
  software.** A Aula 05 já havia adiantado isso: *"erro sistemático não é
  ruído — só calibração contra um padrão resolve, e o padrão tem incerteza
  própria."* Não temos acesso ao chip por dentro para recalibrá-lo; o que
  resta é **declarar** a incerteza do fabricante e deixá-la explícita nos
  dados publicados.

Incerteza declarada pelo fabricante (DHT22, folha de dados):

| Grandeza | Incerteza típica |
|---|---|
| Temperatura | ± 0,5 °C |
| Umidade relativa | ± 2 %RH (± 5 %RH nas extremidades da faixa) |

O firmware imprime essa incerteza junto de cada leitura, para que ela não se
perca entre o sensor e quem consome o dado depois.

### 4.2 O ruído que ainda existe: falha de comunicação, não de sinal

Mesmo com o conversor terceirizado, o barramento de dado do DHT22 pode falhar
por timing (o protocolo é sensível a microssegundos) ou por interferência
elétrica no fio — o análogo, aqui, dos "outliers de comunicação" do I²C na
Aula 06 (endereço que não responde, ACK ausente). O sintoma não é um valor
levemente deslocado: é uma leitura completamente inválida (`NaN`) ou, mais
raramente, um valor absurdo isolado.

Contra isso, o firmware aplica dois filtros com papéis diferentes, seguindo
a disciplina da Aula 05 de que **cada filtro tem um preço declarado**:

**1. Mediana de N amostras (N = 5, ímpar), por ciclo de leitura**
Descarta com eficiência um outlier isolado, sem o efeito de "arrastar" a
média inteira que uma média aritmética teria. Custo: cada amostra do DHT22
exige um intervalo mínimo de 2 s entre leituras (limite do próprio sensor,
não escolha nossa), então 5 amostras custam ~10 s de nó acordado por ciclo —
o preço em latência e em energia que a Aula 05 pede para declarar.

**2. Média exponencial (EMA) entre ciclos de sono**
```
ema_novo = α × amostra_atual + (1 − α) × ema_anterior      (α = 0,30 no firmware)
```
Suaviza a variação natural da grandeza física entre um despertar e o
seguinte, sem herdar o atraso de uma janela longa — e sem exigir guardar um
histórico de N leituras passadas: basta um número por grandeza, guardado em
memória RTC (`RTC_DATA_ATTR`), que sobrevive ao deep sleep.

**Se um ciclo tiver menos da maioria das amostras válidas** (na configuração
padrão, menos de 3 de 5), o firmware **declara falha explicitamente** em vez
de calcular uma mediana pouco confiável a partir de poucos pontos — a mesma
disciplina de "número sem procedência não é número" da Aula 08.

### 4.3 Ajustando os parâmetros de filtro

| Parâmetro | Efeito de aumentar | Efeito de diminuir |
|---|---|---|
| `N_AMOSTRAS` | mais robusto a outlier, mais tempo acordado (mais energia) | menos robusto, ciclo mais curto |
| `EMA_ALPHA` | responde mais rápido a mudanças reais, filtra menos ruído entre ciclos | mais suave, mas atrasa mudanças reais |

---

## 5. Gerenciamento de energia

### 5.1 Arquitetura de sono

O firmware usa **deep sleep** (não modem-sleep nem light-sleep), a mesma
escolha que a Aula 08 recomenda para o caso que dá autonomia de meses:

> *"Deep sleep: quase tudo desliga, e a RAM se perde. Ao despertar, o
> programa recomeça do setup(). É o único dos três que muda a arquitetura
> do firmware — e é o que dá autonomia."*

A única fonte de despertar configurada é o **temporizador**
(`esp_sleep_enable_timer_wakeup`), o caso comum da Aula 08 para "medir a
cada N minutos". Se o seu projeto precisar acordar por um evento externo
(botão, sensor de presença), veja a seção 8 (Extensões).

### 5.2 O que sobrevive ao sono, e o que não sobrevive

| Variável | Atravessa o deep sleep? | Por quê |
|---|---|---|
| `bootCount`, `emaTemp`, `emaHum`, `falhasConsecutivas` | Sim | declaradas com `RTC_DATA_ATTR` |
| Qualquer variável local do `setup()` | Não | RAM comum é zerada a cada despertar (Aula 08, Demonstração 03) |

### 5.3 Orçamento de energia — como preencher com números reais

O firmware imprime, no primeiro boot, uma **estimativa inicial** de consumo
diário para o cenário do ciclo que acabou de rodar (normal ou de alarme —
Seção 10), seguindo os quatro passos da Aula 08 ("Bateria e painel em quatro
passos"). Os valores de corrente usados vêm marcados por origem, exatamente
como a oficina da Aula 08 pede:

| Estado | Corrente usada no cálculo | Origem |
|---|---|---|
| ESP32 acordado, sem rádio | 45 mA | folha de dados do ESP32 |
| DHT22 em leitura | 1,5 mA | folha de dados do DHT22 |
| Alarme ativo (buzzer + LED) | 35 mA | **estimativa — meça buzzer + LED juntos na bancada** |
| Placa em deep sleep | 2,0 mA | **estimativa — meça na sua placa específica** |

Como o intervalo agora é configurável (Seção 9), o consumo diário
**depende do cenário de uso**: um ambiente configurado para checar a
cada 5 min consome mais que um configurado para 30 min, e um nó que
está disparando alarme com frequência (por estar preso fora da faixa)
consome mais que um em operação normal, porque passa a acordar no
intervalo mais curto de alarme.

**Antes de fechar o orçamento real do projeto**, siga a Demonstração 04 da
Aula 08: meça na bancada, com multímetro em série, pelo menos:

1. Corrente do nó acordado, sem rádio (só lendo o DHT22)
2. Corrente do nó dormindo (deep sleep)
3. Corrente do buzzer + LED de alarme juntos, durante o disparo
4. Se o seu projeto publica por WiFi: corrente durante a associação e envio
   (tipicamente a linha mais cara da tabela, e a que "ninguém consegue
   estimar bem sem medir" — Aula 08)

Depois de medir, atualize as constantes `I_ACORDADO_SEM_RADIO_mA`,
`I_DHT22_ATIVO_mA`, `I_ALARME_ATIVO_mA`, `I_DEEP_SLEEP_PLACA_mA` e
`T_ACORDADO_S` no topo do firmware com os valores reais.

### 5.4 Dimensionando a bateria (passo 3 da Aula 08)

Com o consumo diário real em mãos (`cargaDiariaMah`, impresso pelo
firmware):

```
Autonomia (dias) = (Capacidade da bateria × Profundidade de descarga útil) / Consumo diário (mA·h)
```

Exemplo com uma 18650 de 2600 mA·h e 80% de profundidade de descarga útil
(mesmo exemplo numérico da Aula 08): capacidade útil ≈ 2080 mA·h.

Se o consumo diário for baixo (nó só medindo e publicando, sem atuador
ligado o tempo todo), a Aula 08 já adianta a conclusão que costuma se
confirmar aqui: **quem dimensiona o sistema é a corrente de repouso da
placa, não a leitura do DHT22** — o sensor é, de longe, a linha mais barata
da tabela.

### 5.5 Ajustando o intervalo entre leituras

O intervalo entre leituras é, de longe, o parâmetro que mais afeta o
orçamento de energia — e agora é configurável **sem recompilar**, pela
NVS (Seção 9). O padrão de fábrica é de 10 min (`PADRAO_INTERVALO_NORMAL_S`),
por ser o caso fechado no relatório do projeto; cada instalação real
deve ajustar esse valor à criticidade do ambiente:

| Intervalo | Ciclos/dia | Efeito |
|---|---|---|
| 1 min | 1440 | leitura quase contínua, consumo diário alto |
| 10 min (padrão de fábrica deste firmware) | 144 | caso de teste do projeto |
| 15 min | 96 | equilíbrio comum para monitoramento ambiental |
| 1 hora | 24 | autonomia muito maior, granularidade temporal menor |

Além do intervalo normal, existe um segundo parâmetro,
`PADRAO_INTERVALO_ALARME_S` (padrão: 1 min), usado **só enquanto a
leitura está fora da faixa configurada** — ver Seção 10. Compare
cenários diferentes antes de fechar o projeto, incluindo o custo de um
alarme que dispara com frequência (mais ciclos curtos por dia).

---

## 6. Instalação e dependências

Seguindo a ordem recomendada pela Aula 03 ("driver, IDE, core — nessa
ordem"):

1. **Arduino IDE 2.x** instalada, com o driver USB-serial da sua placa
   (CH340 ou CP2102) e o core do ESP32 já configurados (ver Aula 03, se
   ainda não tiver feito esse passo).
2. No **Gerenciador de Bibliotecas**, instale:
   - `DHT sensor library` (Adafruit)
   - `Adafruit Unified Sensor` (dependência da anterior)
3. Selecione a placa correta em `Ferramentas → Placa → ESP32 → WEMOS D1 R32`
   (ou o board correspondente ao seu hardware).
4. Grave `esp32_dht22.ino` e abra o Monitor Serial em **115200 baud**.

---

## 7. Diagnóstico de falhas

No estilo das tabelas de diagnóstico das Aulas 03/06/08 — pelo sintoma, não
pelo componente que parece óbvio:

| Sintoma | Causa mais provável | Correção |
|---|---|---|
| `lerDHTFiltrado` sempre retorna falha | Pull-up ausente no pino de dado | Confirme os 10 kΩ entre DATA e 3V3 |
| Leituras plausíveis, mas travam sempre no mesmo valor | Fio de dado mal contatado na protoboard | Refaça a ligação antes de suspeitar do código (Aula 03) |
| Placa reinicia sozinha ao energizar | Fonte USB fraca alimentando outra coisa junto | Verifique se nada mais está puxando corrente do 3V3 (Aula 03) |
| Leituras "quase certas", mas deslocadas de um valor fixo | Erro sistemático do sensor (não corrigível por filtro) | Compare contra um sensor de referência; é um problema de calibração, não de ruído (Aula 05) |
| `VBAT` sempre em torno de metade da tensão esperada | Divisor mal montado ou R1/R2 diferentes do firmware | Confira os resistores e ajuste `VBAT_R1_OHMS`/`VBAT_R2_OHMS` |
| Monitor serial mostra caracteres sem sentido | Baud do monitor diferente de 115200 | Ajuste o monitor serial para 115200 (Aula 03) |
| Nó nunca acorda de novo | `esp_sleep_enable_timer_wakeup` não foi chamado antes do `esp_deep_sleep_start()` | Confirme a ordem das chamadas em `dormir()` |

---

## 8. Extensões sugeridas

Este firmware é uma base intencionalmente enxuta. Extensões coerentes com o
que as próprias aulas descrevem como próximo passo:

- **Publicação via WiFi (MQTT/HTTP)**: adicionar a conexão e o envio dentro
  do bloco `if (leituraOk)` do `setup()`, **antes** de chamar `dormir()`.
  Meça o custo de corrente dessa etapa separadamente — é a linha mais cara
  do orçamento, segundo a Aula 08.
- **Gravação em cartão SD**: usar o barramento SPI (Aula 06), seguindo o
  padrão `SD.begin(PINO_CS)` + `flush()` periódico visto na Demonstração 03
  da Aula 06, para não perder dados se o nó reiniciar.
- **Despertar por evento**, além do temporizador: usar um pino RTC
  (`esp_sleep_enable_ext0_wakeup`), útil se, por exemplo, um botão de
  calibração manual precisar acordar o nó fora do ciclo programado
  (Aula 08, "Despertar").
- **Multiplexação de vários DHT22**: como o DHT22 não tem endereço
  configurável (ao contrário do I²C, Aula 06), cada sensor adicional exige
  um pino de dado próprio — não há como colocar dois no mesmo fio.
- **Alarme contínuo durante o deep sleep**: usar `gpio_hold_en()` sobre
  um pino RTC para manter o buzzer/LED ligados durante o sono, em vez
  do pulso de alguns segundos por ciclo descrito na Seção 10.1 —
  compromete a autonomia de bateria enquanto o alarme estiver ativo,
  então vale medir o impacto antes de adotar.

---

## 9. Configuração em tempo de execução

### 9.1 Por que configuração, e não mais um `#define`

A primeira versão deste firmware fixava o intervalo de leitura em
`#define`. Isso funciona para um único cenário, mas o projeto do curso
generalizou o produto para **ambientes de criticidade diferente** — uma
câmara fria de farmácia pode exigir checagem a cada poucos minutos,
enquanto uma sala de TI tolera um intervalo maior. Recompilar o
firmware para cada instalação não escala; por isso o intervalo e os
limiares de alarme agora são **parâmetros gravados na NVS** (memória
não-volátil do ESP32), através da biblioteca `Preferences`.

**Isto não contradiz a decisão de não persistir dados de leitura**
(Capítulo 1 do relatório do projeto): o que é gravado aqui é
*parâmetro de configuração* (um intervalo, um limiar), não uma série
temporal de medições. A NVS guarda alguns números fixos até que
alguém os troque; não guarda histórico.

### 9.2 Parâmetros configuráveis

| Parâmetro | Comando | Padrão de fábrica |
|---|---|---|
| Intervalo normal entre leituras | `SET INTERVALO=<segundos>` | 600 s (10 min) |
| Intervalo enquanto em alarme | `SET INTERVALO_ALARME=<segundos>` | 60 s (1 min) |
| Limite inferior de temperatura | `SET TEMP_MIN=<°C>` | 15 °C |
| Limite superior de temperatura | `SET TEMP_MAX=<°C>` | 25 °C |
| Limite inferior de umidade | `SET UMID_MIN=<%RH>` | 40 %RH |
| Limite superior de umidade | `SET UMID_MAX=<%RH>` | 70 %RH |

Os limiares padrão são os do cenário de teste do projeto (farmácia de
manipulação). **Cada instalação real deve ajustar esses valores** ao
ambiente monitorado — é exatamente o requisito RF-02 do relatório:
faixa configurável por ambiente, não fixa em firmware.

### 9.3 Como configurar

Nos primeiros ~4 segundos após ligar (monitor serial em 115200 baud),
o firmware aguarda comandos, um por linha:

```
SHOW
SET INTERVALO=300
SET TEMP_MIN=2
SET TEMP_MAX=8
SHOW
```

Cada `SET` aceito é gravado imediatamente na NVS e a janela de espera
se renova por mais ~4 s, para permitir enviar vários comandos em
sequência sem correr contra o relógio. `RESET` volta aos padrões de
fábrica listados acima. Se nada for digitado dentro da janela, o nó
segue com a última configuração gravada (ou os padrões, no primeiro
boot) — em campo, sem ninguém no monitor serial, o comportamento é o
mesmo de antes: o ciclo roda sozinho.

### 9.4 Limitação conhecida

A janela de configuração exige conexão USB/serial ativa no momento do
boot — não há, neste ciclo, forma remota de reconfigurar um nó já
instalado em campo (isso dependeria da camada de rede do Módulo 2).
Para trocar a configuração de um nó em produção hoje, é preciso
acessá-lo fisicamente.

---

## 10. Alarme local

### 10.1 O que aciona, e quando desliga

A cada ciclo com leitura válida, o firmware compara a mediana filtrada
de temperatura e umidade contra a faixa configurada (Seção 9). Se
qualquer uma das duas grandezas estiver fora da faixa, o nó:

1. liga o buzzer ativo (GPIO26) e o LED de alarme (GPIO27);
2. mantém os dois ligados por `ALARME_DURACAO_MS` (padrão: 3 s);
3. desliga os dois antes de dormir.

Não existe um terceiro estado "alarme ligado durante o sono": o
`deep sleep` desliga essencialmente tudo, e manter um GPIO ligado
durante o sono exigiria `gpio_hold_en()` sobre um pino RTC, o que este
firmware-base não faz (ver 10.3). O alarme, portanto, é um pulso de
alguns segundos a cada ciclo em que a leitura está fora da faixa — não
um som contínuo.

### 10.2 Por que o intervalo muda quando o alarme dispara

Um pulso de alguns segundos a cada 10 minutos poderia passar
despercebido em um ambiente ruidoso. Por isso, **enquanto a última
leitura estiver fora da faixa, o nó passa a acordar no intervalo de
alarme** (padrão: 1 min) em vez do intervalo normal — o alarme soa com
mais frequência até a condição ser corrigida, e o nó volta sozinho ao
intervalo normal assim que uma leitura voltar a ficar dentro da faixa.
Essa é uma decisão de compromisso entre energia e resposta: acordar
mais vezes custa mais bateria (Seção 5.3), mas é aceitável porque, por
definição, isso só acontece enquanto algo está errado — não é o
regime de operação esperado do dia a dia.

### 10.3 Limitações conhecidas

- **O alarme não soa continuamente**, só em pulsos a cada ciclo (ver
  10.1). Para um alarme verdadeiramente contínuo seria necessário
  manter o nó fora de deep sleep (outro perfil de energia,
  incompatível com a autonomia de bateria que motivou o deep sleep em
  primeiro lugar) ou usar `gpio_hold_en()` para reter o nível do pino
  durante o sono — não implementado nesta revisão.
- **A corrente do alarme (`I_ALARME_ATIVO_mA`) é uma estimativa**,
  ainda não medida em bancada com o buzzer e o LED específicos do
  protótipo. Meça antes de fechar o orçamento de energia definitivo.
- Como neste ciclo não há rede (Módulo 2), **um alarme disparado só é
  percebido por quem está fisicamente perto do nó** — não há
  notificação remota ainda.

---

## 11. Referências

- ESPRESSIF SYSTEMS. *ESP32 series datasheet*. Rev. 4.9. Xangai, 2025.
- ESPRESSIF SYSTEMS. *ESP-IDF Programming Guide — Sleep Modes e RTC GPIO*. 2025.
- AOSONG (GUANGZHOU) ELECTRONICS. *DHT22/AM2302 digital-output relative
  humidity & temperature sensor — datasheet*.
- Aula 03 — *Eletrônica para prototipagem e a plataforma ESP32*, UFPI,
  19/08/2026.
- Aula 05 — *Ruído e calibração*, UFPI, 26/08/2026.
- Aula 06 — *Você, agora: barramentos e interfaces de comunicação local*,
  UFPI, 31/08/2026.
- Aula 08 — *O pino decide, mas não paga: atuadores e gerenciamento de
  energia*, UFPI, 09/09/2026.
