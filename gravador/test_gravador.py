"""Testes do gravador:  python -m unittest test_gravador   (dentro de gravador/)"""

import unittest
from datetime import datetime

from gravador import analisa, interpreta, nome_teste


def pkt(seq, rtt=0, seq_rtt=0, falhas=0, mac="AABBCCDDEEFF", rssi=-60):
    linha = (f"@PKT mac={mac} seq={seq} bytes=25 rssi={rssi} rtt_us={rtt} seq_rtt={seq_rtt} "
             f"tx_falhas={falhas} temp_c=26.1 umid_pct=null descartados_fila=0")
    return interpreta(linha)


class TestInterpreta(unittest.TestCase):
    def test_linha_do_gateway(self):
        p = interpreta("@PKT mac=24DCC3A1B2C3 seq=12 bytes=25 rssi=-71 rtt_us=3120 "
                       "seq_rtt=11 tx_falhas=0 temp_c=26.1 umid_pct=null descartados_fila=0\r\n")
        self.assertEqual(p["mac"], "24DCC3A1B2C3")
        self.assertEqual(p["seq"], 12)
        self.assertEqual(p["rssi"], -71)
        self.assertAlmostEqual(p["temp_c"], 26.1)
        self.assertIsNone(p["umid_pct"])

    def test_mac_so_com_digitos_continua_texto(self):
        self.assertEqual(interpreta("@PKT mac=000000000001 seq=1")["mac"], "000000000001")

    def test_outras_linhas_ignoradas(self):
        self.assertIsNone(interpreta("[gateway] MAC 24:DC:C3 · canal 1"))
        self.assertIsNone(interpreta(""))


class TestAnalisa(unittest.TestCase):
    def test_perda_pelo_seq(self):
        pacotes = [pkt(s) for s in (10, 11, 13, 14, 17)]   # faltam 12, 15, 16
        r = analisa(pacotes, 60)["AABBCCDDEEFF"]
        self.assertEqual((r["recebidos"], r["esperados"], r["perdidos"]), (5, 8, 3))
        self.assertAlmostEqual(r["perda_pct"], 37.5)

    def test_duplicata_nao_conta_como_recebido(self):
        r = analisa([pkt(1), pkt(2), pkt(2), pkt(3)], 60)["AABBCCDDEEFF"]
        self.assertEqual((r["recebidos"], r["duplicados"], r["perdidos"]), (3, 1, 0))

    def test_reinicio_do_sensor_nao_vira_perda(self):
        r = analisa([pkt(500), pkt(501), pkt(0), pkt(1)], 60)["AABBCCDDEEFF"]
        self.assertEqual((r["reinicios"], r["perdidos"], r["recebidos"]), (1, 0, 4))

    def test_latencia_e_rtt_sobre_dois_sem_repetir_seq(self):
        pacotes = [pkt(1), pkt(2, rtt=4000, seq_rtt=1), pkt(3, rtt=4000, seq_rtt=1),
                   pkt(4, rtt=8000, seq_rtt=3)]
        r = analisa(pacotes, 60)["AABBCCDDEEFF"]
        self.assertEqual(r["lat_n"], 2)               # seq 1 e seq 3
        self.assertAlmostEqual(r["lat_mediana_ms"], 3.0)
        self.assertAlmostEqual(r["lat_max_ms"], 4.0)

    def test_rtt_de_antes_do_teste_e_ignorado(self):
        r = analisa([pkt(50, rtt=9000, seq_rtt=49), pkt(51)], 60)["AABBCCDDEEFF"]
        self.assertEqual(r["lat_n"], 0)
        self.assertIsNone(r["lat_mediana_ms"])

    def test_taxa_e_falhas_mac(self):
        pacotes = [pkt(s, falhas=2 + s) for s in range(600)]
        r = analisa(pacotes, 60)["AABBCCDDEEFF"]
        self.assertAlmostEqual(r["pacotes_s"], 10.0)
        self.assertAlmostEqual(r["bytes_s"], 250.0)
        self.assertAlmostEqual(r["bits_s"], 2000.0)
        self.assertEqual(r["falhas_mac"], 599)

    def test_dois_sensores_separados(self):
        res = analisa([pkt(1, mac="A1"), pkt(1, mac="B2"), pkt(3, mac="B2")], 60)
        self.assertEqual(res["A1"]["perdidos"], 0)
        self.assertEqual(res["B2"]["perdidos"], 1)

    def test_sem_pacotes(self):
        self.assertEqual(analisa([], 60), {})


class TestNome(unittest.TestCase):
    quando = datetime(2026, 10, 24, 10, 15, 0)

    def test_numero_ganha_metros(self):
        self.assertEqual(nome_teste("5", self.quando), "teste_5m_2026-10-24_101500")

    def test_virgula_decimal(self):
        self.assertEqual(nome_teste("2,5", self.quando), "teste_2.5m_2026-10-24_101500")

    def test_texto_livre_fica_seguro(self):
        self.assertEqual(nome_teste("10m parede/porta", self.quando),
                         "teste_10mparede_porta_2026-10-24_101500")


if __name__ == "__main__":
    unittest.main()
