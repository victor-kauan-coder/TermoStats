
# Laboratório 01 — O primeiro sistema funcional

**Disciplina:** Tópicos em Sistemas Computacionais I (IoT/AIoT) — 2026.2  
**Equipe:** [Felipe Gabriel, Luanderson Oliveira, João Pedro Saleh, Victor Kuan, Davi Soares]  
**Trilha:** [x] Simulação no Wokwi [ ] Bancada [ ] Ambas  
**Link público do Wokwi:** [https://wokwi.com/projects/475597187970163713]

## Objetivo

Montar um sistema com ESP32 que lê a luminosidade por um LDR, ajusta proporcionalmente o brilho de um LED, identifica o modo noturno com histerese e aciona um buzzer intermitente cuja frequência aumenta com a escuridão.

## Circuito

|Componente|GPIO ESP32|Ligação|
|---|---|---|
|LDR|34|`AO → GPIO34`, `VCC → 3V3`, `GND → GND`|
|LED|26|`GPIO26 → resistor 220 Ω → anodo`; catodo → `GND`|
|Buzzer passivo|25|pino 2 (+) → `GPIO25`; pino 1 (-) → `GND`|

## Medições do LDR

A tensão foi calculada pela fórmula:


$$V = \frac{\text{ADC} \times 3,3}{4095}$$  

| Lux ajustado | Contagem do ADC | Tensão calculada (V) | Observação                                           |
| ------------ | --------------- | -------------------- | ---------------------------------------------------- |
| 10           | 3413            | 2,750                | Modo noturno; LED com PWM 212; frequência de 1503 Hz |
| 100          | 2045            | 1,648                | Modo diurno; LED com PWM 127; frequência de 989 Hz   |
| 500          | 999             | 0,805                | Modo diurno; LED com PWM 62; frequência de 595 Hz    |
| 2000         | 435             | 0,351                | Modo diurno; LED com PWM 27; frequência de 383 Hz    |
| 10000        | 161             | 0,130                | Modo diurno; LED com PWM 10; frequência de           |


**Verificação da polaridade:** a contagem cai de 3413 em 10lux para 161 em 10000lux, apresentando polaridade esperada, a contagem clara representa escuro enquanto a baixa representa o claro 

## Decisão e histerese

| Parâmetro          | Valor escolhido |
| ------------------ | --------------- |
| `LIMIAR_ESCURECER` | 2500            |
| `LIMIAR_CLAREAR`   | 1800            |

**Justificativa:**

O limiar para escurecer, 2500, fica entre as contagens de 100 lux (2045) e 10 lux (3413), acionando o modo noturno apenas quando a iluminação já está bem baixa. O limiar para clarear, 1800, é menor e fica entre as contagens de 100 lux e 500 lux. Assim, ao sair do escuro, o sistema só retorna ao modo diurno quando há luz suficiente; entre 1800 e 2500, ele conserva o estado anterior e evita oscilações.

### Comparação: um limiar x histerese

**Comportamento esperado com apenas um limiar:** quando a leitura do LDR fica próxima ao valor de corte, pequenas variações de luminosidade ou ruído do ADC fazem a contagem passar repetidamente de um lado para o outro do limiar. Como consequência, o modo noturno alternaria rapidamente entre ligado e desligado, e o buzzer poderia ser ativado e desativado sem estabilidade.

**Comportamento com os dois limiares adotados:** o sistema entra no modo noturno somente acima de `2500` e só retorna ao modo diurno abaixo de `1800`. Entre esses valores, o estado anterior é mantido; assim, pequenas variações próximas à fronteira não provocam alternâncias sucessivas

## Funcionamento das saídas

- **LED:** o PWM é proporcional à contagem do ADC; como contagem alta significa escuro, o LED fica mais brilhante no escuro.
    
- **Buzzer:** é autorizado pelo modo noturno; emite um bipe de 150 ms a cada 1 s, controlado por `millis()`, sem bloquear o `loop()`.
    
- **Frequência:** varia de 220 Hz a 1760 Hz; aumenta conforme o ambiente escurece.
    

## Telemetria e Evidência da execução

O monitor serial foi configurado para **115200 bps** e imprime uma linha CSV por amostra:

>A captura `20linhas_MS.png` contém mais de 20 linhas de telemetria e mostra os modos `diurno` e `noturno`, com o campo `bipe` nos estados `0` e `1`.


## Arquivos entregues

- `sketch.ino` comentado
    
- `diagram.json`
    
- Tabela de cinco medições preenchida
    
- Limiares registrados e justificados
    
- Captura do monitor serial com 20 ou mais linhas
    
- Link público do Wokwi na planilha da turma










