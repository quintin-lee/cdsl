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
