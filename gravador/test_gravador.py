"""Testes do gravador:  python -m unittest test_gravador   (dentro de gravador/)"""

import queue
import tempfile
import time
import unittest
from datetime import datetime
from pathlib import Path

import gravador
from gravador import analisa, checksum, interpreta, nome_teste


def com_soma(conteudo: str) -> str:
    """Linha como o gateway imprime: conteúdo + *XX."""
    return f"{conteudo}*{checksum(conteudo):02X}\n"


def linha_pkt(seq, rx, rtt=0, seq_rtt=0, falhas=0, rssi=-70, mac="AABBCCDDEEFF"):
    return com_soma(f"@PKT mac={mac} seq={seq} rx={rx} bytes=25 rssi={rssi} rtt_us={rtt} "
                    f"seq_rtt={seq_rtt} tx_falhas={falhas} temp_c=21.4 umid_pct=null "
                    f"descartados_fila=0")


def pkt(seq, rx, t=0.0, **kw):
    item = interpreta(linha_pkt(seq, rx, **kw))
    item["t"] = t
    return item


def evt(tipo, t):
    item = interpreta(com_soma(f"@EVT tipo={tipo} mac=AABBCCDDEEFF t_ms=1000"))
    item["t"] = t
    return item


class TestInterpreta(unittest.TestCase):
    def test_linha_valida(self):
        p = interpreta(linha_pkt(12, 40, rtt=9000, seq_rtt=11))
        self.assertEqual((p["tag"], p["seq"], p["rx"], p["rtt_us"]), ("PKT", 12, 40, 9000))
        self.assertIsNone(p["umid_pct"])

    def test_checksum_errado_e_rejeitado(self):
        boa = linha_pkt(4165, 100)
        corrompida = boa.replace("seq=4165", "seq=465")     # o defeito real de 24/10
        self.assertIsNotNone(interpreta(boa))
        self.assertIsNone(interpreta(corrompida))

    def test_linhas_emendadas_sao_rejeitadas(self):
        a, b = linha_pkt(1, 1).strip(), linha_pkt(2, 2).strip()
        self.assertIsNone(interpreta(a[:40] + b))

    def test_linha_sem_checksum_e_rejeitada(self):
        self.assertIsNone(interpreta("@PKT mac=AA seq=1 rx=1 bytes=25 rssi=-60 rtt_us=0 "
                                     "seq_rtt=0 tx_falhas=0"))

    def test_evento(self):
        e = interpreta(com_soma("@EVT tipo=conectado mac=AABBCCDDEEFF t_ms=5 mtu=64"))
        self.assertEqual((e["tag"], e["tipo"], e["mtu"]), ("EVT", "conectado", 64))

    def test_mensagem_comum_ignorada(self):
        self.assertIsNone(interpreta("[gateway] BLE 30:76:f5 · procurando TermoStats-sensor"))


