#ifndef CDSL_HASHMAP_H
#define CDSL_HASHMAP_H

#include <stddef.h>

typedef struct cdsl_hashmap_entry {
    char* key;
    void* value;
    struct cdsl_hashmap_entry* next;
} cdsl_hashmap_entry_t;

typedef struct cdsl_hashmap {
    cdsl_hashmap_entry_t** buckets;
    int bucket_count;
    int size;
} cdsl_hashmap_t;

typedef void (*cdsl_hashmap_free_fn)(void* value);

cdsl_hashmap_t* cdsl_hashmap_create(int bucket_count);
void cdsl_hashmap_free(cdsl_hashmap_t* map, cdsl_hashmap_free_fn free_fn);
int cdsl_hashmap_put(cdsl_hashmap_t* map, const char* key, void* value);
void* cdsl_hashmap_get(cdsl_hashmap_t* map, const char* key);
int cdsl_hashmap_remove(cdsl_hashmap_t* map, const char* key, cdsl_hashmap_free_fn free_fn);

#endif
