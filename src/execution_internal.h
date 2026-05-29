#ifndef CDSL_EXECUTION_INTERNAL_H
#define CDSL_EXECUTION_INTERNAL_H

#include "execution.h"

/* Internal evaluation helpers shared across execution modules */

/**
 * @brief Recursively evaluate an expression against the context.
 */
cdsl_value_t cdsl_eval_expr_internal(
    cdsl_expr_node_t* expr, cdsl_context_t* ctx, cdsl_vm_t* vm, int debug, int depth);

/**
 * @brief Look up and invoke the registered callback for an action.
 */
void cdsl_trigger_action_internal(cdsl_vm_t* vm, cdsl_action_node_t* action);

/**
 * @brief Get a high-resolution timestamp in microseconds.
 */
double cdsl_get_time_us_internal(void);

/**
 * @brief Convert status enum to string.
 */
const char* cdsl_status_str_internal(cdsl_rule_status_t s);

/**
 * @brief Internal context lookup.
 */
cdsl_context_entry_t* cdsl_context_get_entry_internal(cdsl_context_t* ctx, const char* name);

/**
 * @brief Register all built-in functions in the VM.
 */
void cdsl_vm_register_builtins(cdsl_vm_t* vm);

#endif
