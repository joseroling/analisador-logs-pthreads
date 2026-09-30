#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <time.h>
#include "log_common.h"
//arquivoi que guarda definicoes compartilhadas entre o seq e par

#define HASH_BUCKETS 65536


// TABELA HASH PARA CONTAGEM DE IPs E URLs
// Mesma ideia usada na versao sequencial


typedef struct HashNode {
    char key[MAX_URL_LEN];
    long long count;
    struct HashNode *next;
} HashNode;

typedef struct {
    HashNode *buckets[HASH_BUCKETS];
    size_t unique_count;
} HashTable;

typedef struct {
    char key[MAX_URL_LEN];
    long long count;
} SortItem;

// Argumentos enviados para cada thread
typedef struct {
    const char *filename;
    long start;
    long end;
    int thread_id;
} ThreadArgs;



// VARIAVEIS GLOBAIS


LogStats global_stats;
long long total_errors = 0;

HashTable url_table;
HashTable ip_table;

pthread_mutex_t stats_mutex;



// FUNCOES DA TABELA HASH

static inline unsigned int hash_func(const char *str) {

    unsigned int hash = 5381;
    int c;

    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % HASH_BUCKETS;
}


void hash_insert(HashTable *ht, const char *key) {

    if (!key || key[0] == '\0')
        return;

    unsigned int idx = hash_func(key);

    for (HashNode *curr = ht->buckets[idx]; curr; curr = curr->next) {

        if (strcmp(curr->key, key) == 0) {
            curr->count++;
            return;
        }
    }

    HashNode *novo = (HashNode *)malloc(sizeof(HashNode));

    if (!novo)
        return;

    strncpy(novo->key, key, MAX_URL_LEN - 1);
    novo->key[MAX_URL_LEN - 1] = '\0';

    novo->count = 1;
    novo->next = ht->buckets[idx];

    ht->buckets[idx] = novo;
    ht->unique_count++;
}


void hash_free(HashTable *ht) {

    for (int i = 0; i < HASH_BUCKETS; i++) {

        HashNode *curr = ht->buckets[i];

        while (curr) {

            HashNode *temp = curr;
            curr = curr->next;
            free(temp);
        }
    }
}


// -------------------------------------------------------------
// PARSER
// Usa ParsedLogEntry e os enums definidos em log_common.h
// -------------------------------------------------------------
//------estrtura--------
//----linha do arquivo---
//--parse_log_line(line)--
//----ParsedLogEntry----
//--atualizar_estatisticas(&entry)--
ParsedLogEntry parse_log_line(char *line) {

    ParsedLogEntry entry;

    memset(&entry, 0, sizeof(ParsedLogEntry));
    entry.valid = false;

    if (!line || strlen(line) < 15)
        return entry;


    // IP
    char *space = strchr(line, ' ');

    if (!space)
        return entry;

    size_t ip_len = space - line;

    if (ip_len >= MAX_IP_LEN)
        ip_len = MAX_IP_LEN - 1;

    strncpy(entry.ip, line, ip_len);
    entry.ip[ip_len] = '\0';


    // Hora
    char *time_start = strchr(space, '[');

    if (!time_start)
        return entry;

    char *colon = strchr(time_start, ':');

    if (!colon || !isdigit((unsigned char)colon[1]) ||
        !isdigit((unsigned char)colon[2]))
        return entry;

    entry.hour =
        (colon[1] - '0') * 10 +
        (colon[2] - '0');

    if (entry.hour < 0 || entry.hour > 23)
        entry.hour = 0;


    // Metodo HTTP e URL
    char *req_start = strchr(time_start, '"');

    if (!req_start)
        return entry;

    req_start++;

    char method_buf[16] = {0};
    char url_buf[MAX_URL_LEN] = {0};

    if (sscanf(
            req_start,
            "%15s %255s",
            method_buf,
            url_buf
        ) < 2)
        return entry;

    //categorias definidas no log como 0 get 1 post etc
    if (strcmp(method_buf, "GET") == 0)
        entry.method = METHOD_GET;

    else if (strcmp(method_buf, "POST") == 0)
        entry.method = METHOD_POST;

    else if (strcmp(method_buf, "PUT") == 0)
        entry.method = METHOD_PUT;

    else if (strcmp(method_buf, "DELETE") == 0)
        entry.method = METHOD_DELETE;

    else
        entry.method = METHOD_OUTROS;


    strncpy(entry.url, url_buf, MAX_URL_LEN - 1);
    entry.url[MAX_URL_LEN - 1] = '\0';


    // Status e bytes
    char *req_end = strchr(req_start, '"');

    if (!req_end)
        return entry;

    int status = 0;
    char bytes_buf[32] = {0};

    if (sscanf(
            req_end + 1,
            "%d %31s",
            &status,
            bytes_buf
        ) < 2)
        return entry;

    entry.status_code = status;

    entry.bytes =
        (strcmp(bytes_buf, "-") == 0)
        ? 0
        : atoll(bytes_buf);


    // User-Agent
    char *rest_of_line = req_end + 1;

    if (strstr(rest_of_line, "Edge"))
        entry.user_agent = UA_EDGE;

    else if (strstr(rest_of_line, "Chrome"))
        entry.user_agent = UA_CHROME;

    else if (strstr(rest_of_line, "Firefox"))
        entry.user_agent = UA_FIREFOX;

    else if (strstr(rest_of_line, "Safari"))
        entry.user_agent = UA_SAFARI;

    else
        entry.user_agent = UA_OUTROS;


    entry.valid = true;

    return entry;
}



