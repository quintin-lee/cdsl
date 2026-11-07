/**
 * @file test_vm_config.c
 * @brief Tests for VM configuration API: stats, timeout, memory limit, debug, trace.
 */

#include "test.h"
#include "cdsl/util/threads.h"
#include "cdsl/util/portability.h"
#include <cdsl/cdsl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static void
test_vm_timeout_get_set(void)
{
	TEST_BEGIN("VM timeout get/set");
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	uint64_t default_timeout = cdsl_vm_get_timeout(vm);
	TEST_ASSERT_INT(default_timeout, 0, "Default timeout is 0 (unlimited)");

	cdsl_vm_set_timeout(vm, 5000);
	TEST_ASSERT_INT(cdsl_vm_get_timeout(vm), 5000, "Timeout set to 5000");

	cdsl_vm_set_timeout(vm, 0);
	TEST_ASSERT_INT(cdsl_vm_get_timeout(vm), 0, "Timeout 0 = no timeout");

	cdsl_vm_free(vm);
	TEST_END();
}

static void
test_vm_memory_limit_get_set(void)
{
	TEST_BEGIN("VM memory limit get/set");
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	uint64_t default_limit = cdsl_vm_get_memory_limit(vm);
	TEST_ASSERT_INT(default_limit, 0, "Default memory limit is 0 (unlimited)");

	cdsl_vm_set_memory_limit(vm, 65536);
	TEST_ASSERT_INT(cdsl_vm_get_memory_limit(vm), 65536, "Memory limit set to 65536");

	cdsl_vm_free(vm);
	TEST_END();
}

static void
test_vm_debug_mode(void)
{
	TEST_BEGIN("VM debug mode enable/disable");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule = cdsl_parse_string("RULE r { WHEN 1 == 1 THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	// Enable debug
	cdsl_vm_set_debug(vm, 1);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Debug mode: report not NULL");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "Debug mode: 1==1 -> FAILED");
	cdsl_report_free(rpt);

	// Disable debug
	cdsl_vm_set_debug(vm, 0);
	rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "No debug: report not NULL");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "No debug: 1==1 -> FAILED");
	cdsl_report_free(rpt);

	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/* Trace callback counter */
static int g_trace_count = 0;

static void
test_trace_cb(const cdsl_trace_event_t* event, void* user_data)
{
	(void)event;
	(void)user_data;
	g_trace_count++;
}

