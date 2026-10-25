# Relatório técnico de ciclo de projeto — modelo LaTeX

**Tópicos em Sistemas Computacionais I (IoT/AIoT) · UFPI · 2026.2**

Este é o modelo oficial da documentação de projeto da disciplina. Ele serve
às três avaliações parciais: trocar um número em um arquivo liga e desliga os
capítulos específicos de cada ciclo.

---

## Novidades da versão AP2 (17/10/2026)

Esta versão já vem com `\def\CICLO{2}`. O que mudou em relação à versão da AP1:

| Arquivo | O que mudou |
|---|---|
| `conteudo/05-conectividade.tex` | **reescrito**: oito seções, uma para cada instrumento do Módulo 2 (matriz, caminho do dado, contrato de dados, contrato de tópicos, custo do rádio, campanha, qualidade, descrição do conjunto de dados) |
| `apendices/E-checklist-de-prontidao.tex` | **novo**: o checklist de 06/10 com a coluna *Na campanha* |
| `apendices/F-registro-da-campanha.tex` | **novo**: diário da campanha, registro de eventos para rotulagem e a saída do `qualidade.py` |
| `main.tex` | inclui os apêndices E e F quando `\CICLO` é 2 ou 3 |
| `config/comandos.tex` | três marcas da matriz: `\serve`, `\ressalva`, `\naoserve` |
| `config/dados-da-equipe.tex` | `\CICLO{2}`, data de 25/10 e os papéis do Módulo 2 |

**A equipe que já tem o projeto da AP1 no Overleaf não começa do zero.** Faça uma cópia do
projeto (Menu → *Copy Project*), chame-a de AP2 e envie para a cópia os cinco arquivos do
pacote `Arquivos-novos-AP2.zip`, substituindo os que existirem: `main.tex`,
`config/comandos.tex`, `conteudo/05-conectividade.tex` e os dois apêndices novos. Depois, em
`config/dados-da-equipe.tex`, troque `\CICLO{1}` por `\CICLO{2}`, a data e os papéis.
Os capítulos 1 a 4 e 6 a 9 continuam sendo os da equipe: eles são **atualizados**, não
reescritos (as instruções de entrega dizem o que mudar em cada um).

Se a equipe mexeu no `main.tex` da AP1, não substitua o arquivo: acrescente, logo depois da
linha `\input{apendices/D-declaracao-de-ia}`, as quatro linhas que incluem os apêndices E e F.

---

## Começar em cinco minutos

### No Overleaf (recomendado)

1. Descompacte nada: no Overleaf, **New Project → Upload Project** e envie o
   `.zip` como está.
2. Confirme que o arquivo principal é o `main.tex` **da raiz**
   (Menu → Main document). A pasta `exemplo/` tem um `main.tex` próprio; ele
   não deve ser o principal.
3. Aperte **Recompile**. O documento fecha sem erro já na primeira vez.
4. Abra `config/dados-da-equipe.tex` e preencha.

### Na sua máquina

Requer uma distribuição TeX com a classe `abntex2` (TeX Live completo, ou
`apt install texlive-publishers texlive-lang-portuguese texlive-latex-extra`).

```
latexmk -pdf main.tex
```

O `latexmkrc` já está configurado para rodar o BibTeX e as passagens
necessárias.

---

## O único arquivo que a equipe edita para configurar

`config/dados-da-equipe.tex`. Nada mais. Ele contém:

| O quê | Para quê |
|---|---|
| `\def\CICLO{1}` | **1**, **2** ou **3**. Define qual AP o relatório documenta. |
| `\def\MOSTRARNOTAS{}` | Liga as caixas cinzas de instrução. **Comente esta linha antes de entregar.** |
| título, subtítulo, equipe, repositório | vão para a capa e para os metadados do PDF |
| `\listaIntegrantes` | uma linha por integrante; alimenta a capa, a divisão de trabalho e a declaração de IA |
| dados institucionais e data | capa e folha de rosto |

### O que o `\CICLO` muda

| | AP1 | AP2 | AP3 |
|---|:--:|:--:|:--:|
| Problema, requisitos, arquitetura, implementação | ✓ | ✓ | ✓ |
| Cap. *Conectividade e conjunto de dados* | — | ✓ | ✓ |
| Cap. *Pipeline de dados, modelo e inferência embarcada* | — | — | ✓ |
| Testes, resultados, conclusão, apêndices | ✓ | ✓ | ✓ |
| Título do ciclo na capa e nas etiquetas de rubrica | automático | automático | automático |

---

## Estrutura das pastas