// CATEGORIA DO STATUS


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



// ATUALIZACAO DAS ESTATISTICAS
//
// Essa funcao foi separada do process_block para deixar cada
// funcao com uma responsabilidade mais clara.
// Toda alteracao compartilhada fica protegida pelo mutex global.


void atualizar_estatisticas(ParsedLogEntry *entry) {

    StatusIndex status = status_index(entry->status_code);

    pthread_mutex_lock(&stats_mutex);

    global_stats.total_requests++;

    if (entry->status_code == 404) {
        global_stats.total_404++;
    }

    if (entry->status_code == 200) {
        global_stats.total_200++;

        // Apenas requisicoes 200 entram no total de bytes
        global_stats.total_bytes += entry->bytes;
    }

    if (entry->status_code >= 400) {
        total_errors++;
    }

    if (entry->hour >= 0 && entry->hour < 24) {
        global_stats.requests_per_hour[entry->hour]++;
    }

    global_stats.status_dist[status]++;
    global_stats.method_dist[entry->method]++;
    global_stats.ua_dist[entry->user_agent]++;

    // As tabelas hash tambem sao compartilhadas.
    // Por isso sao atualizadas enquanto o mutex esta travado.
    hash_insert(&url_table, entry->url);
    hash_insert(&ip_table, entry->ip);

    // Libera para outra thread atualizar as estatisticas
    pthread_mutex_unlock(&stats_mutex);
}



// FUNCAO EXECUTADA POR CADA THREAD


void *process_block(void *arg) {

    ThreadArgs *args = (ThreadArgs *)arg;

    FILE *file = fopen(args->filename, "r");

    if (!file) {
        perror("Erro ao abrir arquivo");
        pthread_exit(NULL);
    }


    /*
       Cada thread recebe uma faixa de bytes.

       Se o inicio cair no meio de uma linha, a thread avanca
       ate o final dessa linha. Se cair exatamente no inicio
       de uma linha, ela nao descarta a linha.
    */
    if (args->start > 0) {

        fseek(file, args->start - 1, SEEK_SET);

        int anterior = fgetc(file);

        if (anterior != '\n') {

            char descarte[MAX_LINE_LEN];

            if (!fgets(descarte, sizeof(descarte), file)) {
                fclose(file);
                pthread_exit(NULL);
            }
        }

    } else {

        fseek(file, 0, SEEK_SET);
    }


    char line[MAX_LINE_LEN];

    while (1) {

        long position = ftell(file);

        if (position < 0 || position >= args->end)
            break;

        if (!fgets(line, sizeof(line), file))
            break;


        // O parsing acontece fora do mutex.
        ParsedLogEntry entry = parse_log_line(line);

        if (!entry.valid)
            continue;


        atualizar_estatisticas(&entry);
    }


    fclose(file);

    pthread_exit(NULL);
}



// TOP 10


int compare_items(const void *a, const void *b) {

    const SortItem *itemA = (const SortItem *)a;
    const SortItem *itemB = (const SortItem *)b;

    if (itemA->count < itemB->count)
        return 1;

    if (itemA->count > itemB->count)
        return -1;

    return 0;
}


void extract_top_10(HashTable *ht, SortItem *out_top) {

    memset(out_top, 0, TOP_K * sizeof(SortItem));

    if (ht->unique_count == 0)
        return;


    SortItem *items =
        (SortItem *)malloc(
            ht->unique_count *
            sizeof(SortItem)
        );

    if (!items)
        return;


    size_t idx = 0;

    for (int i = 0; i < HASH_BUCKETS; i++) {

        HashNode *curr = ht->buckets[i];

        while (curr) {

            snprintf(
                items[idx].key,
                MAX_URL_LEN,
                "%s",
                curr->key
            );

            items[idx].count = curr->count;

            idx++;
            curr = curr->next;
        }
    }


    qsort(
        items,
        ht->unique_count,
        sizeof(SortItem),
        compare_items
    );


    for (
        int i = 0;
        i < TOP_K && i < (int)ht->unique_count;
        i++
    ) {

        out_top[i] = items[i];
    }


    free(items);
}


