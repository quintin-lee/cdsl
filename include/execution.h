/**
 * @file execution.h
 * @brief Execution layer: VM, context binding, and rule evaluation.
 *
 * Provides the runtime engine that evaluates DSL rules against a bound
 * context. Supports both simple pass/fail rules and multi-metric scoring
 * with threshold-based tri-state output (PASSED / PARTIALLY_PASSED / FAILED).
 *
 * @defgroup execution Execution Layer
 * @{
 */

#ifndef CDSL_EXECUTION_H
#define CDSL_EXECUTION_H

#include "ast.h"
#include "abstract.h"
#include "cdsl_hashmap.h"
#include <pthread.h>

/**
 * @brief Runtime value wrapper.
 *
 * Used to hold the result of expression evaluation or context variable values.
 */
typedef struct cdsl_value {
	cdsl_type_t type; /**< Value type discriminator */
	union {
		int int_val;	  /**< Integer value */
		double float_val; /**< Float value */
		int bool_val;	  /**< Boolean value */
		char* string_val; /**< String pointer (not owned) */
	} data;
} cdsl_value_t;

/**
 * @brief Context variable entry (linked list node).
 */
typedef struct cdsl_context_entry {
	char* name;			 /**< Variable name */
	cdsl_value_t value;		 /**< Current value */
	struct cdsl_context_entry* next; /**< Next entry */
} cdsl_context_entry_t;

/**
 * @brief Execution context holding all variable bindings.
 *
 * Bind variables to the context before executing rules. Supports
 * programmatic API (cdsl_context_set_*) or JSON loading (cdsl_context_load_json).
 *
 * @code
 * cdsl_context_t* ctx = cdsl_context_create(schema);
 * cdsl_context_set_int(ctx, "user.age", 25);
 * cdsl_context_set_string(ctx, "user.name", "Alice");
 * // or from JSON:
 * cdsl_context_load_json(ctx, "{\"user\":{\"age\":25,\"name\":\"Alice\"}}");
 * @endcode
 */
typedef struct cdsl_context {
	const cdsl_schema_t* schema;   /**< Associated schema */
	cdsl_context_entry_t* entries; /**< Variable bindings */
	cdsl_hashmap_t* map;	       /**< Fast lookup index */
} cdsl_context_t;

/**
 * @brief Action callback function type.
 *
 * @param action_name Name of the triggered action
 * @param args Linked list of argument expressions
 * @param user_data User-provided data pointer from the VM
 */
typedef void (*cdsl_action_cb_t)(const char* action_name, cdsl_arg_node_t* args, void* user_data);

/**
 * @brief Registered action callback entry.
 */
typedef struct cdsl_action_cb_entry {
	char* action_name;		   /**< Action name */
	cdsl_action_cb_t cb;		   /**< Callback function */
	struct cdsl_action_cb_entry* next; /**< Next entry */
} cdsl_action_cb_entry_t;

/**
 * @brief Custom function callback type.
 *
 * @param func_name Name of the function
 * @param args Linked list of argument expressions
 * @param user_data User-provided data pointer
 * @return Result value (caller owns the memory for string values)
 */
typedef cdsl_value_t (*cdsl_func_cb_t)(const char* func_name,
				       cdsl_arg_node_t* args,
				       void* user_data);

/**
 * @brief Registered function entry.
 */
typedef struct cdsl_func_entry {
	char* func_name;	      /**< Function name */
	cdsl_func_cb_t cb;	      /**< Callback function */
	struct cdsl_func_entry* next; /**< Next entry */
} cdsl_func_entry_t;

/**
 * @brief Execution statistics for performance monitoring.
 */
typedef struct {
	long total_executions;	      /**< Total rule executions */
	long total_rules_executed;    /**< Total rules evaluated */
	long total_metrics_evaluated; /**< Total metrics evaluated */
	long total_actions_triggered; /**< Total actions triggered */
	double total_time_us;	      /**< Total execution time in microseconds */
	double avg_time_us;	      /**< Average execution time per rule */
} cdsl_stats_t;

