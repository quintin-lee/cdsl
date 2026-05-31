/**
 * @file cdsl/vm.h
 * @brief Virtual Machine: lifecycle, callbacks, execution control.
 *
 * @defgroup cdsl_vm Virtual Machine
 * @{
 */
#ifndef CDSL_VM_H
#define CDSL_VM_H

#include "cdsl/schema.h"
#include "cdsl/context.h"
#include "cdsl/ast.h"
#include <stdatomic.h>
#include <time.h>

struct cdsl_vm;
struct cdsl_context;

/**
 * @brief Action callback function type.
 */
typedef void (*cdsl_action_cb_t)(const char* action_name, cdsl_arg_node_t* args, void* user_data);

/**
 * @brief Registered action callback entry.
 */
typedef struct cdsl_action_cb_entry {
	char* action_name;
	cdsl_action_cb_t cb;
	struct cdsl_action_cb_entry* next;
} cdsl_action_cb_entry_t;

/**
 * @brief Custom function callback type.
 */
typedef cdsl_value_t (*cdsl_func_cb_t)(const char* func_name,
				       cdsl_arg_node_t* args,
				       cdsl_context_t* ctx,
				       struct cdsl_vm* vm);

/**
 * @brief Registered function entry.
 */
typedef struct cdsl_func_entry {
	char* func_name;
	cdsl_func_cb_t cb;
	struct cdsl_func_entry* next;
} cdsl_func_entry_t;

/**
 * @brief Execution statistics for performance monitoring.
 */
typedef struct {
	_Atomic long total_executions;
	_Atomic long total_rules_executed;
	_Atomic long total_metrics_evaluated;
	_Atomic long total_actions_triggered;
	double total_time_us;
	double avg_time_us;
} cdsl_stats_t;

/** Execution tracer types */

typedef enum {
	CDSL_TRACE_EXPR,   /**< Expression evaluated */
	CDSL_TRACE_METRIC, /**< Metric scoring decision */
	CDSL_TRACE_RULE,   /**< Rule decision (PASSED/FAILED) */
	CDSL_TRACE_ACTION  /**< Action triggered */
} cdsl_trace_kind_t;

typedef struct {
	cdsl_trace_kind_t kind;
	const char* rule_name;
	const char* detail;  /**< Expression text, metric name, or action name */
	cdsl_value_t value;  /**< Evaluated value (type+data) */
	int depth;	     /**< Expression nesting depth, or 0 */
	double timestamp_us; /**< Monotonic timestamp in microseconds */
} cdsl_trace_event_t;

typedef void (*cdsl_trace_cb_t)(const cdsl_trace_event_t* event, void* user_data);

/**
 * @brief Virtual Machine for DSL rule execution.
 */
typedef struct cdsl_vm {
	const cdsl_schema_t* schema;
	cdsl_action_cb_entry_t* callbacks;
	cdsl_func_entry_t* functions;
	void* user_data;
	int debug_enabled;
	cdsl_stats_t stats;
	int max_expr_depth;
	int64_t timeout_us;	     /**< Execution timeout in microseconds; 0=unlimited */
	int64_t memory_limit;	     /**< Max allocation bytes per execution; 0=unlimited */
	_Atomic int64_t alloc_bytes; /**< Current allocation counter (for limit enforcement) */
	_Atomic int error_state;     /**< Non-zero if execution was aborted (timeout/OOM) */
	cdsl_trace_cb_t trace_cb;    /**< Optional trace callback for debugging */
	void* trace_ud;		     /**< User data pointer for trace callback */
} cdsl_vm_t;

/** @name Execution quota control */
/** @{ */
void cdsl_vm_set_timeout(cdsl_vm_t* vm, int64_t timeout_us);
int64_t cdsl_vm_get_timeout(const cdsl_vm_t* vm);
void cdsl_vm_set_memory_limit(cdsl_vm_t* vm, int64_t limit_bytes);
int64_t cdsl_vm_get_memory_limit(const cdsl_vm_t* vm);
/** @} */

/**
 * @brief Set an execution trace callback.
 *
 * When set, the callback is invoked for each expression evaluation,
 * metric score decision, rule outcome, and action trigger during
 * cdsl_vm_execute(). Use for debugging, profiling, or visualization.
 *
 * @param vm VM instance
 * @param cb Callback function (NULL to disable tracing)
 * @param user_data Opaque pointer passed to each callback invocation
 */
void cdsl_vm_set_trace_callback(cdsl_vm_t* vm, cdsl_trace_cb_t cb, void* user_data);

[[nodiscard]]
cdsl_vm_t* cdsl_vm_create(const cdsl_schema_t* schema);
void cdsl_vm_free(cdsl_vm_t* vm);
void cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb);
void cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb);
void cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled);
int cdsl_vm_get_max_expr_depth(const cdsl_vm_t* vm);
void cdsl_vm_set_max_expr_depth(cdsl_vm_t* vm, int depth);
[[nodiscard]]
cdsl_stats_t* cdsl_vm_get_stats(const cdsl_vm_t* vm);
void cdsl_vm_reset_stats(cdsl_vm_t* vm);

#endif
/** @} */
