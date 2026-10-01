#include "parallel_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static unsigned hf(const char *s)
{
    unsigned h = 5381;
    int c;

    while ((c = (unsigned char) *s++))
        h = ((h << 5) + h) + (unsigned) c;

    return h % HASH_BUCKETS;
}


void hash_init(HashTable *h)
{
    memset(h, 0, sizeof(*h));
}


void hash_insert(HashTable *h, const char *k)
{
    if (!k || !*k)
        return;

    unsigned b = hf(k);

    for (HashNode *n = h->buckets[b]; n; n = n->next) {
        if (!strcmp(n->key, k)) {
            n->count++;
            return;
        }
    }

    HashNode *n = calloc(1, sizeof(*n));

    if (!n) {
        perror("calloc");
        exit(1);
    }

    snprintf(n->key, sizeof(n->key), "%s", k);

    n->count = 1;
    n->next = h->buckets[b];

    h->buckets[b] = n;
    h->unique_count++;
}


void hash_merge(HashTable *d, const HashTable *s)
{
    for (int i = 0; i < HASH_BUCKETS; i++) {

        for (HashNode *n = s->buckets[i]; n; n = n->next) {

            unsigned b = hf(n->key);
            HashNode *x;

            for (x = d->buckets[b]; x; x = x->next) {
                if (!strcmp(x->key, n->key))
                    break;
            }

            if (x) {

                x->count += n->count;

            } else {

                x = calloc(1, sizeof(*x));

                if (!x) {
                    perror("calloc");
                    exit(1);
                }

                snprintf(x->key, sizeof(x->key), "%s", n->key);

                x->count = n->count;
                x->next = d->buckets[b];

                d->buckets[b] = x;
                d->unique_count++;
            }
        }
    }
}


void hash_free(HashTable *h)
{
    for (int i = 0; i < HASH_BUCKETS; i++) {

        HashNode *n = h->buckets[i];

        while (n) {

            HashNode *x = n->next;

            free(n);

            n = x;
        }

        h->buckets[i] = NULL;
    }

    h->unique_count = 0;
}


typedef struct {
    char key[MAX_URL_LEN];
    long long count;
} Item;


static int cmp(const void *a, const void *b)
{
    const Item *x = a;
    const Item *y = b;

    if (x->count < y->count)
        return 1;

    if (x->count > y->count)
        return -1;

    return strcmp(x->key, y->key);
}


static void top(const HashTable *h, Item out[TOP_K])
{
    size_t k = 0;

    for (int i = 0; i < HASH_BUCKETS; i++) {
        for (HashNode *n = h->buckets[i]; n; n = n->next)
            k++;
    }

    if (!k)
        return;

    Item *a = malloc(k * sizeof(*a));

    if (!a) {
        perror("malloc");
        exit(1);
    }

    size_t p = 0;

    for (int i = 0; i < HASH_BUCKETS; i++) {

        for (HashNode *n = h->buckets[i]; n; n = n->next) {

            snprintf(
                a[p].key,
                sizeof(a[p].key),
                "%s",
                n->key
            );

            a[p++].count = n->count;
        }
    }

    qsort(a, k, sizeof(*a), cmp);

    size_t m = k < TOP_K ? k : TOP_K;

    for (size_t i = 0; i < m; i++)
        out[i] = a[i];

    free(a);
}


void top_from_hash(const HashTable *h, URLCount o[TOP_K])
{
    memset(o, 0, sizeof(URLCount) * TOP_K);

    Item a[TOP_K] = {0};

    top(h, a);

    for (int i = 0; i < TOP_K; i++) {

        size_t nu = strlen(a[i].key);

        if (nu >= sizeof(o[i].url))
            nu = sizeof(o[i].url) - 1;

        memcpy(o[i].url, a[i].key, nu);

        o[i].url[nu] = 0;
        o[i].count = a[i].count;
    }
}


void ip_top_from_hash(const HashTable *h, IPCount o[TOP_K])
{
    memset(o, 0, sizeof(IPCount) * TOP_K);

    Item a[TOP_K] = {0};

    top(h, a);

    for (int i = 0; i < TOP_K; i++) {

        size_t ni = strlen(a[i].key);

        if (ni >= sizeof(o[i].ip))
            ni = sizeof(o[i].ip) - 1;

        memcpy(o[i].ip, a[i].key, ni);

        o[i].ip[ni] = 0;
        o[i].count = a[i].count;
    }
}


void stats_init(LogStats *s)
{
    memset(s, 0, sizeof(*s));
}


