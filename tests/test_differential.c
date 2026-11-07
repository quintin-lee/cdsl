/**
 * @file test_differential.c
 * @brief Differential tests verifying equivalence between AST tree-walk
 *        and Bytecode VM execution paths.
 */

#include "test.h"
#include <cdsl/cdsl.h>
#include <cdsl/context.h>
#include <cdsl/ast.h>
#include <cdsl/schema.h>
#include <cdsl/execution.h>
#include <cdsl/bytecode.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
	int call_count;
	char last_action[64];
	char last_arg_str[64];
	int last_arg_int;
} test_tracker_t;

static void
test_tracking_cb(const char* action_name, cdsl_arg_node_t* args, void* ud)
{
	test_tracker_t* tr = (test_tracker_t*)ud;
	if (!tr) {
		return;
	}
	tr->call_count++;
	strncpy(tr->last_action, action_name ? action_name : "", sizeof(tr->last_action) - 1);
	if (args && args->expr) {
		if (args->expr->type == CDSL_EXPR_STRING && args->expr->data.string_val) {
			strncpy(tr->last_arg_str,
				args->expr->data.string_val,
				sizeof(tr->last_arg_str) - 1);
		} else if (args->expr->type == CDSL_EXPR_INT) {
			tr->last_arg_int = args->expr->data.int_val;
		}
	}
}

static void
assert_reports_equivalent(const cdsl_rule_report_t* a, const cdsl_rule_report_t* b)
{
	if (!a && !b) {
		return;
	}
	TEST_ASSERT_NOT_NULL(a, "AST report not null");
	TEST_ASSERT_NOT_NULL(b, "Bytecode report not null");
	if (!a || !b) {
		return;
	}

	TEST_ASSERT_INT(a->status, b->status, "Status must match");
	TEST_ASSERT_INT(
	    a->total_obtained_score, b->total_obtained_score, "Obtained score must match");
	TEST_ASSERT_INT(a->total_max_score, b->total_max_score, "Max score must match");
	TEST_ASSERT_INT(a->metric_count, b->metric_count, "Metric count must match");

	const char* a_rule = a->rule_name ? a->rule_name : "";
	const char* b_rule = b->rule_name ? b->rule_name : "";
	TEST_ASSERT_STR(a_rule, b_rule, "Rule name must match");

	const char* a_desc = a->description ? a->description : "";
	const char* b_desc = b->description ? b->description : "";
	TEST_ASSERT_STR(a_desc, b_desc, "Description must match");

	const char* a_sum = a->decision_summary ? a->decision_summary : "";
	const char* b_sum = b->decision_summary ? b->decision_summary : "";
	TEST_ASSERT_STR(a_sum, b_sum, "Decision summary must match");

	if (a->error || b->error) {
		TEST_ASSERT_NOT_NULL(a->error, "AST error not null");
		TEST_ASSERT_NOT_NULL(b->error, "Bytecode error not null");
		if (a->error && b->error) {
			TEST_ASSERT_INT(a->error->kind, b->error->kind, "Error kind must match");
		}
	}

	for (int i = 0; i < a->metric_count && i < b->metric_count; i++) {
		const cdsl_metric_result_t* ma = &a->metrics[i];
		const cdsl_metric_result_t* mb = &b->metrics[i];

		const char* ma_name = ma->metric_name ? ma->metric_name : "";
		const char* mb_name = mb->metric_name ? mb->metric_name : "";
		TEST_ASSERT_STR(ma_name, mb_name, "Metric name must match");

		const char* ma_desc = ma->description ? ma->description : "";
		const char* mb_desc = mb->description ? mb->description : "";
		TEST_ASSERT_STR(ma_desc, mb_desc, "Metric description must match");

		TEST_ASSERT_INT(ma->max_weight, mb->max_weight, "Metric max_weight must match");
		TEST_ASSERT_INT(
		    ma->score_obtained, mb->score_obtained, "Metric score_obtained must match");
		TEST_ASSERT_INT(ma->is_passed, mb->is_passed, "Metric is_passed must match");
		TEST_ASSERT_INT(ma->is_critical, mb->is_critical, "Metric is_critical must match");

		if (ma->violation_reason || mb->violation_reason) {
			const char* ra = ma->violation_reason ? ma->violation_reason : "";
			const char* rb = mb->violation_reason ? mb->violation_reason : "";
			TEST_ASSERT_STR(ra, rb, "Metric violation_reason must match");
		}
	}
}

