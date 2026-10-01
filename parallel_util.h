#ifndef PARALLEL_UTIL_H
#define PARALLEL_UTIL_H
#include <stddef.h>
#include "log_common.h"
#define HASH_BUCKETS 65536
typedef struct HashNode { char key[MAX_URL_LEN]; long long count; struct HashNode*next; } HashNode;
typedef struct { HashNode*buckets[HASH_BUCKETS]; size_t unique_count; } HashTable;
void hash_init(HashTable*);void hash_insert(HashTable*,const char*);void hash_merge(HashTable*,const HashTable*);void hash_free(HashTable*);void top_from_hash(const HashTable*,URLCount[TOP_K]);void ip_top_from_hash(const HashTable*,IPCount[TOP_K]);void stats_init(LogStats*);void stats_add_entry(LogStats*,const ParsedLogEntry*);void stats_finalize(LogStats*);void print_report(const char*,int,double,const LogStats*);
#endif
