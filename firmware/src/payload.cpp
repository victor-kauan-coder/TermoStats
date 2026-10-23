#include "payload.h"

#include <ArduinoJson.h>
#include <time.h>

#include "config.h"
#include "relogio.h"
#include "sensor.h"

// RTC_DATA_ATTR: sobrevive ao deep sleep (Aula 08). Detecta perda e duplicata.
static RTC_DATA_ATTR uint32_t seq = 0;

String montaPayload(bool consomeSeq) {
  JsonDocument doc;
  doc["id"] = ID_EQUIPE;
  doc["seq"] = consomeSeq ? seq++ : seq;

  if (relogioSincronizado()) {
    doc["ts"] = (uint32_t)time(nullptr);
  } else {
    doc["ts"] = nullptr;
  }

  // Uma casa decimal: é a resolução do DHT22. serialized() evita 26.10000038.
  float t, u;
  if (sensorLe(t, u)) {
    doc["temp_c"] = serialized(String(t, 1));
    doc["umid_pct"] = serialized(String(u, 1));
  } else {
    doc["temp_c"] = nullptr;
    doc["umid_pct"] = nullptr;
  }

  String json;
  serializeJson(doc, json);
  return json;
}
