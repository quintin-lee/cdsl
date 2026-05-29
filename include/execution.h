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

/**
 * @brief Runtime value wrapper.
 *
 * Used to hold the result of expression evaluation or context variable values.
 */
typedef struct cdsl_value {
    cdsl_type_t type; /**< Value type discriminator */
    union {
        int int_val;       /**< Integer value */
        double float_val;  /**< Float value */
        int bool_val;      /**< Boolean value */
        char* string_val;  /**< String pointer (not owned) */
    } data;
} cdsl_value_t;

/**
 * @brief Context variable entry (linked list node).
 */
typedef struct cdsl_context_entry {
    char* name;            /**< Variable name */
    cdsl_value_t value;    /**< Current value */
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
    const cdsl_schema_t* schema;     /**< Associated schema */
    cdsl_context_entry_t* entries;   /**< Variable bindings */
} cdsl_context_t;

/**
 * @brief Action callback function type.
 *
 * @param action_name Name of the triggered action
 * @param args Linked list of argument expressions
 * @param user_data User-provided data pointer from the VM
 */
typedef void (*cdsl_action_cb_t)(const char* action_name, cdsl_arg_node_t* args,
                                  void* user_data);

/**
 * @brief Registered action callback entry.
 */
typedef struct cdsl_action_cb_entry {
    char* action_name;       /**< Action name */
    cdsl_action_cb_t cb;     /**< Callback function */
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
typedef cdsl_value_t (*cdsl_func_cb_t)(const char* func_name, cdsl_arg_node_t* args, void* user_data);

/**
 * @brief Registered function entry.
 */
typedef struct cdsl_func_entry {
    char* func_name;         /**< Function name */
    cdsl_func_cb_t cb;       /**< Callback function */
    struct cdsl_func_entry* next; /**< Next entry */
} cdsl_func_entry_t;

/**
 * @brief Execution statistics for performance monitoring.
 */
typedef struct {
    long total_executions;        /**< Total rule executions */
    long total_rules_executed;    /**< Total rules evaluated */
    long total_metrics_evaluated; /**< Total metrics evaluated */
    long total_actions_triggered; /**< Total actions triggered */
    double total_time_us;         /**< Total execution time in microseconds */
    double avg_time_us;           /**< Average execution time per rule */
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
    const cdsl_schema_t* schema;       /**< Schema reference */
    cdsl_action_cb_entry_t* callbacks; /**< Registered action callbacks */
    cdsl_func_entry_t* functions;      /**< Registered function callbacks */
    void* user_data;                   /**< User data passed to callbacks */
    int debug_enabled;                 /**< 1 to enable trace output */
    cdsl_stats_t stats;                /**< Execution statistics */
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
    CDSL_STATUS_PASSED,           /**< Rule passed (score >= pass_threshold) */
    CDSL_STATUS_PARTIALLY_PASSED, /**< Partially passed (between thresholds) */
    CDSL_STATUS_FAILED,           /**< Rule failed (below threshold or critical veto) */
    CDSL_STATUS_ERROR             /**< Execution error */
} cdsl_rule_status_t;

/**
 * @brief Individual metric evaluation result.
 *
 * Contains the score obtained, whether the metric passed, and any
 * violation reason if it failed.
 */
typedef struct {
    char* metric_name;        /**< Metric identifier */
    char* description;        /**< Human-readable description */
    int max_weight;           /**< Maximum possible score for this metric */
    int score_obtained;       /**< Actual score obtained */
    int is_critical;          /**< 1 if this is a critical (veto) metric */
    int is_passed;            /**< 1 if score > 0 */
    char* matched_case_expr;  /**< Expression of matched CASE (if any) */
    char* violation_reason;   /**< Reason for failure (if failed) */
} cdsl_metric_result_t;

/**
 * @brief Complete rule evaluation report.
 *
 * Contains per-metric results, aggregate scores, and the final tri-state
 * decision. Returned by cdsl_vm_execute() and must be freed with
 * cdsl_report_free().
 */
typedef struct {
    char* rule_name;                /**< Rule name */
    char* description;              /**< Rule description from META */
    cdsl_metric_result_t* metrics;  /**< Array of metric results */
    int metric_count;               /**< Number of metrics */
    int total_max_score;            /**< Sum of all metric weights */
    int total_obtained_score;       /**< Sum of all obtained scores */
    cdsl_rule_status_t status;      /**< Final tri-state decision */
    char* decision_summary;         /**< Human-readable summary */
} cdsl_rule_report_t;

/** @name Context Management */
/** @{ */
cdsl_context_t* cdsl_context_create(const cdsl_schema_t* schema);
void cdsl_context_free(cdsl_context_t* ctx);
void cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val);
void cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val);

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
cdsl_vm_t* cdsl_vm_create(const cdsl_schema_t* schema);
void cdsl_vm_free(cdsl_vm_t* vm);
void cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb);
void cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb);
void cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled);

