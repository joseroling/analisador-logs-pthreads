#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>

#include "log_parser.h"
#include "parallel_util.h"


/*
 * Estrutura utilizada para armazenar os dados
 * que serão utilizados por cada thread.
 */
typedef struct {
    const char *file;

    off_t start;
    off_t end;

    LogStats s;

    HashTable u;
    HashTable i;

} Arg;


/*
 * Mutex utilizado para proteger o acesso
 * às estatísticas globais e às tabelas hash.
 */
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;


/*
 * Mutex utilizado pelo escalonador de blocos.
 * Ele garante que duas threads não peguem
 * o mesmo bloco do arquivo.
 */
static pthread_mutex_t scheduler = PTHREAD_MUTEX_INITIALIZER;


/* Estatísticas globais */
static LogStats gs;


/* Tabelas hash globais */
static HashTable gu;
static HashTable gi;


/*
 * Variáveis utilizadas para dividir o arquivo
 * em blocos durante o processamento.
 */
static off_t next_block = 0;
static off_t file_size = 0;
static off_t block_size = 0;


/*
 * Junta as estatísticas locais de uma thread
 * nas estatísticas globais.
 */
static void merge(Arg *a)
{
    pthread_mutex_lock(&m);

    /* Soma as estatísticas básicas */
    gs.total_requests += a->s.total_requests;
    gs.total_404 += a->s.total_404;
    gs.total_200 += a->s.total_200;
    gs.total_bytes += a->s.total_bytes;


    /* Soma as requisições por hora */
    for (int i = 0; i < 24; i++)
        gs.requests_per_hour[i] +=
            a->s.requests_per_hour[i];


    /* Soma a distribuição dos códigos HTTP */
    for (int i = 0; i < NUM_STATUS_CODES; i++)
        gs.status_dist[i] +=
            a->s.status_dist[i];


    /* Soma a distribuição dos métodos HTTP */
    for (int i = 0; i < NUM_METHODS; i++)
        gs.method_dist[i] +=
            a->s.method_dist[i];


    /* Soma a distribuição dos User-Agents */
    for (int i = 0; i < NUM_UA; i++)
        gs.ua_dist[i] +=
            a->s.ua_dist[i];


    /* Junta as tabelas hash */
    hash_merge(&gu, &a->u);
    hash_merge(&gi, &a->i);


    pthread_mutex_unlock(&m);
}


/*
 * Função executada por cada thread.
 *
 * Cada thread abre o arquivo e processa
 * uma parte dele.
 */
static void *run(void *p)
{
    Arg *a = p;


    /* Abre o arquivo de log */
    FILE *f = fopen(a->file, "r");

    if (!f)
        return NULL;


    /* Inicializa as estatísticas locais */
    stats_init(&a->s);

    /* Inicializa as tabelas hash locais */
    hash_init(&a->u);
    hash_init(&a->i);


    char line[MAX_LINE_LEN];


    /*
     * Continua processando enquanto existirem
     * blocos do arquivo para serem analisados.
     */
    for (;;) {

        off_t start;
        off_t end;


        /*
         * Se block_size for maior que zero,
         * utiliza divisão dinâmica em blocos.
         */
        if (block_size > 0) {

            pthread_mutex_lock(&scheduler);


            /*
             * Verifica se todo o arquivo já foi
             * distribuído entre as threads.
             */
            if (next_block >= file_size) {

                pthread_mutex_unlock(&scheduler);

                break;
            }


            /* Define o início do próximo bloco */
            start = next_block;

            /* Define o final do bloco */
            end = start + block_size;


            /* Não ultrapassa o final do arquivo */
            if (end > file_size)
                end = file_size;


            /* Atualiza o próximo bloco disponível */
            next_block = end;


            pthread_mutex_unlock(&scheduler);


        } else {

            /*
             * Caso não esteja utilizando blocos,
             * cada thread utiliza sua parte fixa
             * do arquivo.
             */
            start = a->start;
            end = a->end;


            /* Indica que não existe mais trabalho */
            if (start < 0)
                break;


            /*
             * Impede que a mesma thread
             * processe novamente sua região.
             */
            a->start = -1;
        }


        /*
         * Posiciona o arquivo no início
         * da região que será processada.
         */
        if (fseeko(f, start, SEEK_SET) != 0)
            break;


        /*
         * Se não estamos no início do arquivo,
         * pulamos a linha que começou antes
         * do nosso bloco.
         */
        if (start > 0) {

            if (!fgets(line, sizeof(line), f))
                break;
        }


        /* Obtém a posição atual no arquivo */
        off_t pos = ftello(f);


        /*
         * Lê as linhas enquanto estiver
         * dentro dos limites do bloco.
         */
        while (pos < end &&
               fgets(line, sizeof(line), f)) {

            /* Converte a linha para uma entrada de log */
            ParsedLogEntry e = parse_log_line(line);


            /*
             * Se a linha for válida,
             * atualiza as estatísticas.
             */
            if (e.valid) {

                stats_add_entry(&a->s, &e);


                /* Registra a URL */
                hash_insert(&a->u, e.url);


                /* Registra o endereço IP */
                hash_insert(&a->i, e.ip);
            }


            /* Atualiza a posição atual */
            pos = ftello(f);


            /* Verifica erro na leitura */
            if (pos < 0)
                break;
        }
    }


    /* Fecha o arquivo */
    fclose(f);


    /*
     * Junta os resultados dessa thread
     * com os resultados globais.
     */
    merge(a);


    return NULL;
}


