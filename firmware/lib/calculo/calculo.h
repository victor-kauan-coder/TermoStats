#pragma once
// Lógica pura, sem Arduino: roda na placa e nos testes nativos
// (pio test -e native). Tudo o que é conta ou decisão mora aqui.

#include <stddef.h>
#include <stdint.h>
#include <string>

// ---------------------------------------------------------------------------
// Estatística da série de ciclos (teclas 5 e 6)
// ---------------------------------------------------------------------------
struct Resumo {
  uint32_t minimo;
  uint32_t mediana;   // n par: média dos dois centrais
  uint32_t maximo;
};

Resumo resume(const uint32_t* valores, size_t n);

// Índice do maior valor (a "fase mais cara"). n == 0 devolve 0.
size_t indiceDoMaior(const uint32_t* valores, size_t n);

// ---------------------------------------------------------------------------
// Orçamento de energia (Aula 08, Apêndice B)
//   carga [mA·h/dia] = corrente [mA] × duração [s] × ciclos/dia ÷ 3600
// ---------------------------------------------------------------------------
float cargaDiaria_mAh(float corrente_mA, float duracao_s, uint32_t ciclosDia);

// ---------------------------------------------------------------------------
// Política de retentativa (slides 15 e 17)
//   2xx        -> ninguém errou, segue a vida
//   4xx        -> erro do nó: não retenta, vai falhar igual
//   5xx        -> erro do caminho: retenta com espera crescente
//   negativo   -> a requisição nem saiu (sem rede, DNS, recusa): retenta
//   1xx / 3xx  -> fora do contrato deste nó: não retenta
// ---------------------------------------------------------------------------
enum class ClasseStatus { Sucesso, ErroDoNo, ErroDoCaminho, SemRede, Outro };

ClasseStatus classifica(int status);
bool deveRetentar(ClasseStatus c);
const char* nomeClasse(ClasseStatus c);
// tentativa 1 -> 1000 ms, 2 -> 2000 ms, 3 -> 4000 ms ...
uint32_t esperaRetentativa_ms(int tentativa);

// ---------------------------------------------------------------------------
// HTTP à mão
// ---------------------------------------------------------------------------
// Desfaz "Transfer-Encoding: chunked". Devolve false se o corpo vier truncado.
bool desfazChunked(const std::string& entrada, std::string& saida);

// Insere uma vírgula antes do último '}' (Demo 03: JSON inválido de propósito).
std::string comVirgulaAMais(const std::string& json);
