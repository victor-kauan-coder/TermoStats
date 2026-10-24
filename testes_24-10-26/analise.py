"""Reanálise da bateria de 24/10/2026 a partir dos arquivos brutos.

    python analise.py        (dentro de testes_24-10-26/)

Por que reanalisar: o resumo do gravador (brutos/resumo_gravador.csv) contou
como perda de rádio dois defeitos da SERIAL entre o gateway e o notebook:
  1. linhas velhas: a primeira linha de quase todo teste é um pacote preso no
     buffer da porta desde o teste anterior; o salto de seq até a segunda
     linha inflava os "esperados";
  2. linhas corrompidas: bytes perdidos ou emendados (ex.: "seq=465" no lugar
     de "seq=4165"), que o gravador aceitou como seq válido.

Critérios desta reanálise:
  - linha válida: campos completos, 25 bytes, 0 < seq - seq_rtt <= 1000,
    seq crescente e tx_falhas coerente com o número de envios desde a anterior;
  - linhas velhas no início: o seq avança mais do que o tempo permite (10/s);
  - recebido pelo gateway = seq de linha válida OU seq_rtt de linha válida
    (seq_rtt é o último ACK que o sensor recebeu: o gateway teve esse pacote);
  - perda de rádio = esperados - recebidos pelo gateway;
  - conferência independente: tx_falhas, as falhas de envio vistas pelo sensor.

Saída: resultados_corrigidos.csv e as tabelas em Markdown no terminal.
"""

from __future__ import annotations

import csv
import statistics
import sys
from pathlib import Path

AQUI = Path(__file__).resolve().parent
BRUTOS = AQUI / "brutos"
DURACAO_S = 60.0
BYTES_PACOTE = 25
TAXA_ENVIO = 10          # pacotes/s do sensor (INTERVALO_MS = 100)

# arquivo -> (rótulo, distância em m, obstáculos), na ordem em que foram feitos
TESTES = {
    "teste_1.5m_2026-10-24_095646": ("1,5 m", 1.5, "nenhum declarado"),
    "teste_3m_2026-10-24_095906": ("3 m", 3, "nenhum declarado"),
    "teste_5m_2026-10-24_100124": ("5 m", 5, "nenhum declarado"),
    "teste_5com2portasdemadeira_2026-10-24_100332": ("5 m", 5, "2 portas de madeira"),
    "teste_8m1com1parede_2026-10-24_100539": ("8 m", 8, "1 parede"),
    "teste_11mcom2paredes_2026-10-24_100725": ("11 m", 11, "2 paredes"),
    "teste_18mcomdiversasparedes_2026-10-24_101048": ("18 m", 18, "várias paredes"),
}

CAMPOS_INT = ["seq", "bytes", "rssi", "rtt_us", "seq_rtt", "tx_falhas"]


def _linhas(caminho: Path) -> list[dict]:
    """Linhas do CSV do gravador com os campos inteiros convertidos (ou None)."""
    saida = []
    with open(caminho, encoding="utf-8") as f:
        for ln in csv.DictReader(f):
            try:
                r = {k: int(ln[k]) for k in CAMPOS_INT}
                r["t_s"] = float(ln["t_s"])
                r["mac"] = ln["mac"]
            except (TypeError, ValueError, KeyError):
                r = None
            saida.append(r)
    return saida


def valida(linhas: list[dict]) -> tuple[list[dict], int]:
    """Devolve (linhas válidas em ordem, quantidade descartada)."""
    candidatas = []
    macs = [r["mac"] for r in linhas if r]
    mac = max(set(macs), key=macs.count) if macs else None
    for r in linhas:
        if not r or r["mac"] != mac or r["bytes"] != BYTES_PACOTE:
            continue
        if not -110 <= r["rssi"] <= 0:
            continue
        if r["rtt_us"] and not (0 < r["seq"] - r["seq_rtt"] <= 1000 and r["rtt_us"] < 1_000_000):
            continue
        candidatas.append(r)

    # linhas velhas no início: o seq até a próxima avança mais do que o tempo permite
    while len(candidatas) > 1:
        a, b = candidatas[0], candidatas[1]
        if b["seq"] - a["seq"] > TAXA_ENVIO * (b["t_s"] - a["t_s"]) + 30:
            candidatas.pop(0)
        else:
            break

    validas = []
    for r in candidatas:
        if validas:
            ant = validas[-1]
            envios = r["seq"] - ant["seq"]
            if envios <= 0:
                continue                                   # seq fora de ordem: corrompido
            if not ant["tx_falhas"] <= r["tx_falhas"] <= ant["tx_falhas"] + envios:
                continue                                   # contador corrompido
        validas.append(r)
    return validas, len(linhas) - len(validas)


