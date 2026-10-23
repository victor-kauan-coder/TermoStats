"""Contrato entre o firmware e o gravador: as linhas que começam com "@".

Formato:  @TAG chave=valor chave=valor ... [{json}]

Exemplos que o firmware imprime:
  @FASES origem=t5 serie=1 n=3 tipo=http assoc=812 dhcp=430 ... total=1694 status=200 mais_cara=assoc
  @ENVIO status=200 {"id":"termo-no01","seq":7,"ts":1790169247,"temp_c":26.1,"umid_pct":58.0}
  @ORCAMENTO tipo=http mA=135.0 origem_mA=bancada s=2.310 ciclos_dia=144 mAh_dia=12.474
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

# O "@" pode vir depois de um prefixo (por exemplo, o filtro "time" do pio monitor).
_PADRAO = re.compile(r"@([A-Z]+)(?:\s+(.*))?$")


@dataclass
class Evento:
    tag: str
    campos: dict = field(default_factory=dict)
    json: str | None = None     # texto cru do JSON no fim da linha, se houver
    sessao: str = ""            # nome do arquivo de log de onde veio
    linha: int = 0


def _valor(texto: str):
    for tipo in (int, float):
        try:
            return tipo(texto)
        except ValueError:
            pass
    return texto


def interpreta(linha: str) -> Evento | None:
    """Converte uma linha do monitor serial em Evento, ou None se não for "@"."""
    m = _PADRAO.search(linha.rstrip("\r\n"))
    if not m:
        return None
    evento = Evento(tag=m.group(1))
    resto = (m.group(2) or "").strip()
    while resto:
        if resto.startswith("{"):
            evento.json = resto
            break
        token, _, resto = resto.partition(" ")
        resto = resto.lstrip()
        chave, igual, valor = token.partition("=")
        if igual:
            evento.campos[chave] = _valor(valor)
    return evento


def le_log(caminho: Path) -> list[Evento]:
    eventos = []
    with open(caminho, encoding="utf-8", errors="replace") as f:
        for numero, linha in enumerate(f, start=1):
            evento = interpreta(linha)
            if evento:
                evento.sessao = caminho.stem
                evento.linha = numero
                eventos.append(evento)
    return eventos