/**
 * @brief Virtual Machine for DSL rule execution.
 *
 * The VM holds registered action and function callbacks, and a reference
 * to the schema. Create one VM per execution thread.
 *
 * @code
 * cdsl_vm_t* vm = cdsl_vm_create(schema);
 * cdsl_vm_register_action(vm, "block", my_block_handler);
 * cdsl_vm_register_function(vm, "strlen", my_strlen);
 * cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
 * @endcode
 */
typedef struct cdsl_vm {
	const cdsl_schema_t* schema;	   /**< Schema reference */
	cdsl_action_cb_entry_t* callbacks; /**< Registered action callbacks */
	cdsl_func_entry_t* functions;	   /**< Registered function callbacks */
	void* user_data;		   /**< User data passed to callbacks */
	int debug_enabled;		   /**< 1 to enable trace output */
	cdsl_stats_t stats;		   /**< Execution statistics */
	int max_expr_depth; /**< Max expression nesting depth (default CDSL_MAX_EXPR_DEPTH) */
} cdsl_vm_t;

/**
 * @brief Rule execution status (tri-state).
 *
 * - CDSL_STATUS_PASSED: All criteria met, score >= pass_threshold
 * - CDSL_STATUS_PARTIALLY_PASSED: Between partial and pass thresholds
 * - CDSL_STATUS_FAILED: Score < partial threshold or critical item failed
 * - CDSL_STATUS_ERROR: Execution error
 */
typedef enum {
	CDSL_STATUS_PASSED,	      /**< Rule passed (score >= pass_threshold) */
	CDSL_STATUS_PARTIALLY_PASSED, /**< Partially passed (between thresholds) */
	CDSL_STATUS_FAILED,	      /**< Rule failed (below threshold or critical veto) */
	CDSL_STATUS_ERROR	      /**< Execution error */
} cdsl_rule_status_t;

/**
 * @brief Individual metric evaluation result.
 *
 * Contains the score obtained, whether the metric passed, and any
 * violation reason if it failed.
 */
typedef struct {
	char* metric_name;	 /**< Metric identifier */
	char* description;	 /**< Human-readable description */
	int max_weight;		 /**< Maximum possible score for this metric */
	int score_obtained;	 /**< Actual score obtained */
	int is_critical;	 /**< 1 if this is a critical (veto) metric */
	int is_passed;		 /**< 1 if score > 0 */
	char* matched_case_expr; /**< Expression of matched CASE (if any) */
	char* violation_reason;	 /**< Reason for failure (if failed) */
} cdsl_metric_result_t;

/**
 * @brief Complete rule evaluation report.
 *
 * Contains per-metric results, aggregate scores, and the final tri-state
 * decision. Returned by cdsl_vm_execute() and must be freed with
 * cdsl_report_free().
 */
typedef struct {
	char* rule_name;	       /**< Rule name */
	char* description;	       /**< Rule description from META */
	cdsl_metric_result_t* metrics; /**< Array of metric results */
	int metric_count;	       /**< Number of metrics */
	int total_max_score;	       /**< Sum of all metric weights */
	int total_obtained_score;      /**< Sum of all obtained scores */
	cdsl_rule_status_t status;     /**< Final tri-state decision */
	char* decision_summary;	       /**< Human-readable summary */
} cdsl_rule_report_t;

/** @name Context Management */
/** @{ */

/**
 * @brief Create an empty execution context.
 *
 * @param schema Associated schema used for type-safe variable lookups
 * @return Newly allocated context (must be freed with cdsl_context_free), or NULL on failure
 */
cdsl_context_t* cdsl_context_create(const cdsl_schema_t* schema);

/**
 * @brief Free an execution context and all its variable bindings.
 *
 * @param ctx Context to free (NULL-safe)
 */
void cdsl_context_free(cdsl_context_t* ctx);

/**
 * @brief Bind an integer variable to the context.
 *
 * @param ctx Target context
 * @param name Variable path (e.g. "user.age")
 * @param val Integer value to bind
 */
void cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val);

/**
 * @brief Bind a float variable to the context.
 *
 * @param ctx Target context
 * @param name Variable path (e.g. "item.price")
 * @param val Double value to bind
 */
void cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val);

/**
 * @brief Bind a boolean variable to the context.
 *
 * @param ctx Target context
 * @param name Variable path (e.g. "is_active")
 * @param val Boolean value (non-zero for true, zero for false)
 */
void cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val);

/**
 * @brief Bind a string variable to the context.
 *
 * @param ctx Target context
 * @param name Variable path (e.g. "user.role")
 * @param val String value (duplicated internally)
 */
void cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val);

/**
 * @brief Retrieve an integer variable from the context.
 *
 * @param ctx Source context
 * @param name Variable path
 * @param default_val Value to return if variable is missing or type mismatched
 * @return Current value or @p default_val
 */
int cdsl_context_get_int(const cdsl_context_t* ctx, const char* name, int default_val);

/**
 * @brief Retrieve a float variable from the context.
 *
 * @param ctx Source context
 * @param name Variable path
 * @param default_val Value to return if variable is missing or type mismatched
 * @return Current value or @p default_val
 */
double cdsl_context_get_float(const cdsl_context_t* ctx, const char* name, double default_val);

/**
 * @brief Retrieve a boolean variable from the context.
 *
 * @param ctx Source context
 * @param name Variable path
 * @param default_val Value to return if variable is missing or type mismatched
 * @return Current value (0 or 1) or @p default_val
 */
int cdsl_context_get_bool(const cdsl_context_t* ctx, const char* name, int default_val);

/**
 * @brief Retrieve a string variable from the context.
 *
 * @param ctx Source context
 * @param name Variable path
 * @param default_val Value to return if variable is missing or type mismatched
 * @return Internal string pointer (not owned by caller, valid until ctx is modified/freed) or @p default_val
 */
const char*
cdsl_context_get_string(const cdsl_context_t* ctx, const char* name, const char* default_val);

/**
 * @brief Remove a variable binding from the context.
 *
 * @param ctx Target context
 * @param name Variable path to remove
 * @return 1 if removed, 0 if not found
 */
int cdsl_context_remove(cdsl_context_t* ctx, const char* name);

/**
 * @brief Load context variables from a JSON string.
 *
 * Supports nested objects (flattened with dot notation) and types:
 * numbers (int/float), booleans, and strings.
 *
 * @param ctx Target context
 * @param json_str JSON string (e.g. `{"user":{"age":25}}`)
 * @return 1 on success, 0 on parse error
 */
int cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str);
/** @} */

/** @name Virtual Machine */
/** @{ */

/**
 * @brief Create a new DSL Virtual Machine.
 *
 * Each VM instance is associated with a schema and can hold its own
 * set of action and function callbacks. For thread-safe execution,
 * create one VM per thread.
 *
 * @param schema Associated schema (must remain valid for the lifetime of the VM)
 * @return Newly allocated VM (must be freed with cdsl_vm_free), or NULL on failure
 */
cdsl_vm_t* cdsl_vm_create(const cdsl_schema_t* schema);

/**
 * @brief Free a DSL Virtual Machine.
 *
 * @param vm VM to free (NULL-safe)
 */
void cdsl_vm_free(cdsl_vm_t* vm);

/**
 * @brief Register a callback for a DSL action.
 *
 * Actions are triggered by the @c DO command in DSL rules.
 *
 * @param vm Target VM
 * @param action_name Name of the action as defined in the schema
 * @param cb Callback function to invoke
 */
void cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb);

/**
 * @brief Register a custom function for use in DSL expressions.
 *
 * @param vm Target VM
 * @param func_name Name of the function to register
 * @param cb Callback function to invoke
 */
void cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb);

/**
 * @brief Enable or disable debug trace output to stdout.
 *
 * @param vm Target VM
 * @param enabled 1 to enable, 0 to disable
 */
void cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled);

/**
 * @brief Get the current maximum expression nesting depth.
 *
 * @param vm Source VM
 * @return Current depth limit
 */
int cdsl_vm_get_max_expr_depth(const cdsl_vm_t* vm);

/**
 * @brief Set the maximum expression nesting depth to prevent stack overflow.
 *
 * @param vm Target VM
 * @param depth New depth limit (default is CDSL_MAX_EXPR_DEPTH)
 */
void cdsl_vm_set_max_expr_depth(cdsl_vm_t* vm, int depth);

