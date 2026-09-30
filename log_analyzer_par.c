#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define MAX_LINE 2048
#define MAX_URLS 100
#define MAX_IPS 100

// Estrutura de uma linha do log
   

typedef struct {
    char ip[16];
    char timestamp[30];
    char method[10];
    char url[256];
    char http_version[20];
    int status;
    long long bytes;
    char user_agent[256];
} LogEntry;


//Estruturas para contar URLs e IPs
  

typedef struct {
    char url[256];
    long long count;
} URLCount;

typedef struct {
    char ip[16];
    long long count;
} IPCount;


//Estatísticas globais
   

typedef struct {

    //primeiro nivel de requisicoes
    long long total_requests;
    long long total_404;
    long long total_200;
    long long total_bytes;

    //calculo de taxa de erro
    long long total_errors;

    //segundo nivel de requisicoes
    URLCount urls[MAX_URLS];
    int url_count;

    IPCount ips[MAX_IPS];
    int ip_count;

    long long requests_per_hour[24];

    /*
        Índices:
        0 = 200
        1 = 301
        2 = 302
        3 = 400
        4 = 403
        5 = 404
        6 = 500
        7 = 502
        8 = 503
        9 = outros
    */
    long long status_dist[10];

    /*
        0 = GET
        1 = POST
        2 = PUT
        3 = DELETE
        4 = outros
    */
    long long method_dist[5];

} LogStats;


//Argumentos enviados para cada thread

typedef struct {
    const char *filename;
    long start;
    long end;
    int thread_id;
} ThreadArgs;


//Variáveis globais 

LogStats global_stats;

//mutex global
pthread_mutex_t stats_mutex;

//arquivo parse
int parse_log_line(const char *line, LogEntry *entry) {

    memset(entry, 0, sizeof(LogEntry));

    char ip[16];

    if (sscanf(line, "%15s", ip) != 1)
        return 0;

    strcpy(entry->ip, ip);


    const char *ptr = strstr(line, "- -");

    if (!ptr)
        return 0;

    ptr += 3;


    //timestamp(medir a hora)
    char timestamp[30];

    if (sscanf(ptr, "[%29[^]]]", timestamp) != 1)
        return 0;

    strcpy(entry->timestamp, timestamp);


    //pula o timestamp
    ptr = strstr(ptr, "] ");

    if (!ptr)
        return 0;

    ptr += 2;


   //requisição HTTP
    char request[512];

    if (sscanf(ptr, "\"%511[^\"]\"", request) != 1)
        return 0;


    ptr = strstr(ptr, "\"");

    if (!ptr)
        return 0;

    ptr = strstr(ptr + 1, "\"");

    if (!ptr)
        return 0;

    ptr += 2;


    //método + URL + HTTP version

    char method[10];
    char url[256];
    char version[20];

    if (sscanf(request,
               "%9s %255s %19s",
               method,
               url,
               version) != 3)
        return 0;

    strcpy(entry->method, method);
    strcpy(entry->url, url);
    strcpy(entry->http_version, version);


   //status do arquivo

    int status;

    if (sscanf(ptr, "%d", &status) != 1)
        return 0;

    entry->status = status;


    //pula status 

    ptr = strchr(ptr, ' ');

    if (!ptr)
        return 0;

    ptr++;



    long long bytes = 0;

    if (*ptr == '-') {

        bytes = 0;
        ptr++;

    } else {

        if (sscanf(ptr, "%lld", &bytes) != 1)
            return 0;
    }

    entry->bytes = bytes;

    return 1;
}


//Extrair hora

int extract_hour(const char *timestamp) {

    int hour = -1;

    sscanf(timestamp, "%*[^:]:%d:", &hour);

    return hour;
}


//Categoria do método HTTP
   

int method_category(const char *method) {

    if (strcmp(method, "GET") == 0)
        return 0;

    if (strcmp(method, "POST") == 0)
        return 1;

    if (strcmp(method, "PUT") == 0)
        return 2;

    if (strcmp(method, "DELETE") == 0)
        return 3;

    return 4;
}


//Categoria de status

int status_index(int status) {

    switch (status) {

        case 200:
            return 0;

        case 301:
            return 1;

        case 302:
            return 2;

        case 400:
            return 3;

        case 403:
            return 4;

        case 404:
            return 5;

        case 500:
            return 6;

        case 502:
            return 7;

        case 503:
            return 8;

        default:
            return 9;
    }
}


//atuliza o contador de url dentro do mutex
  

void update_url(const char *url) {

    for (int i = 0; i < global_stats.url_count; i++) {

        if (strcmp(global_stats.urls[i].url, url) == 0) {

            global_stats.urls[i].count++;

            return;
        }
    }


    //verifica o registro do mutex

    if (global_stats.url_count < MAX_URLS) {

        strcpy(
            global_stats.urls[global_stats.url_count].url,
            url
        );

        global_stats.urls[global_stats.url_count].count = 1;

        global_stats.url_count++;
    }
}


//Atualiza contador de IP

