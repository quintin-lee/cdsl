/**
 * @file cdsl/vm.h
 * @brief Virtual Machine: lifecycle, callbacks, execution control.
 *
 * The VM holds registered action callbacks, custom functions, execution
 * statistics, sandboxing quotas, and an optional trace callback for
 * debugging.  One VM per thread.
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
 * @brief Action callback — invoked when a THEN action fires.
 * @param action_name  Name of the action (e.g. "block", "reject")
 * @param args         Linked list of argument expressions
 * @param user_data    Opaque pointer set via vm->user_data
 */
typedef void (*cdsl_action_cb_t)(const char* action_name, cdsl_arg_node_t* args, void* user_data);

/**
 * @brief Registered action callback entry (singly-linked list).
 */
typedef struct cdsl_action_cb_entry {
	char* action_name;		   /**< Action name the callback matches */
	cdsl_action_cb_t cb;		   /**< Callback function pointer */
	struct cdsl_action_cb_entry* next; /**< Next entry in list */
} cdsl_action_cb_entry_t;

/**
 * @brief Custom function callback — invoked for CALL expressions in DSL.
 * @param func_name  Name used in the DSL (e.g. "strlen", "my_abs")
 * @param args       Linked list of argument expressions
 * @param ctx        Execution context for variable lookups
 * @param vm         VM instance (for built-in dispatch)
 * @return           Evaluated result value
 */
typedef cdsl_value_t (*cdsl_func_cb_t)(const char* func_name,
				       cdsl_arg_node_t* args,
				       cdsl_context_t* ctx,
				       struct cdsl_vm* vm);

/**
 * @brief Registered function entry (singly-linked list).
 */
typedef struct cdsl_func_entry {
	char* func_name;	      /**< Function name used in DSL expressions */
	cdsl_func_cb_t cb;	      /**< Callback implementation */
	struct cdsl_func_entry* next; /**< Next entry in list */
} cdsl_func_entry_t;

/**
 * @brief Execution statistics for performance monitoring.
 *
 * Counters use _Atomic to support lock-free reads during parallel
 * rule execution.  Time values are in microseconds (monotonic clock).
 */
typedef struct {
	_Atomic long total_executions;	      /**< Total cdsl_vm_execute() calls */
	_Atomic long total_rules_executed;    /**< Individual rule evaluations */
	_Atomic long total_metrics_evaluated; /**< Metric CASE evaluations */
	_Atomic long total_actions_triggered; /**< THEN action invocations */
	double total_time_us;		      /**< Cumulative execution time (us) */
	double avg_time_us;		      /**< Average time per execution (us) */
} cdsl_stats_t;

/* ---- Execution tracer types ---- */

/** @brief Kinds of trace events emitted during rule execution. */
typedef enum {
	CDSL_TRACE_EXPR,   /**< An expression was evaluated (e.g. WHEN condition) */
	CDSL_TRACE_METRIC, /**< A metric CASE scored or failed */
	CDSL_TRACE_RULE,   /**< Rule outcome decided (PASSED / FAILED) */
	CDSL_TRACE_ACTION  /**< A THEN action was triggered */
} cdsl_trace_kind_t;

/**
 * @brief Single trace event passed to cdsl_trace_cb_t.
 *
 * String pointers (rule_name, detail) are borrowed and valid only
 * for the duration of the callback.
 */
typedef struct {
	cdsl_trace_kind_t kind; /**< What happened */
	const char* rule_name;	/**< Owning rule's name (may be NULL) */
	const char* detail;	/**< Expression text, metric name, or action name */
	cdsl_value_t value;	/**< Evaluated value (type + data union) */
	int depth;		/**< Expression nesting depth (0 for top-level) */
	double timestamp_us;	/**< Monotonic timestamp in microseconds */
} cdsl_trace_event_t;

/** @brief Trace callback — see cdsl_vm_set_trace_callback(). */
typedef void (*cdsl_trace_cb_t)(const cdsl_trace_event_t* event, void* user_data);

/* ---- Virtual Machine ---- */

/**
 * @brief Virtual Machine for DSL rule execution.
 *
 * Holds all per-thread execution state: registered actions, custom
 * functions, statistics, sandboxing quotas, and optional tracing.
 * Created with cdsl_vm_create(), destroyed with cdsl_vm_free().
 */
typedef struct cdsl_vm {
	const cdsl_schema_t* schema;	   /**< Schema for type resolution */
	cdsl_action_cb_entry_t* callbacks; /**< Action callback chain */
	cdsl_func_entry_t* functions;	   /**< Custom function chain */
	void* user_data;		   /**< Opaque pointer for action callbacks */
	int debug_enabled;		   /**< Non-zero enables stderr trace output */
	cdsl_stats_t stats;		   /**< Execution counters + timing */
	int max_expr_depth;		   /**< Max expression nesting (default 64) */

	/* ---- Sandboxing quotas ---- */
	int64_t timeout_us;	     /**< Per-execution timeout in us; 0=unlimited */
	int64_t memory_limit;	     /**< Per-execution allocation cap; 0=unlimited */
	_Atomic int64_t alloc_bytes; /**< Current allocation counter */
	_Atomic int error_state;     /**< Non-zero if execution was aborted */

	/* ---- Tracing ---- */
	cdsl_trace_cb_t trace_cb; /**< Optional trace callback (NULL=disabled) */
	void* trace_ud;		  /**< User data pointer for trace callback */
} cdsl_vm_t;

