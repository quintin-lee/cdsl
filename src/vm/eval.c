/**
 * @file vm_eval.c
 * @brief Core expression evaluator and rule execution engine.
 *
 * Implements the C-DSL runtime: recursive expression evaluation,
 * simple rule (WHEN/THEN) execution, metric-based scoring rule
 * execution, tri-state reporting, and JSON report serialization.
 *
 * ### Execution flow
 *
 *     cdsl_vm_execute()
 *       ├── execute_metric_rule()   (rule->metrics != NULL)
 *       └── execute_simple_rule()   (rule->when_expr  != NULL)
 *            └── evaluate_condition()
 *                 ├── cdsl_bytecode_execute()  (fast path)
 *                 └── cdsl_eval_expr_internal() (tree-walk fallback)
 *
 * ### Type coercion in binary expressions
 *
 * Comparison: numerics promoted to double before comparison using
 * strict IEEE 754 equality (lv == rv).  For fuzzy comparison, use
 * the built-in approx_eq(a, b, [eps]) function.  Strings via `strcmp()`,
 * dates via `time_t` arithmetic.
 *
 * Arithmetic: INT/FLOAT/LONG/BOOL promoted to widest type (FLOAT >
 * LONG > INT).  Division by zero → CDSL_TYPE_VOID.
 *
 * @defgroup eval Expression Evaluator & Rule Execution
 * @{
 */

#include "cdsl/execution.h"
#include "cdsl/cse.h"
#include "internal.h"
#include "value_ops.h"
#include "cdsl/cache.h"
#include "cdsl/util/portability.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>

/**
 * @brief Parse a string to int with error detection.
 * @return int value, or default_val on parse failure.
 */
int
cdsl_safe_atoi(const char* str, int default_val)
{
	if (!str) {
		return default_val;
	}
	char* end = NULL;
	errno = 0;
	long val = strtol(str, &end, 10);
	if (errno != 0 || end == str || *end != '\0') {
		return default_val;
	}
	return (int)val;
}

int
cdsl_meta_get_int(cdsl_meta_item_t* list, const char* key, int default_val)
{
	char* val = cdsl_meta_get(list, key);
	return cdsl_safe_atoi(val, default_val);
}

int
cdsl_meta_get_bool(cdsl_meta_item_t* list, const char* key, int default_val)
{
	char* val = cdsl_meta_get(list, key);
	if (!val) {
		return default_val;
	}
	if (strcmp(val, "true") == 0) {
		return 1;
	}
	if (strcmp(val, "false") == 0) {
		return 0;
	}
	return default_val;
}

/**
 * @brief Fire a trace event if callback is registered.
 */
static void
fire_trace(cdsl_vm_t* vm,
	   cdsl_trace_kind_t kind,
	   const char* rule_name,
	   const char* detail,
	   cdsl_value_t val,
	   int depth)
{
	if (!vm || !vm->trace_cb) {
		return;
	}
	cdsl_trace_event_t ev = {
	    .kind = kind,
	    .rule_name = rule_name,
	    .detail = detail,
	    .value = val,
	    .depth = depth,
	    .timestamp_us = cdsl_get_time_us_internal(),
	};
	vm->trace_cb(&ev, vm->trace_ud);
}

/**
 * @brief Evaluate condition using bytecode if available, else tree-walk.
 */
static cdsl_value_t
evaluate_condition(cdsl_expr_node_t* expr,
		   cdsl_context_t* ctx,
		   cdsl_vm_t* vm,
		   const cdsl_bytecode_t* bc,
		   int debug)
{
	if (vm && vm->pred_cache && expr) {
		cdsl_value_t cached;
		if (cdsl_pred_cache_get(vm->pred_cache, expr, &cached)) {
			return cached;
		}
	}
	cdsl_value_t res;
	if (bc && bc->code) {
		(void)debug;
		res = cdsl_bytecode_execute(vm, bc, ctx);
	} else {
		res = cdsl_eval_expr_internal(expr, ctx, vm, debug, 0);
	}
	if (vm && vm->pred_cache && expr) {
		cdsl_pred_cache_put(vm->pred_cache, expr, &res);
	}
	return res;
}

static size_t
json_escape_len(const char* s)
{
	size_t len = 0;
	for (const char* p = s; *p; p++) {
		switch (*p) {
		case '"':
		case '\\':
		case '\n':
		case '\r':
		case '\t':
		case '\b':
		case '\f':
			len += 2;
			break;
		default:
			if ((unsigned char)*p < 0x20) {
				len += 6;
			} else {
				len++;
			}
			break;
		}
	}
	return len;
}

static size_t
json_escape_buf(char* dst, const char* src, size_t dst_cap)
{
	char* out = dst;
	char* end = dst + dst_cap;
	for (const char* p = src; *p; p++) {
		if (out >= end - 7) {
			break;
		}
		switch (*p) {
		case '"':
			*out++ = '\\';
			*out++ = '"';
			break;
		case '\\':
			*out++ = '\\';
			*out++ = '\\';
			break;
		case '\n':
			*out++ = '\\';
			*out++ = 'n';
			break;
		case '\r':
			*out++ = '\\';
			*out++ = 'r';
			break;
		case '\t':
			*out++ = '\\';
			*out++ = 't';
			break;
		case '\b':
			*out++ = '\\';
			*out++ = 'b';
			break;
		case '\f':
			*out++ = '\\';
			*out++ = 'f';
			break;
		default:
			if ((unsigned char)*p < 0x20) {
				int n = snprintf(out, 7, "\\u%04x", (unsigned char)*p);
				out += n > 0 ? n : 0;
			} else {
				*out++ = *p;
			}
			break;
		}
	}
	if (out < end) {
		*out = '\0';
	} else if (dst_cap > 0) {
		dst[dst_cap - 1] = '\0';
	}
	return (size_t)(out - dst);
}

