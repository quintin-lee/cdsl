/**
 * @file cdsl/report.h
 * @brief Rule evaluation reports and execution results.
 *
 * @defgroup cdsl_report Report
 * @{
 */
#ifndef CDSL_REPORT_H
#define CDSL_REPORT_H

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

[[nodiscard]]
cdsl_rule_report_t*
cdsl_vm_execute(struct cdsl_vm* vm, const cdsl_rule_t* rule, struct cdsl_context* ctx);
void cdsl_report_free(cdsl_rule_report_t* report);
void cdsl_report_print(const cdsl_rule_report_t* report);
[[nodiscard]]
char* cdsl_report_to_json(const cdsl_rule_report_t* report);
[[nodiscard]]
cdsl_rule_report_t* cdsl_vm_execute_compiled(struct cdsl_vm* vm,
					     struct cdsl_compiled_rule* compiled,
					     struct cdsl_context* ctx);

#endif
/** @} */
