#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "cdsl/cdsl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_jit_basic_execution(void)
{
	printf("Running test_jit_basic_execution...\n");
	cdsl_schema_t* schema = cdsl_schema_create();
	assert(schema != NULL);
	cdsl_schema_register_var(schema, "age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_action(schema, "pass", CDSL_TYPE_VOID, 0);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	assert(vm != NULL);

	const char* dsl = "RULE basic { WHEN age > 18 && score >= 80.5 THEN pass() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	assert(rule != NULL);

	cdsl_bytecode_t bc = {0};
	assert(cdsl_bytecode_compile(rule, schema, &bc) == 1);

	cdsl_jit_code_t* jit = cdsl_jit_compile(vm, &bc);
	assert(jit != NULL);

	cdsl_context_t* ctx1 = cdsl_context_create(schema);
	cdsl_context_set_int(ctx1, "age", 25);
	cdsl_context_set_float(ctx1, "score", 90.0);

	cdsl_value_t res1;
	assert(cdsl_jit_execute(jit, vm, ctx1, &res1) == 0);
	assert(res1.type == CDSL_TYPE_BOOL);
	assert(res1.data.bool_val == 1);

	cdsl_context_t* ctx2 = cdsl_context_create(schema);
	cdsl_context_set_int(ctx2, "age", 16);
	cdsl_context_set_float(ctx2, "score", 95.0);

	cdsl_value_t res2;
	assert(cdsl_jit_execute(jit, vm, ctx2, &res2) == 0);
	assert(res2.type == CDSL_TYPE_BOOL);
	assert(res2.data.bool_val == 0);

	cdsl_context_free(ctx1);
	cdsl_context_free(ctx2);
	cdsl_jit_code_free(jit);
	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
}

static void
test_penta_engine_equivalence(void)
{
	printf("Running test_penta_engine_equivalence across AST, Bytecode, Threaded, JIT, "
	       "Columnar...\n");
	cdsl_schema_t* schema = cdsl_schema_create();
	assert(schema != NULL);
	cdsl_schema_register_var(schema, "amount", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "ratio", CDSL_TYPE_FLOAT);
	cdsl_schema_register_action(schema, "ok", CDSL_TYPE_VOID, 0);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	assert(vm != NULL);

	const char* test_rules[] = {
	    "RULE r1 { WHEN amount > 100 THEN ok() }",
	    "RULE r2 { WHEN ratio >= 0.5 THEN ok() }",
	    "RULE r3 { WHEN amount > 50 && ratio < 0.8 THEN ok() }",
	};
	size_t num_rules = sizeof(test_rules) / sizeof(test_rules[0]);

	for (size_t i = 0; i < num_rules; i++) {
		const char* dsl_src = test_rules[i];
		cdsl_rule_t* rule = cdsl_parse_string(dsl_src, NULL);
		assert(rule != NULL && rule->when_expr != NULL);

		cdsl_context_t* ctx = cdsl_context_create(schema);
		cdsl_context_set_int(ctx, "amount", 120);
		cdsl_context_set_float(ctx, "ratio", 0.6);

		/* 1. AST Engine via standard VM execute */
		cdsl_rule_report_t* rpt_ast = cdsl_vm_execute(vm, rule, ctx);
		assert(rpt_ast != NULL);

		/* 2. Bytecode Engine */
		cdsl_bytecode_t bc = {0};
		assert(cdsl_bytecode_compile(rule, schema, &bc) == 1);
		cdsl_value_t v_bc = cdsl_bytecode_execute(vm, &bc, ctx);

		/* 3. Threaded Engine */
		cdsl_value_t v_threaded = cdsl_bytecode_execute_threaded(vm, &bc, ctx);

		/* 4. JIT Engine */
		cdsl_jit_code_t* jit = cdsl_jit_compile(vm, &bc);
		assert(jit != NULL);
		cdsl_value_t v_jit;
		assert(cdsl_jit_execute(jit, vm, ctx, &v_jit) == 0);

		/* Verify semantic equivalence */
		assert(v_bc.type == CDSL_TYPE_BOOL);
		assert(v_threaded.type == CDSL_TYPE_BOOL);
		assert(v_jit.type == CDSL_TYPE_BOOL);
		assert(v_bc.data.bool_val == v_threaded.data.bool_val);
		assert(v_threaded.data.bool_val == v_jit.data.bool_val);
		assert((rpt_ast->status == CDSL_STATUS_FAILED) == (v_jit.data.bool_val == 1));

		/* 5. Columnar Engine */
		cdsl_columnar_ctx_t* col_ctx = cdsl_columnar_create(1);
		int int_arr[1] = {120};
		double dbl_arr[1] = {0.6};
		assert(cdsl_columnar_add_int(col_ctx, "amount", int_arr) == 1);
		assert(cdsl_columnar_add_float(col_ctx, "ratio", dbl_arr) == 1);

		uint8_t matches[1] = {0};
		int64_t matched_count = cdsl_columnar_eval_rule(rule, col_ctx, matches);
		assert(matched_count == (v_jit.data.bool_val ? 1 : 0));
		assert(matches[0] == (uint8_t)v_jit.data.bool_val);

		cdsl_columnar_free(col_ctx);
		cdsl_jit_code_free(jit);
		cdsl_bytecode_free(&bc);
		cdsl_report_free(rpt_ast);
		cdsl_context_free(ctx);
		cdsl_free_rule(rule);
	}

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
}

static void
test_jit_vm_integration(void)
{
	printf("Running test_jit_vm_integration...\n");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "reward", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	assert(vm != NULL);

	assert(cdsl_vm_is_jit_enabled(vm) == 0);
	cdsl_vm_set_jit_enabled(vm, 1);
	assert(cdsl_vm_is_jit_enabled(vm) == 1);

	const char* dsl = "RULE r_integ { WHEN score > 90 THEN reward(500) }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	assert(rule != NULL);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "score", 95);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	assert(rpt != NULL);
	assert(rpt->status == CDSL_STATUS_FAILED);

	cdsl_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
}

int
main(void)
{
	test_jit_basic_execution();
	test_penta_engine_equivalence();
	test_jit_vm_integration();
	printf("All JIT tests passed successfully!\n");
	return 0;
}
