/**
 * @file test_dates.c
 * @brief Unit tests for native date support and date functions.
 */

#include "test.h"
#include <cdsl/cdsl.h>
#include <cdsl/context.h>
#include <cdsl/ast.h>
#include <cdsl/schema.h>
#include <cdsl/execution.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static void test_date_parsing(void);
static void test_date_comparison(void);
static void test_date_builtins(void);
static void test_date_arithmetic(void);

static void
test_date_parsing()
{
	TEST_BEGIN("Date literal parsing");

	const char* dsl = "RULE date_test { WHEN @2026-05-30 == @2026-05-30 THEN block(\"ok\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl);
	TEST_ASSERT_NOT_NULL(rule, "Rule with date literal should parse");

	TEST_ASSERT(rule->when_expr->type == CDSL_EXPR_BINARY, "Should be binary expression");
	TEST_ASSERT(rule->when_expr->data.binary.left->type == CDSL_EXPR_DATE,
		    "Left should be date literal");

	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_date_comparison()
{
	TEST_BEGIN("Date comparison operators");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	const char* dsls[] = {
	    "RULE r1 { WHEN @2026-01-01 < @2026-12-31 THEN block() }",
	    "RULE r2 { WHEN @2026-12-31 > @2026-01-01 THEN block() }",
	    "RULE r3 { WHEN @2026-05-30 == @2026-05-30 THEN block() }",
	    "RULE r4 { WHEN @2026-05-30 != @2026-05-31 THEN block() }",
	    "RULE r5 { WHEN @2026-05-30 12:00:00 > @2026-05-30 08:00:00 THEN block() }"};

	for (int i = 0; i < 5; i++) {
		cdsl_rule_t* rule = cdsl_parse_string(dsls[i]);
		TEST_ASSERT_NOT_NULL(rule, "Rule should parse");
		cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
		TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "Comparison should be true");
		cdsl_report_free(rpt);
		cdsl_free_rule(rule);
	}

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_date_builtins()
{
	TEST_BEGIN("Date built-in functions");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	// Test now()
	cdsl_rule_t* r_now =
	    cdsl_parse_string("RULE r_now { WHEN now() > @2000-01-01 THEN block() }");
	cdsl_rule_report_t* rpt_now = cdsl_vm_execute(vm, r_now, ctx);
	TEST_ASSERT_INT(rpt_now->status, CDSL_STATUS_FAILED, "now() should be after 2000");

	// Test days_between()
	cdsl_rule_t* r_days = cdsl_parse_string(
	    "RULE r_days { WHEN days_between(@2026-01-11, @2026-01-01) == 10 THEN block() }");
	cdsl_rule_report_t* rpt_days = cdsl_vm_execute(vm, r_days, ctx);
	TEST_ASSERT_INT(rpt_days->status, CDSL_STATUS_FAILED, "days_between should return 10");

	cdsl_report_free(rpt_now);
	cdsl_report_free(rpt_days);
	cdsl_free_rule(r_now);
	cdsl_free_rule(r_days);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main()
{
	printf("Running date support tests...\n");
	test_date_parsing();
	test_date_comparison();
	test_date_builtins();
	test_date_arithmetic();
	TEST_SUMMARY();
	TEST_EXIT();
}

void
test_date_arithmetic(void)
{
	TEST_BEGIN("date arithmetic in expressions");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "start", CDSL_TYPE_DATE);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_date(ctx, "start", (time_t)1704067200);

	cdsl_vm_t* vm = cdsl_vm_create(schema);

	/* DATE + INT: 2024-01-01 + 30 → 2024-01-31 */
	cdsl_rule_t* r1 =
	    cdsl_parse_string("RULE da1 { WHEN start + 30 > @2024-01-30 THEN block(\"ok\") }");
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, r1, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "DATE + 30 works");
	cdsl_report_free(rpt);
	cdsl_free_rule(r1);

	/* DATE - INT: 2024-01-01 - 30 */
	cdsl_rule_t* r2 =
	    cdsl_parse_string("RULE da2 { WHEN start - 30 < @2024-01-01 THEN block(\"ok\") }");
	rpt = cdsl_vm_execute(vm, r2, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "DATE - 30 works");
	cdsl_report_free(rpt);
	cdsl_free_rule(r2);

	/* DATE - DATE → INT (days): use simple rule to verify */
	cdsl_rule_t* r3 = cdsl_parse_string(
	    "RULE da3 { WHEN @2024-01-10 - @2024-01-01 == 9 THEN block(\"ok\") }");
	rpt = cdsl_vm_execute(vm, r3, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "DATE - DATE = 9 days");
	cdsl_report_free(rpt);
	cdsl_free_rule(r3);

	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_END();
}
