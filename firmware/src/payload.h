#pragma once
#include <Arduino.h>

// Contrato de dados do TermoStats (slide 19 e roteiro E2):
//   id        texto    —              quem mediu
//   seq       inteiro  —              qual leitura; sobrevive ao deep sleep
//   ts        inteiro  s (epoch UTC)  quando mediu; null antes do NTP
//   temp_c    real     °C             temperatura do DHT22; null se falhou
//   umid_pct  real     % UR           umidade relativa do DHT22; null se falhou

// consomeSeq: true para uma leitura que vai ser enviada (incrementa seq);
// false só para mostrar o próximo payload (tecla p).
String montaPayload(bool consomeSeq);
