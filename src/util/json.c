/**
 * @file src/util/json.c
 * @brief Lightweight JSON parser implementation.
 *
 * @ingroup cdsl_json
 * @defgroup cdsl_json_impl JSON parser implementation
 * @{
 */

#include "cdsl/util/json.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/** @brief Maximum nesting depth for JSON parsing (prevents C stack overflow). */
#define CDSL_JSON_MAX_DEPTH 256

typedef struct {
	const char* src;
	int pos;
	int depth;
} json_parser_t;

static void
skip_ws(json_parser_t* p)
{
	while (p->src[p->pos] && isspace((unsigned char)p->src[p->pos])) {
		p->pos++;
	}
}

static char*
parse_string_raw(json_parser_t* p)
{
	if (p->src[p->pos] != '"') {
		return NULL;
	}
	p->pos++;
	size_t cap = 64;
	size_t len = 0;
	char* s = malloc(cap);
	if (!s) {
		return NULL;
	}

#define JSON_STR_ENSURE(n)                                                                         \
	do {                                                                                       \
		if (len + (n) + 1 > cap) {                                                         \
			cap = (len + (n) + 1) * 2;                                                 \
			char* ns = realloc(s, cap);                                                \
			if (!ns) {                                                                 \
				free(s);                                                           \
				return NULL;                                                       \
			}                                                                          \
			s = ns;                                                                    \
		}                                                                                  \
	} while (0)

	while (p->src[p->pos] && p->src[p->pos] != '"') {
		if (p->src[p->pos] == '\\') {
			p->pos++;
			switch (p->src[p->pos]) {
			case '"':
				JSON_STR_ENSURE(1);
				s[len++] = '"';
				break;
			case '\\':
				JSON_STR_ENSURE(1);
				s[len++] = '\\';
				break;
			case '/':
				JSON_STR_ENSURE(1);
				s[len++] = '/';
				break;
			case 'b':
				JSON_STR_ENSURE(1);
				s[len++] = '\b';
				break;
			case 'f':
				JSON_STR_ENSURE(1);
				s[len++] = '\f';
				break;
			case 'n':
				JSON_STR_ENSURE(1);
				s[len++] = '\n';
				break;
			case 'r':
				JSON_STR_ENSURE(1);
				s[len++] = '\r';
				break;
			case 't':
				JSON_STR_ENSURE(1);
				s[len++] = '\t';
				break;
			case 'u': {
				int code = 0;
				for (int i = 0; i < 4; i++) {
					p->pos++;
					char c = p->src[p->pos];
					code *= 16;
					if (c >= '0' && c <= '9') {
						code += c - '0';
					} else if (c >= 'a' && c <= 'f') {
						code += c - 'a' + 10;
					} else if (c >= 'A' && c <= 'F') {
						code += c - 'A' + 10;
					} else {
						free(s);
						return NULL;
					}
				}
				if (code < 128) {
					JSON_STR_ENSURE(1);
					s[len++] = (char)code;
				} else if (code < 0x800) {
					JSON_STR_ENSURE(2);
					s[len++] = (char)(unsigned char)(0xC0 | (code >> 6));
					s[len++] = (char)(unsigned char)(0x80 | (code & 0x3F));
				} else {
					JSON_STR_ENSURE(3);
					s[len++] = (char)(unsigned char)(0xE0 | (code >> 12));
					s[len++] =
					    (char)(unsigned char)(0x80 | ((code >> 6) & 0x3F));
					s[len++] = (char)(unsigned char)(0x80 | (code & 0x3F));
				}
				break;
			}
			default:
				JSON_STR_ENSURE(1);
				s[len++] = p->src[p->pos];
				break;
			}
			p->pos++;
		} else {
			JSON_STR_ENSURE(1);
			s[len++] = p->src[p->pos++];
		}
	}
#undef JSON_STR_ENSURE
	if (p->src[p->pos] == '"') {
		p->pos++;
	}
	s[len] = '\0';
	return s;
}

static cdsl_json_value_t* parse_value(json_parser_t* p);

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
			/* malformed entry */
			break;
		}
		skip_ws(p);
		if (p->src[p->pos] == ':') {
			p->pos++;
		} else {
			/* missing colon -> malformed */
			free(key);
			break;
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
			/* malformed value */
			break;
		}
		skip_ws(p);
		if (p->src[p->pos] == ',') {
			p->pos++;
		} else {
			/* if next char is not ',' and not '}', loop will exit and we'll handle it */
		}
	}
	/* if we didn't end with a closing brace, treat as error */
	if (p->src[p->pos] != '}') {
		/* free collected items */
		cdsl_json_value_t* it = head;
		while (it) {
			cdsl_json_value_t* next = it->next;
			cdsl_json_free(it);
			it = next;
		}
		return NULL;
	}
	p->pos++; /* consume '}' */
	cdsl_json_value_t* obj = calloc(1, sizeof(*obj));
	obj->type = CDSL_JSON_OBJECT;
	obj->value.object.items = head;
	obj->value.object.count = count;
	return obj;
}

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
		skip_ws(p);
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
		} else {
			/* malformed value */
			break;
		}
		skip_ws(p);
		if (p->src[p->pos] == ',') {
			p->pos++;
		} else {
			/* continue to check for closing ] */
		}
	}
	/* if closing bracket missing, free items and return NULL */
	if (p->src[p->pos] != ']') {
		cdsl_json_value_t* it = head;
		while (it) {
			cdsl_json_value_t* next = it->next;
			cdsl_json_free(it);
			it = next;
		}
		return NULL;
	}
	p->pos++; /* consume ']' */
	cdsl_json_value_t* arr = calloc(1, sizeof(*arr));
	arr->type = CDSL_JSON_ARRAY;
	arr->value.array.items = head;
	arr->value.array.count = count;
	return arr;
}

