"""gravador.py - testes de alcance do ESP-NOW (sensor DHT22 -> gateway).

Lê a serial do gateway por um tempo fixo (60 s por padrão), grava tudo e
imprime latência, taxa de dados e perda de pacotes. Cada teste recebe o nome
da distância informada no início.

    pip install pyserial
    python gravador.py --listar                 descobre a porta do gateway
    python gravador.py --porta COM5             roda os testes, um por distância

Arquivos em dados/:
    <teste>.log    tudo o que o gateway imprimiu (procedência)
    <teste>.csv    um pacote por linha
    resumo.csv     uma linha por teste, para comparar as distâncias

Como cada número é obtido:
    perda      pelo seq: esperados = do primeiro ao último seq visto no teste
    latência   RTT/2: o gateway ecoa cada pacote, o sensor mede a ida e volta
               e a manda no pacote seguinte (os relógios das placas não são
               sincronizados, então a ida sozinha não dá para medir)
    taxa       pacotes e bytes de carga útil ESP-NOW recebidos por segundo
"""

from __future__ import annotations

import argparse
import csv
import re
import statistics
import sys
import time
from datetime import datetime
from pathlib import Path

AQUI = Path(__file__).resolve().parent
DADOS = AQUI / "dados"

_PADRAO = re.compile(r"@PKT\s+(.*)$")
COLUNAS = ["t_s", "mac", "seq", "bytes", "rssi", "rtt_us", "seq_rtt", "tx_falhas",
           "temp_c", "umid_pct", "descartados_fila"]


# ------------------------------------------------------------------ leitura

def interpreta(linha: str) -> dict | None:
    """'@PKT mac=AABB.. seq=12 rssi=-60 temp_c=null ...' -> dict, ou None."""
    m = _PADRAO.search(linha.strip())
    if not m:
        return None
    pacote = {}
    for token in m.group(1).split():
        chave, igual, valor = token.partition("=")
        if not igual:
            continue
        if valor == "null":
            pacote[chave] = None
        elif chave == "mac":
            pacote[chave] = valor
        else:
            try:
                pacote[chave] = int(valor)
            except ValueError:
                try:
                    pacote[chave] = float(valor)
                except ValueError:
                    pacote[chave] = valor
    return pacote if "seq" in pacote else None


def nome_teste(distancia: str, quando: datetime) -> str:
    """'2,5' -> 'teste_2.5m_2026-10-24_101500'. Seguro para nome de arquivo."""
    d = distancia.strip().replace(",", ".").replace(" ", "")
    if re.fullmatch(r"\d+(\.\d+)?", d):
        d += "m"
    d = re.sub(r"[^0-9A-Za-z._-]", "_", d) or "sem_distancia"
    return f"teste_{d}_{quando:%Y-%m-%d_%H%M%S}"


# ------------------------------------------------------------------ análise

def _percentil(valores: list[float], p: float) -> float:
    ordenados = sorted(valores)
    k = (len(ordenados) - 1) * p
    i = int(k)
    j = min(i + 1, len(ordenados) - 1)
    return ordenados[i] + (ordenados[j] - ordenados[i]) * (k - i)


