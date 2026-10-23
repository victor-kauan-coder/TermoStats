#pragma once
#include <stdint.h>

// O ciclo que um nó paga a cada despertar (slide 04):
//   rádio desligado -> associa -> DHCP -> DNS -> conexão -> POST -> desliga

enum Fase { FASE_ASSOC, FASE_DHCP, FASE_DNS, FASE_CONEXAO, FASE_HTTP, FASE_DESC, N_FASES };

struct TemposCiclo {
  uint32_t fase[N_FASES];   // ms
  uint32_t total;           // ms, do rádio ligando ao rádio desligado
  int status;               // HTTP; < 0 se o ciclo não chegou à resposta
};

// Um ciclo completo. origem marca de onde veio na linha @FASES (t1, t5, t6, r).
TemposCiclo cicloCompleto(bool https, const char* origem, uint32_t serie, uint32_t n);

void teclaCiclo();                 // tecla 1
void teclaSerie(bool https);       // teclas 5 e 6
void teclaRajada();                // tecla r
void defineCorrente(float mA);     // tecla i<mA>
