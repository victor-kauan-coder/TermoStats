"""gravador.py - testes de alcance do BLE (sensor DHT22 -> gateway, ESP32 com ESP32).

Lê a serial do gateway por um tempo fixo (60 s por padrão), grava tudo e
imprime perda de pacotes, desconexões, latência e taxa de dados. Cada teste
recebe o nome da distância informada no início.

    pip install pyserial
    python gravador.py --listar                 descobre a porta do gateway
    python gravador.py --porta COM5             roda os testes, um por distância

Arquivos em dados/:
    <teste>.log    tudo o que o gateway imprimiu na janela do teste (procedência)
    <teste>.csv    um pacote válido por linha
    resumo.csv     uma linha por teste, para comparar as distâncias

Proteções contra perda falsa (o que deu errado na bateria de ESP-NOW de 24/10):
    - a serial é lida o tempo todo, numa thread, e cada linha recebe a hora de
      chegada no notebook: o teste usa só o que chegou dentro da janela, e
      nada fica parado no buffer entre um teste e outro;
    - toda linha do gateway termina em *XX (checksum): linha com byte perdido
      ou emendado é descartada, em vez de virar um seq errado;
    - a perda é "esperados pelo seq" menos "recebidos pelo contador rx do
      gateway": uma linha descartada não vira perda, porque o rx da linha
      seguinte já conta o pacote dela.

Como cada número é obtido:
    perda          esperados = do primeiro ao último seq da janela; recebidos = avanço do rx
    entrega        recebidos ÷ (taxa do sensor × duração): pega também as quedas de
                   conexão no começo ou no fim do teste, que o seq não enxerga
    desconexões    linhas @EVT do gateway, com o tempo sem conexão dentro da janela
    latência       RTT/2: o gateway ecoa cada pacote, o sensor mede a ida e volta e a
                   manda no pacote seguinte (os relógios das placas não são sincronizados)
    taxa           pacotes e bytes de carga útil recebidos pelo gateway por segundo
"""

from __future__ import annotations

import argparse
import csv
import queue
import re
import statistics
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

AQUI = Path(__file__).resolve().parent
DADOS = AQUI / "dados"

_LINHA = re.compile(r"(@[A-Z]+ [^*\r\n]*)\*([0-9A-F]{2})\s*$")
COLUNAS = ["t_s", "mac", "seq", "rx", "bytes", "rssi", "rtt_us", "seq_rtt", "tx_falhas",
           "temp_c", "umid_pct", "descartados_fila"]
CAMPOS_PKT = {"mac", "seq", "rx", "bytes", "rssi", "rtt_us", "seq_rtt", "tx_falhas"}


# ------------------------------------------------------------------ leitura

def checksum(conteudo: str) -> int:
    """XOR dos caracteres depois do '@', como o gateway calcula."""
    soma = 0
    for c in conteudo[1:]:
        soma ^= ord(c)
    return soma


def interpreta(linha: str) -> dict | None:
    """'@PKT mac=.. seq=12 ...*5A' -> {'tag': 'PKT', ...}; None se inválida ou não for '@'."""
    m = _LINHA.search(linha)
    if not m or checksum(m.group(1)) != int(m.group(2), 16):
        return None
    tag, _, resto = m.group(1).partition(" ")
    item = {"tag": tag[1:]}
    for token in resto.split():
        chave, igual, valor = token.partition("=")
        if not igual:
            return None
        if valor == "null":
            item[chave] = None
        elif chave in ("mac", "tipo", "motivo"):
            item[chave] = valor
        else:
            try:
                item[chave] = int(valor)
            except ValueError:
                try:
                    item[chave] = float(valor)
                except ValueError:
                    return None
    if item["tag"] == "PKT" and not CAMPOS_PKT <= item.keys():
        return None
    return item


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


