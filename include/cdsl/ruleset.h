/**
 * @file cdsl/ruleset.h
 * @brief RuleSet: batch execution with priority ordering.
 *
 * @defgroup cdsl_ruleset RuleSet
 * @{
 */
#ifndef CDSL_RULESET_H
#define CDSL_RULESET_H

#include "cdsl/ast.h"
#include "cdsl/schema.h"
#include "cdsl/report.h"

/**
 * @brief RuleSet entry with priority ordering.
 */
typedef struct cdsl_ruleset_entry {
	cdsl_rule_t* rule;
	int priority;
	struct cdsl_ruleset_entry* next;
} cdsl_ruleset_entry_t;

/**
 * @brief Collection of rules for batch execution.
 */
typedef struct cdsl_ruleset {
	cdsl_ruleset_entry_t* entries;
	int count;
} cdsl_ruleset_t;

/**
 * @brief Batch execution report for a ruleset.
 */
typedef struct {
	cdsl_rule_report_t** rule_reports;
	int rule_count;
	int total_passed;
	int total_partially;
	int total_failed;
	int total_error;
	int aggregate_score;
	int aggregate_max;
	char* summary;
} cdsl_ruleset_report_t;

struct cdsl_vm;

cdsl_ruleset_t* cdsl_ruleset_create(void);
void cdsl_ruleset_free(cdsl_ruleset_t* set);
void cdsl_ruleset_add(cdsl_ruleset_t* set, cdsl_rule_t* rule, int priority);
int cdsl_ruleset_remove(cdsl_ruleset_t* set, const char* rule_name);
cdsl_ruleset_report_t* cdsl_vm_execute_ruleset(struct cdsl_vm* vm, cdsl_ruleset_t* set, struct cdsl_context* ctx);
cdsl_ruleset_report_t* cdsl_vm_execute_ruleset_parallel(struct cdsl_vm* vm, cdsl_ruleset_t* set, struct cdsl_context* ctx, int thread_count);
void cdsl_ruleset_report_free(cdsl_ruleset_report_t* report);
void cdsl_ruleset_report_print(const cdsl_ruleset_report_t* report);
int cdsl_ruleset_load_file(cdsl_ruleset_t* set, const char* filepath, int priority, const cdsl_schema_t* schema, char* err_buf, int err_buf_sz);
int cdsl_ruleset_load_string(cdsl_ruleset_t* set, const char* dsl_code, int priority, const cdsl_schema_t* schema, char* err_buf, int err_buf_sz);
int cdsl_ruleset_reload_file(cdsl_ruleset_t* set, const char* rule_name, const char* filepath, const cdsl_schema_t* schema, char* err_buf, int err_buf_sz);
int cdsl_ruleset_validate_deps(const cdsl_ruleset_t* set, char* err_buf, int err_buf_sz);
int cdsl_ruleset_topo_sort(cdsl_ruleset_t* set);

#endif
/** @} */
