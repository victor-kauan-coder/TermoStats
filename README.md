# TermoStats — teste de BLE

Branch `teste-ble`: alcance e desempenho do BLE entre um nó sensor (ESP32 +
DHT22) e um gateway (ESP32 ligado ao notebook). É o mesmo teste da branch
`teste-esp-now` (mesmo pacote de 25 bytes, 10 pacotes/s, 60 s por situação),
para os dois rádios serem comparados nas mesmas distâncias.

```
firmware/
├── comum/pacote.h     formato do pacote e UUIDs, usados pelos dois lados
├── sensor/            periférico BLE: lê o DHT22 e notifica 10 pacotes/s
└── gateway/           central BLE: conecta, confirma cada pacote e repassa pela serial
gravador/
├── gravador.py        testes de 60 s nomeados pela distância
└── test_gravador.py
```

## Como o BLE é usado

- **Conexão GATT.** O sensor anuncia o serviço e o gateway conecta. O sensor
  envia cada pacote por **notify**; o gateway devolve um ACK escrevendo na
  característica de ACK (write sem resposta). O sensor mede a ida e volta e a
  manda no pacote seguinte, como no teste de ESP-NOW.
- **Potência máxima do BLE (+9 dBm) nos dois lados**, para dar ao BLE a melhor
  chance na comparação.
- **MTU de 64.** O padrão do BLE só carrega 20 bytes por notificação, e o
  pacote tem 25. O gateway informa o MTU negociado no evento `conectado`.

**Diferença importante em relação ao ESP-NOW:** dentro de uma conexão BLE, a
camada de enlace retransmite até conseguir. O BLE não perde pacotes avulsos; ele
perde a **conexão** (quando o sinal fica ruim por tempo demais). Por isso o
sensor continua numerando os pacotes mesmo desconectado, e a perda aparece como
o intervalo em que não havia conexão. O gravador mostra também quantas vezes a
conexão caiu e quanto tempo ficou fora.

## Gravar as placas

DHT22 no GPIO4 do sensor (3V3, GND e dado). O pio não está no PATH desta
máquina; no PowerShell:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
cd firmware\sensor;  & $pio run -t upload --upload-port COMx   # placa do sensor
cd ..\gateway;       & $pio run -t upload --upload-port COMy   # placa do gateway
```

Se o upload parar em `Connecting....`, segure o botão BOOT até começar a gravar.

Para conferir: `& $pio device monitor -p COMy` no gateway deve mostrar
`@EVT tipo=conectado ... mtu=64*XX` e depois linhas `@PKT ...`, cerca de 10 por
segundo. Feche o monitor (Ctrl+C) antes de rodar o gravador.

## Rodar os testes

```powershell
cd gravador
python gravador.py --listar          # descobre a COM do gateway
python gravador.py --porta COMy      # pede a distância, grava 60 s, mostra o resultado
```

Para cada teste, o gravador mostra:

- **entrega:** pacotes recebidos pelo gateway em relação aos 600 que o sensor
  gera em 60 s. Inclui quedas de conexão no começo ou no fim do teste;
- **perda pelo seq:** esperados do primeiro ao último `seq` da janela, contra o
  que o contador do gateway recebeu;
- **conexão:** quantas vezes caiu e quanto tempo ficou sem conexão;
- **latência:** RTT/2, mediana, p95 e máximo;
- **taxa de dados:** pacotes/s, B/s e bit/s de carga útil;
- **RSSI:** amostrado uma vez por segundo no gateway;
- **serial:** linhas rejeitadas pelo checksum (não contam como perda).

Arquivos em `gravador/dados/`: `<teste>.log`, `<teste>.csv` e `resumo.csv`.

## Proteções contra perda falsa

Na bateria de ESP-NOW de 24/10, o gravador contou como perda de rádio dois
defeitos da serial (ver `testes_24-10-26/README.md` na branch `teste-esp-now`).
Aqui eles não acontecem:

| Defeito em 24/10 | Correção nesta versão |
|---|---|
| Linha velha no buffer da porta entre um teste e outro inflava os "esperados" | O gravador lê a serial o tempo todo, numa thread, e carimba a hora de chegada de cada linha. O teste usa só o que chegou dentro da janela de 60 s. |
| Byte perdido virava um `seq` errado (`4165` → `465`) | Toda linha do gateway termina em `*XX`, um checksum. Linha corrompida é descartada. |
| Linha descartada virava perda | O gateway mantém um contador `rx` de pacotes recebidos. A perda é calculada por ele, então o pacote de uma linha descartada continua contado como recebido. |

```powershell
python -m unittest test_gravador     # 19 testes, incluindo os dois defeitos de 24/10
```