/*
 * Calcula o tempo decorrido entre dois
 * pontos obtidos com CLOCK_MONOTONIC.
 */
static double tm(
    struct timespec *a,
    struct timespec *b
)
{
    return (b->tv_sec - a->tv_sec)
         + (b->tv_nsec - a->tv_nsec) / 1e9;
}


/*
 * Função principal.
 */
int main(int ac, char **av)
{
    /*
     * Verifica se foram informados:
     *
     * ./programa arquivo threads
     */
    if (ac < 3) {

        fprintf(
            stderr,
            "Uso: %s <log> <threads>\n",
            av[0]
        );

        return 1;
    }


    /* Converte o número de threads */
    int n = atoi(av[2]);


    /* Limita o número de threads */
    if (n < 1 || n > 256)
        return 1;


    /*
     * Se o usuário informou um quarto argumento,
     * ele representa o tamanho dos blocos.
     */
    if (ac > 4)
        block_size = (off_t) atoll(av[4]);


    /* Abre o arquivo para descobrir seu tamanho */
    FILE *f = fopen(av[1], "rb");


    if (!f) {

        perror(av[1]);

        return 1;
    }


    /* Vai para o final do arquivo */
    fseeko(f, 0, SEEK_END);


    /* Obtém o tamanho do arquivo */
    off_t size = ftello(f);


    /* Fecha o arquivo */
    fclose(f);


    /* Guarda o tamanho global do arquivo */
    file_size = size;


    /*
     * Aloca memória para as threads.
     */
    pthread_t *th =
        calloc(n, sizeof(*th));


    /*
     * Aloca memória para os argumentos
     * de cada thread.
     */
    Arg *a =
        calloc(n, sizeof(*a));


    if (!th || !a)
        return 1;


    /* Inicializa as estatísticas globais */
    stats_init(&gs);


    /* Inicializa as tabelas hash globais */
    hash_init(&gu);
    hash_init(&gi);


    /* Começa a distribuição dos blocos */
    next_block = 0;


    /*
     * Variáveis utilizadas para medir
     * o tempo total de execução.
     */
    struct timespec s;
    struct timespec e;


    clock_gettime(
        CLOCK_MONOTONIC,
        &s
    );


    /*
     * Cria todas as threads.
     */
    for (int i = 0; i < n; i++) {

        /*
         * Cada thread recebe uma parte
         * proporcional do arquivo.
         */
        a[i] = (Arg) {
            .file = av[1],

            .start =
                size * i / n,

            .end =
                size * (i + 1) / n
        };


        /*
         * Cria a thread e executa a função run().
         */
        if (pthread_create(
                &th[i],
                NULL,
                run,
                &a[i]
            ))
        {
            return 1;
        }
    }


    /*
     * Espera todas as threads terminarem.
     */
    for (int i = 0; i < n; i++)
        pthread_join(th[i], NULL);


    /* Marca o fim da medição de tempo */
    clock_gettime(
        CLOCK_MONOTONIC,
        &e
    );


    /*
     * Obtém as URLs mais acessadas.
     */
    top_from_hash(
        &gu,
        gs.top_urls
    );


    /*
     * Obtém os IPs mais ativos.
     */
    ip_top_from_hash(
        &gi,
        gs.top_ips
    );


    /*
     * Calcula estatísticas finais,
     * como média de bytes e taxa de erro.
     */
    stats_finalize(&gs);


    /*
     * Imprime o relatório completo.
     */
    print_report(
        av[1],
        n,
        tm(&s, &e),
        &gs
    );


    /*
     * Libera as tabelas hash locais
     * de cada thread.
     */
    for (int i = 0; i < n; i++) {

        hash_free(&a[i].u);
        hash_free(&a[i].i);
    }


    /* Libera as tabelas hash globais */
    hash_free(&gu);
    hash_free(&gi);


    /* Libera a memória das threads */
    free(th);


    /* Libera os argumentos */
    free(a);


    return 0;
}
