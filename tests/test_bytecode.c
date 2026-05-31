/**
 * @file test_bytecode.c
 * @brief Unit tests for the bytecode compiler and stack VM.
 */

#include "test.h"
#include "cdsl/ast.h"
#include "cdsl/schema.h"
#include "cdsl/execution.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int action_called;
static void
test_cb(const char* name, cdsl_arg_node_t* args, void* ud)
{
	(void)name;
	(void)args;
	(void)ud;
	action_called = 1;
}

void
test_bytecode_simple(void)
{
	TEST_BEGIN("bytecode simple rules");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "a", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "block", test_cb);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "a", 10);

	/* Parse and compile to bytecode */
	cdsl_rule_t* rule = cdsl_parse_string("RULE r { WHEN a > 5 THEN block(\"ok\") }");
	TEST_ASSERT_NOT_NULL(rule, "rule parsed");

	cdsl_bytecode_t bc = {0};
	int ok = cdsl_bytecode_compile(rule, schema, &bc);
	TEST_ASSERT_INT(ok, 1, "bytecode compiled");

	/* Execute bytecode directly */
	action_called = 0;
	cdsl_value_t result = cdsl_bytecode_execute(vm, &bc, ctx);
	TEST_ASSERT_INT(result.data.bool_val, 1, "a>5 is true via bytecode");

	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_bytecode_arithmetic(void)
{
	TEST_BEGIN("bytecode arithmetic and constant folding");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "block", test_cb);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* 2+3 should be constant-folded to 5 */
	cdsl_rule_t* r1 = cdsl_parse_string("RULE r { WHEN 2 + 3 == 5 THEN block(\"ok\") }");
	TEST_ASSERT_NOT_NULL(r1, "rule parsed");
	cdsl_bytecode_t bc1 = {0};
	cdsl_bytecode_compile(r1, schema, &bc1);
	cdsl_value_t v1 = cdsl_bytecode_execute(vm, &bc1, ctx);
	TEST_ASSERT_INT(v1.data.bool_val, 1, "2+3==5 folded to true");
	cdsl_bytecode_free(&bc1);
	cdsl_free_rule(r1);

	/* Float arithmetic */
	cdsl_rule_t* r2 =
	    cdsl_parse_string("RULE r { WHEN 3.14 * 2.0 == 6.28 THEN block(\"ok\") }");
	TEST_ASSERT_NOT_NULL(r2, "float rule");
	cdsl_bytecode_t bc2 = {0};
	cdsl_bytecode_compile(r2, schema, &bc2);
	cdsl_value_t v2 = cdsl_bytecode_execute(vm, &bc2, ctx);
	TEST_ASSERT_INT(v2.data.bool_val, 1, "3.14*2==6.28");
	cdsl_bytecode_free(&bc2);
	cdsl_free_rule(r2);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_bytecode_short_circuit(void)
{
	TEST_BEGIN("bytecode short-circuit AND/OR");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* AND short-circuit: true && true */
	cdsl_rule_t* r1 = cdsl_parse_string("RULE r { WHEN 1 == 1 AND 2 == 2 THEN block(\"ok\") }");
	cdsl_bytecode_t bc1 = {0};
	cdsl_bytecode_compile(r1, schema, &bc1);
	cdsl_value_t v1 = cdsl_bytecode_execute(vm, &bc1, ctx);
	TEST_ASSERT_INT(v1.data.bool_val, 1, "AND true&&true");
	cdsl_bytecode_free(&bc1);
	cdsl_free_rule(r1);

	/* AND short-circuit: false && anything */
	cdsl_rule_t* r2 = cdsl_parse_string("RULE r { WHEN 1 == 2 AND 3 == 3 THEN block(\"ok\") }");
	cdsl_bytecode_t bc2 = {0};
	cdsl_bytecode_compile(r2, schema, &bc2);
	cdsl_value_t v2 = cdsl_bytecode_execute(vm, &bc2, ctx);
	TEST_ASSERT_INT(v2.data.bool_val, 0, "AND false short-circuits");
	cdsl_bytecode_free(&bc2);
	cdsl_free_rule(r2);

	/* OR short-circuit */
	cdsl_rule_t* r3 = cdsl_parse_string("RULE r { WHEN 1 == 1 OR 1 == 2 THEN block(\"ok\") }");
	cdsl_bytecode_t bc3 = {0};
	cdsl_bytecode_compile(r3, schema, &bc3);
	cdsl_value_t v3 = cdsl_bytecode_execute(vm, &bc3, ctx);
	TEST_ASSERT_INT(v3.data.bool_val, 1, "OR true short-circuits");
	cdsl_bytecode_free(&bc3);
	cdsl_free_rule(r3);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_bytecode_builtin(void)
{
	TEST_BEGIN("bytecode built-in function calls");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* strlen() via bytecode */
	cdsl_rule_t* r1 =
	    cdsl_parse_string("RULE r { WHEN strlen(\"hello\") == 5 THEN block(\"ok\") }");
	cdsl_bytecode_t bc1 = {0};
	cdsl_bytecode_compile(r1, schema, &bc1);
	cdsl_value_t v1 = cdsl_bytecode_execute(vm, &bc1, ctx);
	TEST_ASSERT_INT(v1.data.bool_val, 1, "strlen via bytecode");
	cdsl_bytecode_free(&bc1);
	cdsl_free_rule(r1);

	/* contains() via bytecode */
	cdsl_rule_t* r2 = cdsl_parse_string(
	    "RULE r { WHEN contains(\"hello world\", \"world\") THEN block(\"ok\") }");
	cdsl_bytecode_t bc2 = {0};
	cdsl_bytecode_compile(r2, schema, &bc2);
	cdsl_value_t v2 = cdsl_bytecode_execute(vm, &bc2, ctx);
	TEST_ASSERT_INT(v2.data.bool_val, 1, "contains via bytecode");
	cdsl_bytecode_free(&bc2);
	cdsl_free_rule(r2);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_bytecode_vars(void)
{
	TEST_BEGIN("bytecode variable lookups");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "y", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "name", CDSL_TYPE_STRING);
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "x", 42);
	cdsl_context_set_float(ctx, "y", 3.14);
	cdsl_context_set_string(ctx, "name", "test");

	/* Int variable */
	cdsl_rule_t* r1 = cdsl_parse_string("RULE r { WHEN x == 42 THEN block(\"ok\") }");
	cdsl_bytecode_t bc1 = {0};
	cdsl_bytecode_compile(r1, schema, &bc1);
	cdsl_value_t v1 = cdsl_bytecode_execute(vm, &bc1, ctx);
	TEST_ASSERT_INT(v1.data.bool_val, 1, "x==42 via bytecode");
	cdsl_bytecode_free(&bc1);
	cdsl_free_rule(r1);

	/* Float variable */
	cdsl_rule_t* r2 = cdsl_parse_string("RULE r { WHEN y > 3.0 THEN block(\"ok\") }");
	cdsl_bytecode_t bc2 = {0};
	cdsl_bytecode_compile(r2, schema, &bc2);
	cdsl_value_t v2 = cdsl_bytecode_execute(vm, &bc2, ctx);
	TEST_ASSERT_INT(v2.data.bool_val, 1, "y>3.0 via bytecode");
	cdsl_bytecode_free(&bc2);
	cdsl_free_rule(r2);

	/* String comparison */
	cdsl_rule_t* r3 = cdsl_parse_string("RULE r { WHEN name == \"test\" THEN block(\"ok\") }");
	cdsl_bytecode_t bc3 = {0};
	cdsl_bytecode_compile(r3, schema, &bc3);
	cdsl_value_t v3 = cdsl_bytecode_execute(vm, &bc3, ctx);
	TEST_ASSERT_INT(v3.data.bool_val, 1, "string== via bytecode");
	cdsl_bytecode_free(&bc3);
	cdsl_free_rule(r3);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_bytecode_date_arith(void)
{
	TEST_BEGIN("bytecode date arithmetic");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* DATE - DATE */
	cdsl_rule_t* r1 =
	    cdsl_parse_string("RULE r { WHEN @2024-01-10 - @2024-01-01 == 9 THEN block(\"ok\") }");
	TEST_ASSERT_NOT_NULL(r1, "date rule");
	cdsl_bytecode_t bc1 = {0};
	cdsl_bytecode_compile(r1, schema, &bc1);
	cdsl_value_t v1 = cdsl_bytecode_execute(vm, &bc1, ctx);
	TEST_ASSERT_INT(v1.data.bool_val, 1, "DATE-DATE via bytecode");
	cdsl_bytecode_free(&bc1);
	cdsl_free_rule(r1);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main(void)
{
	printf("========================================\n");
	printf("  Bytecode VM Unit Tests\n");
	printf("========================================\n");

	test_bytecode_simple();
	test_bytecode_arithmetic();
	test_bytecode_short_circuit();
	test_bytecode_builtin();
	test_bytecode_vars();
	test_bytecode_date_arith();

	TEST_SUMMARY();
	TEST_EXIT();
}
