# Resultados — primeira bateria de BLE, 24/10/2026

Alcance e desempenho do BLE entre um nó sensor (ESP32 + DHT22) e um gateway
(ESP32 no notebook), nas mesmas posições da bateria de ESP-NOW feita na mesma
manhã (branch `teste-esp-now`, pasta `testes_24-10-26`). Mesmo pacote de 25
bytes, 10 pacotes/s, 60 s por situação, das 12:35 às 12:53.

## Como o teste foi feito

- **Sensor:** periférico BLE, fixo no desktop. Envia cada pacote por notify,
  a +9 dBm (o máximo do BLE no ESP32).
- **Gateway:** central BLE no notebook, que se deslocava. Também a +9 dBm.
  Devolve um ACK por pacote, para a medida de ida e volta.
- **Conexão GATT:** dentro dela, a camada de enlace do BLE retransmite até
  conseguir. Por isso o sensor continua numerando os pacotes mesmo quando não
  consegue enviá-los, e o buraco no `seq` é a perda.

## Arquivos desta pasta

```
teste_<situação>.log   o que o gateway imprimiu dentro da janela de 60 s
teste_<situação>.csv   um pacote válido por linha
resumo.csv             o resumo do gravador (faltam duas linhas, ver abaixo)
resultados.md          este arquivo
```

## A serial ficou limpa

As correções feitas depois da bateria de ESP-NOW funcionaram. Nos 6 testes
com conexão, o gravador **não rejeitou nenhuma linha** pelo checksum, não houve
salto de `seq` nem de `rx` causado pela serial, e a primeira linha de cada
janela já é contínua com as seguintes, ou seja, não sobrou linha velha no
buffer. Todos os números abaixo vêm
direto do gravador, sem a reanálise que a bateria de ESP-NOW precisou.

## Resultados

| Situação | Pacotes entregues | Desconexões | RSSI médio / p10 (dBm) | Latência RTT/2 mediana / p95 / máx (ms) |
|---|---:|---:|---:|---:|
| 1,5 m | **600 de 600 (100 %)** | 0 | -72,0 / -77 | 16,1 / 22,7 / 41,2 |
| 3 m | **600 de 600 (100 %)** | 0 | -73,9 / -82 | 15,3 / 22,2 / 41,2 |
| 5 m | **600 de 600 (100 %)** | 0 | -81,2 / -89 | 16,1 / 24,9 / 37,9 |
| 5 m, 2 portas de madeira | **600 de 600 (100 %)** | 0 | -82,1 / -86 | 16,0 / 21,5 / 33,5 |
| 8 m, 2 paredes | **600 de 600 (100 %)** | 0 | -89,6 / -93 | 16,8 / 26,4 / 36,7 |
| 11 m, 3 paredes | **239 de 600 (39,8 %)** | 0 | -97,8 / -101 | **3.881 / 5.966 / 6.388** |
| 18 m, várias paredes | **0** | sem conexão desde antes do teste | — | — |
| 13 m, 4 paredes | **0** | sem conexão desde antes do teste | — | — |
| avulso | **0** | sem conexão desde antes do teste | — | — |
| avulso 2 | **600 de 600 (100 %)** | 0 | -69,5 / -76 | 15,9 / 22,0 / 32,6 |

Os testes de 13 m e "avulso" não estão no `resumo.csv`. Os arquivos deles
existem e estão vazios: o gravador provavelmente foi interrompido (Ctrl+C)
durante esses dois testes, e por isso não chegou a gravar a linha de resumo.
O resultado é o mesmo do de 18 m: nenhuma linha do gateway em 60 s.

## O que os dados mostram

### Até 8 m, o BLE é perfeito, mas 8 vezes mais lento que o ESP-NOW
Nenhuma perda até 8 m com paredes. A latência mediana fica em torno de 16 ms
(ida e volta de cerca de 32 ms), contra 1,9 ms do ESP-NOW nas mesmas posições.
A ida e volta nunca foi menor que 23 ms, nem a 1,5 m. Isso indica que a
latência vem do **intervalo de conexão** do BLE, não da distância: a
notificação espera o próximo evento de conexão para sair, e o ACK espera o
seguinte. O intervalo não foi medido diretamente, mas os números são
compatíveis com um intervalo na casa de 20 a 30 ms.

