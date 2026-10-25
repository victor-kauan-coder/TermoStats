# TermoStats · entrega da AP2 (equipe 01): nó MQTT e conjunto de dados

Commit da entrega: o marcado com a tag `ap2` na branch `entrega-ap2` (`git checkout ap2`).
Ele parte do commit `7b4482e` da branch `lab3`, o que rodou nos testes de falha e na
campanha de 24/10/2026, e acrescenta as fontes do relatório e a integração com o servidor
do produto.

Um nó ESP32 com DHT22 publica temperatura e umidade por MQTT. Um gravador no computador
assina os tópicos da equipe e guarda tudo num CSV. Com esses dados, a equipe provoca
falhas de propósito (testes mínimos da AP2) e roda uma campanha de coleta. Os scripts dão
a completude, as lacunas e a série temporal.

```
firmware/
├── sketch/sketch.ino       firmware do nó (o mesmo no Wokwi e na placa; DHT22 no GPIO4)
├── sketch/secrets.example.h  modelo do secrets.h (o secrets.h não vai para o git)
├── platformio.ini          compila e grava o sketch na placa
└── diagram.json            circuito do Wokwi (DHT22 no GPIO15: ver "Montar o nó")
gravador/
├── gravador.py             assina os tópicos e grava dados/mensagens.csv
├── qualidade.py            recebidas, faltando, repetidas, completude e lacunas
└── grafico.py              série temporal com as lacunas visíveis
dados/
├── testes_falha_24-10-26/  testes mínimos da AP2 (CSV, saída do qualidade.py, eventos)
└── campanha_24-10-26/      campanha (CSV bruto, saída do qualidade.py, gráfico, resultados.md)
integracao/
├── ponte_api.py            leva o CSV da campanha ao servidor do produto, em lotes (CT-16)
├── resultado_25-10-26.md   resultado do teste da ponte contra a API
└── conta_reconexoes.py     tempo de reconexão contado no CSV (CT-09)
docs/                       fontes LaTeX do relatório técnico da AP2 (as que geraram o PDF)
```

A pasta `firmware/` é a `wokwi/` da branch `lab3`, renomeada como pedem as instruções da
AP2, sem mudança no conteúdo. O conjunto de dados é o CSV bruto do gravador, sem edição e
sem versão limpa.

As outras medições citadas no relatório ficam nas branches de trabalho, cada uma com o
próprio README e os dados brutos:

| Branch | Commit | O que tem |
|---|---|---|
| `apendice_b` | `f30a6de` | ferramenta da Aula 10: tempo do ciclo de rádio (Apêndice B), política de erro HTTP (CT-17) |
| `teste-esp-now` | `affb704` | bateria de alcance e desempenho do ESP-NOW, com o DHT22 no pacote |
| `teste-ble` | `f149308` | a mesma bateria com BLE |
| `victor#sensores` | `c3642d4` | firmware da AP1 (`esp32_dht22.ino`): filtragem, alarme e deep sleep |

## Metodologia

### O sistema

```
DHT22 ──GPIO4── ESP32 ──WiFi/TLS──> broker.hivemq.com:8883 ──TLS──> gravador.py ──> mensagens.csv
```

- **Nó:** a cada `periodo_s` (5 s), lê o DHT22 e publica um JSON com QoS 1 e sem reter:
  `{"id":"eq01-no02","seq":N,"ts":<epoch>,"temp_c":T,"umid_pct":U}`. Se a leitura do DHT22
  falhar, envia `rssi_dbm` no lugar das grandezas.
- **Tópicos** (árvore `ufpi-iot26/eq01/#`):

  | Tópico | Quem publica | QoS | Retido |
  |---|---|---|---|
  | `ufpi-iot26/eq01/no02/telemetria` | o nó | 1 | não |
  | `ufpi-iot26/eq01/no02/status` | o nó (`online`) e o broker pelo testamento (`offline`) | 1 | sim |
  | `ufpi-iot26/eq01/no02/config` | quem quiser mudar o período: `{"periodo_s":N}` | 1 | — |

- **Conexão:** testamento `offline` retido, sessão persistente e keepalive de 30 s. Sem
  rede, o nó tenta o broker de novo a cada 5 s. O relógio vem do NTP; antes de ele acertar,
  `ts` sai `null`.
