/**
 * @file vm.c
 * @brief Virtual Machine lifecycle, configuration, and sandboxing.
 *
 * Implements cdsl_vm_create/free, action/function registration,
 * debug mode, expression-depth limits, statistics, sandbox quotas
 * (timeout/memory/instruction), and trace callbacks.
 *
 * @defgroup cdsl_vm Virtual Machine Lifecycle
 * @{
 */

#include "cdsl/execution.h"
#include "cdsl/cse.h"
#include "internal.h"
#include <stdlib.h>

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
	vm->last_error = NULL;
	if (CDSL_RWLOCK_INIT(&vm->rwlock) != 0) {
		free(vm);
		return NULL;
	}
	cdsl_vm_register_builtins(vm);
	return vm;
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
	cdsl_error_free(vm->last_error);
	if (vm->pred_cache_owned && vm->pred_cache) {
		cdsl_pred_cache_free(vm->pred_cache);
	}
	CDSL_RWLOCK_DESTROY(&vm->rwlock);
	free(vm);
}

int
cdsl_vm_init_worker(cdsl_vm_t* wvm,
		    void* user_data,
		    int debug_enabled,
		    int max_expr_depth,
		    int64_t timeout_us,
		    int64_t memory_limit,
		    int64_t instruction_limit,
		    cdsl_trace_cb_t trace_cb,
		    void* trace_ud,
		    cdsl_action_cb_entry_t* cb_snapshot,
		    cdsl_func_entry_t* fn_snapshot)
{
	if (!wvm) {
		return 0;
	}
	memset(wvm, 0, sizeof(*wvm));
	if (CDSL_RWLOCK_INIT(&wvm->rwlock) != 0) {
		return 0;
	}
	wvm->schema = NULL;
	wvm->user_data = user_data;
	wvm->debug_enabled = debug_enabled;
	wvm->max_expr_depth = max_expr_depth > 0 ? max_expr_depth : CDSL_MAX_EXPR_DEPTH;
	wvm->timeout_us = timeout_us;
	wvm->memory_limit = memory_limit;
	wvm->instruction_limit = instruction_limit;
	wvm->trace_cb = trace_cb;
	wvm->trace_ud = trace_ud;
	wvm->callbacks = cb_snapshot;
	wvm->functions = fn_snapshot;
	return 1;
}

void
cdsl_vm_release_worker(cdsl_vm_t* wvm)
{
	if (!wvm) {
		return;
	}
	cdsl_error_free(wvm->last_error);
	wvm->last_error = NULL;
	CDSL_RWLOCK_DESTROY(&wvm->rwlock);
}

void
cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb)
{
	if (!vm || !action_name || !cb) {
		return;
	}
	CDSL_RWLOCK_WRLOCK(&vm->rwlock);
	cdsl_action_cb_entry_t* e = calloc(1, sizeof(*e));
	if (!e) {
		CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
		return;
	}
	e->action_name = cdsl_strdup(action_name);
	if (!e->action_name) {
		free(e);
		CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
		return;
	}
	e->cb = cb;
	e->next = vm->callbacks;
	vm->callbacks = e;
	CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
}

void
cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb)
{
	if (!vm || !func_name || !cb) {
		return;
	}
	CDSL_RWLOCK_WRLOCK(&vm->rwlock);
	cdsl_func_entry_t* e = calloc(1, sizeof(*e));
	if (!e) {
		CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
		return;
	}
	e->func_name = cdsl_strdup(func_name);
	if (!e->func_name) {
		free(e);
		CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
		return;
	}
	e->cb = cb;
	e->next = vm->functions;
	vm->functions = e;
	CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
}

void
cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled)
{
	if (vm) {
		vm->debug_enabled = enabled;
	}
}

void
cdsl_vm_set_pred_cache(cdsl_vm_t* vm, cdsl_pred_cache_t* cache)
{
	if (!vm) {
		return;
	}
	if (vm->pred_cache_owned && vm->pred_cache) {
		cdsl_pred_cache_free(vm->pred_cache);
	}
	vm->pred_cache = cache;
	vm->pred_cache_owned = 0;
}

cdsl_pred_cache_t*
cdsl_vm_get_pred_cache(const cdsl_vm_t* vm)
{
	return vm ? vm->pred_cache : NULL;
}

