#include "parallel_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * Função responsável por calcular o hash
 * de uma string.
 *
 * O resultado é utilizado para descobrir
 * em qual posição da tabela hash a chave
 * será armazenada.
 */
static unsigned hf(const char *s)
{
    unsigned h = 5381;

    int c;


    /*
     * Percorre todos os caracteres da string
     * calculando o valor do hash.
     */
    while ((c = (unsigned char) *s++)) {

        h = ((h << 5) + h)
          + (unsigned) c;
    }


    /*
     * Garante que o resultado esteja
     * dentro dos limites da tabela.
     */
    return h % HASH_BUCKETS;
}


/*
 * Inicializa uma tabela hash.
 *
 * O memset coloca todos os campos
 * inicialmente como zero.
 */
void hash_init(HashTable *h)
{
    memset(
        h,
        0,
        sizeof(*h)
    );
}


/*
 * Insere uma chave na tabela hash.
 *
 * Se a chave já existir, apenas aumenta
 * o contador de ocorrências.
 */
void hash_insert(
    HashTable *h,
    const char *k
)
{
    /* Ignora chaves vazias ou nulas */
    if (!k || !*k)
        return;


    /* Calcula o bucket da chave */
    unsigned b = hf(k);


    /*
     * Procura a chave dentro do bucket.
     */
    for (
        HashNode *n = h->buckets[b];
        n;
        n = n->next
    ) {

        /*
         * Se encontrou a chave,
         * aumenta sua quantidade.
         */
        if (!strcmp(n->key, k)) {

            n->count++;

            return;
        }
    }


    /*
     * A chave ainda não existe.
     * Portanto, cria um novo nó.
     */
    HashNode *n =
        calloc(1, sizeof(*n));


    if (!n) {

        perror("calloc");

        exit(1);
    }


    /* Copia a chave para o novo nó */
    snprintf(
        n->key,
        sizeof(n->key),
        "%s",
        k
    );


    /* Primeira ocorrência da chave */
    n->count = 1;


    /*
     * Insere o novo nó no início
     * da lista do bucket.
     */
    n->next = h->buckets[b];

    h->buckets[b] = n;


    /* Atualiza o número de chaves únicas */
    h->unique_count++;
}


/*
 * Junta os dados de uma tabela hash
 * com outra tabela hash.
 *
 * É utilizada para juntar os resultados
 * produzidos pelas diferentes threads.
 */
void hash_merge(
    HashTable *d,
    const HashTable *s
)
{
    /*
     * Percorre todos os buckets
     * da tabela de origem.
     */
    for (int i = 0; i < HASH_BUCKETS; i++) {

        /*
         * Percorre todos os elementos
         * existentes no bucket.
         */
        for (
            HashNode *n = s->buckets[i];
            n;
            n = n->next
        ) {

            /* Descobre o bucket da chave */
            unsigned b = hf(n->key);

            HashNode *x;


            /*
             * Procura a chave na tabela
             * de destino.
             */
            for (
                x = d->buckets[b];
                x;
                x = x->next
            ) {

                if (!strcmp(x->key, n->key))
                    break;
            }


            /*
             * Se a chave já existe,
             * soma as ocorrências.
             */
            if (x) {

                x->count += n->count;


            } else {

                /*
                 * Caso contrário, cria um
                 * novo elemento na tabela.
                 */
                x = calloc(
                    1,
                    sizeof(*x)
                );


                if (!x) {

                    perror("calloc");

                    exit(1);
                }


                /* Copia a chave */
                snprintf(
                    x->key,
                    sizeof(x->key),
                    "%s",
                    n->key
                );


                /* Copia a quantidade */
                x->count = n->count;


                /* Insere na lista */
                x->next = d->buckets[b];

                d->buckets[b] = x;


                /* Atualiza número de chaves únicas */
                d->unique_count++;
            }
        }
    }
}


/*
 * Libera toda a memória utilizada
 * por uma tabela hash.
 */
void hash_free(HashTable *h)
{
    /*
     * Percorre todos os buckets.
     */
    for (int i = 0; i < HASH_BUCKETS; i++) {

        HashNode *n =
            h->buckets[i];


        /*
         * Percorre a lista encadeada
         * de cada bucket.
         */
        while (n) {

            HashNode *x =
                n->next;


            /* Libera o nó atual */
            free(n);


            /* Vai para o próximo nó */
            n = x;
        }


        /* Limpa o bucket */
        h->buckets[i] = NULL;
    }


    /* Zera o número de elementos únicos */
    h->unique_count = 0;
}


/*
 * Estrutura auxiliar utilizada
 * para ordenar os elementos.
 */
