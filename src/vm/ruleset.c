/**
 * @file vm_ruleset.c
 * @brief RuleSet batch and parallel execution implementation.
 *
 * Implements the RuleSet abstraction for managing and executing
 * collections of DSL rules as a batch. Supports:
 * - Priority-ordered rule insertion
 * - Rule removal by name
 * - Sequential batch execution with aggregate reporting
 * - Parallel execution with per-thread VM cloning
 * - File-based rule loading and hot reload
 * - Topological sorting and dependency validation (stubs)
 *
 * @defgroup ruleset RuleSet Batch Execution
 * @{
 */

#include "cdsl/execution.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/**
 * @brief Create an empty ruleset.
 *
 * @return Newly allocated ruleset (must be freed with cdsl_ruleset_free)
 */
cdsl_ruleset_t*
cdsl_ruleset_create(void)
{
	return calloc(1, sizeof(cdsl_ruleset_t));
}

void
cdsl_ruleset_free(cdsl_ruleset_t* set)
{
	if (!set) {
		return;
	}
	cdsl_ruleset_entry_t* e = set->entries;
	while (e) {
		cdsl_ruleset_entry_t* next = e->next;
		cdsl_free_rule(e->rule);
		free(e);
		e = next;
	}
	free(set);
}

void
cdsl_ruleset_add(cdsl_ruleset_t* set, cdsl_rule_t* rule, int priority)
{
	if (!set || !rule) {
		return;
	}
	cdsl_ruleset_entry_t* e = calloc(1, sizeof(*e));
	e->rule = rule;
	e->priority = priority;
	if (!set->entries || priority < set->entries->priority) {
		e->next = set->entries;
		set->entries = e;
	} else {
		cdsl_ruleset_entry_t* cur = set->entries;
		while (cur->next && cur->next->priority <= priority) {
			cur = cur->next;
		}
		e->next = cur->next;
		cur->next = e;
	}
	set->count++;
}

int
cdsl_ruleset_remove(cdsl_ruleset_t* set, const char* rule_name)
{
	if (!set || !rule_name) {
		return 0;
	}
	cdsl_ruleset_entry_t* prev = NULL;
	cdsl_ruleset_entry_t* cur = set->entries;
	while (cur) {
		if (strcmp(cur->rule->name, rule_name) == 0) {
			if (prev) {
				prev->next = cur->next;
			} else {
				set->entries = cur->next;
			}
			cdsl_free_rule(cur->rule);
			free(cur);
			set->count--;
			return 1;
		}
		prev = cur;
		cur = cur->next;
	}
	return 0;
}

cdsl_ruleset_report_t*
cdsl_vm_execute_ruleset(cdsl_vm_t* vm, cdsl_ruleset_t* set, cdsl_context_t* ctx)
{
	if (!vm || !set) {
		return NULL;
	}
	cdsl_ruleset_report_t* batch = calloc(1, sizeof(*batch));
	batch->rule_reports = calloc(set->count, sizeof(cdsl_rule_report_t*));
	batch->rule_count = set->count;

	int idx = 0;
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next, idx++) {
		batch->rule_reports[idx] = cdsl_vm_execute(vm, e->rule, ctx);
		if (batch->rule_reports[idx]) {
			cdsl_rule_report_t* r = batch->rule_reports[idx];
			batch->aggregate_score += r->total_obtained_score;
			batch->aggregate_max += r->total_max_score;
			switch (r->status) {
			case CDSL_STATUS_PASSED:
				batch->total_passed++;
				break;
			case CDSL_STATUS_PARTIALLY_PASSED:
				batch->total_partially++;
				break;
			case CDSL_STATUS_FAILED:
				batch->total_failed++;
				break;
			case CDSL_STATUS_ERROR:
				batch->total_error++;
				break;
			}
		}
	}

	char buf[512];
	snprintf(
	    buf,
	    sizeof(buf),
	    "Batch completed: %d passed, %d partially, %d failed, %d errors. Total score: %d/%d",
	    batch->total_passed,
	    batch->total_partially,
	    batch->total_failed,
	    batch->total_error,
	    batch->aggregate_score,
	    batch->aggregate_max);
	batch->summary = strdup(buf);
	return batch;
}

typedef struct {
	cdsl_vm_t* vm;
	cdsl_rule_t* rule;
	cdsl_context_t* ctx;
	cdsl_rule_report_t* result;
} parallel_worker_arg_t;

static void*
parallel_worker(void* arg)
{
	parallel_worker_arg_t* a = (parallel_worker_arg_t*)arg;
	a->result = cdsl_vm_execute(a->vm, a->rule, a->ctx);
	return NULL;
}

