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
	} data;
} cdsl_value_t;

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
 * @return 0 on success, -1 if not found
 */
int cdsl_context_remove(cdsl_context_t* ctx, const char* name);

/**
 * @brief Load variables from a JSON string into the context.
 * @param ctx      Context
 * @param json_str JSON object string
 * @return 0 on success, -1 on parse error
 */
int cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str);

/**
 * @brief Free memory held by a value (e.g. array, string).
 * @param val Value to free
 */
void cdsl_value_free(cdsl_value_t* val);

#endif
/** @} */