typedef struct {

    char key[MAX_URL_LEN];

    long long count;

} Item;


/*
 * Função de comparação utilizada
 * pelo qsort().
 *
 * Ordena do maior número de ocorrências
 * para o menor.
 */
static int cmp(
    const void *a,
    const void *b
)
{
    const Item *x = a;
    const Item *y = b;


    if (x->count < y->count)
        return 1;


    if (x->count > y->count)
        return -1;


    /*
     * Caso as quantidades sejam iguais,
     * ordena alfabeticamente.
     */
    return strcmp(
        x->key,
        y->key
    );
}


/*
 * Obtém os elementos mais acessados
 * de uma tabela hash.
 */
static void top(
    const HashTable *h,
    Item out[TOP_K]
)
{
    size_t k = 0;


    /*
     * Conta quantos elementos existem
     * na tabela hash.
     */
    for (int i = 0; i < HASH_BUCKETS; i++) {

        for (
            HashNode *n = h->buckets[i];
            n;
            n = n->next
        ) {
            k++;
        }
    }


    /* Se não houver elementos, termina */
    if (!k)
        return;


    /*
     * Aloca um vetor temporário
     * para armazenar os elementos.
     */
    Item *a =
        malloc(k * sizeof(*a));


    if (!a) {

        perror("malloc");

        exit(1);
    }


    size_t p = 0;


    /*
     * Copia os elementos da tabela
     * para o vetor.
     */
    for (int i = 0; i < HASH_BUCKETS; i++) {

        for (
            HashNode *n = h->buckets[i];
            n;
            n = n->next
        ) {

            snprintf(
                a[p].key,
                sizeof(a[p].key),
                "%s",
                n->key
            );


            a[p++].count =
                n->count;
        }
    }


    /*
     * Ordena os elementos pelo número
     * de ocorrências.
     */
    qsort(
        a,
        k,
        sizeof(*a),
        cmp
    );


    /*
     * Define quantos elementos serão
     * copiados para o resultado final.
     */
    size_t m =
        k < TOP_K
        ? k
        : TOP_K;


    for (size_t i = 0; i < m; i++)
        out[i] = a[i];


    /* Libera o vetor temporário */
    free(a);
}


/*
 * Obtém as URLs mais acessadas
 * a partir da tabela hash.
 */
void top_from_hash(
    const HashTable *h,
    URLCount o[TOP_K]
)
{
    /*
     * Inicializa o resultado com zeros.
     */
    memset(
        o,
        0,
        sizeof(URLCount) * TOP_K
    );


    Item a[TOP_K] = {0};


    /* Obtém os elementos mais acessados */
    top(h, a);


    /*
     * Copia os resultados para
     * a estrutura URLCount.
     */
    for (int i = 0; i < TOP_K; i++) {

        size_t nu =
            strlen(a[i].key);


        /*
         * Evita ultrapassar o tamanho
         * máximo permitido para a URL.
         */
        if (nu >= sizeof(o[i].url))
            nu = sizeof(o[i].url) - 1;


        memcpy(
            o[i].url,
            a[i].key,
            nu
        );


        /* Garante o final da string */
        o[i].url[nu] = 0;


        /* Copia o número de acessos */
        o[i].count =
            a[i].count;
    }
}


/*
 * Obtém os IPs mais ativos
 * a partir da tabela hash.
 */
void ip_top_from_hash(
    const HashTable *h,
    IPCount o[TOP_K]
)
{
    /*
     * Inicializa o resultado.
     */
    memset(
        o,
        0,
        sizeof(IPCount) * TOP_K
    );


    Item a[TOP_K] = {0};


    /* Obtém os elementos mais acessados */
    top(h, a);


    /*
     * Copia os resultados para
     * a estrutura IPCount.
     */
    for (int i = 0; i < TOP_K; i++) {

        size_t ni =
            strlen(a[i].key);


        /*
         * Evita ultrapassar o tamanho
         * máximo permitido para o IP.
         */
        if (ni >= sizeof(o[i].ip))
            ni = sizeof(o[i].ip) - 1;


        memcpy(
            o[i].ip,
            a[i].key,
            ni
        );


        /* Finaliza a string */
        o[i].ip[ni] = 0;


        /* Copia o número de requisições */
        o[i].count =
            a[i].count;
    }
}


/*
 * Inicializa uma estrutura de estatísticas.
 */
void stats_init(LogStats *s)
{
    memset(
        s,
        0,
        sizeof(*s)
    );
}


/*
 * Adiciona uma entrada de log
 * às estatísticas.
 */