def _trechos(lista: list[dict]) -> tuple[list[list[dict]], int]:
    """Separa onde o sensor (seq) ou o gateway (rx) reiniciou; conta seq repetidos."""
    trechos: list[list[dict]] = [[]]
    vistos: set[int] = set()
    duplicados = 0
    for p in lista:
        atual = trechos[-1]
        if p["seq"] in vistos:
            duplicados += 1
            continue
        if atual and (p["seq"] < atual[-1]["seq"] or p["rx"] < atual[-1]["rx"]):
            trechos.append([])
            vistos = set()
            atual = trechos[-1]
        vistos.add(p["seq"])
        atual.append(p)
    return [t for t in trechos if t], duplicados


def _tempo_desconectado(eventos: list[dict], inicio: float, fim: float) -> float:
    total, caiu_em = 0.0, None
    for i, e in enumerate(eventos):
        if e["tipo"] == "desconectado":
            caiu_em = e["t"]
        elif e["tipo"] == "conectado":
            if caiu_em is not None:
                total += e["t"] - caiu_em
            elif i == 0:
                total += e["t"] - inicio        # o teste começou sem conexão
            caiu_em = None
    if caiu_em is not None:
        total += fim - caiu_em
    return total


def analisa(pacotes: list[dict], eventos: list[dict], duracao_s: float, taxa_hz: float,
            rejeitadas: int = 0) -> dict:
    """Estatísticas da janela. pacotes e eventos na ordem de chegada, com 't' (s desde o início)."""
    desconexoes = sum(1 for e in eventos if e["tipo"] == "desconectado")
    sem_conexao = _tempo_desconectado(eventos, 0.0, duracao_s)
    base = {"desconexoes": desconexoes, "tempo_desconectado_s": sem_conexao,
            "linhas_rejeitadas": rejeitadas}
    if not pacotes:
        return {**base, "mac": "-", "esperados": 0, "recebidos": 0, "perdidos": 0,
                "perda_pct": None, "entrega_pct": 0.0, "duplicados": 0, "reinicios": 0,
                "perdidas_so_serial": 0, "pacotes_s": 0.0, "bytes_s": 0.0, "lat_n": 0,
                "lat_mediana_ms": None, "lat_p95_ms": None, "lat_max_ms": None,
                "rssi_medio": None, "rssi_min": None, "tx_falhas": 0}

    trechos, duplicados = _trechos(pacotes)
    esperados = sum(t[-1]["seq"] - t[0]["seq"] + 1 for t in trechos)
    recebidos = sum(t[-1]["rx"] - t[0]["rx"] + 1 for t in trechos) - duplicados
    validos = sum(len(t) for t in trechos)
    perdidos = max(esperados - recebidos, 0)

    rtts: dict[int, int] = {}
    for t in trechos:
        ini, fim = t[0]["seq"], t[-1]["seq"]
        for p in t:
            if p["rtt_us"] and ini <= p["seq_rtt"] <= fim:
                rtts[p["seq_rtt"]] = p["rtt_us"]
    lat_ms = [r / 2000 for r in rtts.values()]
    rssis = [p["rssi"] for p in pacotes if p["rssi"]]
    macs = [p["mac"] for p in pacotes]

    return {
        **base,
        "mac": max(set(macs), key=macs.count),
        "esperados": esperados,
        "recebidos": recebidos,
        "perdidos": perdidos,
        "perda_pct": 100 * perdidos / esperados,
        "entrega_pct": min(100.0, 100 * recebidos / (taxa_hz * duracao_s)),
        "duplicados": duplicados,
        "reinicios": len(trechos) - 1,
        "perdidas_so_serial": max(recebidos - validos, 0),
        "pacotes_s": recebidos / duracao_s,
        "bytes_s": sum(p["bytes"] for p in pacotes) / validos * recebidos / duracao_s,
        "lat_n": len(lat_ms),
        "lat_mediana_ms": statistics.median(lat_ms) if lat_ms else None,
        "lat_p95_ms": _percentil(lat_ms, 0.95) if lat_ms else None,
        "lat_max_ms": max(lat_ms) if lat_ms else None,
        "rssi_medio": statistics.mean(rssis) if rssis else None,
        "rssi_min": min(rssis) if rssis else None,
        "tx_falhas": sum(t[-1]["tx_falhas"] - t[0]["tx_falhas"] for t in trechos),
    }


