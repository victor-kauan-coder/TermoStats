#include "sensor.h"

#include <DHT.h>

#include "config.h"

static DHT dht(DHT_PIN, DHT_TYPE);
static bool jaLeu = false;
static uint32_t tUltimaLeitura = 0;
static float ultimaTemp = NAN;
static float ultimaUmid = NAN;

void sensorInicia() {
  dht.begin();
}

bool sensorLe(float& temperatura_c, float& umidade_pct) {
  if (!jaLeu || millis() - tUltimaLeitura >= DHT_INTERVALO_MS) {
    ultimaTemp = dht.readTemperature();
    ultimaUmid = dht.readHumidity();
    tUltimaLeitura = millis();
    jaLeu = true;
  }
  temperatura_c = ultimaTemp;
  umidade_pct = ultimaUmid;
  return !isnan(ultimaTemp) && !isnan(ultimaUmid);
}
