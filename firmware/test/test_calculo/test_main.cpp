// Testes da lógica pura no PC:  pio test -e native
#include <unity.h>

#include "calculo.h"

void setUp() {}
void tearDown() {}

void test_resumo_impar() {
  uint32_t v[] = {1900, 1700, 2400, 1800, 1750};
  Resumo r = resume(v, 5);
  TEST_ASSERT_EQUAL_UINT32(1700, r.minimo);
  TEST_ASSERT_EQUAL_UINT32(1800, r.mediana);
  TEST_ASSERT_EQUAL_UINT32(2400, r.maximo);
}

void test_resumo_par_usa_media_dos_centrais() {
  uint32_t v[] = {10, 40, 20, 30};
  Resumo r = resume(v, 4);
  TEST_ASSERT_EQUAL_UINT32(25, r.mediana);
}

void test_resumo_vazio() {
  Resumo r = resume(nullptr, 0);
  TEST_ASSERT_EQUAL_UINT32(0, r.maximo);
}

void test_fase_mais_cara() {
  uint32_t fases[] = {900, 500, 50, 450, 10};
  TEST_ASSERT_EQUAL(0, indiceDoMaior(fases, 5));
}

void test_carga_do_exemplo_de_0909() {
  // 120 mA × 5 s × 96 / 3600 = 16,0 mA·h/dia (slide 28)
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 16.0f, cargaDiaria_mAh(120.0f, 5.0f, 96));
}

void test_carga_ciclo_do_projeto() {
  // 135 mA × 2 s × 144 / 3600 = 10,8 mA·h/dia
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.8f, cargaDiaria_mAh(135.0f, 2.0f, 144));
}

void test_classificacao_e_retentativa() {
  TEST_ASSERT_TRUE(classifica(201) == ClasseStatus::Sucesso);
  TEST_ASSERT_TRUE(classifica(404) == ClasseStatus::ErroDoNo);
  TEST_ASSERT_TRUE(classifica(503) == ClasseStatus::ErroDoCaminho);
  TEST_ASSERT_TRUE(classifica(-1) == ClasseStatus::SemRede);
  TEST_ASSERT_TRUE(classifica(301) == ClasseStatus::Outro);
  TEST_ASSERT_FALSE(deveRetentar(classifica(404)));
  TEST_ASSERT_TRUE(deveRetentar(classifica(503)));
  TEST_ASSERT_TRUE(deveRetentar(classifica(-11)));
  TEST_ASSERT_FALSE(deveRetentar(classifica(200)));
}

void test_espera_crescente() {
  TEST_ASSERT_EQUAL_UINT32(1000, esperaRetentativa_ms(1));
  TEST_ASSERT_EQUAL_UINT32(2000, esperaRetentativa_ms(2));
  TEST_ASSERT_EQUAL_UINT32(4000, esperaRetentativa_ms(3));
}

void test_desfaz_chunked() {
  std::string saida;
  TEST_ASSERT_TRUE(desfazChunked("4\r\nWiki\r\n6;x=1\r\npedia \r\n0\r\n\r\n", saida));
  TEST_ASSERT_EQUAL_STRING("Wikipedia ", saida.c_str());
  TEST_ASSERT_FALSE(desfazChunked("a\r\ncurto\r\n", saida));
}

void test_virgula_a_mais() {
  TEST_ASSERT_EQUAL_STRING("{\"a\":1,}", comVirgulaAMais("{\"a\":1}").c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_resumo_impar);
  RUN_TEST(test_resumo_par_usa_media_dos_centrais);
  RUN_TEST(test_resumo_vazio);
  RUN_TEST(test_fase_mais_cara);
  RUN_TEST(test_carga_do_exemplo_de_0909);
  RUN_TEST(test_carga_ciclo_do_projeto);
  RUN_TEST(test_classificacao_e_retentativa);
  RUN_TEST(test_espera_crescente);
  RUN_TEST(test_desfaz_chunked);
  RUN_TEST(test_virgula_a_mais);
  return UNITY_END();
}
