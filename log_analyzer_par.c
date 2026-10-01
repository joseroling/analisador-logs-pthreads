#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <time.h>

#include "log_common.h"


// ============================================================
// ESTRUTURA PARA CONTAGEM DE URLs E IPS
// Sem tabela hash.
// ============================================================

typedef struct {
    char key[MAX_URL_LEN];
    long long count;
} CountItem;


// Vetores dinamicos
CountItem *url_items = NULL;
CountItem *ip_items = NULL;

size_t url_count = 0;
size_t ip_count = 0;

size_t url_capacity = 0;
size_t ip_capacity = 0;


// ============================================================
// ARGUMENTOS DAS THREADS
// ============================================================

typedef struct {
    const char *filename;
    long start;
    long end;
    int thread_id;
} ThreadArgs;


// ============================================================
// VARIAVEIS GLOBAIS
// ============================================================

LogStats global_stats;

long long total_errors = 0;

pthread_mutex_t stats_mutex;


// ============================================================
// FUNCAO PARA ADICIONAR / INCREMENTAR URL
// ============================================================

void add_url(const char *url) {

    if (!url || url[0] == '\0')
        return;


    // Procura se a URL ja existe
    for (size_t i = 0; i < url_count; i++) {

        if (strcmp(url_items[i].key, url) == 0) {

            url_items[i].count++;

            return;
        }
    }


    // Se o vetor estiver cheio, aumenta o tamanho
    if (url_count >= url_capacity) {

        size_t new_capacity =
            (url_capacity == 0)
            ? 100
            : url_capacity * 2;


        CountItem *temp =
            realloc(
                url_items,
                new_capacity * sizeof(CountItem)
            );


        if (!temp)
            return;


        url_items = temp;

        url_capacity = new_capacity;
    }


    // Adiciona nova URL
    strncpy(
        url_items[url_count].key,
        url,
        MAX_URL_LEN - 1
    );

    url_items[url_count].key[MAX_URL_LEN - 1] = '\0';

    url_items[url_count].count = 1;

    url_count++;
}


// ============================================================
// FUNCAO PARA ADICIONAR / INCREMENTAR IP
// ============================================================

void add_ip(const char *ip) {

    if (!ip || ip[0] == '\0')
        return;


    // Procura se o IP ja existe
    for (size_t i = 0; i < ip_count; i++) {

        if (strcmp(ip_items[i].key, ip) == 0) {

            ip_items[i].count++;

            return;
        }
    }


    // Aumenta o vetor quando necessario
    if (ip_count >= ip_capacity) {

        size_t new_capacity =
            (ip_capacity == 0)
            ? 100
            : ip_capacity * 2;


        CountItem *temp =
            realloc(
                ip_items,
                new_capacity * sizeof(CountItem)
            );


        if (!temp)
            return;


        ip_items = temp;

        ip_capacity = new_capacity;
    }


    // Adiciona novo IP
    strncpy(
        ip_items[ip_count].key,
        ip,
        MAX_URL_LEN - 1
    );

    ip_items[ip_count].key[MAX_URL_LEN - 1] = '\0';

    ip_items[ip_count].count = 1;

    ip_count++;
}


// ============================================================
// COMPARACAO PARA ORDENAR TOP 10
// ============================================================

int compare_items(const void *a, const void *b) {

    const CountItem *itemA =
        (const CountItem *)a;

    const CountItem *itemB =
        (const CountItem *)b;


    if (itemA->count < itemB->count)
        return 1;


    if (itemA->count > itemB->count)
        return -1;


    return strcmp(
        itemA->key,
        itemB->key
    );
}


// ============================================================
// COPIA OS TOP 10 PARA O LogStats
// ============================================================

