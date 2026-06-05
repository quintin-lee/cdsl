#include "test.h"
#include "cdsl/cdsl.h"

static void
test_array_count_pass(void)
{
	TEST_BEGIN("array count - pass");
	// WHEN condition is FALSE → no violation → PASSED
	const char* dsl = "RULE r { WHEN count([1, 2, 3]) != 3 THEN fail() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL");
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_PASSED, "count([1,2,3])==3, condition false → PASSED");

	cdsl_report_free(rpt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_array_count_fail(void)
{
	TEST_BEGIN("array count - fail");
	// WHEN condition is TRUE → violation detected → FAILED
	const char* dsl = "RULE r { WHEN count([1, 2, 3]) == 3 THEN fail() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL");
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_FAILED, "count([1,2,3])==3, condition true → FAILED");

	cdsl_report_free(rpt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_array_sum_pass(void)
{
	TEST_BEGIN("array sum - pass");
	// WHEN condition is FALSE → no violation → PASSED
	const char* dsl = "RULE r { WHEN sum([1, 2, 3]) != 6 THEN fail() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL");
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_PASSED, "sum([1,2,3])==6, condition false → PASSED");

	cdsl_report_free(rpt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_array_sum_fail(void)
{
	TEST_BEGIN("array sum - fail");
	// WHEN condition is TRUE → violation detected → FAILED
	const char* dsl = "RULE r { WHEN sum([1, 2, 3]) == 6 THEN fail() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL");
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_FAILED, "sum([1,2,3])==6, condition true → FAILED");

	cdsl_report_free(rpt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_array_empty(void)
{
	TEST_BEGIN("array empty count");
	const char* dsl = "RULE r { WHEN count([]) != 0 THEN fail() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse empty array rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report should not be NULL");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "count([])==0, condition false → PASSED");

	cdsl_report_free(rpt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	TEST_END();
}

int
main()
{
	test_array_count_pass();
	test_array_count_fail();
	test_array_sum_pass();
	test_array_sum_fail();
	test_array_empty();
	return 0;
}