# ------------------------------------------------------------------ saída

def _fmt(valor, casas=1, sufixo=""):
    return "-" if valor is None else f"{valor:.{casas}f}{sufixo}"


def imprime_resumo(nome: str, distancia: str, duracao_s: float, r: dict) -> None:
    print(f"\n=== {nome}  (distância: {distancia}, {duracao_s:.0f} s)")
    print(f"    entrega     {r['recebidos']} pacotes recebidos pelo gateway"
          f" = {r['entrega_pct']:.1f} % do que o sensor gerou na janela")
    if not r["recebidos"]:
        print("    nenhum pacote: o sensor está ligado e anunciando? está no alcance?")
    else:
        print(f"    perda       {r['perdidos']} de {r['esperados']} esperados pelo seq"
              f" ({_fmt(r['perda_pct'], 2, ' %')})")
    print(f"    conexão     {r['desconexoes']} desconexões · "
          f"{r['tempo_desconectado_s']:.1f} s sem conexão")
    if r["recebidos"]:
        print(f"    taxa        {r['pacotes_s']:.2f} pacotes/s · {r['bytes_s']:.0f} B/s"
              f" · {8 * r['bytes_s']:.0f} bit/s (carga útil)")
        print(f"    latência    mediana {_fmt(r['lat_mediana_ms'], 2, ' ms')}"
              f" · p95 {_fmt(r['lat_p95_ms'], 2, ' ms')} · máx {_fmt(r['lat_max_ms'], 2, ' ms')}"
              f"  (RTT/2, {r['lat_n']} medidas)")
        print(f"    RSSI        médio {_fmt(r['rssi_medio'], 1, ' dBm')}"
              f" · pior {_fmt(r['rssi_min'], 0, ' dBm')}  (amostrado 1 vez por segundo)")
    print(f"    serial      {r['linhas_rejeitadas']} linhas rejeitadas pelo checksum"
          f" · {r['perdidas_so_serial']} pacotes recebidos cuja linha não chegou inteira"
          " (não contam como perda)")
    if r["duplicados"] or r["reinicios"]:
        print(f"    avisos      {r['duplicados']} seq repetidos · {r['reinicios']} reinícios")


RESUMO_COLUNAS = ["teste", "distancia", "duracao_s", "mac", "esperados", "recebidos",
                  "perdidos", "perda_pct", "entrega_pct", "desconexoes",
                  "tempo_desconectado_s", "pacotes_s", "bytes_s", "lat_mediana_ms",
                  "lat_p95_ms", "lat_max_ms", "rssi_medio", "rssi_min", "tx_falhas",
                  "linhas_rejeitadas", "perdidas_so_serial"]


def anexa_resumo(caminho: Path, nome: str, distancia: str, duracao_s: float, r: dict) -> None:
    novo = not caminho.exists()
    linha = {**r, "teste": nome, "distancia": distancia, "duracao_s": f"{duracao_s:.0f}"}
    for chave in ("perda_pct", "entrega_pct", "tempo_desconectado_s", "pacotes_s", "bytes_s",
                  "lat_mediana_ms", "lat_p95_ms", "lat_max_ms", "rssi_medio"):
        linha[chave] = _fmt(r[chave], 3)
    with open(caminho, "a", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=RESUMO_COLUNAS, extrasaction="ignore")
        if novo:
            w.writeheader()
        w.writerow(linha)


# ------------------------------------------------------------------ serial

class LeitorSerial(threading.Thread):
    """Esvazia a serial o tempo todo e carimba cada linha com a hora de chegada."""

    def __init__(self, conexao):
        super().__init__(daemon=True)
        self.conexao = conexao
        self.linhas: queue.Queue[tuple[float, bytes]] = queue.Queue()
        self.erro: Exception | None = None
        self._parar = threading.Event()

    def run(self) -> None:
        while not self._parar.is_set():
            try:
                bruto = self.conexao.readline()
            except Exception as e:   # porta desconectada
                self.erro = e
                return
            if bruto:
                self.linhas.put((time.monotonic(), bruto))

    def parar(self) -> None:
        self._parar.set()

    def descarta_ate_agora(self) -> None:
        while True:
            try:
                self.linhas.get_nowait()
            except queue.Empty:
                return


