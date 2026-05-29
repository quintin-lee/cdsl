#include "cdsl_arena.h"
#include <stdlib.h>
#include <string.h>

#define ARENA_DEFAULT_BLOCK_SIZE (64 * 1024)

cdsl_arena_t* cdsl_arena_create(size_t block_size) {
    cdsl_arena_t* a = calloc(1, sizeof(*a));
    a->block_size = block_size > 0 ? block_size : ARENA_DEFAULT_BLOCK_SIZE;
    return a;
}

static cdsl_arena_block_t* arena_new_block(cdsl_arena_t* arena, size_t min_size) {
    size_t cap = arena->block_size;
    if (min_size > cap) cap = min_size;
    cdsl_arena_block_t* b = calloc(1, sizeof(*b));
    b->data = malloc(cap);
    b->capacity = cap;
    b->used = 0;
    b->next = arena->blocks;
    arena->blocks = b;
    return b;
}

void* cdsl_arena_alloc(cdsl_arena_t* arena, size_t size) {
    if (!arena || size == 0) return NULL;
    size = (size + 7) & ~7;
    cdsl_arena_block_t* b = arena->blocks;
    if (!b || b->used + size > b->capacity) {
        b = arena_new_block(arena, size);
    }
    void* ptr = b->data + b->used;
    b->used += size;
    return ptr;
}

char* cdsl_arena_strdup(cdsl_arena_t* arena, const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* d = cdsl_arena_alloc(arena, len);
    memcpy(d, s, len);
    return d;
}

void cdsl_arena_free(cdsl_arena_t* arena) {
    if (!arena) return;
    cdsl_arena_block_t* b = arena->blocks;
    while (b) {
        cdsl_arena_block_t* next = b->next;
        free(b->data);
        free(b);
        b = next;
    }
    free(arena);
}
