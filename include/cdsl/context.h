/**
 * @file cdsl/context.h
 * @brief Execution context: variable bindings and lifecycle.
 *
 * @defgroup cdsl_context Context
 * @{
 */
#ifndef CDSL_CONTEXT_H
#define CDSL_CONTEXT_H

#include "cdsl/util/portability.h"

#include "cdsl/schema.h"
#include "cdsl/util/hashmap.h"
#include <time.h>
#include <stdint.h>

/**
 * @brief Runtime value wrapper.
 */

struct cdsl_map;

typedef struct cdsl_array {
	struct cdsl_value* items;
	int count;
	int capacity;
} cdsl_array_t;

typedef struct cdsl_value {
	cdsl_type_t type;
	union {
		int int_val;
		double float_val;
		int bool_val;
		char* string_val;
		time_t date_val;
		int64_t long_val;
		cdsl_array_t* array_val;
		struct cdsl_map* map_val;
	} data;
	int owned_string; /**< 1 if data.string_val is heap-owned and must be freed by cdsl_value_free; 0 if borrowed from AST/context/bytecode */
} cdsl_value_t;

/**
 * @brief Map entry for CDSL_TYPE_MAP.
 */
typedef struct cdsl_map_entry {
	char* key;
	cdsl_value_t value;
	struct cdsl_map_entry* next;
} cdsl_map_entry_t;

/**
 * @brief Map / Object data structure.
 */
typedef struct cdsl_map {
	cdsl_map_entry_t** buckets;
	int bucket_count;
	int count;
} cdsl_map_t;

/**
 * @brief Context variable entry (linked list node).
 */
typedef struct cdsl_context_entry {
	char* name;
	cdsl_value_t value;
	struct cdsl_context_entry* next;
} cdsl_context_entry_t;

/**
 * @brief Execution context holding all variable bindings.
 */
typedef struct cdsl_context {
	const cdsl_schema_t* schema;
	cdsl_context_entry_t* entries;
	cdsl_hashmap_t* map;
} cdsl_context_t;

/**
 * @brief Create a new execution context bound to a schema.
 * @param schema Schema defining allowed variables
 * @return New context, or NULL on allocation failure
 */
CDSL_NODISCARD
cdsl_context_t* cdsl_context_create(const cdsl_schema_t* schema);

/**
 * @brief Free a context and all its variable bindings.
 * @param ctx Context to free (may be NULL)
 */
void cdsl_context_free(cdsl_context_t* ctx);

/**
 * @brief Set an integer variable.
 * @param ctx  Context
 * @param name Variable name
 * @param val  Value
 */
void cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val);

/**
 * @brief Set a float variable.
 * @param ctx  Context
 * @param name Variable name
 * @param val  Value
 */
void cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val);

/**
 * @brief Set a boolean variable.
 * @param ctx  Context
 * @param name Variable name
 * @param val  Value (nonzero = true)
 */
void cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val);

/**
 * @brief Set a string variable (copies the string).
 * @param ctx  Context
 * @param name Variable name
 * @param val  Value
 */
void cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val);

/**
 * @brief Set a date variable.
 * @param ctx  Context
 * @param name Variable name
 * @param val  Unix timestamp
 */
void cdsl_context_set_date(cdsl_context_t* ctx, const char* name, time_t val);

/**
 * @brief Set a 64-bit long variable.
 * @param ctx  Context
 * @param name Variable name
 * @param val  Value
 */
void cdsl_context_set_long(cdsl_context_t* ctx, const char* name, int64_t val);

/**
 * @brief Set a Map variable in the context (takes ownership of val).
 * @param ctx  Context
 * @param name Variable name
 * @param val  Map pointer
 */
void cdsl_context_set_map(cdsl_context_t* ctx, const char* name, cdsl_map_t* val);

/**
 * @brief Get a Map variable from the context.
 * @param ctx  Context
 * @param name Variable name
 * @return Stored Map pointer or NULL
 */
cdsl_map_t* cdsl_context_get_map(const cdsl_context_t* ctx, const char* name);

