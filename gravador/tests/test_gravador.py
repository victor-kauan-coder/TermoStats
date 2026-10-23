"""Testes do gravador:  python -m unittest discover -s tests   (dentro de gravador/)"""

import json
import tempfile
import unittest
from pathlib import Path

from gravador import orcamento
from gravador.processamento import agrupa_series, organiza_leituras, processar
from gravador.protocolo import interpreta, le_log

# Trecho no formato exato dos printf do firmware (medicao.cpp, main.cpp, politica.cpp).
LOG = """\
sensor: DHT22 no GPIO4 respondeu (26.1 C, 58.0 %UR)
@PAYLOAD bytes=72 {"id":"termo-no01","seq":0,"ts":null,"temp_c":26.1,"umid_pct":58.0}
@NTP antes=12 depois=1790169247
@FASES origem=t1 serie=0 n=1 tipo=http assoc=900 dhcp=500 dns=40 conexao=120 http=300 desc=10 total=1870 status=200 mais_cara=assoc
@ENVIO status=200 {"id":"termo-no01","seq":0,"ts":1790169250,"temp_c":26.1,"umid_pct":58.0}
@POST quebrado=0 cabecalho=141 corpo=80 status=200 json_igual=1
@ENVIO status=200 {"id":"termo-no01","seq":1,"ts":1790169260,"temp_c":26.1,"umid_pct":58.0}
@POST quebrado=1 cabecalho=141 corpo=81 status=200 json_igual=0
@ENVIO status=200 {"id":"termo-no01","seq":2,"ts":1790169270,"temp_c":26.1,"umid_pct":58.0,}
@POLITICA url=http://httpbin.org/status/404 tentativa=1 status=404 classe=4xx espera_ms=0
@POLITICA url=http://httpbin.org/status/503 tentativa=1 status=503 classe=5xx espera_ms=1000
@CORRENTE mA=135.0 origem=bancada
@FASES origem=t5 serie=1 n=1 tipo=http assoc=1500 dhcp=600 dns=80 conexao=150 http=400 desc=10 total=2800 status=200 mais_cara=assoc
@ENVIO status=200 {"id":"termo-no01","seq":3,"ts":1790169280,"temp_c":26.2,"umid_pct":58.0}
@FASES origem=t5 serie=1 n=2 tipo=http assoc=800 dhcp=400 dns=20 conexao=100 http=300 desc=10 total=1700 status=200 mais_cara=assoc
@ENVIO status=200 {"id":"termo-no01","seq":4,"ts":1790169290,"temp_c":26.2,"umid_pct":58.0}
@FASES origem=t5 serie=1 n=3 tipo=http assoc=850 dhcp=450 dns=20 conexao=100 http=300 desc=10 total=1800 status=-1 mais_cara=assoc
@ENVIO status=-1 {"id":"termo-no01","seq":5,"ts":1790169300,"temp_c":26.2,"umid_pct":58.0}
@FASES origem=t5 serie=1 n=4 tipo=http assoc=820 dhcp=420 dns=20 conexao=100 http=300 desc=10 total=1750 status=200 mais_cara=assoc
@ENVIO status=200 {"id":"termo-no01","seq":6,"ts":1790169310,"temp_c":26.2,"umid_pct":58.0}
@ENVIO status=200 {"id":"termo-no01","seq":6,"ts":1790169310,"temp_c":26.2,"umid_pct":58.0}
@ORCAMENTO tipo=http mA=135.0 origem_mA=bancada s=2.800 ciclos_dia=144 mAh_dia=15.120
"""


class TestProtocolo(unittest.TestCase):
    def test_campos_e_tipos(self):
        e = interpreta("@FASES origem=t5 serie=1 n=2 tipo=http total=1700 status=200")
        self.assertEqual(e.tag, "FASES")
        self.assertEqual(e.campos["total"], 1700)
        self.assertEqual(e.campos["tipo"], "http")

    def test_json_no_fim(self):
        e = interpreta('@ENVIO status=200 {"id":"a b","seq":1}\r\n')
        self.assertEqual(e.campos["status"], 200)
        self.assertEqual(json.loads(e.json), {"id": "a b", "seq": 1})

    def test_float_e_prefixo_de_horario(self):
        e = interpreta("10:14:07.123 > @ORCAMENTO mA=135.0 s=2.310")
        self.assertAlmostEqual(e.campos["mA"], 135.0)

    def test_linha_comum_ignorada(self):
        self.assertIsNone(interpreta("  associar + autenticar   812 ms"))


class TestProcessamento(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.pasta = Path(self.tmp.name)
        self.log = self.pasta / "2026-09-23_101400.log"
        self.log.write_text(LOG, encoding="utf-8")
        self.eventos = le_log(self.log)

    def tearDown(self):
        self.tmp.cleanup()

    def test_serie_ignora_ciclo_sem_2xx(self):
        (serie,) = agrupa_series(self.eventos)
        r = serie.resumo()
        self.assertEqual((r["ok"], r["n"]), (3, 4))
        self.assertEqual((r["min"], r["med"], r["max"]), (1700, 1750, 2800))
        self.assertEqual(r["primeiro"], 2800)
        self.assertEqual(r["mais_cara"], "assoc")

    def test_leituras_sem_duplicata_e_com_lacuna(self):
        l = organiza_leituras(self.eventos)
        self.assertEqual([x["seq"] for x in l.validas], [0, 1, 3, 4, 6])
        self.assertEqual(l.duplicadas, 1)
        self.assertEqual(l.invalidas, 1)   # a vírgula a mais da tecla 3
        self.assertEqual(l.sem_2xx, 1)
        self.assertEqual(len(l.lacunas), 2)   # seq 2 (inválido) e seq 5 (sem 2xx)

    def test_processar_gera_arquivos(self):
        resumo = processar([self.log], self.pasta, 144, None).read_text(encoding="utf-8")
        self.assertIn("| **ciclo completo** | **1870** |", resumo)
        self.assertIn("135.0 mA (origem: bancada)", resumo)
        self.assertIn(r"\bancada{} (corrente)", resumo)
        linhas = (self.pasta / "leituras.jsonl").read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(linhas), 5)
        self.assertTrue((self.pasta / "ciclos.csv").exists())


class TestOrcamento(unittest.TestCase):
    def test_exemplo_de_0909(self):
        self.assertAlmostEqual(orcamento.carga(120, 5, 96), 16.0)

    def test_linhas_da_ap1_corrigidas(self):
        acorda, radio, dorme = orcamento.monta(135.0, 2.0, "bancada")
        self.assertAlmostEqual(acorda.carga, 22.32)
        self.assertAlmostEqual(radio.carga, 10.8)
        self.assertAlmostEqual(dorme.duracao_s, 586.0)   # 600 - 12 - 2
        self.assertAlmostEqual(dorme.carga, 46.88)

    def test_origem_estimativa(self):
        radio = orcamento.monta(120.0, 2.0, "estimativa")[1]
        self.assertIn(r"\estimativa{}", radio.origem_tex)


if __name__ == "__main__":
    unittest.main()
