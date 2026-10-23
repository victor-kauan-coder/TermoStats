#include "politica.h"

#include <HTTPClient.h>

#include "calculo.h"
#include "config.h"

int postComPolitica(const String& url, const String& corpo) {
  int status = -1;
  for (int tentativa = 1; tentativa <= MAX_TENTATIVAS; tentativa++) {
    HTTPClient http;
    http.setTimeout(TIMEOUT_RESPOSTA_MS);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    status = http.POST(corpo);
    http.end();

    ClasseStatus classe = classifica(status);
    bool retenta = deveRetentar(classe) && tentativa < MAX_TENTATIVAS;
    uint32_t espera = retenta ? esperaRetentativa_ms(tentativa) : 0;

    Serial.printf("  tentativa %d: status %d (%s)", tentativa, status, nomeClasse(classe));
    if (status < 0) Serial.printf(" - %s", HTTPClient::errorToString(status).c_str());
    if (classe == ClasseStatus::Sucesso) Serial.print(" -> segue");
    else if (classe == ClasseStatus::ErroDoNo) Serial.print(" -> erro do no: nao retenta");
    else if (retenta) Serial.printf(" -> retenta em %u ms", (unsigned)espera);
    else if (deveRetentar(classe)) Serial.print(" -> limite de tentativas: desiste");
    else Serial.print(" -> nao retenta");
    Serial.println();

    Serial.printf("@POLITICA url=%s tentativa=%d status=%d classe=%s espera_ms=%u\n",
                  url.c_str(), tentativa, status, nomeClasse(classe), (unsigned)espera);

    if (!retenta) break;
    delay(espera);
  }
  return status;
}
