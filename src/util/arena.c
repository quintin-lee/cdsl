/**
 * @file src/util/arena.c
 * @brief Arena allocator implementation.
 *
 * @ingroup cdsl_arena
 * @defgroup cdsl_arena_impl Arena implementation
 * @{
 */

#include "cdsl/util/arena.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Allocate a new arena block when current block is exhausted.
 *
 * Creates a fresh memory block sized to fit min_size or the default
 * block size, whichever is larger.
 * @param arena Target arena
 * @param min_size Minimum bytes the new block must hold
 * @return New block, or NULL on allocation failure
 */
static cdsl_arena_block_t*
arena_new_block(cdsl_arena_t* arena, size_t min_size)
{
	size_t cap = arena->block_size;
	if (min_size > cap) {
		cap = min_size;
	}
	cdsl_arena_block_t* b = calloc(1, sizeof(*b));
	if (!b) {
		return NULL;
	}
	b->data = malloc(cap);
	if (!b->data) {
		free(b);
		return NULL;
	}
	b->capacity = cap;
	b->used = 0;
	b->next = arena->blocks;
	arena->blocks = b;
	return b;
}

/**
 * @brief Create a new arena allocator.
 *
 * Allocates the arena control structure. No backing memory is allocated
 * until the first cdsl_arena_alloc() call.
 */
cdsl_arena_t*
cdsl_arena_create(size_t block_size)
{
	cdsl_arena_t* a = calloc(1, sizeof(*a));
	if (!a) {
		return NULL;
	}
	a->block_size = block_size > 0 ? block_size : CDSL_ARENA_DEFAULT_BLOCK_SIZE;
	return a;
}

/**
 * @brief Allocate 8-byte aligned memory from an arena.
 *
 * If the current block lacks space, a new block is created automatically.
 */
void*
cdsl_arena_alloc(cdsl_arena_t* arena, size_t size)
{
	if (!arena || size == 0) {
		return NULL;
	}
	size = (size + 7) & ~7;
	cdsl_arena_block_t* b = arena->blocks;
	if (!b || b->used + size > b->capacity) {
		b = arena_new_block(arena, size);
		if (!b) {
			return NULL;
		}
	}
	void* ptr = b->data + b->used;
	b->used += size;
	return ptr;
}

/**
 * @brief Duplicate a string using arena memory.
 */
char*
cdsl_arena_strdup(cdsl_arena_t* arena, const char* s)
{
	if (!s) {
		return NULL;
	}
	size_t len = strlen(s) + 1;
	char* d = cdsl_arena_alloc(arena, len);
	memcpy(d, s, len);
	return d;
}

/**
 * @brief Free all arena memory at once.
 *
 * Iterates through all blocks and frees them, then frees the arena.
 */
void
cdsl_arena_free(cdsl_arena_t* arena)
{
	if (!arena) {
		return;
	}
	cdsl_arena_block_t* b = arena->blocks;
	while (b) {
		cdsl_arena_block_t* next = b->next;
		free(b->data);
		free(b);
		b = next;
	}
	free(arena);
}
/** @} */
