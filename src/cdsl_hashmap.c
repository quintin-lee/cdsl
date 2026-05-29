#include "cdsl_hashmap.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Compute a hash for a string key (internal).
 *
 * Uses the djb2 hash algorithm.
 *
 * @param key          NUL-terminated string
 * @param bucket_count Number of buckets in the map
 * @return Bucket index in [0, bucket_count)
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

/**
 * @brief Create a new hash map.
 *
 * @param bucket_count Number of buckets (0 = default 64)
 * @return New map, or NULL on allocation failure
 */
cdsl_hashmap_t*
cdsl_hashmap_create(int bucket_count)
{
	cdsl_hashmap_t* m = calloc(1, sizeof(*m));
	m->bucket_count = bucket_count > 0 ? bucket_count : 64;
	m->buckets = calloc(m->bucket_count, sizeof(cdsl_hashmap_entry_t*));
	return m;
}

/**
 * @brief Free a hash map and all its entries.
 *
 * @param map     Map to destroy (NULL-safe)
 * @param free_fn Optional destructor for values (can be NULL)
 */
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

/**
 * @brief Insert or update a key-value pair.
 *
 * If the key already exists, its value is replaced.
 *
 * @param map   Target map
 * @param key   Key string (copied internally)
 * @param value Value pointer (not copied)
 * @return 1 on success, 0 on error
 */
int
cdsl_hashmap_put(cdsl_hashmap_t* map, const char* key, void* value)
{
	if (!map || !key) {
		return 0;
	}
	unsigned int idx = hash_string(key, map->bucket_count);
	cdsl_hashmap_entry_t* e = map->buckets[idx];
	while (e) {
		if (strcmp(e->key, key) == 0) {
			e->value = value;
			return 1;
		}
		e = e->next;
	}
	e = calloc(1, sizeof(*e));
	e->key = strdup(key);
	e->value = value;
	e->next = map->buckets[idx];
	map->buckets[idx] = e;
	map->size++;
	return 1;
}

/**
 * @brief Look up a value by key.
 *
 * @param map Target map
 * @param key Key to look up
 * @return Value pointer, or NULL if not found
 */
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

/**
 * @brief Remove an entry by key.
 *
 * @param map     Target map
 * @param key     Key to remove
 * @param free_fn Optional destructor for the removed value
 * @return 1 if the entry was found and removed, 0 otherwise
 */
int
cdsl_hashmap_remove(cdsl_hashmap_t* map, const char* key, cdsl_hashmap_free_fn free_fn)
{
	if (!map || !key) {
		return 0;
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
			return 1;
		}
		pp = &(*pp)->next;
	}
	return 0;
}

int
cdsl_hashmap_has(const cdsl_hashmap_t* map, const char* key)
{
	if (!map || !key) {
		return 0;
	}
	unsigned int idx = hash_string(key, map->bucket_count);
	const cdsl_hashmap_entry_t* e = map->buckets[idx];
	while (e) {
		if (strcmp(e->key, key) == 0) {
			return 1;
		}
		e = e->next;
	}
	return 0;
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
