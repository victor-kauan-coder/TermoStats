# -*- coding: utf-8 -*-
"""
qualidade.py - as leituras chegaram todas?
Laboratorio 03 · 30/09/2026 · Topicos em Sistemas Computacionais I (IoT/AIoT) · UFPI 2026.2

Le dados/mensagens.csv (do gravador) e, para cada no, conta pelo campo seq:
quantas leituras chegaram, quantas faltam (o seq pulou), quantas vieram repetidas,
e quanto tempo levaram do sensor ao gravador (chegada - ts). Mostra tambem a
sequencia de estados (online / offline) de cada no.

COMPLETUDE: as leituras esperadas saem de uma conta, nao do arquivo:
(fim - inicio) / periodo + 1. Com --de e --ate, a janela e a que voce deu
(o no deveria estar publicando nela inteira); sem eles, vai da primeira a
ultima leitura. O seq nao basta: se o no nao mede enquanto esta fora, o seq
nao pula, e so a conta pelo tempo mostra o buraco (as LACUNAS).

    python qualidade.py
    python qualidade.py --de 10:40 --ate 10:50
    python qualidade.py --periodo 2          (se a config mudou o periodo_s)
"""
import argparse, csv, json, os, statistics
from collections import defaultdict
from datetime import datetime

AQUI = os.path.dirname(os.path.abspath(__file__))
p = argparse.ArgumentParser()
p.add_argument("csv", nargs="?", default=os.path.join(AQUI, "dados", "mensagens.csv"))
p.add_argument("--de", help="inicio da janela, HH:MM")
p.add_argument("--ate", help="fim da janela, HH:MM")
p.add_argument("--periodo", type=float, default=5, help="periodo_s do no, em s (padrao 5)")
a = p.parse_args()


def hora(t):
    return datetime.fromtimestamp(t).strftime("%H:%M:%S")


def no_dia(t, hhmm, seg):                 # "HH:MM" no dia da leitura t -> epoch
    h, m = map(int, hhmm.split(":"))
    return datetime.fromtimestamp(t).replace(hour=h, minute=m, second=seg, microsecond=0).timestamp()


def dentro(chegada):                      # compara "HH:MM:SS" com "HH:MM" como texto
    return (not a.de or chegada >= a.de) and (not a.ate or chegada[:len(a.ate)] <= a.ate)


if not os.path.exists(a.csv):
    raise SystemExit(f"Ainda nao ha dados em {a.csv}\n"
                     "Rode primeiro o gravador (ele cria esse arquivo) e espere chegar alguma mensagem:\n"
                     "  python gravador.py --host ENDERECO_DO_BROKER --usuario eqNN")

leituras = defaultdict(list)              # id -> [(seq, ts, t_chegada)]
estados = defaultdict(list)               # no -> [(t_chegada, estado)]
with open(a.csv, newline="", encoding="utf-8") as f:
    for ln in csv.DictReader(f):
        if not dentro(ln["chegada"]):
            continue
        t, partes = float(ln["t_chegada"]), ln["topico"].split("/")
        if partes[-1] == "status":
            estados[f"{partes[1]}-{partes[2]}"].append((t, ln["payload"]))
        elif partes[-1] == "telemetria":
            try:
                d = json.loads(ln["payload"])
                leituras[d["id"]].append((int(d["seq"]), d.get("ts"), t))
            except (ValueError, KeyError, TypeError):
                print(f"mensagem que nao segue o contrato: {ln['payload'][:60]}")

for no in sorted(set(leituras) | set(estados)):
    print(f"\n=== {no}")
    lista = leituras.get(no, [])
    if lista:
        seqs = [s for s, _, _ in lista]
        # o seq volta a 1 quando o no reinicia: cada "volta" e contada separado
        trechos, atual = [], [seqs[0]]
        for s in seqs[1:]:
            if s < atual[-1] and s <= 2:
                trechos.append(atual); atual = [s]
            else:
                atual.append(s)
        trechos.append(atual)
        faltam = sum(max(tr) - min(tr) + 1 - len(set(tr)) for tr in trechos)
        repetidas = sum(len(tr) - len(set(tr)) for tr in trechos)
        atrasos = [tc - ts for _, ts, tc in lista if ts]
        sem_hora = sum(1 for _, ts, _ in lista if not ts)
        print(f"  leituras recebidas  {len(lista)}   (seq {seqs[0]} a {seqs[-1]})")
        print(f"  faltam (seq pulou)  {faltam}")
        print(f"  repetidas           {repetidas}")
        if len(trechos) > 1:
            print(f"  reinicios do no     {len(trechos) - 1}   (o seq voltou a 1)")
        if sem_hora:
            print(f"  sem hora (ts null)  {sem_hora}   (o relogio do no ainda nao tinha acertado)")
        if atrasos:
            print(f"  atraso chegada-ts   mediana {statistics.median(atrasos):.0f} s · maximo {max(atrasos):.0f} s")

        # hora da MEDICAO (ts); sem ts, a da chegada
        medidas = sorted(ts or tc for _, ts, tc in lista)
        ini = no_dia(medidas[0], a.de, 0) if a.de else medidas[0]
        fim = no_dia(medidas[-1], a.ate, 59) if a.ate else medidas[-1]
        esperadas = int((fim - ini) // a.periodo) + 1
        unicas = len(lista) - repetidas
        print(f"  esperadas           {esperadas}   ({hora(ini)} a {hora(fim)}, {(fim - ini) / 60:.1f} min / {a.periodo:g} s)")
        print(f"  completude          {100 * unicas / esperadas:.1f}%   ({unicas} unicas de {esperadas})")

        # lacunas: intervalo entre medicoes maior que 1,5 periodo
        bordas = [ini] + medidas + [fim]
        lacunas = [(t0, t1) for t0, t1 in zip(bordas, bordas[1:]) if t1 - t0 > 1.5 * a.periodo]
        if lacunas:
            total = sum(t1 - t0 for t0, t1 in lacunas)
            print(f"  lacunas             {len(lacunas)}   (somam {total:.0f} s)")
            for t0, t1 in sorted(lacunas, key=lambda l: l[0] - l[1])[:10]:
                print(f"    {hora(t0)} a {hora(t1)}   {t1 - t0:5.0f} s   ~{round((t1 - t0) / a.periodo) - 1} leituras")
    trans = []
    for t, e in estados.get(no, []):
        if not trans or trans[-1][1] != e:
            trans.append((t, e))
    if trans:
        print("  status              " + "  >  ".join(f"{e or '(vazio)'} {hora(t)}" for t, e in trans))
