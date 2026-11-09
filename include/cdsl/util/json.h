/**
 * @file cdsl/util/json.h
 * @brief Lightweight zero-dependency JSON parser.
 *
 * @defgroup cdsl_json JSON Parser
 * @{
 */
#ifndef CDSL_UTIL_JSON_H
#define CDSL_UTIL_JSON_H

#include "cdsl/util/portability.h"

/**
 * @brief JSON value type.
 */
typedef enum {
	CDSL_JSON_NULL,	  /**< null */
	CDSL_JSON_BOOL,	  /**< true / false */
	CDSL_JSON_NUMBER, /**< number */
	CDSL_JSON_STRING, /**< string */
	CDSL_JSON_OBJECT, /**< object */
	CDSL_JSON_ARRAY	  /**< array */
} cdsl_json_type_t;

/**
 * @brief JSON value node.
 */
typedef struct cdsl_json_value {
	char* key;
	cdsl_json_type_t type;
	int is_int; /**< Number fields: 1 when the number was parsed as a pure
	             * integer (so value.i64 is the lossless 64-bit value), 0
	             * otherwise (so value.f64 is the double). 0 for non-number
	             * types. A pure integer that overflows int64_t falls back to
	             * f64 and is marked non-int (overflow protocol: degrade). */
	union {
		int bool_val;
		double f64; /**< floating-point number (formerly number_val) */
		int64_t i64; /**< 64-bit integer, valid when is_int != 0 */
		char* string_val;
		struct {
			struct cdsl_json_value* items;
			int count;
		} object;
		struct {
			struct cdsl_json_value* items;
			int count;
		} array;
	} value;
	struct cdsl_json_value* next;
} cdsl_json_value_t;

/**
 * @brief Parse a JSON string into a value tree.
 * @param json Null-terminated JSON string
 * @return Root value (must be freed with cdsl_json_free), or NULL on error
 */
CDSL_NODISCARD
cdsl_json_value_t* cdsl_json_parse(const char* json);

/**
 * @brief Free a JSON value tree.
 * @param val Root value to free (NULL-safe)
 */
void cdsl_json_free(cdsl_json_value_t* val);

/* Helper convenience functions expected by some tests */
int cdsl_json_object_length(const cdsl_json_value_t* obj);
int cdsl_json_array_length(const cdsl_json_value_t* arr);
cdsl_json_value_t* cdsl_json_get_object(const cdsl_json_value_t* obj, const char* key);
cdsl_json_value_t* cdsl_json_get_array(const cdsl_json_value_t* arr, int idx);

#endif
/** @} */