void stats_add_entry(
    LogStats *s,
    const ParsedLogEntry *e
)
{
    /* Conta mais uma requisição */
    s->total_requests++;


    /* Conta respostas 404 */
    if (e->status_code == 404)
        s->total_404++;


    /*
     * Para respostas 200,
     * contabiliza requisições e bytes.
     */
    if (e->status_code == 200) {

        s->total_200++;

        s->total_bytes +=
            e->bytes;
    }


    /* Distribuição das requisições por hora */
    s->requests_per_hour[e->hour]++;


    /* Distribuição dos métodos HTTP */
    s->method_dist[e->method]++;


    /* Distribuição dos User-Agents */
    s->ua_dist[e->user_agent]++;


    /*
     * Distribuição dos códigos
     * de status HTTP.
     */
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


/*
 * Calcula as estatísticas derivadas
 * depois que todas as entradas foram processadas.
 */
void stats_finalize(LogStats *s)
{
    /*
     * Calcula a média de bytes por requisição.
     */
    s->avg_bytes =
        s->total_requests
        ? (double) s->total_bytes
          / s->total_requests
        : 0;


    /* Total de respostas consideradas como erro */
    long long er = 0;


    /*
     * Soma todos os códigos a partir
     * de STATUS_400.
     */
    for (
        int i = STATUS_400;
        i < NUM_STATUS_CODES;
        i++
    ) {

        er +=
            s->status_dist[i];
    }


    /*
     * Calcula a porcentagem de erros.
     */
    s->error_rate =
        s->total_requests
        ? 100.0 * er
          / s->total_requests
        : 0;
}


/*
 * Imprime o relatório final
 * do analisador de logs.
 */
void print_report(
    const char *f,
    int threads,
    double t,
    const LogStats *s
)
{
    /*
     * Nomes dos códigos de status.
     */
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


    /*
     * Nomes dos métodos HTTP.
     */
    const char *ml[] = {

        "GET",
        "POST",
        "PUT",
        "DELETE",
        "OUTROS"
    };


    /*
     * Nomes dos principais User-Agents.
     */
    const char *ul[] = {

        "Chrome",
        "Firefox",
        "Safari",
        "Edge",
        "Outros"
    };


    /*
     * Cabeçalho do relatório.
     */
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


    /*
     * Calcula a porcentagem de respostas 200.
     */
    double p2 =
        s->total_requests
        ? 100.0 * s->total_200
          / s->total_requests
        : 0;


    /*
     * Calcula a porcentagem de respostas 404.
     */
    double p4 =
        s->total_requests
        ? 100.0 * s->total_404
          / s->total_requests
        : 0;


    /*
     * Imprime as estatísticas básicas.
     */
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


    /*
     * Distribuição das requisições por hora.
     */
    printf(
        "------------------------------------------------------------\n"
        "DISTRIBUIÇÃO POR HORA (0-23h)\n"
        "------------------------------------------------------------\n"
    );


    for (int i = 0; i < 24; i++) {

        printf(
            "%02dh: %lld\n",
            i,
            s->requests_per_hour[i]
        );
    }


    /*
     * Lista das URLs mais acessadas.
     */
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


    /*
     * Lista dos IPs mais ativos.
     */
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


    /*
     * Distribuição dos códigos HTTP.
     */
    printf(
        "------------------------------------------------------------\n"
        "DISTRIBUIÇÃO DE CÓDIGOS DE STATUS\n"
        "------------------------------------------------------------\n"
    );


    for (int i = 0; i < NUM_STATUS_CODES; i++) {

        printf(
            "%-16s %lld\n",
            sl[i],
            s->status_dist[i]
        );
    }


    /*
     * Distribuição dos métodos HTTP.
     */
    printf(
        "------------------------------------------------------------\n"
        "ANÁLISE DE MÉTODOS HTTP\n"
        "------------------------------------------------------------\n"
    );


    for (int i = 0; i < NUM_METHODS; i++) {

        printf(
            "%-10s %lld\n",
            ml[i],
            s->method_dist[i]
        );
    }


    /*
     * Distribuição dos User-Agents.
     */
    printf(
        "------------------------------------------------------------\n"
        "ANÁLISE DE USER-AGENT (Top 5)\n"
        "------------------------------------------------------------\n"
    );


    for (int i = 0; i < NUM_UA; i++) {

        printf(
            "%-12s %lld\n",
            ul[i],
            s->ua_dist[i]
        );
    }


    /*
     * Final do relatório.
     */
    printf(
        "============================================================\n"
        "FIM DO RELATÓRIO\n"
        "============================================================\n"
    );
}
