/**
 * @file cdsl_arena.h
 * @brief Arena (bump) allocator for batch memory allocation.
 *
 * Provides fast allocation by bumping a pointer within pre-allocated
 * blocks. All memory is freed at once when the arena is destroyed,
 * making it ideal for AST nodes with shared lifetimes.
 *
 * @defgroup arena Arena Allocator
 * @{
 */

#ifndef CDSL_ARENA_H
#define CDSL_ARENA_H

#include <stddef.h>

/**
 * @brief Arena memory block (internal).
 */
typedef struct cdsl_arena_block {
    char* data;                  /**< Block data buffer */
    size_t used;                 /**< Bytes used */
    size_t capacity;             /**< Block capacity */
    struct cdsl_arena_block* next; /**< Next block in chain */
} cdsl_arena_block_t;

/**
 * @brief Arena allocator.
 *
 * Allocates from large pre-allocated blocks. Individual allocations
 * cannot be freed; only the entire arena can be freed at once.
 *
 * @code
 * cdsl_arena_t* arena = cdsl_arena_create(0); // default 64KB blocks
 * char* s = cdsl_arena_strdup(arena, "hello");
 * // ... use s ...
 * cdsl_arena_free(arena); // frees everything at once
 * @endcode
 */
typedef struct cdsl_arena {
    cdsl_arena_block_t* blocks; /**< Linked list of memory blocks */
    size_t block_size;          /**< Default block size */
} cdsl_arena_t;

/**
 * @brief Create a new arena allocator.
 * @param block_size Block size in bytes (0 for default 64KB)
 * @return Newly allocated arena
 */
cdsl_arena_t* cdsl_arena_create(size_t block_size);

/**
 * @brief Free an arena and all its memory.
 * @param arena Arena to free (NULL-safe)
 */
void cdsl_arena_free(cdsl_arena_t* arena);

/**
 * @brief Allocate memory from the arena.
 *
 * Memory is 8-byte aligned. Returns NULL if size is 0.
 *
 * @param arena Target arena
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory (valid until arena is freed)
 */
void* cdsl_arena_alloc(cdsl_arena_t* arena, size_t size);

/**
 * @brief Duplicate a string using arena allocation.
 * @param arena Target arena
 * @param s String to duplicate (NULL returns NULL)
 * @return Pointer to duplicated string
 */
char* cdsl_arena_strdup(cdsl_arena_t* arena, const char* s);

#endif
/** @} */