/**
 * @brief Convert a tri-state rule status to a human-readable string.
 *
 * @param s Status enum value
 * @return Static string: "PASSED", "PARTIALLY PASSED", "FAILED", or "ERROR"
 */
const char*
cdsl_status_str_internal(cdsl_rule_status_t s)
{
	switch (s) {
	case CDSL_STATUS_PASSED:
		return "PASSED";
	case CDSL_STATUS_PARTIALLY_PASSED:
		return "PARTIALLY PASSED";
	case CDSL_STATUS_FAILED:
		return "FAILED";
	default:
		return "ERROR";
	}
}

cdsl_value_t
cdsl_eval_expr_internal(
    cdsl_expr_node_t* expr, cdsl_context_t* ctx, cdsl_vm_t* vm, int debug, int depth)
{
	cdsl_value_t result = {.type = CDSL_TYPE_VOID};
	if (!expr) {
		return result;
	}
	int limit = vm ? vm->max_expr_depth : CDSL_MAX_EXPR_DEPTH;
	if (depth >= limit) {
		if (debug) {
			fprintf(stderr, "[TRACE]   max expr depth (%d) exceeded\n", limit);
		}
		return result;
	}

	/* Instruction quota check for tree-walk evaluator */
	if (vm && vm->instruction_limit > 0) {
		int64_t count = CDSL_ATOMIC_FETCH_ADD(&vm->instruction_count, 1) + 1;
		if (count > vm->instruction_limit) {
			cdsl_vm_set_last_error(
			    vm, CDSL_ERR_RUNTIME, 0, 0, "Instruction quota exceeded", NULL);
			CDSL_ATOMIC_STORE(&vm->error_state, 1);
			return result;
		}
	}

	if (vm && vm->pred_cache &&
	    (expr->type == CDSL_EXPR_BINARY || expr->type == CDSL_EXPR_UNARY ||
	     expr->type == CDSL_EXPR_CALL)) {
		cdsl_value_t cached;
		if (cdsl_pred_cache_get(vm->pred_cache, expr, &cached)) {
			return cached;
		}
	}

	switch (expr->type) {
	case CDSL_EXPR_INT:
		result.type = CDSL_TYPE_INT;
		result.data.int_val = expr->data.int_val;
		if (debug) {
			fprintf(stderr, "[TRACE]   literal int: %d\n", expr->data.int_val);
		}
		break;
	case CDSL_EXPR_FLOAT:
		result.type = CDSL_TYPE_FLOAT;
		result.data.float_val = expr->data.float_val;
		if (debug) {
			fprintf(stderr, "[TRACE]   literal float: %.2f\n", expr->data.float_val);
		}
		break;
	case CDSL_EXPR_BOOL:
		result.type = CDSL_TYPE_BOOL;
		result.data.bool_val = expr->data.bool_val;
		if (debug) {
			fprintf(stderr,
				"[TRACE]   literal bool: %s\n",
				expr->data.bool_val ? "true" : "false");
		}
		break;
	case CDSL_EXPR_STRING:
		result.type = CDSL_TYPE_STRING;
		result.data.string_val = expr->data.string_val;
		if (debug) {
			fprintf(
			    stderr, "[TRACE]   literal string: \"%s\"\n", expr->data.string_val);
		}
		break;
	case CDSL_EXPR_DATE:
		result.type = CDSL_TYPE_DATE;
		result.data.date_val = expr->data.date_val;
		if (debug) {
			char buf[32];
			struct tm tm_buf;
			CDSL_LOCALTIME_R(&expr->data.date_val, &tm_buf);
			strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
			fprintf(stderr, "[TRACE]   literal date: %s\n", buf);
		}
		break;
	case CDSL_EXPR_LONG:
		result.type = CDSL_TYPE_LONG;
		result.data.long_val = expr->data.long_val;
		if (debug) {
			fprintf(stderr, "[TRACE]   literal long: %ld\n", (long)expr->data.long_val);
		}
		break;
	case CDSL_EXPR_ARRAY: {
		cdsl_arg_node_t* arg = expr->data.array.elements;
		int count = 0;
		while (arg) {
			count++;
			arg = arg->next;
		}
		cdsl_array_t* arr = cdsl_vm_calloc(vm, 1, sizeof(cdsl_array_t));
		if (!arr) {
			result.type = CDSL_TYPE_VOID;
			if (vm) {
				if (!vm->last_error) {
					cdsl_vm_set_last_error(vm,
							       CDSL_ERR_RESOURCE_LIMIT,
							       0,
							       0,
							       "Out of memory allocating array",
							       NULL);
				}
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
			}
			break;
		}
		arr->items =
		    cdsl_vm_calloc(vm, count > 0 ? (size_t)count : 1, sizeof(cdsl_value_t));
		if (!arr->items) {
			free(arr);
			result.type = CDSL_TYPE_VOID;
			if (vm) {
				if (!vm->last_error) {
					cdsl_vm_set_last_error(
					    vm,
					    CDSL_ERR_RESOURCE_LIMIT,
					    0,
					    0,
					    "Out of memory allocating array elements",
					    NULL);
				}
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
			}
			break;
		}
		arr->count = count;
		arr->capacity = count;
		arg = expr->data.array.elements;
		for (int i = 0; i < count; i++) {
			arr->items[i] =
			    cdsl_eval_expr_internal(arg->expr, ctx, vm, debug, depth + 1);
			arg = arg->next;
		}
		result.type = CDSL_TYPE_ARRAY;
		result.data.array_val = arr;
		if (debug) {
			fprintf(stderr, "[TRACE]   literal array: [%d elements]\n", count);
		}
		break;
	}
	case CDSL_EXPR_ID: {
		const cdsl_value_t* val_ptr = cdsl_context_lookup_value(ctx, expr->data.id_val);
		if (val_ptr) {
			if (debug) {
				fprintf(stderr, "[TRACE]   lookup: %s = ", expr->data.id_val);
				switch (val_ptr->type) {
				case CDSL_TYPE_INT:
					fprintf(stderr, "%d\n", val_ptr->data.int_val);
					break;
				case CDSL_TYPE_FLOAT:
					fprintf(stderr, "%.2f\n", val_ptr->data.float_val);
					break;
				case CDSL_TYPE_BOOL:
					fprintf(stderr,
						"%s\n",
						val_ptr->data.bool_val ? "true" : "false");
					break;
				case CDSL_TYPE_STRING:
					fprintf(stderr, "\"%s\"\n", val_ptr->data.string_val);
					break;
				case CDSL_TYPE_DATE: {
					char buf[32];
					struct tm tm_buf;
					CDSL_LOCALTIME_R(&val_ptr->data.date_val, &tm_buf);
					strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
					fprintf(stderr, "%s\n", buf);
					break;
				}
				case CDSL_TYPE_LONG:
					fprintf(stderr, "%ld\n", (long)val_ptr->data.long_val);
					break;
				default:
					fprintf(stderr, "?\n");
					break;
				}
			}
			cdsl_value_t v = *val_ptr;
			if (v.type == CDSL_TYPE_STRING && v.data.string_val) {
				v.data.string_val = cdsl_vm_strdup(vm, v.data.string_val);
				v.owned_string = 1;
				if (!v.data.string_val) {
					v.type = CDSL_TYPE_VOID;
					if (vm) {
						if (!vm->last_error) {
							cdsl_vm_set_last_error(
							    vm,
							    CDSL_ERR_RESOURCE_LIMIT,
							    0,
							    0,
							    "Out of memory duplicating string "
							    "variable",
							    NULL);
						}
						CDSL_ATOMIC_STORE(&vm->error_state, 1);
					}
					return v;
				}
			} else if (v.type == CDSL_TYPE_ARRAY || v.type == CDSL_TYPE_MAP) {
				return cdsl_value_copy(val_ptr);
			}
			return v;
		}
		if (debug) {
			fprintf(stderr, "[TRACE]   lookup: %s = NOT FOUND\n", expr->data.id_val);
		}
		break;
	}
	case CDSL_EXPR_MAP: {
		cdsl_map_t* m = cdsl_map_create(16);
		if (!m) {
			result.type = CDSL_TYPE_VOID;
			break;
		}
		for (cdsl_map_ast_entry_t* me = expr->data.map.entries; me; me = me->next) {
			cdsl_value_t item_val =
			    cdsl_eval_expr_internal(me->value, ctx, vm, debug, depth + 1);
			cdsl_map_set(m, me->key, &item_val);
			cdsl_value_free(&item_val);
		}
		result.type = CDSL_TYPE_MAP;
		result.data.map_val = m;
		break;
	}
	case CDSL_EXPR_SUBSCRIPT: {
		cdsl_value_t target =
		    cdsl_eval_expr_internal(expr->data.subscript.target, ctx, vm, debug, depth + 1);
		cdsl_value_t index =
		    cdsl_eval_expr_internal(expr->data.subscript.index, ctx, vm, debug, depth + 1);
		if (target.type == CDSL_TYPE_MAP && target.data.map_val) {
			const char* key =
			    (index.type == CDSL_TYPE_STRING) ? index.data.string_val : NULL;
			if (key) {
				const cdsl_value_t* found = cdsl_map_get(target.data.map_val, key);
				if (found) {
					result = cdsl_value_copy(found);
				}
			}
		} else if (target.type == CDSL_TYPE_ARRAY && target.data.array_val) {
			int idx = (index.type == CDSL_TYPE_INT)	   ? index.data.int_val
				  : (index.type == CDSL_TYPE_LONG) ? (int)index.data.long_val
								   : -1;
			if (idx >= 0 && idx < target.data.array_val->count) {
				result = cdsl_value_copy(&target.data.array_val->items[idx]);
			}
		}
		cdsl_value_free(&target);
		cdsl_value_free(&index);
		break;
	}
	case CDSL_EXPR_RANGE:
	case CDSL_EXPR_LAMBDA:
		result.type = CDSL_TYPE_VOID;
		break;
	case CDSL_EXPR_UNARY: {
		cdsl_value_t v =
		    cdsl_eval_expr_internal(expr->data.unary.expr, ctx, vm, debug, depth + 1);
		if (vm && CDSL_ATOMIC_LOAD(&vm->error_state)) {
			cdsl_value_free(&v);
			break;
		}
		cdsl_valop_status_t st = cdsl_value_unop(expr->data.unary.op, &v, &result);
		if (st != CDSL_VALOP_OK && st != CDSL_VALOP_VOID_OPERAND) {
			if (vm) {
				cdsl_vm_set_last_error(vm,
						       CDSL_ERR_RUNTIME,
						       expr->line,
						       expr->column,
						       cdsl_valop_status_message(st),
						       cdsl_valop_status_hint(st));
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
			}
			result.type = CDSL_TYPE_VOID;
		}
		if (debug) {
			if (expr->data.unary.op == CDSL_OP_NOT) {
				fprintf(stderr,
					"[TRACE]   NOT %s = %s\n",
					v.data.bool_val ? "true" : "false",
					result.data.bool_val ? "true" : "false");
			} else {
				fprintf(stderr, "[TRACE]   NEG\n");
			}
		}
		cdsl_value_free(&v);
		break;
	}
	case CDSL_EXPR_BINARY: {
		cdsl_op_t op = expr->data.binary.op;
		if (op == CDSL_OP_AND) {
			cdsl_value_t l = cdsl_eval_expr_internal(
			    expr->data.binary.left, ctx, vm, debug, depth + 1);
			if (vm && CDSL_ATOMIC_LOAD(&vm->error_state)) {
				cdsl_value_free(&l);
				break;
			}
			if (!l.data.bool_val) {
				result.type = CDSL_TYPE_BOOL;
				result.data.bool_val = 0;
				if (debug) {
					fprintf(stderr, "[TRACE]   AND short-circuit: false\n");
				}
				cdsl_value_free(&l);
				break;
			}
			cdsl_value_free(&l);
			cdsl_value_t r = cdsl_eval_expr_internal(
			    expr->data.binary.right, ctx, vm, debug, depth + 1);
			if (vm && CDSL_ATOMIC_LOAD(&vm->error_state)) {
				cdsl_value_free(&r);
				break;
			}
			result.type = CDSL_TYPE_BOOL;
			result.data.bool_val = r.data.bool_val;
			if (debug) {
				fprintf(stderr,
					"[TRACE]   AND: true AND %s = %s\n",
					r.data.bool_val ? "true" : "false",
					result.data.bool_val ? "true" : "false");
			}
			cdsl_value_free(&r);
			break;
		}
		if (op == CDSL_OP_OR) {
			cdsl_value_t l = cdsl_eval_expr_internal(
			    expr->data.binary.left, ctx, vm, debug, depth + 1);
			if (vm && CDSL_ATOMIC_LOAD(&vm->error_state)) {
				cdsl_value_free(&l);
				break;
			}
			if (l.data.bool_val) {
				result.type = CDSL_TYPE_BOOL;
				result.data.bool_val = 1;
				if (debug) {
					fprintf(stderr, "[TRACE]   OR short-circuit: true\n");
				}
				cdsl_value_free(&l);
				break;
			}
			cdsl_value_free(&l);
			cdsl_value_t r = cdsl_eval_expr_internal(
			    expr->data.binary.right, ctx, vm, debug, depth + 1);
			if (vm && CDSL_ATOMIC_LOAD(&vm->error_state)) {
				cdsl_value_free(&r);
				break;
			}
			result.type = CDSL_TYPE_BOOL;
			result.data.bool_val = r.data.bool_val;
			if (debug) {
				fprintf(stderr,
					"[TRACE]   OR: false OR %s = %s\n",
					r.data.bool_val ? "true" : "false",
					result.data.bool_val ? "true" : "false");
			}
			cdsl_value_free(&r);
			break;
		}
		cdsl_value_t l =
		    cdsl_eval_expr_internal(expr->data.binary.left, ctx, vm, debug, depth + 1);
		if (vm && CDSL_ATOMIC_LOAD(&vm->error_state)) {
			cdsl_value_free(&l);
			break;
		}
		if (op == CDSL_OP_IN && expr->data.binary.right->type == CDSL_EXPR_RANGE) {
			cdsl_expr_node_t* range_node = expr->data.binary.right;
			cdsl_value_t min_v = cdsl_eval_expr_internal(
			    range_node->data.range.min_expr, ctx, vm, debug, depth + 1);
			cdsl_value_t max_v = cdsl_eval_expr_internal(
			    range_node->data.range.max_expr, ctx, vm, debug, depth + 1);
			cdsl_value_t ge_res, le_res;
			cdsl_valop_status_t st1 = cdsl_value_binop(CDSL_OP_GE, &l, &min_v, &ge_res);
			cdsl_valop_status_t st2 = cdsl_value_binop(CDSL_OP_LE, &l, &max_v, &le_res);
			if (st1 == CDSL_VALOP_OK && st2 == CDSL_VALOP_OK) {
				result.type = CDSL_TYPE_BOOL;
				result.data.bool_val =
				    (ge_res.data.bool_val && le_res.data.bool_val);
			} else {
				result.type = CDSL_TYPE_BOOL;
				result.data.bool_val = 0;
			}
			cdsl_value_free(&l);
			cdsl_value_free(&min_v);
			cdsl_value_free(&max_v);
			break;
		}
		cdsl_value_t r =
		    cdsl_eval_expr_internal(expr->data.binary.right, ctx, vm, debug, depth + 1);
		if (vm && CDSL_ATOMIC_LOAD(&vm->error_state)) {
			cdsl_value_free(&l);
			cdsl_value_free(&r);
			break;
		}

		cdsl_valop_status_t st = cdsl_value_binop(op, &l, &r, &result);
		if (st != CDSL_VALOP_OK && st != CDSL_VALOP_VOID_OPERAND) {
			if (vm) {
				cdsl_vm_set_last_error(vm,
						       CDSL_ERR_RUNTIME,
						       expr->line,
						       expr->column,
						       cdsl_valop_status_message(st),
						       cdsl_valop_status_hint(st));
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
			}
			result.type = CDSL_TYPE_VOID;
		}
		cdsl_value_free(&l);
		cdsl_value_free(&r);
		break;
	}
	case CDSL_EXPR_CALL: {
		if (!vm) {
			break;
		}
		cdsl_func_cb_t target_fn = NULL;
		CDSL_RWLOCK_RDLOCK(&vm->rwlock);
		for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
			if (strcmp(fn->func_name, expr->data.call.func_name) == 0) {
				target_fn = fn->cb;
				break;
			}
		}
		CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);
		if (target_fn) {
			if (debug) {
				fprintf(stderr,
					"[TRACE]   calling function: %s()\n",
					expr->data.call.func_name);
			}
			result =
			    target_fn(expr->data.call.func_name, expr->data.call.args, ctx, vm);
			if (debug) {
				fprintf(stderr, "[TRACE]   function result: ");
				switch (result.type) {
				case CDSL_TYPE_INT:
					fprintf(stderr, "%d\n", result.data.int_val);
					break;
				case CDSL_TYPE_FLOAT:
					fprintf(stderr, "%.2f\n", result.data.float_val);
					break;
				case CDSL_TYPE_BOOL:
					fprintf(stderr,
						"%s\n",
						result.data.bool_val ? "true" : "false");
					break;
				case CDSL_TYPE_STRING:
					fprintf(stderr, "\"%s\"\n", result.data.string_val);
					break;
				default:
					fprintf(stderr, "void\n");
					break;
				}
			}
		}
		break;
	}
	case CDSL_EXPR_ERROR: {
		result.type = CDSL_TYPE_VOID;
		if (debug) {
			fprintf(stderr, "[TRACE]   error expression (parse recovery)\n");
		}
		break;
	}
	}
	if (vm && vm->pred_cache &&
	    (expr->type == CDSL_EXPR_BINARY || expr->type == CDSL_EXPR_UNARY ||
	     expr->type == CDSL_EXPR_CALL)) {
		cdsl_pred_cache_put(vm->pred_cache, expr, &result);
	}
	return result;
}