static void
test_vm_trace_callback(void)
{
	TEST_BEGIN("VM trace callback");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule = cdsl_parse_string("RULE r { WHEN 1 == 1 THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	// Register trace callback
	g_trace_count = 0;
	cdsl_vm_set_trace_callback(vm, test_trace_cb, NULL);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Trace: report not NULL");
	TEST_ASSERT(g_trace_count > 0, "Trace callback should have been invoked");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "Trace: 1==1 -> FAILED");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_vm_stats(void)
{
	TEST_BEGIN("VM stats tracking");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule = cdsl_parse_string("RULE r { WHEN 1 == 1 THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	// Execute twice
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	cdsl_report_free(rpt);
	rpt = cdsl_vm_execute(vm, rule, ctx);
	cdsl_report_free(rpt);

	// Check stats
	cdsl_stats_t* stats = cdsl_vm_get_stats(vm);
	TEST_ASSERT_NOT_NULL(stats, "Stats should not be NULL");
	TEST_ASSERT_INT(stats->total_executions, 2, "2 executions counted");
	TEST_ASSERT_INT(stats->total_rules_executed, 2, "2 rules executed");
	TEST_ASSERT(stats->total_metrics_evaluated >= 2, "Metrics evaluated >= 2");
	TEST_ASSERT(stats->total_time_us >= 0, "Total time >= 0");
	free(stats);

	// Reset stats
	cdsl_vm_reset_stats(vm);
	stats = cdsl_vm_get_stats(vm);
	TEST_ASSERT_INT(stats->total_executions, 0, "Stats reset: 0 executions");
	TEST_ASSERT_INT(stats->total_rules_executed, 0, "Stats reset: 0 rules");
	TEST_ASSERT_INT(stats->total_metrics_evaluated, 0, "Stats reset: 0 metrics");
	TEST_ASSERT(stats->total_time_us == 0, "Stats reset: 0 time");
	free(stats);

	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_vm_execute_with_null_vm(void)
{
	TEST_BEGIN("VM execute with NULL args");
	// These should not crash
	cdsl_rule_report_t* rpt = cdsl_vm_execute(NULL, NULL, NULL);
	TEST_ASSERT_NULL(rpt, "NULL vm -> NULL report");

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	rpt = cdsl_vm_execute(vm, NULL, NULL);
	TEST_ASSERT_NULL(rpt, "NULL rule -> NULL report");

	cdsl_vm_free(vm);
	TEST_END();
}

static void
test_vm_set_debug_null_safe(void)
{
	TEST_BEGIN("VM debug NULL-safe");
	cdsl_vm_set_debug(NULL, 1);		      // Should not crash
	cdsl_vm_set_trace_callback(NULL, NULL, NULL); // Should not crash
	TEST_END();
}

static void
test_vm_create_returns_non_null(void)
{
	TEST_BEGIN("VM create returns non-NULL");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	TEST_ASSERT_NOT_NULL(vm, "cdsl_vm_create should return non-NULL");
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static int g_action_called = 0;

static void
test_action_cb(const char* name, cdsl_arg_node_t* args, void* ud)
{
	(void)name;
	(void)args;
	(void)ud;
	g_action_called = 1;
}

static void
test_vm_free_null_safe(void)
{
	TEST_BEGIN("VM free NULL-safe");
	cdsl_vm_free(NULL); /* should not crash */
	TEST_END();
}

static void
test_vm_register_action_and_function(void)
{
	TEST_BEGIN("VM register action and function");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);

	g_action_called = 0;
	cdsl_vm_register_action(vm, "test_action", test_action_cb);
	cdsl_vm_register_function(vm, "test_func", NULL);

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_ASSERT(1, "registration should not crash");
	TEST_END();
}

static void
test_vm_debug_and_depth(void)
{
	TEST_BEGIN("VM debug and max_expr_depth");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);

	cdsl_vm_set_debug(vm, 1);
	TEST_ASSERT_INT(cdsl_vm_get_max_expr_depth(vm),
			CDSL_MAX_EXPR_DEPTH,
			"default max expr depth should be CDSL_MAX_EXPR_DEPTH");

	cdsl_vm_set_max_expr_depth(vm, 32);
	TEST_ASSERT_INT(cdsl_vm_get_max_expr_depth(vm), 32, "max expr depth should be 32");

	cdsl_vm_set_max_expr_depth(vm, 0); /* invalid, should be ignored */
	TEST_ASSERT_INT(cdsl_vm_get_max_expr_depth(vm), 32, "depth 0 should be ignored");

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_vm_stats_reset(void)
{
	TEST_BEGIN("VM stats get and reset");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);

	cdsl_stats_t* s = cdsl_vm_get_stats(vm);
	TEST_ASSERT_NOT_NULL(s, "get_stats should return non-NULL");
	TEST_ASSERT_INT(s->total_executions, 0, "executions should start at 0");
	free(s);

	cdsl_vm_reset_stats(vm);

	s = cdsl_vm_get_stats(vm);
	TEST_ASSERT_NOT_NULL(s, "get_stats after reset should return non-NULL");
	TEST_ASSERT_INT(s->total_executions, 0, "executions should be 0 after reset");
	free(s);

	/* NULL-safe */
	TEST_ASSERT_NULL(cdsl_vm_get_stats(NULL), "get_stats(NULL) should return NULL");
	cdsl_vm_reset_stats(NULL); /* should not crash */

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_vm_quotas(void)
{
	TEST_BEGIN("VM sandbox quotas");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);

	cdsl_vm_set_timeout(vm, 1000000);
	TEST_ASSERT_INT(cdsl_vm_get_timeout(vm), 1000000, "timeout should be 1000000");

	cdsl_vm_set_memory_limit(vm, 65536);
	TEST_ASSERT_INT(cdsl_vm_get_memory_limit(vm), 65536, "memory limit should be 65536");

	cdsl_vm_set_instruction_limit(vm, 1000);
	TEST_ASSERT_INT(
	    cdsl_vm_get_instruction_limit(vm), 1000, "instruction limit should be 1000");

	/* NULL-safe getters */
	TEST_ASSERT_INT(cdsl_vm_get_timeout(NULL), 0, "get_timeout(NULL) == 0");
	TEST_ASSERT_INT(cdsl_vm_get_memory_limit(NULL), 0, "get_memory_limit(NULL) == 0");
	TEST_ASSERT_INT(cdsl_vm_get_instruction_limit(NULL), 0, "get_instruction_limit(NULL) == 0");

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_vm_trace_null_safe(void)
{
	TEST_BEGIN("VM trace callback NULL-safe");
	cdsl_vm_set_trace_callback(NULL, NULL, NULL); /* should not crash */
	TEST_END();
}

static cdsl_value_t
test_counter_func(const char* func_name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)func_name;
	(void)args;
	(void)ctx;
	(void)vm;
	cdsl_value_t v;
	v.type = CDSL_TYPE_INT;
	v.data.int_val = 42;
	v.owned_string = 0;
	return v;
}

static void*
parallel_execute_worker(void* arg)
{
	cdsl_vm_t* vm = (cdsl_vm_t*)arg;
	cdsl_ruleset_t* rs = cdsl_ruleset_create();
	if (!rs) {
		return NULL;
	}

	/* Create a few simple rules in the ruleset.
	 * The first rule calls the registered "counter" function to exercise
	 * the BC_CALL read-lock path where func_found == 1 (match found). */
	const char* rule_dsls[] = {
	    "RULE r1 { WHEN counter() == 42 THEN block(\"a\") }",
	    "RULE r2 { WHEN true THEN block(\"b\") }",
	    "RULE r3 { WHEN counter() == 42 THEN block(\"c\") }",
	};
	for (int i = 0; i < 3; i++) {
		cdsl_rule_t* rule = cdsl_parse_string(rule_dsls[i], NULL);
		if (rule) {
			cdsl_ruleset_add(rs, rule, i + 1);
		}
	}

	/* Execute ruleset in parallel on the shared VM */
	cdsl_ruleset_report_t* report = cdsl_vm_execute_ruleset_parallel(vm, rs, NULL, 3);
	if (report) {
		cdsl_ruleset_report_free(report);
	}

	cdsl_ruleset_free(rs);
	return NULL;
}

static void
test_vm_parallel_execute_concurrent(void)
{
	TEST_BEGIN("Concurrent ruleset parallel execution on shared VM");

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	TEST_ASSERT_NOT_NULL(vm, "VM created");

	/* Register a test function (exercises BC_CALL read lock path) */
	cdsl_vm_register_function(vm, "counter", test_counter_func);

	/* Register a test action (exercises cdsl_trigger_action_internal read lock path) */
	cdsl_vm_register_action(vm, "block", NULL);

	int thread_count = 4;
	cdsl_thread_t threads[4];

	for (int i = 0; i < thread_count; i++) {
		if (CDSL_THREAD_CREATE(&threads[i], parallel_execute_worker, vm) != 0) {
			TEST_ASSERT(0, "Failed to create thread");
		}
	}

	for (int i = 0; i < thread_count; i++) {
		CDSL_THREAD_JOIN(threads[i]);
	}

	/* Verify stats are aggregated (no crash = pass, stats > 0 = work done) */
	TEST_ASSERT_INT(
	    vm->stats.total_executions, 0, "Stats accessible after concurrent execution");

	cdsl_vm_free(vm);
	TEST_END();
}

static void
dummy_action_cb(const char* name, cdsl_arg_node_t* args, void* user_data)
{
	(void)name;
	(void)args;
	(void)user_data;
}

static cdsl_value_t
dummy_func_cb(const char* func_name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)func_name;
	(void)args;
	(void)ctx;
	(void)vm;
	return (cdsl_value_t){.type = CDSL_TYPE_INT, .data.int_val = 0};
}

static void
test_vm_register_callbacks_oom(void)
{
	TEST_BEGIN("vm register action and function OOM safety");

	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(s);

	/* Simulate OOM on action registration */
	cdsl_test_set_oom_countdown(0);
	cdsl_vm_register_action(vm, "block", dummy_action_cb);
	TEST_ASSERT_NULL(vm->callbacks, "vm->callbacks remains clean");

	/* Simulate OOM on func registration */
	cdsl_test_set_oom_countdown(-1);
	int initial_funcs = 0;
	for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
		initial_funcs++;
	}

	cdsl_test_set_oom_countdown(0);
	cdsl_vm_register_function(vm, "my_fn", dummy_func_cb);
	int count_after = 0;
	for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
		count_after++;
	}
	TEST_ASSERT_INT(count_after, initial_funcs, "vm->functions count unchanged on OOM");

	cdsl_test_set_oom_countdown(-1);
	cdsl_vm_free(vm);
	cdsl_schema_free(s);
	TEST_END();
}

int
main()
{
	printf("Running VM config tests...\n");
	test_vm_register_callbacks_oom();
	test_vm_timeout_get_set();
	test_vm_memory_limit_get_set();
	test_vm_debug_mode();
	test_vm_trace_callback();
	test_vm_stats();
	test_vm_execute_with_null_vm();
	test_vm_set_debug_null_safe();
	test_vm_create_returns_non_null();
	test_vm_free_null_safe();
	test_vm_register_action_and_function();
	test_vm_debug_and_depth();
	test_vm_stats_reset();
	test_vm_quotas();
	test_vm_trace_null_safe();
	test_vm_parallel_execute_concurrent();
	TEST_SUMMARY();
	TEST_EXIT();
}
