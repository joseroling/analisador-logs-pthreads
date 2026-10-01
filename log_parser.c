#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char ip[16];
    char timestamp[30];
    char method[10];
    char url[256];
    char http_version[10];
    int status;
    long long bytes;
    char user_agent[256];
} LogEntry;

int parse_log_line(const char* line, LogEntry* entry) {
    // Inicializa
    memset(entry, 0, sizeof(LogEntry));
    
    // Extrai IP (primeiro campo)
    char ip[16];
    if (sscanf(line, "%15s", ip) != 1) return 0;
    strcpy(entry->ip, ip);
    
    // Pula os dois "-"
    const char* ptr = strstr(line, "- -");
    if (!ptr) return 0;
    ptr += 3; // Pula "- - "
    
    // Extrai timestamp
    char timestamp[30];
    if (sscanf(ptr, "[%29[^]]]", timestamp) != 1) return 0;
    strcpy(entry->timestamp, timestamp);
    
    // Pula o timestamp
    ptr = strstr(ptr, "] ");
    if (!ptr) return 0;
    ptr += 2;
    
    // Extrai a requisição (MÉTODO URL VERSION)
    char request[512];
    if (sscanf(ptr, "\"%[^\"]\"", request) != 1) return 0;
    ptr = strstr(ptr, "\"");
    if (!ptr) return 0;
    ptr = strstr(ptr + 1, "\"");
    if (!ptr) return 0;
    ptr += 2; // Pula a aspa
    
    // Extrai method, url, version da requisição
    char method[10], url[256], version[20];
    sscanf(request, "%s %s %s", method, url, version);
    strcpy(entry->method, method);
    strcpy(entry->url, url);
    strcpy(entry->http_version, version);
    
    // Extrai status code
    int status;
    if (sscanf(ptr, "%d", &status) != 1) return 0;
    entry->status = status;
    
    // Pula o status
    ptr = strstr(ptr, " ");
    if (!ptr) return 0;
    ptr++;
    
    // Extrai bytes (pode ser "-")
    long long bytes;
    if (strncmp(ptr, "-", 1) == 0) {
        bytes = 0;
        ptr++;
    } else {
        if (sscanf(ptr, "%lld", &bytes) != 1) return 0;
        ptr = strstr(ptr, " ");
        if (ptr) ptr++;
    }
    entry->bytes = bytes;
    
    // Extrai User-Agent (se existir)
    if (ptr && *ptr == '\"') {
        if (sscanf(ptr, "\"%[^\"]\"", entry->user_agent) == 1) {
            // Extrai o navegador principal
            char* chrome = strstr(entry->user_agent, "Chrome");
            char* firefox = strstr(entry->user_agent, "Firefox");
            char* safari = strstr(entry->user_agent, "Safari");
            char* edge = strstr(entry->user_agent, "Edge");
            char* bot = strstr(entry->user_agent, "Googlebot") || 
                        strstr(entry->user_agent, "Bingbot");
            
            // Classifica o navegador
            if (chrome) strcpy(entry->user_agent, "Chrome");
            else if (firefox) strcpy(entry->user_agent, "Firefox");
            else if (safari) strcpy(entry->user_agent, "Safari");
            else if (edge) strcpy(entry->user_agent, "Edge");
            else if (bot) strcpy(entry->user_agent, "Bot");
            else strcpy(entry->user_agent, "Other");
        }
    }
    
    return 1; // Sucesso
}

// Função para extrair hora do timestamp
int extract_hour(const char* timestamp) {
    int hour;
    sscanf(timestamp, "%*[^:]:%d:", &hour);
    return hour;
}

// Função para classificar o código de status
int status_category(int status) {
    if (status >= 200 && status < 300) return 0; // 2xx
    if (status >= 300 && status < 400) return 1; // 3xx
    if (status >= 400 && status < 500) return 2; // 4xx
    if (status >= 500 && status < 600) return 3; // 5xx
    return 4; // Outros
}

// Função para classificar o método HTTP
int method_category(const char* method) {
    if (strcmp(method, "GET") == 0) return 0;
    if (strcmp(method, "POST") == 0) return 1;
    if (strcmp(method, "PUT") == 0) return 2;
    if (strcmp(method, "DELETE") == 0) return 3;
    return 4; // Outros
}