void
cdsl_trigger_action_internal(cdsl_vm_t* vm, cdsl_action_node_t* action)
{
	if (!vm || !action) {
		return;
	}
	cdsl_action_cb_t target_cb = NULL;
	CDSL_RWLOCK_RDLOCK(&vm->rwlock);
	for (cdsl_action_cb_entry_t* cb = vm->callbacks; cb; cb = cb->next) {
		if (strcmp(cb->action_name, action->action_name) == 0) {
			target_cb = cb->cb;
			break;
		}
	}
	CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);
	if (target_cb) {
		CDSL_ATOMIC_FETCH_ADD(&vm->stats.total_actions_triggered, 1);
		target_cb(action->action_name, action->args, vm->user_data);
	}
}

static cdsl_rule_report_t*
execute_metric_rule(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx)
{
	cdsl_rule_report_t* report = calloc(1, sizeof(*report));
	if (!report) {
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating rule report",
				       NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return NULL;
	}
	report->rule_name = rule->name ? cdsl_strdup(rule->name) : cdsl_strdup("");
	char* desc = cdsl_meta_get(rule->meta_list, "description");
	report->description = desc ? cdsl_strdup(desc) : cdsl_strdup("");
	if (!report->rule_name || !report->description) {
		cdsl_report_free(report);
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating rule report metadata",
				       NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return NULL;
	}

	int metric_count = 0;
	for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
		metric_count++;
	}
	report->metric_count = metric_count;
	report->metrics = calloc(metric_count, sizeof(cdsl_metric_result_t));
	if (!report->metrics && metric_count > 0) {
		cdsl_report_free(report);
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating metric array",
				       NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return NULL;
	}

	int total_max = 0, total_obtained = 0;
	int any_critical_failed = 0;

	int idx = 0;
	for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next, idx++) {
		if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
			break;
		}
		cdsl_metric_result_t* mr = &report->metrics[idx];
		mr->metric_name = cdsl_strdup(m->name);
		char* mdesc = cdsl_meta_get(m->meta_list, "description");
		mr->description = mdesc ? cdsl_strdup(mdesc) : cdsl_strdup("");
		if (!mr->metric_name || !mr->description) {
			cdsl_report_free(report);
			cdsl_vm_set_last_error(vm,
					       CDSL_ERR_RESOURCE_LIMIT,
					       0,
					       0,
					       "Out of memory allocating metric result metadata",
					       NULL);
			CDSL_ATOMIC_STORE(&vm->error_state, 1);
			return NULL;
		}
		mr->max_weight = cdsl_meta_get_int(m->meta_list, "weight", 0);
		mr->is_critical = cdsl_meta_get_bool(m->meta_list, "is_critical", 0);

		total_max += mr->max_weight;

		int matched = 0;
		for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
			if (vm->debug_enabled) {
				fprintf(stderr,
					"[TRACE] eval CASE condition for metric '%s'\n",
					m->name);
			}
			cdsl_value_t cond =
			    cdsl_eval_expr_internal(c->condition, ctx, vm, vm->debug_enabled, 0);
			if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
				cdsl_value_free(&cond);
				break;
			}
			if (cond.type == CDSL_TYPE_BOOL && cond.data.bool_val) {
				if (vm->debug_enabled) {
					fprintf(stderr, "[TRACE]   CASE matched\n");
				}
				cdsl_trigger_action_internal(vm, c->action);
				if (c->action && strcmp(c->action->action_name, "score") == 0 &&
				    c->action->args) {
					cdsl_value_t sv = cdsl_eval_expr_internal(
					    c->action->args->expr, ctx, vm, vm->debug_enabled, 0);
					mr->score_obtained =
					    (sv.type == CDSL_TYPE_INT) ? sv.data.int_val : 0;
					cdsl_value_free(&sv);
					if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
						cdsl_value_free(&cond);
						break;
					}
				} else if (c->action &&
					   strcmp(c->action->action_name, "fail_metric") == 0) {
					mr->score_obtained = 0;
					if (c->action->args && c->action->args->next) {
						cdsl_value_t rv = cdsl_eval_expr_internal(
						    c->action->args->next->expr,
						    ctx,
						    vm,
						    vm->debug_enabled,
						    0);
						if (!CDSL_ATOMIC_LOAD(&vm->error_state) &&
						    rv.type == CDSL_TYPE_STRING &&
						    rv.data.string_val) {
							mr->violation_reason =
							    cdsl_strdup(rv.data.string_val);
						}
						cdsl_value_free(&rv);
					}
				} else {
					mr->score_obtained = mr->max_weight;
				}
				if (vm->debug_enabled) {
					fprintf(stderr,
						"[TRACE]   score obtained: %d/%d\n",
						mr->score_obtained,
						mr->max_weight);
				}
				mr->is_passed = (mr->score_obtained > 0);
				matched = 1;
				cdsl_value_free(&cond);
				break;
			}
			cdsl_value_free(&cond);
		}
		if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
			break;
		}

		if (!matched) {
			if (vm->debug_enabled) {
				fprintf(stderr, "[TRACE]   no CASE matched, executing DEFAULT\n");
			}
			cdsl_trigger_action_internal(vm, m->default_action);
			if (!CDSL_ATOMIC_LOAD(&vm->error_state) && m->default_action &&
			    strcmp(m->default_action->action_name, "score") == 0 &&
			    m->default_action->args) {
				cdsl_value_t sv = cdsl_eval_expr_internal(
				    m->default_action->args->expr, ctx, vm, vm->debug_enabled, 0);
				mr->score_obtained =
				    (sv.type == CDSL_TYPE_INT) ? sv.data.int_val : 0;
				cdsl_value_free(&sv);
			} else if (!CDSL_ATOMIC_LOAD(&vm->error_state) && m->default_action &&
				   strcmp(m->default_action->action_name, "fail_metric") == 0) {
				mr->score_obtained = 0;
				if (m->default_action->args && m->default_action->args->next) {
					cdsl_value_t rv = cdsl_eval_expr_internal(
					    m->default_action->args->next->expr,
					    ctx,
					    vm,
					    vm->debug_enabled,
					    0);
					if (!CDSL_ATOMIC_LOAD(&vm->error_state) &&
					    rv.type == CDSL_TYPE_STRING && rv.data.string_val) {
						mr->violation_reason =
						    cdsl_strdup(rv.data.string_val);
					}
					cdsl_value_free(&rv);
				}
			} else {
				mr->score_obtained = 0;
			}
			mr->is_passed = (mr->score_obtained > 0);
			if (mr->violation_reason == NULL && !mr->is_passed) {
				mr->violation_reason = cdsl_strdup("default_condition_triggered");
			}
		}

		if (mr->is_critical && !mr->is_passed) {
			any_critical_failed = 1;
		}
		total_obtained += mr->score_obtained;
	}

	report->total_max_score = total_max;
	report->total_obtained_score = total_obtained;

	if (any_critical_failed) {
		report->status = CDSL_STATUS_FAILED;
	} else {
		int pass_thresh = cdsl_meta_get_int(rule->meta_list, "pass_threshold", 80);
		int partial_thresh = cdsl_meta_get_int(rule->meta_list, "partial_threshold", 60);

		if (total_obtained >= pass_thresh) {
			report->status = CDSL_STATUS_PASSED;
		} else if (total_obtained >= partial_thresh) {
			report->status = CDSL_STATUS_PARTIALLY_PASSED;
		} else {
			report->status = CDSL_STATUS_FAILED;
		}
	}

	char buf[512];
	switch (report->status) {
	case CDSL_STATUS_PASSED:
		snprintf(buf, sizeof(buf), "PASSED (score: %d/%d)", total_obtained, total_max);
		break;
	case CDSL_STATUS_PARTIALLY_PASSED:
		snprintf(buf,
			 sizeof(buf),
			 "PARTIALLY PASSED (score: %d/%d, needs improvement)",
			 total_obtained,
			 total_max);
		break;
	case CDSL_STATUS_FAILED:
		snprintf(buf,
			 sizeof(buf),
			 "FAILED (score: %d/%d, critical items or threshold not met)",
			 total_obtained,
			 total_max);
		break;
	default:
		snprintf(buf, sizeof(buf), "ERROR");
		break;
	}
	report->decision_summary = cdsl_strdup(buf);
	if (!report->decision_summary) {
		cdsl_report_free(report);
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating decision summary",
				       NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return NULL;
	}

	return report;
}