cdsl_stats_t* cdsl_vm_get_stats(const cdsl_vm_t* vm);
void cdsl_vm_reset_stats(cdsl_vm_t* vm);
/** @} */

/** @name Rule Execution */
/** @{ */
cdsl_rule_report_t* cdsl_vm_execute(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx);
void cdsl_report_free(cdsl_rule_report_t* report);
void cdsl_report_print(const cdsl_rule_report_t* report);
char* cdsl_report_to_json(const cdsl_rule_report_t* report);

typedef struct cdsl_compiled_rule {
    cdsl_rule_t* rule;
    char* dsl_hash;
    int verified;
} cdsl_compiled_rule_t;

typedef struct cdsl_compile_cache {
    cdsl_compiled_rule_t** entries;
    int count;
    int capacity;
} cdsl_compile_cache_t;

cdsl_compile_cache_t* cdsl_compile_cache_create(int capacity);
void cdsl_compile_cache_free(cdsl_compile_cache_t* cache);
cdsl_compiled_rule_t* cdsl_compile(cdsl_compile_cache_t* cache, const char* dsl_code,
                                    const cdsl_schema_t* schema, char* err_buf, int err_buf_sz);
cdsl_rule_report_t* cdsl_vm_execute_compiled(cdsl_vm_t* vm, cdsl_compiled_rule_t* compiled,
                                              cdsl_context_t* ctx);
/** @} */

/** @name Code Generation */
/** @{ */
char* cdsl_codegen_rule_to_c(const cdsl_rule_t* rule, const cdsl_schema_t* schema);
int cdsl_codegen_to_file(const cdsl_rule_t* rule, const cdsl_schema_t* schema, const char* filepath);
/** @} */

/**
 * @brief RuleSet entry with priority ordering.
 */
typedef struct cdsl_ruleset_entry {
    cdsl_rule_t* rule;           /**< Rule to execute */
    int priority;                /**< Execution priority (lower = earlier) */
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
    int count;                     /**< Number of rules */
} cdsl_ruleset_t;

/**
 * @brief Batch execution report for a ruleset.
 *
 * Contains individual rule reports plus aggregate statistics.
 */
typedef struct {
    cdsl_rule_report_t** rule_reports; /**< Array of per-rule reports */
    int rule_count;                    /**< Total rules executed */
    int total_passed;                  /**< Rules with PASSED status */
    int total_partially;               /**< Rules with PARTIALLY_PASSED status */
    int total_failed;                  /**< Rules with FAILED status */
    int total_error;                   /**< Rules with ERROR status */
    int aggregate_score;               /**< Sum of all obtained scores */
    int aggregate_max;                 /**< Sum of all max scores */
    char* summary;                     /**< Human-readable summary */
} cdsl_ruleset_report_t;

/** @name RuleSet Management */
/** @{ */
cdsl_ruleset_t* cdsl_ruleset_create(void);
void cdsl_ruleset_free(cdsl_ruleset_t* set);
void cdsl_ruleset_add(cdsl_ruleset_t* set, cdsl_rule_t* rule, int priority);
int cdsl_ruleset_remove(cdsl_ruleset_t* set, const char* rule_name);
cdsl_ruleset_report_t* cdsl_vm_execute_ruleset(cdsl_vm_t* vm, cdsl_ruleset_t* set, cdsl_context_t* ctx);
cdsl_ruleset_report_t* cdsl_vm_execute_ruleset_parallel(cdsl_vm_t* vm, cdsl_ruleset_t* set,
                                                          cdsl_context_t* ctx, int thread_count);
void cdsl_ruleset_report_free(cdsl_ruleset_report_t* report);
void cdsl_ruleset_report_print(const cdsl_ruleset_report_t* report);
int cdsl_ruleset_load_file(cdsl_ruleset_t* set, const char* filepath, int priority,
                            const cdsl_schema_t* schema, char* err_buf, int err_buf_sz);
int cdsl_ruleset_load_string(cdsl_ruleset_t* set, const char* dsl_code, int priority,
                              const cdsl_schema_t* schema, char* err_buf, int err_buf_sz);
int cdsl_ruleset_reload_file(cdsl_ruleset_t* set, const char* rule_name,
                              const char* filepath, const cdsl_schema_t* schema,
                              char* err_buf, int err_buf_sz);
int cdsl_ruleset_validate_deps(const cdsl_ruleset_t* set, char* err_buf, int err_buf_sz);
int cdsl_ruleset_topo_sort(cdsl_ruleset_t* set);
/** @} */

#endif
/** @} */
