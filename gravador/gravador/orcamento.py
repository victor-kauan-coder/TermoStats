"""Orçamento de energia v2 (Apêndice B), com a linha do rádio medida.

carga [mA·h/dia] = corrente [mA] × duração [s] × ciclos/dia ÷ 3600
"""

from __future__ import annotations

from dataclasses import dataclass

# Constantes do Apêndice B da AP1 (ciclo de 10 min do RF-01).
CICLOS_POR_DIA = 144
PERIODO_S = 600.0
ACORDADO_mA = 46.5          # ESP32 ativo + DHT22 em leitura (folha de dados)
ACORDADO_S = 12.0
SONO_mA = 2.0               # deep sleep da placa (bancada)
CAPACIDADE_UTIL_mAh = 2600 * 0.80   # 18650 com 80 % de profundidade de descarga


def carga(corrente_mA: float, duracao_s: float, ciclos_dia: int) -> float:
    return corrente_mA * duracao_s * ciclos_dia / 3600


@dataclass
class Linha:
    estado: str
    ligado: str
    corrente_mA: float
    duracao_s: float
    ciclos_dia: int
    origem_tex: str
    origem_md: str

    @property
    def carga(self) -> float:
        return carga(self.corrente_mA, self.duracao_s, self.ciclos_dia)


def monta(radio_mA: float, radio_s: float, origem_corrente: str,
          ciclos_dia: int = CICLOS_POR_DIA) -> list[Linha]:
    """As três linhas medidas do orçamento. O alarme continua fora (não medido)."""
    corrente_tex = r"\bancada{}" if origem_corrente == "bancada" else r"\estimativa{}"
    sono_s = PERIODO_S - ACORDADO_S - radio_s
    return [
        Linha("acorda e lê", "ESP32 ativo + DHT22 em leitura", ACORDADO_mA, ACORDADO_S,
              ciclos_dia, r"\folhadedados", "folha de dados"),
        Linha("associa WiFi e publica",
              "ESP32 + rádio WiFi: associa, DHCP, DNS, POST, desliga",
              radio_mA, radio_s, ciclos_dia,
              rf"{corrente_tex} (corrente) + \bancada{{}} (tempo: máximo de 10 ciclos)",
              f"corrente: {origem_corrente} · tempo: bancada da equipe (máximo)"),
        Linha("dorme", "ESP32 em deep sleep", SONO_mA, sono_s, ciclos_dia,
              r"\bancada", "bancada"),
    ]


def _br(x: float, casas: int = 2) -> str:
    return f"{x:.{casas}f}".replace(".", ",")


def tabela_md(linhas: list[Linha]) -> str:
    total = sum(l.carga for l in linhas)
    saida = ["| Estado | mA | s | ciclos/dia | mA·h/dia | Origem |",
             "|---|---:|---:|---:|---:|---|"]
    for l in linhas:
        saida.append(f"| {l.estado} | {_br(l.corrente_mA, 1)} | {_br(l.duracao_s)} | "
                     f"{l.ciclos_dia} | {_br(l.carga)} | {l.origem_md} |")
    saida.append(f"| **Total** | | | | **{_br(total)}** | |")
    return "\n".join(saida)


def tabela_tex(linhas: list[Linha]) -> str:
    """Linhas no formato da tabela tab:energia do Apêndice B."""
    saida = []
    for l in linhas:
        saida.append(f"{l.estado} & {l.ligado} & {_br(l.corrente_mA, 1)} & "
                     f"{_br(l.duracao_s)} & {l.ciclos_dia} & {_br(l.carga)} & "
                     f"{l.origem_tex} \\\\")
    total = sum(l.carga for l in linhas)
    saida.append(r"\midrule")
    saida.append(r"\multicolumn{5}{@{}r}{\textbf{Total diário (excluindo o alarme, "
                 rf"ainda não medido)}}}} & \textbf{{{_br(total, 1)}}} & \\")
    return "\n".join(saida)


def decisao(linhas: list[Linha]) -> str:
    """As frases que o Apêndice B pede: consumo, autonomia e quem dimensiona."""
    total = sum(l.carga for l in linhas)
    maior = max(linhas, key=lambda l: l.carga)
    fatia = 100 * maior.carga / total
    return (f"Consumo diário de {_br(total, 1)} mA·h (corrente média de "
            f"{_br(total / 24)} mA). Autonomia com a 18650 ({_br(CAPACIDADE_UTIL_mAh, 0)} mA·h "
            f"úteis): {_br(CAPACIDADE_UTIL_mAh / total, 1)} dias. "
            f"Quem dimensiona o sistema agora é o estado \"{maior.estado}\", "
            f"com {_br(fatia, 0)} % do consumo diário.")