/* ---- Lifecycle ---- */

/**
 * @brief Create a VM instance.
 *
 * Registers all built-in functions (strlen, contains, now, …).
 * @param schema  Schema for type resolution (may be NULL)
 * @return        New VM (must be freed with cdsl_vm_free), or NULL on OOM
 */
[[nodiscard]]
cdsl_vm_t* cdsl_vm_create(const cdsl_schema_t* schema);

/**
 * @brief Free a VM and all registered callbacks/functions.
 * @param vm  VM to free (NULL-safe)
 */
void cdsl_vm_free(cdsl_vm_t* vm);

/* ---- Registration ---- */

/**
 * @brief Register an action callback.
 *
 * When a rule's THEN clause names @p action_name, the VM invokes @p cb.
 * Multiple callbacks can be registered for the same name (all are called).
 *
 * @param vm          Target VM
 * @param action_name Action name to match (duplicated internally)
 * @param cb          Callback function
 */
void cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb);

/**
 * @brief Register a custom function for use in DSL expressions.
 *
 * Functions are called via `func_name(arg1, arg2, ...)` syntax.
 * Overrides any previously registered function with the same name
 * (the new entry is prepended to the chain and matched first).
 *
 * @param vm        Target VM
 * @param func_name Function name in DSL expressions (duplicated internally)
 * @param cb        Callback implementation
 */
void cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb);

/* ---- Debug ---- */

/**
 * @brief Enable or disable stderr debug tracing.
 *
 * When enabled, the tree-walk evaluator prints every expression
 * evaluation to stderr with type and value information.
 *
 * @param vm      Target VM
 * @param enabled Non-zero to enable, zero to disable
 */
void cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled);

/* ---- Expression depth ---- */

/**
 * @brief Get the maximum expression nesting depth.
 * @param vm  VM (NULL returns default 64)
 * @return    Current max depth limit
 */
int cdsl_vm_get_max_expr_depth(const cdsl_vm_t* vm);

/**
 * @brief Set the maximum expression nesting depth.
 *
 * Protects against stack overflow from deeply nested expressions.
 * Depth is measured as AST node nesting (binary/unary/call chains).
 *
 * @param vm    Target VM
 * @param depth New limit (values <= 0 are ignored)
 */
void cdsl_vm_set_max_expr_depth(cdsl_vm_t* vm, int depth);

/* ---- Statistics ---- */

/**
 * @brief Get a snapshot of execution statistics.
 *
 * Atomically reads counters from the VM.  The returned struct is
 * heap-allocated; caller must free() it.
 *
 * @param vm  Target VM
 * @return    Heap-allocated stats snapshot, or NULL if vm is NULL
 */
[[nodiscard]]
cdsl_stats_t* cdsl_vm_get_stats(const cdsl_vm_t* vm);

/**
 * @brief Reset all execution statistics counters to zero.
 * @param vm  Target VM (NULL-safe)
 */
void cdsl_vm_reset_stats(cdsl_vm_t* vm);

/* ---- Sandboxing quotas ---- */

/**
 * @brief Set a per-execution time limit.
 *
 * When non-zero, cdsl_vm_execute() aborts if total execution time
 * exceeds @p timeout_us microseconds.  Aborted executions return
 * a CDSL_STATUS_ERROR report.
 *
 * @param vm         Target VM
 * @param timeout_us Timeout in microseconds (0 = unlimited)
 */
void cdsl_vm_set_timeout(cdsl_vm_t* vm, int64_t timeout_us);

/**
 * @brief Get the current execution timeout.
 * @param vm  VM (NULL returns 0)
 * @return    Timeout in microseconds, 0 if unlimited
 */
int64_t cdsl_vm_get_timeout(const cdsl_vm_t* vm);

/**
 * @brief Set a per-execution memory limit.
 *
 * When non-zero, allocations exceeding this cap cause execution
 * to abort with CDSL_STATUS_ERROR.
 *
 * @param vm          Target VM
 * @param limit_bytes Maximum bytes to allocate per execution (0 = unlimited)
 */
void cdsl_vm_set_memory_limit(cdsl_vm_t* vm, int64_t limit_bytes);

/**
 * @brief Get the current memory limit.
 * @param vm  VM (NULL returns 0)
 * @return    Limit in bytes, 0 if unlimited
 */
int64_t cdsl_vm_get_memory_limit(const cdsl_vm_t* vm);

/* ---- Tracing ---- */

/**
 * @brief Set an execution trace callback.
 *
 * When set, the callback is invoked for each expression evaluation,
 * metric score decision, rule outcome, and action trigger during
 * cdsl_vm_execute(). Use for debugging, profiling, or visualization.
 *
 * String pointers in cdsl_trace_event_t are borrowed — do not free
 * or retain them past the callback.
 *
 * @param vm        VM instance
 * @param cb        Callback function (NULL to disable tracing)
 * @param user_data Opaque pointer passed to each callback invocation
 */
void cdsl_vm_set_trace_callback(cdsl_vm_t* vm, cdsl_trace_cb_t cb, void* user_data);

#endif
/** @} */
