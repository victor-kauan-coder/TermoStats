# Projeto Wokwi do Laboratório 03

**Lab 03 · 30/09/2026 · Tópicos em Sistemas Computacionais I (IoT/AIoT) · UFPI**

Um nó MQTT com o mínimo necessário. **Sem teclas e sem as demonstrações da Aula 11.** O que o sketch faz, na ordem:

1. entra no WiFi e acerta o relógio (NTP);
2. conecta ao broker (TLS, porta 8883) com **testamento** `offline` retido no tópico de status, **sessão persistente** e keepalive de 30 s;
3. publica `online` **retido** no status e **assina** o próprio tópico de config;
4. publica a telemetria (`id`, `seq`, `ts` e as grandezas) a cada `periodo_s` segundos, com QoS 1;
5. se chegar `{"periodo_s":N}` no config, muda o período; mensagem vazia volta ao padrão (5 s).

| Tópico | Quem publica | QoS | Retido |
|---|---|---|---|
| `ufpi-iot26/<equipe>/<no>/telemetria` | o nó | 1 | não |
| `ufpi-iot26/<equipe>/<no>/status` | o nó (`online`) e o broker em nome dele (`offline`, testamento) | 1 | sim |
| `ufpi-iot26/<equipe>/<no>/config` | o MQTT Explorer (ou um painel) | 1 | a critério de quem publica |

**A equipe edita:** `secrets.h` (o mesmo de 28/09), `EQUIPE` e `NO`, e as funções `iniciaSensores()` e `leSensores()`.

**Arquivos:** `sketch.ino`, `secrets.example.h`, `.gitignore`, `diagram.json` (ESP32 + DHT22 no GPIO15, sem os LEDs da Aula 11), `libraries.txt` (MQTT, ArduinoJson e DHT sensor library for ESPx), `wokwi.toml`. O `ca_raiz.h` saiu: o sketch usa `setInsecure()` (atalho de sala; a verificação do servidor volta em 30/11).

**Para a equipe no Wokwi:** na cópia de 28/09, trocar a aba `sketch.ino` e repor `EQUIPE` e `NO = "no01"`. A aba `ca_raiz.h` pode ficar; não é mais usada.

*Compilado sem erros nem avisos no PlatformIO (espressif32 8.0.0) com MQTT, ArduinoJson 7 e DHTesp (86% da flash). Ainda não executado no Wokwi nem em placa.*

## Rodar na placa com PlatformIO

O `platformio.ini` usa a pasta `sketch/` como código-fonte, então a placa roda o mesmo `sketch.ino` do Wokwi. DHT22 no GPIO15 (3V3, GND e dado). Antes de gravar, confira no `secrets.h` o WiFi da placa e, no sketch, `NO = "no02"`.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
cd wokwi
& $pio run -t upload --upload-port COMx    # se parar em "Connecting....", segure o BOOT
& $pio device monitor -p COMx              # 115200
```

O `wokwi.toml` aponta para o binário do `pio run`, para simular no VS Code com a extensão Wokwi.
