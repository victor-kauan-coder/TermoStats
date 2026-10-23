"""Gravador da Aula 10.

  python -m gravador portas                  lista as portas seriais
  python -m gravador capturar --porta COM5   terminal + log bruto em dados/brutos/
  python -m gravador processar               logs -> ciclos.csv, leituras.*, resumo.md
"""

from __future__ import annotations

import argparse
from pathlib import Path

from .orcamento import CICLOS_POR_DIA

DADOS = Path(__file__).resolve().parent.parent / "dados"


def main() -> None:
    p = argparse.ArgumentParser(prog="gravador", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="comando", required=True)

    sub.add_parser("portas", help="lista as portas seriais")

    cap = sub.add_parser("capturar", help="terminal serial que grava a sessão")
    cap.add_argument("--porta", required=True, help="ex.: COM5 ou /dev/ttyUSB0")
    cap.add_argument("--baud", type=int, default=115200)

    proc = sub.add_parser("processar", help="gera as tabelas a partir dos logs")
    proc.add_argument("logs", nargs="*", type=Path,
                      help="logs a processar (padrão: todos em dados/brutos/)")
    proc.add_argument("--ciclos-dia", type=int, default=CICLOS_POR_DIA)
    proc.add_argument("--corrente", type=float, default=None,
                      help="corrente do rádio ativo em mA (padrão: a da última tecla 5/6)")

    args = p.parse_args()

    if args.comando == "portas":
        from .captura import lista_portas
        lista_portas()
    elif args.comando == "capturar":
        from .captura import capturar
        capturar(args.porta, args.baud, DADOS / "brutos")
    elif args.comando == "processar":
        from .processamento import processar
        logs = args.logs or sorted((DADOS / "brutos").glob("*.log"))
        if not logs:
            p.error(f"nenhum log em {DADOS / 'brutos'}")
        destino = processar(logs, DADOS, args.ciclos_dia, args.corrente)
        print(f"{len(logs)} log(s) processado(s) -> {destino}")


if __name__ == "__main__":
    main()
