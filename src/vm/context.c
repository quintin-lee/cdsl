/**
 * @file vm_context.c
 * @brief Execution context and VM lifecycle implementation.
 *
 * Implements the runtime context for variable bindings, the Virtual
 * Machine lifecycle (create/destroy), action and function registration,
 * and JSON-based context loading with automatic type inference.
 *
 * The context uses a dual-index structure: a linked list for iteration
 * and a hash map for O(1) variable lookups during expression evaluation.
 *
 * @defgroup context Execution Context & VM
 * @{
 */

#include "cdsl/execution.h"
#include "internal.h"
#include "cdsl/util/json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/**
 * @brief Get a high-resolution timestamp in microseconds.
 *
 * Uses CLOCK_MONOTONIC for elapsed time measurements that are
 * immune to system clock adjustments.
 *
 * @return Current timestamp in microseconds
 */
double
cdsl_get_time_us_internal(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

cdsl_context_entry_t*
cdsl_context_get_entry_internal(cdsl_context_t* ctx, const char* name)
{
	if (!ctx || !ctx->map || !name) {
		return NULL;
	}
	return (cdsl_context_entry_t*)cdsl_hashmap_get(ctx->map, name);
}

cdsl_context_t*
cdsl_context_create(const cdsl_schema_t* schema)
{
	cdsl_context_t* ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		return NULL;
	}
	ctx->schema = schema;
	ctx->map = cdsl_hashmap_create(64);
	if (!ctx->map) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

void
cdsl_value_free(cdsl_value_t* val)
{
	if (!val) {
		return;
	}
	if (val->type == CDSL_TYPE_STRING && val->data.string_val) {
		free(val->data.string_val);
		val->data.string_val = NULL;
	} else if (val->type == CDSL_TYPE_ARRAY && val->data.array_val) {
		cdsl_array_t* arr = val->data.array_val;
		for (int i = 0; i < arr->count; i++) {
			cdsl_value_free(&arr->items[i]);
		}
		free(arr->items);
		free(arr);
		val->data.array_val = NULL;
	}
}

void
cdsl_context_free(cdsl_context_t* ctx)
{
	if (!ctx) {
		return;
	}
	cdsl_hashmap_free(ctx->map, NULL);
	cdsl_context_entry_t* e = ctx->entries;
	while (e) {
		cdsl_context_entry_t* next = e->next;
		free(e->name);
		cdsl_value_free(&e->value);
		free(e);
		e = next;
	}
	free(ctx);
}

/**
 * @brief Check if a context variable is read-only per schema.
 */
static int
context_var_is_readonly(const cdsl_context_t* ctx, const char* name)
{
	if (!ctx || !ctx->schema || !name) {
		return 0;
	}
	cdsl_var_schema_t* vs = cdsl_hashmap_get(ctx->schema->var_map, name);
	return vs && vs->is_readonly;
}

/**
 * @brief Track memory allocation and enforce per-VM limit (internal).
 * @return 1 if allocation should proceed, 0 if limit exceeded
 */
int
cdsl_vm_track_alloc(cdsl_vm_t* vm, size_t bytes)
{
	if (!vm) {
		return 1;
	}
	if (vm->memory_limit <= 0) {
		return 1;
	}
	int64_t cur = atomic_fetch_add(&vm->alloc_bytes, (int64_t)bytes) + (int64_t)bytes;
	if (cur > vm->memory_limit) {
		vm->error_state = 1;
		return 0;
	}
	return 1;
}

/**
 * @brief Track memory deallocation (internal).
 */
void
cdsl_vm_track_free(cdsl_vm_t* vm, size_t bytes)
{
	if (!vm || !bytes) {
		return;
	}
	atomic_fetch_sub(&vm->alloc_bytes, (int64_t)bytes);
}

/**
 * @brief Check if execution has been aborted (timeout or OOM).
 */
int
cdsl_vm_check_abort(cdsl_vm_t* vm, double start_time_us)
{
	if (!vm) {
		return 0;
	}
	if (vm->error_state) {
		return 1;
	}
	if (vm->timeout_us > 0) {
		double elapsed = cdsl_get_time_us_internal() - start_time_us;
		if (elapsed > (double)vm->timeout_us) {
			vm->error_state = 1;
			return 1;
		}
	}
	return 0;
}

void
cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val)
{
	if (!ctx || !name) {
		return;
	}
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_INT;
		e->value.data.int_val = val;
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			return;
		}
		ne->name = strdup(name);
		if (!ne->name) {
			free(ne);
			return;
		}
		ne->value.type = CDSL_TYPE_INT;
		ne->value.data.int_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

