# Integração com a API do produto · 25/10/2026

Teste da ponte `ponte_api.py` contra uma instância local e isolada do servidor do produto
(FastAPI + PostgreSQL, release `v0.1.0`, migrations até `0009`), subida com o
`docker-compose.yml` do repositório do produto num projeto Docker próprio (`termolog-ap2`,
porta 8001, banco vazio), para não gravar no banco de desenvolvimento.

Fonte: `dados/campanha_24-10-26/mensagens.csv` (o CSV bruto da campanha, sem edição).

## Cadastro (pela própria API)

- `POST /gateways` → gateway 2, "Nó central (ponte da AP2)", com chave de API
- `POST /salas` → sala 1 com a faixa da sala de manipulação (15–25 °C, 40–70 % UR); sala 2 sem faixa
- `POST /salas/{id}/sensores` → sensor 1 (sala 1) e sensor 2 (sala 2), ambos do gateway 2

## Ingestão completa (sala 2, sem faixa)

```
$ python ponte_api.py dados/campanha_24-10-26/mensagens.csv --api http://localhost:8001 \
      --gateway 2 --chave <chave> --sensor 2 --repetir
1213 leituras com grandeza em 13 lotes
envio: 13 lotes, HTTP 201 em todos, 1213 aceitas, 0 rejeitadas; total gravado para o sensor: 1213
reenvio: 13 lotes, HTTP 201 em todos, 1213 aceitas, 0 rejeitadas; total gravado para o sensor: 1213
```

- 13 lotes em 0,3 s; nenhuma leitura rejeitada (nenhuma fora dos limites físicos do DHT22).
- Reenvio dos mesmos 13 lotes (mesmo `lote_id`): a API devolve a resposta da primeira vez e o
  total continua 1213, ou seja, nada foi gravado de novo (idempotência por lote).
- `GET /sensores/2/leituras`: primeira leitura 2026-10-24T19:01:32Z (16:01:32 local), última
  21:48:26Z (18:48:26), temperatura de 24,0 a 30,7 °C: igual ao conjunto de dados.
- `GET /gateways`: gateway 2 `online`, com o último contato registrado.
- `GET /salas/status`: sala 2 `ok`; sala 1 `fora_da_faixa`.

## Defeito encontrado no servidor (sala 1, com faixa)

- O primeiro lote da campanha na sala 1 voltou HTTP 500, e o lote inteiro foi recusado.
- Reprodução mínima:
  - 1, 2 ou 3 leituras com temperatura **e** umidade fora da faixa → HTTP 201;
  - 6 leituras assim → HTTP 500;
  - lotes de até 10 leituras com **um** desvio só → HTTP 201.
- Erro no log da API:
  `psycopg.errors.InvalidColumnReference: there is no unique or exclusion constraint matching
  the ON CONFLICT specification`, no `INSERT INTO alertas ... ON CONFLICT (sala_id, tipo)
  WHERE situacao = %(situacao_1)s DO NOTHING`.
- Causa provável: o `INSERT` do alerta usa o índice parcial único (um pendente por sala e tipo)
  com o predicado `situacao = 'pendente'` passado como **parâmetro**. Depois de várias execuções
  na mesma conexão, o comando passa a ser preparado, e o PostgreSQL não reconhece o índice parcial
  num predicado parametrizado. Correção sugerida: predicado literal no `index_where`
  (`text("situacao = 'pendente'")`) ou `prepare_threshold=None` no psycopg.
