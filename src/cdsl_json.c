#include "cdsl_json.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/**
 * @brief Internal JSON parser state.
 */
typedef struct {
	const char* src; /**< Source string being parsed */
	int pos;	 /**< Current parse position */
} json_parser_t;

/**
 * @brief Skip whitespace characters (internal).
 * @param p Parser state (modified in place)
 */
static void
skip_ws(json_parser_t* p)
{
	while (p->src[p->pos] && isspace((unsigned char)p->src[p->pos])) {
		p->pos++;
	}
}

/**
 * @brief Parse a raw JSON string (internal).
 *
 * Expects the current position to be at the opening quote.
 * Advances past the closing quote and returns the extracted content.
 * Does NOT handle escape sequences (except positioning).
 *
 * @param p Parser state
 * @return Allocated string content (caller frees), or NULL on error
 */
static char*
parse_string_raw(json_parser_t* p)
{
	if (p->src[p->pos] != '"') {
		return NULL;
	}
	p->pos++;
	int start = p->pos;
	while (p->src[p->pos] && p->src[p->pos] != '"') {
		if (p->src[p->pos] == '\\') {
			p->pos++;
		}
		p->pos++;
	}
	int len = p->pos - start;
	if (p->src[p->pos] == '"') {
		p->pos++;
	}
	char* s = malloc(len + 1);
	memcpy(s, p->src + start, len);
	s[len] = '\0';
	return s;
}

static cdsl_json_value_t* parse_value(json_parser_t* p);

/**
 * @brief Parse a JSON object {...} (internal).
 *
 * Recursively parses key-value pairs into a linked list of
 * cdsl_json_value_t nodes with JSON_OBJECT type.
 *
 * @param p Parser state
 * @return Object value node, or NULL on parse failure
 */
static cdsl_json_value_t*
parse_object(json_parser_t* p)
{
	if (p->src[p->pos] != '{') {
		return NULL;
	}
	p->pos++;
	cdsl_json_value_t* head = NULL;
	cdsl_json_value_t* tail = NULL;
	int count = 0;
	skip_ws(p);
	while (p->src[p->pos] && p->src[p->pos] != '}') {
		skip_ws(p);
		char* key = parse_string_raw(p);
		if (!key) {
			break;
		}
		skip_ws(p);
		if (p->src[p->pos] == ':') {
			p->pos++;
		}
		skip_ws(p);
		cdsl_json_value_t* val = parse_value(p);
		if (val) {
			val->key = key;
			val->next = NULL;
			if (tail) {
				tail->next = val;
			} else {
				head = val;
			}
			tail = val;
			count++;
		} else {
			free(key);
		}
		skip_ws(p);
		if (p->src[p->pos] == ',') {
			p->pos++;
		}
	}
	if (p->src[p->pos] == '}') {
		p->pos++;
	}

	cdsl_json_value_t* obj = calloc(1, sizeof(*obj));
	obj->type = JSON_OBJECT;
	obj->value.object.items = head;
	obj->value.object.count = count;
	return obj;
}

/**
 * @brief Parse a JSON array [...] (internal).
 *
 * Recursively parses elements into a linked list of
 * cdsl_json_value_t nodes with JSON_ARRAY type.
 *
 * @param p Parser state
 * @return Array value node, or NULL on parse failure
 */
static cdsl_json_value_t*
parse_array(json_parser_t* p)
{
	if (p->src[p->pos] != '[') {
		return NULL;
	}
	p->pos++;
	cdsl_json_value_t* head = NULL;
	cdsl_json_value_t* tail = NULL;
	int count = 0;
	skip_ws(p);
	while (p->src[p->pos] && p->src[p->pos] != ']') {
		cdsl_json_value_t* val = parse_value(p);
		if (val) {
			val->next = NULL;
			if (tail) {
				tail->next = val;
			} else {
				head = val;
			}
			tail = val;
			count++;
		}
		skip_ws(p);
		if (p->src[p->pos] == ',') {
			p->pos++;
		}
	}
	if (p->src[p->pos] == ']') {
		p->pos++;
	}

	cdsl_json_value_t* arr = calloc(1, sizeof(*arr));
	arr->type = JSON_ARRAY;
	arr->value.array.items = head;
	arr->value.array.count = count;
	return arr;
}

/**
 * @brief Parse any JSON value (internal).
 *
 * Dispatches to the appropriate sub-parser based on the
 * first non-whitespace character.
 *
 * @param p Parser state
 * @return Parsed value node, or NULL
 */
static cdsl_json_value_t*
parse_value(json_parser_t* p)
{
	skip_ws(p);
	char c = p->src[p->pos];
	if (c == '{') {
		return parse_object(p);
	}
	if (c == '[') {
		return parse_array(p);
	}
	if (c == '"') {
		char* s = parse_string_raw(p);
		cdsl_json_value_t* v = calloc(1, sizeof(*v));
		v->type = JSON_STRING;
		v->value.string_val = s;
		return v;
	}
	if (c == 't' || c == 'f') {
		cdsl_json_value_t* v = calloc(1, sizeof(*v));
		v->type = JSON_BOOL;
		if (strncmp(p->src + p->pos, "true", 4) == 0) {
			v->value.bool_val = 1;
			p->pos += 4;
		} else {
			v->value.bool_val = 0;
			p->pos += 5;
		}
		return v;
	}
	if (c == 'n') {
		p->pos += 4;
		return NULL;
	}
	if (c == '-' || isdigit((unsigned char)c)) {
		cdsl_json_value_t* v = calloc(1, sizeof(*v));
		v->type = JSON_NUMBER;
		char* end;
		v->value.number_val = strtod(p->src + p->pos, &end);
		p->pos = (int)(end - p->src);
		return v;
	}
	return NULL;
}

/**
 * @brief Parse a JSON string into a value tree.
 *
 * Supports objects, arrays, strings, numbers, booleans, and null.
 * Strings are not de-escaped.
 *
 * @param json NUL-terminated JSON string
 * @return Root value node (caller must free with cdsl_json_free), or NULL on error
 */
cdsl_json_value_t*
cdsl_json_parse(const char* json)
{
	if (!json) {
		return NULL;
	}
	json_parser_t p = {.src = json, .pos = 0};
	return parse_value(&p);
}

/**
 * @brief Recursively free a JSON value tree.
 *
 * Frees the node, its key (if any), and all child nodes.
 *
 * @param val Root value to free (NULL-safe)
 */
void
cdsl_json_free(cdsl_json_value_t* val)
{
	if (!val) {
		return;
	}
	if (val->key) {
		free(val->key);
	}
	switch (val->type) {
	case JSON_STRING:
		free(val->value.string_val);
		break;
	case JSON_OBJECT:
	case JSON_ARRAY: {
		cdsl_json_value_t* item =
		    val->type == JSON_OBJECT ? val->value.object.items : val->value.array.items;
		while (item) {
			cdsl_json_value_t* next = item->next;
			cdsl_json_free(item);
			item = next;
		}
		break;
	}
	default:
		break;
	}
	free(val);
}
