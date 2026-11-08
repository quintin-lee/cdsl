#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "cdsl/scorecard.h"
#include "cdsl/context.h"
#include "cdsl/schema.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void
test_scorecard_basic(void)
{
	cdsl_scorecard_t* sc = cdsl_scorecard_create("credit_scorecard", 600.0, "total_score");
	assert(sc != NULL);
	assert(cdsl_scorecard_add_characteristic(sc, "age", 10.0) == 0);
	assert(cdsl_scorecard_add_characteristic(sc, "income", 0.0) == 0);

	/* Age cases: < 22 -> -20, [22..35] -> +15, [36..55] -> +30 */
	cdsl_expr_node_t* cond_age1 =
	    cdsl_create_expr_binary(CDSL_OP_LT,
				    cdsl_create_expr_id(strdup("age"), 1, 1),
				    cdsl_create_expr_int(22, 1, 1),
				    1,
				    1);
	assert(cdsl_scorecard_add_case(sc, "age", cond_age1, -20.0) == 0);

	cdsl_expr_node_t* cond_age2 = cdsl_create_expr_binary(
	    CDSL_OP_IN,
	    cdsl_create_expr_id(strdup("age"), 1, 1),
	    cdsl_create_expr_range(
		cdsl_create_expr_int(22, 1, 1), cdsl_create_expr_int(35, 1, 1), 1, 1),
	    1,
	    1);
	assert(cdsl_scorecard_add_case(sc, "age", cond_age2, 15.0) == 0);

	/* Income cases: >= 5000.0 -> +25, < 5000.0 -> -15 */
	cdsl_expr_node_t* cond_inc1 =
	    cdsl_create_expr_binary(CDSL_OP_GE,
				    cdsl_create_expr_id(strdup("income"), 1, 1),
				    cdsl_create_expr_float(5000.0, 1, 1),
				    1,
				    1);
	assert(cdsl_scorecard_add_case(sc, "income", cond_inc1, 25.0) == 0);

	cdsl_expr_node_t* cond_inc2 =
	    cdsl_create_expr_binary(CDSL_OP_LT,
				    cdsl_create_expr_id(strdup("income"), 1, 1),
				    cdsl_create_expr_float(5000.0, 1, 1),
				    1,
				    1);
	assert(cdsl_scorecard_add_case(sc, "income", cond_inc2, -15.0) == 0);

	/* Evaluation test: age = 28 (matches case 2: +15), income = 8000.0 (matches case 1: +25) */
	/* Total score = 600 + 15 + 25 = 640 */
	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_context_set_int(ctx, "age", 28);
	cdsl_context_set_float(ctx, "income", 8000.0);

	cdsl_scorecard_report_t* rpt = cdsl_scorecard_eval(sc, ctx);
	assert(rpt != NULL);
	assert(rpt->baseline_score == 600.0);
	assert(rpt->final_score == 640.0);
	assert(rpt->num_contributions == 2);
	assert(rpt->contributions[0].delta == 15.0);
	assert(rpt->contributions[1].delta == 25.0);

	/* Context should have 'total_score' injected */
	double out_val = cdsl_context_get_float(ctx, "total_score", 0.0);
	assert(out_val == 640.0);

	cdsl_scorecard_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_scorecard_free(sc);
}

static void
test_scorecard_dsl_parse(void)
{
	const char* dsl = "SCORECARD credit_fast\n"
			  "  BASELINE: 500\n"
			  "  CHARACTERISTIC age:\n"
			  "    CASE < 25: -30\n"
			  "    CASE [25..50]: +40\n"
			  "    DEFAULT: +10\n"
			  "  CHARACTERISTIC income:\n"
			  "    CASE >= 10000: +50\n"
			  "    DEFAULT: 0\n"
			  "  OUTPUT: final_credit\n"
			  "END\n";

	cdsl_scorecard_t* sc = cdsl_scorecard_parse_string(dsl, NULL);
	assert(sc != NULL);
	assert(strcmp(sc->name, "credit_fast") == 0);
	assert(sc->baseline_score == 500.0);
	assert(sc->num_characteristics == 2);
	assert(strcmp(sc->output_var, "final_credit") == 0);

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_context_set_int(ctx, "age", 30);
	cdsl_context_set_float(ctx, "income", 12000.0);

	cdsl_scorecard_report_t* rpt = cdsl_scorecard_eval(sc, ctx);
	assert(rpt != NULL);
	/* 500 + 40 + 50 = 590 */
	assert(rpt->final_score == 590.0);

	double v = cdsl_context_get_float(ctx, "final_credit", 0.0);
	assert(v == 590.0);

	cdsl_scorecard_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_scorecard_free(sc);
}

int
main(void)
{
	test_scorecard_basic();
	test_scorecard_dsl_parse();
	printf("All scorecard tests passed successfully!\n");
	return 0;
}
