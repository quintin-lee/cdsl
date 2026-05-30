/**
 * @file cdsl_arena.c
 * @brief Arena (bump) allocator implementation.
 *
 * Provides fast O(1) allocation by bumping a pointer within pre-allocated
 * blocks. All memory is freed at once when the arena is destroyed,
 * making it ideal for AST nodes, parser temporaries, and other
 * batch-lifetime allocations. No individual free is required.
 *
 * Each arena manages a linked list of fixed-size blocks. When the current
 * block is exhausted, a new block (doubling or sized to fit the request)
 * is allocated and added to the list.
 */

#include "cdsl_arena.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Default block size for arena allocations.
 *
 * 64 KB provides a good balance between memory overhead and allocation
 * frequency for typical DSL rule ASTs.
 */
#define ARENA_DEFAULT_BLOCK_SIZE (64 * 1024)

/**
 * @brief Create a new arena allocator.
 *
 * Allocates the arena control structure. No backing memory is allocated
 * until the first cdsl_arena_alloc() call.
 *
 * @param block_size  Size of each arena block (0 = default 64KB)
 * @return New arena instance, or NULL on allocation failure
 */
cdsl_arena_t*
cdsl_arena_create(size_t block_size)
{
	cdsl_arena_t* a = calloc(1, sizeof(*a));
	a->block_size = block_size > 0 ? block_size : ARENA_DEFAULT_BLOCK_SIZE;
	return a;
}

/**
 * @brief Allocate a new arena block (internal).
 *
 * Creates a fresh memory block when the current one is exhausted.
 * If min_size exceeds the default block size, the new block is
 * sized to fit min_size.
 *
 * @param arena    Target arena
 * @param min_size Minimum capacity required
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
	b->data = malloc(cap);
	b->capacity = cap;
	b->used = 0;
	b->next = arena->blocks;
	arena->blocks = b;
	return b;
}

/**
 * @brief Allocate memory from an arena.
 *
 * Returns 8-byte aligned memory. If the current block lacks space,
 * a new block is created automatically.
 *
 * @param arena Target arena
 * @param size  Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on error
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
	}
	void* ptr = b->data + b->used;
	b->used += size;
	return ptr;
}

/**
 * @brief Duplicate a string using arena memory.
 *
 * @param arena Target arena
 * @param s     String to duplicate
 * @return Arena-allocated copy of s, or NULL on error
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
 * Iterates through all blocks and frees them, then frees the arena itself.
 * Individual allocations do not need to be freed separately.
 *
 * @param arena Arena to destroy (NULL-safe)
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
