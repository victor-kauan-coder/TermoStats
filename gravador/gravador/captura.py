"""Terminal serial que grava tudo o que a placa imprime.

Substitui o `pio device monitor`: mostra a saída, envia as teclas digitadas
(cada linha + Enter) e salva o log bruto em dados/brutos/, que é a
procedência de todo número que sai do `processar`.
"""

from __future__ import annotations

import sys
import threading
from datetime import datetime
from pathlib import Path

import serial
from serial.tools import list_ports


def lista_portas() -> None:
    portas = list(list_ports.comports())
    if not portas:
        print("nenhuma porta serial encontrada (placa ligada? driver CH340/CP210x instalado?)")
    for p in portas:
        print(f"{p.device:8} {p.description}")


def _le_placa(porta: serial.Serial, log, parar: threading.Event) -> None:
    while not parar.is_set():
        try:
            bruto = porta.readline()
        except serial.SerialException as erro:
            print(f"\n[gravador] porta caiu: {erro}")
            parar.set()
            return
        if not bruto:
            continue
        texto = bruto.decode("utf-8", errors="replace")
        sys.stdout.write(texto)
        sys.stdout.flush()
        log.write(texto)
        log.flush()   # um corte de energia no PC não leva a sessão inteira


def capturar(porta: str, baud: int, pasta_brutos: Path) -> Path:
    pasta_brutos.mkdir(parents=True, exist_ok=True)
    destino = pasta_brutos / f"{datetime.now():%Y-%m-%d_%H%M%S}.log"

    with serial.Serial(porta, baud, timeout=0.2) as conexao, \
            open(destino, "w", encoding="utf-8", newline="") as log:
        print(f"[gravador] {porta} @ {baud} -> {destino}")
        print("[gravador] digite a tecla e Enter (ex.: 5, i135). Ctrl+C ou 'sair' encerra.")
        parar = threading.Event()
        leitor = threading.Thread(target=_le_placa, args=(conexao, log, parar), daemon=True)
        leitor.start()
        try:
            while not parar.is_set():
                comando = input()
                if comando.strip().lower() == "sair":
                    break
                conexao.write((comando + "\n").encode("utf-8"))
        except (KeyboardInterrupt, EOFError):
            pass
        finally:
            parar.set()
            leitor.join(timeout=1)
    print(f"\n[gravador] sessao salva em {destino}")
    return destino