```
main.tex                    ordem dos capítulos; normalmente não se mexe
config/
  dados-da-equipe.tex       ← o arquivo da equipe
  pacotes.tex               pacotes e estilo de código
  comandos.tex              caixas de decisão, rubrica, vereditos
  estilo.tex                capa, folha de rosto, metadados
conteudo/
  00-resumo.tex             resumo e abstract
  00-siglas.tex             lista de siglas
  01-problema.tex           … até …
  09-conclusao.tex
apendices/
  A-mapa-de-barramento.tex  instrumento da Aula 06, já formatado
  B-orcamento-de-energia.tex instrumento da Aula 08, já formatado
  C-plano-de-testes.tex     um quadro por caso de teste
  D-declaracao-de-ia.tex    declaração de uso de IA generativa
figuras/                    coloque aqui as imagens do projeto
referencias.bib             bibliografia (ABNT, autor-data)
exemplo/                    relatório fictício completo, para consulta
```

Se você colocar um arquivo `figuras/logo-ufpi.png`, ele aparece no topo da capa
automaticamente.

---

## Comandos que valem a pena conhecer

**Caixa de decisão de projeto** — é o instrumento da fatia de 25 % da rubrica.
Numera sozinha (DP-1, DP-2, …) e aceita `\label`/`\ref`.

```latex
\begin{decisao}{Escolha do sensor de temperatura}
  \alternativas{NTC, DS18B20, PT100.}
  \criterio{Faixa útil abaixo de -20 °C e custo.}
  \escolha{DS18B20 estanque.}
  \evidencia{Dispersão de 18 contagens em cabo de 2 m; folha de dados.}
  \consequencia{Conversão de 750 ms obriga laço não bloqueante.}
\end{decisao}
```

**Procedência de um número** — `\folhadedados`, `\bancada`, `\estimativa`.
Estimativa é resposta legítima, desde que declarada.

**Vereditos de teste** — `\aprovado`, `\parcial`, `\reprovado`.

**Identificadores** — `\RF{01}`, `\RNF{02}`, `\CT{03}`, `\DP{1}`.

**Código do ESP32** —

```latex
\begin{lstlisting}[style=arduino, caption={...}, label={lst:laco}]
void loop() { ... }
\end{lstlisting}
```

**Citações ABNT** — `\cite{alfuqaha2015}` (entre parênteses) e
`\citeonline{alfuqaha2015}` (no corpo da frase).

---

## Onde cada capítulo responde à rubrica

A rubrica da AP é a mesma nos três ciclos. Cada capítulo traz, sob o título,
uma etiqueta discreta indicando a que fatia ele responde.

| Fatia da rubrica | Peso | Onde ela é ganha |
|---|:--:|---|
| Funcionalidade do protótipo | 30 % | Cap. 4 (implementação); na AP2, também a Seção 5.2 (o caminho do dado funcionando); Cap. 6, na AP3 |
| Justificativa técnica das decisões | 25 % | Cap. 1, Cap. 3 (caixas DP-*n*); na AP2, as Seções 5.1 e 5.3 a 5.5 |
| Robustez, testes e documentação | 25 % | Cap. 2 (critérios de aceitação), Cap. 7 e apêndices A a C; na AP2, as Seções 5.6 a 5.8 e os apêndices E e F |
| Comunicação dos resultados | 20 % | resumo, Cap. 8 e Cap. 9 (a AP2 não tem apresentação: esta fatia é ganha só no relatório) |

Antes de entregar, confira na tabela de rastreabilidade (Seção 2.4) se todo
requisito tem implementação e caso de teste. É a checagem que mais rende.

---

## Antes de entregar

1. Comente `\def\MOSTRARNOTAS{}` em `config/dados-da-equipe.tex` e recompile.
2. Confira que não sobrou nenhuma reticência `\ldots` nem texto de exemplo.
3. Confira que o *commit* citado no Capítulo 4 existe no repositório.
4. Envie o **PDF** pelo SIGAA e deixe as fontes `.tex` em `docs/` no
   repositório da equipe.
5. Na AP2: confira que o arquivo de dados citado na Seção 5.8 está no repositório, no
   *commit* indicado, e que a saída do `qualidade.py` do Apêndice F foi gerada sobre ele.

---

## O exemplo preenchido

A pasta `exemplo/` traz um relatório de **AP1** completo e fictício (não há exemplo de AP2) — o
*Sentinela*, monitor de temperatura de um freezer de amostras. Ele existe para
mostrar o **nível de detalhe** esperado, não para ser copiado: o projeto é
outro, o problema é outro e as decisões são outras.

Vale ler, em particular, três coisas que costumam faltar nos relatórios:

- a caixa **DP-4**, em que uma alternativa foi descartada por uma medição, e
  não por preferência;
- a seção **7.3, *O que falhou***, que descreve três erros reais e o que cada
  um ensinou;
- o fecho do **Apêndice B**, que identifica quem realmente dimensiona o
  sistema — e não é o firmware.

O exemplo declara dois requisitos não plenamente atendidos. Isso é
deliberado: relatório honesto sobre o que não funcionou vale mais, nesta
disciplina, do que relatório que declara sucesso em tudo.