// -------------------------------------------------------------
// RELATORIO
// Formato alinhado com a versao sequencial
// -------------------------------------------------------------

void print_report(
    const char *filename,
    int threads,
    double elapsed,
    LogStats *stats
) {

    printf("============================================================\n");
    printf("ANALISADOR DE LOGS - RELATORIO COMPLETO\n");
    printf("============================================================\n");

    printf("ARQUIVO: %s\n", filename);
    printf("THREADS: %d\n", threads);
    printf("TEMPO DE EXECUCAO: %.6f segundos\n", elapsed);


    printf("------------------------------------------------------------\n");
    printf("ESTATISTICAS BASICAS\n");
    printf("------------------------------------------------------------\n");


    double pct_200 =
        stats->total_requests > 0
        ? stats->total_200 * 100.0 / stats->total_requests
        : 0.0;

    double pct_404 =
        stats->total_requests > 0
        ? stats->total_404 * 100.0 / stats->total_requests
        : 0.0;


    printf(
        "Total de Requisicoes:        %lld\n",
        stats->total_requests
    );

    printf(
        "Requisicoes 200 (OK):        %lld (%.2f%%)\n",
        stats->total_200,
        pct_200
    );

    printf(
        "Requisicoes 404 (Not Found): %lld (%.2f%%)\n",
        stats->total_404,
        pct_404
    );

    printf(
        "Total de Bytes:              %lld\n",
        stats->total_bytes
    );

    printf(
        "Media de Bytes/Req:          %.0f bytes\n",
        stats->avg_bytes
    );

    printf(
        "Taxa de Erro Geral:          %.2f%%\n",
        stats->error_rate
    );


    printf("------------------------------------------------------------\n");
    printf("DISTRIBUICAO POR HORA (0-23h)\n");
    printf("------------------------------------------------------------\n");

    for (int i = 0; i < 24; i++) {

        double pct =
            stats->total_requests > 0
            ? stats->requests_per_hour[i] * 100.0 /
              stats->total_requests
            : 0.0;

        printf(
            "%02dh: %lld (%.2f%%)\n",
            i,
            stats->requests_per_hour[i],
            pct
        );
    }


    printf("------------------------------------------------------------\n");
    printf("TOP 10 URLs MAIS ACESSADAS\n");
    printf("------------------------------------------------------------\n");

    for (int i = 0; i < TOP_K; i++) {

        if (stats->top_urls[i].count > 0) {

            printf(
                "%2d. %-30s %lld acessos\n",
                i + 1,
                stats->top_urls[i].url,
                stats->top_urls[i].count
            );
        }
    }


    printf("------------------------------------------------------------\n");
    printf("TOP 10 IPS MAIS ATIVOS\n");
    printf("------------------------------------------------------------\n");

    for (int i = 0; i < TOP_K; i++) {

        if (stats->top_ips[i].count > 0) {

            printf(
                "%2d. %-20s %lld requisicoes\n",
                i + 1,
                stats->top_ips[i].ip,
                stats->top_ips[i].count
            );
        }
    }


    const char *status_labels[NUM_STATUS_CODES] = {
        "200 OK",
        "301 Moved",
        "302 Found",
        "400 Bad Req",
        "403 Forbidden",
        "404 Not Found",
        "500 Internal",
        "502 Bad Gateway",
        "503 Unavail",
        "Outros"
    };


    printf("------------------------------------------------------------\n");
    printf("DISTRIBUICAO DE CODIGOS DE STATUS\n");
    printf("------------------------------------------------------------\n");

    for (int i = 0; i < NUM_STATUS_CODES; i++) {

        double pct =
            stats->total_requests > 0
            ? stats->status_dist[i] * 100.0 /
              stats->total_requests
            : 0.0;

        printf(
            "%-16s %lld (%.2f%%)\n",
            status_labels[i],
            stats->status_dist[i],
            pct
        );
    }


    const char *method_labels[NUM_METHODS] = {
        "GET",
        "POST",
        "PUT",
        "DELETE",
        "OUTROS"
    };


    printf("------------------------------------------------------------\n");
    printf("ANALISE DE METODOS HTTP\n");
    printf("------------------------------------------------------------\n");

    for (int i = 0; i < NUM_METHODS; i++) {

        double pct =
            stats->total_requests > 0
            ? stats->method_dist[i] * 100.0 /
              stats->total_requests
            : 0.0;

        printf(
            "%-10s %lld (%.2f%%)\n",
            method_labels[i],
            stats->method_dist[i],
            pct
        );
    }


    const char *ua_labels[NUM_UA] = {
        "1. Chrome",
        "2. Firefox",
        "3. Safari",
        "4. Edge",
        "5. Outros"
    };


    printf("------------------------------------------------------------\n");
    printf("ANALISE DE USER-AGENT (Top 5)\n");
    printf("------------------------------------------------------------\n");

    for (int i = 0; i < NUM_UA; i++) {

        double pct =
            stats->total_requests > 0
            ? stats->ua_dist[i] * 100.0 /
              stats->total_requests
            : 0.0;

        printf(
            "%-12s %lld (%.2f%%)\n",
            ua_labels[i],
            stats->ua_dist[i],
            pct
        );
    }


    printf("============================================================\n");
    printf("FIM DO RELATORIO\n");
    printf("============================================================\n");
}


