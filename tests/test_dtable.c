#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "cdsl/dtable.h"
#include "cdsl/context.h"
#include "cdsl/schema.h"
#include <stdio.h>
#include <string.h>

static void
test_dtable_basic_first(void)
{
	cdsl_dtable_t* dt = cdsl_dtable_create("pricing_table", CDSL_HIT_FIRST);
	assert(dt != NULL);
	assert(cdsl_dtable_add_input(dt, "age", CDSL_TYPE_INT) == 0);
	assert(cdsl_dtable_add_input(dt, "income", CDSL_TYPE_FLOAT) == 0);
	assert(cdsl_dtable_add_output(dt, "decision", CDSL_TYPE_STRING) == 0);
	assert(cdsl_dtable_add_output(dt, "limit", CDSL_TYPE_FLOAT) == 0);

	/* Row 1: age < 18, _ -> "REJECT", 0.0 */
	cdsl_dtable_cell_t in1[2] = {
	    {.kind = CDSL_CELL_EXPR,
	     .expr = cdsl_create_expr_binary(CDSL_OP_LT,
					     cdsl_create_expr_id(strdup("age"), 1, 1),
					     cdsl_create_expr_int(18, 1, 1),
					     1,
					     1)},
	    {.kind = CDSL_CELL_WILDCARD, .expr = NULL}};
	cdsl_dtable_cell_t out1[2] = {
	    {.kind = CDSL_CELL_EXPR, .expr = cdsl_create_expr_string(strdup("REJECT"), 1, 1)},
	    {.kind = CDSL_CELL_EXPR, .expr = cdsl_create_expr_float(0.0, 1, 1)}};
	assert(cdsl_dtable_add_row(dt, in1, out1) == 0);

	/* Row 2: age >= 18, income >= 5000.0 -> "APPROVE", 20000.0 */
	cdsl_dtable_cell_t in2[2] = {
	    {.kind = CDSL_CELL_EXPR,
	     .expr = cdsl_create_expr_binary(CDSL_OP_GE,
					     cdsl_create_expr_id(strdup("age"), 1, 1),
					     cdsl_create_expr_int(18, 1, 1),
					     1,
					     1)},
	    {.kind = CDSL_CELL_EXPR,
	     .expr = cdsl_create_expr_binary(CDSL_OP_GE,
					     cdsl_create_expr_id(strdup("income"), 1, 1),
					     cdsl_create_expr_float(5000.0, 1, 1),
					     1,
					     1)}};
	cdsl_dtable_cell_t out2[2] = {
	    {.kind = CDSL_CELL_EXPR, .expr = cdsl_create_expr_string(strdup("APPROVE"), 1, 1)},
	    {.kind = CDSL_CELL_EXPR, .expr = cdsl_create_expr_float(20000.0, 1, 1)}};
	assert(cdsl_dtable_add_row(dt, in2, out2) == 0);

	/* Default: "MANUAL", 500.0 */
	cdsl_dtable_cell_t def_out[2] = {
	    {.kind = CDSL_CELL_EXPR, .expr = cdsl_create_expr_string(strdup("MANUAL"), 1, 1)},
	    {.kind = CDSL_CELL_EXPR, .expr = cdsl_create_expr_float(500.0, 1, 1)}};
	assert(cdsl_dtable_set_default(dt, def_out) == 0);

	/* Test 1: Minor (age 16) -> Row 1 match */
	cdsl_context_t* ctx1 = cdsl_context_create(NULL);
	cdsl_context_set_int(ctx1, "age", 16);
	cdsl_context_set_float(ctx1, "income", 8000.0);
	cdsl_dtable_report_t* rpt1 = cdsl_dtable_eval(dt, ctx1);
	assert(rpt1 != NULL);
	assert(rpt1->matched == true);
	assert(rpt1->num_matches == 1);
	assert(rpt1->final_outputs[0].type == CDSL_TYPE_STRING);
	assert(strcmp(rpt1->final_outputs[0].data.string_val, "REJECT") == 0);
	assert(rpt1->final_outputs[1].data.float_val == 0.0);
	cdsl_dtable_report_free(rpt1);
	cdsl_context_free(ctx1);

	/* Test 2: Adult high income (age 28, income 9000.0) -> Row 2 match */
	cdsl_context_t* ctx2 = cdsl_context_create(NULL);
	cdsl_context_set_int(ctx2, "age", 28);
	cdsl_context_set_float(ctx2, "income", 9000.0);
	cdsl_dtable_report_t* rpt2 = cdsl_dtable_eval(dt, ctx2);
	assert(rpt2 != NULL);
	assert(rpt2->matched == true);
	assert(strcmp(rpt2->final_outputs[0].data.string_val, "APPROVE") == 0);
	assert(rpt2->final_outputs[1].data.float_val == 20000.0);
	cdsl_dtable_report_free(rpt2);
	cdsl_context_free(ctx2);

	/* Test 3: Adult low income (age 28, income 2000.0) -> Default fallback */
	cdsl_context_t* ctx3 = cdsl_context_create(NULL);
	cdsl_context_set_int(ctx3, "age", 28);
	cdsl_context_set_float(ctx3, "income", 2000.0);
	cdsl_dtable_report_t* rpt3 = cdsl_dtable_eval(dt, ctx3);
	assert(rpt3 != NULL);
	assert(rpt3->matched == true);
	assert(strcmp(rpt3->final_outputs[0].data.string_val, "MANUAL") == 0);
	assert(rpt3->final_outputs[1].data.float_val == 500.0);
	cdsl_dtable_report_free(rpt3);
	cdsl_context_free(ctx3);

	cdsl_dtable_free(dt);
}

