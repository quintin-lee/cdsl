#include "cdsl_hashmap.h"
#include <stdlib.h>
#include <string.h>

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
	m->bucket_count = bucket_count > 0 ? bucket_count : 64;
	m->buckets = calloc(m->bucket_count, sizeof(cdsl_hashmap_entry_t*));
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