/**
 * @brief Get an integer variable, or default if missing.
 * @param ctx         Context
 * @param name        Variable name
 * @param default_val Fallback value
 * @return Stored value or default
 */
int cdsl_context_get_int(const cdsl_context_t* ctx, const char* name, int default_val);

/**
 * @brief Get a float variable, or default if missing.
 * @param ctx         Context
 * @param name        Variable name
 * @param default_val Fallback value
 * @return Stored value or default
 */
double cdsl_context_get_float(const cdsl_context_t* ctx, const char* name, double default_val);

/**
 * @brief Get a boolean variable, or default if missing.
 * @param ctx         Context
 * @param name        Variable name
 * @param default_val Fallback value
 * @return Stored value or default
 */
int cdsl_context_get_bool(const cdsl_context_t* ctx, const char* name, int default_val);

/**
 * @brief Get a string variable, or default if missing.
 * @param ctx         Context
 * @param name        Variable name
 * @param default_val Fallback value
 * @return Stored value or default (owned by context, do not free)
 */
const char*
cdsl_context_get_string(const cdsl_context_t* ctx, const char* name, const char* default_val);

/**
 * @brief Get a date variable, or default if missing.
 * @param ctx         Context
 * @param name        Variable name
 * @param default_val Fallback value
 * @return Unix timestamp or default
 */
time_t cdsl_context_get_date(const cdsl_context_t* ctx, const char* name, time_t default_val);

/**
 * @brief Get a 64-bit long variable, or default if missing.
 * @param ctx         Context
 * @param name        Variable name
 * @param default_val Fallback value
 * @return Stored value or default
 */
int64_t cdsl_context_get_long(const cdsl_context_t* ctx, const char* name, int64_t default_val);

/**
 * @brief Remove a variable from the context.
 * @param ctx  Context
 * @param name Variable name
 * @return 1 on success, 0 if not found
 */
int cdsl_context_remove(cdsl_context_t* ctx, const char* name);

/**
 * @brief Load variables from a JSON string into the context.
 * @param ctx      Context
 * @param json_str JSON object string
 * @return 1 on success, 0 on parse error
 */
int cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str);

/**
 * @brief Create a new runtime Map.
 * @param bucket_count Initial bucket count (0 for default 16)
 * @return Newly allocated Map, or NULL on error
 */
CDSL_NODISCARD
cdsl_map_t* cdsl_map_create(int bucket_count);

/**
 * @brief Free a Map and all contained values.
 * @param map Map to free (NULL-safe)
 */
void cdsl_map_free(cdsl_map_t* map);

/**
 * @brief Set or update a key in the Map (deep-copies the value).
 * @param map Target Map
 * @param key Key string
 * @param val Value to store
 * @return 1 on success, 0 on failure
 */
int cdsl_map_set(cdsl_map_t* map, const char* key, const cdsl_value_t* val);

/**
 * @brief Get a value pointer from the Map by key.
 * @param map Target Map
 * @param key Key string
 * @return Pointer to value, or NULL if not found
 */
const cdsl_value_t* cdsl_map_get(const cdsl_map_t* map, const char* key);

/**
 * @brief Check if a key exists in the Map.
 * @param map Target Map
 * @param key Key string
 * @return 1 if exists, 0 otherwise
 */
int cdsl_map_has(const cdsl_map_t* map, const char* key);

/**
 * @brief Deep copy a Map.
 * @param map Source map
 * @return Newly allocated copy of Map, or NULL on error
 */
CDSL_NODISCARD
cdsl_map_t* cdsl_map_copy(const cdsl_map_t* map);

/**
 * @brief Deep copy a runtime value.
 * @param src Source value
 * @return Cloned value
 */
cdsl_value_t cdsl_value_copy(const cdsl_value_t* src);

/**
 * @brief Free memory held by a value (e.g. array, map, string).
 * @param val Value to free
 */
void cdsl_value_free(cdsl_value_t* val);

#endif
/** @} */
