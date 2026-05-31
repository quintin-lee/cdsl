/**
 * @file cdsl/context.h
 * @brief Execution context: variable bindings and lifecycle.
 *
 * @defgroup cdsl_context Context
 * @{
 */
#ifndef CDSL_CONTEXT_H
#define CDSL_CONTEXT_H

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

[[nodiscard]]
cdsl_context_t* cdsl_context_create(const cdsl_schema_t* schema);
void cdsl_context_free(cdsl_context_t* ctx);
void cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val);
void cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val);
void cdsl_context_set_date(cdsl_context_t* ctx, const char* name, time_t val);
void cdsl_context_set_long(cdsl_context_t* ctx, const char* name, int64_t val);
int cdsl_context_get_int(const cdsl_context_t* ctx, const char* name, int default_val);
double cdsl_context_get_float(const cdsl_context_t* ctx, const char* name, double default_val);
int cdsl_context_get_bool(const cdsl_context_t* ctx, const char* name, int default_val);
const char*
cdsl_context_get_string(const cdsl_context_t* ctx, const char* name, const char* default_val);
time_t cdsl_context_get_date(const cdsl_context_t* ctx, const char* name, time_t default_val);
int64_t cdsl_context_get_long(const cdsl_context_t* ctx, const char* name, int64_t default_val);
int cdsl_context_remove(cdsl_context_t* ctx, const char* name);
int cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str);

/**
 * @brief Free memory held by a value (e.g. array, string).
 * @param val Value to free
 */
void cdsl_value_free(cdsl_value_t* val);

#endif
/** @} */
