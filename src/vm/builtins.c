/**
 * @file vm_builtins.c
 * @brief Built-in function implementations for the DSL VM.
 *
 * Provides standard library functions available in DSL expressions:
 * - strlen(str): Returns the length of a string
 * - contains(haystack, needle): Checks if a string contains a substring
 * - is_before(date1, date2): Compares ISO 8601 dates
 * - is_after(date1, date2): Compares ISO 8601 dates
 *
 * All built-in functions are auto-registered in every VM via
 * cdsl_vm_register_builtins(), called during cdsl_vm_create().
 *
 * @defgroup builtins Built-in Functions
 * @{
 */

#include "cdsl/execution.h"
#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

/**
 * @brief Built-in strlen: returns the length of a string.
 *
 * @param name Function name ("strlen")
 * @param args Argument list (expects 1 argument)
 * @param ctx Execution context (for evaluating the argument expression)
 * @param vm VM instance
 * @return Integer value with the string length
 */
static cdsl_value_t
builtin_strlen(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_INT, .data = {.int_val = 0}};
	if (!args || !args->expr) {
		return res;
	}

	cdsl_value_t v = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	if (v.type == CDSL_TYPE_STRING && v.data.string_val) {
		res.data.int_val = (int)strlen(v.data.string_val);
	} else if (v.type == CDSL_TYPE_INT) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%d", v.data.int_val);
		res.data.int_val = (int)strlen(buf);
	}
	return res;
}

/**
 * @brief built-in contains(haystack, needle)
 */
static cdsl_value_t
builtin_contains(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_BOOL, .data = {.bool_val = 0}};
	if (!args || !args->next || !args->expr || !args->next->expr) {
		return res;
	}

	cdsl_value_t haystack = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	cdsl_value_t needle = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);

	if (haystack.type == CDSL_TYPE_STRING && needle.type == CDSL_TYPE_STRING &&
	    haystack.data.string_val && needle.data.string_val) {
		res.data.bool_val =
		    (strstr(haystack.data.string_val, needle.data.string_val) != NULL);
	}
	return res;
}

/**
 * @brief Parse ISO 8601 date (YYYY-MM-DD) to time_t (internal).
 */
static time_t
parse_iso_date(const char* s)
{
	if (!s) {
		return (time_t)-1;
	}
	struct tm tm = {0};
	char* end = NULL;
	errno = 0;
	long y = strtol(s, &end, 10);
	if (end == s || *end != '-') {
		return (time_t)-1;
	}
	tm.tm_year = (int)y;
	s = end + 1;
	errno = 0;
	long m = strtol(s, &end, 10);
	if (end == s || *end != '-') {
		return (time_t)-1;
	}
	tm.tm_mon = (int)m;
	s = end + 1;
	errno = 0;
	long d = strtol(s, &end, 10);
	if (end == s) {
		return (time_t)-1;
	}
	tm.tm_mday = (int)d;
	/* If there's more content, try HH:MM:SS */
	if (*end == ' ') {
		s = end + 1;
		errno = 0;
		long hh = strtol(s, &end, 10);
		if (end != s && *end == ':') {
			tm.tm_hour = (int)hh;
			s = end + 1;
			errno = 0;
			long mm = strtol(s, &end, 10);
			if (end != s && *end == ':') {
				tm.tm_min = (int)mm;
				s = end + 1;
				errno = 0;
				long ss = strtol(s, &end, 10);
				if (end != s) {
					tm.tm_sec = (int)ss;
				}
			}
		}
	}
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	time_t result = mktime(&tm);
	return result != (time_t)-1 ? result : (time_t)-1;
}

/**
 * @brief built-in now(): returns current system time.
 */
static cdsl_value_t
builtin_now(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	(void)args;
	(void)ctx;
	(void)vm;
	cdsl_value_t res = {.type = CDSL_TYPE_DATE, .data = {.date_val = time(NULL)}};
	return res;
}

/**
 * @brief built-in days_between(d1, d2): returns integer difference in days.
 */
static cdsl_value_t
builtin_days_between(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_INT, .data = {.int_val = 0}};
	if (!args || !args->next || !args->expr || !args->next->expr) {
		return res;
	}

	cdsl_value_t v1 = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	cdsl_value_t v2 = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);

	time_t t1 = 0, t2 = 0;
	if (v1.type == CDSL_TYPE_DATE) {
		t1 = v1.data.date_val;
	} else if (v1.type == CDSL_TYPE_STRING) {
		t1 = parse_iso_date(v1.data.string_val);
	}

	if (v2.type == CDSL_TYPE_DATE) {
		t2 = v2.data.date_val;
	} else if (v2.type == CDSL_TYPE_STRING) {
		t2 = parse_iso_date(v2.data.string_val);
	}

	if (t1 != (time_t)-1 && t2 != (time_t)-1) {
		double diff = difftime(t1, t2);
		res.data.int_val = (int)(diff / (24 * 3600));
	}
	return res;
}