void
cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_FLOAT;
		e->value.data.float_val = val;
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			return;
		}
		ne->name = strdup(name);
		if (!ne->name) {
			free(ne);
			return;
		}
		ne->value.type = CDSL_TYPE_FLOAT;
		ne->value.data.float_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

void
cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_BOOL;
		e->value.data.bool_val = val;
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			return;
		}
		ne->name = strdup(name);
		if (!ne->name) {
			free(ne);
			return;
		}
		ne->value.type = CDSL_TYPE_BOOL;
		ne->value.data.bool_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

void
cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_STRING;
		e->value.data.string_val = strdup(val);
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			return;
		}
		ne->name = strdup(name);
		if (!ne->name) {
			free(ne);
			return;
		}
		ne->value.type = CDSL_TYPE_STRING;
		ne->value.data.string_val = strdup(val);
		if (!ne->value.data.string_val) {
			free(ne->name);
			free(ne);
			return;
		}
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

void
cdsl_context_set_date(cdsl_context_t* ctx, const char* name, time_t val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_DATE;
		e->value.data.date_val = val;
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			return;
		}
		ne->name = strdup(name);
		if (!ne->name) {
			free(ne);
			return;
		}
		ne->value.type = CDSL_TYPE_DATE;
		ne->value.data.date_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

void
cdsl_context_set_long(cdsl_context_t* ctx, const char* name, int64_t val)
{
	cdsl_context_entry_t* e;
	if (!ctx || !name) {
		return;
	}
	e = cdsl_context_get_entry_internal(ctx, name);
	if (e && context_var_is_readonly(ctx, name)) {
		return;
	}
	if (e) {
		cdsl_value_free(&e->value);
		e->value.type = CDSL_TYPE_LONG;
		e->value.data.long_val = val;
	} else {
		cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
		if (!ne) {
			return;
		}
		ne->name = strdup(name);
		if (!ne->name) {
			free(ne);
			return;
		}
		ne->value.type = CDSL_TYPE_LONG;
		ne->value.data.long_val = val;
		ne->next = ctx->entries;
		ctx->entries = ne;
		cdsl_hashmap_put(ctx->map, name, ne);
	}
}

int
cdsl_context_get_int(const cdsl_context_t* ctx, const char* name, int default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_INT) {
			return e->value.data.int_val;
		}
		if (e->value.type == CDSL_TYPE_FLOAT) {
			return (int)e->value.data.float_val;
		}
		if (e->value.type == CDSL_TYPE_BOOL) {
			return e->value.data.bool_val;
		}
	}
	return default_val;
}

double
cdsl_context_get_float(const cdsl_context_t* ctx, const char* name, double default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_FLOAT) {
			return e->value.data.float_val;
		}
		if (e->value.type == CDSL_TYPE_INT) {
			return (double)e->value.data.int_val;
		}
		if (e->value.type == CDSL_TYPE_BOOL) {
			return (double)e->value.data.bool_val;
		}
	}
	return default_val;
}

int
cdsl_context_get_bool(const cdsl_context_t* ctx, const char* name, int default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e) {
		if (e->value.type == CDSL_TYPE_BOOL) {
			return e->value.data.bool_val;
		}
		if (e->value.type == CDSL_TYPE_INT) {
			return e->value.data.int_val != 0;
		}
		if (e->value.type == CDSL_TYPE_FLOAT) {
			return e->value.data.float_val != 0.0;
		}
	}
	return default_val;
}

