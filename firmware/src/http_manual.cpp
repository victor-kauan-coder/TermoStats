#include "http_manual.h"

#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "calculo.h"
#include "config.h"

// Acrescenta uma linha (com CRLF) à requisição e, se verbose, mostra os bytes.
static size_t poeLinha(String& req, const String& linha, bool verbose,
                       const char* rotulo = nullptr) {
  String comFim = linha + "\r\n";
  req += comFim;
  if (verbose) {
    Serial.printf("  %-38s %3u bytes\n", rotulo ? rotulo : linha.c_str(),
                  (unsigned)comFim.length());
  }
  return comFim.length();
}

// Lê uma linha terminada em \n (sem o \r\n), esperando até o prazo.
static bool leLinha(Client& c, String& linha, uint32_t prazo) {
  linha = "";
  while (millis() < prazo) {
    while (c.available()) {
      char ch = (char)c.read();
      if (ch == '\n') {
        if (linha.endsWith("\r")) linha.remove(linha.length() - 1);
        return true;
      }
      linha += ch;
    }
    if (!c.connected()) return false;
    delay(1);
  }
  return false;
}

static RespostaHttp postNoCliente(Client& c, const String& corpo, bool verbose) {
  RespostaHttp r = {-1, 0, corpo.length(), ""};

  String req;
  r.bytesCabecalho += poeLinha(req, "POST " ECO_CAMINHO " HTTP/1.1", verbose);
  r.bytesCabecalho += poeLinha(req, "Host: " ECO_HOST, verbose);
  r.bytesCabecalho += poeLinha(req, "User-Agent: " USER_AGENT, verbose);
  r.bytesCabecalho += poeLinha(req, "Content-Type: application/json", verbose);
  r.bytesCabecalho += poeLinha(req, "Content-Length: " + String(corpo.length()), verbose);
  r.bytesCabecalho += poeLinha(req, "Connection: close", verbose);
  r.bytesCabecalho += poeLinha(req, "", verbose, "(linha em branco)");
  req += corpo;
  if (verbose) {
    Serial.printf("  %s\n", corpo.c_str());
    Serial.printf("  cabecalho: %u bytes  |  corpo: %u bytes\n",
                  (unsigned)r.bytesCabecalho, (unsigned)r.bytesCorpo);
  }

  c.print(req);   // uma escrita só: menos pacotes no ar

  uint32_t prazo = millis() + TIMEOUT_RESPOSTA_MS;
  String linha;
  if (!leLinha(c, linha, prazo)) return r;   // sem resposta
  if (verbose) Serial.printf("  <<< resposta: %s\n", linha.c_str());

  // "HTTP/1.1 200 OK"
  int espaco = linha.indexOf(' ');
  r.status = espaco > 0 ? linha.substring(espaco + 1).toInt() : -1;

  bool chunked = false;
  int contentLength = -1;
  while (leLinha(c, linha, prazo) && linha.length() > 0) {
    String minusc = linha;
    minusc.toLowerCase();
    if (minusc.startsWith("transfer-encoding:") && minusc.indexOf("chunked") > 0) chunked = true;
    if (minusc.startsWith("content-length:")) contentLength = linha.substring(15).toInt();
  }

  std::string bruto;
  while (millis() < prazo) {
    while (c.available()) bruto += (char)c.read();
    if (contentLength >= 0 && (int)bruto.size() >= contentLength) break;
    if (!c.connected() && !c.available()) break;
    delay(1);
  }

  if (chunked) {
    std::string limpo;
    desfazChunked(bruto, limpo);
    r.corpo = limpo.c_str();
  } else {
    r.corpo = bruto.c_str();
  }
  return r;
}

RespostaHttp enviaPost(bool https, const String& corpo, bool verbose,
                       uint32_t* tConexao, uint32_t* tHttp) {
  NetworkClient tcp;
  NetworkClientSecure tls;
  // setInsecure(): cifra, mas não verifica quem é o servidor.
  // Atalho de sala, inaceitável em produto (volta em 30/11).
  tls.setInsecure();
  Client& c = https ? (Client&)tls : (Client&)tcp;

  uint32_t t = millis();
  bool conectou = https ? tls.connect(ECO_HOST, 443) : tcp.connect(ECO_HOST, 80);
  if (tConexao) *tConexao = millis() - t;
  if (!conectou) {
    if (verbose) Serial.printf("  conexao com %s recusada ou expirada\n", ECO_HOST);
    if (tHttp) *tHttp = 0;
    return {-1, 0, corpo.length(), ""};
  }

  t = millis();
  RespostaHttp r = postNoCliente(c, corpo, verbose);
  if (tHttp) *tHttp = millis() - t;
  c.stop();
  return r;
}