cdsl_ruleset_report_t*
cdsl_vm_execute_ruleset_parallel(cdsl_vm_t* vm,
				 cdsl_ruleset_t* set,
				 cdsl_context_t* ctx,
				 int thread_count)
{
	if (!vm || !set) {
		return NULL;
	}
	if (thread_count <= 1 || set->count <= 1) {
		return cdsl_vm_execute_ruleset(vm, set, ctx);
	}

	cdsl_ruleset_report_t* batch = calloc(1, sizeof(*batch));
	batch->rule_reports = calloc(set->count, sizeof(cdsl_rule_report_t*));
	batch->rule_count = set->count;

	parallel_worker_arg_t* args = calloc(set->count, sizeof(parallel_worker_arg_t));
	pthread_t* threads = calloc(set->count, sizeof(pthread_t));

	/* Note: In a real system, we'd use a thread pool. Here we spawn one per rule
	 * up to set->count, which is simple but not efficient for huge rulesets.
	 * Also, each thread needs its own VM if they share state, but our VMs
	 * are mostly read-only except for stats. For safety, we should clones VMs.
	 */

	int idx = 0;
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next, idx++) {
		args[idx].rule = e->rule;
		args[idx].ctx = ctx;
		/* Simple VM clone for thread safety */
		args[idx].vm = cdsl_vm_create(vm->schema);
		args[idx].vm->callbacks = vm->callbacks;
		args[idx].vm->functions = vm->functions;
		args[idx].vm->user_data = vm->user_data;
		args[idx].vm->debug_enabled = vm->debug_enabled;

		pthread_create(&threads[idx], NULL, parallel_worker, &args[idx]);
	}

	for (int i = 0; i < set->count; i++) {
		pthread_join(threads[i], NULL);
		batch->rule_reports[i] = args[i].result;
		if (batch->rule_reports[i]) {
			cdsl_rule_report_t* r = batch->rule_reports[i];
			batch->aggregate_score += r->total_obtained_score;
			batch->aggregate_max += r->total_max_score;
			switch (r->status) {
			case CDSL_STATUS_PASSED:
				batch->total_passed++;
				break;
			case CDSL_STATUS_PARTIALLY_PASSED:
				batch->total_partially++;
				break;
			case CDSL_STATUS_FAILED:
				batch->total_failed++;
				break;
			case CDSL_STATUS_ERROR:
				batch->total_error++;
				break;
			}
		}
		/* Update main VM stats from worker VM */
		vm->stats.total_executions += args[i].vm->stats.total_executions;
		vm->stats.total_rules_executed += args[i].vm->stats.total_rules_executed;
		vm->stats.total_metrics_evaluated += args[i].vm->stats.total_metrics_evaluated;
		vm->stats.total_time_us += args[i].vm->stats.total_time_us;

		/* Clean up worker VM (but don't free callbacks/functions as they are shared) */
		args[i].vm->callbacks = NULL;
		args[i].vm->functions = NULL;
		cdsl_vm_free(args[i].vm);
	}

	free(args);
	free(threads);

	char buf[512];
	snprintf(buf,
		 sizeof(buf),
		 "Parallel batch completed: %d passed, %d partially, %d failed, %d errors. Total "
		 "score: %d/%d",
		 batch->total_passed,
		 batch->total_partially,
		 batch->total_failed,
		 batch->total_error,
		 batch->aggregate_score,
		 batch->aggregate_max);
	batch->summary = strdup(buf);
	return batch;
}

void
cdsl_ruleset_report_free(cdsl_ruleset_report_t* report)
{
	if (!report) {
		return;
	}
	for (int i = 0; i < report->rule_count; i++) {
		cdsl_report_free(report->rule_reports[i]);
	}
	free(report->rule_reports);
	free(report->summary);
	free(report);
}

void
cdsl_ruleset_report_print(const cdsl_ruleset_report_t* report)
{
	if (!report) {
		printf("No batch report.\n");
		return;
	}
	for (int i = 0; i < report->rule_count; i++) {
		cdsl_report_print(report->rule_reports[i]);
	}
	printf("\n========================================\n");
	printf("  BATCH SUMMARY\n");
	printf("========================================\n");
	printf("  %s\n", report->summary);
	printf("========================================\n\n");
}

int
cdsl_ruleset_load_file(cdsl_ruleset_t* set,
		       const char* filepath,
		       int priority,
		       const cdsl_schema_t* schema,
		       char* err_buf,
		       int err_buf_sz)
{
	if (!set || !filepath) {
		return 0;
	}
	FILE* f = fopen(filepath, "r");
	if (!f) {
		if (err_buf) {
			snprintf(err_buf, err_buf_sz, "Could not open file: %s", filepath);
		}
		return 0;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* buf = malloc(sz + 1);
	fread(buf, 1, sz, f);
	buf[sz] = '\0';
	fclose(f);

	int res = cdsl_ruleset_load_string(set, buf, priority, schema, err_buf, err_buf_sz);
	free(buf);
	return res;
}

int
cdsl_ruleset_load_string(cdsl_ruleset_t* set,
			 const char* dsl_code,
			 int priority,
			 const cdsl_schema_t* schema,
			 char* err_buf,
			 int err_buf_sz)
{
	cdsl_rule_t* rule = cdsl_parse_string(dsl_code);
	if (!rule) {
		if (err_buf) {
			snprintf(err_buf, err_buf_sz, "Parse error");
		}
		return 0;
	}
	if (schema) {
		if (!cdsl_verify_rule(rule, schema, err_buf, err_buf_sz)) {
			cdsl_free_rule(rule);
			return 0;
		}
	}
	cdsl_ruleset_add(set, rule, priority);
	return 1;
}

int
cdsl_ruleset_reload_file(cdsl_ruleset_t* set,
			 const char* rule_name,
			 const char* filepath,
			 const cdsl_schema_t* schema,
			 char* err_buf,
			 int err_buf_sz)
{
	cdsl_ruleset_remove(set, rule_name);
	return cdsl_ruleset_load_file(set, filepath, 0, schema, err_buf, err_buf_sz);
}

int
cdsl_ruleset_validate_deps(const cdsl_ruleset_t* set, char* err_buf, int err_buf_sz)
{
	(void)set;
	(void)err_buf;
	(void)err_buf_sz;
	/* TODO: Implement dependency cycle detection */
	return 1;
}

int
cdsl_ruleset_topo_sort(cdsl_ruleset_t* set)
{
	(void)set;
	/* TODO: Implement topological sort based on depends_on */
	return 1;
}
/** @} */