static cdsl_rule_report_t*
execute_simple_rule(cdsl_vm_t* vm,
		    const cdsl_rule_t* rule,
		    cdsl_context_t* ctx,
		    const cdsl_bytecode_t* bc)
{
	cdsl_rule_report_t* report = calloc(1, sizeof(*report));
	if (!report) {
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating rule report",
				       NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return NULL;
	}
	report->rule_name = rule->name ? cdsl_strdup(rule->name) : cdsl_strdup("");
	char* desc = cdsl_meta_get(rule->meta_list, "description");
	report->description = desc ? cdsl_strdup(desc) : cdsl_strdup("");
	if (!report->rule_name || !report->description) {
		cdsl_report_free(report);
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating rule report metadata",
				       NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return NULL;
	}
	report->metric_count = 1;
	report->metrics = calloc(1, sizeof(cdsl_metric_result_t));
	if (!report->metrics) {
		cdsl_report_free(report);
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating rule report metrics",
				       NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return NULL;
	}
	report->metrics[0].metric_name = rule->name ? cdsl_strdup(rule->name) : cdsl_strdup("");
	report->metrics[0].description =
	    report->description ? cdsl_strdup(report->description) : cdsl_strdup("");
	if (!report->metrics[0].metric_name || !report->metrics[0].description) {
		cdsl_report_free(report);
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating rule report metric metadata",
				       NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return NULL;
	}

	if (vm->debug_enabled) {
		fprintf(stderr, "[TRACE] Evaluating simple rule '%s'\n", rule->name);
	}
	/* Abort if timeout or OOM */
	if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
		report->status = CDSL_STATUS_ERROR;
		report->decision_summary = cdsl_strdup("ERROR");
		return report;
	}
	cdsl_value_t cond = evaluate_condition(rule->when_expr, ctx, vm, bc, vm->debug_enabled);
	if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
		cdsl_value_free(&cond);
		report->status = CDSL_STATUS_ERROR;
		report->decision_summary = cdsl_strdup("ERROR");
		return report;
	}
	fire_trace(vm, CDSL_TRACE_EXPR, rule->name, "WHEN", cond, 0);
	int triggered = (cond.type == CDSL_TYPE_BOOL) ? cond.data.bool_val : 0;
	cdsl_value_free(&cond);
	if (vm->debug_enabled) {
		fprintf(stderr, "[TRACE] WHEN result: %s\n", triggered ? "true" : "false");
	}

	if (triggered) {
		report->metrics[0].score_obtained = 0;
		report->metrics[0].max_weight = 100;
		report->metrics[0].is_passed = 0;
		report->status = CDSL_STATUS_FAILED;
		report->total_max_score = 100;
		report->total_obtained_score = 0;
		cdsl_trigger_action_internal(vm, rule->then_action);
		fire_trace(vm,
			   CDSL_TRACE_ACTION,
			   rule->name,
			   rule->then_action ? rule->then_action->action_name : "?",
			   (cdsl_value_t){.type = CDSL_TYPE_VOID},
			   0);
	} else {
		report->metrics[0].score_obtained = 100;
		report->metrics[0].max_weight = 100;
		report->metrics[0].is_passed = 1;
		report->status = CDSL_STATUS_PASSED;
		report->total_max_score = 100;
		report->total_obtained_score = 100;
	}
	fire_trace(vm,
		   CDSL_TRACE_RULE,
		   rule->name,
		   report->status == CDSL_STATUS_PASSED ? "PASSED" : "FAILED",
		   (cdsl_value_t){.type = CDSL_TYPE_VOID},
		   0);

	char buf[256];
	snprintf(buf,
		 sizeof(buf),
		 "%s (score: %d/100)",
		 triggered ? "FAILED" : "PASSED",
		 report->total_obtained_score);
	report->decision_summary = cdsl_strdup(buf);
	if (!report->decision_summary) {
		cdsl_report_free(report);
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating decision summary",
				       NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		return NULL;
	}
	return report;
}

