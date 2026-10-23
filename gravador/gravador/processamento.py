"""Transforma os logs brutos em tabelas: ciclos, leituras e o resumo da prática."""

from __future__ import annotations

import csv
import json
import statistics
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from . import orcamento
from .protocolo import Evento, le_log

FASES = ["assoc", "dhcp", "dns", "conexao", "http", "desc"]
NOME_FASE = {
    "assoc": "associar + autenticar", "dhcp": "obter IP (DHCP)",
    "dns": "resolver nome (DNS)", "conexao": "conexão (TCP ou TCP+TLS)",
    "http": "requisição HTTP", "desc": "desconectar",
}


def _sucesso(status) -> bool:
    return isinstance(status, int) and 200 <= status < 300


@dataclass
class Serie:
    sessao: str
    serie: int
    tipo: str
    ciclos: list[dict] = field(default_factory=list)

    @property
    def validos(self) -> list[dict]:
        return [c for c in self.ciclos if _sucesso(c.get("status"))]

    def resumo(self) -> dict | None:
        ok = self.validos
        if not ok:
            return None
        totais = [c["total"] for c in ok]
        medianas = {f: statistics.median(c[f] for c in ok) for f in FASES}
        return {
            "ok": len(ok), "n": len(self.ciclos),
            "min": min(totais), "med": statistics.median(totais), "max": max(totais),
            "mais_cara": max(medianas, key=medianas.get),
            "primeiro": self.ciclos[0]["total"],
        }


def agrupa_series(eventos: list[Evento]) -> list[Serie]:
    series: dict[tuple, Serie] = {}
    for e in eventos:
        if e.tag != "FASES" or e.campos.get("origem") not in ("t5", "t6"):
            continue
        chave = (e.sessao, e.campos["serie"])
        if chave not in series:
            series[chave] = Serie(e.sessao, e.campos["serie"], e.campos["tipo"])
        series[chave].ciclos.append(e.campos)
    return list(series.values())


@dataclass
class Leituras:
    validas: list[dict] = field(default_factory=list)
    duplicadas: int = 0
    invalidas: int = 0          # JSON que nem o gravador entende (tecla 3)
    sem_2xx: int = 0            # envio sem confirmação: pode ou não ter chegado
    lacunas: list[str] = field(default_factory=list)


def organiza_leituras(eventos: list[Evento]) -> Leituras:
    """Uma linha por leitura confirmada (2xx), sem duplicatas de (sessão, id, seq).

    seq vive em RTC_DATA_ATTR: sobrevive ao deep sleep, mas recomeça quando a
    placa é reiniciada. Por isso a sessão entra na chave.
    """
    r = Leituras()
    vistos = set()
    for e in eventos:
        if e.tag != "ENVIO":
            continue
        if not _sucesso(e.campos.get("status")):
            r.sem_2xx += 1
            continue
        try:
            dado = json.loads(e.json or "")
        except json.JSONDecodeError:
            r.invalidas += 1
            continue
        chave = (e.sessao, dado.get("id"), dado.get("seq"))
        if chave in vistos:
            r.duplicadas += 1
            continue
        vistos.add(chave)
        r.validas.append({"sessao": e.sessao, "status": e.campos["status"], **dado})

    por_no = defaultdict(list)
    for l in r.validas:
        por_no[(l["sessao"], l.get("id"))].append(l.get("seq"))
    for (sessao, no), seqs in por_no.items():
        seqs = sorted(s for s in seqs if isinstance(s, int))
        for a, b in zip(seqs, seqs[1:]):
            if b - a == 2:
                r.lacunas.append(f"{sessao} {no}: seq {a + 1} sem 2xx")
            elif b - a > 2:
                r.lacunas.append(f"{sessao} {no}: seq {a + 1} a {b - 1} sem 2xx")
    return r


def _ultimo(eventos: list[Evento], tag: str, **filtro) -> Evento | None:
    for e in reversed(eventos):
        if e.tag == tag and all(e.campos.get(k) == v for k, v in filtro.items()):
            return e
    return None


def _corrente(eventos: list[Evento], forcada: float | None) -> tuple[float, str]:
    if forcada is not None:
        return forcada, "bancada"
    ultimo = _ultimo(eventos, "ORCAMENTO")
    if ultimo:
        return float(ultimo.campos["mA"]), ultimo.campos["origem_mA"]
    return 120.0, "estimativa"


def _escreve_csv(caminho: Path, linhas: list[dict], colunas: list[str]) -> None:
    with open(caminho, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=colunas, extrasaction="ignore")
        w.writeheader()
        w.writerows(linhas)


def processar(logs: list[Path], pasta_dados: Path, ciclos_dia: int,
              corrente_mA: float | None) -> Path:
    eventos: list[Evento] = []
    for log in sorted(logs):
        eventos.extend(le_log(log))

    # ciclos.csv: todo ciclo cronometrado, de qualquer tecla
    ciclos = [{"sessao": e.sessao, **e.campos} for e in eventos if e.tag == "FASES"]
    _escreve_csv(pasta_dados / "ciclos.csv", ciclos,
                 ["sessao", "origem", "serie", "n", "tipo", *FASES, "total", "status",
                  "mais_cara"])

    # leituras: a linha do conjunto de dados
    leituras = organiza_leituras(eventos)
    with open(pasta_dados / "leituras.jsonl", "w", encoding="utf-8") as f:
        for l in leituras.validas:
            f.write(json.dumps(l, ensure_ascii=False) + "\n")
    _escreve_csv(pasta_dados / "leituras.csv", leituras.validas,
                 ["sessao", "status", "id", "seq", "ts", "temp_c", "umid_pct"])

    resumo = _resumo_md(eventos, logs, leituras, ciclos_dia, corrente_mA)
    destino = pasta_dados / "resumo.md"
    destino.write_text(resumo, encoding="utf-8")
    return destino


