int main() {
    const char* lines[] = {
        "127.0.0.1 - - [15/Sep/2025:15:30:00 -0300] \"GET /index.html HTTP/1.1\" 200 1500 \"Mozilla/5.0 Chrome/120.0\"",
        "192.168.1.10 - - [15/Sep/2025:15:30:01 -0300] \"GET /products/iphone HTTP/1.1\" 200 4500 \"Mozilla/5.0 Firefox/121.0\"",
        "10.0.0.5 - - [15/Sep/2025:15:30:02 -0300] \"GET /nonexistent.jpg HTTP/1.1\" 404 250 \"Mozilla/5.0 Safari/17.0\"",
        "172.16.0.1 - - [15/Sep/2025:15:30:03 -0300] \"POST /api/login HTTP/1.1\" 200 120 \"Mozilla/5.0 Chrome/120.0\""
    };
    
    LogEntry entry;
    
    for (int i = 0; i < 4; i++) {
        if (parse_log_line(lines[i], &entry)) {
            printf("Linha %d:\n", i+1);
            printf("  IP: %s\n", entry.ip);
            printf("  Hora: %02d:00\n", extract_hour(entry.timestamp));
            printf("  Método: %s\n", entry.method);
            printf("  URL: %s\n", entry.url);
            printf("  Status: %d (%s)\n", entry.status, 
                   entry.status == 200 ? "OK" : 
                   entry.status == 404 ? "Not Found" : "Other");
            printf("  Bytes: %lld\n", entry.bytes);
            printf("  User-Agent: %s\n", entry.user_agent);
            printf("  Categoria Status: %d\n", status_category(entry.status));
            printf("  Categoria Método: %d\n", method_category(entry.method));
            printf("\n");
        }
    }
    
    return 0;
}