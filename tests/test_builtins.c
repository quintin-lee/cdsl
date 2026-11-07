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
 * @brief Regression test: string-returning built-ins (uppercase/lowercase/trim)
 *        must not clobber a previously-returned string. Before the fix they
 *        returned a shared static THREAD_LOCAL buffer, so the second call
 *        overwrote the first result. Now each call returns an independent
 *        heap allocation marked owned_string=1, freed by cdsl_value_free.
 */
void
test_builtin_string_isolation(void)
{
	TEST_BEGIN("built-in string isolation (no buffer clobber)");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* Fetch the registered uppercase() callback. */
	cdsl_func_cb_t up = NULL;
	for (cdsl_func_entry_t* f = vm->functions; f; f = f->next) {
		if (strcmp(f->func_name, "uppercase") == 0) {
			up = f->cb;
			break;
		}
	}
	TEST_ASSERT_NOT_NULL(up, "uppercase builtin registered");
	if (!up) {
		cdsl_context_free(ctx);
		cdsl_vm_free(vm);
		cdsl_schema_free(schema);
		TEST_END();
		return;
	}

	/* First call: uppercase("abc") -> "ABC" (heap-allocated, owned) */
	cdsl_expr_node_t* s1 = cdsl_create_expr_string(strdup("abc"), 0, 0);
	cdsl_arg_node_t* a1 = cdsl_create_arg(s1);
	cdsl_value_t r1 = up("uppercase", a1, ctx, vm);
	TEST_ASSERT(r1.type == CDSL_TYPE_STRING && strcmp(r1.data.string_val, "ABC") == 0,
		    "first uppercase == 'ABC'");
	char* first_ptr = r1.data.string_val; /* owned heap buffer */

	/* Second call: uppercase("xyz") -> "XYZ" (independent heap buffer) */
	cdsl_expr_node_t* s2 = cdsl_create_expr_string(strdup("xyz"), 0, 0);
	cdsl_arg_node_t* a2 = cdsl_create_arg(s2);
	cdsl_value_t r2 = up("uppercase", a2, ctx, vm);
	TEST_ASSERT(r2.type == CDSL_TYPE_STRING && strcmp(r2.data.string_val, "XYZ") == 0,
		    "second uppercase == 'XYZ'");

	/* The first result must still read "ABC" (independent allocation). */
	TEST_ASSERT(strcmp(first_ptr, "ABC") == 0, "first result not clobbered by second call");
	TEST_ASSERT(first_ptr != r2.data.string_val, "each call returns a distinct buffer");

	free(first_ptr);
	free(r2.data.string_val);
	/* Release the argument/expr nodes created for the two calls. */
	cdsl_free_arg(a1);
	cdsl_free_arg(a2);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

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
	cdsl_rule_t* r_trim = cdsl_parse_string(
	    "RULE r { WHEN trim(\"  abc  \") == \"abc\" THEN block(\"ok\") }", NULL);
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
					       "abs(-5) == 5 THEN score(100) DEFAULT score(0) } }",
					       NULL);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, r_abs, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "abs(-5) == 5");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_abs);

	cdsl_rule_t* r_min =
	    cdsl_parse_string("RULE r { META { w = \"100\" } METRIC m { CASE min(10, 3) == 3 THEN "
			      "score(100) DEFAULT score(0) } }",
			      NULL);
	rpt = cdsl_vm_execute(vm, r_min, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "min(10,3) == 3");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_min);

	cdsl_rule_t* r_max =
	    cdsl_parse_string("RULE r { META { w = \"100\" } METRIC m { CASE max(10, 3) == 10 THEN "
			      "score(100) DEFAULT score(0) } }",
			      NULL);
	rpt = cdsl_vm_execute(vm, r_max, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "max(10,3) == 10");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_max);

	cdsl_rule_t* r_round =
	    cdsl_parse_string("RULE r { META { w = \"100\" } METRIC m { CASE round(3.14) == 3 THEN "
			      "score(100) DEFAULT score(0) } }",
			      NULL);
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

	cdsl_rule_t* r_typeof = cdsl_parse_string(
	    "RULE r { WHEN typeof(\"hello\") == \"STRING\" THEN block(\"ok\") }", NULL);
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
			      "7)) THEN block(\"ok\") }",
			      NULL);
	rpt = cdsl_vm_execute(vm, r_dadd, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "date_add adds 7 days");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_dadd);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_builtin_approx_eq(void)
{
	TEST_BEGIN("built-in approx_eq");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* 1. 双参默认容差: 0.1 + 0.2 近似等于 0.3 */
	cdsl_rule_t* r1 = cdsl_parse_string(
	    "RULE r1 { WHEN approx_eq(0.1 + 0.2, 0.3) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r1, "parse r1 ok");
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, r1, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r1 ok");
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_FAILED, "approx_eq(0.1+0.2, 0.3) is true -> FAILED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r1);

	/* 2. 三参自定义容差: 宽松匹配成功 (10.0 与 10.05, 容差 0.1) */
	cdsl_rule_t* r2 = cdsl_parse_string(
	    "RULE r2 { WHEN approx_eq(10.0, 10.05, 0.1) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r2, "parse r2 ok");
	rpt = cdsl_vm_execute(vm, r2, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r2 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "approx_eq with eps=0.1 matches");
	cdsl_report_free(rpt);
	cdsl_free_rule(r2);

	/* 3. 三参自定义容差: 严格匹配失败 (10.0 与 10.05, 容差 0.001: 相对差 0.5% > 0.1%) */
	cdsl_rule_t* r3 = cdsl_parse_string(
	    "RULE r3 { WHEN approx_eq(10.0, 10.05, 0.001) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r3, "parse r3 ok");
	rpt = cdsl_vm_execute(vm, r3, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r3 ok");
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_PASSED, "approx_eq with eps=0.001 does not match -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r3);

	/* 4. 大数相对尺度匹配 (1e12 与 1e12 + 100, eps 1e-9) */
	cdsl_rule_t* r4 = cdsl_parse_string("RULE r4 { WHEN approx_eq(1000000000000.0, "
					    "1000000000100.0, 0.000000001) THEN block(\"ok\") }",
					    NULL);
	TEST_ASSERT_NOT_NULL(r4, "parse r4 ok");
	rpt = cdsl_vm_execute(vm, r4, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r4 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "large number relative approx_eq matches");
	cdsl_report_free(rpt);
	cdsl_free_rule(r4);

	/* 5. 跨类型参数 (整型与浮点提升) */
	cdsl_rule_t* r5 = cdsl_parse_string(
	    "RULE r5 { WHEN approx_eq(5, 5.0000000001) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r5, "parse r5 ok");
	rpt = cdsl_vm_execute(vm, r5, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r5 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "mixed int/float approx_eq matches");
	cdsl_report_free(rpt);
	cdsl_free_rule(r5);

	/* 6. 异常参数防护: 负容差视为非法返回 false */
	cdsl_rule_t* r6 = cdsl_parse_string(
	    "RULE r6 { WHEN approx_eq(1.0, 1.0, -0.01) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r6, "parse r6 ok");
	rpt = cdsl_vm_execute(vm, r6, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r6 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "negative eps returns false -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r6);

	/* 7. 参数数量不足: 仅 1 个参数返回 false */
	cdsl_rule_t* r7 =
	    cdsl_parse_string("RULE r7 { WHEN approx_eq(1.0) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r7, "parse r7 ok");
	rpt = cdsl_vm_execute(vm, r7, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r7 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "too few args returns false -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r7);

	/* 8. 参数数量过多: 4 个参数返回 false */
	cdsl_rule_t* r8 = cdsl_parse_string(
	    "RULE r8 { WHEN approx_eq(1.0, 1.0, 0.1, 0.2) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r8, "parse r8 ok");
	rpt = cdsl_vm_execute(vm, r8, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r8 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "too many args returns false -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r8);

	/* 9. 非数值类型: 字符串参数返回 false */
	cdsl_rule_t* r9 = cdsl_parse_string(
	    "RULE r9 { WHEN approx_eq(\"hello\", 1.0) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r9, "parse r9 ok");
	rpt = cdsl_vm_execute(vm, r9, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r9 ok");
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_PASSED, "non-numeric args returns false -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r9);

	/* 10. 极小数尺度: 1e-10 与 2e-10 在严格容差 1e-11 下不匹配 */
	cdsl_rule_t* r10 = cdsl_parse_string("RULE r10 { WHEN approx_eq(0.0000000001, "
					     "0.0000000002, 0.00000000001) THEN block(\"ok\") }",
					     NULL);
	TEST_ASSERT_NOT_NULL(r10, "parse r10 ok");
	rpt = cdsl_vm_execute(vm, r10, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r10 ok");
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_PASSED, "small numbers with strict eps don't match -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r10);

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
	test_builtin_string_isolation();
	test_builtin_approx_eq();

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

	cdsl_rule_t* r_typeof = cdsl_parse_string(
	    "RULE r { WHEN typeof([1, 2]) == \"ARRAY\" THEN block(\"ok\") }", NULL);
	rpt = cdsl_vm_execute(vm, r_typeof, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "typeof([1, 2]) == ARRAY");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_typeof);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}