void generate_top_10(void) {

    // --------------------------------------------------------
    // URLs
    // --------------------------------------------------------

    if (url_count > 0) {

        qsort(
            url_items,
            url_count,
            sizeof(CountItem),
            compare_items
        );
    }


    // --------------------------------------------------------
    // IPS
    // --------------------------------------------------------

    if (ip_count > 0) {

        qsort(
            ip_items,
            ip_count,
            sizeof(CountItem),
            compare_items
        );
    }


    // --------------------------------------------------------
    // Copia URLs
    // --------------------------------------------------------

    for (int i = 0; i < TOP_K; i++) {

        global_stats.top_urls[i].url[0] = '\0';

        global_stats.top_urls[i].count = 0;


        if (i < (int)url_count) {

            strncpy(
                global_stats.top_urls[i].url,
                url_items[i].key,
                MAX_URL_LEN - 1
            );

            global_stats.top_urls[i]
                .url[MAX_URL_LEN - 1] = '\0';


            global_stats.top_urls[i].count =
                url_items[i].count;
        }
    }


    // --------------------------------------------------------
    // Copia IPs
    // --------------------------------------------------------

    for (int i = 0; i < TOP_K; i++) {

        global_stats.top_ips[i].ip[0] = '\0';

        global_stats.top_ips[i].count = 0;


        if (i < (int)ip_count) {

            strncpy(
                global_stats.top_ips[i].ip,
                ip_items[i].key,
                MAX_IP_LEN - 1
            );

            global_stats.top_ips[i]
                .ip[MAX_IP_LEN - 1] = '\0';


            global_stats.top_ips[i].count =
                ip_items[i].count;
        }
    }
}


// ============================================================
// CATEGORIA DO STATUS
// ============================================================

StatusIndex status_index(int status) {

    switch (status) {

        case 200:
            return STATUS_200;

        case 301:
            return STATUS_301;

        case 302:
            return STATUS_302;

        case 400:
            return STATUS_400;

        case 403:
            return STATUS_403;

        case 404:
            return STATUS_404;

        case 500:
            return STATUS_500;

        case 502:
            return STATUS_502;

        case 503:
            return STATUS_503;

        default:
            return STATUS_OUTROS;
    }
}


// ============================================================
// ATUALIZACAO DAS ESTATISTICAS
// ============================================================

void atualizar_estatisticas(ParsedLogEntry *entry) {

    if (!entry || !entry->valid)
        return;


    StatusIndex status =
        status_index(entry->status_code);


    pthread_mutex_lock(&stats_mutex);


    // Total de requisicoes
    global_stats.total_requests++;


    // Requisicoes 404
    if (entry->status_code == 404) {

        global_stats.total_404++;
    }


    // Requisicoes 200
    if (entry->status_code == 200) {

        global_stats.total_200++;

        global_stats.total_bytes +=
            entry->bytes;
    }


    // Erros
    if (entry->status_code >= 400) {

        total_errors++;
    }


    // Requisicoes por hora
    if (entry->hour >= 0 &&
        entry->hour < 24) {

        global_stats
            .requests_per_hour[entry->hour]++;
    }


    // Distribuicao de status
    global_stats.status_dist[status]++;


    // Distribuicao de metodos
    global_stats.method_dist[
        entry->method
    ]++;


    // Distribuicao de User-Agent
    global_stats.ua_dist[
        entry->user_agent
    ]++;


    // Contagem de URLs e IPs
    add_url(entry->url);

    add_ip(entry->ip);


    pthread_mutex_unlock(&stats_mutex);
}


// ============================================================
// FUNCAO EXECUTADA POR CADA THREAD
// ============================================================

void *process_block(void *arg) {

    ThreadArgs *args =
        (ThreadArgs *)arg;


    FILE *file =
        fopen(args->filename, "r");


    if (!file) {

        perror("Erro ao abrir arquivo");

        pthread_exit(NULL);
    }


    /*
       Cada thread recebe uma faixa do arquivo.

       Se o inicio estiver no meio de uma linha,
       a thread avanca ate encontrar o final
       dessa linha.
    */

    if (args->start > 0) {

        fseek(
            file,
            args->start - 1,
            SEEK_SET
        );


        int anterior =
            fgetc(file);


        if (anterior != '\n') {

            char descarte[MAX_LINE_LEN];


            if (!fgets(
                    descarte,
                    sizeof(descarte),
                    file
                )) {

                fclose(file);

                pthread_exit(NULL);
            }
        }

    } else {

        fseek(
            file,
            0,
            SEEK_SET
        );
    }


    char line[MAX_LINE_LEN];


    while (1) {

        long position =
            ftell(file);


        if (position < 0 ||
            position >= args->end) {

            break;
        }


        if (!fgets(
                line,
                sizeof(line),
                file
            )) {

            break;
        }


        // O parser nao precisa do mutex
        ParsedLogEntry entry =
            parse_log_line(line);


        if (!entry.valid)
            continue;


        atualizar_estatisticas(
            &entry
        );
    }


    fclose(file);


    pthread_exit(NULL);
}


// ============================================================
// MAIN
// ============================================================