static void
test_dtable_unique_policy(void)
{
	cdsl_dtable_t* dt = cdsl_dtable_create("unique_table", CDSL_HIT_UNIQUE);
	assert(dt != NULL);
	assert(cdsl_dtable_add_input(dt, "tier", CDSL_TYPE_INT) == 0);
	assert(cdsl_dtable_add_output(dt, "discount", CDSL_TYPE_FLOAT) == 0);

	/* Row 1: tier == 1 -> 0.10 */
	cdsl_dtable_cell_t in1[1] = {
	    {.kind = CDSL_CELL_EXPR,
	     .expr = cdsl_create_expr_binary(CDSL_OP_EQ,
					     cdsl_create_expr_id(strdup("tier"), 1, 1),
					     cdsl_create_expr_int(1, 1, 1),
					     1,
					     1)}};
	cdsl_dtable_cell_t out1[1] = {
	    {.kind = CDSL_CELL_EXPR, .expr = cdsl_create_expr_float(0.10, 1, 1)}};
	assert(cdsl_dtable_add_row(dt, in1, out1) == 0);

	/* Row 2: tier >= 1 -> 0.20 (overlapping!) */
	cdsl_dtable_cell_t in2[1] = {
	    {.kind = CDSL_CELL_EXPR,
	     .expr = cdsl_create_expr_binary(CDSL_OP_GE,
					     cdsl_create_expr_id(strdup("tier"), 1, 1),
					     cdsl_create_expr_int(1, 1, 1),
					     1,
					     1)}};
	cdsl_dtable_cell_t out2[1] = {
	    {.kind = CDSL_CELL_EXPR, .expr = cdsl_create_expr_float(0.20, 1, 1)}};
	assert(cdsl_dtable_add_row(dt, in2, out2) == 0);

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_context_set_int(ctx, "tier", 1);
	cdsl_dtable_report_t* rpt = cdsl_dtable_eval(dt, ctx);
	assert(rpt != NULL);
	/* Should detect UNIQUE policy violation because both row 1 and row 2 match */
	assert(rpt->matched == false);
	assert(rpt->error_message != NULL);

	cdsl_dtable_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_dtable_free(dt);
}

static void
test_dtable_dsl_parse(void)
{
	const char* dsl = "DECISION_TABLE auto_rate\n"
			  "  INPUTS (credit: INT, score: FLOAT)\n"
			  "  OUTPUTS (approved: BOOL, rate: FLOAT)\n"
			  "  HIT_POLICY: FIRST\n"
			  "  ROW: credit > 600, score >= 80.0 -> true, 0.05\n"
			  "  ROW: credit <= 600, _ -> false, 0.15\n"
			  "  DEFAULT: false, 0.20\n"
			  "END\n";

	cdsl_dtable_t* dt = cdsl_dtable_parse_string(dsl, NULL);
	assert(dt != NULL);
	assert(strcmp(dt->name, "auto_rate") == 0);
	assert(dt->num_inputs == 2);
	assert(dt->num_outputs == 2);
	assert(dt->num_rows == 2);

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_context_set_int(ctx, "credit", 720);
	cdsl_context_set_float(ctx, "score", 95.0);

	cdsl_dtable_report_t* rpt = cdsl_dtable_eval(dt, ctx);
	assert(rpt != NULL);
	assert(rpt->matched == true);
	assert(rpt->final_outputs[0].data.bool_val == 1);
	assert(rpt->final_outputs[1].data.float_val == 0.05);

	cdsl_dtable_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_dtable_free(dt);
}

int
main(void)
{
	test_dtable_basic_first();
	test_dtable_unique_policy();
	test_dtable_dsl_parse();
	printf("All decision table tests passed successfully!\n");
	return 0;
}
