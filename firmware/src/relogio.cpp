#include "relogio.h"

#include <Arduino.h>
#include <time.h>

#include "config.h"

bool relogioSincronizado() {
  return time(nullptr) > (time_t)EPOCH_VALIDO;
}

bool relogioSincroniza(uint32_t timeout_ms) {
  configTime(FUSO_S, 0, NTP_SERVIDOR);
  uint32_t t0 = millis();
  while (!relogioSincronizado() && millis() - t0 < timeout_ms) delay(50);
  return relogioSincronizado();
}
