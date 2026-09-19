# Aula 05 — Laboratório 01.5

Circuito igual ao da Aula 04, sem alteração: `3V3 → 10 kΩ → nó (GPIO34) → LDR → GND`.

Notebook em `notebook/Analise_Ruido_Aula05.ipynb`, rodado com os dados de `dados/`.

## Caracterização

| cena | σ (cont.) | σ (mV) | p-p | ENOB |
|---|---|---|---|---|
| saturado | 1,94 | 1,57 | 14 | 9,25 |
| estavel | 2,54 | 2,05 | 18 | 8,86 |
| forte | 2,62 | 2,11 | 21 | 8,82 |
| escuro | 2,68 | 2,16 | 19 | 8,78 |
| ambiente_rc | 3,30 | 2,66 | 21 | 8,48 |
| ambiente2k | 5,77 | 4,65 | 29 | 7,68 |
| ambiente100 | 5,88 | 4,74 | 29 | 7,65 |
| ambiente | 6,08 | 4,90 | 36 | 7,60 |
| emi | 7,53 | 6,07 | 214 | 7,29 |

`degrau` ficou de fora da tabela porque não é cena estacionária (o LDR é coberto no meio da captura).

**σ de `forte` menor que o de `escuro` não quer dizer medida mais precisa.** `escuro` opera perto do teto da faixa útil (~3066 mV de 2450 mV recomendados), onde a curva do divisor achata. Convertendo pra lux (calibração abaixo), `forte` fica com 1,05 % de incerteza relativa e `escuro` com 1,42 % — pior, mesmo com σ elétrico "melhor".

**Em `saturado` o LDR está totalmente vedado**, a tensão fica colada perto do fundo de escala (~3908/4095) e por isso σ é o menor da tabela (1,94) — não porque a medida é boa, mas porque o sinal está comprimido e a informação sumiu junto com o ruído.

Bônus: convertendo `escuro` e `saturado` pra lux, a ordem de qualidade se inverte em relação à ordem por σ elétrico — `saturado` (menor σ) vira a pior medida (1,55 % de incerteza), e `ambiente` (maior σ) vira a melhor (0,89 %). É a mesma explicação: perto das duas pontas da faixa a curva `R_LDR × lux` achata, e o mesmo ruído elétrico vira uma incerteza relativa bem maior.

## Filtragem

Curva σ(N), médias móveis de N = 2 a 128:

| N | σ ambiente | σ estavel |
|---|---|---|
| 2 | 4,63 | 1,79 |
| 4 | 2,49 | 1,28 |
| 8 | 2,32 | 0,92 |
| 16 | 2,22 | 0,69 |
| 32 | 2,17 | 0,54 |
| 64 | 2,14 | 0,45 |
| 128 | 2,13 | 0,39 |

Em `estavel` a lei do 1/√N vale bem até N ≈ 16. Em `ambiente` ela trava em N ≈ 8: daí pra frente é deriva da lâmpada/temperatura, não ruído, e filtro não tira deriva.

Em `emi` (fio perto de fonte chaveada), média de 5 e mediana de 5 empatam em σ (3,72 e 3,85), mas a mediana corta o pico a pico de 50 para 26 — ela descarta o impulso em vez de diluir.

No degrau, o sinal bruto já leva 64 ms pra chegar aos 63 % da transição sozinho (mão + inércia do LDR). Média de 8 soma só +8 ms; média de 32 soma +34 ms.

**Filtro escolhido: média móvel, N = 8.** Em `ambiente` cai de 6,08 para 2,32 contagens (−62 %) por 8 ms de atraso, irrelevante pra um sensor de luz ambiente. Subir pra N = 32 só ganha mais 0,15 de σ pagando 4× mais atraso — não compensa, porque a curva já bateu no piso de deriva em N ≈ 8.

O piso de ~2,1–2,2 contagens que sobra mesmo em N = 128 é deriva, não ruído — pra melhorar além dele precisaria de calibração periódica, não de mais filtro.

## Calibração

| | |
|---|---|
| Erro médio (linear − calibrado) | +23,3 mV |
| Faixa do erro | −1,7 a +36,8 mV |
| Em contagens | até 46, sistemático |
| σ do ruído aleatório em `ambiente` | 6,1 contagens |

O erro da conversão ingênua (`bruto × 3300/4095`) é maior que todo o ruído que a filtragem reduz, e não some com média nem mediana. Trocado por `analogReadMilliVolts()`.

Curva do LDR ajustada em log-log a partir de `calibracao.csv`:

```
γ = 0,700 ± 0,002
R10 = 50,0 ± 0,3 kΩ
```

(bate com os parâmetros do `wokwi-photoresistor-sensor` no diagram.json: rl10=50, gamma=0.7)

| cena | σ (cont.) | lux médio | σ (lux) | σ relativo |
|---|---|---|---|---|
| escuro | 2,68 | 2,5 | 0,04 | 1,42 % |
| ambiente | 6,08 | 183,6 | 1,63 | 0,89 % |
| forte | 2,62 | 2435,8 | 25,54 | 1,05 % |

`ambiente`, que é o mais ruidoso em contagens, é o que mede melhor em lux — está no meio da faixa, onde o divisor é mais sensível.

Aplicando o filtro de N=8 e a calibração em `ambiente`: 183,6 lux, σ ≈ 0,62 lux (0,34 %). Mas isso é só o ruído do canal — a calibração foi feita contra um luxímetro de referência que declara 4 % de incerteza, e é esse número que domina, não o σ do canal. É a incerteza que devia acompanhar toda medida publicada.

## Sketch

`sketch/sketch_aula05.ino` — mesmo gabarito da Aula 04, com o filtro de N=8 e `analogReadMilliVolts()` aplicados antes da conversão pra lux. Telemetria sai em `t_ms;adc_bruto;mv_filtrado;lux;incerteza_pct;pwm;modo;freq_hz;bipe`. Os limiares do modo noturno foram passados de contagem crua pra lux (52,4 / 141,0). `diagram.json` não mudou, o circuito é o mesmo.
