/**
 * @file cdsl_hashmap.h
 * @brief Hash map implementation for O(1) key-value lookups.
 *
 * @defgroup hashmap Hash Map
 * @{
 */

#ifndef CDSL_HASHMAP_H
#define CDSL_HASHMAP_H

#include <stddef.h>

/**
 * @brief Hash map entry (internal).
 */
typedef struct cdsl_hashmap_entry {
    char* key;                    /**< Key string (owned) */
    void* value;                  /**< Value pointer (not owned) */
    struct cdsl_hashmap_entry* next; /**< Collision chain */
} cdsl_hashmap_entry_t;

/**
 * @brief Hash map with separate chaining.
 *
 * Provides O(1) average-case lookup, insertion, and deletion.
 * Keys are strings; values are opaque pointers.
 *
 * @code
 * cdsl_hashmap_t* map = cdsl_hashmap_create(64);
 * cdsl_hashmap_put(map, "user.age", &age_val);
 * int* p = cdsl_hashmap_get(map, "user.age");
 * cdsl_hashmap_free(map, NULL);
 * @endcode
 */
typedef struct cdsl_hashmap {
    cdsl_hashmap_entry_t** buckets; /**< Bucket array */
    int bucket_count;               /**< Number of buckets */
    int size;                       /**< Number of entries */
} cdsl_hashmap_t;

/**
 * @brief Value destructor callback type for cdsl_hashmap_free.
 * @param value Value pointer to free
 */
typedef void (*cdsl_hashmap_free_fn)(void* value);

/**
 * @brief Create a new hash map.
 * @param bucket_count Number of buckets (0 for default 64)
 * @return Newly allocated hash map
 */
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
 * @return 1 on success
 */
int cdsl_hashmap_put(cdsl_hashmap_t* map, const char* key, void* value);

/**
 * @brief Look up a value by key.
 * @param map Target hash map
 * @param key Key to search for
 * @return Value pointer, or NULL if not found
 */
void* cdsl_hashmap_get(cdsl_hashmap_t* map, const char* key);

/**
 * @brief Remove an entry by key.
 * @param map Target hash map
 * @param key Key to remove
 * @param free_fn Optional destructor for the removed value (may be NULL)
 * @return 1 if removed, 0 if not found
 */
int cdsl_hashmap_remove(cdsl_hashmap_t* map, const char* key, cdsl_hashmap_free_fn free_fn);

#endif
/** @} */