/**
 * @brief built-in is_before(t1, t2)
 */
static cdsl_value_t
builtin_is_before(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_BOOL, .data = {.bool_val = 0}};
	if (!args || !args->next || !args->expr || !args->next->expr) {
		return res;
	}

	cdsl_value_t t1_val = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	cdsl_value_t t2_val = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);

	/* Accept both DATE type and ISO-8601 STRING type */
	time_t t1 = (time_t)-1, t2 = (time_t)-1;
	if (t1_val.type == CDSL_TYPE_DATE) {
		t1 = t1_val.data.date_val;
	} else if (t1_val.type == CDSL_TYPE_STRING) {
		t1 = parse_iso_date(t1_val.data.string_val);
	}
	if (t2_val.type == CDSL_TYPE_DATE) {
		t2 = t2_val.data.date_val;
	} else if (t2_val.type == CDSL_TYPE_STRING) {
		t2 = parse_iso_date(t2_val.data.string_val);
	}

	if (t1 != (time_t)-1 && t2 != (time_t)-1) {
		res.data.bool_val = (t1 < t2);
	}
	return res;
}

/**
 * @brief built-in is_after(t1, t2)
 */
static cdsl_value_t
builtin_is_after(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_BOOL, .data = {.bool_val = 0}};
	if (!args || !args->next || !args->expr || !args->next->expr) {
		return res;
	}

	cdsl_value_t t1_val = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	cdsl_value_t t2_val = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);

	/* Accept both DATE type and ISO-8601 STRING type */
	time_t t1 = (time_t)-1, t2 = (time_t)-1;
	if (t1_val.type == CDSL_TYPE_DATE) {
		t1 = t1_val.data.date_val;
	} else if (t1_val.type == CDSL_TYPE_STRING) {
		t1 = parse_iso_date(t1_val.data.string_val);
	}
	if (t2_val.type == CDSL_TYPE_DATE) {
		t2 = t2_val.data.date_val;
	} else if (t2_val.type == CDSL_TYPE_STRING) {
		t2 = parse_iso_date(t2_val.data.string_val);
	}

	if (t1 != (time_t)-1 && t2 != (time_t)-1) {
		res.data.bool_val = (t1 > t2);
	}
	return res;
}

/**
 * @brief built-in uppercase(s): converts string to uppercase.
 */
static cdsl_value_t
builtin_uppercase(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_STRING, .data = {.string_val = NULL}};
	if (!args || !args->expr) {
		return res;
	}
	cdsl_value_t v = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	if (v.type != CDSL_TYPE_STRING || !v.data.string_val) {
		return res;
	}
	static THREAD_LOCAL char buf[4096];
	size_t len = strlen(v.data.string_val);
	if (len >= sizeof(buf)) {
		return res;
	}
	for (size_t i = 0; i < len; i++) {
		buf[i] = (char)toupper((unsigned char)v.data.string_val[i]);
	}
	buf[len] = '\0';
	res.data.string_val = buf;
	return res;
}

/**
 * @brief built-in lowercase(s): converts string to lowercase.
 */
static cdsl_value_t
builtin_lowercase(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_STRING, .data = {.string_val = NULL}};
	if (!args || !args->expr) {
		return res;
	}
	cdsl_value_t v = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	if (v.type != CDSL_TYPE_STRING || !v.data.string_val) {
		return res;
	}
	static THREAD_LOCAL char buf[4096];
	size_t len = strlen(v.data.string_val);
	if (len >= sizeof(buf)) {
		return res;
	}
	for (size_t i = 0; i < len; i++) {
		buf[i] = (char)tolower((unsigned char)v.data.string_val[i]);
	}
	buf[len] = '\0';
	res.data.string_val = buf;
	return res;
}

/**
 * @brief built-in trim(s): removes leading and trailing whitespace.
 */
static cdsl_value_t
builtin_trim(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_STRING, .data = {.string_val = NULL}};
	if (!args || !args->expr) {
		return res;
	}
	cdsl_value_t v = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	if (v.type != CDSL_TYPE_STRING || !v.data.string_val) {
		return res;
	}
	const char* s = v.data.string_val;
	while (*s && isspace((unsigned char)*s)) {
		s++;
	}
	if (*s == '\0') {
		static THREAD_LOCAL char empty[1];
		empty[0] = '\0';
		res.data.string_val = empty;
		return res;
	}
	size_t len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1])) {
		len--;
	}
	static THREAD_LOCAL char buf[4096];
	if (len >= sizeof(buf)) {
		return res;
	}
	memcpy(buf, s, len);
	buf[len] = '\0';
	res.data.string_val = buf;
	return res;
}

