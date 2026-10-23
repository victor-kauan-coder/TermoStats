#pragma once
#include <stdint.h>

// Conexão WiFi com o tempo de cada fase vindo dos eventos do driver
// (STA_CONNECTED e STA_GOT_IP), e não de WiFi.status(): é assim que
// associação e DHCP aparecem separados.

void redeInicia();   // registra os eventos; chamar uma vez no setup()

// Liga o rádio e conecta. Espera-se que o rádio esteja em WIFI_OFF.
// tAssoc: de WiFi.mode(STA) até STA_CONNECTED (inclui ligar o rádio).
// tDhcp:  de STA_CONNECTED até STA_GOT_IP.
bool redeConecta(uint32_t* tAssoc, uint32_t* tDhcp);

// Para as teclas que só precisam de rede (2, 3, e, n).
bool redeGaranteConectada();

// Desassocia e desliga o rádio (WIFI_OFF), como antes de um deep sleep.
void redeDesliga();