- **Gravador:** cliente MQTT com **sessão persistente**. Se ele parar, o broker guarda as
  mensagens de QoS 1 e entrega na volta. Cada linha do CSV recebe a hora de chegada
  (`t_chegada`), além do `ts` da medição que vem no payload.
- **Broker:** público, sem login. Qualquer um pode ler e publicar nos mesmos tópicos.

### Como a qualidade é medida

- **Leituras esperadas saem de uma conta, não do arquivo:**
  `(fim − início) / periodo_s + 1`. A **completude** é o número de leituras únicas
  dividido pelas esperadas.
- **O `seq` sozinho não basta.** Desconectado, o nó não mede, então o `seq` não pula. Só
  a conta pelo tempo mostra essas perdas. O `seq` detecta publicações que falharam,
  repetições e reinícios (quando volta a 1).
- **Lacuna** é qualquer intervalo entre medições maior que 1,5 × `periodo_s`. O
  `qualidade.py` lista as maiores, e o `grafico.py` corta a linha nelas: **nada é
  interpolado**.
- **Atraso** é a diferença `t_chegada − ts`, do sensor ao CSV.
- **Registro de eventos:** tudo o que acontece no ambiente ou no sistema durante a coleta
  é anotado com a hora (ar-condicionado, quedas provocadas etc.). É ele que explica as
  lacunas e as mudanças na série.

### Testes mínimos da AP2

| Teste | Como provocar | O que conferir |
|---|---|---|
| Queda do WiFi | desligar o roteador por ~2 min | o nó reconecta sozinho; a duração da lacuna (não há fila local) |
| O nó morre sem avisar | tirar a alimentação da placa | o `offline` do testamento e quanto tempo ele levou (esperado ≤ 45 s) |
| O nó reinicia | botão EN | `seq` volta a 1; nenhuma leitura faltando ou repetida no `qualidade.py` |
| O gravador para | Ctrl+C no gravador por alguns minutos e religar | o backlog chega junto, com os `ts` originais, sem buraco no `seq` |
| Antes do NTP | religar o nó e olhar as primeiras leituras | quantas saem com `ts` nulo |
| Longa duração | a própria campanha | completude, lacunas e o que as explicou |

## Passo a passo

### 1. Preparar (uma vez)

- PlatformIO instalado (extensão do VS Code). Fora do PATH, o executável fica em
  `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`.
- Python 3 com as bibliotecas:
  ```powershell
  pip install paho-mqtt matplotlib
  ```
- Copiar `firmware/sketch/secrets.example.h` para `firmware/sketch/secrets.h` e preencher o
  WiFi. O ESP32 só conecta em rede de **2,4 GHz**. O broker já vem configurado
  (`broker.hivemq.com`, porta 8883, sem usuário).
- Em `firmware/sketch/sketch.ino`, conferir `EQUIPE = "eq01"` e `NO = "no02"` para a placa.
  No Wokwi, use `no01`: nunca rode os dois com o mesmo `NO`.

### 2. Montar o nó

DHT22 ligado ao ESP32: VCC no **3V3**, GND no **GND** e dado no **GPIO4**, o pino que o
`sketch.ino` lê (`PINO_DHT = 4`) e o mesmo da AP1. O sensor solto, de 4 pinos, precisa de
um resistor de 10 kΩ entre o dado e o 3V3; o módulo de 3 pinos já tem o resistor.

O `diagram.json` do Wokwi liga o DHT22 ao GPIO15. Para simular, troque `esp:15` por
`esp:4` no `diagram.json`; como está, o DHT22 não é lido no Wokwi.

### 3. Gravar e conferir o nó

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
cd firmware
& $pio device list                               # descobre a porta (ex.: COM3)
& $pio run -t upload --upload-port COM3          # se parar em "Connecting....", segure o BOOT
& $pio device monitor -p COM3                    # 115200; Ctrl+C para sair
```

O monitor deve mostrar, nesta ordem: `[wifi] conectado`, `[mqtt] conectado`,
`status = online` e uma linha `[pub] ... {"temp_c":...}` a cada 5 s. Se aparecer
`rssi_dbm` em vez de `temp_c`, a leitura do DHT22 falhou: confira as ligações.

### 4. Ligar o gravador

Num terminal separado, que fica aberto durante todo o teste:

```powershell
cd gravador
python gravador.py --host broker.hivemq.com --equipe eq01
```

Ele deve mostrar `[mqtt] conectado` e as mensagens chegando. Tudo vai para
`gravador/dados/mensagens.csv`. Não use `--sessao-limpa`: é a sessão persistente que
permite o teste "o gravador para". Deixe o computador sem suspender.

Para ver as mensagens ao vivo por outro programa, use o MQTT Explorer: `broker.hivemq.com`,
porta 8883 com TLS, sem login, assinando `ufpi-iot26/eq01/#`.

