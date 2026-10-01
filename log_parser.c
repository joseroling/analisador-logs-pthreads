#include "log_common.h"

/*
 * Faz o parsing de uma linha no formato:
 *
 * 172.16.31.40 - - [15/Sep/2025:00:21:26 -0300-0300]
 * "GET /js/app.js HTTP/1.1" 200 4184 "Mozilla/5.0 ..."
 */

ParsedLogEntry parse_log_line(char *line) {
    ParsedLogEntry entry;

    memset(&entry, 0, sizeof(ParsedLogEntry));

    entry.valid = false;
    entry.method = METHOD_OUTROS;
    entry.user_agent = UA_OUTROS;

    /* -------------------------------------------------
       1. IP
       ------------------------------------------------- */
    char ip[MAX_IP_LEN];

    if (sscanf(line, "%45s", ip) != 1) {
        return entry;
    }

    snprintf(entry.ip, MAX_IP_LEN, "%s", ip);

    /* -------------------------------------------------
       2. Timestamp
       ------------------------------------------------- */
    char timestamp[64];

    const char *start_time = strchr(line, '[');

    if (!start_time) {
        return entry;
    }

    start_time++;

    const char *end_time = strchr(start_time, ']');

    if (!end_time) {
        return entry;
    }

    size_t timestamp_len = (size_t)(end_time - start_time);

    if (timestamp_len >= sizeof(timestamp)) {
        return entry;
    }

    memcpy(timestamp, start_time, timestamp_len);
    timestamp[timestamp_len] = '\0';

    /* Extrai a hora.
       Exemplo:
       15/Sep/2025:00:21:26 -0300-0300
                       ^^
                       hora
    */
    int hour;

    if (sscanf(timestamp, "%*[^:]:%d:", &hour) != 1) {
        return entry;
    }

    if (hour < 0 || hour > 23) {
        return entry;
    }

    entry.hour = hour;

    /* -------------------------------------------------
       3. Requisição HTTP
       ------------------------------------------------- */
    const char *request_start = strchr(end_time, '"');

    if (!request_start) {
        return entry;
    }

    request_start++;

    const char *request_end = strchr(request_start, '"');

    if (!request_end) {
        return entry;
    }

    char request[512];

    size_t request_len = (size_t)(request_end - request_start);

    if (request_len >= sizeof(request)) {
        return entry;
    }

    memcpy(request, request_start, request_len);
    request[request_len] = '\0';

    /* Método, URL e versão HTTP */
    char method[32];
    char url[MAX_URL_LEN];
    char version[32];

    if (sscanf(
            request,
            "%31s %255s %31s",
            method,
            url,
            version
        ) != 3) {
        return entry;
    }

    snprintf(entry.url, MAX_URL_LEN, "%s", url);

    /* Classificação do método */
    if (strcmp(method, "GET") == 0) {
        entry.method = METHOD_GET;
    }
    else if (strcmp(method, "POST") == 0) {
        entry.method = METHOD_POST;
    }
    else if (strcmp(method, "PUT") == 0) {
        entry.method = METHOD_PUT;
    }
    else if (strcmp(method, "DELETE") == 0) {
        entry.method = METHOD_DELETE;
    }
    else {
        entry.method = METHOD_OUTROS;
    }

    /* -------------------------------------------------
       4. Código HTTP
       ------------------------------------------------- */
    const char *after_request = request_end + 1;

    int status;

    if (sscanf(after_request, "%d", &status) != 1) {
        return entry;
    }

    entry.status_code = status;

    /* -------------------------------------------------
       5. Bytes
       ------------------------------------------------- */
    const char *status_end = strchr(after_request, ' ');

    if (!status_end) {
        return entry;
    }

    status_end++;

    long long bytes;

    if (*status_end == '-') {
        bytes = 0;
    }
    else if (sscanf(status_end, "%lld", &bytes) == 1) {
        /* valor lido normalmente */
    }
    else {
        return entry;
    }

    entry.bytes = bytes;

    /* -------------------------------------------------
       6. User-Agent
       ------------------------------------------------- */
    const char *user_agent_start = strchr(status_end, '"');

    if (user_agent_start) {

        user_agent_start++;

        const char *user_agent_end = strchr(
            user_agent_start,
            '"'
        );

        if (user_agent_end) {

            char user_agent[512];

            size_t ua_len =
                (size_t)(user_agent_end - user_agent_start);

            if (ua_len >= sizeof(user_agent)) {
                ua_len = sizeof(user_agent) - 1;
            }

            memcpy(
                user_agent,
                user_agent_start,
                ua_len
            );

            user_agent[ua_len] = '\0';

            /*
             * Edge deve ser testado antes de Chrome/Safari,
             * porque User-Agents do Edge normalmente possuem
             * "Chrome" e "Safari" também.
             *
             * Exemplo:
             * Chrome/... Safari/... Edg/...
             */
            if (strstr(user_agent, "Edg") ||
                strstr(user_agent, "Edge")) {

                entry.user_agent = UA_EDGE;
            }
            else if (strstr(user_agent, "Chrome")) {

                entry.user_agent = UA_CHROME;
            }
            else if (strstr(user_agent, "Firefox")) {

                entry.user_agent = UA_FIREFOX;
            }
            else if (strstr(user_agent, "Safari")) {

                entry.user_agent = UA_SAFARI;
            }
            else {

                entry.user_agent = UA_OUTROS;
            }
        }
    }

    /* Linha processada com sucesso */
    entry.valid = true;

    return entry;
}