void
cdsl_vm_enable_pred_cache(cdsl_vm_t* vm, int enable)
{
	if (!vm) {
		return;
	}
	if (enable) {
		if (!vm->pred_cache) {
			vm->pred_cache = cdsl_pred_cache_create(256);
			vm->pred_cache_owned = 1;
		}
	} else {
		if (vm->pred_cache_owned && vm->pred_cache) {
			cdsl_pred_cache_free(vm->pred_cache);
		}
		vm->pred_cache = NULL;
		vm->pred_cache_owned = 0;
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
	s->total_executions = CDSL_ATOMIC_LOAD(&vm->stats.total_executions);
	s->total_rules_executed = CDSL_ATOMIC_LOAD(&vm->stats.total_rules_executed);
	s->total_metrics_evaluated = CDSL_ATOMIC_LOAD(&vm->stats.total_metrics_evaluated);
	s->total_actions_triggered = CDSL_ATOMIC_LOAD(&vm->stats.total_actions_triggered);
	s->total_time_us = vm->stats.total_time_us;
	s->avg_time_us = vm->stats.avg_time_us;
	if (s->total_executions > 0) {
		s->avg_time_us = s->total_time_us / (double)s->total_executions;
	}
	return s;
}

void
cdsl_vm_reset_stats(cdsl_vm_t* vm)
{
	if (vm) {
		CDSL_ATOMIC_STORE(&vm->stats.total_executions, 0);
		CDSL_ATOMIC_STORE(&vm->stats.total_rules_executed, 0);
		CDSL_ATOMIC_STORE(&vm->stats.total_metrics_evaluated, 0);
		CDSL_ATOMIC_STORE(&vm->stats.total_actions_triggered, 0);
		vm->stats.total_time_us = 0;
		vm->stats.avg_time_us = 0;
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
cdsl_vm_set_trace_callback(cdsl_vm_t* vm, cdsl_trace_cb_t cb, void* user_data)
{
	if (vm) {
		vm->trace_cb = cb;
		vm->trace_ud = user_data;
	}
}

/**
 * @brief Set the VM's last runtime error, replacing any previous one.
 *
 * Copies message and hint strings internally.  The VM owns the
 * resulting cdsl_error_t and frees it on cdsl_vm_free() or when
 * overwritten by a subsequent call.
 *
 * @param vm     VM instance
 * @param kind   Error kind (SYNTAX, TYPE, SEMANTIC, RUNTIME, WARNING)
 * @param line   Source line number (0 if unknown)
 * @param column Source column number (0 if unknown)
 * @param message Error message (copied internally)
 * @param hint   Optional hint (copied internally, may be NULL)
 */
void
cdsl_vm_set_last_error(cdsl_vm_t* vm,
		       cdsl_error_kind_t kind,
		       int line,
		       int column,
		       const char* message,
		       const char* hint)
{
	if (!vm) {
		return;
	}
	cdsl_error_free(vm->last_error);
	vm->last_error = cdsl_error_create(kind, line, column, message, hint);
}

const cdsl_error_t*
cdsl_vm_get_last_error(const cdsl_vm_t* vm)
{
	return vm ? vm->last_error : NULL;
}

void
cdsl_vm_begin_execution(cdsl_vm_t* vm)
{
	if (!vm) {
		return;
	}
	CDSL_ATOMIC_STORE(&vm->instruction_count, 0);
	CDSL_ATOMIC_STORE(&vm->alloc_bytes, 0);
	CDSL_ATOMIC_STORE(&vm->error_state, 0);
	cdsl_error_free(vm->last_error);
	vm->last_error = NULL;
}

/* ---- Context Handle ---- */

cdsl_ctx_t*
cdsl_ctx_create(const cdsl_schema_t* schema)
{
	cdsl_ctx_t* c = calloc(1, sizeof(*c));
	if (!c) {
		return NULL;
	}
	int owned_schema = 0;
	cdsl_schema_t* owned_schema_ptr = NULL;
	if (!schema) {
		schema = cdsl_schema_create();
		if (!schema) {
			free(c);
			return NULL;
		}
		owned_schema = 1;
		owned_schema_ptr = (cdsl_schema_t*)schema;
	}
	c->schema = schema;
	c->owned_schema = owned_schema;
	c->vm = cdsl_vm_create(schema);
	if (!c->vm) {
		if (owned_schema) {
			cdsl_schema_free(owned_schema_ptr);
		}
		free(c);
		return NULL;
	}
	c->ctx = cdsl_context_create(schema);
	if (!c->ctx) {
		cdsl_vm_free(c->vm);
		if (owned_schema) {
			cdsl_schema_free(owned_schema_ptr);
		}
		free(c);
		return NULL;
	}
	return c;
}

void
cdsl_ctx_free(cdsl_ctx_t* c)
{
	if (!c) {
		return;
	}
	if (c->ctx) {
		cdsl_context_free(c->ctx);
	}
	if (c->vm) {
		cdsl_vm_free(c->vm);
	}
	if (c->owned_schema && c->schema) {
		cdsl_schema_free((cdsl_schema_t*)c->schema);
	}
	free(c);
}

cdsl_vm_t*
cdsl_ctx_vm(cdsl_ctx_t* c)
{
	return c ? c->vm : NULL;
}

const cdsl_schema_t*
cdsl_ctx_schema(cdsl_ctx_t* c)
{
	return c ? c->schema : NULL;
}

cdsl_context_t*
cdsl_ctx_context(cdsl_ctx_t* c)
{
	return c ? c->ctx : NULL;
}

void
cdsl_ctx_register_action(cdsl_ctx_t* c, const char* name, cdsl_action_cb_t cb)
{
	if (c && c->vm) {
		cdsl_vm_register_action(c->vm, name, cb);
	}
}

void
cdsl_ctx_register_function(cdsl_ctx_t* c, const char* name, cdsl_func_cb_t cb)
{
	if (c && c->vm) {
		cdsl_vm_register_function(c->vm, name, cb);
	}
}

cdsl_rule_report_t*
cdsl_ctx_execute(cdsl_ctx_t* c, const cdsl_rule_t* rule)
{
	if (!c || !c->vm || !c->ctx || !rule) {
		return NULL;
	}
	return cdsl_vm_execute(c->vm, rule, c->ctx);
}

cdsl_ruleset_report_t*
cdsl_ctx_execute_ruleset(cdsl_ctx_t* c, cdsl_ruleset_t* set, int thread_count)
{
	if (!c || !c->vm || !c->ctx || !set) {
		return NULL;
	}
	return cdsl_vm_execute_ruleset_parallel(c->vm, set, c->ctx, thread_count);
}

/** @} */
