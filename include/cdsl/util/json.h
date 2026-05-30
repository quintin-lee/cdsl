/**
 * @file cdsl/util/json.h
 * @brief Lightweight zero-dependency JSON parser.
 *
 * @defgroup cdsl_json JSON Parser
 * @{
 */
#ifndef CDSL_UTIL_JSON_H
#define CDSL_UTIL_JSON_H

/**
 * @brief JSON value node.
 */
typedef struct cdsl_json_value {
	char* key;
	enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_OBJECT, JSON_ARRAY } type;
	union {
		int bool_val;
		double number_val;
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
[[nodiscard]]
cdsl_json_value_t* cdsl_json_parse(const char* json);

/**
 * @brief Free a JSON value tree.
 * @param val Root value to free (NULL-safe)
 */
void cdsl_json_free(cdsl_json_value_t* val);

#endif
/** @} */
