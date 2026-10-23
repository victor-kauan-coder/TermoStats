#pragma once
#include <Arduino.h>

// postComPolitica(): a função que a equipe leva para o projeto (roteiro E3).
//   2xx     -> segue
//   4xx     -> erro do nó: registra e NÃO retenta
//   5xx/<0  -> erro do caminho: retenta com espera crescente, até MAX_TENTATIVAS
// Devolve o último status obtido.
//
// Nesta demo a espera usa delay(): o laço para. No firmware do projeto, a
// espera vira um estado da tarefa de rede (slide 10), sem bloquear o sensor.
int postComPolitica(const String& url, const String& corpo);
