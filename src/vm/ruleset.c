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
#include "cdsl/util/threads.h"

/**
 * @brief Create an empty ruleset.
 *
 * @return Newly allocated ruleset (must be freed with cdsl_ruleset_free)
 */
cdsl_ruleset_t*
cdsl_ruleset_create(void)
{
	cdsl_ruleset_t* set = calloc(1, sizeof(cdsl_ruleset_t));
	if (!set) {
		return NULL;
	}
	return set;
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
	if (!e) {
		/* Out of memory: caller has already yielded ownership of rule,
		 * so we must free it here to avoid a leak. */
		cdsl_free_rule(rule);
		return;
	}
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

static cdsl_ruleset_entry_t*
find_entry_by_name(cdsl_ruleset_t* set, const char* name)
{
	if (!set || !name) {
		return NULL;
	}
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		if (e->rule && e->rule->name && strcmp(e->rule->name, name) == 0) {
			return e;
		}
	}
	return NULL;
}

cdsl_ruleset_report_t*
cdsl_vm_execute_ruleset(cdsl_vm_t* vm, cdsl_ruleset_t* set, cdsl_context_t* ctx)
{
	if (!vm || !set) {
		return NULL;
	}
	cdsl_ruleset_report_t* batch = calloc(1, sizeof(*batch));
	if (!batch) {
		return NULL;
	}
	batch->rule_reports = (cdsl_rule_report_t**)calloc(set->count, sizeof(cdsl_rule_report_t*));
	if (!batch->rule_reports) {
		free(batch);
		return NULL;
	}
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
	cdsl_action_cb_entry_t* callbacks_snapshot; /* Snapshot of parent's callbacks */
	cdsl_func_entry_t* functions_snapshot;	    /* Snapshot of parent's functions */
	void* user_data;
	int debug_enabled;
	int max_expr_depth;
	int64_t timeout_us;
	int64_t memory_limit;
	int64_t instruction_limit;
	cdsl_trace_cb_t trace_cb;
	void* trace_ud;
	cdsl_stats_t worker_stats;
	cdsl_ruleset_t* set;
	cdsl_context_t* ctx;
	cdsl_atomic_int_t* next_idx;
	cdsl_rule_report_t** rule_reports;
	int total_rules;
} parallel_worker_arg_t;

