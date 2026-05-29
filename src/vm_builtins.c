#include "execution.h"
#include "execution_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/**
 * @brief built-in strlen(str)
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
		res.data.bool_val = (strstr(haystack.data.string_val, needle.data.string_val) != NULL);
	}
	return res;
}

/**
 * @brief Parse ISO 8601 date (YYYY-MM-DD) to time_t (internal).
 */
static time_t
parse_iso_date(const char* s)
{
	if (!s || strlen(s) < 10)
		return 0;
	struct tm tm = {0};
	if (sscanf(s, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) {
		return 0;
	}
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	return mktime(&tm);
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

	if (t1_val.type == CDSL_TYPE_STRING && t2_val.type == CDSL_TYPE_STRING) {
		time_t t1 = parse_iso_date(t1_val.data.string_val);
		time_t t2 = parse_iso_date(t2_val.data.string_val);
		if (t1 && t2) {
			res.data.bool_val = (t1 < t2);
		}
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

	if (t1_val.type == CDSL_TYPE_STRING && t2_val.type == CDSL_TYPE_STRING) {
		time_t t1 = parse_iso_date(t1_val.data.string_val);
		time_t t2 = parse_iso_date(t2_val.data.string_val);
		if (t1 && t2) {
			res.data.bool_val = (t1 > t2);
		}
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
}
