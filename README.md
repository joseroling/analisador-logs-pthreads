# Analisador de Logs de Alta Performance com Pthreads

Projeto prático da disciplina de **Computação Paralela** da Faculdade de Computação e Informática (FCI) — Universidade Presbiteriana Mackenzie.

O projeto consiste no desenvolvimento de um analisador de arquivos de log HTTP utilizando a linguagem **C** e **Pthreads**, comparando uma implementação sequencial com diferentes abordagens de processamento paralelo.

## Objetivo

O objetivo principal é analisar arquivos de log no formato **Common Log Format (CLF)** e comparar o desempenho entre:

* Processamento sequencial;
* Processamento paralelo utilizando `pthread` e `mutex`;
* Processamento paralelo otimizado utilizando **redução local**.

Além da análise dos dados, o projeto avalia **speedup, eficiência, escalabilidade e impacto da granularidade dos blocos** no desempenho da aplicação.

---

## Tecnologias utilizadas

* **C**
* **Pthreads**
* **GCC**
* **Linux / WSL**
* **Makefile**
* **Valgrind**
* **perf**
* **gprof**
* **Python** para automação dos experimentos e geração de gráficos

---

## Estrutura do projeto

```text
analisador-logs-pthreads/
│
├── log_common.h
│
├── log_analyzer_seq.c
│
├── log_analyzer_par.c
│
├── log_analyzer_par_optimized.c
│
├── log_parser.c
├── log_parser.h
│
├── parallel_util.c
├── parallel_util.h
│
├── run_experiments.py
├── plot_graphs.py
├── profile.sh
│
├── Makefile
│
└── README.md
```

### Principais arquivos

| Arquivo                        | Descrição                                           |
| ------------------------------ | --------------------------------------------------- |
| `log_common.h`                 | Estruturas e tipos utilizados pelo analisador       |
| `log_analyzer_seq.c`           | Implementação sequencial                            |
| `log_analyzer_par.c`           | Implementação paralela utilizando mutex             |
| `log_analyzer_par_optimized.c` | Implementação paralela com redução local            |
| `log_parser.c`                 | Funções responsáveis pelo parsing das linhas do log |
| `log_parser.h`                 | Declarações das funções de parsing                  |
| `parallel_util.c`              | Funções auxiliares para o processamento paralelo    |
| `parallel_util.h`              | Declarações das funções auxiliares                  |
| `run_experiments.py`           | Automação dos experimentos                          |
| `plot_graphs.py`               | Geração dos gráficos                                |
| `profile.sh`                   | Automação de profiling                              |
| `Makefile`                     | Compilação e limpeza do projeto                     |

---

# Estatísticas analisadas

## Nível 1

O programa calcula:

* Total de requisições;
* Quantidade de respostas `404`;
* Quantidade de respostas `200`;
* Total de bytes das respostas `200`;
* Média de bytes por requisição;
* Taxa geral de erros.

## Nível 2

Também são analisadas:

* Top 10 URLs;
* Top 10 endereços IP;
* Distribuição das requisições por hora;
* Distribuição dos códigos HTTP;
* Análise dos métodos HTTP.

Os códigos HTTP considerados incluem:

```text
200
301
302
400
403
404
500
502
503
Outros
```

Os métodos HTTP considerados incluem:

```text
GET
POST
PUT
DELETE
Outros
```

---

# Compilação

O projeto utiliza `gcc` com suporte a Pthreads.

Para compilar:

```bash
make
```

Para remover os executáveis gerados:

```bash
make clean
```

A compilação utiliza opções como:

```text
-Wall
-Wextra
-O3
-pthread
```

---

# Execução

## Versão sequencial

```bash
./log_analyzer_seq arquivo.log
```

Exemplo:

```bash
./log_analyzer_seq access.log
```

---

## Versão paralela com mutex

A quantidade de threads é informada como segundo argumento:

```bash
./log_analyzer_par arquivo.log NUM_THREADS
```

Exemplos:

```bash
./log_analyzer_par access.log 1
./log_analyzer_par access.log 2
./log_analyzer_par access.log 4
./log_analyzer_par access.log 8
./log_analyzer_par access.log 16
```

---

## Versão paralela otimizada

A versão otimizada utiliza **redução local**, evitando o uso de mutex durante o processamento principal das linhas.

Execução:

```bash
./log_analyzer_par_optimized arquivo.log NUM_THREADS
```

Exemplos:

```bash
./log_analyzer_par_optimized access.log 1
./log_analyzer_par_optimized access.log 2
./log_analyzer_par_optimized access.log 4
./log_analyzer_par_optimized access.log 8
./log_analyzer_par_optimized access.log 16
```

---

# Estratégias de paralelização

## Implementação com Mutex

Na primeira implementação paralela, as threads processam diferentes partes do arquivo e atualizam estruturas compartilhadas.

O acesso às estruturas compartilhadas é protegido utilizando:

```c
pthread_mutex_t
```

Isso garante que duas ou mais threads não modifiquem simultaneamente os mesmos dados de maneira incorreta.

Uma consequência dessa abordagem é a existência de **overhead de sincronização**, principalmente quando muitas threads tentam acessar os mesmos recursos compartilhados.

