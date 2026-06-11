/**
 * @file cdsl/report.h
 * @brief Rule evaluation reports and execution results.
 *
 * @defgroup cdsl_report Report
 * @{
 */
#ifndef CDSL_REPORT_H
#define CDSL_REPORT_H

#include "cdsl/util/portability.h"

#include "cdsl/ast.h"
#include "cdsl/schema.h"

/**
 * @brief Rule execution status (tri-state).
 */
typedef enum {
	CDSL_STATUS_PASSED,
	CDSL_STATUS_PARTIALLY_PASSED,
	CDSL_STATUS_FAILED,
	CDSL_STATUS_ERROR
} cdsl_rule_status_t;

/**
 * @brief Individual metric evaluation result.
 */
typedef struct cdsl_metric_result {
	char* metric_name;
	char* description;
	int max_weight;
	int score_obtained;
	int is_critical;
	int is_passed;
	char* matched_case_expr;
	char* violation_reason;
} cdsl_metric_result_t;

/**
 * @brief Complete rule evaluation report.
 */
typedef struct cdsl_rule_report {
	char* rule_name;
	char* description;
	cdsl_metric_result_t* metrics;
	int metric_count;
	int total_max_score;
	int total_obtained_score;
	cdsl_rule_status_t status;
	char* decision_summary;
} cdsl_rule_report_t;

struct cdsl_vm;
struct cdsl_context;
struct cdsl_compiled_rule;

/**
 * @brief Evaluate a rule and produce a report.
 * @param vm       VM instance
 * @param rule     Rule AST to evaluate
 * @param ctx      Context with variable bindings
 * @return Report with score, status, and per-metric results
 */
CDSL_NODISCARD
cdsl_rule_report_t*
cdsl_vm_execute(struct cdsl_vm* vm, const cdsl_rule_t* rule, struct cdsl_context* ctx);

/**
 * @brief Free a rule report and all owned fields.
 * @param report Report to free (may be NULL)
 */
void cdsl_report_free(cdsl_rule_report_t* report);

/**
 * @brief Print a rule report to stdout.
 * @param report Report to print
 */
void cdsl_report_print(const cdsl_rule_report_t* report);

/**
 * @brief Serialize a rule report to JSON string.
 * @param report Report to serialize
 * @return malloc'd JSON string, or NULL on error
 */
CDSL_NODISCARD
char* cdsl_report_to_json(const cdsl_rule_report_t* report);

/**
 * @brief Evaluate a compiled rule and produce a report.
 * @param vm       VM instance
 * @param compiled Pre-compiled rule (bytecode)
 * @param ctx      Context with variable bindings
 * @return Report with score, status, and per-metric results
 */
CDSL_NODISCARD
cdsl_rule_report_t* cdsl_vm_execute_compiled(struct cdsl_vm* vm,
					     struct cdsl_compiled_rule* compiled,
					     struct cdsl_context* ctx);

#endif
/** @} */
