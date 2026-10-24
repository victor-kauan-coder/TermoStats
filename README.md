# TermoStats — teste de ESP-NOW

Branch `teste-esp-now`: alcance e desempenho do ESP-NOW entre um nó sensor
(ESP32 + DHT22) e um gateway (ESP32 ligado ao notebook).

```
firmware/
├── comum/pacote.h     formato dos pacotes, usado pelos dois lados
├── sensor/            PlatformIO: lê o DHT22 e envia 10 pacotes/s
└── gateway/           PlatformIO: recebe, confirma e repassa pela serial
gravador/
├── gravador.py        testes de 60 s nomeados pela distância
└── test_gravador.py
```

## Gravar as placas

Ligação do sensor: DHT22 no GPIO4 (3V3, GND e dado). O gateway não precisa de nada além do USB.

```bash
cd firmware/sensor  && pio run -t upload --upload-port COMx   # placa do sensor
cd firmware/gateway && pio run -t upload --upload-port COMy   # placa do gateway
```

Não é preciso configurar MAC. O sensor começa em broadcast e, no primeiro ACK do
gateway, aprende o endereço dele e passa a enviar em unicast. Os dois usam o
canal WiFi 1 (`CANAL_ESPNOW` em `firmware/comum/pacote.h`).

## Rodar os testes

Com o gateway no USB do notebook e o sensor alimentado em outro lugar (USB ou bateria):

```bash
cd gravador
pip install pyserial
python gravador.py --listar          # descobre a COM do gateway
python gravador.py --porta COMy      # pede a distância, grava 60 s, mostra o resultado
```

Feche o `pio device monitor` antes: a porta só abre para um programa por vez.
A cada teste o gravador pergunta a distância (`5`, `2,5`, `10m parede`...), grava
60 s (`--duracao` muda isso) e imprime:

- **perda de pacotes**: pelo `seq`, do primeiro ao último visto no teste;
- **latência**: RTT/2. O gateway ecoa cada pacote, o sensor mede a ida e volta e
  a envia no pacote seguinte. Os relógios das duas placas não são sincronizados,
  então a ida sozinha não dá para medir;
- **taxa de dados**: pacotes/s, B/s e bit/s de carga útil (25 bytes por pacote);
- **RSSI** no gateway, falhas da camada MAC vistas pelo sensor e a última leitura do DHT22.

Arquivos em `gravador/dados/`: `<teste>.log` (saída bruta do gateway),
`<teste>.csv` (um pacote por linha) e `resumo.csv` (uma linha por teste, para
comparar as distâncias).

```bash
python -m unittest test_gravador     # testes do gravador, sem placa
```
