// secrets.example.h - ESTE vai para o repositorio; o secrets.h, nao.
// Copie para secrets.h, preencha, e confira que o .gitignore tem a linha:  secrets.h
#pragma once
#define WIFI_SSID  "Wokwi-GUEST"   // no Wokwi; na placa, a rede de voces
#define WIFI_SENHA ""
#define WIFI_CANAL 0      // 0 = procurar em todos os canais; no Wokwi use 6

// Broker PUBLICO da HiveMQ: sem usuario e sem senha, e QUALQUER UM pode ler e
// publicar nos mesmos topicos. Nada de dado sensivel aqui. Arvore da equipe:
// ufpi-iot26/eq01/#
#define MQTT_HOST    "broker.hivemq.com"
#define MQTT_PORTA   8883              // TLS; 1883 seria sem criptografia
#define MQTT_USUARIO ""                // vazio = conecta sem autenticacao
#define MQTT_SENHA   ""
