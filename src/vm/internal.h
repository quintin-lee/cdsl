/**
 * @file src/vm/internal.h
 * @brief Internal shared declarations for the execution module.
 *
 * Declares functions that are shared across the execution sub-modules
 * (vm_eval, vm_context, vm_builtins, vm_cache, vm_codegen, vm_ruleset,
 * vm_visualize) but are not part of the public API.
 *
 * These functions should not be called by user code directly.
 *
 * @defgroup execution_internal Execution Internal API
 * @{
 */

#ifndef CDSL_EXECUTION_INTERNAL_H
#define CDSL_EXECUTION_INTERNAL_H

#include "cdsl/context.h"
#include "cdsl/vm.h"
#include "cdsl/report.h"

/**
 * @brief Recursively evaluate an expression against the context.
 *
 * @param expr Expression node to evaluate
 * @param ctx Execution context for variable lookups
 * @param vm VM instance (for built-in function dispatch)
 * @param debug Enable debug trace output to stderr
 * @param depth Current recursion depth (for overflow protection)
 * @return Evaluated value (caller does NOT own string pointers)
 */
cdsl_value_t cdsl_eval_expr_internal(
    cdsl_expr_node_t* expr, cdsl_context_t* ctx, cdsl_vm_t* vm, int debug, int depth);

/**
 * @brief Look up and invoke the registered callback for an action.
 *
 * Searches the VM's callback list and invokes the first matching handler.
 *
 * @param vm VM instance containing registered callbacks
 * @param action Action node to trigger
 */
void cdsl_trigger_action_internal(cdsl_vm_t* vm, cdsl_action_node_t* action);

/**
 * @brief Get a high-resolution timestamp in microseconds.
 *
 * Uses CLOCK_MONOTONIC for elapsed time measurements.
 *
 * @return Current monotonic time in microseconds
 */
double cdsl_get_time_us_internal(void);

/**
 * @brief Convert a tri-state status enum to a string.
 *
 * @param s Status enum value
 * @return Static string representation ("PASSED", "FAILED", "ERROR")
 */
const char* cdsl_status_str_internal(cdsl_rule_status_t s);

/**
 * @brief Internal context variable lookup by name.
 *
 * @param ctx Execution context
 * @param name Variable path to find
 * @return Pointer to the context entry, or NULL if not found
 */
cdsl_context_entry_t* cdsl_context_get_entry_internal(cdsl_context_t* ctx, const char* name);

/**
 * @brief Register all built-in functions (strlen, contains, etc.) in a VM.
 *
 * Called automatically during cdsl_vm_create().
 *
 * @param vm VM instance to register built-ins into
 */
void cdsl_vm_register_builtins(cdsl_vm_t* vm);

#endif
/** @} */