/**
 * @brief Retrieve execution statistics from the VM.
 *
 * @param vm Source VM
 * @return Internal stats pointer (valid until VM is freed)
 */
cdsl_stats_t* cdsl_vm_get_stats(const cdsl_vm_t* vm);

/**
 * @brief Reset all execution statistics in the VM to zero.
 *
 * @param vm Target VM
 */
void cdsl_vm_reset_stats(cdsl_vm_t* vm);
/** @} */

/** @name Rule Execution */
/** @{ */

/**
 * @brief Execute a single DSL rule.
 *
 * This is the main entry point for rule evaluation. It evaluates the rule's
 * logic against the provided context and triggers any associated actions.
 *
 * @param vm VM instance to use for execution
 * @param rule Parsed AST rule to execute
 * @param ctx Context containing variable bindings
 * @return Detailed execution report (must be freed with cdsl_report_free), or NULL on error
 */
cdsl_rule_report_t* cdsl_vm_execute(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx);

/**
 * @brief Free a rule evaluation report.
 *
 * @param report Report to free (NULL-safe)
 */
void cdsl_report_free(cdsl_rule_report_t* report);

/**
 * @brief Print a human-readable summary of a rule report to stdout.
 *
 * @param report Report to print
 */
void cdsl_report_print(const cdsl_rule_report_t* report);

/**
 * @brief Generate a JSON representation of a rule report.
 *
 * @param report Report to serialize
 * @return Newly allocated JSON string (must be freed by caller)
 */
char* cdsl_report_to_json(const cdsl_rule_report_t* report);

/**
 * @brief Handle to a compiled rule in the cache.
 */
typedef struct cdsl_compiled_rule {
	cdsl_rule_t* rule; /**< Parsed and verified AST */
	char* dsl_hash;	   /**< Hash of the original DSL source */
	int verified;	   /**< 1 if successfully verified against schema */
} cdsl_compiled_rule_t;

/**
 * @brief Thread-safe compilation cache.
 *
 * Stores compiled ASTs indexed by their DSL source hash to avoid
 * redundant parsing and verification.
 */
typedef struct cdsl_compile_cache {
	cdsl_hashmap_t* map;   /**< Internal hashmap storage */
	pthread_rwlock_t lock; /**< Reader-writer lock for concurrency */
} cdsl_compile_cache_t;

/**
 * @brief Compile a DSL rule string into an AST with caching.
...
 * This function handles parsing, verification, and caching. If the same DSL code
 * (by hash) is already in the cache, the cached AST is returned.
 *
 * @param cache Global compilation cache (thread-safe)
 * @param dsl_code Raw DSL rule string
 * @param schema Schema to verify against
 * @param[out] err_buf Buffer to store error messages on failure
 * @param err_buf_sz Size of @p err_buf
 * @return Compiled rule handle, or NULL on failure
 */
cdsl_compiled_rule_t* cdsl_compile(cdsl_compile_cache_t* cache,
				   const char* dsl_code,
				   const cdsl_schema_t* schema,
				   char* err_buf,
				   int err_buf_sz);

/**
 * @brief Execute a compiled rule handle.
 *
 * @param vm VM instance
 * @param compiled Compiled rule handle from cdsl_compile()
 * @param ctx Execution context
 * @return Execution report
 */
cdsl_rule_report_t*
cdsl_vm_execute_compiled(cdsl_vm_t* vm, cdsl_compiled_rule_t* compiled, cdsl_context_t* ctx);

/**
 * @brief Create a thread-safe compilation cache.
 *
 * @param capacity Initial capacity of the internal hashmap
 * @return Newly allocated cache (must be freed with cdsl_compile_cache_free), or NULL on failure
 */
cdsl_compile_cache_t* cdsl_compile_cache_create(int capacity);

/**
 * @brief Free a compilation cache and all its cached rule ASTs.
 *
 * @param cache Cache to free (NULL-safe)
 */
void cdsl_compile_cache_free(cdsl_compile_cache_t* cache);
/** @} */

/** @name Code Generation */
/** @{ */

/**
 * @brief Generate C code implementing a rule's logic.
 *
 * @param rule Rule AST to translate
 * @param schema Schema for variable and action lookups
 * @return Newly allocated C source string (must be freed by caller)
 */