static void*
parallel_worker(void* arg)
{
	parallel_worker_arg_t* a = (parallel_worker_arg_t*)arg;

	/* Create worker VM with snapshot callbacks/functions (no parent lock needed) */
	cdsl_vm_t* wvm = calloc(1, sizeof(*wvm));
	if (!wvm) {
		return NULL;
	}
	CDSL_RWLOCK_INIT(&wvm->rwlock);
	wvm->schema = NULL;
	wvm->user_data = a->user_data;
	wvm->debug_enabled = a->debug_enabled;
	wvm->max_expr_depth = a->max_expr_depth > 0 ? a->max_expr_depth : 64;
	wvm->timeout_us = a->timeout_us;
	wvm->memory_limit = a->memory_limit;
	wvm->instruction_limit = a->instruction_limit;
	wvm->trace_cb = a->trace_cb;
	wvm->trace_ud = a->trace_ud;
	wvm->callbacks = a->callbacks_snapshot;
	wvm->functions = a->functions_snapshot;

	while (1) {
		int idx = CDSL_ATOMIC_FETCH_ADD(a->next_idx, 1);
		if (idx >= a->total_rules) {
			break;
		}
		/* Find the idx-th rule entry */
		cdsl_ruleset_entry_t* e = a->set->entries;
		for (int i = 0; i < idx && e; i++) {
			e = e->next;
		}
		if (!e) {
			break;
		}
		a->rule_reports[idx] = cdsl_vm_execute(wvm, e->rule, a->ctx);
	}

	a->worker_stats = wvm->stats;
	CDSL_RWLOCK_DESTROY(&wvm->rwlock);
	free(wvm);
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
	if (!batch) {
		return NULL;
	}
	batch->rule_reports = (cdsl_rule_report_t**)calloc(set->count, sizeof(cdsl_rule_report_t*));
	if (!batch->rule_reports) {
		free(batch);
		return NULL;
	}
	batch->rule_count = set->count;

	/* Snapshot parent's callbacks/functions and config under write lock so workers
	   can traverse them without holding any parent lock (avoids self-deadlock
	   if a user callback tries to register a new action/function). */
	cdsl_action_cb_entry_t* cb_snapshot = NULL;
	cdsl_action_cb_entry_t* cb_tail = NULL;
	cdsl_func_entry_t* fn_snapshot = NULL;
	cdsl_func_entry_t* fn_tail = NULL;
	CDSL_RWLOCK_WRLOCK(&vm->rwlock);
	void* snap_user_data = vm->user_data;
	int snap_debug_enabled = vm->debug_enabled;
	int snap_max_expr_depth = vm->max_expr_depth;
	int64_t snap_timeout_us = vm->timeout_us;
	int64_t snap_memory_limit = vm->memory_limit;
	int64_t snap_instruction_limit = vm->instruction_limit;
	cdsl_trace_cb_t snap_trace_cb = vm->trace_cb;
	void* snap_trace_ud = vm->trace_ud;

	for (cdsl_action_cb_entry_t* cb = vm->callbacks; cb; cb = cb->next) {
		cdsl_action_cb_entry_t* entry = calloc(1, sizeof(*entry));
		if (!entry) {
			break;
		}
		entry->action_name = strdup(cb->action_name);
		entry->cb = cb->cb;
		entry->next = NULL;
		if (!cb_snapshot) {
			cb_snapshot = entry;
		} else {
			cb_tail->next = entry;
		}
		cb_tail = entry;
	}
	for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
		cdsl_func_entry_t* entry = calloc(1, sizeof(*entry));
		if (!entry) {
			break;
		}
		entry->func_name = strdup(fn->func_name);
		entry->cb = fn->cb;
		entry->next = NULL;
		if (!fn_snapshot) {
			fn_snapshot = entry;
		} else {
			fn_tail->next = entry;
		}
		fn_tail = entry;
	}
	CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);

	/* Limit concurrency to thread_count (clamped to rule count) */
	int nthreads = thread_count > 0 && thread_count < set->count ? thread_count : set->count;
	cdsl_atomic_int_t next_idx = 0;

	parallel_worker_arg_t* args = calloc(nthreads, sizeof(*args));
	cdsl_thread_t* threads = calloc(nthreads, sizeof(*threads));
	if (!args || !threads) {
		free(args);
		free(threads);
		/* Free snapshots */
		while (cb_snapshot) {
			cdsl_action_cb_entry_t* next = cb_snapshot->next;
			free(cb_snapshot->action_name);
			free(cb_snapshot);
			cb_snapshot = next;
		}
		while (fn_snapshot) {
			cdsl_func_entry_t* next = fn_snapshot->next;
			free(fn_snapshot->func_name);
			free(fn_snapshot);
			fn_snapshot = next;
		}
		free((void*)batch->rule_reports);
		free(batch);
		return NULL;
	}

	for (int t = 0; t < nthreads; t++) {
		args[t].callbacks_snapshot = cb_snapshot;
		args[t].functions_snapshot = fn_snapshot;
		args[t].user_data = snap_user_data;
		args[t].debug_enabled = snap_debug_enabled;
		args[t].max_expr_depth = snap_max_expr_depth;
		args[t].timeout_us = snap_timeout_us;
		args[t].memory_limit = snap_memory_limit;
		args[t].instruction_limit = snap_instruction_limit;
		args[t].trace_cb = snap_trace_cb;
		args[t].trace_ud = snap_trace_ud;
		args[t].set = set;
		args[t].ctx = ctx;
		args[t].next_idx = &next_idx;
		args[t].rule_reports = batch->rule_reports;
		args[t].total_rules = set->count;

		if (CDSL_THREAD_CREATE(&threads[t], parallel_worker, &args[t]) != 0) {
			args[t].callbacks_snapshot = NULL;
			args[t].functions_snapshot = NULL;
		}
	}

	for (int t = 0; t < nthreads; t++) {
		CDSL_THREAD_JOIN(threads[t]);
	}

	/* Aggregate worker stats into parent VM under write lock */
	CDSL_RWLOCK_WRLOCK(&vm->rwlock);
	for (int t = 0; t < nthreads; t++) {
		vm->stats.total_executions += args[t].worker_stats.total_executions;
		vm->stats.total_rules_executed += args[t].worker_stats.total_rules_executed;
		vm->stats.total_metrics_evaluated += args[t].worker_stats.total_metrics_evaluated;
		vm->stats.total_actions_triggered += args[t].worker_stats.total_actions_triggered;
		vm->stats.total_time_us += args[t].worker_stats.total_time_us;
	}
	if (vm->stats.total_executions > 0) {
		vm->stats.avg_time_us =
		    vm->stats.total_time_us / (double)vm->stats.total_executions;
	}
	for (int i = 0; i < set->count; i++) {
		if (!batch->rule_reports[i]) {
			continue;
		}
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
	CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);

	/* Free snapshots */
	while (cb_snapshot) {
		cdsl_action_cb_entry_t* next = cb_snapshot->next;
		free(cb_snapshot->action_name);
		free(cb_snapshot);
		cb_snapshot = next;
	}
	while (fn_snapshot) {
		cdsl_func_entry_t* next = fn_snapshot->next;
		free(fn_snapshot->func_name);
		free(fn_snapshot);
		fn_snapshot = next;
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
	free((void*)report->rule_reports);
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
	if (sz <= 0) {
		if (err_buf) {
			snprintf(err_buf, err_buf_sz, "Empty or unreadable file: %s", filepath);
		}
		fclose(f);
		return 0;
	}
	char* buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return 0;
	}
	size_t nread = fread(buf, 1, sz, f);
	// NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
	buf[nread] = '\0';
	fclose(f);

	if ((long)nread != sz) {
		free(buf);
		return 0;
	}

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
	cdsl_rule_t* rule = cdsl_parse_string(dsl_code, NULL);
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
	/* If filepath appears to be DSL code rather than a filename, load from string. */
	if (filepath && (strstr(filepath, "RULE ") != NULL || strstr(filepath, "META") != NULL)) {
		return cdsl_ruleset_load_string(set, filepath, 0, schema, err_buf, err_buf_sz);
	}
	return cdsl_ruleset_load_file(set, filepath, 0, schema, err_buf, err_buf_sz);
}

