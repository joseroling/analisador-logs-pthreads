#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include "log_common.h"

#define HASH_BUCKETS 65536

// -------------------------------------------------------------
// TABELA HASH PARA CONTAGEM O(1) DE IPs E URLs
// -------------------------------------------------------------
typedef struct HashNode {
    char key[MAX_URL_LEN];
    long long count;
    struct HashNode *next;
} HashNode;

typedef struct {
    HashNode *buckets[HASH_BUCKETS];
    size_t unique_count;
} HashTable;

static inline unsigned int hash_func(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c; // djb2: hash * 33 + c
    }
    return hash % HASH_BUCKETS;
}

void hash_insert(HashTable *ht, const char *key) {
    if (!key || key[0] == '\0') return;

    unsigned int idx = hash_func(key);
    for (HashNode *curr = ht->buckets[idx]; curr; curr = curr->next) {
        if (strcmp(curr->key, key) == 0) {
            curr->count++;
            return;
        }
    }

    HashNode *newNode = (HashNode *)malloc(sizeof(HashNode));
    if (!newNode) return;
    strncpy(newNode->key, key, MAX_URL_LEN - 1);
    newNode->key[MAX_URL_LEN - 1] = '\0';
    newNode->count = 1;
    newNode->next = ht->buckets[idx];
    ht->buckets[idx] = newNode;
    ht->unique_count++;
}

void hash_free(HashTable *ht) {
    for (int i = 0; i < HASH_BUCKETS; i++) {
        HashNode *curr = ht->buckets[i];
        while (curr) {
            HashNode *tmp = curr;
            curr = curr->next;
            free(tmp);
        }
    }
}

// -------------------------------------------------------------
// PARSING ROBUSTO, SIMPLES E THREAD-SAFE
// -------------------------------------------------------------
ParsedLogEntry parse_log_line(char *line) {
    ParsedLogEntry entry;
    memset(&entry, 0, sizeof(ParsedLogEntry));
    entry.valid = false;

    if (!line || strlen(line) < 15) return entry;

    // 1. IP (tudo até o primeiro espaço)
    char *space = strchr(line, ' ');
    if (!space) return entry;
    size_t ip_len = space - line;
    if (ip_len >= MAX_IP_LEN) ip_len = MAX_IP_LEN - 1;
    strncpy(entry.ip, line, ip_len);
    entry.ip[ip_len] = '\0';

    // 2. Hora (dois dígitos após os dois-pontos dentro de [...])
    char *time_start = strchr(space, '[');
    if (!time_start) return entry;
    char *colon = strchr(time_start, ':');
    if (!colon || !isdigit(colon[1]) || !isdigit(colon[2])) return entry;
    entry.hour = (colon[1] - '0') * 10 + (colon[2] - '0');
    if (entry.hour < 0 || entry.hour > 23) entry.hour = 0;

    // 3. Método HTTP e URL (dentro das primeiras aspas)
    char *req_start = strchr(time_start, '"');
    if (!req_start) return entry;
    req_start++; // Pula aspas

    char method_buf[16] = {0};
    char url_buf[MAX_URL_LEN] = {0};
    if (sscanf(req_start, "%15s %255s", method_buf, url_buf) < 2) return entry;

    if (strcmp(method_buf, "GET") == 0) entry.method = METHOD_GET;
    else if (strcmp(method_buf, "POST") == 0) entry.method = METHOD_POST;
    else if (strcmp(method_buf, "PUT") == 0) entry.method = METHOD_PUT;
    else if (strcmp(method_buf, "DELETE") == 0) entry.method = METHOD_DELETE;
    else entry.method = METHOD_OUTROS;

    strncpy(entry.url, url_buf, MAX_URL_LEN - 1);
    entry.url[MAX_URL_LEN - 1] = '\0';

    // 4. Status e Bytes (após fechar a aspa da requisição)
    char *req_end = strchr(req_start, '"');
    if (!req_end) return entry;

    int status = 0;
    char bytes_buf[32] = {0};
    if (sscanf(req_end + 1, "%d %31s", &status, bytes_buf) < 2) return entry;

    entry.status_code = status;
    entry.bytes = (strcmp(bytes_buf, "-") == 0) ? 0 : atoll(bytes_buf);

    // 5. User-Agent (busca direta e simples no restante da linha)
    char *rest_of_line = req_end + 1;
    if (strstr(rest_of_line, "Edge")) entry.user_agent = UA_EDGE;
    else if (strstr(rest_of_line, "Chrome")) entry.user_agent = UA_CHROME;
    else if (strstr(rest_of_line, "Firefox")) entry.user_agent = UA_FIREFOX;
    else if (strstr(rest_of_line, "Safari")) entry.user_agent = UA_SAFARI;
    else entry.user_agent = UA_OUTROS;

    entry.valid = true;
    return entry;
}