char* cdsl_codegen_rule_to_c(const cdsl_rule_t* rule, const cdsl_schema_t* schema);

/**
 * @brief Generate C code for a rule and write it to a file.
 *
 * @param rule Rule AST to translate
 * @param schema Schema for variable and action lookups
 * @param filepath Path to the output C file
 * @return 1 on success, 0 on failure
 */
int
cdsl_codegen_to_file(const cdsl_rule_t* rule, const cdsl_schema_t* schema, const char* filepath);
/** @} */

/**
 * @brief RuleSet entry with priority ordering.
 */
typedef struct cdsl_ruleset_entry {
	cdsl_rule_t* rule;		 /**< Rule to execute */
	int priority;			 /**< Execution priority (lower = earlier) */
	struct cdsl_ruleset_entry* next; /**< Next entry (sorted by priority) */
} cdsl_ruleset_entry_t;

/**
 * @brief Collection of rules for batch execution.
 *
 * Rules are executed in priority order. The batch report aggregates
 * results from all rules including pass/fail counts and total scores.
 */
typedef struct cdsl_ruleset {
	cdsl_ruleset_entry_t* entries; /**< Sorted list of rules */
	int count;		       /**< Number of rules */
} cdsl_ruleset_t;

/**
 * @brief Batch execution report for a ruleset.
 *
 * Contains individual rule reports plus aggregate statistics.
 */
typedef struct {
	cdsl_rule_report_t** rule_reports; /**< Array of per-rule reports */
	int rule_count;			   /**< Total rules executed */
	int total_passed;		   /**< Rules with PASSED status */
	int total_partially;		   /**< Rules with PARTIALLY_PASSED status */
	int total_failed;		   /**< Rules with FAILED status */
	int total_error;		   /**< Rules with ERROR status */
	int aggregate_score;		   /**< Sum of all obtained scores */
	int aggregate_max;		   /**< Sum of all max scores */
	char* summary;			   /**< Human-readable summary */
} cdsl_ruleset_report_t;

/** @name RuleSet Management */
/** @{ */

/**
 * @brief Create an empty ruleset.
 *
 * @return Newly allocated ruleset (must be freed with cdsl_ruleset_free)
 */
cdsl_ruleset_t* cdsl_ruleset_create(void);

/**
 * @brief Free a ruleset and all its entries.
 *
 * @param set Ruleset to free (NULL-safe)
 */
void cdsl_ruleset_free(cdsl_ruleset_t* set);

/**
 * @brief Add a rule to the ruleset with a specific priority.
 *
 * @param set Target ruleset
 * @param rule Rule AST to add
 * @param priority Execution priority (lower values are executed first)
 */
void cdsl_ruleset_add(cdsl_ruleset_t* set, cdsl_rule_t* rule, int priority);

/**
 * @brief Remove a rule from the ruleset by its name.
 *
 * @param set Target ruleset
 * @param rule_name Name of the rule to remove
 * @return 1 if removed, 0 if not found
 */
int cdsl_ruleset_remove(cdsl_ruleset_t* set, const char* rule_name);

/**
 * @brief Execute all rules in a ruleset sequentially in priority order.
 *
 * @param vm VM instance
 * @param set Ruleset to execute
 * @param ctx Execution context
 * @return Aggregate ruleset report (must be freed with cdsl_ruleset_report_free)
 */
cdsl_ruleset_report_t*
cdsl_vm_execute_ruleset(cdsl_vm_t* vm, cdsl_ruleset_t* set, cdsl_context_t* ctx);

/**
 * @brief Execute all rules in a ruleset in parallel.
 *
 * Spawns multiple threads to evaluate rules concurrently. Each thread
 * uses its own internal VM cloned from the provided @p vm.
 *
 * @param vm Prototype VM instance
 * @param set Ruleset to execute
 * @param ctx Shared execution context (thread-safe for reads)
 * @param thread_count Number of threads to spawn
 * @return Aggregate ruleset report
 */
cdsl_ruleset_report_t* cdsl_vm_execute_ruleset_parallel(cdsl_vm_t* vm,
							cdsl_ruleset_t* set,
							cdsl_context_t* ctx,
							int thread_count);

