# TermoStats

Nó IoT de temperatura e umidade (ESP32 + DHT22). Esta branch (`apendice_b`)
traz a ferramenta da **Aula 10 — WiFi, HTTP/REST e JSON**, que mede o ciclo
*conectar–publicar–desconectar* e reescreve a linha do rádio no
**Apêndice B (orçamento de energia)** com procedência.

```
firmware/   PlatformIO: o sketch de medição (teclas da prática)
gravador/   Python: grava a serial e organiza os dados em tabelas
```

## 1. Firmware (PlatformIO)

Pré-requisitos: VS Code + extensão PlatformIO (ou `pio` na linha de comando) e o
driver USB da placa (CH340 na WeMos D1 R32). A plataforma `espressif32@8.0.0`
traz o core Arduino-ESP32 3.3.x, que é o "core 3.x" do roteiro.

```bash
cd firmware
cp include/secrets.example.h include/secrets.h   # e preencha SSID e senha da sala
pio run -t upload          # compila e grava (porta detectada sozinha; ou --upload-port COM5)
pio device monitor         # 115200; tecle m
pio test -e native         # testes da lógica pura, no PC, sem placa
```

`secrets.h` está no `.gitignore`. **Nenhuma senha real em commit.**

Ligação (a mesma da AP1): DHT22 no GPIO4, LED de status no GPIO25. Se o sensor
não responder, o firmware funciona igual e `temp_c`/`umid_pct` vão como `null`.

| Tecla | O que faz | Etapa |
|---|---|---|
| `1` | um ciclo HTTP com o tempo de cada fase e a fase mais cara | E1 |
| `p` | payload atual e tamanho em bytes | E2 |
| `n` | `time()` antes e depois do NTP (`ts` deixa de ser `null`) | E2 |
| `2` | POST escrito à mão, com os bytes de cada linha; compara o `json` do eco | E3 |
| `3` | o mesmo POST com uma vírgula a mais: `200 OK` e `"json": null` | E3 |
| `e` | 404 (não retenta) e 503 (retenta com 1 s, 2 s) via `postComPolitica()` | E3 |
| `i135` | corrente do rádio ativo medida na bancada, em mA | E4 |
| `5` / `6` | dez ciclos HTTP / HTTPS: mín., mediana, máx. e linha do orçamento | E4 |
| `r` | rajada de 30 s para o medidor USB estabilizar | bancada |

A tecla `4` (Open-Meteo) é opcional na aula e ficou de fora. Se o
`httpbin.org` estiver instável, troque `ECO_HOST` por `"postman-echo.com"` em
`firmware/include/config.h`.

### Contrato de dados

```json
{"id":"termo-no01","seq":7,"ts":1790169247,"temp_c":26.1,"umid_pct":58.0}
```

| Campo | Tipo | Unidade | Significado |
|---|---|---|---|
| `id` | texto | — | identificador do nó (`ID_EQUIPE` em `config.h`) |
| `seq` | inteiro | — | contador de leituras, `RTC_DATA_ATTR`: sobrevive ao deep sleep |
| `ts` | inteiro | s (epoch UTC) | instante da medição, carimbado no nó; `null` antes do NTP |
| `temp_c` | real | °C | temperatura do DHT22, 1 casa; `null` se o sensor falhou |
| `umid_pct` | real | % UR | umidade relativa do DHT22, 1 casa; `null` se o sensor falhou |

## 2. Gravador (Python 3.10+)

```bash
cd gravador
pip install -r requirements.txt
python -m gravador portas                    # descobre a COM da placa
python -m gravador capturar --porta COM5     # terminal: digite 1, p, n, 2, 3, e, i135, 5, 6...
python -m gravador processar                 # gera as tabelas a partir de dados/brutos/
python -m unittest discover -s tests         # testes
```

`capturar` substitui o `pio device monitor` (feche o monitor antes, porque a
porta só abre uma vez). Tudo o que a placa imprime vai para
`dados/brutos/AAAA-MM-DD_HHMMSS.log`, que é a procedência dos números.

`processar` lê as linhas `@...` dos logs e escreve em `gravador/dados/`:

- `resumo.md`: tabelas de E1 a E4, a linha do orçamento e o trecho LaTeX
  pronto para o `apendices/B-orcamento-de-energia.tex`;
- `ciclos.csv`: todo ciclo cronometrado, fase por fase;
- `leituras.jsonl` / `leituras.csv`: as leituras confirmadas com 2xx, sem
  duplicatas de `seq`, com as lacunas apontadas no resumo.

O orçamento usa **144 ciclos/dia** (RF-01, uma leitura a cada 10 min) e o
**máximo** da série HTTP, porque a bateria também paga os dias ruins.
`--corrente 135` sobrescreve a corrente registrada pela tecla `i`.