static int
validate_deps_recursive(cdsl_ruleset_t* set,
			cdsl_ruleset_entry_t* entry,
			int* states,
			cdsl_ruleset_entry_t** entries_arr,
			char* err_buf,
			int err_buf_sz)
{
	int idx = -1;
	for (int i = 0; i < set->count; i++) {
		if (entries_arr[i] == entry) {
			idx = i;
			break;
		}
	}
	if (idx == -1) {
		return 1;
	}

	if (states[idx] == 1) {
		if (err_buf) {
			snprintf(err_buf,
				 err_buf_sz,
				 "Dependency cycle detected involving rule '%s'",
				 entry->rule->name);
		}
		return 0;
	}
	if (states[idx] == 2) {
		return 1;
	}

	states[idx] = 1;
	char* deps = cdsl_meta_get(entry->rule->meta_list, "depends_on");
	if (deps) {
		char* dep_buf = strdup(deps);
		char* token = strtok(dep_buf, ",");
		while (token) {
			while (*token == ' ') {
				token++;
			}
			cdsl_ruleset_entry_t* dep_entry = find_entry_by_name(set, token);
			if (!dep_entry) {
				if (err_buf) {
					snprintf(err_buf,
						 err_buf_sz,
						 "Missing dependency '%s' for rule '%s'",
						 token,
						 entry->rule->name);
				}
				free(dep_buf);
				return 0;
			}
			if (!validate_deps_recursive(
				set, dep_entry, states, entries_arr, err_buf, err_buf_sz)) {
				free(dep_buf);
				return 0;
			}
			token = strtok(NULL, ",");
		}
		free(dep_buf);
	}
	states[idx] = 2;
	return 1;
}