cdsl_rule_report_t*
cdsl_vm_execute_compiled(cdsl_vm_t* vm, cdsl_compiled_rule_t* compiled, cdsl_context_t* ctx)
{
	if (!vm || !compiled || !compiled->rule || !ctx) {
		return NULL;
	}
	if (compiled->bc.code) {
		return cdsl_bytecode_execute_rule(vm, &compiled->bc, ctx);
	}
	return cdsl_vm_execute(vm, compiled->rule, ctx);
}

cdsl_rule_report_t*
cdsl_vm_execute(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx)
{
	if (!vm || !rule || !ctx) {
		return NULL;
	}
	double t0 = cdsl_get_time_us_internal();
	cdsl_rule_report_t* rpt;
	/* Reset per-execution sandbox counters and error state */
	cdsl_vm_begin_execution(vm);
	if (rule->metrics) {
		rpt = execute_metric_rule(vm, rule, ctx);
	} else {
		rpt = execute_simple_rule(vm, rule, ctx, NULL);
	}
	/* Check if aborted (instruction quota, timeout, etc.) */
	if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
		if (!rpt) {
			rpt = calloc(1, sizeof(cdsl_rule_report_t));
		}
		if (rpt) {
			rpt->status = CDSL_STATUS_ERROR;
			if (!rpt->rule_name && rule && rule->name) {
				rpt->rule_name = cdsl_strdup(rule->name);
			}
			if (!rpt->description && rule) {
				char* desc = cdsl_meta_get(rule->meta_list, "description");
				rpt->description = cdsl_strdup(desc ? desc : "");
			}
			if (!rpt->decision_summary) {
				rpt->decision_summary = cdsl_strdup("ERROR");
			}
			if (vm->last_error) {
				rpt->error = vm->last_error;
				vm->last_error = NULL;
			} else {
				rpt->error =
				    cdsl_error_create(CDSL_ERR_RESOURCE_LIMIT,
						      0,
						      0,
						      "Execution failed due to resource limits",
						      NULL);
			}
		}
	}
	double elapsed = cdsl_get_time_us_internal() - t0;
	/* Protect stats update against concurrent execution on the same VM.
	 * total_time_us is a plain double (not _Atomic), so concurrent
	 * updates without locking would be a data race. */
	CDSL_RWLOCK_WRLOCK(&vm->rwlock);
	vm->stats.total_executions++;
	vm->stats.total_rules_executed++;
	vm->stats.total_time_us += elapsed;
	if (rpt) {
		vm->stats.total_metrics_evaluated += rpt->metric_count;
	}
	CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
	return rpt;
}

