a)Sensor desconectado com o sistema ligado:
R- sem o DHT11, a leitura trava tempo suficiente para o watchdog reiniciar a ESP32. Ela entra em um ciclo de reinicializações e nem chega ao trecho que registra “Falha de leitura”.
b)Queda e retorno da alimentação durante o acionamento:
R-A porta serial é desconectada; ao religar, a ESP32 reinicia, recupera a configuração da NVS e realiza uma nova leitura. O alarme é reavaliado conforme o valor atual do sensor.
c) Entrada fora da Faixa:
O sistema apita o alarme e indica no monitor serial que a medição esta fora do escopo deejavel, alterando o intervalo de medição, nesse caso os valores continuam normais mesmo fora do intervalo 

