#include "calculo.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

Resumo resume(const uint32_t* valores, size_t n) {
  Resumo r = {0, 0, 0};
  if (n == 0) return r;
  std::vector<uint32_t> v(valores, valores + n);
  std::sort(v.begin(), v.end());
  r.minimo = v.front();
  r.maximo = v.back();
  if (n % 2 == 1) {
    r.mediana = v[n / 2];
  } else {
    // soma em 64 bits para não estourar com tempos grandes
    r.mediana = (uint32_t)(((uint64_t)v[n / 2 - 1] + v[n / 2]) / 2);
  }
  return r;
}

size_t indiceDoMaior(const uint32_t* valores, size_t n) {
  size_t maior = 0;
  for (size_t i = 1; i < n; i++) {
    if (valores[i] > valores[maior]) maior = i;
  }
  return maior;
}

float cargaDiaria_mAh(float corrente_mA, float duracao_s, uint32_t ciclosDia) {
  return corrente_mA * duracao_s * (float)ciclosDia / 3600.0f;
}

ClasseStatus classifica(int status) {
  if (status < 0) return ClasseStatus::SemRede;
  if (status >= 200 && status < 300) return ClasseStatus::Sucesso;
  if (status >= 400 && status < 500) return ClasseStatus::ErroDoNo;
  if (status >= 500 && status < 600) return ClasseStatus::ErroDoCaminho;
  return ClasseStatus::Outro;
}

bool deveRetentar(ClasseStatus c) {
  return c == ClasseStatus::ErroDoCaminho || c == ClasseStatus::SemRede;
}

const char* nomeClasse(ClasseStatus c) {
  switch (c) {
    case ClasseStatus::Sucesso:       return "2xx";
    case ClasseStatus::ErroDoNo:      return "4xx";
    case ClasseStatus::ErroDoCaminho: return "5xx";
    case ClasseStatus::SemRede:       return "sem_rede";
    default:                          return "outro";
  }
}

uint32_t esperaRetentativa_ms(int tentativa) {
  if (tentativa < 1) tentativa = 1;
  if (tentativa > 16) tentativa = 16;  // teto para não estourar o deslocamento
  return 1000u << (tentativa - 1);
}

bool desfazChunked(const std::string& entrada, std::string& saida) {
  saida.clear();
  size_t pos = 0;
  while (pos < entrada.size()) {
    size_t fimLinha = entrada.find("\r\n", pos);
    if (fimLinha == std::string::npos) return false;
    // o tamanho vem em hexadecimal, opcionalmente seguido de ";extensao"
    std::string linhaTamanho = entrada.substr(pos, fimLinha - pos);
    char* fim = nullptr;
    unsigned long tamanho = strtoul(linhaTamanho.c_str(), &fim, 16);
    if (fim == linhaTamanho.c_str()) return false;
    pos = fimLinha + 2;
    if (tamanho == 0) return true;
    if (pos + tamanho > entrada.size()) return false;
    saida.append(entrada, pos, tamanho);
    pos += tamanho + 2;  // pula o CRLF que fecha o pedaço
  }
  return false;  // acabou sem o pedaço de tamanho zero
}

std::string comVirgulaAMais(const std::string& json) {
  size_t fecha = json.rfind('}');
  if (fecha == std::string::npos) return json + ",";
  std::string r = json;
  r.insert(fecha, ",");
  return r;
}