def _percentil(v: list[float], p: float) -> float:
    s = sorted(v)
    k = (len(s) - 1) * p
    i = int(k)
    j = min(i + 1, len(s) - 1)
    return s[i] + (s[j] - s[i]) * (k - i)


def analisa(nome: str) -> dict:
    rotulo, distancia, obstaculos = TESTES[nome]
    linhas = _linhas(BRUTOS / f"{nome}.csv")
    validas, descartadas = valida(linhas)
    seqs = {r["seq"] for r in validas}
    ini, fim = min(seqs), max(seqs)
    acks = {r["seq_rtt"] for r in validas if r["rtt_us"] and ini <= r["seq_rtt"] <= fim}
    no_gateway = seqs | acks
    esperados = fim - ini + 1

    rtts = {}
    for r in validas:
        if r["rtt_us"] and ini <= r["seq_rtt"] <= fim:
            rtts[r["seq_rtt"]] = r["rtt_us"]
    lat = [v / 2000 for v in rtts.values()]
    rssi = [r["rssi"] for r in validas]

    return {
        "teste": nome,
        "distancia": rotulo,
        "distancia_m": distancia,
        "obstaculos": obstaculos,
        "linhas_csv": len(linhas),
        "linhas_descartadas": descartadas,
        "esperados": esperados,
        "recebidos_gateway": len(no_gateway),
        "perda_radio": esperados - len(no_gateway),
        "perda_radio_pct": 100 * (esperados - len(no_gateway)) / esperados,
        "perdidos_so_na_serial": len(no_gateway - seqs),
        "falhas_mac": validas[-1]["tx_falhas"] - validas[0]["tx_falhas"],
        "envios": fim - ini,
        "pacotes_s": len(no_gateway) / DURACAO_S,
        "bytes_s": len(no_gateway) * BYTES_PACOTE / DURACAO_S,
        "lat_n": len(lat),
        "lat_mediana_ms": statistics.median(lat) if lat else None,
        "lat_p95_ms": _percentil(lat, 0.95) if lat else None,
        "lat_max_ms": max(lat) if lat else None,
        "rssi_medio": statistics.mean(rssi),
        "rssi_p10": _percentil(rssi, 0.10),
        "rssi_min": min(rssi),
    }


def _br(x, casas=1):
    return "—" if x is None else f"{x:.{casas}f}".replace(".", ",")


def main() -> None:
    for fluxo in (sys.stdout, sys.stderr):
        fluxo.reconfigure(encoding="utf-8", errors="replace")
    resultados = [analisa(n) for n in TESTES]

    with open(AQUI / "resultados_corrigidos.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(resultados[0]))
        w.writeheader()
        for r in resultados:
            w.writerow({k: (f"{v:.3f}" if isinstance(v, float) else v) for k, v in r.items()})

    bruto = {}
    with open(BRUTOS / "resumo_gravador.csv", encoding="utf-8") as f:
        for ln in csv.DictReader(f):
            bruto[ln["teste"]] = float(ln["perda_pct"])

    print("| Distância | Obstáculos | Perda (gravador) | Perda de rádio real | Falhas MAC no sensor "
          "| Erros só da serial | RSSI médio / p10 (dBm) | Latência mediana / p95 / máx (ms) |")
    print("|---|---|---:|---:|---:|---:|---:|---:|")
    for r in resultados:
        print(f"| {r['distancia']} | {r['obstaculos']} | {_br(bruto[r['teste']])} % | "
              f"**{r['perda_radio']} de {r['esperados']} ({_br(r['perda_radio_pct'])} %)** | "
              f"{r['falhas_mac']} de {r['envios']} | {r['perdidos_so_na_serial']} | "
              f"{_br(r['rssi_medio'])} / {_br(r['rssi_p10'], 0)} | "
              f"{_br(r['lat_mediana_ms'], 2)} / {_br(r['lat_p95_ms'], 2)} / {_br(r['lat_max_ms'], 2)} |")

    print("\n| Distância | Obstáculos | Pacotes/s no gateway | Carga útil (B/s) | Linhas descartadas |")
    print("|---|---|---:|---:|---:|")
    for r in resultados:
        print(f"| {r['distancia']} | {r['obstaculos']} | {_br(r['pacotes_s'], 2)} | "
              f"{_br(r['bytes_s'], 0)} | {r['linhas_descartadas']} de {r['linhas_csv']} |")


if __name__ == "__main__":
    main()