// -------------------------------------------------------------
// EXTRAÇÃO DOS TOP 10 COM QSORT
// -------------------------------------------------------------
typedef struct {
    char key[MAX_URL_LEN];
    long long count;
} SortItem;

int compare_items(const void *a, const void *b) {
    long long diff = ((SortItem *)b)->count - ((SortItem *)a)->count;
    return (diff > 0) - (diff < 0);
}

void extract_top_10(HashTable *ht, SortItem *out_top) {
    memset(out_top, 0, TOP_K * sizeof(SortItem));
    if (ht->unique_count == 0) return;

    SortItem *items = (SortItem *)malloc(ht->unique_count * sizeof(SortItem));
    if (!items) return;

    size_t idx = 0;
    for (int i = 0; i < HASH_BUCKETS; i++) {
        HashNode *curr = ht->buckets[i];
        while (curr) {
            snprintf(items[idx].key, MAX_URL_LEN, "%s", curr->key);
            items[idx].count = curr->count;
            idx++;
            curr = curr->next;
        }
    }

    qsort(items, ht->unique_count, sizeof(SortItem), compare_items);

    for (int i = 0; i < TOP_K && i < (int)ht->unique_count; i++) {
        out_top[i] = items[i];
    }

    free(items);
}

// -------------------------------------------------------------
// FORMATAÇÃO DO RELATÓRIO CONFORME APÊNDICE 11
// -------------------------------------------------------------
void print_report(const char *filename, int threads, double elapsed, const LogStats *stats) {
    printf("============================================================\n");
    printf("ANALISADOR DE LOGS - RELATÓRIO COMPLETO\n");
    printf("============================================================\n");
    printf("ARQUIVO: %s\n", filename);
    printf("THREADS: %d\n", threads);
    printf("TEMPO DE EXECUÇÃO: %.2f segundos\n", elapsed);
    printf("------------------------------------------------------------\n");
    printf("ESTATÍSTICAS BÁSICAS\n");
    printf("------------------------------------------------------------\n");

    double pct_200 = stats->total_requests > 0 ? (stats->total_200 * 100.0 / stats->total_requests) : 0.0;
    double pct_404 = stats->total_requests > 0 ? (stats->total_404 * 100.0 / stats->total_requests) : 0.0;

    printf("Total de Requisições:        %lld\n", stats->total_requests);
    printf("Requisições 200 (OK):        %lld (%.2f%%)\n", stats->total_200, pct_200);
    printf("Requisições 404 (Not Found): %lld (%.2f%%)\n", stats->total_404, pct_404);
    printf("Total de Bytes:              %lld\n", stats->total_bytes);
    printf("Média de Bytes/Req:          %.0f bytes\n", stats->avg_bytes);
    printf("Taxa de Erro Geral:          %.2f%%\n", stats->error_rate);

    printf("------------------------------------------------------------\n");
    printf("DISTRIBUIÇÃO POR HORA (0-23h)\n");
    printf("------------------------------------------------------------\n");
    for (int i = 0; i < 24; i++) {
        double pct = stats->total_requests > 0 ? (stats->requests_per_hour[i] * 100.0 / stats->total_requests) : 0.0;
        printf("%02dh: %lld (%.2f%%)\n", i, stats->requests_per_hour[i], pct);
    }

    printf("------------------------------------------------------------\n");
    printf("TOP 10 URLs MAIS ACESSADAS\n");
    printf("------------------------------------------------------------\n");
    for (int i = 0; i < TOP_K; i++) {
        if (stats->top_urls[i].count > 0) {
            printf("%2d. %-30s %lld acessos\n", i + 1, stats->top_urls[i].url, stats->top_urls[i].count);
        }
    }

    printf("------------------------------------------------------------\n");
    printf("TOP 10 IPS MAIS ATIVOS\n");
    printf("------------------------------------------------------------\n");
    for (int i = 0; i < TOP_K; i++) {
        if (stats->top_ips[i].count > 0) {
            printf("%2d. %-20s %lld requisições\n", i + 1, stats->top_ips[i].ip, stats->top_ips[i].count);
        }
    }

    printf("------------------------------------------------------------\n");
    printf("DISTRIBUIÇÃO DE CÓDIGOS DE STATUS\n");
    printf("------------------------------------------------------------\n");
    const char *status_labels[NUM_STATUS_CODES] = {
        "200 OK", "301 Moved", "302 Found", "400 Bad Req", "403 Forbidden",
        "404 Not Found", "500 Internal", "502 Bad Gateway", "503 Unavail", "Outros"
    };
    for (int i = 0; i < NUM_STATUS_CODES; i++) {
        double pct = stats->total_requests > 0 ? (stats->status_dist[i] * 100.0 / stats->total_requests) : 0.0;
        printf("%-16s %lld (%.2f%%)\n", status_labels[i], stats->status_dist[i], pct);
    }

    printf("------------------------------------------------------------\n");
    printf("ANÁLISE DE MÉTODOS HTTP\n");
    printf("------------------------------------------------------------\n");
    const char *method_labels[NUM_METHODS] = { "GET", "POST", "PUT", "DELETE", "OUTROS" };
    for (int i = 0; i < NUM_METHODS; i++) {
        double pct = stats->total_requests > 0 ? (stats->method_dist[i] * 100.0 / stats->total_requests) : 0.0;
        printf("%-10s %lld (%.2f%%)\n", method_labels[i], stats->method_dist[i], pct);
    }

    printf("------------------------------------------------------------\n");
    printf("ANÁLISE DE USER-AGENT (Top 5)\n");
    printf("------------------------------------------------------------\n");
    const char *ua_labels[NUM_UA] = { "1. Chrome", "2. Firefox", "3. Safari", "4. Edge", "5. Outros" };
    for (int i = 0; i < NUM_UA; i++) {
        double pct = stats->total_requests > 0 ? (stats->ua_dist[i] * 100.0 / stats->total_requests) : 0.0;
        printf("%-12s %lld (%.2f%%)\n", ua_labels[i], stats->ua_dist[i], pct);
    }

    printf("============================================================\n");
    printf("FIM DO RELATÓRIO\n");
    printf("============================================================\n");
}