const char*
cdsl_context_get_string(const cdsl_context_t* ctx, const char* name, const char* default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e && e->value.type == CDSL_TYPE_STRING) {
		return e->value.data.string_val;
	}
	return default_val;
}

time_t
cdsl_context_get_date(const cdsl_context_t* ctx, const char* name, time_t default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e && e->value.type == CDSL_TYPE_DATE) {
		return e->value.data.date_val;
	}
	return default_val;
}

int64_t
cdsl_context_get_long(const cdsl_context_t* ctx, const char* name, int64_t default_val)
{
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal((cdsl_context_t*)ctx, name);
	if (e && e->value.type == CDSL_TYPE_LONG) {
		return e->value.data.long_val;
	}
	if (e && e->value.type == CDSL_TYPE_INT) {
		return (int64_t)e->value.data.int_val;
	}
	return default_val;
}

int
cdsl_context_remove(cdsl_context_t* ctx, const char* name)
{
	if (!ctx || !name) {
		return 0;
	}
	cdsl_hashmap_remove(ctx->map, name, NULL);
	cdsl_context_entry_t* prev = NULL;
	for (cdsl_context_entry_t* e = ctx->entries; e; prev = e, e = e->next) {
		if (strcmp(e->name, name) == 0) {
			if (prev) {
				prev->next = e->next;
			} else {
				ctx->entries = e->next;
			}
			free(e->name);
			if (e->value.type == CDSL_TYPE_STRING) {
				free(e->value.data.string_val);
			}
			free(e);
			return 1;
		}
	}
	return 0;
}

static void
load_json_recursive(cdsl_context_t* ctx, cdsl_json_value_t* obj, const char* prefix)
{
	if (!obj || obj->type != CDSL_JSON_OBJECT) {
		return;
	}
	cdsl_json_value_t* child = obj->value.object.items;
	while (child) {
		char* key;
		if (prefix[0]) {
			size_t klen = strlen(prefix) + strlen(child->key) + 2;
			key = malloc(klen);
			if (!key) {
				return;
			}
			snprintf(key, klen, "%s.%s", prefix, child->key);
		} else {
			key = strdup(child->key);
			if (!key) {
				return;
			}
		}

		if (child->type == CDSL_JSON_OBJECT) {
			load_json_recursive(ctx, child, key);
		} else if (child->type == CDSL_JSON_NUMBER) {
			double v = child->value.number_val;
			if (v != (double)(int)v) {
				cdsl_context_set_float(ctx, key, v);
			} else {
				cdsl_context_set_int(ctx, key, (int)v);
			}
		} else if (child->type == CDSL_JSON_BOOL) {
			cdsl_context_set_bool(ctx, key, child->value.bool_val);
		} else if (child->type == CDSL_JSON_STRING) {
			cdsl_context_set_string(ctx, key, child->value.string_val);
		}
		free(key);
		child = child->next;
	}
}

int
cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str)
{
	cdsl_json_value_t* root = cdsl_json_parse(json_str);
	if (!root) {
		return 0;
	}
	load_json_recursive(ctx, root, "");
	cdsl_json_free(root);
	return 1;
}

cdsl_vm_t*
cdsl_vm_create(const cdsl_schema_t* schema)
{
	cdsl_vm_t* vm = calloc(1, sizeof(*vm));
	if (!vm) {
		return NULL;
	}
	vm->schema = schema;
	vm->debug_enabled = 0;
	vm->max_expr_depth = CDSL_MAX_EXPR_DEPTH;
	cdsl_vm_register_builtins(vm);
	return vm;
}

void
cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled)
{
	if (vm) {
		vm->debug_enabled = enabled;
	}
}

int
cdsl_vm_get_max_expr_depth(const cdsl_vm_t* vm)
{
	return vm ? vm->max_expr_depth : CDSL_MAX_EXPR_DEPTH;
}

void
cdsl_vm_set_max_expr_depth(cdsl_vm_t* vm, int depth)
{
	if (vm && depth > 0) {
		vm->max_expr_depth = depth;
	}
}