void
cdsl_report_free(cdsl_rule_report_t* report)
{
	if (!report) {
		return;
	}
	free(report->rule_name);
	free(report->description);
	free(report->decision_summary);
	cdsl_error_free(report->error);
	if (report->metrics) {
		for (int i = 0; i < report->metric_count; i++) {
			cdsl_metric_result_t* m = &report->metrics[i];
			free(m->metric_name);
			free(m->description);
			free(m->matched_case_expr);
			free(m->violation_reason);
		}
		free(report->metrics);
	}
	free(report);
}

void
cdsl_report_print(const cdsl_rule_report_t* report)
{
	if (!report) {
		printf("No report.\n");
		return;
	}
	printf("\n========================================\n");
	printf("  AUDIT REPORT: %s\n", report->rule_name ? report->rule_name : "");
	printf("  %s\n", report->description ? report->description : "");
	printf("========================================\n");

	for (int i = 0; i < report->metric_count; i++) {
		cdsl_metric_result_t* m = &report->metrics[i];
		printf("  [%s] %s (weight: %d, score: %d/%d)%s\n",
		       m->is_passed ? "PASS" : "FAIL",
		       m->metric_name ? m->metric_name : "",
		       m->max_weight,
		       m->score_obtained,
		       m->max_weight,
		       m->is_critical ? " *" : "");
		if (m->violation_reason) {
			printf("         Reason: %s\n", m->violation_reason);
		}
	}

	printf("----------------------------------------\n");
	printf("  Status:   %s\n", cdsl_status_str_internal(report->status));
	printf("  Score:    %d / %d\n", report->total_obtained_score, report->total_max_score);
	printf("  Summary:  %s\n", report->decision_summary ? report->decision_summary : "");
	printf("========================================\n\n");
}

