# Registros do Laboratório 02

**1. Scanner I2C e WHO_AM_I**
* Endereços encontrados: 0x3C e 0x68.
* Valor retornado por `WHO_AM_I`: 0x68.

**2. Tempos de display()**
* Custo a 100 kHz: ~41.277 μs.
* Custo a 400 kHz: ~41.277 μs.
* *PS: Quando a função display.display() é executada, a biblioteca Adafruit_SSD1306 assume o controle do barramento I2C e força a velocidade para 400 kHz para transferir os 1024 bytes da imagem o mais rápido possível.*

**3. Tempos de Gravação (Cartão SD)**
* Custo médio do printf (gravaLinha): ~864 μs.
* Custo médio do flush(): ~6.444 μs.

**4. Comparativo de Laços (Etapas 5A e 5B)**

| Métrica | Ingênuo (5A) | Escalonado (5B) |
| :--- | :--- | :--- |
| **Intervalo Mínimo** | 70 ms | 20 ms |
| **Intervalo Máximo** | 82 ms | 84 ms |

**5. Tabela de Orçamento de Tempo**

| OPERAÇÃO | TEMPO MEDIDO | VEZES POR SEGUNDO | CUSTO POR SEGUNDO |
| :--- | :--- | :--- | :--- |
| `leMPU()` | ~1965 μs | 50 | ~98,25 ms |
| `desenhaPainel()` | ~41.277 μs | 2 | ~82,5 ms |
| `flush()` do SD | ~6.444 μs | 1 | ~6,4 ms |
| **Total comprometido** | | | **~187,15 ms** de 1000 ms |

**6. Decisões de Projeto**
* **Velocidade do barramento escolhida:** 400 kHz utilizando a função `Wire.setClock(400000)`, visando reduzir o tempo que o `leMPU()` gasta no barramento e compensar os atrasos gerados pelo SD e desenho na tela OLED.
* **Intervalo de flush e tolerância a falhas:** Foi escolhido um flush de 1000 ms, de forma que caso ocorra um corte de energia, perde-se no máximo 1 segundo de dados que estavam na RAM.
* **Saída da Etapa 5 adotada e justificativa:** A saída combina subir a velocidade do barramento I²C para 400 kHz para reduzir o tempo ocupado pelo sensor e aceitar o atraso residual do display, registrando o instante exato em `millis()` para dar prioridade à confiabilidade temporal dos dados.