cdsl_stats_t*
cdsl_vm_get_stats(const cdsl_vm_t* vm)
{
	if (!vm) {
		return NULL;
	}
	cdsl_stats_t* s = malloc(sizeof(cdsl_stats_t));
	if (!s) {
		return NULL;
	}
	s->total_executions = atomic_load(&vm->stats.total_executions);
	s->total_rules_executed = atomic_load(&vm->stats.total_rules_executed);
	s->total_metrics_evaluated = atomic_load(&vm->stats.total_metrics_evaluated);
	s->total_actions_triggered = atomic_load(&vm->stats.total_actions_triggered);
	s->total_time_us = vm->stats.total_time_us;
	s->avg_time_us = vm->stats.avg_time_us;
	if (s->total_executions > 0) {
		s->avg_time_us = s->total_time_us / s->total_executions;
	}
	return s;
}

void
cdsl_vm_reset_stats(cdsl_vm_t* vm)
{
	if (vm) {
		atomic_store(&vm->stats.total_executions, 0);
		atomic_store(&vm->stats.total_rules_executed, 0);
		atomic_store(&vm->stats.total_metrics_evaluated, 0);
		atomic_store(&vm->stats.total_actions_triggered, 0);
		vm->stats.total_time_us = 0;
		vm->stats.avg_time_us = 0;
	}
}

void
cdsl_vm_set_trace_callback(cdsl_vm_t* vm, cdsl_trace_cb_t cb, void* user_data)
{
	if (vm) {
		vm->trace_cb = cb;
		vm->trace_ud = user_data;
	}
}

void
cdsl_vm_set_timeout(cdsl_vm_t* vm, int64_t timeout_us)
{
	if (vm) {
		vm->timeout_us = timeout_us;
	}
}

int64_t
cdsl_vm_get_timeout(const cdsl_vm_t* vm)
{
	return vm ? vm->timeout_us : 0;
}

void
cdsl_vm_set_memory_limit(cdsl_vm_t* vm, int64_t limit_bytes)
{
	if (vm) {
		vm->memory_limit = limit_bytes;
	}
}

int64_t
cdsl_vm_get_memory_limit(const cdsl_vm_t* vm)
{
	return vm ? vm->memory_limit : 0;
}

void
cdsl_vm_set_instruction_limit(cdsl_vm_t* vm, int64_t limit)
{
	if (vm) {
		vm->instruction_limit = limit;
	}
}

int64_t
cdsl_vm_get_instruction_limit(const cdsl_vm_t* vm)
{
	return vm ? vm->instruction_limit : 0;
}

void
cdsl_vm_free(cdsl_vm_t* vm)
{
	if (!vm) {
		return;
	}
	cdsl_action_cb_entry_t* cb = vm->callbacks;
	while (cb) {
		cdsl_action_cb_entry_t* next = cb->next;
		free(cb->action_name);
		free(cb);
		cb = next;
	}
	cdsl_func_entry_t* fn = vm->functions;
	while (fn) {
		cdsl_func_entry_t* next = fn->next;
		free(fn->func_name);
		free(fn);
		fn = next;
	}
	free(vm);
}

void
cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb)
{
	if (!vm || !action_name || !cb) {
		return;
	}
	cdsl_action_cb_entry_t* e = calloc(1, sizeof(*e));
	if (!e) {
		return;
	}
	e->action_name = strdup(action_name);
	if (!e->action_name) {
		free(e);
		return;
	}
	e->cb = cb;
	e->next = vm->callbacks;
	vm->callbacks = e;
}

void
cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb)
{
	if (!vm || !func_name || !cb) {
		return;
	}
	cdsl_func_entry_t* e = calloc(1, sizeof(*e));
	if (!e) {
		return;
	}
	e->func_name = strdup(func_name);
	if (!e->func_name) {
		free(e);
		return;
	}
	e->cb = cb;
	e->next = vm->functions;
	vm->functions = e;
}
/** @} */
