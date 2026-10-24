# -*- coding: utf-8 -*-
"""
grafico.py - a serie temporal da campanha, com as lacunas a mostra
AP2 · Topicos em Sistemas Computacionais I (IoT/AIoT) · UFPI 2026.2

Le o CSV do gravador e desenha temperatura e umidade pela hora da MEDICAO (ts).
Onde faltou leitura (intervalo > 1,5 periodo) a linha e cortada: nada e
interpolado. Embaixo, quantos "offline" o no teve a cada 10 min.

    pip install matplotlib
    python grafico.py ..\\dados\\campanha_24-10-26\\mensagens.csv -o serie.png
"""
import argparse, csv, json, os
from collections import Counter
from datetime import datetime
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

AQUI = os.path.dirname(os.path.abspath(__file__))
p = argparse.ArgumentParser()
p.add_argument("csv", nargs="?", default=os.path.join(AQUI, "dados", "mensagens.csv"))
p.add_argument("-o", "--saida", default="serie_temporal.png")
p.add_argument("--periodo", type=float, default=5, help="periodo_s do no, em s (padrao 5)")
a = p.parse_args()

leituras, offlines = [], Counter()
with open(a.csv, newline="", encoding="utf-8") as f:
    for ln in csv.DictReader(f):
        if ln["topico"].endswith("/status") and ln["payload"] == "offline":
            t = datetime.fromtimestamp(float(ln["t_chegada"]))
            offlines[t.replace(minute=t.minute // 10 * 10, second=0, microsecond=0)] += 1
        elif ln["topico"].endswith("/telemetria"):
            d = json.loads(ln["payload"])
            if d.get("ts") and "temp_c" in d:
                leituras.append((d["ts"], float(d["temp_c"]), float(d["umid_pct"])))
leituras.sort()


def cortada(i):                           # None entre duas leituras distantes: a linha quebra
    xs, ys = [], []
    for k, l in enumerate(leituras):
        if k and l[0] - leituras[k - 1][0] > 1.5 * a.periodo:
            xs.append(datetime.fromtimestamp(leituras[k - 1][0] + a.periodo)); ys.append(None)
        xs.append(datetime.fromtimestamp(l[0])); ys.append(l[i])
    return xs, [float("nan") if y is None else y for y in ys]


fig, (ax1, ax2, ax3) = plt.subplots(3, 1, sharex=True, figsize=(10, 7),
                                    gridspec_kw={"height_ratios": [3, 3, 1.4]})
ax1.plot(*cortada(1), color="#c0392b", lw=1.2)
ax1.set_ylabel("temperatura (°C)")
ax2.plot(*cortada(2), color="#2471a3", lw=1.2)
ax2.set_ylabel("umidade relativa (%)")
ini = min(offlines) if offlines else None
ax3.bar(list(offlines), list(offlines.values()), width=10 / 1440, align="edge", color="#7f8c8d")
ax3.set_ylabel("offline\n/ 10 min")
ax3.set_xlabel("hora da medição (ts)")
ax3.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
for ax in (ax1, ax2, ax3):
    ax.grid(alpha=0.3)
dia = datetime.fromtimestamp(leituras[0][0]).strftime("%d/%m/%Y")
ax1.set_title(f"Campanha {dia} · DHT22 a cada {a.periodo:g} s · linha cortada = lacuna (sem interpolação)")
fig.tight_layout()
fig.savefig(a.saida, dpi=150)
print(f"[grafico] {len(leituras)} leituras -> {a.saida}")
