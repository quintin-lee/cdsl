#ifndef CDSL_ARENA_H
#define CDSL_ARENA_H

#include <stddef.h>

typedef struct cdsl_arena_block {
    char* data;
    size_t used;
    size_t capacity;
    struct cdsl_arena_block* next;
} cdsl_arena_block_t;

typedef struct cdsl_arena {
    cdsl_arena_block_t* blocks;
    size_t block_size;
} cdsl_arena_t;

cdsl_arena_t* cdsl_arena_create(size_t block_size);
void cdsl_arena_free(cdsl_arena_t* arena);
void* cdsl_arena_alloc(cdsl_arena_t* arena, size_t size);
char* cdsl_arena_strdup(cdsl_arena_t* arena, const char* s);

#endif
