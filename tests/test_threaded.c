/**
 * @file tests/test_threaded.c
 * @brief Unit tests for the direct-threaded bytecode VM execution engine.
 */

#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/bytecode.h"
#include <string.h>

static void
test_threaded_simple_comparison(void)
{
	TEST_BEGIN("threaded: simple integer comparison");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "pass", CDSL_TYPE_VOID, 0);

	cdsl_rule_t* rule = cdsl_parse_string("RULE r { WHEN score >= 80 THEN pass() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse rule");

	cdsl_bytecode_t bc = {0};
	int ok = cdsl_bytecode_compile(rule, schema, &bc);
	TEST_ASSERT_INT(ok, 1, "compile to bytecode");

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "score", 95);

	cdsl_value_t res = cdsl_bytecode_execute_threaded(vm, &bc, ctx);
	TEST_ASSERT_INT(res.type, CDSL_TYPE_BOOL, "result is bool");
	TEST_ASSERT_INT(res.data.bool_val, 1, "score >= 80 is true");

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_threaded_arithmetic_and_logic(void)
{
	TEST_BEGIN("threaded: arithmetic and logical combinations");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "a", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "b", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "pass", CDSL_TYPE_VOID, 0);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE r { WHEN (a + b * 2 == 25) AND !(a > b) THEN pass() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse arithmetic logic rule");

	cdsl_bytecode_t bc = {0};
	int ok = cdsl_bytecode_compile(rule, schema, &bc);
	TEST_ASSERT_INT(ok, 1, "compile to bytecode");

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "a", 5);
	cdsl_context_set_int(ctx, "b", 10);

	cdsl_value_t res = cdsl_bytecode_execute_threaded(vm, &bc, ctx);
	TEST_ASSERT_INT(res.data.bool_val, 1, "a + b * 2 == 25 AND !(a > b) is true");

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_threaded_metric_rule(void)
{
	TEST_BEGIN("threaded: metric scoring rule execution");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "val", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);

	const char* dsl = "RULE m_rule {\n"
			  "  METRIC m1 {\n"
			  "    META { weight = \"100\" }\n"
			  "    CASE val >= 100 THEN score(100)\n"
			  "    CASE val >= 50 THEN score(50)\n"
			  "    DEFAULT score(0)\n"
			  "  }\n"
			  "}";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse metric rule");

	cdsl_bytecode_t bc = {0};
	cdsl_bytecode_compile(rule, schema, &bc);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "val", 75);

	cdsl_rule_report_t* rpt = cdsl_bytecode_execute_rule(vm, &bc, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute metric rule");
	TEST_ASSERT_INT(rpt->total_obtained_score, 50, "metric score obtained 50");

	cdsl_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main(void)
{
	printf("Running direct-threaded bytecode VM tests...\n");
	test_threaded_simple_comparison();
	test_threaded_arithmetic_and_logic();
	test_threaded_metric_rule();
	TEST_SUMMARY();
	TEST_EXIT();
}
