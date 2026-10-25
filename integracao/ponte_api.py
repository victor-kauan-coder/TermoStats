#!/usr/bin/env python3
"""Ponte entre o conjunto de dados da AP2 e a API do produto TermoLOG.

Lê o CSV bruto do gravador (dados/<pasta>/mensagens.csv), converte cada leitura
do contrato da campanha ({"id","seq","ts","temp_c","umid_pct"}) para o contrato
da API ({"sensor_id","temperatura","umidade","data_hora"}) e envia em lotes para
POST /gateways/{id}/leituras, com a chave de API do gateway, como faria o nó
central do produto.

- data_hora: o ts do nó (NTP, época Unix) em ISO 8601 com fuso UTC.
- Leituras só com rssi_dbm (DHT22 em falha) não têm grandeza e ficam de fora.
- Repetidas (mesmo seq dentro do mesmo trecho entre reinícios) ficam de fora.
- lote_id: UUID5 do nó e do intervalo de seq do lote. O mesmo lote reenviado
  tem o mesmo lote_id, e a API não grava nada da segunda vez (idempotência).

Só usa a biblioteca padrão.

Uso:
  python ponte_api.py dados/campanha_24-10-26/mensagens.csv \\
      --api http://localhost:8001 --gateway 1 --chave <chave> --sensor 1
  (--repetir reenvia todos os lotes para conferir a idempotência)
"""

import argparse
import csv
import json
import sys
import urllib.error
import urllib.request
import uuid
from datetime import UTC, datetime

NAMESPACE = uuid.UUID("6e1a0001-7d3f-4b6a-9c4e-2f1b8a5d0c01")  # o do contrato GATT


def ler_leituras(caminho):
    """Leituras com grandeza, em ordem de medição, sem repetidas."""
    vistas, leituras, trecho, ultimo_seq = set(), [], 0, None
    with open(caminho, encoding="utf-8") as f:
        linhas = [l for l in csv.DictReader(f) if l["topico"].endswith("/telemetria")]
    for linha in linhas:
        msg = json.loads(linha["payload"])
        seq = msg["seq"]
        if ultimo_seq is not None and seq < ultimo_seq and seq <= 2:
            trecho += 1  # o seq voltou a 1 ou 2: reinício do nó
        ultimo_seq = seq
        chave = (msg["id"], trecho, seq)
        if chave in vistas:
            continue
        vistas.add(chave)
        if "temp_c" not in msg or msg.get("ts") is None:
            continue
        leituras.append((msg["id"], trecho, seq, msg))
    return leituras


def montar_lotes(leituras, sensor_id, tamanho):
    lotes = []
    for i in range(0, len(leituras), tamanho):
        parte = leituras[i : i + tamanho]
        no, t0, s0, _ = parte[0]
        _, t1, s1, _ = parte[-1]
        lote_id = uuid.uuid5(NAMESPACE, f"{no}:{t0}:{s0}:{t1}:{s1}")
        itens = [
            {
                "sensor_id": sensor_id,
                "temperatura": float(m["temp_c"]),
                "umidade": float(m["umid_pct"]),
                "data_hora": datetime.fromtimestamp(m["ts"], UTC).isoformat(),
            }
            for _, _, _, m in parte
        ]
        lotes.append({"lote_id": str(lote_id), "versao_config": 0, "leituras": itens})
    return lotes


def enviar(api, gateway, chave, lote):
    req = urllib.request.Request(
        f"{api}/gateways/{gateway}/leituras",
        data=json.dumps(lote).encode(),
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {chave}"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csv")
    ap.add_argument("--api", default="http://localhost:8000")
    ap.add_argument("--gateway", type=int, required=True)
    ap.add_argument("--chave", required=True)
    ap.add_argument("--sensor", type=int, required=True)
    ap.add_argument("--lote", type=int, default=100, help="leituras por lote (máx. 100)")
    ap.add_argument("--repetir", action="store_true", help="reenvia os lotes no fim")
    a = ap.parse_args()

    leituras = ler_leituras(a.csv)
    lotes = montar_lotes(leituras, a.sensor, min(a.lote, 100))
    print(f"{len(leituras)} leituras com grandeza em {len(lotes)} lotes")
    rodadas = [("envio", lotes)] + ([("reenvio", lotes)] if a.repetir else [])
    for nome, lista in rodadas:
        aceitas = rejeitadas = 0
        for lote in lista:
            status, resp = enviar(a.api, a.gateway, a.chave, lote)
            if status != 201:
                sys.exit(f"{nome}: lote {lote['lote_id']} -> HTTP {status}: {resp}")
            aceitas += resp["aceitas"]
            rejeitadas += len(resp["rejeitadas"])
        # A API devolve, num reenvio, a resposta da primeira vez; quem mostra
        # se algo foi gravado de novo é o total de leituras do sensor.
        with urllib.request.urlopen(
            f"{a.api}/sensores/{a.sensor}/leituras?tamanho=1", timeout=30
        ) as r:
            total = json.loads(r.read())["total"]
        print(f"{nome}: {len(lista)} lotes, HTTP 201 em todos, "
              f"{aceitas} aceitas, {rejeitadas} rejeitadas; "
              f"total gravado para o sensor: {total}")


if __name__ == "__main__":
    main()
