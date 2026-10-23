# Resumo da prática — Aula 10

Gerado por `python -m gravador processar` a partir de:

- `2026-10-23_172639.log`

## E1 — Fases de um ciclo (tecla 1)

| Fase | Placa (ms) |
|---|---:|
| associar + autenticar | 226 |
| obter IP (DHCP) | 1528 |
| resolver nome (DNS) | 1 |
| conexão (TCP ou TCP+TLS) | 219 |
| requisição HTTP | 236 |
| desconectar | 19 |
| requisição (TCP + HTTP) | 455 |
| **ciclo completo** | **2234** |

Status 200; fase mais cara: obter IP (DHCP) (sessão `2026-10-23_172639`).

## E2 — Contrato de dados (tecla p)

```json
{"id":"termo-no01","seq":8,"ts":1792787350,"temp_c":25.4,"umid_pct":35.5}
```

73 bytes.

| Campo | Tipo | Unidade | Significado |
|---|---|---|---|
| id | texto | — | identificador do nó |
| seq | inteiro | — | contador de leituras, sobrevive ao deep sleep |
| ts | inteiro | s (epoch UTC) | instante da medição, carimbado no nó; null antes do NTP |
| temp_c | real | °C | temperatura do DHT22, 1 casa; null se o sensor falhou |
| umid_pct | real | % UR | umidade relativa do DHT22, 1 casa; null se o sensor falhou |

## E3 — Publicar e tratar o erro (teclas 2, 3 e e)

- tecla 2: cabeçalho 143 bytes · corpo 73 bytes · status 200 · json igual ao enviado: sim
- tecla 3, vírgula a mais: cabeçalho 143 bytes · corpo 74 bytes · status 200 · json igual ao enviado: não

| URL | Tentativa | Status | Classe | Espera (ms) |
|---|---:|---:|---|---:|
| http://httpbin.org/status/404 | 1 | 404 | 4xx | 0 |
| http://httpbin.org/status/503 | 1 | 503 | 5xx | 1000 |
| http://httpbin.org/status/503 | 2 | 503 | 5xx | 2000 |
| http://httpbin.org/status/503 | 3 | 503 | 5xx | 0 |

## E4 — Ciclo medido (teclas 5 e 6)

| Sessão | Série | Ciclo | 2xx | Mínimo (ms) | Mediana (ms) | Máximo (ms) | 1º ciclo (ms) | Fase mais cara |
|---|---:|---|---:|---:|---:|---:|---:|---|
| 2026-10-23_172639 | 1 | HTTP | 10/10 | 2163 | 2270 | 2475 | 2287 | obter IP (DHCP) |
| 2026-10-23_172639 | 2 | HTTPS | 10/10 | 2954 | 3158 | 8994 | 3012 | obter IP (DHCP) |

HTTPS ÷ HTTP (medianas): 1.39×

## Orçamento de energia v2 (Apêndice B)

Linha do rádio com o **máximo** da última série HTTP (2475 ms) e 300.0 mA (origem: bancada). O alarme continua fora, como na AP1.

| Estado | mA | s | ciclos/dia | mA·h/dia | Origem |
|---|---:|---:|---:|---:|---|
| acorda e lê | 46,5 | 12,00 | 144 | 22,32 | folha de dados |
| associa WiFi e publica | 300,0 | 2,48 | 144 | 29,70 | corrente: bancada · tempo: bancada da equipe (máximo) |
| dorme | 2,0 | 585,52 | 144 | 46,84 | bancada |
| **Total** | | | | **98,86** | |

Consumo diário de 98,9 mA·h (corrente média de 4,12 mA). Autonomia com a 18650 (2080 mA·h úteis): 21,0 dias. Quem dimensiona o sistema agora é o estado "dorme", com 47 % do consumo diário.

Para colar no `apendices/B-orcamento-de-energia.tex`:

```latex
acorda e lê & ESP32 ativo + DHT22 em leitura & 46,5 & 12,00 & 144 & 22,32 & \folhadedados \\
associa WiFi e publica & ESP32 + rádio WiFi: associa, DHCP, DNS, POST, desliga & 300,0 & 2,48 & 144 & 29,70 & \bancada{} (corrente) + \bancada{} (tempo: máximo de 10 ciclos) \\
dorme & ESP32 em deep sleep & 2,0 & 585,52 & 144 & 46,84 & \bancada \\
\midrule
\multicolumn{5}{@{}r}{\textbf{Total diário (excluindo o alarme, ainda não medido)}} & \textbf{98,9} & \\
```

## Leituras (conjunto de dados)

- leituras confirmadas (2xx): 35 → `leituras.jsonl`, `leituras.csv`
- duplicadas descartadas: 0
- envios sem 2xx (podem ter chegado ou não): 2
- corpos que não são JSON válido: 1
- lacuna: 2026-10-23_172639 termo-no01: seq 5 sem 2xx
- lacuna: 2026-10-23_172639 termo-no01: seq 9 sem 2xx
- lacuna: 2026-10-23_172639 termo-no01: seq 32 sem 2xx
