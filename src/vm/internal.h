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

#include "cdsl/util/portability.h"
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
 * Uses CLOCK_MONOTONIC for elapsed time measurements that are
 * immune to system clock adjustments.
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

int cdsl_vm_track_alloc(cdsl_vm_t* vm, size_t bytes);
void cdsl_vm_track_free(cdsl_vm_t* vm, size_t bytes);
int cdsl_vm_check_abort(cdsl_vm_t* vm, double start_time_us);

/**
 * @brief Metric boundary event enum.
 * @internal
 */
typedef enum {
	BC_EV_METRIC_START, /**< BC_METRIC_START instruction reached */
	BC_EV_SET_SCORE,    /**< BC_SET_SCORE instruction reached (value popped) */
	BC_EV_FAIL_METRIC,  /**< BC_FAIL_METRIC instruction reached */
	BC_EV_RULE_END	    /**< BC_RULE_END instruction reached */
} bc_metric_event_t;

/**
 * @brief Metric boundary event handler callback.
 *
 * Invoked by bc_run_instructions() at metric instruction boundaries.
 * The core loop performs stack operations (BC_SET_SCORE pops the score
 * value, BC_POP discards the condition). The handler receives event
 * notifications + constant-pool operands so it can update report state
 * (grow metrics array, set score, set violation reason, finalize).
 *
 * @param event      Metric boundary event enum
 * @param bc         Bytecode chunk (for constant-pool operand access)
 * @param inst       Current instruction (for reading operand.const_idx)
 * @param value      For BC_EV_SET_SCORE: the popped score value; for other
 *                   events: unused (set to VOID).
 * @param user_data  Opaque pointer to metric context (bc_metric_ctx_t*)
 * @return 1 to continue, 0 to abort the instruction loop
 */
typedef int (*bc_metric_handler_t)(bc_metric_event_t event,
				   const cdsl_bytecode_t* bc,
				   const bc_inst_t* inst,
				   const cdsl_value_t* value,
				   void* user_data);

/**
 * @brief Metric execution context owned by the caller (cdsl_bytecode_execute_rule).
 * @internal
 */
typedef struct bc_metric_ctx {
	cdsl_rule_report_t* report; /**< Report being built */
} bc_metric_ctx_t;

/**
 * @brief Core instruction execution loop shared by both executors.
 *
 * Runs the bytecode instruction loop (stack machine) with sandbox
 * checks (instruction limit, timeout/abort). Ordinary opcodes are
 * executed inline. Metric opcodes are dispatched to @p metric_handler
 * if non-NULL; otherwise they are skipped (ip++).
 *
 * @param vm            VM instance (for limits, abort check, function dispatch)
 * @param bc            Bytecode chunk to execute
 * @param ctx           Execution context (for variable lookups)
 * @param metric_handler Optional handler for metric opcodes (may be NULL)
 * @param user_data     Opaque pointer forwarded to metric_handler
 * @param out_result    Output evaluated value (only meaningful when metric_handler is NULL)
 * @return 1 on normal completion, 0 on abort/error
 */
int bc_run_instructions(cdsl_vm_t* vm,
			const cdsl_bytecode_t* bc,
			cdsl_context_t* ctx,
			bc_metric_handler_t metric_handler,
			void* user_data,
			cdsl_value_t* out_result);

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
void cdsl_vm_set_last_error(cdsl_vm_t* vm,
			    cdsl_error_kind_t kind,
			    int line,
			    int column,
			    const char* message,
			    const char* hint);

#endif
/** @} */