typedef void (*setup_ctx_fn_t)(cdsl_context_t* ctx);

static void
run_diff_test(const char* name,
	      cdsl_schema_t* schema,
	      const char* dsl,
	      setup_ctx_fn_t setup_fn,
	      const char* expected_action,
	      int expected_action_count)
{
	TEST_BEGIN(name);

	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule parse succeeded");

	cdsl_bytecode_t bc = {0};
	int compiled = cdsl_bytecode_compile(rule, schema, &bc);
	TEST_ASSERT_INT(compiled, 1, "Bytecode compile succeeded");

	test_tracker_t tracker_ast = {0};
	test_tracker_t tracker_bc = {0};

	cdsl_vm_t* vm_ast = cdsl_vm_create(schema);
	vm_ast->user_data = &tracker_ast;
	if (expected_action) {
		cdsl_vm_register_action(vm_ast, expected_action, test_tracking_cb);
	}
	cdsl_vm_register_action(vm_ast, "score", test_tracking_cb);
	cdsl_vm_register_action(vm_ast, "fail_metric", test_tracking_cb);
	cdsl_vm_register_action(vm_ast, "block", test_tracking_cb);
	cdsl_vm_register_action(vm_ast, "notify", test_tracking_cb);

	cdsl_vm_t* vm_bc = cdsl_vm_create(schema);
	vm_bc->user_data = &tracker_bc;
	if (expected_action) {
		cdsl_vm_register_action(vm_bc, expected_action, test_tracking_cb);
	}
	cdsl_vm_register_action(vm_bc, "score", test_tracking_cb);
	cdsl_vm_register_action(vm_bc, "fail_metric", test_tracking_cb);
	cdsl_vm_register_action(vm_bc, "block", test_tracking_cb);
	cdsl_vm_register_action(vm_bc, "notify", test_tracking_cb);

	cdsl_context_t* ctx_ast = cdsl_context_create(schema);
	cdsl_context_t* ctx_bc = cdsl_context_create(schema);
	if (setup_fn) {
		setup_fn(ctx_ast);
		setup_fn(ctx_bc);
	}

	cdsl_rule_report_t* rpt_ast = cdsl_vm_execute(vm_ast, rule, ctx_ast);
	cdsl_rule_report_t* rpt_bc = cdsl_bytecode_execute_rule(vm_bc, &bc, ctx_bc);

	assert_reports_equivalent(rpt_ast, rpt_bc);

	TEST_ASSERT_INT(tracker_ast.call_count, tracker_bc.call_count, "Action call count matches");
	if (expected_action_count >= 0) {
		TEST_ASSERT_INT(
		    tracker_ast.call_count, expected_action_count, "Expected action count");
	}
	if (expected_action && expected_action_count > 0) {
		TEST_ASSERT_STR(
		    tracker_ast.last_action, tracker_bc.last_action, "Last action matches");
		TEST_ASSERT_STR(tracker_ast.last_action, expected_action, "Expected action fired");
	}

	/* Stats comparison */
	TEST_ASSERT_INT(vm_ast->stats.total_executions,
			vm_bc->stats.total_executions,
			"Total executions match");
	TEST_ASSERT_INT(vm_ast->stats.total_rules_executed,
			vm_bc->stats.total_rules_executed,
			"Total rules executed match");
	TEST_ASSERT_INT(vm_ast->stats.total_metrics_evaluated,
			vm_bc->stats.total_metrics_evaluated,
			"Total metrics evaluated match");
	TEST_ASSERT_INT(vm_ast->stats.total_actions_triggered,
			vm_bc->stats.total_actions_triggered,
			"Total actions triggered match");

	/* Also verify cdsl_vm_execute_compiled path */
	cdsl_compiled_rule_t crule = {.rule = rule, .bc = bc};
	test_tracker_t tracker_comp = {0};
	cdsl_vm_t* vm_comp = cdsl_vm_create(schema);
	vm_comp->user_data = &tracker_comp;
	if (expected_action) {
		cdsl_vm_register_action(vm_comp, expected_action, test_tracking_cb);
	}
	cdsl_vm_register_action(vm_comp, "score", test_tracking_cb);
	cdsl_vm_register_action(vm_comp, "fail_metric", test_tracking_cb);
	cdsl_vm_register_action(vm_comp, "block", test_tracking_cb);
	cdsl_vm_register_action(vm_comp, "notify", test_tracking_cb);
	cdsl_context_t* ctx_comp = cdsl_context_create(schema);
	if (setup_fn) {
		setup_fn(ctx_comp);
	}
	cdsl_rule_report_t* rpt_comp = cdsl_vm_execute_compiled(vm_comp, &crule, ctx_comp);
	assert_reports_equivalent(rpt_ast, rpt_comp);
	cdsl_report_free(rpt_comp);
	cdsl_context_free(ctx_comp);
	cdsl_vm_free(vm_comp);

	cdsl_report_free(rpt_ast);
	cdsl_report_free(rpt_bc);
	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx_ast);
	cdsl_context_free(ctx_bc);
	cdsl_vm_free(vm_ast);
	cdsl_vm_free(vm_bc);

	TEST_END();
}

