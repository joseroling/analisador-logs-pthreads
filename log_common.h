#ifndef LOG_COMMON_H
#define LOG_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

#define MAX_URL_LEN   256
#define MAX_IP_LEN    46
#define MAX_LINE_LEN  2048
#define TOP_K         10

// Estruturas para os Top 10 (Nível 2)
typedef struct {
    char url[MAX_URL_LEN];
    long long count;
} URLCount;

typedef struct {
    char ip[MAX_IP_LEN];
    long long count;
} IPCount;

// Índices para Distribuição de Códigos de Status (Nível 2)
typedef enum {
    STATUS_200 = 0,
    STATUS_301,
    STATUS_302,
    STATUS_400,
    STATUS_403,
    STATUS_404,
    STATUS_500,
    STATUS_502,
    STATUS_503,
    STATUS_OUTROS,
    NUM_STATUS_CODES
} StatusIndex;

// Índices para Métodos HTTP (Nível 2)
typedef enum {
    METHOD_GET = 0,
    METHOD_POST,
    METHOD_PUT,
    METHOD_DELETE,
    METHOD_OUTROS,
    NUM_METHODS
} MethodIndex;

// Índices para User-Agent (Nível 3 / Apêndice 11)
typedef enum {
    UA_CHROME = 0,
    UA_FIREFOX,
    UA_SAFARI,
    UA_EDGE,
    UA_OUTROS,
    NUM_UA
} UAIndex;

// Estrutura Principal de Resultados Estatísticos
typedef struct {
    // Nível 1 - Básicas
    long long total_requests;
    long long total_404;
    long long total_200;
    long long total_bytes;
    double avg_bytes;
    double error_rate;

    // Nível 2 - Intermediárias
    URLCount top_urls[TOP_K];
    IPCount top_ips[TOP_K];
    long long requests_per_hour[24];
    long long status_dist[NUM_STATUS_CODES];
    long long method_dist[NUM_METHODS];

    // Nível 3 - Apêndice
    long long ua_dist[NUM_UA];
} LogStats;

// Dados extraídos de uma única linha de log
typedef struct {
    char ip[MAX_IP_LEN];
    int hour;
    MethodIndex method;
    char url[MAX_URL_LEN];
    int status_code;
    long long bytes;
    UAIndex user_agent;
    bool valid;
} ParsedLogEntry;

ParsedLogEntry parse_log_line(char *line);

void print_report(
    const char *filename,
    int threads,
    double elapsed,
    LogStats *stats
);

#endif // LOG_COMMON_H
