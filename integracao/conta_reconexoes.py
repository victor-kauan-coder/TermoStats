"""Tempo de reconexão do nó, contado sobre o CSV bruto do gravador.

Para cada `offline` publicado pelo broker (testamento) no tópico status, mede o
tempo até o `online` seguinte do nó. As duas horas são de chegada ao gravador
(t_chegada), então o trânsito até ele se cancela. Mensagens retidas e as que
chegaram em lote (gravador parado, sessão persistente) ficam de fora, porque a
hora de chegada delas não é a hora do evento.

Uso:
    python integracao/conta_reconexoes.py dados/campanha_24-10-26/mensagens.csv \
        --lote 16:10:14 16:10:17

Só usa a biblioteca padrão.
"""
import argparse
import csv
import statistics
from datetime import datetime


def hora(t):
    return datetime.fromtimestamp(t).strftime("%H:%M:%S")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csv")
    ap.add_argument("--lote", nargs=2, metavar=("INICIO", "FIM"), action="append", default=[],
                    help="janela de chegada em lote a excluir (HH:MM:SS HH:MM:SS); pode repetir")
    a = ap.parse_args()

    with open(a.csv, newline="", encoding="utf-8") as f:
        linhas = list(csv.DictReader(f))

    eventos = [(float(l["t_chegada"]), l["chegada"], l["payload"]) for l in linhas
               if l["topico"].endswith("/status") and l["retido"] == "0"]

    def no_lote(chegada):
        return any(ini <= chegada <= fim for ini, fim in a.lote)

    tempos = []
    for (t0, c0, p0), (t1, c1, p1) in zip(eventos, eventos[1:]):
        if p0 == "offline" and p1 == "online" and not no_lote(c0) and not no_lote(c1):
            tempos.append((t1 - t0, t0))

    if not tempos:
        print("nenhum par offline -> online")
        return
    valores = sorted(d for d, _ in tempos)
    n = len(valores)

    def pct(p):
        return valores[min(n - 1, round(p * (n - 1)))]

    print(f"pares offline -> online   {n}")
    print(f"mediana                   {statistics.median(valores):.1f} s")
    print(f"p90 / p99                 {pct(0.90):.1f} s / {pct(0.99):.1f} s")
    print(f"minimo / maximo           {valores[0]:.1f} s / {valores[-1]:.1f} s")
    for limite in (5, 10, 30):
        k = sum(v <= limite for v in valores)
        print(f"ate {limite:>2} s                  {k} ({100 * k / n:.1f}%)")
    for d, t0 in sorted(tempos, reverse=True)[:3]:
        print(f"  maior: {d:.1f} s a partir do offline de {hora(t0)}")


if __name__ == "__main__":
    main()