def analisa(pacotes: list[dict], duracao_s: float) -> dict[str, dict]:
    """Estatísticas por sensor (MAC). pacotes na ordem de chegada."""
    por_mac: dict[str, list[dict]] = {}
    for p in pacotes:
        por_mac.setdefault(p.get("mac", "?"), []).append(p)

    resultado = {}
    for mac, lista in por_mac.items():
        # trechos: o seq volta a 0 quando o sensor reinicia
        trechos: list[list[dict]] = [[]]
        vistos: set[int] = set()
        duplicados = 0
        for p in lista:
            s = p["seq"]
            atual = trechos[-1]
            if s in vistos:
                duplicados += 1
                continue
            if atual and s < atual[-1]["seq"]:
                trechos.append([])
                vistos = set()
                atual = trechos[-1]
            vistos.add(s)
            atual.append(p)

        esperados = sum(t[-1]["seq"] - t[0]["seq"] + 1 for t in trechos if t)
        recebidos = sum(len(t) for t in trechos)
        perdidos = esperados - recebidos

        # latência: um RTT por seq confirmado, só dos pacotes deste teste
        rtts: dict[int, int] = {}
        for t in trechos:
            if not t:
                continue
            ini, fim = t[0]["seq"], t[-1]["seq"]
            for p in t:
                if p.get("rtt_us") and ini <= p.get("seq_rtt", -1) <= fim:
                    rtts[p["seq_rtt"]] = p["rtt_us"]
        lat_ms = [r / 2000 for r in rtts.values()]

        bytes_total = sum(p.get("bytes", 0) for p in lista)
        rssis = [p["rssi"] for p in lista if isinstance(p.get("rssi"), int)]
        ultimo = trechos[-1]
        falhas_mac = (ultimo[-1].get("tx_falhas", 0) - ultimo[0].get("tx_falhas", 0)
                      if ultimo else 0)
        temps = [p["temp_c"] for p in lista if p.get("temp_c") is not None]
        umids = [p["umid_pct"] for p in lista if p.get("umid_pct") is not None]

        resultado[mac] = {
            "recebidos": recebidos,
            "esperados": esperados,
            "perdidos": perdidos,
            "perda_pct": 100 * perdidos / esperados if esperados else 0.0,
            "duplicados": duplicados,
            "reinicios": len([t for t in trechos if t]) - 1,
            "pacotes_s": recebidos / duracao_s,
            "bytes_s": bytes_total / duracao_s,
            "bits_s": 8 * bytes_total / duracao_s,
            "lat_n": len(lat_ms),
            "lat_mediana_ms": statistics.median(lat_ms) if lat_ms else None,
            "lat_p95_ms": _percentil(lat_ms, 0.95) if lat_ms else None,
            "lat_max_ms": max(lat_ms) if lat_ms else None,
            "rssi_medio": statistics.mean(rssis) if rssis else None,
            "rssi_min": min(rssis) if rssis else None,
            "falhas_mac": falhas_mac,
            "temp_c": temps[-1] if temps else None,
            "umid_pct": umids[-1] if umids else None,
        }
    return resultado


# ------------------------------------------------------------------ saída

def _fmt(valor, casas=1, sufixo=""):
    return "-" if valor is None else f"{valor:.{casas}f}{sufixo}"


def imprime_resumo(nome: str, distancia: str, duracao_s: float, res: dict[str, dict]) -> None:
    print(f"\n=== {nome}  (distância: {distancia}, {duracao_s:.0f} s)")
    if not res:
        print("  nenhum pacote recebido: o gateway está ligado? o sensor está no alcance?")
        return
    for mac, r in res.items():
        print(f"  sensor {mac}")
        print(f"    pacotes     {r['recebidos']} recebidos de {r['esperados']} esperados"
              f"  ->  {r['perdidos']} perdidos ({r['perda_pct']:.2f} %)")
        if r["duplicados"] or r["reinicios"]:
            print(f"    duplicados  {r['duplicados']} · reinícios do sensor {r['reinicios']}")
        print(f"    taxa        {r['pacotes_s']:.2f} pacotes/s · {r['bytes_s']:.0f} B/s"
              f" · {r['bits_s']:.0f} bit/s (carga útil)")
        print(f"    latência    mediana {_fmt(r['lat_mediana_ms'], 2, ' ms')}"
              f" · p95 {_fmt(r['lat_p95_ms'], 2, ' ms')} · máx {_fmt(r['lat_max_ms'], 2, ' ms')}"
              f"  (RTT/2, {r['lat_n']} medidas)")
        print(f"    RSSI        médio {_fmt(r['rssi_medio'], 1, ' dBm')}"
              f" · pior {_fmt(r['rssi_min'], 0, ' dBm')}")
        print(f"    falhas MAC  {r['falhas_mac']} envios que o sensor deu como não confirmados"
              " (já depois das retransmissões automáticas)")
        print(f"    DHT22       {_fmt(r['temp_c'], 1, ' C')} · {_fmt(r['umid_pct'], 1, ' %')}")