int
cdsl_ruleset_validate_deps(const cdsl_ruleset_t* set, char* err_buf, int err_buf_sz)
{
	if (!set || set->count == 0) {
		return 1;
	}
	int* states = calloc(set->count, sizeof(int));
	cdsl_ruleset_entry_t** entries_arr =
	    (cdsl_ruleset_entry_t**)malloc(set->count * sizeof(cdsl_ruleset_entry_t*));
	if (!states || !entries_arr) {
		free(states);	   // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
		free(entries_arr); // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
		return 0;
	}
	int i = 0;
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		entries_arr[i++] = e;
	}

	int ok = 1;
	for (i = 0; i < set->count; i++) {
		if (states[i] == 0) {
			// NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
			if (!validate_deps_recursive((cdsl_ruleset_t*)set,
						     entries_arr[i],
						     states,
						     entries_arr,
						     err_buf,
						     err_buf_sz)) {
				ok = 0;
				break;
			}
		}
	}
	free(states);
	free((void*)entries_arr);
	return ok;
}

static void
topo_sort_recursive(cdsl_ruleset_t* set,
		    cdsl_ruleset_entry_t* entry,
		    int* visited,
		    cdsl_ruleset_entry_t** entries_arr,
		    cdsl_ruleset_entry_t*** output_ptr)
{
	int idx = -1;
	for (int i = 0; i < set->count; i++) {
		if (entries_arr[i] == entry) {
			idx = i;
			break;
		}
	}
	if (idx == -1 || visited[idx]) {
		return;
	}

	visited[idx] = 1;

	char* deps = cdsl_meta_get(entry->rule->meta_list, "depends_on");
	if (deps) {
		char* dep_buf = strdup(deps);
		char* token = strtok(dep_buf, ",");
		while (token) {
			while (*token == ' ') {
				token++;
			}
			cdsl_ruleset_entry_t* dep_entry = find_entry_by_name(set, token);
			if (dep_entry) {
				topo_sort_recursive(
				    set, dep_entry, visited, entries_arr, output_ptr);
			}
			token = strtok(NULL, ",");
		}
		free(dep_buf);
	}

	**output_ptr = entry;
	(*output_ptr)++;
}

int
cdsl_ruleset_topo_sort(cdsl_ruleset_t* set)
{
	if (!set || set->count <= 1) {
		return 1;
	}

	/* First, check for cycles. Topo sort only works on DAGs. */
	if (!cdsl_ruleset_validate_deps(set, NULL, 0)) {
		return 0;
	}

	int* visited = calloc(set->count, sizeof(int));
	cdsl_ruleset_entry_t** entries_arr =
	    (cdsl_ruleset_entry_t**)malloc(set->count * sizeof(cdsl_ruleset_entry_t*));
	cdsl_ruleset_entry_t** sorted_arr =
	    (cdsl_ruleset_entry_t**)malloc(set->count * sizeof(cdsl_ruleset_entry_t*));
	if (!visited || !entries_arr || !sorted_arr) {
		free(visited);	   // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
		free(entries_arr); // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
		free(sorted_arr);  // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
		return 0;
	}
	cdsl_ruleset_entry_t** output_ptr = sorted_arr;

	int i = 0;
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		entries_arr[i++] = e;
	}

	for (i = 0; i < set->count; i++) {
		if (!visited[i]) {
			// NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
			topo_sort_recursive(set, entries_arr[i], visited, entries_arr, &output_ptr);
		}
	}

	/* Rebuild the linked list based on sorted order */
	// NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign)
	set->entries = sorted_arr[0];
	cdsl_ruleset_entry_t* cur = set->entries;
	for (i = 1; i < set->count; i++) {
		cur->next = sorted_arr[i];
		cur = cur->next;
	}
	cur->next = NULL;

	free(visited);
	free((void*)entries_arr);
	free((void*)sorted_arr);
	return 1;
}
/** @} */