void stats_add_entry(LogStats *s, const ParsedLogEntry *e)
{
    s->total_requests++;

    if (e->status_code == 404)
        s->total_404++;

    if (e->status_code == 200) {
        s->total_200++;
        s->total_bytes += e->bytes;
    }

    s->requests_per_hour[e->hour]++;
    s->method_dist[e->method]++;
    s->ua_dist[e->user_agent]++;

    switch (e->status_code) {

        case 200:
            s->status_dist[STATUS_200]++;
            break;

        case 301:
            s->status_dist[STATUS_301]++;
            break;

        case 302:
            s->status_dist[STATUS_302]++;
            break;

        case 400:
            s->status_dist[STATUS_400]++;
            break;

        case 403:
            s->status_dist[STATUS_403]++;
            break;

        case 404:
            s->status_dist[STATUS_404]++;
            break;

        case 500:
            s->status_dist[STATUS_500]++;
            break;

        case 502:
            s->status_dist[STATUS_502]++;
            break;

        case 503:
            s->status_dist[STATUS_503]++;
            break;

        default:
            s->status_dist[STATUS_OUTROS]++;
    }
}


void stats_finalize(LogStats *s)
{
    s->avg_bytes =
        s->total_requests
        ? (double) s->total_bytes / s->total_requests
        : 0;

    long long er = 0;

    for (int i = STATUS_400; i < NUM_STATUS_CODES; i++)
        er += s->status_dist[i];

    s->error_rate =
        s->total_requests
        ? 100.0 * er / s->total_requests
        : 0;
}


void print_report(
    const char *f,
    int threads,
    double t,
    const LogStats *s
)
{
    const char *sl[] = {
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

    const char *ml[] = {
        "GET",
        "POST",
        "PUT",
        "DELETE",
        "OUTROS"
    };

    const char *ul[] = {
        "Chrome",
        "Firefox",
        "Safari",
        "Edge",
        "Outros"
    };


    printf(
        "============================================================\n"
        "ANALISADOR DE LOGS - RELATÓRIO COMPLETO\n"
        "============================================================\n"
        "ARQUIVO: %s\n"
        "THREADS: %d\n"
        "TEMPO DE EXECUÇÃO: %.6f segundos\n"
        "------------------------------------------------------------\n"
        "ESTATÍSTICAS BÁSICAS\n"
        "------------------------------------------------------------\n",
        f,
        threads,
        t
    );


    double p2 =
        s->total_requests
        ? 100.0 * s->total_200 / s->total_requests
        : 0;

    double p4 =
        s->total_requests
        ? 100.0 * s->total_404 / s->total_requests
        : 0;


    printf(
        "Total de Requisições: %lld\n"
        "Requisições 200 (OK): %lld (%.2f%%)\n"
        "Requisições 404 (Not Found): %lld (%.2f%%)\n"
        "Total de Bytes: %lld\n"
        "Média de Bytes/Req: %.0f bytes\n"
        "Taxa de Erro Geral: %.2f%%\n",
        s->total_requests,
        s->total_200,
        p2,
        s->total_404,
        p4,
        s->total_bytes,
        s->avg_bytes,
        s->error_rate
    );


    printf(
        "------------------------------------------------------------\n"
        "DISTRIBUIÇÃO POR HORA (0-23h)\n"
        "------------------------------------------------------------\n"
    );

    for (int i = 0; i < 24; i++)
        printf("%02dh: %lld\n", i, s->requests_per_hour[i]);


    printf(
        "------------------------------------------------------------\n"
        "TOP 10 URLs MAIS ACESSADAS\n"
        "------------------------------------------------------------\n"
    );

    for (int i = 0; i < TOP_K; i++) {

        if (s->top_urls[i].count) {
            printf(
                "%2d. %-30s %lld acessos\n",
                i + 1,
                s->top_urls[i].url,
                s->top_urls[i].count
            );
        }
    }


    printf(
        "------------------------------------------------------------\n"
        "TOP 10 IPS MAIS ATIVOS\n"
        "------------------------------------------------------------\n"
    );

    for (int i = 0; i < TOP_K; i++) {

        if (s->top_ips[i].count) {
            printf(
                "%2d. %-20s %lld requisições\n",
                i + 1,
                s->top_ips[i].ip,
                s->top_ips[i].count
            );
        }
    }


    printf(
        "------------------------------------------------------------\n"
        "DISTRIBUIÇÃO DE CÓDIGOS DE STATUS\n"
        "------------------------------------------------------------\n"
    );

    for (int i = 0; i < NUM_STATUS_CODES; i++)
        printf(
            "%-16s %lld\n",
            sl[i],
            s->status_dist[i]
        );


    printf(
        "------------------------------------------------------------\n"
        "ANÁLISE DE MÉTODOS HTTP\n"
        "------------------------------------------------------------\n"
    );

    for (int i = 0; i < NUM_METHODS; i++)
        printf(
            "%-10s %lld\n",
            ml[i],
            s->method_dist[i]
        );


    printf(
        "------------------------------------------------------------\n"
        "ANÁLISE DE USER-AGENT (Top 5)\n"
        "------------------------------------------------------------\n"
    );

    for (int i = 0; i < NUM_UA; i++)
        printf(
            "%-12s %lld\n",
            ul[i],
            s->ua_dist[i]
        );


    printf(
        "============================================================\n"
        "FIM DO RELATÓRIO\n"
        "============================================================\n"
    );
}