def anexa_resumo(caminho: Path, nome: str, distancia: str, duracao_s: float,
                 res: dict[str, dict]) -> None:
    novo = not caminho.exists()
    with open(caminho, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if novo:
            w.writerow(["teste", "distancia", "duracao_s", "mac", "recebidos", "esperados",
                        "perdidos", "perda_pct", "duplicados", "pacotes_s", "bytes_s",
                        "lat_mediana_ms", "lat_p95_ms", "lat_max_ms", "rssi_medio",
                        "rssi_min", "falhas_mac"])
        for mac, r in res.items():
            w.writerow([nome, distancia, f"{duracao_s:.0f}", mac, r["recebidos"],
                        r["esperados"], r["perdidos"], f"{r['perda_pct']:.3f}",
                        r["duplicados"], f"{r['pacotes_s']:.3f}", f"{r['bytes_s']:.1f}",
                        _fmt(r["lat_mediana_ms"], 3), _fmt(r["lat_p95_ms"], 3),
                        _fmt(r["lat_max_ms"], 3), _fmt(r["rssi_medio"], 1),
                        _fmt(r["rssi_min"], 0), r["falhas_mac"]])


# ------------------------------------------------------------------ serial

def abre_porta(porta: str, baud: int):
    import serial
    conexao = serial.Serial()
    conexao.port = porta
    conexao.baudrate = baud
    conexao.timeout = 0.5
    conexao.dtr = False   # não segura o EN/BOOT da placa ao abrir a porta
    conexao.rts = False
    conexao.open()
    return conexao


def grava_teste(conexao, duracao_s: float, nome: str) -> list[dict]:
    DADOS.mkdir(parents=True, exist_ok=True)
    conexao.reset_input_buffer()   # descarta o que chegou antes do teste começar
    pacotes = []
    inicio = time.monotonic()
    proximo_aviso = 10
    with open(DADOS / f"{nome}.log", "w", encoding="utf-8", newline="") as log, \
            open(DADOS / f"{nome}.csv", "w", encoding="utf-8", newline="") as tabela:
        w = csv.DictWriter(tabela, fieldnames=COLUNAS, extrasaction="ignore")
        w.writeheader()
        while (decorrido := time.monotonic() - inicio) < duracao_s:
            bruto = conexao.readline()
            if decorrido >= proximo_aviso:
                print(f"  {proximo_aviso:3d} s · {len(pacotes)} pacotes")
                proximo_aviso += 10
            if not bruto:
                continue
            texto = bruto.decode("utf-8", errors="replace")
            log.write(texto)
            pacote = interpreta(texto)
            if pacote is None:
                continue
            pacote["t_s"] = f"{decorrido:.3f}"
            pacotes.append(pacote)
            w.writerow(pacote)
    return pacotes


def lista_portas() -> None:
    from serial.tools import list_ports
    portas = list(list_ports.comports())
    if not portas:
        print("nenhuma porta serial (gateway ligado? driver CH340/CP210x instalado?)")
    for p in portas:
        print(f"{p.device:8} {p.description}")


def main() -> None:
    for fluxo in (sys.stdout, sys.stderr):                      # acentos no Git Bash/VS Code
        fluxo.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--porta", help="porta serial do GATEWAY, ex.: COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--duracao", type=float, default=60, help="segundos por teste (padrão 60)")
    ap.add_argument("--listar", action="store_true", help="lista as portas seriais e sai")
    a = ap.parse_args()

    if a.listar:
        lista_portas()
        return
    if not a.porta:
        ap.error("informe --porta (use --listar para descobrir)")

    import serial
    try:
        conexao = abre_porta(a.porta, a.baud)
    except serial.SerialException as erro:
        sys.exit(f"[gravador] não abriu {a.porta}: {erro}\n"
                 "  a porta existe (--listar)? o pio device monitor está aberto nela?")
    print(f"[gravador] gateway em {a.porta} · testes de {a.duracao:.0f} s · dados em {DADOS}")
    try:
        while True:
            distancia = input("\nDistância sensor-gateway deste teste (ex.: 5 ou 2,5m; "
                              "Enter vazio encerra): ").strip()
            if not distancia:
                break
            nome = nome_teste(distancia, datetime.now())
            print(f"[gravador] {nome}: gravando por {a.duracao:.0f} s - não mexam nas placas")
            pacotes = grava_teste(conexao, a.duracao, nome)
            res = analisa(pacotes, a.duracao)
            imprime_resumo(nome, distancia, a.duracao, res)
            anexa_resumo(DADOS / "resumo.csv", nome, distancia, a.duracao, res)
            print(f"  -> {DADOS / (nome + '.csv')} · resumo.csv atualizado")
    except KeyboardInterrupt:
        print("\n[gravador] interrompido; o teste em andamento ficou incompleto.")
    finally:
        conexao.close()


if __name__ == "__main__":
    main()
