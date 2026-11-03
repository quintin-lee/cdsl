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

/** @} */
