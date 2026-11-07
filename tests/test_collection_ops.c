#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/bytecode.h"

static void
test_range_in_pass(void)
{
	TEST_BEGIN("range in [min..max] - hit and miss");
	const char* dsl_hit = "RULE r1 { WHEN 15 in [10..20] THEN fail() }";
	cdsl_rule_t* r1 = cdsl_parse_string(dsl_hit, NULL);
	TEST_ASSERT_NOT_NULL(r1, "parse range hit rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	cdsl_rule_report_t* rpt1 = cdsl_vm_execute(vm, r1, ctx);
	TEST_ASSERT_NOT_NULL(rpt1, "execute rpt1");
	TEST_ASSERT_INT(rpt1->status, CDSL_STATUS_FAILED, "15 in [10..20] condition is true");

	const char* dsl_miss = "RULE r2 { WHEN 25 in [10..20] THEN fail() }";
	cdsl_rule_t* r2 = cdsl_parse_string(dsl_miss, NULL);
	TEST_ASSERT_NOT_NULL(r2, "parse range miss rule");
	cdsl_rule_report_t* rpt2 = cdsl_vm_execute(vm, r2, ctx);
	TEST_ASSERT_NOT_NULL(rpt2, "execute rpt2");
	TEST_ASSERT_INT(rpt2->status, CDSL_STATUS_PASSED, "25 in [10..20] condition is false");

	cdsl_report_free(rpt1);
	cdsl_report_free(rpt2);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(r1);
	cdsl_free_rule(r2);
	TEST_END();
}

static void
test_range_bytecode(void)
{
	TEST_BEGIN("range in [min..max] - bytecode engine");
	const char* dsl = "RULE r_bc { WHEN 10 in [10..20] THEN fail() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse rule");

	cdsl_bytecode_t bc = {0};
	int ok = cdsl_bytecode_compile(rule, NULL, &bc);
	TEST_ASSERT_INT(ok, 1, "compile range rule to bytecode");

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_value_t res = cdsl_bytecode_execute(vm, &bc, ctx);
	TEST_ASSERT_INT(res.data.bool_val, 1, "10 in [10..20] is true in bytecode");

	cdsl_bytecode_free(&bc);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_membership_container(void)
{
	TEST_BEGIN("membership 'in' array and string");
	// Array membership
	const char* dsl_arr = "RULE r_arr { WHEN 2 in [1, 2, 3] THEN fail() }";
	cdsl_rule_t* r_arr = cdsl_parse_string(dsl_arr, NULL);
	TEST_ASSERT_NOT_NULL(r_arr, "parse array in rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	cdsl_rule_report_t* rpt_arr = cdsl_vm_execute(vm, r_arr, ctx);
	TEST_ASSERT_NOT_NULL(rpt_arr, "rpt_arr not null");
	TEST_ASSERT_INT(rpt_arr->status, CDSL_STATUS_FAILED, "2 in [1,2,3] is true");

	// String substring membership
	const char* dsl_str = "RULE r_str { WHEN \"cat\" in \"concatenation\" THEN fail() }";
	cdsl_rule_t* r_str = cdsl_parse_string(dsl_str, NULL);
	TEST_ASSERT_NOT_NULL(r_str, "parse str in rule");
	cdsl_rule_report_t* rpt_str = cdsl_vm_execute(vm, r_str, ctx);
	TEST_ASSERT_NOT_NULL(rpt_str, "rpt_str not null");
	TEST_ASSERT_INT(rpt_str->status, CDSL_STATUS_FAILED, "'cat' in 'concatenation' is true");

	cdsl_report_free(rpt_arr);
	cdsl_report_free(rpt_str);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(r_arr);
	cdsl_free_rule(r_str);
	TEST_END();
}

static void
test_higher_order_predicates(void)
{
	TEST_BEGIN("higher-order predicates any, all, filter");
	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	// any() - true case
	const char* dsl_any1 = "RULE r_any1 { WHEN any([1, 2, 15, 4], x -> x > 10) THEN fail() }";
	cdsl_rule_t* r_any1 = cdsl_parse_string(dsl_any1, NULL);
	TEST_ASSERT_NOT_NULL(r_any1, "parse any1");
	cdsl_rule_report_t* rpt_any1 = cdsl_vm_execute(vm, r_any1, ctx);
	TEST_ASSERT_NOT_NULL(rpt_any1, "rpt_any1");
	TEST_ASSERT_INT(
	    rpt_any1->status, CDSL_STATUS_FAILED, "any([1,2,15,4], x -> x > 10) is true");

	// any() - false case
	const char* dsl_any2 = "RULE r_any2 { WHEN any([1, 2, 3, 4], x -> x > 10) THEN fail() }";
	cdsl_rule_t* r_any2 = cdsl_parse_string(dsl_any2, NULL);
	TEST_ASSERT_NOT_NULL(r_any2, "parse any2");
	cdsl_rule_report_t* rpt_any2 = cdsl_vm_execute(vm, r_any2, ctx);
	TEST_ASSERT_NOT_NULL(rpt_any2, "rpt_any2");
	TEST_ASSERT_INT(
	    rpt_any2->status, CDSL_STATUS_PASSED, "any([1,2,3,4], x -> x > 10) is false");

	// all() - true case
	const char* dsl_all1 = "RULE r_all1 { WHEN all([2, 4, 6, 8], x -> x > 0) THEN fail() }";
	cdsl_rule_t* r_all1 = cdsl_parse_string(dsl_all1, NULL);
	TEST_ASSERT_NOT_NULL(r_all1, "parse all1");
	cdsl_rule_report_t* rpt_all1 = cdsl_vm_execute(vm, r_all1, ctx);
	TEST_ASSERT_NOT_NULL(rpt_all1, "rpt_all1");
	TEST_ASSERT_INT(rpt_all1->status, CDSL_STATUS_FAILED, "all([2,4,6,8], x -> x > 0) is true");

	// all() - false case
	const char* dsl_all2 = "RULE r_all2 { WHEN all([2, -4, 6, 8], x -> x > 0) THEN fail() }";
	cdsl_rule_t* r_all2 = cdsl_parse_string(dsl_all2, NULL);
	TEST_ASSERT_NOT_NULL(r_all2, "parse all2");
	cdsl_rule_report_t* rpt_all2 = cdsl_vm_execute(vm, r_all2, ctx);
	TEST_ASSERT_NOT_NULL(rpt_all2, "rpt_all2");
	TEST_ASSERT_INT(
	    rpt_all2->status, CDSL_STATUS_PASSED, "all([2,-4,6,8], x -> x > 0) is false");

	// filter()
	const char* dsl_flt =
	    "RULE r_flt { WHEN count(filter([1, 2, 3, 4, 5], x -> x > 2)) == 3 THEN fail() }";
	cdsl_rule_t* r_flt = cdsl_parse_string(dsl_flt, NULL);
	TEST_ASSERT_NOT_NULL(r_flt, "parse flt");
	cdsl_rule_report_t* rpt_flt = cdsl_vm_execute(vm, r_flt, ctx);
	TEST_ASSERT_NOT_NULL(rpt_flt, "rpt_flt");
	TEST_ASSERT_INT(rpt_flt->status, CDSL_STATUS_FAILED, "filter count == 3");

	cdsl_report_free(rpt_any1);
	cdsl_report_free(rpt_any2);
	cdsl_report_free(rpt_all1);
	cdsl_report_free(rpt_all2);
	cdsl_report_free(rpt_flt);
	cdsl_free_rule(r_any1);
	cdsl_free_rule(r_any2);
	cdsl_free_rule(r_all1);
	cdsl_free_rule(r_all2);
	cdsl_free_rule(r_flt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	TEST_END();
}

int
main(void)
{
	printf("Running collection ops tests...\n");
	test_range_in_pass();
	test_range_bytecode();
	test_membership_container();
	test_higher_order_predicates();
	TEST_SUMMARY();
	TEST_EXIT();
}