### 5. Testes de falha

Com o nó e o gravador rodando, provoque cada falha da tabela de testes mínimos, **uma de
cada vez**, e **anote a hora** de cada ação. Espere o nó voltar a publicar normalmente
antes de passar ao teste seguinte. No fim, pare o gravador (Ctrl+C) e guarde o resultado
numa pasta própria:

```powershell
mkdir ..\dados\testes_falha_DD-MM-AA
move dados\mensagens.csv ..\dados\testes_falha_DD-MM-AA\
python qualidade.py ..\dados\testes_falha_DD-MM-AA\mensagens.csv > ..\dados\testes_falha_DD-MM-AA\qualidade_saida.txt
```

Mover o CSV faz o gravador começar um arquivo novo para a campanha.

### 6. Campanha

1. Instale o nó no lugar definitivo e anote onde ele ficou (cômodo, móvel, distância do
   ar-condicionado e de janelas).
2. Ligue o gravador (passo 4) e **anote a hora de início**.
3. Deixe rodando sem mexer. Anote com a hora tudo o que mudar no ambiente: ar-condicionado,
   janela, pessoas entrando.
4. Para encerrar, pare o gravador com Ctrl+C e **anote a hora de fim**.

### 7. Gerar os resultados

```powershell
cd gravador
mkdir ..\dados\campanha_DD-MM-AA
move dados\mensagens.csv ..\dados\campanha_DD-MM-AA\
python qualidade.py ..\dados\campanha_DD-MM-AA\mensagens.csv > ..\dados\campanha_DD-MM-AA\qualidade_saida.txt
python grafico.py ..\dados\campanha_DD-MM-AA\mensagens.csv -o ..\dados\campanha_DD-MM-AA\serie_temporal.png
```

Opções do `qualidade.py`:
- `--de HH:MM --ate HH:MM` calcula a completude na janela em que o nó **deveria** estar
  publicando. Use a janela anotada, e não a do arquivo, para contar também as pontas.
  O filtro compara só a hora: se a coleta virar a meia-noite, rode uma vez para cada dia.
- `--periodo N` serve se o período foi mudado pelo tópico `config`.

Com a saída do `qualidade.py`, o gráfico e o registro de eventos, escreva o
`resultados.md` da pasta. O exemplo é o de
[dados/campanha_24-10-26/resultados.md](dados/campanha_24-10-26/resultados.md).

## Resultados de 24/10/2026

- **Testes de falha** ([dados/testes_falha_24-10-26/](dados/testes_falha_24-10-26/)):
  - o testamento publicou `offline` 46 s depois de a alimentação ser cortada;
  - o backlog do gravador foi entregue sem buraco;
  - nenhuma leitura saiu com `ts` nulo;
  - a queda de WiFi de 101 s perdeu ~19 leituras, e o `seq` só apontou 1.
- **Campanha** ([dados/campanha_24-10-26/](dados/campanha_24-10-26/)): 2 h 47 min com
  completude de **63,1%**.
  - Houve duas fases estáveis (~96%) e uma degradada, das 16:30 às 18:10 (40%), com o nó
    reconectando a cada poucos segundos.
  - A temperatura foi de 24,0 a 30,7 °C depois que o ar-condicionado foi desligado, às 16:35.
  - Nas 553 quedas da conexão com o broker, o nó voltou sozinho em 3,5 s na mediana, e
    552 delas em até 30 s:
    ```powershell
    python integracao\conta_reconexoes.py dados\campanha_24-10-26\mensagens.csv --lote 16:10:14 16:10:17
    ```

## Limitações conhecidas

- **Sem fila local:** o nó não mede enquanto está desconectado, e essas leituras são
  perdidas de vez.
- **Timeout curto:** a biblioteca MQTT espera só 1 s pela resposta do broker e, se o tempo
  estoura, derruba a conexão. É a causa provável da fase degradada da campanha. Ainda é
  hipótese, não está confirmada.
- **Broker público:** não tem autenticação, e o TLS do nó usa `setInsecure()`, ou seja, não
  confere o certificado do servidor.
