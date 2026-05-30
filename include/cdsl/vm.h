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
	long total_executions;
	long total_rules_executed;
	long total_metrics_evaluated;
	long total_actions_triggered;
	double total_time_us;
	double avg_time_us;
} cdsl_stats_t;

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
} cdsl_vm_t;

cdsl_vm_t* cdsl_vm_create(const cdsl_schema_t* schema);
void cdsl_vm_free(cdsl_vm_t* vm);
void cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb);
void cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb);
void cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled);
int cdsl_vm_get_max_expr_depth(const cdsl_vm_t* vm);
void cdsl_vm_set_max_expr_depth(cdsl_vm_t* vm, int depth);
cdsl_stats_t* cdsl_vm_get_stats(const cdsl_vm_t* vm);
void cdsl_vm_reset_stats(cdsl_vm_t* vm);

#endif
/** @} */