int main(int argc, char *argv[]) {

    if (argc != 3) {

        printf(
            "Uso: %s <arquivo_log> <numero_threads>\n",
            argv[0]
        );

        return EXIT_FAILURE;
    }


    const char *filename =
        argv[1];


    int num_threads =
        atoi(argv[2]);


    if (num_threads <= 0) {

        printf(
            "Numero de threads invalido.\n"
        );

        return EXIT_FAILURE;
    }


    // --------------------------------------------------------
    // Descobre o tamanho do arquivo
    // --------------------------------------------------------

    FILE *file =
        fopen(filename, "r");


    if (!file) {

        perror(
            "Erro ao abrir arquivo"
        );

        return EXIT_FAILURE;
    }


    fseek(
        file,
        0,
        SEEK_END
    );


    long file_size =
        ftell(file);


    fclose(file);


    if (file_size < 0) {

        printf(
            "Erro ao descobrir tamanho do arquivo.\n"
        );

        return EXIT_FAILURE;
    }


    // --------------------------------------------------------
    // Tamanho de cada bloco
    // --------------------------------------------------------

    long block_size =
        file_size / num_threads;


    // --------------------------------------------------------
    // Inicializa estatisticas
    // --------------------------------------------------------

    memset(
        &global_stats,
        0,
        sizeof(global_stats)
    );


    pthread_mutex_init(
        &stats_mutex,
        NULL
    );


    // --------------------------------------------------------
    // Cria threads
    // --------------------------------------------------------

    pthread_t *threads =
        malloc(
            num_threads *
            sizeof(pthread_t)
        );


    ThreadArgs *args =
        malloc(
            num_threads *
            sizeof(ThreadArgs)
        );


    if (!threads || !args) {

        printf(
            "Erro de memoria.\n"
        );


        free(threads);
        free(args);


        pthread_mutex_destroy(
            &stats_mutex
        );


        return EXIT_FAILURE;
    }


    // --------------------------------------------------------
    // Inicio da medicao
    // --------------------------------------------------------

    struct timespec start;
    struct timespec end;


    clock_gettime(
        CLOCK_MONOTONIC,
        &start
    );


    int threads_criadas = 0;


    // --------------------------------------------------------
    // Criacao das threads
    // --------------------------------------------------------

    for (int i = 0;
         i < num_threads;
         i++) {


        args[i].filename =
            filename;


        args[i].thread_id =
            i;


        args[i].start =
            i * block_size;


        if (i == num_threads - 1) {

            args[i].end =
                file_size;

        } else {

            args[i].end =
                (i + 1) * block_size;
        }


        int result =
            pthread_create(
                &threads[i],
                NULL,
                process_block,
                &args[i]
            );


        if (result != 0) {

            fprintf(
                stderr,
                "Erro ao criar thread %d\n",
                i
            );

            break;
        }


        threads_criadas++;
    }


    // --------------------------------------------------------
    // Espera todas as threads
    // --------------------------------------------------------

    for (int i = 0;
         i < threads_criadas;
         i++) {

        pthread_join(
            threads[i],
            NULL
        );
    }


    // --------------------------------------------------------
    // Calcula estatisticas finais
    // --------------------------------------------------------

    if (global_stats.total_requests > 0) {

        global_stats.avg_bytes =
            (double)global_stats.total_bytes /
            (double)global_stats.total_requests;


        global_stats.error_rate =
            ((double)total_errors /
             (double)global_stats.total_requests)
            * 100.0;
    }


    // --------------------------------------------------------
    // Gera Top 10
    // --------------------------------------------------------

    generate_top_10();


    // --------------------------------------------------------
    // Final da medicao
    // --------------------------------------------------------

    clock_gettime(
        CLOCK_MONOTONIC,
        &end
    );


    double elapsed =
        (end.tv_sec - start.tv_sec) +
        (end.tv_nsec - start.tv_nsec) /
        1e9;


    // --------------------------------------------------------
    // Relatorio
    //
    // Esta funcao fica em parallel_util.c
    // --------------------------------------------------------

    print_report(
        filename,
        num_threads,
        elapsed,
        &global_stats
    );


    // --------------------------------------------------------
    // Liberacao da memoria
    // --------------------------------------------------------

    free(url_items);
    free(ip_items);

    free(threads);
    free(args);


    pthread_mutex_destroy(
        &stats_mutex
    );


    return EXIT_SUCCESS;
}
