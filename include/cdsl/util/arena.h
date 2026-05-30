/**
 * @file cdsl/util/arena.h
 * @brief Arena memory allocator with O(1) bulk deallocation.
 *
 * Provides a bump allocator where all allocations are released at once
 * by freeing the arena. Ideal for AST node allocation during parsing.
 *
 * @defgroup cdsl_arena Arena Allocator
 * @{
 */
#ifndef CDSL_UTIL_ARENA_H
#define CDSL_UTIL_ARENA_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Arena memory block (internal).
 */
typedef struct cdsl_arena_block {
	char* data;		       /**< Block data buffer */
	size_t used;		       /**< Bytes used */
	size_t capacity;	       /**< Block capacity */
	struct cdsl_arena_block* next; /**< Next block in chain */
} cdsl_arena_block_t;

/**
 * @brief Arena allocator.
 *
 * Allocates from large pre-allocated blocks. Individual allocations
 * cannot be freed; only the entire arena can be freed at once.
 */
typedef struct cdsl_arena {
	cdsl_arena_block_t* blocks; /**< Linked list of memory blocks */
	size_t block_size;	    /**< Default block size */
} cdsl_arena_t;

/** @brief Default block size for new arenas (64 KB). */
constexpr size_t CDSL_ARENA_DEFAULT_BLOCK_SIZE = 65536;

static_assert(CDSL_ARENA_DEFAULT_BLOCK_SIZE >= 16,
	      "Arena block size must accommodate 8-byte alignment");

/**
 * @brief Create a new arena allocator.
 * @param block_size Block size in bytes (0 for default 64KB)
 * @return Newly allocated arena, or NULL on failure
 */
[[nodiscard]]
cdsl_arena_t* cdsl_arena_create(size_t block_size);

/**
 * @brief Free an arena and all its memory.
 * @param arena Arena to free (NULL-safe)
 */
void cdsl_arena_free(cdsl_arena_t* arena);

/**
 * @brief Allocate 8-byte aligned memory from the arena.
 * @param arena Target arena
 * @param size Number of bytes to allocate (0 returns NULL)
 * @return Pointer to allocated memory, or NULL on failure
 */
[[nodiscard]]
void* cdsl_arena_alloc(cdsl_arena_t* arena, size_t size);

/**
 * @brief Duplicate a string using arena allocation.
 * @param arena Target arena
 * @param s String to duplicate (NULL-safe, returns NULL)
 * @return Arena-allocated copy of s, or NULL on failure
 */
[[nodiscard]]
char* cdsl_arena_strdup(cdsl_arena_t* arena, const char* s);

#endif
/** @} */