/**
 * @brief Free a ruleset execution report.
 *
 * @param report Report to free (NULL-safe)
 */
void cdsl_ruleset_report_free(cdsl_ruleset_report_t* report);

/**
 * @brief Print a human-readable summary of a ruleset report to stdout.
 *
 * @param report Report to print
 */
void cdsl_ruleset_report_print(const cdsl_ruleset_report_t* report);

/**
 * @brief Load a rule from a file and add it to the ruleset.
 *
 * @param set Target ruleset
 * @param filepath Path to the DSL file
 * @param priority Execution priority
 * @param schema Schema to verify against
 * @param[out] err_buf Buffer for error messages
 * @param err_buf_sz Size of error buffer
 * @return 1 on success, 0 on failure
 */
int cdsl_ruleset_load_file(cdsl_ruleset_t* set,
			   const char* filepath,
			   int priority,
			   const cdsl_schema_t* schema,
			   char* err_buf,
			   int err_buf_sz);

/**
 * @brief Load a rule from a string and add it to the ruleset.
 *
 * @param set Target ruleset
 * @param dsl_code DSL rule string
 * @param priority Execution priority
 * @param schema Schema to verify against
 * @param[out] err_buf Buffer for error messages
 * @param err_buf_sz Size of error buffer
 * @return 1 on success, 0 on failure
 */
int cdsl_ruleset_load_string(cdsl_ruleset_t* set,
			     const char* dsl_code,
			     int priority,
			     const cdsl_schema_t* schema,
			     char* err_buf,
			     int err_buf_sz);

/**
 * @brief Reload a rule from a file (replaces existing rule with same name).
 *
 * @param set Target ruleset
 * @param rule_name Name of the rule to replace
 * @param filepath Path to the new DSL file
 * @param schema Schema to verify against
 * @param[out] err_buf Buffer for error messages
 * @param err_buf_sz Size of error buffer
 * @return 1 on success, 0 on failure
 */
int cdsl_ruleset_reload_file(cdsl_ruleset_t* set,
			     const char* rule_name,
			     const char* filepath,
			     const cdsl_schema_t* schema,
			     char* err_buf,
			     int err_buf_sz);

/**
 * @brief Validate cross-rule dependencies within a ruleset.
 *
 * @param set Ruleset to validate
 * @param[out] err_buf Buffer for error messages
 * @param err_buf_sz Size of error buffer
 * @return 1 if valid, 0 if invalid (e.g. circular dependencies)
 */
int cdsl_ruleset_validate_deps(const cdsl_ruleset_t* set, char* err_buf, int err_buf_sz);

/**
 * @brief Sort rules in the ruleset based on priority and dependencies.
 *
 * @param set Ruleset to sort
 * @return 1 on success, 0 if sorting failed (e.g. cycle detected)
 */
int cdsl_ruleset_topo_sort(cdsl_ruleset_t* set);
/** @} */

/** @name Visualization */
/** @{ */

/**
 * @brief Generate a Graphviz DOT representation of a single rule AST.
 *
 * @param rule Rule AST to visualize
 * @return Newly allocated DOT string (must be freed by caller), or NULL on failure
 */
char* cdsl_rule_to_dot(const cdsl_rule_t* rule);

/**
 * @brief Generate a DOT representation of a rule and write it to a file.
 *
 * @param rule Rule AST to visualize
 * @param filepath Path to the output .dot file
 * @return 1 on success, 0 on failure
 */
int cdsl_rule_to_dot_file(const cdsl_rule_t* rule, const char* filepath);

/**
 * @brief Generate a DOT representation of a ruleset (dependency and priority graph).
 *
 * @param set Ruleset to visualize
 * @return Newly allocated DOT string (must be freed by caller), or NULL on failure
 */
char* cdsl_ruleset_to_dot(const cdsl_ruleset_t* set);

/**
 * @brief Generate a DOT representation of a ruleset and write it to a file.
 *
 * @param set Ruleset to visualize
 * @param filepath Path to the output .dot file
 * @return 1 on success, 0 on failure
 */
int cdsl_ruleset_to_dot_file(const cdsl_ruleset_t* set, const char* filepath);
/** @} */

#endif
/** @} */
