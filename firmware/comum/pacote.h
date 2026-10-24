#pragma once
// Formato dos quadros ESP-NOW trocados entre o sensor e o gateway.
// Incluído pelos dois projetos (build_flags = -I../comum): mudou aqui, muda nos dois.

#include <stdint.h>

#define CANAL_ESPNOW   1        // os dois lados precisam estar no mesmo canal WiFi

#define TIPO_DADO      1        // sensor -> gateway
#define TIPO_ACK       2        // gateway -> sensor (eco do t_envio_us)

#define TEMP_INVALIDA  INT16_MIN   // DHT22 não respondeu
#define UMID_INVALIDA  UINT16_MAX

struct __attribute__((packed)) PacoteDado {
  uint8_t  tipo;              // TIPO_DADO
  uint32_t seq;               // 0, 1, 2... recomeça quando o sensor reinicia
  uint32_t t_envio_us;        // micros() do sensor no envio
  uint32_t rtt_us;            // ida e volta medida no último ACK recebido (0 = nenhum)
  uint32_t seq_rtt;           // a qual seq esse rtt_us se refere
  uint32_t tx_falhas;         // envios sem ACK da camada MAC, acumulado desde o boot
  int16_t  temp_c_x10;        // 26,1 °C -> 261
  uint16_t umid_pct_x10;      // 58,0 %  -> 580
};

struct __attribute__((packed)) PacoteAck {
  uint8_t  tipo;              // TIPO_ACK
  uint32_t seq;               // seq do dado confirmado
  uint32_t t_envio_us;        // devolvido sem alteração: o sensor calcula o RTT
};
