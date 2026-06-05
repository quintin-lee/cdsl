/**
 * @file test_vm_config.c
 * @brief Tests for VM configuration API: stats, timeout, memory limit, debug, trace.
 */

#include "test.h"
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
	TEST_ASSERT(stats->total_time_us > 0, "Total time > 0");
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

int
main()
{
	printf("Running VM config tests...\n");
	test_vm_timeout_get_set();
	test_vm_memory_limit_get_set();
	test_vm_debug_mode();
	test_vm_trace_callback();
	test_vm_stats();
	test_vm_execute_with_null_vm();
	test_vm_set_debug_null_safe();
	TEST_SUMMARY();
	TEST_EXIT();
}