### A 11 m com 3 paredes, o BLE não perde a conexão: ele congestiona
Não houve nenhuma desconexão, e mesmo assim só 40 % dos pacotes chegaram,
com **4 a 6 segundos de atraso** (RTT/2). O que aconteceu, pelos dados:

1. Com o sinal no limite (RSSI médio de -98 dBm), a camada de enlace passou a
   retransmitir cada pacote muitas vezes. A vazão real caiu para uns 4
   pacotes/s.
2. O sensor continuou gerando 10 por segundo. Os pacotes se acumularam na fila
   do Bluetooth e passaram a esperar segundos para sair: a ida e volta chegou a
   12,8 s.
3. Com a fila cheia, o sensor passou a recusar pacotes novos. As falhas
   contadas por ele foram exatamente **335, o mesmo número de pacotes que não
   chegaram**.

Para monitoramento de temperatura isso é pior do que perder pacotes: o que
chega é velho, e o gateway mostra uma leitura de vários segundos atrás como
se fosse atual.

### A 18 m e a 13 m, não houve enlace, e a queda aconteceu fora dos testes
Os testes de 18 m, 13 m e o primeiro avulso não registraram nenhuma linha,
nem mesmo o evento de desconexão. A ordem dos fatos:

- **A conexão caiu entre o fim do teste de 11 m (12:47) e o começo do de 18 m
  (12:48),** provavelmente enquanto o notebook era levado até a nova posição.
- **O gateway imprimiu `@EVT tipo=desconectado` naquele momento, fora da
  janela.** O gravador descarta tudo o que chega fora da janela de 60 s; é a
  proteção que eliminou a perda falsa do ESP-NOW. Esse efeito colateral não
  estava previsto.
- **Desconectado, o gateway fica procurando o sensor em silêncio.** Por isso o
  log de 60 s ficou vazio, sem nenhum indício de que não havia conexão.
- **Entre o primeiro avulso e o avulso 2, as duas placas reiniciaram.** O `seq`
  do sensor e o `rx` do gateway recomeçaram de perto de zero. A conexão do
  avulso 2 (perto do gateway, RSSI de -69,5 dBm) foi feita antes desse teste
  começar.

O resultado (0 % de entrega a 13 m e 18 m) está certo. O que faltou foi o
registro de *por que* não chegou nada, e isso precisa de correção no código
(ver "Próximos passos").

## Comparação com o ESP-NOW

### Mesmas posições, lado a lado

| Posição | ESP-NOW: obstáculos anotados | ESP-NOW: perda real | ESP-NOW: RSSI médio | ESP-NOW: RTT/2 mediana | BLE: obstáculos anotados | BLE: entrega | BLE: RSSI médio | BLE: RTT/2 mediana |
|---|---|---:|---:|---:|---|---:|---:|---:|
| 1,5 m | nenhum | 0 % | -63,2 | 1,9 ms | nenhum | 100 % | -72,0 | 16,1 ms |
| 3 m | nenhum | 0 % | -66,3 | 1,9 ms | nenhum | 100 % | -73,9 | 15,3 ms |
| 5 m | nenhum | 0 % | -71,2 | 1,9 ms | nenhum | 100 % | -81,2 | 16,1 ms |
| 5 m | 2 portas | 0 % | -73,8 | 1,9 ms | 2 portas | 100 % | -82,1 | 16,0 ms |
| 8 m | 1 parede | 0 % | -83,6 | 2,3 ms | 2 paredes | 100 % | -89,6 | 16,8 ms |
| 11 m | 2 paredes | 18,7 % | -94,3 | 3,3 ms | 3 paredes | 60,2 % não entregue | -97,8 | 3.881 ms |
| 13 m | — | — | — | — | 4 paredes | sem enlace | — | — |
| 18 m | várias | 97,6 % | -95,3 | 4,0 ms | várias | sem enlace | — | — |

**Atenção às paredes.** A 8 m e a 11 m, o número de paredes anotado no BLE é
uma a mais que no ESP-NOW. A diferença de RSSI entre os dois rádios a 8 m
(6 dB) é menor que nas posições sem parede (8,7 dB em média). Se houvesse uma
parede a mais de verdade, a diferença deveria crescer uns 6 dB, não diminuir.
Isso indica que **as posições são as mesmas e a contagem de paredes é que foi
diferente**. Vale confirmar antes de citar os números de paredes em
relatório.

