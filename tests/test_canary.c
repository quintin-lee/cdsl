/**
 * @file tests/test_canary.c
 * @brief Unit tests for Canary Pre-Flight check and isolated dry-run validation.
 */

#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/canary.h"
#include <stdlib.h>
#include <string.h>

static void
test_canary_syntax_and_schema_validation(void)
{
	TEST_BEGIN("canary: syntax and schema verification failure rejection");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "pass", CDSL_TYPE_VOID, 0);

	/* 1. Syntax error in DSL */
	const char* bad_syntax = "RULE bad { WHEN age >= THEN pass() }";
	cdsl_canary_report_t rep1 = cdsl_canary_verify_dsl(schema, bad_syntax, NULL, 0);
	TEST_ASSERT_INT(rep1.passed, 0, "syntax error rejected");
	TEST_ASSERT_INT(rep1.status, CDSL_CANARY_ERR_SYNTAX, "status is syntax error");
	TEST_ASSERT_NOT_NULL(rep1.message, "diagnostic message present");
	cdsl_canary_report_free(&rep1);

	/* 2. Schema type check error: unknown variable 'unknown_var' */
	const char* bad_schema = "RULE bad_var { WHEN unknown_var >= 10 THEN pass() }";
	cdsl_canary_report_t rep2 = cdsl_canary_verify_dsl(schema, bad_schema, NULL, 0);
	TEST_ASSERT_INT(rep2.passed, 0, "schema error rejected");
	TEST_ASSERT_INT(rep2.status, CDSL_CANARY_ERR_SCHEMA, "status is schema error");
	cdsl_canary_report_free(&rep2);

	/* 3. Valid rule passes */
	const char* good_dsl = "RULE good { WHEN age >= 18 THEN pass() }";
	cdsl_canary_report_t rep3 = cdsl_canary_verify_dsl(schema, good_dsl, NULL, 0);
	TEST_ASSERT_INT(rep3.passed, 1, "good DSL passes");
	TEST_ASSERT_INT(rep3.status, CDSL_CANARY_OK, "status is OK");
	cdsl_canary_report_free(&rep3);

	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_canary_mock_dry_run(void)
{
	TEST_BEGIN("canary: mock contexts dry run execution");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "pass", CDSL_TYPE_VOID, 0);

	cdsl_context_t* mocks[3];
	for (int i = 0; i < 3; i++) {
		mocks[i] = cdsl_context_create(schema);
		cdsl_context_set_int(mocks[i], "score", (i + 1) * 30);
	}

	const char* dsl = "RULE r { WHEN score >= 50 THEN pass() }";
	cdsl_canary_report_t rep = cdsl_canary_verify_dsl(schema, dsl, mocks, 3);
	TEST_ASSERT_INT(rep.passed, 1, "dry run passed");
	TEST_ASSERT_INT((int)rep.mocks_executed, 3, "3 mock contexts executed");
	cdsl_canary_report_free(&rep);

	for (int i = 0; i < 3; i++) {
		cdsl_context_free(mocks[i]);
	}
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_canary_manager_deployment_gate(void)
{
	TEST_BEGIN("canary: manager deployment protected by canary check");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "level", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "promote", CDSL_TYPE_VOID, 0);

	cdsl_manager_t* mgr = cdsl_manager_create(schema);

	/* Initial valid deployment */
	cdsl_manager_deploy_dsl(
	    mgr, "RULE r1 { WHEN level >= 1 THEN promote() }", "v1", NULL, NULL);
	TEST_ASSERT_INT((int)cdsl_manager_get_active_version(mgr), 1, "v1 active");

	/* Attempt to deploy invalid syntax through canary */
	cdsl_canary_report_t rep = {0};
	uint64_t v = 0;
	int ok =
	    cdsl_manager_deploy_canary(mgr, "INVALID SYNTAX RULE", "v2-bad", NULL, 0, &rep, &v);
	TEST_ASSERT_INT(ok, 0, "canary deployment rejected");
	TEST_ASSERT_INT(rep.passed, 0, "canary report indicates failure");
	TEST_ASSERT_INT((int)cdsl_manager_get_active_version(mgr), 1, "active version remains 1");
	cdsl_canary_report_free(&rep);

	/* Valid canary deployment */
	ok = cdsl_manager_deploy_canary(
	    mgr, "RULE r2 { WHEN level >= 2 THEN promote() }", "v2-good", NULL, 0, &rep, &v);
	TEST_ASSERT_INT(ok, 1, "canary deployment accepted");
	TEST_ASSERT_INT(
	    (int)cdsl_manager_get_active_version(mgr), 2, "active version upgraded to 2");
	cdsl_canary_report_free(&rep);

	cdsl_manager_free(mgr);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main(void)
{
	printf("Running Canary Pre-Flight verification tests...\n");
	test_canary_syntax_and_schema_validation();
	test_canary_mock_dry_run();
	test_canary_manager_deployment_gate();
	TEST_SUMMARY();
	TEST_EXIT();
}
