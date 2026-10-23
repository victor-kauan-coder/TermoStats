#pragma once
#include <stdint.h>

// O nó não sabe que horas são até falar com um servidor NTP (slide 20).
bool relogioSincronizado();

// Chama configTime() e espera até timeout_ms pelo primeiro acerto.
// Exige rede no ar.
bool relogioSincroniza(uint32_t timeout_ms);