// -------------------------------------------------------------
// MAIN
// -------------------------------------------------------------

int main(int argc, char *argv[]) {

    if (argc != 3) {

        printf(
            "Uso: %s <arquivo_log> <numero_threads>\n",
            argv[0]
        );

        return EXIT_FAILURE;
    }


    const char *filename = argv[1];

    int num_threads = atoi(argv[2]);

    if (num_threads <= 0) {

        printf("Numero de threads invalido.\n");

        return EXIT_FAILURE;
    }


    // Descobre o tamanho do arquivo
    FILE *file = fopen(filename, "r");

    if (!file) {

        perror("Erro ao abrir arquivo");

        return EXIT_FAILURE;
    }


    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    fclose(file);


    if (file_size < 0) {

        printf("Erro ao descobrir tamanho do arquivo.\n");

        return EXIT_FAILURE;
    }


    long block_size = file_size / num_threads;


    // Inicializa estruturas globais
    memset(
        &global_stats,
        0,
        sizeof(global_stats)
    );

    memset(
        &url_table,
        0,
        sizeof(url_table)
    );

    memset(
        &ip_table,
        0,
        sizeof(ip_table)
    );


    pthread_mutex_init(
        &stats_mutex,
        NULL
    );


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

        printf("Erro de memoria.\n");

        free(threads);
        free(args);

        pthread_mutex_destroy(&stats_mutex);

        return EXIT_FAILURE;
    }


    struct timespec start;
    struct timespec end;

    clock_gettime(
        CLOCK_MONOTONIC,
        &start
    );


    int threads_criadas = 0;

    for (int i = 0; i < num_threads; i++) {

        args[i].filename = filename;
        args[i].thread_id = i;

        args[i].start =
            i * block_size;


        if (i == num_threads - 1) {

            args[i].end = file_size;

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


    for (int i = 0; i < threads_criadas; i++) {

        pthread_join(
            threads[i],
            NULL
        );
    }


    // Calcula os valores consolidados
    if (global_stats.total_requests > 0) {

        global_stats.avg_bytes =
            (double)global_stats.total_bytes /
            (double)global_stats.total_requests;

        global_stats.error_rate =
            ((double)total_errors /
             (double)global_stats.total_requests) *
            100.0;
    }


    // Extrai Top 10 usando as mesmas estruturas do log_common.h
    SortItem top_urls[TOP_K];
    SortItem top_ips[TOP_K];

    extract_top_10(
        &url_table,
        top_urls
    );

    extract_top_10(
        &ip_table,
        top_ips
    );


    for (int i = 0; i < TOP_K; i++) {

        size_t url_len = strnlen(top_urls[i].key, MAX_URL_LEN - 1);
        memcpy(global_stats.top_urls[i].url, top_urls[i].key, url_len);
        global_stats.top_urls[i].url[url_len] = '\0';

        global_stats.top_urls[i].count =
            top_urls[i].count;


        size_t ip_len = strnlen(top_ips[i].key, MAX_IP_LEN - 1);
        memcpy(global_stats.top_ips[i].ip, top_ips[i].key, ip_len);
        global_stats.top_ips[i].ip[ip_len] = '\0';

        global_stats.top_ips[i].count =
            top_ips[i].count;
    }


    clock_gettime(
        CLOCK_MONOTONIC,
        &end
    );


    double elapsed =
        (end.tv_sec - start.tv_sec) +
        (end.tv_nsec - start.tv_nsec) /
        1e9;


    print_report(
        filename,
        num_threads,
        elapsed,
        &global_stats
    );


    hash_free(&url_table);
    hash_free(&ip_table);

    pthread_mutex_destroy(
        &stats_mutex
    );

    free(threads);
    free(args);


    return EXIT_SUCCESS;
}