### O que a comparação mostra

- **O BLE chega uns 9 dB mais fraco.** Nas quatro posições sem parede, a
  diferença de RSSI ficou entre 7,6 e 10,0 dB (média de 8,7 dB). A previsão
  pela folha de dados era de cerca de 11 dB (+19,5 dBm do WiFi contra +9 dBm do
  BLE), então a medida confirma a causa: **a diferença é a potência de
  transmissão**.
- **Com sinal bom, os dois entregam tudo.** Até 8 m, tanto o ESP-NOW quanto o
  BLE entregaram 100 %.
- **No limite, os dois falham de formas opostas:**
  - O **ESP-NOW** perde pacotes avulsos, mas o que chega é recente: a 11 m,
    perdeu 18,7 % e a latência continuou na casa dos milissegundos.
  - O **BLE** não perde pacotes avulsos: ele retransmite, enche a fila, entrega
    com segundos de atraso, passa a recusar pacotes novos e, quando o sinal
    piora mais, perde a conexão de vez e precisa procurar o sensor de novo.
    Para telemetria, em que o valor mais recente é o que importa, o
    comportamento do ESP-NOW é melhor.
- **A 18 m, nenhum dos dois serve.** O ESP-NOW ainda passava 2,4 % dos
  pacotes; o BLE não conseguiu nem conectar.
- **O BLE tem um custo de conexão que o ESP-NOW não tem.** O ESP-NOW envia
  assim que o rádio liga. O BLE precisa anunciar, ser encontrado e conectar
  antes de enviar o primeiro pacote, e depois de uma queda precisa repetir
  tudo. Para um nó que dorme e acorda a cada 5 min, isso pesa no consumo de
  energia e no tempo acordado. O modelo de energia da branch `teste-esp-now`
  vale só para o ESP-NOW.

### Revisão do que foi escrito na branch `teste-esp-now`

O `testes_24-10-26/README.md` daquela branch foi escrito antes deste teste e
fez uma previsão. Com os dados medidos:

| O que foi escrito lá | O que o teste mostrou |
|---|---|
| O BLE tem cerca de 11 dB a menos de orçamento de enlace | **Confirmado:** cerca de 9 dB a menos, medido nas mesmas posições |
| O BLE não sustenta o enlace a 13 m com 3 paredes | **Confirmado, com outra contagem de paredes:** a 13 m (anotado como 4 paredes) não houve enlace. Já a 11 m com 3 paredes, o BLE entregou só 40 %, com 4 a 6 s de atraso, e não serve para telemetria |
| A 11 m com 2 paredes, o BLE ficaria sem enlace | **Pessimista demais:** na posição de 11 m o BLE manteve a conexão e entregou 40 % |
| O BLE fica restrito à mesma sala | **Pessimista demais:** o BLE entregou 100 % a 8 m, atravessando parede |

Uma conclusão mais precisa: **o BLE do ESP32 cobre até cerca de 8 m com
paredes no meio, com latência de dezenas de milissegundos; o ESP-NOW cobre o
mesmo trecho com latência de milissegundos e ainda funciona, com perdas, um
ponto adiante (11 m), onde o BLE já congestiona.** Sugiro corrigir as duas
últimas frases da tabela no README da branch `teste-esp-now`.

## Próximos passos

1. **Fazer as quedas de conexão aparecerem nos testes.** Duas mudanças:
   - o gateway imprimir uma linha de estado a cada segundo
     (`@EST conectado=0 rx=...`), inclusive enquanto procura o sensor;
   - o gravador guardar o último estado visto antes da janela e informar, por
     exemplo, "o teste começou sem conexão (caiu às 12:47:40)".
2. **Limitar a fila do sensor quando o enlace congestiona.** Descartar o
   pacote mais antigo em vez do mais novo, para que o que chega ao gateway seja
   recente. Vale também medir o intervalo de conexão e testar intervalos
   diferentes.
3. **Repetir 11 m e 13 m conferindo a contagem de paredes**, e anotar o que
   foram os testes "avulso" e "avulso 2".
4. **Tentar o ponto de 13 m também com o ESP-NOW**, que não foi medido na
   outra bateria.
