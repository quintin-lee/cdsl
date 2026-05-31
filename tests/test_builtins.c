/**
 * @file test_builtins.c
 * @brief Unit tests for built-in DSL functions.
 *
 * Tests all built-in functions (string, date, math, type introspection)
 * using simple WHEN/THEN rules.
 */

#include "test.h"
#include "cdsl/ast.h"
#include "cdsl/schema.h"
#include "cdsl/execution.h"
#include <stdlib.h>
#include <string.h>

void test_builtin_array(void);

/**
 * @brief Test the strlen built-in function.
 */
void
test_builtin_strlen(void)
{
	TEST_BEGIN("built-in strlen");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE test { WHEN strlen(\"hello\") == 5 THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "rule parse ok");

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execution ok");
	TEST_ASSERT_INT(rpt->status,
			CDSL_STATUS_FAILED,
			"strlen(5) == 5 is true, so rule triggers (FAILED status)");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/**
 * @brief Test the contains built-in function.
 */
void
test_builtin_contains(void)
{
	TEST_BEGIN("built-in contains");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE test { WHEN contains(\"hello world\", \"world\") THEN block(\"ok\") }", NULL);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_FAILED, "contains('hello world', 'world') is true");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/**
 * @brief Test the is_before and is_after date comparison functions.
 */
void
test_builtin_date_cmp(void)
{
	TEST_BEGIN("built-in is_before/is_after");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* r1 = cdsl_parse_string(
	    "RULE r1 { WHEN is_before(\"2023-01-01\", \"2024-01-01\") THEN block(\"ok\") }", NULL);
	cdsl_rule_report_t* rpt1 = cdsl_vm_execute(vm, r1, ctx);
	TEST_ASSERT_INT(rpt1->status, CDSL_STATUS_FAILED, "2023 is before 2024");

	cdsl_rule_t* r2 = cdsl_parse_string(
	    "RULE r2 { WHEN is_after(\"2025-05-29\", \"2024-01-01\") THEN block(\"ok\") }", NULL);
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

/**
 * @brief Test string utility built-ins: uppercase, lowercase, trim, startswith, endswith.
 */
void
test_builtin_string_utils(void)
{
	TEST_BEGIN("built-in string utilities");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* uppercase() */
	cdsl_rule_t* r_upper = cdsl_parse_string(
	    "RULE r { WHEN uppercase(\"hello\") == \"HELLO\" THEN block(\"ok\") }", NULL);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, r_upper, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "uppercase works");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_upper);

	/* lowercase() */
	cdsl_rule_t* r_lower = cdsl_parse_string(
	    "RULE r { WHEN lowercase(\"HELLO\") == \"hello\" THEN block(\"ok\") }", NULL);
	rpt = cdsl_vm_execute(vm, r_lower, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "lowercase works");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_lower);

	/* trim() — leading/trailing spaces removed */
	cdsl_rule_t* r_trim =
	    cdsl_parse_string("RULE r { WHEN trim(\"  abc  \") == \"abc\" THEN block(\"ok\") }", NULL);
	rpt = cdsl_vm_execute(vm, r_trim, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "trim works");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_trim);

	/* startswith() */
	cdsl_rule_t* r_sw = cdsl_parse_string(
	    "RULE r { WHEN startswith(\"hello world\", \"hello\") THEN block(\"ok\") }", NULL);
	rpt = cdsl_vm_execute(vm, r_sw, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "startswith works");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_sw);

	/* endswith() */
	cdsl_rule_t* r_ew = cdsl_parse_string(
	    "RULE r { WHEN endswith(\"hello world\", \"world\") THEN block(\"ok\") }", NULL);
	rpt = cdsl_vm_execute(vm, r_ew, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "endswith works");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_ew);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/**
 * @brief Test math built-ins: abs, min, max, round.
 */
void
test_builtin_math_utils(void)
{
	TEST_BEGIN("built-in math functions");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* r_abs = cdsl_parse_string("RULE r { META { w = \"100\" } METRIC m { CASE "
					       "abs(-5) == 5 THEN score(100) DEFAULT score(0) } }", NULL);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, r_abs, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "abs(-5) == 5");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_abs);

	cdsl_rule_t* r_min =
	    cdsl_parse_string("RULE r { META { w = \"100\" } METRIC m { CASE min(10, 3) == 3 THEN "
			      "score(100) DEFAULT score(0) } }", NULL);
	rpt = cdsl_vm_execute(vm, r_min, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "min(10,3) == 3");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_min);

	cdsl_rule_t* r_max =
	    cdsl_parse_string("RULE r { META { w = \"100\" } METRIC m { CASE max(10, 3) == 10 THEN "
			      "score(100) DEFAULT score(0) } }", NULL);
	rpt = cdsl_vm_execute(vm, r_max, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "max(10,3) == 10");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_max);

	cdsl_rule_t* r_round =
	    cdsl_parse_string("RULE r { META { w = \"100\" } METRIC m { CASE round(3.14) == 3 THEN "
			      "score(100) DEFAULT score(0) } }", NULL);
	rpt = cdsl_vm_execute(vm, r_round, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "round(3.14) == 3");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_round);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/**
 * @brief Test typeof and date_add built-ins.
 */
void
test_builtin_misc(void)
{
	TEST_BEGIN("built-in typeof and date_add");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* r_typeof =
	    cdsl_parse_string("RULE r { WHEN typeof(\"hello\") == \"STRING\" THEN block(\"ok\") }", NULL);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, r_typeof, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "typeof('hello') == STRING");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_typeof);

	cdsl_rule_t* r_typeof_int =
	    cdsl_parse_string("RULE r { WHEN typeof(42) == \"INT\" THEN block(\"ok\") }", NULL);
	rpt = cdsl_vm_execute(vm, r_typeof_int, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "typeof(42) == INT");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_typeof_int);

	cdsl_rule_t* r_dadd =
	    cdsl_parse_string("RULE r { WHEN is_before(\"2024-01-01\", date_add(\"2024-01-01\", "
			      "7)) THEN block(\"ok\") }", NULL);
	rpt = cdsl_vm_execute(vm, r_dadd, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "date_add adds 7 days");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_dadd);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

/**
 * @brief Main entry: run all built-in function test cases.
 * @return 0 if all tests passed, 1 otherwise
 */
int
main(void)
{
	printf("========================================\n");
	printf("  Built-in Functions Unit Tests\n");
	printf("========================================\n");

	test_builtin_strlen();
	test_builtin_contains();
	test_builtin_date_cmp();
	test_builtin_string_utils();
	test_builtin_math_utils();
	test_builtin_misc();
	test_builtin_array();

	TEST_SUMMARY();
	TEST_EXIT();
}

/**
 * @brief Test array built-ins.
 */
void
test_builtin_array(void)
{
	TEST_BEGIN("built-in array utilities");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_t* r_count =
	    cdsl_parse_string("RULE r { WHEN count([1, 2, 3]) == 3 THEN block(\"ok\") }", NULL);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, r_count, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "count([1, 2, 3]) == 3");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_count);

	cdsl_rule_t* r_sum =
	    cdsl_parse_string("RULE r { WHEN sum([1, 2, 3]) == 6 THEN block(\"ok\") }", NULL);
	rpt = cdsl_vm_execute(vm, r_sum, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "sum([1, 2, 3]) == 6");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_sum);

	cdsl_rule_t* r_typeof =
	    cdsl_parse_string("RULE r { WHEN typeof([1, 2]) == \"ARRAY\" THEN block(\"ok\") }", NULL);
	rpt = cdsl_vm_execute(vm, r_typeof, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "typeof([1, 2]) == ARRAY");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_typeof);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}