char*
cdsl_report_to_json(const cdsl_rule_report_t* report)
{
	if (!report) {
		return cdsl_strdup("{}");
	}

	size_t cap = 1024;
	char* json = malloc(cap);
	if (!json) {
		return NULL;
	}

	size_t off = 0;

	size_t rn_esc_len = json_escape_len(report->rule_name ? report->rule_name : "");
	size_t desc_esc_len = json_escape_len(report->description ? report->description : "");
	size_t ds_esc_len =
	    json_escape_len(report->decision_summary ? report->decision_summary : "");

	size_t needed = 256 + rn_esc_len + desc_esc_len + ds_esc_len;
	if (needed > cap) {
		cap = needed * 2;
		char* new_json = realloc(json, cap);
		if (!new_json) {
			free(json);
			return NULL;
		}
		json = new_json;
	}

#define JSON_CHECK_CAP(n)                                                                          \
	do {                                                                                       \
		if (off + (n) + 1 > cap) {                                                         \
			cap = (off + (n) + 1) * 2;                                                 \
			char* new_j = realloc(json, cap);                                          \
			if (!new_j) {                                                              \
				free(json);                                                        \
				return NULL;                                                       \
			}                                                                          \
			json = new_j;                                                              \
		}                                                                                  \
	} while (0)

	off += snprintf(json + off, cap - off, "{\"rule_name\":\"");
	off += json_escape_buf(json + off, report->rule_name ? report->rule_name : "", cap - off);
	off += snprintf(json + off, cap - off, "\",\"description\":\"");
	off +=
	    json_escape_buf(json + off, report->description ? report->description : "", cap - off);
	off += snprintf(json + off,
			cap - off,
			"\",\"status\":\"%s\",\"total_max_score\":%d,\"total_obtained_score\":%d,"
			"\"decision_summary\":\"",
			cdsl_status_str_internal(report->status),
			report->total_max_score,
			report->total_obtained_score);
	off += json_escape_buf(
	    json + off, report->decision_summary ? report->decision_summary : "", cap - off);
	off += snprintf(json + off, cap - off, "\",\"metrics\":[");

	for (int i = 0; i < report->metric_count; i++) {
		cdsl_metric_result_t* m = &report->metrics[i];
		size_t mn_esc = json_escape_len(m->metric_name ? m->metric_name : "");
		size_t md_esc = json_escape_len(m->description ? m->description : "");
		size_t vr_esc = m->violation_reason ? json_escape_len(m->violation_reason) : 0;

		JSON_CHECK_CAP(256 + mn_esc + md_esc + vr_esc);

		if (i > 0) {
			off += snprintf(json + off, cap - off, ",");
		}
		off += snprintf(json + off, cap - off, "{\"metric_name\":\"");
		off += json_escape_buf(json + off, m->metric_name ? m->metric_name : "", cap - off);
		off += snprintf(json + off, cap - off, "\",\"description\":\"");
		off += json_escape_buf(json + off, m->description ? m->description : "", cap - off);
		off += snprintf(json + off,
				cap - off,
				"\",\"max_weight\":%d,\"score_obtained\":%d,\"is_critical\":%d,"
				"\"is_passed\":%d",
				m->max_weight,
				m->score_obtained,
				m->is_critical,
				m->is_passed);
		if (m->violation_reason) {
			off += snprintf(json + off, cap - off, ",\"violation_reason\":\"");
			off += json_escape_buf(json + off, m->violation_reason, cap - off);
			off += snprintf(json + off, cap - off, "\"");
		}
		off += snprintf(json + off, cap - off, "}");
	}

	JSON_CHECK_CAP(4);
	snprintf(json + off, cap - off, "]}");

#undef JSON_CHECK_CAP

	return json;
}
/** @} */
