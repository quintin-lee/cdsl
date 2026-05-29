#include "test.h"
#include "ast.h"
#include "abstract.h"
#include "execution.h"
#include <stdlib.h>
#include <string.h>

void
test_builtin_strlen(void)
{
	TEST_BEGIN("built-in strlen");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* RULE test { WHEN strlen("hello") == 5 THEN ... } */
	cdsl_rule_t* rule = cdsl_parse_string("RULE test { WHEN strlen(\"hello\") == 5 THEN block(\"ok\") }");
	TEST_ASSERT_NOT_NULL(rule, "rule parse ok");
	
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execution ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "strlen(5) == 5 is true, so rule triggers (FAILED status)");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_builtin_contains(void)
{
	TEST_BEGIN("built-in contains");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule = cdsl_parse_string("RULE test { WHEN contains(\"hello world\", \"world\") THEN block(\"ok\") }");
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "contains('hello world', 'world') is true");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_builtin_date_cmp(void)
{
	TEST_BEGIN("built-in is_before/is_after");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* is_before */
	cdsl_rule_t* r1 = cdsl_parse_string("RULE r1 { WHEN is_before(\"2023-01-01\", \"2024-01-01\") THEN block(\"ok\") }");
	cdsl_rule_report_t* rpt1 = cdsl_vm_execute(vm, r1, ctx);
	TEST_ASSERT_INT(rpt1->status, CDSL_STATUS_FAILED, "2023 is before 2024");

	/* is_after */
	cdsl_rule_t* r2 = cdsl_parse_string("RULE r2 { WHEN is_after(\"2025-05-29\", \"2024-01-01\") THEN block(\"ok\") }");
	cdsl_rule_report_t* rpt2 = cdsl_vm_execute(vm, r2, ctx);
	TEST_ASSERT_INT(rpt2->status, CDSL_STATUS_FAILED, "2025 is after 2024");

	cdsl_report_free(rpt1);
	cdsl_report_free(rpt2);
	cdsl_free_rule(r1);
	cdsl_free_rule(r2);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main(void)
{
	printf("========================================\n");
	printf("  Built-in Functions Unit Tests\n");
	printf("========================================\n");

	test_builtin_strlen();
	test_builtin_contains();
	test_builtin_date_cmp();

	TEST_SUMMARY();
	TEST_EXIT();
}
