#pragma once
#include <Arduino.h>

// POST escrito à mão (slide 16): HTTP é texto, e dá para contar.

struct RespostaHttp {
  int status;              // < 0: não conectou ou não houve resposta
  size_t bytesCabecalho;   // da requisição enviada
  size_t bytesCorpo;       // da requisição enviada
  String corpo;            // corpo da resposta, já sem chunked
};

// Conecta em ECO_HOST (porta 80 ou 443), envia o POST e lê a resposta.
// verbose: imprime cada linha do cabeçalho com seus bytes.
// tConexao: TCP (HTTP) ou TCP + handshake TLS (HTTPS).
// tHttp:    envio da requisição até o fim da resposta.
RespostaHttp enviaPost(bool https, const String& corpo, bool verbose,
                       uint32_t* tConexao, uint32_t* tHttp);
