"""Vida útil de baterias com um envio ESP-NOW a cada 5 minutos.

    python energia.py        (dentro de testes_24-10-26/; rode analise.py antes)

O ciclo de cada nó sensor: acorda do deep sleep, lê o DHT22, tenta enviar até
o gateway confirmar e volta a dormir. No pior caso (18 m, várias paredes)
cada envio só é confirmado com probabilidade p, medida nesta bateria; o
número de tentativas segue uma geométrica, com média 1/p.

carga [mA·h/dia] = corrente [mA] × duração [s] × ciclos/dia ÷ 3600

Saída: vida_util_baterias.png, energia.csv e as tabelas em Markdown.
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

AQUI = Path(__file__).resolve().parent

# ---------------------------------------------------------------- parâmetros
INTERVALO_S = 300                  # um envio a cada 5 min
CICLOS_DIA = 86400 // INTERVALO_S  # 288

ACORDADO_mA = 46.5      # ESP32 ativo sem rádio + DHT22 lendo (folha de dados; mesmo valor da AP1)
ACORDADO_S = 0.30       # estimativa: sair do deep sleep + iniciar WiFi/ESP-NOW + uma leitura
RADIO_mA = 240.0        # folha de dados: TX 802.11b 1 Mb/s a +19,5 dBm (pior caso: o tempo todo em TX)
TENTATIVA_S = 0.015     # estimativa: ordem da cauda de RTT medida a 11-18 m (p95 12,5-13,2 ms),
                        # que já inclui as retransmissões automáticas da camada MAC

SONO_mA = {
    "Placa de desenvolvimento": 2.0,     # bancada (AP1): regulador + conversor USB-serial acesos
    "Módulo otimizado": 0.065,           # folha de dados: ESP32 10 µA + LDO 5 µA + DHT22 em espera 50 µA
}

APROVEITAMENTO = 0.80   # profundidade de descarga útil, a mesma convenção da AP1

BATERIAS = [            # (nome, mAh nominal)
    ("LiPo 1000 mAh", 1000),
    ("LiFePO4 18650 1500 mAh", 1500),
    ("3× AA NiMH 2000 mAh", 2000),
    ("3× AA alcalina 2500 mAh", 2500),
    ("Li-ion 18650 2600 mAh", 2600),
    ("2× 18650 em paralelo 5200 mAh", 5200),
]

AZUL, LARANJA = "#2a78d6", "#eb6834"          # paleta de referência, validada
TEXTO, TEXTO_2, FUNDO, GRADE = "#0b0b0b", "#52514e", "#fcfcfb", "#e4e3df"


def p_sucesso(teste_parcial: str) -> float:
    with open(AQUI / "resultados_corrigidos.csv", encoding="utf-8") as f:
        for ln in csv.DictReader(f):
            if teste_parcial in ln["teste"]:
                return int(ln["recebidos_gateway"]) / int(ln["esperados"])
    raise SystemExit(f"rode analise.py antes: {teste_parcial} não está em resultados_corrigidos.csv")


def consumo_dia(p: float, sono_mA: float) -> dict:
    tentativas = 1 / p
    acordado = ACORDADO_mA * ACORDADO_S * CICLOS_DIA / 3600
    radio = RADIO_mA * TENTATIVA_S * tentativas * CICLOS_DIA / 3600
    sono = sono_mA * 24
    return {"tentativas": tentativas, "acordado": acordado, "radio": radio, "sono": sono,
            "total": acordado + radio + sono}


def _br(x, casas=1):
    return f"{x:,.{casas}f}".replace(",", "X").replace(".", ",").replace("X", ".")


def grafico(dias: dict[tuple[str, str, str], float], destino: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update({"font.size": 10, "axes.edgecolor": GRADE, "text.color": TEXTO,
                         "axes.labelcolor": TEXTO_2, "xtick.color": TEXTO_2,
                         "ytick.color": TEXTO})
    fig, eixos = plt.subplots(1, 2, figsize=(12, 5.2), facecolor=FUNDO)
    nomes = [b[0] for b in BATERIAS]
    y = range(len(nomes))
    alt = 0.36                                    # duas barras por bateria, com folga entre elas
    series = [("pior", "Pior caso: 18 m, várias paredes", AZUL),
              ("melhor", "Melhor caso: 1,5 m, sem obstáculo", LARANJA)]

    for ax, hw in zip(eixos, SONO_mA):
        ax.set_facecolor(FUNDO)
        maximo = max(dias[(hw, c, n)] for c, _, _ in series for n in nomes)
        for k, (caso, rotulo, cor) in enumerate(series):
            pos = [i + (k - 0.5) * (alt + 0.04) for i in y]
            vals = [dias[(hw, caso, n)] for n in nomes]
            ax.barh(pos, vals, height=alt, color=cor, label=rotulo, zorder=3)
            for py, v in zip(pos, vals):
                ax.text(v + maximo * 0.012, py, f"{v:.0f} d", va="center", fontsize=8.5,
                        color=TEXTO_2)
        ax.set_yticks(list(y))
        ax.set_yticklabels(nomes if ax is eixos[0] else [""] * len(nomes))
        ax.invert_yaxis()
        ax.set_xlim(0, maximo * 1.16)
        ax.set_xlabel("dias de autonomia")
        ax.set_title(f"{hw} (sono: {_br(SONO_mA[hw], 3).rstrip('0').rstrip(',')} mA)",
                     loc="left", fontsize=11, color=TEXTO)
        ax.grid(axis="x", color=GRADE, linewidth=0.8, zorder=0)
        for lado in ("top", "right", "left"):
            ax.spines[lado].set_visible(False)
        ax.tick_params(axis="y", length=0)

    alcas, rotulos = eixos[0].get_legend_handles_labels()
    fig.legend(alcas, rotulos, loc="upper left", bbox_to_anchor=(0.01, 0.95), ncol=2,
               frameon=False, fontsize=9.5)
    fig.suptitle("Autonomia com um envio ESP-NOW a cada 5 min (80 % da capacidade nominal)",
                 x=0.01, ha="left", fontsize=12.5, color=TEXTO)
    fig.text(0.01, 0.01, "Pior caso: p de entrega por envio medido em 24/10 (≈ 1/p tentativas "
             "por leitura). Correntes: folha de dados e bancada da AP1; tempos de tentativa: "
             "estimativa.", fontsize=8, color=TEXTO_2)
    fig.tight_layout(rect=(0, 0.04, 1, 0.89))
    fig.savefig(destino, dpi=150, facecolor=FUNDO)


def main() -> None:
    for fluxo in (sys.stdout, sys.stderr):
        fluxo.reconfigure(encoding="utf-8", errors="replace")
    casos = {"pior": p_sucesso("18m"), "melhor": p_sucesso("1.5m")}

    linhas, dias = [], {}
    print("| Hardware | Caso | p de entrega | Tentativas por leitura | Acordado | Rádio | Sono "
          "| Total (mA·h/dia) |")
    print("|---|---|---:|---:|---:|---:|---:|---:|")
    for hw, sono in SONO_mA.items():
        for caso, p in casos.items():
            c = consumo_dia(p, sono)
            print(f"| {hw} | {caso} | {_br(100 * p)} % | {_br(c['tentativas'])} | "
                  f"{_br(c['acordado'], 2)} | {_br(c['radio'], 2)} | {_br(c['sono'], 2)} | "
                  f"**{_br(c['total'], 2)}** |")
            for nome, mah in BATERIAS:
                d = mah * APROVEITAMENTO / c["total"]
                dias[(hw, caso, nome)] = d
                linhas.append({"hardware": hw, "caso": caso, "bateria": nome, "mAh": mah,
                               "mAh_dia": f"{c['total']:.3f}", "dias": f"{d:.1f}"})

    print("\n| Bateria | Placa dev, pior caso | Módulo otimizado, pior caso "
          "| Módulo otimizado, melhor caso |")
    print("|---|---:|---:|---:|")
    for nome, _ in BATERIAS:
        a = dias[("Placa de desenvolvimento", "pior", nome)]
        b = dias[("Módulo otimizado", "pior", nome)]
        m = dias[("Módulo otimizado", "melhor", nome)]
        print(f"| {nome} | {_br(a, 0)} dias | {_br(b, 0)} dias (~{_br(b / 30.4, 1)} meses) "
              f"| {_br(m, 0)} dias (~{_br(m / 365, 1)} anos) |")

    with open(AQUI / "energia.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(linhas[0]))
        w.writeheader()
        w.writerows(linhas)
    grafico(dias, AQUI / "vida_util_baterias.png")
    print("\n-> vida_util_baterias.png, energia.csv")


if __name__ == "__main__":
    main()