/**
 * @brief built-in startswith(s, prefix): checks if string starts with prefix.
 */
static cdsl_value_t
builtin_startswith(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_BOOL, .data = {.bool_val = 0}};
	if (!args || !args->next || !args->expr || !args->next->expr) {
		return res;
	}
	cdsl_value_t v1 = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	cdsl_value_t v2 = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);
	if (v1.type == CDSL_TYPE_STRING && v2.type == CDSL_TYPE_STRING && v1.data.string_val &&
	    v2.data.string_val) {
		res.data.bool_val =
		    (strncmp(v1.data.string_val, v2.data.string_val, strlen(v2.data.string_val)) ==
		     0);
	}
	return res;
}

/**
 * @brief built-in endswith(s, suffix): checks if string ends with suffix.
 */
static cdsl_value_t
builtin_endswith(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_BOOL, .data = {.bool_val = 0}};
	if (!args || !args->next || !args->expr || !args->next->expr) {
		return res;
	}
	cdsl_value_t v1 = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	cdsl_value_t v2 = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);
	if (v1.type == CDSL_TYPE_STRING && v2.type == CDSL_TYPE_STRING && v1.data.string_val &&
	    v2.data.string_val) {
		size_t len1 = strlen(v1.data.string_val);
		size_t len2 = strlen(v2.data.string_val);
		res.data.bool_val = (len1 >= len2 && strcmp(v1.data.string_val + len1 - len2,
							    v2.data.string_val) == 0);
	}
	return res;
}

/**
 * @brief built-in abs(n): returns the absolute value of an INT or FLOAT.
 */
static cdsl_value_t
builtin_abs(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_INT, .data = {.int_val = 0}};
	if (!args || !args->expr) {
		return res;
	}
	cdsl_value_t v = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	if (v.type == CDSL_TYPE_INT) {
		res.type = CDSL_TYPE_INT;
		res.data.int_val = abs(v.data.int_val);
	} else if (v.type == CDSL_TYPE_FLOAT) {
		res.type = CDSL_TYPE_FLOAT;
		res.data.float_val = fabs(v.data.float_val);
	}
	return res;
}

/**
 * @brief Shared numeric min/max core.
 */
static cdsl_value_t
builtin_minmax(
    const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm, int want_max)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_INT, .data = {.int_val = 0}};
	if (!args || !args->next || !args->expr || !args->next->expr) {
		return res;
	}
	cdsl_value_t a = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	cdsl_value_t b = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);
	int a_num = (a.type == CDSL_TYPE_INT || a.type == CDSL_TYPE_BOOL ||
		     a.type == CDSL_TYPE_FLOAT || a.type == CDSL_TYPE_LONG);
	int b_num = (b.type == CDSL_TYPE_INT || b.type == CDSL_TYPE_BOOL ||
		     b.type == CDSL_TYPE_FLOAT || b.type == CDSL_TYPE_LONG);
	if (!a_num || !b_num) {
		return res;
	}
	double av = (a.type == CDSL_TYPE_FLOAT)	 ? a.data.float_val
		    : (a.type == CDSL_TYPE_LONG) ? (double)a.data.long_val
		    : (a.type == CDSL_TYPE_BOOL) ? (double)a.data.bool_val
						 : (double)a.data.int_val;
	double bv = (b.type == CDSL_TYPE_FLOAT)	 ? b.data.float_val
		    : (b.type == CDSL_TYPE_LONG) ? (double)b.data.long_val
		    : (b.type == CDSL_TYPE_BOOL) ? (double)b.data.bool_val
						 : (double)b.data.int_val;
	int is_float = (a.type == CDSL_TYPE_FLOAT || b.type == CDSL_TYPE_FLOAT);
	double cmp_val = want_max ? ((av > bv) ? av : bv) : ((av < bv) ? av : bv);
	if (is_float) {
		res.type = CDSL_TYPE_FLOAT;
		res.data.float_val = cmp_val;
	} else {
		res.type = CDSL_TYPE_INT;
		res.data.int_val = (int)cmp_val;
	}
	return res;
}

static cdsl_value_t
builtin_min(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	return builtin_minmax(name, args, ctx, vm, 0);
}

static cdsl_value_t
builtin_max(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	return builtin_minmax(name, args, ctx, vm, 1);
}

/**
 * @brief built-in round(n): rounds a FLOAT to the nearest integer.
 */
static cdsl_value_t
builtin_round(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_INT, .data = {.int_val = 0}};
	if (!args || !args->expr) {
		return res;
	}
	cdsl_value_t v = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	if (v.type == CDSL_TYPE_FLOAT) {
		res.data.int_val = (int)round(v.data.float_val);
	} else if (v.type == CDSL_TYPE_INT) {
		res.data.int_val = v.data.int_val;
	}
	return res;
}

