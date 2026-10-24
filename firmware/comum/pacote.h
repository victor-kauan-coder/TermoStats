#pragma once
// Formato dos dados trocados entre o sensor (periférico BLE) e o gateway (central).
// Incluído pelos dois projetos (build_flags = -I../comum): mudou aqui, muda nos dois.
// O pacote é o mesmo do teste de ESP-NOW, para os resultados serem comparáveis.

#include <stdint.h>

#define NOME_SENSOR  "TermoStats-sensor"
#define UUID_SERVICO "6e1a0001-7d3f-4b6a-9c4e-2f1b8a5d0c01"
#define UUID_DADOS   "6e1a0002-7d3f-4b6a-9c4e-2f1b8a5d0c01"   // sensor -> gateway (notify)
#define UUID_ACK     "6e1a0003-7d3f-4b6a-9c4e-2f1b8a5d0c01"   // gateway -> sensor (write sem resposta)

#define MTU_DESEJADO 64        // o padrão do BLE (23) só carrega 20 bytes; o pacote tem 25

#define TIPO_DADO      1
#define TIPO_ACK       2

#define TEMP_INVALIDA  INT16_MIN   // DHT22 não respondeu
#define UMID_INVALIDA  UINT16_MAX

struct __attribute__((packed)) PacoteDado {
  uint8_t  tipo;              // TIPO_DADO
  uint32_t seq;               // avança a cada 100 ms, conectado ou não: o buraco é a perda
  uint32_t t_envio_us;        // micros() do sensor no envio
  uint32_t rtt_us;            // ida e volta medida no último ACK recebido (0 = nenhum)
  uint32_t seq_rtt;           // a qual seq esse rtt_us se refere
  uint32_t tx_falhas;         // pacotes não enviados (sem conexão ou erro de notify), desde o boot
  int16_t  temp_c_x10;        // 26,1 °C -> 261
  uint16_t umid_pct_x10;      // 58,0 %  -> 580
};

struct __attribute__((packed)) PacoteAck {
  uint8_t  tipo;              // TIPO_ACK
  uint32_t seq;               // seq do dado confirmado
  uint32_t t_envio_us;        // devolvido sem alteração: o sensor calcula o RTT
};