void update_ip(const char *ip) {

    for (int i = 0; i < global_stats.ip_count; i++) {

        if (strcmp(global_stats.ips[i].ip, ip) == 0) {

            global_stats.ips[i].count++;

            return;
        }
    }


    if (global_stats.ip_count < MAX_IPS) {

        strcpy(
            global_stats.ips[global_stats.ip_count].ip,
            ip
        );

        global_stats.ips[global_stats.ip_count].count = 1;

        global_stats.ip_count++;
    }
}


//ordenar Top URLs/IPs
   

int compare_urls(const void *a, const void *b) {

    const URLCount *urlA = a;
    const URLCount *urlB = b;

    if (urlA->count < urlB->count)
        return 1;

    if (urlA->count > urlB->count)
        return -1;

    return 0;
}


int compare_ips(const void *a, const void *b) {

    const IPCount *ipA = a;
    const IPCount *ipB = b;

    if (ipA->count < ipB->count)
        return 1;

    if (ipA->count > ipB->count)
        return -1;

    return 0;
}


/*fiz uma alteracao junto com a ia pq antes o process block fazia duas responsabilidades:
lia/processava as linhas do arquivo e também atualizava todas as estatísticas globais*/
//agora foi criado a void onde so e responsavel por atualizar
void atualizar_estatisticas(LogEntry *entry) {

    int hora = extract_hour(entry->timestamp);
    int metodo = method_category(entry->method);
    int status = status_index(entry->status);

    //apenas uma thread por vez pode alterar as estatisticas globais
    pthread_mutex_lock(&stats_mutex);

    global_stats.total_requests++;

    if (entry->status == 404) {
        global_stats.total_404++;
    }

    if (entry->status == 200) {
        global_stats.total_200++;
        global_stats.total_bytes += entry->bytes;
    }

    if (entry->status >= 400) {
        global_stats.total_errors++;
    }

    if (hora >= 0 && hora < 24) {
        global_stats.requests_per_hour[hora]++;
    }

    global_stats.status_dist[status]++;
    global_stats.method_dist[metodo]++;

    update_url(entry->url);
    update_ip(entry->ip);

    //libera o acesso para outra thread atualizar as estatisticas
    pthread_mutex_unlock(&stats_mutex);
}


//Função executada por cada thread

void *process_block(void *arg) {

    ThreadArgs *args = (ThreadArgs *)arg;

    FILE *file = fopen(args->filename, "r");

    if (!file) {

        perror("Erro ao abrir arquivo");

        pthread_exit(NULL);
    }


    //posiciona a thread no inicio do bloco

    fseek(file, args->start, SEEK_SET);


    //descarta a linha caso a treadh comeca no meio

    if (args->start != 0) {

        char discard[MAX_LINE];

        fgets(discard, sizeof(discard), file);
    }


    char line[MAX_LINE];

    LogEntry entry;


    while (1) {

        //guarda a posicao

        long position = ftell(file);


        //encera se chega no fim

        if (position >= args->end)
            break;


        if (!fgets(line, sizeof(line), file))
            break;


        //faz o parsin fora do mutex(nao bloqueia outras threas enqanto interpreta a linha)
         

        if (!parse_log_line(line, &entry))
            continue;


        atualizar_estatisticas(&entry);
    }


    fclose(file);

    pthread_exit(NULL);
}


//Impressão do relatório
   