/* Context setup fixtures */
static void
setup_user_simple(cdsl_context_t* ctx)
{
	cdsl_context_set_int(ctx, "user.age", 25);
	cdsl_context_set_string(ctx, "user.name", "Alice");
	cdsl_context_set_bool(ctx, "user.is_active", 1);
	cdsl_context_set_float(ctx, "user.score", 85.5);
}

static void
setup_user_minor(cdsl_context_t* ctx)
{
	cdsl_context_set_int(ctx, "user.age", 15);
	cdsl_context_set_string(ctx, "user.name", "Bob");
	cdsl_context_set_bool(ctx, "user.is_active", 0);
	cdsl_context_set_float(ctx, "user.score", 55.0);
}

static void
setup_dates_and_longs(cdsl_context_t* ctx)
{
	cdsl_context_set_date(ctx, "user.created_at", 1704067200); /* 2024-01-01 00:00:00 UTC */
	cdsl_context_set_date(ctx, "user.login_at", 1704153600);   /* 2024-01-02 00:00:00 UTC */
	cdsl_context_set_long(ctx, "user.id_64", 9000000000LL);
	cdsl_context_set_long(ctx, "user.max_limit", 10000000000LL);
}

/* 1. Simple Rules Equivalence */
static void
test_diff_simple_rules(void)
{
	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(s, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_action(s, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	/* Scenario A: WHEN is false -> PASSED, action not fired */
	run_diff_test("diff: simple rule pass (WHEN false)",
		      s,
		      "RULE r1 { META { description = \"Age pass check\" } WHEN user.age < 18 THEN "
		      "block(\"minor\") }",
		      setup_user_simple,
		      "block",
		      0);

	/* Scenario B: WHEN is true -> FAILED, action fired */
	run_diff_test("diff: simple rule fail (WHEN true)",
		      s,
		      "RULE r2 { META { description = \"Age fail check\" } WHEN user.age >= 18 "
		      "THEN block(\"adult\") }",
		      setup_user_simple,
		      "block",
		      1);

	/* Scenario C: Minor user (user.age = 15) -> WHEN is true -> FAILED, action fired */
	run_diff_test("diff: simple rule minor fail",
		      s,
		      "RULE r3 { META { description = \"Age check for minor\" } WHEN user.age < 18 "
		      "THEN block(\"minor\") }",
		      setup_user_minor,
		      "block",
		      1);

	cdsl_schema_free(s);
}

/* 2. Complex Booleans and Short-circuiting */
static void
test_diff_booleans(void)
{
	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(s, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_var(s, "user.is_active", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(s, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_action(s, "notify", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	/* Complex logical expression */
	run_diff_test(
	    "diff: complex booleans and precedence",
	    s,
	    "RULE bool_rule { WHEN (user.age > 20 && user.score > 80.0) || (!user.is_active && "
	    "user.name == \"Bob\") THEN notify(\"matched\") }",
	    setup_user_simple,
	    "notify",
	    1);

	/* Short circuit test: true || ... */
	run_diff_test("diff: short circuit true || ...",
		      s,
		      "RULE sc_or { WHEN user.age > 20 || user.age < 10 THEN notify(\"or_ok\") }",
		      setup_user_simple,
		      "notify",
		      1);

	/* Short circuit test: false && ... */
	run_diff_test(
	    "diff: short circuit false && ...",
	    s,
	    "RULE sc_and { WHEN user.age < 18 && user.score > 50.0 THEN notify(\"and_fail\") }",
	    setup_user_simple,
	    "notify",
	    0);

	cdsl_schema_free(s);
}

/* 3. Comparison Operators across INT, FLOAT, STRING */
static void
test_diff_type_comparisons(void)
{
	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(s, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_var(s, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_action(s, "notify", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	/* String comparison == and != */
	run_diff_test("diff: string comparison ==",
		      s,
		      "RULE str_eq { WHEN user.name == \"Alice\" THEN notify(\"name_match\") }",
		      setup_user_simple,
		      "notify",
		      1);

	run_diff_test("diff: string comparison !=",
		      s,
		      "RULE str_ne { WHEN user.name != \"Alice\" THEN notify(\"name_mismatch\") }",
		      setup_user_simple,
		      "notify",
		      0);

	/* Float comparison and arithmetic */
	run_diff_test(
	    "diff: float arithmetic and comparison",
	    s,
	    "RULE flt_cmp { WHEN (user.score + 10.0) / 2.0 > 45.0 THEN notify(\"flt_ok\") }",
	    setup_user_simple,
	    "notify",
	    1);

	/* Integer arithmetic */
	run_diff_test("diff: int arithmetic",
		      s,
		      "RULE int_arith { WHEN (user.age * 2 - 10) == 40 THEN notify(\"int_ok\") }",
		      setup_user_simple,
		      "notify",
		      1);

	cdsl_schema_free(s);
}

/* 4. Date and Long comparisons and arithmetic */
static void
test_diff_dates_and_longs(void)
{
	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.created_at", CDSL_TYPE_DATE);
	cdsl_schema_register_var(s, "user.login_at", CDSL_TYPE_DATE);
	cdsl_schema_register_var(s, "user.id_64", CDSL_TYPE_LONG);
	cdsl_schema_register_var(s, "user.max_limit", CDSL_TYPE_LONG);
	cdsl_schema_register_action(s, "notify", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	/* Date literal comparison */
	run_diff_test("diff: date literal comparison",
		      s,
		      "RULE dt_cmp { WHEN user.created_at >= @2024-01-01 && user.login_at < "
		      "@2024-01-03 THEN notify(\"date_ok\") }",
		      setup_dates_and_longs,
		      "notify",
		      1);

	/* Date difference: login_at - created_at == 1 (1 day) */
	run_diff_test("diff: date arithmetic",
		      s,
		      "RULE dt_arith { WHEN (user.login_at - user.created_at) == 1 THEN "
		      "notify(\"1day_diff\") }",
		      setup_dates_and_longs,
		      "notify",
		      1);

	/* 64-bit Long comparison and arithmetic */
	run_diff_test("diff: 64-bit long arithmetic",
		      s,
		      "RULE long_cmp { WHEN (user.id_64 + 1000000000L) == user.max_limit THEN "
		      "notify(\"long_ok\") }",
		      setup_dates_and_longs,
		      "notify",
		      1);

	cdsl_schema_free(s);
}

/* 5. Array functions: count, sum, avg, min, max */
static void
test_diff_array_functions(void)
{
	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(s, "notify", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	/* count([1, 2, 3, 4]) == 4 */
	run_diff_test("diff: array count()",
		      s,
		      "RULE arr_count { WHEN count([1, 2, 3, 4]) == 4 THEN notify(\"count_ok\") }",
		      setup_user_simple,
		      "notify",
		      1);

	/* sum([10, 20, 30]) == 60 */
	run_diff_test("diff: array sum()",
		      s,
		      "RULE arr_sum { WHEN sum([10, 20, 30]) == 60 THEN notify(\"sum_ok\") }",
		      setup_user_simple,
		      "notify",
		      1);

	/* min & max builtins */
	run_diff_test("diff: array and scalar min() and max()",
		      s,
		      "RULE arr_minmax { WHEN min(5, 2) == 2 && max(5, 2) == 5 "
		      "THEN notify(\"minmax_ok\") }",
		      setup_user_simple,
		      "notify",
		      1);

	cdsl_schema_free(s);
}

static void
setup_user_partial(cdsl_context_t* ctx)
{
	cdsl_context_set_int(ctx, "user.age", 16);	 /* age_metric -> 0 */
	cdsl_context_set_float(ctx, "user.score", 85.0); /* score_metric -> 50 */
}

static void
setup_user_fail(cdsl_context_t* ctx)
{
	cdsl_context_set_int(ctx, "user.age", 16);	 /* age_metric -> 0 */
	cdsl_context_set_float(ctx, "user.score", 65.0); /* score_metric -> 25 */
}

static void
setup_user_veto(cdsl_context_t* ctx)
{
	cdsl_context_set_int(ctx, "user.age", 16);
	cdsl_context_set_float(ctx, "user.score", 90.0);
}

/* 6. Metric Rules: Full Pass, Partial Pass, Failed, Critical Veto */
static void
test_diff_metric_rules(void)
{
	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(s, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_action(s, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
	cdsl_schema_register_action(
	    s, "fail_metric", CDSL_TYPE_VOID, 2, CDSL_TYPE_INT, CDSL_TYPE_STRING);

	const char* dsl =
	    "RULE credit_audit {"
	    "  META { pass_threshold = \"80\" partial_threshold = \"50\" description = \"Credit "
	    "scoring\" }"
	    "  METRIC age_metric {"
	    "    META { weight = \"50\" description = \"Age factor\" }"
	    "    CASE user.age >= 18 THEN score(50)"
	    "    DEFAULT score(0)"
	    "  }"
	    "  METRIC score_metric {"
	    "    META { weight = \"50\" description = \"Score factor\" }"
	    "    CASE user.score >= 80.0 THEN score(50)"
	    "    CASE user.score >= 60.0 THEN score(25)"
	    "    DEFAULT score(0)"
	    "  }"
	    "}";

	/* Full pass: 50 + 50 = 100 >= 80 -> PASSED */
	run_diff_test("diff: metric rule full pass", s, dsl, setup_user_simple, "score", -1);

	/* Partial pass: 0 + 50 = 50 -> PARTIALLY_PASSED */
	run_diff_test("diff: metric rule partial pass", s, dsl, setup_user_partial, "score", -1);

	/* Fail below threshold: 0 + 25 = 25 < 50 -> FAILED */
	run_diff_test("diff: metric rule failed", s, dsl, setup_user_fail, "score", -1);

	/* Critical veto: is_critical = true fails -> rule FAILED even if score >= pass_threshold */
	const char* dsl_critical = "RULE critical_audit {"
				   "  META { pass_threshold = \"60\" partial_threshold = \"40\" }"
				   "  METRIC age_critical {"
				   "    META { weight = \"40\" is_critical = \"true\" }"
				   "    CASE user.age >= 18 THEN score(40)"
				   "    DEFAULT fail_metric(0, \"underage_veto\")"
				   "  }"
				   "  METRIC score_metric {"
				   "    META { weight = \"60\" }"
				   "    CASE user.score >= 60.0 THEN score(60)"
				   "    DEFAULT score(0)"
				   "  }"
				   "}";

	/* user.age = 16 (fails critical metric, sets violation_reason 'underage_veto')
	   user.score = 90 (scores 60 >= pass_threshold 60)
	   Result MUST be FAILED because critical metric vetoed! */
	run_diff_test("diff: critical metric veto", s, dsl_critical, setup_user_veto, NULL, -1);

	cdsl_schema_free(s);
}

/* 7. Instruction Quota Error Handling Equivalence */
static void
test_diff_instruction_quota(void)
{
	TEST_BEGIN("diff: instruction quota error equivalence");

	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "x", CDSL_TYPE_INT);
	cdsl_schema_register_action(s, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	/* Deeply nested arithmetic expression that exceeds tiny instruction limit */
	const char* dsl =
	    "RULE quota_rule { WHEN x + x + x + x + x + x + x + x + x + x + x + x > 0 THEN "
	    "block(\"yes\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule parse succeeded");

	cdsl_bytecode_t bc = {0};
	cdsl_bytecode_compile(rule, s, &bc);

	cdsl_vm_t* vm_ast = cdsl_vm_create(s);
	cdsl_vm_set_instruction_limit(vm_ast, 3); /* tiny limit */

	cdsl_vm_t* vm_bc = cdsl_vm_create(s);
	cdsl_vm_set_instruction_limit(vm_bc, 3); /* tiny limit */

	cdsl_context_t* ctx_ast = cdsl_context_create(s);
	cdsl_context_set_int(ctx_ast, "x", 1);
	cdsl_context_t* ctx_bc = cdsl_context_create(s);
	cdsl_context_set_int(ctx_bc, "x", 1);

	cdsl_rule_report_t* rpt_ast = cdsl_vm_execute(vm_ast, rule, ctx_ast);
	cdsl_rule_report_t* rpt_bc = cdsl_bytecode_execute_rule(vm_bc, &bc, ctx_bc);

	TEST_ASSERT_NOT_NULL(rpt_ast, "AST report not null");
	TEST_ASSERT_NOT_NULL(rpt_bc, "Bytecode report not null");
	if (rpt_ast && rpt_bc) {
		TEST_ASSERT_INT(rpt_ast->status, CDSL_STATUS_ERROR, "AST status ERROR");
		TEST_ASSERT_INT(rpt_bc->status, CDSL_STATUS_ERROR, "Bytecode status ERROR");
	}

	cdsl_report_free(rpt_ast);
	cdsl_report_free(rpt_bc);
	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx_ast);
	cdsl_context_free(ctx_bc);
	cdsl_vm_free(vm_ast);
	cdsl_vm_free(vm_bc);
	cdsl_schema_free(s);

	TEST_END();
}

/* 8. Runtime Error and Evaluation Parity Edge Cases */
static void
test_diff_runtime_errors_and_edge_cases(void)
{
	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(s, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	/* Integer overflow -> ERROR on both */
	run_diff_test("diff: int32 add overflow produces ERROR",
		      s,
		      "RULE r_ovf { WHEN 2147483647 + 1 > 0 THEN block(\"bad\") }",
		      setup_user_simple,
		      "block",
		      0);

	/* Integer divide-by-zero -> ERROR on both */
	run_diff_test("diff: int div-by-zero produces ERROR",
		      s,
		      "RULE r_div0 { WHEN user.age / 0 >= 18 THEN block(\"bad\") }",
		      setup_user_simple,
		      "block",
		      0);

	/* Float divide-by-zero -> ERROR on both */
	run_diff_test("diff: float div-by-zero produces ERROR",
		      s,
		      "RULE r_flt0 { WHEN 10.0 / 0.0 > 1.0 THEN block(\"bad\") }",
		      setup_user_simple,
		      "block",
		      0);

	/* Incompatible type comparison (string < string) -> ERROR on both */
	run_diff_test("diff: string relational comparison produces ERROR",
		      s,
		      "RULE r_strop { WHEN \"alpha\" < \"beta\" THEN block(\"bad\") }",
		      setup_user_simple,
		      "block",
		      0);

	/* Exact 64-bit integer comparison (no 53-bit IEEE 754 precision loss) */
	run_diff_test("diff: 64-bit exact comparison (2^53+1 != 2^53)",
		      s,
		      "RULE r_long_prec { WHEN 9007199254740993L == 9007199254740992L THEN "
		      "block(\"bad\") }",
		      setup_user_simple,
		      "block",
		      0);

	/* Unset variable comparison: missing == 0 is FALSE on both engines (PASSED) */
	run_diff_test("diff: missing variable comparison parity",
		      s,
		      "RULE r_missing { WHEN missing_var == 0 THEN block(\"bad\") }",
		      setup_user_simple,
		      "block",
		      0);

	cdsl_schema_free(s);
}

int
main(void)
{
	printf("Running dual execution path differential tests...\n");

	test_diff_simple_rules();
	test_diff_booleans();
	test_diff_type_comparisons();
	test_diff_dates_and_longs();
	test_diff_array_functions();
	test_diff_metric_rules();
	test_diff_instruction_quota();
	test_diff_runtime_errors_and_edge_cases();

	TEST_SUMMARY();
	TEST_EXIT();
}
