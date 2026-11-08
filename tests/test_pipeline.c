#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "cdsl/pipeline.h"
#include "cdsl/context.h"
#include "cdsl/vm.h"
#include <stdio.h>
#include <string.h>

static void
test_pipeline_execution(void)
{
	cdsl_pipeline_t* p = cdsl_pipeline_create("loan_pipeline");
	assert(p != NULL);

	/* Stage 1: Pre-check (rule) */
	cdsl_pipeline_stage_t* s1 = cdsl_pipeline_create_stage("pre_check", CDSL_ON_FAIL_ABORT);
	cdsl_rule_t* r1 = cdsl_parse_string(
	    "RULE blacklist_check { WHEN is_blacklisted == true THEN reject() }", NULL);
	assert(r1 != NULL);
	assert(cdsl_pipeline_stage_add_rule(s1, r1) == 0);
	assert(cdsl_pipeline_add_stage(p, s1) == 0);

	/* Stage 2: Scoring (scorecard) */
	cdsl_pipeline_stage_t* s2 = cdsl_pipeline_create_stage("scoring", CDSL_ON_FAIL_ABORT);
	assert(cdsl_pipeline_stage_add_dependency(s2, "pre_check") == 0);
	cdsl_scorecard_t* sc = cdsl_scorecard_create("sc1", 600.0, "credit_score");
	cdsl_scorecard_add_characteristic(sc, "age", 0.0);
	cdsl_expr_node_t* cond = cdsl_create_expr_binary(CDSL_OP_GE,
							 cdsl_create_expr_id(strdup("age"), 1, 1),
							 cdsl_create_expr_int(25, 1, 1),
							 1,
							 1);
	cdsl_scorecard_add_case(sc, "age", cond, 50.0);
	assert(cdsl_pipeline_stage_add_scorecard(s2, sc) == 0);
	assert(cdsl_pipeline_add_stage(p, s2) == 0);

	/* Stage 3: Pricing (dtable) */
	cdsl_pipeline_stage_t* s3 = cdsl_pipeline_create_stage("pricing", CDSL_ON_FAIL_ABORT);
	assert(cdsl_pipeline_stage_add_dependency(s3, "scoring") == 0);
	cdsl_dtable_t* dt = cdsl_dtable_create("dt1", CDSL_HIT_FIRST);
	cdsl_dtable_add_input(dt, "credit_score", CDSL_TYPE_FLOAT);
	cdsl_dtable_add_output(dt, "limit", CDSL_TYPE_FLOAT);
	cdsl_dtable_cell_t in[1] = {
	    {.kind = CDSL_CELL_EXPR,
	     .expr = cdsl_create_expr_binary(CDSL_OP_GE,
					     cdsl_create_expr_id(strdup("credit_score"), 1, 1),
					     cdsl_create_expr_float(650.0, 1, 1),
					     1,
					     1)}};
	cdsl_dtable_cell_t out[1] = {
	    {.kind = CDSL_CELL_EXPR, .expr = cdsl_create_expr_float(50000.0, 1, 1)}};
	cdsl_dtable_add_row(dt, in, out);
	assert(cdsl_pipeline_stage_add_dtable(s3, dt) == 0);
	assert(cdsl_pipeline_add_stage(p, s3) == 0);

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	assert(vm != NULL);

	/* Case A: Non-blacklisted, age 30 -> all stages pass */
	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_context_set_bool(ctx, "is_blacklisted", 0);
	cdsl_context_set_int(ctx, "age", 30);

	cdsl_pipeline_report_t* rpt = cdsl_pipeline_execute(p, vm, ctx);
	assert(rpt != NULL);
	assert(rpt->status == CDSL_PIPELINE_SUCCESS);
	assert(rpt->num_stages == 3);
	assert(rpt->stage_reports[0].passed == true);
	assert(rpt->stage_reports[1].passed == true);
	assert(rpt->stage_reports[2].passed == true);

	/* Verify credit_score was populated by stage 2 and used by stage 3 */
	double score = cdsl_context_get_float(ctx, "credit_score", 0.0);
	assert(score == 650.0);

	cdsl_pipeline_report_free(rpt);
	cdsl_context_free(ctx);

	/* Case B: Blacklisted -> Stage 1 fails and pipeline aborts */
	cdsl_context_t* ctx2 = cdsl_context_create(NULL);
	cdsl_context_set_bool(ctx2, "is_blacklisted", 1);
	cdsl_context_set_int(ctx2, "age", 30);

	cdsl_pipeline_report_t* rpt2 = cdsl_pipeline_execute(p, vm, ctx2);
	assert(rpt2 != NULL);
	assert(rpt2->status == CDSL_PIPELINE_ABORTED);
	assert(rpt2->stage_reports[0].passed == false);
	/* Later stages should NOT be executed */
	assert(rpt2->num_stages == 1);

	cdsl_pipeline_report_free(rpt2);
	cdsl_context_free(ctx2);

	cdsl_vm_free(vm);
	cdsl_pipeline_free(p);
}

static void
test_pipeline_dsl_parse(void)
{
	const char* dsl = "PIPELINE approval_flow\n"
			  "  STAGE check_identity:\n"
			  "    ON_FAIL: ABORT\n"
			  "  STAGE credit_scoring:\n"
			  "    DEPENDS_ON: check_identity\n"
			  "    ON_FAIL: CONTINUE\n"
			  "END\n";

	cdsl_pipeline_t* p = cdsl_pipeline_parse_string(dsl, NULL);
	assert(p != NULL);
	assert(strcmp(p->name, "approval_flow") == 0);
	assert(p->num_stages == 2);
	assert(strcmp(p->stages->name, "check_identity") == 0);
	assert(p->stages->on_fail == CDSL_ON_FAIL_ABORT);
	assert(strcmp(p->stages->next->name, "credit_scoring") == 0);
	assert(p->stages->next->num_dependencies == 1);
	assert(strcmp(p->stages->next->dependencies[0], "check_identity") == 0);

	cdsl_pipeline_free(p);
}

int
main(void)
{
	test_pipeline_execution();
	test_pipeline_dsl_parse();
	printf("All pipeline tests passed successfully!\n");
	return 0;
}