void print_report(
    const char *filename,
    int num_threads,
    double elapsed
) {

    double avg_bytes = 0.0;

    double error_rate = 0.0;


    if (global_stats.total_requests > 0) {

        avg_bytes =
            (double)global_stats.total_bytes /
            global_stats.total_requests;

        error_rate =
            ((double)global_stats.total_errors /
             global_stats.total_requests) * 100.0;
    }


    printf("\n");
    printf("============================================\n");
    printf("ANALISADOR DE LOGS - RELATORIO COMPLETO\n");
    printf("============================================\n");

    printf("ARQUIVO: %s\n", filename);

    printf("THREADS: %d\n", num_threads);

    printf("TEMPO DE EXECUCAO: %.6f segundos\n", elapsed);


    printf("\n---------------------------------------------\n");
    printf("ESTATISTICAS BASICAS\n");
    printf("\n---------------------------------------------\n");


    printf(
        "Total de Requisicoes: %lld\n",
        global_stats.total_requests
    );


    printf(
        "Requisicoes 200 (OK): %lld\n",
        global_stats.total_200
    );


    printf(
        "Requisicoes 404 (Not Found): %lld\n",
        global_stats.total_404
    );


    printf(
        "Total de Bytes (status 200): %lld\n",
        global_stats.total_bytes
    );


    printf(
        "Media de Bytes/Req: %.2f\n",
        avg_bytes
    );


    printf(
        "Taxa de Erro Geral: %.2f%%\n",
        error_rate
    );


    // TOP URLs
    

    qsort(
        global_stats.urls,
        global_stats.url_count,
        sizeof(URLCount),
        compare_urls
    );


    printf("\n--------------------------------------------\n");
    printf("TOP 10 URLs MAIS ACESSADAS\n");
    printf("\n--------------------------------------------\n");


    int url_limit =
        global_stats.url_count < 10
        ? global_stats.url_count
        : 10;


    for (int i = 0; i < url_limit; i++) {

        printf(
            "%2d. %-35s %lld acessos\n",
            i + 1,
            global_stats.urls[i].url,
            global_stats.urls[i].count
        );
    }


    //TOP IPS
       

    qsort(
        global_stats.ips,
        global_stats.ip_count,
        sizeof(IPCount),
        compare_ips
    );


    printf("\n-----------------------------------------------\n");
    printf("TOP 10 IPS MAIS ATIVOS\n");
    printf("\n-----------------------------------------------\n");


    int ip_limit =
        global_stats.ip_count < 10
        ? global_stats.ip_count
        : 10;


    for (int i = 0; i < ip_limit; i++) {

        printf(
            "%2d. %-16s %lld requisicoes\n",
            i + 1,
            global_stats.ips[i].ip,
            global_stats.ips[i].count
        );
    }


    //Distribuição por hora

    printf("\n-------------------------------------------\n");
    printf("DISTRIBUICAO POR HORA\n");
    printf("\n-------------------------------------------\n");


    for (int i = 0; i < 24; i++) {

        printf(
            "%02d:00 -> %lld requisicoes\n",
            i,
            global_stats.requests_per_hour[i]
        );
    }


    //Status HTTP 

    const char *status_names[10] = {

        "200 OK",
        "301 Moved",
        "302 Found",
        "400 Bad Request",
        "403 Forbidden",
        "404 Not Found",
        "500 Internal",
        "502 Bad Gateway",
        "503 Unavailable",
        "Outros"
    };


    printf("\n-------------------------------------------\n");
    printf("DISTRIBUICAO DE CODIGOS DE STATUS\n");
    printf("\n-------------------------------------------\n");


    for (int i = 0; i < 10; i++) {

        double pct = 0.0;

        if (global_stats.total_requests > 0)

            pct =
                ((double)global_stats.status_dist[i] /
                 global_stats.total_requests) * 100.0;


        printf(
            "%-20s %lld (%.2f%%)\n",
            status_names[i],
            global_stats.status_dist[i],
            pct
        );
    }


    //Metodo http
    

    const char *method_names[5] = {

        "GET",
        "POST",
        "PUT",
        "DELETE",
        "OUTROS"
    };


    printf("\n-----------------------------------------\n");
    printf("ANALISE DE METODOS HTTP\n");
    printf("\n-----------------------------------------\n");


    for (int i = 0; i < 5; i++) {

        double pct = 0.0;

        if (global_stats.total_requests > 0)

            pct =
                ((double)global_stats.method_dist[i] /
                 global_stats.total_requests) * 100.0;


        printf(
            "%-10s %lld (%.2f%%)\n",
            method_names[i],
            global_stats.method_dist[i],
            pct
        );
    }


    printf("============================================================\n");
}



int main(int argc, char *argv[]) {


    if (argc != 3) {

        printf(
            "Uso: %s <arquivo_log> <numero_threads>\n",
            argv[0]
        );

        return 1;
    }


    const char *filename = argv[1];

    int num_threads = atoi(argv[2]);


    if (num_threads <= 0) {

        printf("Numero de threads invalido.\n");

        return 1;
    }


    //Descobre tamanho do arquivo

    FILE *file = fopen(filename, "r");

    if (!file) {

        perror("Erro ao abrir arquivo");

        return 1;
    }


    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    fclose(file);


    //byte por thread

    long block_size = file_size / num_threads;


    /* inicializa estatísticas */

    memset(
        &global_stats,
        0,
        sizeof(global_stats)
    );


    /* inicializa mutex */

    pthread_mutex_init(
        &stats_mutex,
        NULL
    );


    /* arrays das threads */

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

        return 1;
    }


    //Início da medição


    struct timespec start;
    struct timespec end;

    clock_gettime(
        CLOCK_MONOTONIC,
        &start
    );


    //cria a thread

    for (int i = 0; i < num_threads; i++) {

        args[i].filename = filename;

        args[i].thread_id = i;

        args[i].start =
            i * block_size;


        //thread vai ate o final do arquivo

        if (i == num_threads - 1)

            args[i].end = file_size;

        else

            args[i].end =
                (i + 1) * block_size;


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

            return 1;
        }
    }


    //espera o ultimo termina


    for (int i = 0; i < num_threads; i++) {

        pthread_join(
            threads[i],
            NULL
        );
    }


    //medicao final

    clock_gettime(
        CLOCK_MONOTONIC,
        &end
    );


    double elapsed =
        (end.tv_sec - start.tv_sec) +
        (end.tv_nsec - start.tv_nsec) /
        1e9;


    /* relatório */

    print_report(
        filename,
        num_threads,
        elapsed
    );


    /* limpeza */

    pthread_mutex_destroy(
        &stats_mutex
    );


    free(threads);

    free(args);


    return 0;
}