// -------------------------------------------------------------
// PROGRAMA PRINCIPAL
// -------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_de_log>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *filename = argv[1];
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Erro ao abrir arquivo");
        return EXIT_FAILURE;
    }

    LogStats stats;
    memset(&stats, 0, sizeof(LogStats));

    HashTable url_table;
    HashTable ip_table;
    memset(&url_table, 0, sizeof(HashTable));
    memset(&ip_table, 0, sizeof(HashTable));

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    char line[MAX_LINE_LEN];
    long long total_errors = 0;

    while (fgets(line, sizeof(line), fp)) {
        ParsedLogEntry entry = parse_log_line(line);
        if (!entry.valid) continue;

        stats.total_requests++;

        // Contagem universal de erros (RFC 9110: status >= 400)
        if (entry.status_code >= 400) {
            total_errors++;
        }

        // Status e Bytes
        if (entry.status_code == 200) {
            stats.total_200++;
            stats.total_bytes += entry.bytes; // Apenas requisições 200
            stats.status_dist[STATUS_200]++;
        } else {
            switch (entry.status_code) {
                case 301: stats.status_dist[STATUS_301]++; break;
                case 302: stats.status_dist[STATUS_302]++; break;
                case 400: stats.status_dist[STATUS_400]++; break;
                case 403: stats.status_dist[STATUS_403]++; break;
                case 404: 
                    stats.total_404++; 
                    stats.status_dist[STATUS_404]++; 
                    break;
                case 500: stats.status_dist[STATUS_500]++; break;
                case 502: stats.status_dist[STATUS_502]++; break;
                case 503: stats.status_dist[STATUS_503]++; break;
                default:  stats.status_dist[STATUS_OUTROS]++; break;
            }
        }

        // Distribuições
        stats.requests_per_hour[entry.hour]++;
        stats.method_dist[entry.method]++;
        stats.ua_dist[entry.user_agent]++;

        // Tabelas Hash
        hash_insert(&url_table, entry.url);
        hash_insert(&ip_table, entry.ip);
    }

    fclose(fp);

    // Cálculos consolidados (Matemática alinhada ao Apêndice 11)
    if (stats.total_requests > 0) {
        stats.avg_bytes = (double)stats.total_bytes / (double)stats.total_requests;
        stats.error_rate = ((double)total_errors / (double)stats.total_requests) * 100.0;
    }

    // Extração dos Top 10
    SortItem top_urls[TOP_K];
    SortItem top_ips[TOP_K];
    extract_top_10(&url_table, top_urls);
    extract_top_10(&ip_table, top_ips);

    // Nota: origem (SortItem.key) sempre cabe nos destinos (verificado com ASan/UBSan).
    // GCC -O3 às vezes superestima o tamanho de origem ao inlinear extract_top_10
    // para url_table e ip_table (falso positivo conhecido de -Wformat-truncation).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    for (int i = 0; i < TOP_K; i++) {
        snprintf(stats.top_urls[i].url, MAX_URL_LEN, "%s", top_urls[i].key);
        stats.top_urls[i].count = top_urls[i].count;

        snprintf(stats.top_ips[i].ip, MAX_IP_LEN, "%s", top_ips[i].key);
        stats.top_ips[i].count = top_ips[i].count;
    }
#pragma GCC diagnostic pop

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    print_report(filename, 1, elapsed, &stats);

    // Liberação limpa de memória
    hash_free(&url_table);
    hash_free(&ip_table);

    return EXIT_SUCCESS;
}
