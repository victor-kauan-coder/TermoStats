# Testes de falha · 24/10/2026, 15:53 a 16:01

Nó `eq01-no02` (ESP32 + DHT22, firmware em `wokwi/`), período de 5 s, broker público
`broker.hivemq.com:8883`. Gravado com `gravador/gravador.py --host broker.hivemq.com --equipe eq01`.

- `mensagens.csv`: CSV bruto do gravador
- `qualidade_saida.txt`: saída de `python gravador/qualidade.py dados/testes_falha_24-10-26/mensagens.csv`

Não é a campanha: são 7,5 min de testes provocados de propósito, e a completude
(58,9%) reflete isso.

## Registro de eventos

Horários **deduzidos do CSV**. Confirmar com as anotações da equipe.

| Hora | Teste | Evidência no CSV |
|---|---|---|
| 15:53:58 | gravador iniciado com o nó já publicando | `online` retido; primeira leitura com `seq=51` |
| ~15:55:05 | **nó morre sem avisar** (alimentação retirada) | última leitura 15:55:05; `offline` do testamento 15:55:51 (**46 s**); lacuna de 66 s |
| 15:56:11 | **nó reinicia** (1º) | `seq` volta a 1 com `ts` válido |
| ~15:56:47 | **nó reinicia** (2º) | `seq` volta a 1 às 15:57:07 com `ts` válido; lacuna de 21 s |
| ~15:57:46 a 15:58:18 | **gravador para** (sessão persistente) | `seq` 9 a 14 chegam juntos às 15:58:18 com os `ts` originais; nenhum `seq` faltando |
| 15:59:06 a 16:00:47 | **queda do WiFi** | lacuna de 101 s (~19 leituras); o `seq` só pula 1 (seq 25): **o nó não tem fila local** e não mede desconectado |
| 15:56 a 15:57 e 16:01 | status oscilando | `online`/`offline` alternando em segundos (hipótese: testamento da sessão antiga publicado quando a nova assume o mesmo client ID) |
| — | **antes do NTP** | nos 2 reinícios, a 1ª leitura já tem `ts`: 0 leituras com `ts` nulo |

Leituras `seq` 60, 21 e 31 vieram com `rssi_dbm` em vez de `temp_c`/`umid_pct`: falha de leitura do DHT22 (3 de 53).