static cdsl_json_value_t*
parse_value(json_parser_t* p)
{
	if (p->depth >= CDSL_JSON_MAX_DEPTH) {
		return NULL;
	}
	skip_ws(p);
	char c = p->src[p->pos];
	if (c == '{') {
		p->depth++;
		cdsl_json_value_t* v = parse_object(p);
		p->depth--;
		return v;
	}
	if (c == '[') {
		p->depth++;
		cdsl_json_value_t* v = parse_array(p);
		p->depth--;
		return v;
	}
	if (c == '"') {
		char* s = parse_string_raw(p);
		cdsl_json_value_t* v = calloc(1, sizeof(*v));
		if (!v) {
			free(s);
			return NULL;
		}
		v->type = CDSL_JSON_STRING;
		v->value.string_val = s;
		return v;
	}
	if (c == 't') {
		if (strncmp(p->src + p->pos, "true", 4) != 0) {
			return NULL;
		}
		cdsl_json_value_t* v = calloc(1, sizeof(*v));
		if (!v) {
			return NULL;
		}
		v->type = CDSL_JSON_BOOL;
		v->value.bool_val = 1;
		p->pos += 4;
		return v;
	}
	if (c == 'f') {
		if (strncmp(p->src + p->pos, "false", 5) != 0) {
			return NULL;
		}
		cdsl_json_value_t* v = calloc(1, sizeof(*v));
		if (!v) {
			return NULL;
		}
		v->type = CDSL_JSON_BOOL;
		v->value.bool_val = 0;
		p->pos += 5;
		return v;
	}
	if (c == 'n') {
		if (strncmp(p->src + p->pos, "null", 4) != 0) {
			return NULL;
		}
		cdsl_json_value_t* v = calloc(1, sizeof(*v));
		if (!v) {
			return NULL;
		}
		v->type = CDSL_JSON_NULL;
		p->pos += 4;
		return v;
	}
	if (c == '-' || isdigit((unsigned char)c)) {
		cdsl_json_value_t* v = calloc(1, sizeof(*v));
		if (!v) {
			return NULL;
		}
		v->type = CDSL_JSON_NUMBER;
		char* end;
		v->value.number_val = strtod(p->src + p->pos, &end);
		p->pos = (int)(end - p->src);
		return v;
	}
	return NULL;
}

cdsl_json_value_t*
cdsl_json_parse(const char* json)
{
	if (!json) {
		return NULL;
	}
	json_parser_t p = {.src = json, .pos = 0, .depth = 0};
	return parse_value(&p);
}

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
	case CDSL_JSON_STRING:
		free(val->value.string_val);
		break;
	case CDSL_JSON_OBJECT:
	case CDSL_JSON_ARRAY: {
		cdsl_json_value_t* item = val->type == CDSL_JSON_OBJECT ? val->value.object.items
									: val->value.array.items;
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

/* Helper implementations */
int
cdsl_json_object_length(const cdsl_json_value_t* obj)
{
	if (!obj || obj->type != CDSL_JSON_OBJECT) {
		return 0;
	}
	return obj->value.object.count;
}

int
cdsl_json_array_length(const cdsl_json_value_t* arr)
{
	if (!arr || arr->type != CDSL_JSON_ARRAY) {
		return 0;
	}
	return arr->value.array.count;
}

cdsl_json_value_t*
cdsl_json_get_object(const cdsl_json_value_t* obj, const char* key)
{
	if (!obj) {
		return NULL;
	}
	if (obj->type == CDSL_JSON_OBJECT) {
		cdsl_json_value_t* it = obj->value.object.items;
		while (it) {
			if (it->key && key && strcmp(it->key, key) == 0) {
				return it;
			}
			it = it->next;
		}
		return NULL;
	}
	/* If this node is a string/number/bool and key refers to itself, return it */
	return NULL;
}

cdsl_json_value_t*
cdsl_json_get_array(const cdsl_json_value_t* arr, int idx)
{
	if (!arr || arr->type != CDSL_JSON_ARRAY) {
		return NULL;
	}
	if (idx < 0) {
		return NULL;
	}
	cdsl_json_value_t* it = arr->value.array.items;
	int i = 0;
	while (it) {
		if (i == idx) {
			return it;
		}
		it = it->next;
		i++;
	}
	return NULL;
}

/** @} */
