/**
 * @file cdsl/util/hashmap.h
 * @brief Hash map implementation for O(1) key-value lookups.
 *
 * @defgroup cdsl_hashmap Hash Map
 * @{
 */
#ifndef CDSL_UTIL_HASHMAP_H
#define CDSL_UTIL_HASHMAP_H

#include "cdsl/util/portability.h"

#include "cdsl/util/portability.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Hash map entry (internal).
 */
typedef struct cdsl_hashmap_entry {
	char* key;			 /**< Key string (owned) */
	void* value;			 /**< Value pointer (not owned) */
	struct cdsl_hashmap_entry* next; /**< Collision chain */
} cdsl_hashmap_entry_t;

/**
 * @brief Hash map with separate chaining.
 */
typedef struct cdsl_hashmap {
	cdsl_hashmap_entry_t** buckets; /**< Bucket array */
	int bucket_count;		/**< Number of buckets */
	int size;			/**< Number of entries */
} cdsl_hashmap_t;

/**
 * @brief Value destructor callback type.
 */
typedef void (*cdsl_hashmap_free_fn)(void* value);

/**
 * @brief Create a new hash map.
 * @param bucket_count Number of buckets (0 for default 64)
 * @return Newly allocated hash map
 */
CDSL_NODISCARD
cdsl_hashmap_t* cdsl_hashmap_create(int bucket_count);

/**
 * @brief Free a hash map and all entries.
 * @param map Hash map to free (NULL-safe)
 * @param free_fn Optional destructor for values (may be NULL)
 */
void cdsl_hashmap_free(cdsl_hashmap_t* map, cdsl_hashmap_free_fn free_fn);

/**
 * @brief Insert or update a key-value pair.
 * @param map Target hash map
 * @param key Key string (duplicated internally)
 * @param value Value pointer (not owned by the map)
 * @return true on success
 */
bool cdsl_hashmap_put(cdsl_hashmap_t* map, const char* key, void* value);

/**
 * @brief Look up a value by key.
 * @param map Target hash map
 * @param key Key to search for
 * @return Value pointer, or NULL if not found
 */
CDSL_NODISCARD
void* cdsl_hashmap_get(cdsl_hashmap_t* map, const char* key);

/**
 * @brief Remove an entry by key.
 * @param map Target hash map
 * @param key Key to remove
 * @param free_fn Optional destructor for the removed value (may be NULL)
 * @return true if removed, false if not found
 */
bool cdsl_hashmap_remove(cdsl_hashmap_t* map, const char* key, cdsl_hashmap_free_fn free_fn);

/**
 * @brief Check if a key exists in the map.
 * @param map Target map
 * @param key Key to search for
 * @return true if key exists
 */
bool cdsl_hashmap_has(const cdsl_hashmap_t* map, const char* key);

/**
 * @brief Callback iterator function type.
 */
typedef void (*cdsl_hashmap_iter_fn)(const char* key, void* value, void* user_data);

/**
 * @brief Iterate over all entries in the map.
 * @param map Target map
 * @param cb Callback function (called for each entry)
 * @param user_data Opaque pointer passed to callback
 */
void cdsl_hashmap_iterate(const cdsl_hashmap_t* map, cdsl_hashmap_iter_fn cb, void* user_data);

/**
 * @brief Get all keys in the map as a NULL-terminated array.
 *
 * Both the array and individual key strings are heap-allocated;
 * caller must free them.
 *
 * @param map Target map
 * @param count Optional output for key count (may be NULL)
 * @return Allocated array of duplicated key strings, or NULL if empty
 */
CDSL_NODISCARD
char** cdsl_hashmap_keys(const cdsl_hashmap_t* map, int* count);

#endif
/** @} */