def _resumo_md(eventos, logs, leituras: Leituras, ciclos_dia, corrente_forcada) -> str:
    s = ["# Resumo da prática — Aula 10", "",
         "Gerado por `python -m gravador processar` a partir de:", ""]
    s += [f"- `{Path(l).name}`" for l in sorted(logs)]

    # E1
    s += ["", "## E1 — Fases de um ciclo (tecla 1)", ""]
    e1 = _ultimo(eventos, "FASES", origem="t1")
    if e1:
        c = e1.campos
        s += ["| Fase | Placa (ms) |", "|---|---:|"]
        s += [f"| {NOME_FASE[f]} | {c[f]} |" for f in FASES]
        s += [f"| requisição (TCP + HTTP) | {c['conexao'] + c['http']} |",
              f"| **ciclo completo** | **{c['total']}** |", "",
              f"Status {c['status']}; fase mais cara: {NOME_FASE[c['mais_cara']]} "
              f"(sessão `{e1.sessao}`)."]
    else:
        s.append("_Sem tecla 1 nos logs._")

    # E2
    s += ["", "## E2 — Contrato de dados (tecla p)", ""]
    e2 = _ultimo(eventos, "PAYLOAD")
    if e2:
        s += ["```json", e2.json or "", "```", "", f"{e2.campos['bytes']} bytes.", ""]
    s += ["| Campo | Tipo | Unidade | Significado |", "|---|---|---|---|",
          "| id | texto | — | identificador do nó |",
          "| seq | inteiro | — | contador de leituras, sobrevive ao deep sleep |",
          "| ts | inteiro | s (epoch UTC) | instante da medição, carimbado no nó; null antes do NTP |",
          "| temp_c | real | °C | temperatura do DHT22, 1 casa; null se o sensor falhou |",
          "| umid_pct | real | % UR | umidade relativa do DHT22, 1 casa; null se o sensor falhou |"]

    # E3
    s += ["", "## E3 — Publicar e tratar o erro (teclas 2, 3 e e)", ""]
    for quebrado, rotulo in ((0, "tecla 2"), (1, "tecla 3, vírgula a mais")):
        p = _ultimo(eventos, "POST", quebrado=quebrado)
        if p:
            c = p.campos
            s.append(f"- {rotulo}: cabeçalho {c['cabecalho']} bytes · corpo {c['corpo']} bytes · "
                     f"status {c['status']} · json igual ao enviado: "
                     f"{'sim' if c['json_igual'] else 'não'}")
    politica = [e for e in eventos if e.tag == "POLITICA"]
    if politica:
        s += ["", "| URL | Tentativa | Status | Classe | Espera (ms) |", "|---|---:|---:|---|---:|"]
        s += [f"| {e.campos['url']} | {e.campos['tentativa']} | {e.campos['status']} | "
              f"{e.campos['classe']} | {e.campos['espera_ms']} |" for e in politica[-6:]]

    # E4
    s += ["", "## E4 — Ciclo medido (teclas 5 e 6)", ""]
    series = agrupa_series(eventos)
    ultima_por_tipo = {}
    if series:
        s += ["| Sessão | Série | Ciclo | 2xx | Mínimo (ms) | Mediana (ms) | Máximo (ms) | "
              "1º ciclo (ms) | Fase mais cara |", "|---|---:|---|---:|---:|---:|---:|---:|---|"]
        for se in series:
            r = se.resumo()
            if not r:
                s.append(f"| {se.sessao} | {se.serie} | {se.tipo.upper()} | 0/{len(se.ciclos)} "
                         "| — | — | — | — | — |")
                continue
            ultima_por_tipo[se.tipo] = r
            s.append(f"| {se.sessao} | {se.serie} | {se.tipo.upper()} | {r['ok']}/{r['n']} | "
                     f"{r['min']} | {r['med']:.0f} | {r['max']} | {r['primeiro']} | "
                     f"{NOME_FASE[r['mais_cara']]} |")
        if "http" in ultima_por_tipo and "https" in ultima_por_tipo:
            razao = ultima_por_tipo["https"]["med"] / ultima_por_tipo["http"]["med"]
            s += ["", f"HTTPS ÷ HTTP (medianas): {razao:.2f}×"]
    else:
        s.append("_Sem teclas 5/6 nos logs._")

    # Apêndice B
    s += ["", "## Orçamento de energia v2 (Apêndice B)", ""]
    http = ultima_por_tipo.get("http")
    if http:
        mA, origem = _corrente(eventos, corrente_forcada)
        linhas = orcamento.monta(mA, http["max"] / 1000, origem, ciclos_dia)
        s += [f"Linha do rádio com o **máximo** da última série HTTP ({http['max']} ms) e "
              f"{mA:.1f} mA (origem: {origem}). O alarme continua fora, como na AP1.", "",
              orcamento.tabela_md(linhas), "", orcamento.decisao(linhas), "",
              "Para colar no `apendices/B-orcamento-de-energia.tex`:", "",
              "```latex", orcamento.tabela_tex(linhas), "```"]
    else:
        s.append("_Sem série HTTP válida: rode a tecla 5 na placa._")

    # Conjunto de dados
    s += ["", "## Leituras (conjunto de dados)", "",
          f"- leituras confirmadas (2xx): {len(leituras.validas)} → `leituras.jsonl`, `leituras.csv`",
          f"- duplicadas descartadas: {leituras.duplicadas}",
          f"- envios sem 2xx (podem ter chegado ou não): {leituras.sem_2xx}",
          f"- corpos que não são JSON válido: {leituras.invalidas}"]
    s += [f"- lacuna: {l}" for l in leituras.lacunas]
    return "\n".join(s) + "\n"