def abre_porta(porta: str, baud: int):
    import serial
    conexao = serial.Serial()
    conexao.port = porta
    conexao.baudrate = baud
    conexao.timeout = 0.2
    conexao.dtr = False   # não segura o EN/BOOT da placa ao abrir a porta
    conexao.rts = False
    conexao.open()
    return conexao


def grava_teste(leitor: LeitorSerial, duracao_s: float, nome: str
                ) -> tuple[list[dict], list[dict], int]:
    """Coleta a janela [agora, agora + duracao_s). Devolve (pacotes, eventos, rejeitadas)."""
    DADOS.mkdir(parents=True, exist_ok=True)
    leitor.descarta_ate_agora()
    inicio = time.monotonic()
    pacotes, eventos, rejeitadas = [], [], 0
    proximo_aviso = 10
    with open(DADOS / f"{nome}.log", "w", encoding="utf-8", newline="") as log, \
            open(DADOS / f"{nome}.csv", "w", encoding="utf-8", newline="") as tabela:
        w = csv.DictWriter(tabela, fieldnames=COLUNAS, extrasaction="ignore")
        w.writeheader()
        while (agora := time.monotonic()) - inicio < duracao_s:
            if leitor.erro:
                raise SystemExit(f"[gravador] a porta caiu: {leitor.erro}")
            if agora - inicio >= proximo_aviso:
                print(f"  {proximo_aviso:3d} s · {len(pacotes)} pacotes · "
                      f"{sum(e['tipo'] == 'desconectado' for e in eventos)} desconexões")
                proximo_aviso += 10
            try:
                chegada, bruto = leitor.linhas.get(timeout=0.2)
            except queue.Empty:
                continue
            if chegada < inicio:
                continue                          # chegou antes do teste começar
            if chegada - inicio >= duracao_s:
                break
            texto = bruto.decode("utf-8", errors="replace")
            log.write(texto)
            if "@" not in texto:
                continue                          # mensagens [gateway] ...
            item = interpreta(texto)
            if item is None:
                rejeitadas += 1
                continue
            item["t"] = chegada - inicio
            if item["tag"] == "PKT":
                item["t_s"] = f"{item['t']:.3f}"
                pacotes.append(item)
                w.writerow(item)
            elif item["tag"] == "EVT":
                eventos.append(item)
                print(f"  [{item['t']:5.1f} s] {item['tipo']}")
    return pacotes, eventos, rejeitadas


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
    ap.add_argument("--taxa", type=float, default=10,
                    help="pacotes/s que o sensor gera (INTERVALO_MS = 100 -> 10)")
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
    leitor = LeitorSerial(conexao)
    leitor.start()
    print(f"[gravador] gateway em {a.porta} · testes de {a.duracao:.0f} s · dados em {DADOS}")
    try:
        while True:
            distancia = input("\nDistância sensor-gateway deste teste (ex.: 5 ou 2,5m; "
                              "Enter vazio encerra): ").strip()
            if not distancia:
                break
            nome = nome_teste(distancia, datetime.now())
            print(f"[gravador] {nome}: gravando por {a.duracao:.0f} s - não mexam nas placas")
            pacotes, eventos, rejeitadas = grava_teste(leitor, a.duracao, nome)
            r = analisa(pacotes, eventos, a.duracao, a.taxa, rejeitadas)
            imprime_resumo(nome, distancia, a.duracao, r)
            anexa_resumo(DADOS / "resumo.csv", nome, distancia, a.duracao, r)
            print(f"  -> {DADOS / (nome + '.csv')} · resumo.csv atualizado")
    except KeyboardInterrupt:
        print("\n[gravador] interrompido; o teste em andamento ficou incompleto.")
    finally:
        leitor.parar()
        leitor.join(timeout=1)
        conexao.close()


if __name__ == "__main__":
    main()