class TestAnalisa(unittest.TestCase):
    def test_linha_perdida_na_serial_nao_vira_perda(self):
        # o pacote 11 chegou ao gateway (rx avançou), mas a linha dele foi rejeitada
        pacotes = [pkt(10, 100), pkt(12, 102), pkt(13, 103)]
        r = analisa(pacotes, [], 60, 10, rejeitadas=1)
        self.assertEqual((r["esperados"], r["recebidos"], r["perdidos"]), (4, 4, 0))
        self.assertEqual(r["perdidas_so_serial"], 1)

    def test_perda_real_no_radio(self):
        # seq pulou 11..14 e o rx não: o gateway nunca recebeu
        r = analisa([pkt(10, 100), pkt(15, 101), pkt(16, 102)], [], 60, 10)
        self.assertEqual((r["esperados"], r["recebidos"], r["perdidos"]), (7, 3, 4))

    def test_desconexao_e_tempo_sem_conexao(self):
        pacotes = [pkt(0, 1, 0.1), pkt(1, 2, 0.2), pkt(51, 3, 6.0)]
        eventos = [evt("desconectado", 0.5), evt("conectado", 5.5)]
        r = analisa(pacotes, eventos, 60, 10)
        self.assertEqual((r["desconexoes"], r["perdidos"]), (1, 49))
        self.assertAlmostEqual(r["tempo_desconectado_s"], 5.0)

    def test_teste_comeca_sem_conexao(self):
        r = analisa([pkt(0, 1, 3.0)], [evt("conectado", 2.5)], 60, 10)
        self.assertAlmostEqual(r["tempo_desconectado_s"], 2.5)

    def test_teste_termina_sem_conexao(self):
        r = analisa([pkt(0, 1, 1.0)], [evt("desconectado", 40.0)], 60, 10)
        self.assertAlmostEqual(r["tempo_desconectado_s"], 20.0)

    def test_entrega_pelo_tempo_pega_queda_no_fim(self):
        # 300 pacotes nos primeiros 30 s, depois a conexão caiu: o seq não enxerga o resto
        pacotes = [pkt(s, s + 1) for s in range(300)]
        r = analisa(pacotes, [evt("desconectado", 30.0)], 60, 10)
        self.assertEqual(r["perdidos"], 0)
        self.assertAlmostEqual(r["entrega_pct"], 50.0)

    def test_reinicio_do_sensor(self):
        r = analisa([pkt(500, 10), pkt(501, 11), pkt(0, 12), pkt(1, 13)], [], 60, 10)
        self.assertEqual((r["reinicios"], r["perdidos"], r["recebidos"]), (1, 0, 4))

    def test_duplicata(self):
        r = analisa([pkt(1, 1), pkt(2, 2), pkt(2, 3), pkt(3, 4)], [], 60, 10)
        self.assertEqual((r["duplicados"], r["recebidos"], r["perdidos"]), (1, 3, 0))

    def test_latencia_rtt_sobre_dois(self):
        pacotes = [pkt(1, 1), pkt(2, 2, rtt=30000, seq_rtt=1), pkt(3, 3, rtt=50000, seq_rtt=2)]
        r = analisa(pacotes, [], 60, 10)
        self.assertEqual(r["lat_n"], 2)
        self.assertAlmostEqual(r["lat_mediana_ms"], 20.0)

    def test_sem_pacotes(self):
        r = analisa([], [evt("desconectado", 1.0)], 60, 10)
        self.assertEqual((r["recebidos"], r["entrega_pct"], r["desconexoes"]), (0, 0.0, 1))


class LeitorFalso:
    """Imita o LeitorSerial: uma fila de (hora de chegada, bytes)."""

    def __init__(self, linhas):
        self.linhas = queue.Queue()
        self.erro = None
        for item in linhas:
            self.linhas.put(item)

    def descarta_ate_agora(self):
        pass   # o teste abaixo deixa a linha velha na fila de propósito


class TestGravaTeste(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dados_original = gravador.DADOS
        gravador.DADOS = Path(self.tmp.name)

    def tearDown(self):
        gravador.DADOS = self.dados_original
        self.tmp.cleanup()

    def test_linha_velha_antes_da_janela_e_ignorada(self):
        agora = time.monotonic() + 0.05     # as linhas novas chegam já dentro da janela
        linhas = [(agora - 30, linha_pkt(4344, 50).encode())]          # do teste anterior
        linhas += [(agora + 0.01 * i, linha_pkt(5083 + i, 790 + i).encode()) for i in range(20)]
        linhas[10] = (agora + 0.09, linha_pkt(5092, 799).replace("5092", "592").encode())
        pacotes, eventos, rejeitadas = gravador.grava_teste(LeitorFalso(linhas), 0.5, "x")
        r = analisa(pacotes, eventos, 0.5, 10, rejeitadas)
        self.assertEqual(pacotes[0]["seq"], 5083)
        self.assertEqual(rejeitadas, 1)
        self.assertEqual(r["perdidos"], 0)


class TestNome(unittest.TestCase):
    quando = datetime(2026, 10, 24, 10, 15, 0)

    def test_numero_ganha_metros(self):
        self.assertEqual(nome_teste("5", self.quando), "teste_5m_2026-10-24_101500")

    def test_texto_livre_fica_seguro(self):
        self.assertEqual(nome_teste("13m 3 paredes", self.quando),
                         "teste_13m3paredes_2026-10-24_101500")


if __name__ == "__main__":
    unittest.main()
