/**
 * @file cdsl_json.h
 * @brief Lightweight zero-dependency JSON parser.
 *
 * Parses JSON strings into a tree of cdsl_json_value_t nodes.
 * Supports objects, arrays, strings, numbers, booleans, and null.
 * Used internally by cdsl_context_load_json() for context binding.
 *
 * @defgroup json JSON Parser
 * @{
 */

#ifndef CDSL_JSON_H
#define CDSL_JSON_H

/**
 * @brief JSON value node.
 *
 * Represents any JSON value (object, array, string, number, bool, null).
 * For objects and arrays, child items are stored as linked lists via the
 * @c next pointer.
 */
typedef struct cdsl_json_value {
	char* key; /**< Key for object entries (NULL for array items) */
	enum {
		JSON_NULL,   /**< null literal */
		JSON_BOOL,   /**< true/false */
		JSON_NUMBER, /**< numeric value */
		JSON_STRING, /**< string value */
		JSON_OBJECT, /**< object { ... } */
		JSON_ARRAY   /**< array [ ... ] */
	} type;		     /**< Value type discriminator */
	union {
		int bool_val;	   /**< Boolean value */
		double number_val; /**< Numeric value */
		char* string_val;  /**< String value */
		struct {
			struct cdsl_json_value* items; /**< First child item */
			int count;		       /**< Number of items */
		} object;			       /**< Object data */
		struct {
			struct cdsl_json_value* items; /**< First element */
			int count;		       /**< Number of elements */
		} array;			       /**< Array data */
	} value;
	struct cdsl_json_value* next; /**< Next sibling in parent object/array */
} cdsl_json_value_t;

/**
 * @brief Parse a JSON string into a value tree.
 *
 * @param json Null-terminated JSON string
 * @return Root value (must be freed with cdsl_json_free), or NULL on error
 */
cdsl_json_value_t* cdsl_json_parse(const char* json);

/**
 * @brief Free a JSON value tree.
 * @param val Root value to free (NULL-safe)
 */
void cdsl_json_free(cdsl_json_value_t* val);

#endif
/** @} */