/**
 * @brief built-in typeof(expr): returns the type name as a string.
 */
static cdsl_value_t
builtin_typeof(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_STRING, .data = {.string_val = NULL}};
	if (!args || !args->expr) {
		return res;
	}
	cdsl_value_t v = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	switch (v.type) {
	case CDSL_TYPE_INT:
		res.data.string_val = "INT";
		break;
	case CDSL_TYPE_FLOAT:
		res.data.string_val = "FLOAT";
		break;
	case CDSL_TYPE_BOOL:
		res.data.string_val = "BOOL";
		break;
	case CDSL_TYPE_STRING:
		res.data.string_val = "STRING";
		break;
	case CDSL_TYPE_DATE:
		res.data.string_val = "DATE";
		break;
	case CDSL_TYPE_ARRAY:
		res.data.string_val = "ARRAY";
		break;
	default:
		res.data.string_val = "VOID";
		break;
	}
	if (v.type == CDSL_TYPE_ARRAY && v.data.array_val) {
		free(v.data.array_val->items);
		free(v.data.array_val);
	}
	return res;
}

/**
 * @brief built-in count(array): returns the number of elements in an array.
 */
static cdsl_value_t
builtin_count(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_INT, .data = {.int_val = 0}};
	if (!args || !args->expr) {
		return res;
	}
	cdsl_value_t v = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	if (v.type == CDSL_TYPE_ARRAY && v.data.array_val) {
		res.data.int_val = v.data.array_val->count;
		free(v.data.array_val->items);
		free(v.data.array_val);
	}
	return res;
}

/**
 * @brief built-in sum(array): returns the sum of all elements in an array of integers.
 */
static cdsl_value_t
builtin_sum(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_INT, .data = {.int_val = 0}};
	if (!args || !args->expr) {
		return res;
	}
	cdsl_value_t v = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	if (v.type == CDSL_TYPE_ARRAY && v.data.array_val) {
		int sum = 0;
		for (int i = 0; i < v.data.array_val->count; i++) {
			if (v.data.array_val->items[i].type == CDSL_TYPE_INT) {
				sum += v.data.array_val->items[i].data.int_val;
			}
		}
		res.data.int_val = sum;
		free(v.data.array_val->items);
		free(v.data.array_val);
	}
	return res;
}

/**
 * @brief built-in date_add(d, days): adds days to a date, returns a new date.
 */
static cdsl_value_t
builtin_date_add(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_DATE, .data = {.date_val = (time_t)-1}};
	if (!args || !args->next || !args->expr || !args->next->expr) {
		return res;
	}
	cdsl_value_t dv = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	cdsl_value_t nv = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);

	time_t t = (time_t)-1;
	if (dv.type == CDSL_TYPE_DATE) {
		t = dv.data.date_val;
	} else if (dv.type == CDSL_TYPE_STRING) {
		t = parse_iso_date(dv.data.string_val);
	}

	int days = 0;
	if (nv.type == CDSL_TYPE_INT) {
		days = nv.data.int_val;
	} else if (nv.type == CDSL_TYPE_FLOAT) {
		days = (int)nv.data.float_val;
	}

	if (t != (time_t)-1 && days != 0) {
		res.data.date_val = t + (time_t)(days * 86400);
	}
	return res;
}

void
cdsl_vm_register_builtins(cdsl_vm_t* vm)
{
	cdsl_vm_register_function(vm, "strlen", builtin_strlen);
	cdsl_vm_register_function(vm, "contains", builtin_contains);
	cdsl_vm_register_function(vm, "is_before", builtin_is_before);
	cdsl_vm_register_function(vm, "is_after", builtin_is_after);
	cdsl_vm_register_function(vm, "now", builtin_now);
	cdsl_vm_register_function(vm, "days_between", builtin_days_between);
	cdsl_vm_register_function(vm, "uppercase", builtin_uppercase);
	cdsl_vm_register_function(vm, "lowercase", builtin_lowercase);
	cdsl_vm_register_function(vm, "trim", builtin_trim);
	cdsl_vm_register_function(vm, "startswith", builtin_startswith);
	cdsl_vm_register_function(vm, "endswith", builtin_endswith);
	cdsl_vm_register_function(vm, "abs", builtin_abs);
	cdsl_vm_register_function(vm, "min", builtin_min);
	cdsl_vm_register_function(vm, "max", builtin_max);
	cdsl_vm_register_function(vm, "round", builtin_round);
	cdsl_vm_register_function(vm, "typeof", builtin_typeof);
	cdsl_vm_register_function(vm, "count", builtin_count);
	cdsl_vm_register_function(vm, "sum", builtin_sum);
	cdsl_vm_register_function(vm, "date_add", builtin_date_add);
}
/** @} */
