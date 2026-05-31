#include "test.h"
#include "cdsl/cdsl.h"

static void
test_array_basic(void)
{
	TEST_BEGIN("array basic");
	const char* dsl = "RULE array_rule { WHEN count([1, 2, 3]) == 3 THEN accept() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	// Simple rule execution
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "Rule should pass");

	cdsl_report_free(rpt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_array_sum(void)
{
	TEST_BEGIN("array sum");
	const char* dsl = "RULE sum_rule { WHEN sum([1, 2, 3]) == 6 THEN accept() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "Rule should pass");

	cdsl_report_free(rpt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	TEST_END();
}

int
main()
{
	test_array_basic();
	test_array_sum();
	return 0;
}
