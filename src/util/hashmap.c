/**
 * @file src/util/hashmap.c
 * @brief Hash map implementation with separate chaining.
 *
 * @ingroup cdsl_hashmap
 * @defgroup cdsl_hashmap_impl Hash map implementation
 * @{
 */

#include "cdsl/util/hashmap.h"
#include <stdlib.h>
#include <string.h>

/** @brief Max load factor before automatic rehash. */
#define CDSL_HASHMAP_MAX_LOAD 0.75
/** @brief Minimum bucket count (must be power of two). */
#define CDSL_HASHMAP_MIN_BUCKETS 64
/** @brief Maximum allowed bucket count to prevent runaway reallocation. */
#define CDSL_HASHMAP_MAX_BUCKETS (1 << 20)

static unsigned int hash_string(const char* key, int bucket_count);

/**
 * @brief Internal rehash: double the bucket count and redistribute entries.
 * @return true on success, false on allocation failure (map unchanged)
 */
static bool
rehash_internal(cdsl_hashmap_t* map)
{
	int new_bc = map->bucket_count * 2;
	if (new_bc > CDSL_HASHMAP_MAX_BUCKETS) {
		return false;
	}
	cdsl_hashmap_entry_t** new_buckets = calloc(new_bc, sizeof(cdsl_hashmap_entry_t*));
	if (!new_buckets) {
		return false;
	}
	/* Move entries from old buckets to new */
	for (int i = 0; i < map->bucket_count; i++) {
		cdsl_hashmap_entry_t* e = map->buckets[i];
		while (e) {
			cdsl_hashmap_entry_t* next = e->next;
			unsigned int idx = hash_string(e->key, new_bc);
			e->next = new_buckets[idx];
			new_buckets[idx] = e;
			e = next;
		}
	}
	free(map->buckets);
	map->buckets = new_buckets;
	map->bucket_count = new_bc;
	return true;
}

/**
 * @brief Compute djb2 hash for a string key.
 * @param key NUL-terminated string
 * @param bucket_count Number of buckets
 * @return Bucket index
 */
static unsigned int
hash_string(const char* key, int bucket_count)
{
	unsigned int h = 5381;
	for (; *key; key++) {
		h = ((h << 5) + h) + (unsigned char)*key;
	}
	return h % bucket_count;
}

cdsl_hashmap_t*
cdsl_hashmap_create(int bucket_count)
{
	cdsl_hashmap_t* m = calloc(1, sizeof(*m));
	if (!m) {
		return NULL;
	}
	m->bucket_count = bucket_count > 0 ? bucket_count : CDSL_HASHMAP_MIN_BUCKETS;
	m->buckets = calloc(m->bucket_count, sizeof(cdsl_hashmap_entry_t*));
	if (!m->buckets) {
		free(m);
		return NULL;
	}
	return m;
}

void
cdsl_hashmap_free(cdsl_hashmap_t* map, cdsl_hashmap_free_fn free_fn)
{
	if (!map) {
		return;
	}
	for (int i = 0; i < map->bucket_count; i++) {
		cdsl_hashmap_entry_t* e = map->buckets[i];
		while (e) {
			cdsl_hashmap_entry_t* next = e->next;
			free(e->key);
			if (free_fn && e->value) {
				free_fn(e->value);
			}
			free(e);
			e = next;
		}
	}
	free(map->buckets);
	free(map);
}

bool
cdsl_hashmap_put(cdsl_hashmap_t* map, const char* key, void* value)
{
	if (!map || !key) {
		return false;
	}
	unsigned int idx = hash_string(key, map->bucket_count);
	cdsl_hashmap_entry_t* e = map->buckets[idx];
	while (e) {
		if (strcmp(e->key, key) == 0) {
			e->value = value;
			return true;
		}
		e = e->next;
	}
	e = calloc(1, sizeof(*e));
	if (!e) {
		return false;
	}
	e->key = strdup(key);
	if (!e->key) {
		free(e);
		return false;
	}
	e->value = value;
	e->next = map->buckets[idx];
	map->buckets[idx] = e;
	map->size++;
	/* Automatic rehash when load factor exceeds 0.75 */
	if (map->bucket_count < CDSL_HASHMAP_MAX_BUCKETS && map->size * 4 > map->bucket_count * 3) {
		rehash_internal(map);
	}
	return true;
}

void*
cdsl_hashmap_get(cdsl_hashmap_t* map, const char* key)
{
	if (!map || !key) {
		return NULL;
	}
	unsigned int idx = hash_string(key, map->bucket_count);
	cdsl_hashmap_entry_t* e = map->buckets[idx];
	while (e) {
		if (strcmp(e->key, key) == 0) {
			return e->value;
		}
		e = e->next;
	}
	return NULL;
}

bool
cdsl_hashmap_remove(cdsl_hashmap_t* map, const char* key, cdsl_hashmap_free_fn free_fn)
{
	if (!map || !key) {
		return false;
	}
	unsigned int idx = hash_string(key, map->bucket_count);
	cdsl_hashmap_entry_t** pp = &map->buckets[idx];
	while (*pp) {
		if (strcmp((*pp)->key, key) == 0) {
			cdsl_hashmap_entry_t* del = *pp;
			*pp = del->next;
			free(del->key);
			if (free_fn && del->value) {
				free_fn(del->value);
			}
			free(del);
			map->size--;
			return true;
		}
		pp = &(*pp)->next;
	}
	return false;
}

bool
cdsl_hashmap_has(const cdsl_hashmap_t* map, const char* key)
{
	if (!map || !key) {
		return false;
	}
	unsigned int idx = hash_string(key, map->bucket_count);
	const cdsl_hashmap_entry_t* e = map->buckets[idx];
	while (e) {
		if (strcmp(e->key, key) == 0) {
			return true;
		}
		e = e->next;
	}
	return false;
}

void
cdsl_hashmap_iterate(const cdsl_hashmap_t* map, cdsl_hashmap_iter_fn cb, void* user_data)
{
	if (!map || !cb) {
		return;
	}
	for (int i = 0; i < map->bucket_count; i++) {
		const cdsl_hashmap_entry_t* e = map->buckets[i];
		while (e) {
			cb(e->key, e->value, user_data);
			e = e->next;
		}
	}
}

char**
cdsl_hashmap_keys(const cdsl_hashmap_t* map, int* count)
{
	if (count) {
		*count = 0;
	}
	if (!map || map->size == 0) {
		return NULL;
	}
	char** keys = malloc(sizeof(char*) * (map->size + 1));
	if (!keys) {
		return NULL;
	}
	int idx = 0;
	for (int i = 0; i < map->bucket_count; i++) {
		const cdsl_hashmap_entry_t* e = map->buckets[i];
		while (e) {
			keys[idx++] = strdup(e->key);
			e = e->next;
		}
	}
	keys[idx] = NULL;
	if (count) {
		*count = idx;
	}
	return keys;
}
/** @} */
