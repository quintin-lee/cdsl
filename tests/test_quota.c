#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/bytecode.h"
#include <string.h>

/* Simple rule instruction quota: expression has ~15 AST nodes */
static void
test_simple_rule_quota_exceeded(void)
{
	TEST_BEGIN("simple rule - quota exceeded");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { WHEN 1 + 2 + 3 + 4 + 5 + 6 + 7 == 28 THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	cdsl_vm_set_instruction_limit(vm, 5); // Very low limit — aborts during evaluation
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT(rpt == NULL || rpt->status == CDSL_STATUS_ERROR,
		    "Should abort due to instruction quota");

	if (rpt) {
		cdsl_report_free(rpt);
	}
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/* Simple rule with high enough limit should succeed */
static void
test_simple_rule_quota_sufficient(void)
{
	TEST_BEGIN("simple rule - quota sufficient");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { WHEN 1 + 2 + 3 + 4 + 5 + 6 + 7 == 28 THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	cdsl_vm_set_instruction_limit(vm, 1000); // Abundant limit
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "WHEN true -> FAILED");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/* Simple rule no limit (0 = unlimited) */
static void
test_simple_rule_no_limit(void)
{
	TEST_BEGIN("simple rule - no instruction limit");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { WHEN 1 + 2 + 3 + 4 + 5 + 6 + 7 == 28 THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	// Default is 0 (unlimited)
	TEST_ASSERT_INT(cdsl_vm_get_instruction_limit(vm), 0, "Default instruction limit is 0");
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "WHEN true -> FAILED");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/* Instruction count resets between executions (AST and bytecode) */
static void
test_quota_reset_between_executions(void)
{
	TEST_BEGIN("instruction count resets between executions");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE r { WHEN 1 + 2 + 3 == 6 THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	/* Tight limit sufficient for 1 execution (~7 AST nodes), repeated 30 times */
	cdsl_vm_set_instruction_limit(vm, 20);
	for (int i = 0; i < 30; i++) {
		cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
		TEST_ASSERT_NOT_NULL(rpt, "AST execution should succeed");
		TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "WHEN true -> FAILED");
		cdsl_report_free(rpt);
	}

	/* Same check for bytecode execution */
	cdsl_schema_register_var(schema, "v", CDSL_TYPE_INT);
	cdsl_context_set_int(ctx, "v", 1);
	cdsl_rule_t* bc_rule =
	    cdsl_parse_string("RULE r2 { WHEN v + v + v == 3 THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(bc_rule, "Parse bc_rule");
	cdsl_bytecode_t bc = {0};
	TEST_ASSERT_INT(cdsl_bytecode_compile(bc_rule, schema, &bc), 1, "Compile bc_rule");
	cdsl_vm_set_instruction_limit(vm, 20);
	for (int i = 0; i < 30; i++) {
		cdsl_rule_report_t* rpt = cdsl_bytecode_execute_rule(vm, &bc, ctx);
		TEST_ASSERT_NOT_NULL(rpt, "Bytecode execution should succeed");
		TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "Bytecode WHEN true -> FAILED");
		cdsl_report_free(rpt);
	}
	cdsl_bytecode_free(&bc);
	cdsl_free_rule(bc_rule);

	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/* Per-execution cumulative memory allocation budget */
static void
test_memory_limit_enforcement(void)
{
	TEST_BEGIN("memory limit budget enforcement and reset");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "payload", CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	char big_str[256];
	memset(big_str, 'A', sizeof(big_str) - 1);
	big_str[sizeof(big_str) - 1] = '\0';
	cdsl_context_set_string(ctx, "payload", big_str);

	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE mem_r { WHEN uppercase(payload) == \"X\" THEN block(\"hit\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse mem_r");

	/* Budget smaller than string allocation -> ERROR with memory message */
	cdsl_vm_set_memory_limit(vm, 64);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "AST report not NULL on memory quota abort");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_ERROR, "AST status should be ERROR");
	TEST_ASSERT_NOT_NULL(rpt->error, "AST report->error should be populated");
	if (rpt->error) {
		TEST_ASSERT(strstr(rpt->error->message, "Memory") != NULL,
			    "Error message should mention Memory");
	}
	cdsl_report_free(rpt);

	/* Bytecode path also enforces memory_limit */
	cdsl_bytecode_t bc = {0};
	TEST_ASSERT_INT(cdsl_bytecode_compile(rule, schema, &bc), 1, "Compile mem_r");
	rpt = cdsl_bytecode_execute_rule(vm, &bc, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Bytecode report not NULL on memory quota abort");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_ERROR, "Bytecode status should be ERROR");
	cdsl_report_free(rpt);

	/* Sufficient budget (~520 bytes per run for var lookup + uppercase) resets across 20 runs */
	cdsl_vm_set_memory_limit(vm, 1024);
	for (int i = 0; i < 20; i++) {
		rpt = cdsl_vm_execute(vm, rule, ctx);
		TEST_ASSERT_NOT_NULL(rpt, "AST execution within budget");
		TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "Budget resets each run (AST)");
		cdsl_report_free(rpt);

		rpt = cdsl_bytecode_execute_rule(vm, &bc, ctx);
		TEST_ASSERT_NOT_NULL(rpt, "BC execution within budget");
		TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "Budget resets each run (BC)");
		cdsl_report_free(rpt);
	}

	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/* Instruction limit with metric rule */
static void
test_metric_rule_quota(void)
{
	TEST_BEGIN("metric rule - instruction quota");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "x", 42);

	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { META { description = \"test\" } "
	    "METRIC m { META { weight = \"100\" } CASE x > 0 THEN score(100) DEFAULT score(0) } }",
	    NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse metric rule");

	// Very low limit — should abort during metric evaluation
	cdsl_vm_set_instruction_limit(vm, 3);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT(rpt == NULL || rpt->status == CDSL_STATUS_ERROR,
		    "Metric rule should abort with low instruction limit");

	if (rpt) {
		cdsl_report_free(rpt);
	}
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/* Expression depth limit (separate from instruction quota) */
static void
test_expr_depth_limit(void)
{
	TEST_BEGIN("expression depth limit");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE r { WHEN 1 + 2 + 3 == 6 THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	// Set depth to 1 — should be too shallow to evaluate nested expression
	cdsl_vm_set_max_expr_depth(vm, 1);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL with insufficient depth");

	// When condition can't be evaluated because depth exceeded,
	// triggered=0 -> PASSED (no violation detected)
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_PASSED, "WHEN evaluation depth exceeded, default to PASSED");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/* vm_get_instruction_limit roundtrip */
static void
test_instruction_limit_get_set(void)
{
	TEST_BEGIN("instruction limit get/set");
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	TEST_ASSERT_INT(cdsl_vm_get_instruction_limit(vm), 0, "Default is 0");

	cdsl_vm_set_instruction_limit(vm, 42);
	TEST_ASSERT_INT(cdsl_vm_get_instruction_limit(vm), 42, "Get after set matches");

	cdsl_vm_set_instruction_limit(vm, -1);
	TEST_ASSERT_INT(cdsl_vm_get_instruction_limit(vm), -1, "Negative limit is allowed");

	cdsl_vm_set_instruction_limit(vm, 0);
	TEST_ASSERT_INT(cdsl_vm_get_instruction_limit(vm), 0, "Zero = unlimited");

	cdsl_vm_free(vm);
	TEST_END();
}

/* Expression depth get/set roundtrip */
static void
test_expr_depth_get_set(void)
{
	TEST_BEGIN("expression depth get/set");
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	int default_depth = cdsl_vm_get_max_expr_depth(vm);
	TEST_ASSERT(default_depth > 0, "Default depth is positive");

	cdsl_vm_set_max_expr_depth(vm, 10);
	TEST_ASSERT_INT(cdsl_vm_get_max_expr_depth(vm), 10, "Set depth to 10");

	cdsl_vm_free(vm);
	TEST_END();
}

int
main()
{
	test_simple_rule_quota_exceeded();
	test_simple_rule_quota_sufficient();
	test_simple_rule_no_limit();
	test_quota_reset_between_executions();
	test_memory_limit_enforcement();
	test_metric_rule_quota();
	test_expr_depth_limit();
	test_instruction_limit_get_set();
	test_expr_depth_get_set();
	TEST_SUMMARY();
	TEST_EXIT();
}