---

## Implementação com Redução Local

Na versão otimizada, cada thread mantém suas próprias estatísticas durante o processamento.

Ao final da execução, as estatísticas locais são combinadas em uma estrutura global.

A ideia pode ser representada por:

```text
              Arquivo de Log
                    |
        +-----------+-----------+
        |           |           |
     Thread 1    Thread 2    Thread N
        |           |           |
   Estatísticas  Estatísticas  Estatísticas
     locais        locais        locais
        |           |           |
        +-----------+-----------+
                    |
              Redução/Merge
                    |
             Resultado final
```

Essa abordagem reduz a necessidade de sincronização durante o processamento principal.

---

# Experimentos de desempenho

O projeto permite analisar diferentes configurações de execução.

## Strong Scaling

São utilizadas diferentes quantidades de threads:

```text
1
2
4
8
16
```

O objetivo é observar como o tempo de execução varia conforme aumentamos o número de threads.

Também são calculados:

### Speedup

```text
Speedup = T1 / Tp
```

Onde:

* `T1` = tempo utilizando 1 thread;
* `Tp` = tempo utilizando `p` threads.

### Eficiência

```text
Eficiência = Speedup / p
```

---

## Granularidade

Também é analisado o impacto do tamanho dos blocos utilizados para dividir o arquivo entre as threads.

São avaliados tamanhos de bloco entre:

```text
1 KB
...
1 MB
```

O objetivo é verificar como a granularidade influencia o desempenho da aplicação.

Blocos muito pequenos podem aumentar o overhead de gerenciamento, enquanto blocos muito grandes podem diminuir o equilíbrio da carga entre as threads.

---

## Mutex vs. Redução Local

Os resultados das duas implementações paralelas são comparados:

```text
log_analyzer_par
        VS
log_analyzer_par_optimized
```

A comparação considera principalmente:

* Tempo de execução;
* Speedup;
* Eficiência;
* Overhead de sincronização;
* Escalabilidade.

---

# Medição de tempo

Para medir o tempo utilizando o Linux:

```bash
/usr/bin/time -p ./log_analyzer_seq access.log
```

Exemplo para a versão paralela:

```bash
/usr/bin/time -p ./log_analyzer_par access.log 4
```

E para a versão otimizada:

```bash
/usr/bin/time -p ./log_analyzer_par_optimized access.log 4
```

Para obter resultados mais confiáveis, os experimentos devem ser executados múltiplas vezes e os resultados podem ser comparados utilizando a média dos tempos.

---

# Profiling

O projeto também utiliza ferramentas de análise de desempenho, como:

```bash
perf
```

```bash
gprof
```

```bash
valgrind
```

Exemplo:

```bash
valgrind --leak-check=full ./log_analyzer_seq access.log
```

---

# Automação

Os scripts Python podem ser utilizados para automatizar os experimentos e auxiliar na geração dos resultados.

Para verificar as opções disponíveis:

```bash
python3 run_experiments.py --help
```

Para geração dos gráficos:

```bash
python3 plot_graphs.py
```

Os comandos exatos podem variar de acordo com os argumentos implementados nos scripts.

---

# Resultados esperados

A partir dos experimentos, serão analisados:

* Tempo de execução;
* Speedup;
* Eficiência;
* Strong scaling;
* Weak scaling;
* Influência da granularidade;
* Overhead de sincronização;
* Diferença entre mutex e redução local;
* Resultados do profiling.

Os resultados devem ser apresentados por meio de tabelas e gráficos no relatório final.

---

# Ambiente

O projeto foi desenvolvido para execução em ambiente:

```text
Linux / WSL
GCC
Pthreads
Python 3
```

Verificar versão do GCC:

```bash
gcc --version
```

Verificar versão do Python:

```bash
python3 --version
```

---

# Como executar o projeto completo

Um fluxo básico de execução é:

```bash
git clone https://github.com/joseroling/analisador-logs-pthreads.git

cd analisador-logs-pthreads

make clean

make
```

Depois, executar a versão sequencial:

```bash
./log_analyzer_seq access.log
```

Executar a versão paralela:

```bash
./log_analyzer_par access.log 1
./log_analyzer_par access.log 2
./log_analyzer_par access.log 4
./log_analyzer_par access.log 8
./log_analyzer_par access.log 16
```

Executar a versão otimizada:

```bash
./log_analyzer_par_optimized access.log 1
./log_analyzer_par_optimized access.log 2
./log_analyzer_par_optimized access.log 4
./log_analyzer_par_optimized access.log 8
./log_analyzer_par_optimized access.log 16
```

---

# Autores

Projeto desenvolvido para a disciplina de **Computação Paralela**.

**Universidade Presbiteriana Mackenzie**
**Faculdade de Computação e Informática (FCI)**

---

# Uso de Inteligência Artificial

Ferramentas de Inteligência Artificial foram utilizadas como apoio durante o desenvolvimento do projeto, principalmente para auxiliar na compreensão de conceitos, revisão de código, documentação e resolução de problemas.

Os integrantes do grupo são responsáveis pela compreensão, validação e apresentação do código desenvolvido.
