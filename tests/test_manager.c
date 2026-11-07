/**
 * @file tests/test_manager.c
 * @brief Unit tests for Enterprise Rule Manager, RCU hot reload, and rollback.
 */

#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/manager.h"
#include <stdlib.h>
#include <string.h>

static void
test_manager_basic_deploy_and_acquire(void)
{
	TEST_BEGIN("manager: basic deploy and acquire");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "pass", CDSL_TYPE_VOID, 0);

	cdsl_manager_t* mgr = cdsl_manager_create(schema);
	TEST_ASSERT_NOT_NULL(mgr, "create manager");
	TEST_ASSERT_INT((int)cdsl_manager_get_active_version(mgr), 0, "active version 0 initially");

	uint64_t v1 = 0;
	int ok1 = cdsl_manager_deploy_dsl(
	    mgr, "RULE r1 { WHEN age >= 18 THEN pass() }", "v1.0", &v1, NULL);
	TEST_ASSERT_INT(ok1, 1, "deploy v1");
	TEST_ASSERT_INT((int)v1, 1, "version 1 assigned");
	TEST_ASSERT_INT((int)cdsl_manager_get_active_version(mgr), 1, "active version is 1");

	cdsl_snapshot_t* snap = cdsl_manager_acquire(mgr);
	TEST_ASSERT_NOT_NULL(snap, "acquire snapshot");
	TEST_ASSERT_INT((int)snap->version_id, 1, "snapshot version is 1");
	TEST_ASSERT_STR(snap->tag, "v1.0", "snapshot tag is v1.0");

	cdsl_manager_release(mgr, snap);
	cdsl_manager_free(mgr);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_manager_hot_reload_and_rollback(void)
{
	TEST_BEGIN("manager: hot reload and rollback");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "pass", CDSL_TYPE_VOID, 0);

	cdsl_manager_t* mgr = cdsl_manager_create(schema);

	uint64_t v1 = 0, v2 = 0, v3 = 0;
	cdsl_manager_deploy_dsl(mgr, "RULE r { WHEN score >= 60 THEN pass() }", "v1.0", &v1, NULL);
	cdsl_manager_deploy_dsl(mgr, "RULE r { WHEN score >= 80 THEN pass() }", "v2.0", &v2, NULL);
	cdsl_manager_deploy_dsl(mgr, "RULE r { WHEN score >= 90 THEN pass() }", "v3.0", &v3, NULL);

	TEST_ASSERT_INT((int)cdsl_manager_get_active_version(mgr), 3, "active version is 3");

	uint64_t history[8];
	size_t hcount = cdsl_manager_get_history(mgr, history, 8);
	TEST_ASSERT_INT((int)hcount, 2, "history contains 2 prior versions");
	TEST_ASSERT_INT((int)history[0], 1, "history[0] is v1");
	TEST_ASSERT_INT((int)history[1], 2, "history[1] is v2");

	/* Rollback to v1 */
	int rolled = cdsl_manager_rollback(mgr, 1);
	TEST_ASSERT_INT(rolled, 1, "rollback to v1 succeeded");
	TEST_ASSERT_INT((int)cdsl_manager_get_active_version(mgr), 1, "active version is now 1");

	/* Rollback to nonexistent version fails cleanly */
	TEST_ASSERT_INT(cdsl_manager_rollback(mgr, 999), 0, "rollback nonexistent fails");

	cdsl_manager_free(mgr);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_manager_execute_helper(void)
{
	TEST_BEGIN("manager: execute helper across versions");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "pass", CDSL_TYPE_VOID, 0);

	cdsl_manager_t* mgr = cdsl_manager_create(schema);
	cdsl_manager_deploy_dsl(
	    mgr, "RULE r { WHEN score >= 80 THEN pass() }", "pass_80", NULL, NULL);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "score", 85);

	cdsl_ruleset_report_t* rpt = cdsl_manager_execute(mgr, vm, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute active ruleset");
	TEST_ASSERT_INT(rpt->total_failed, 1, "score 85 triggered condition");
	cdsl_ruleset_report_free(rpt);

	/* Hot reload to score >= 90 */
	cdsl_manager_deploy_dsl(
	    mgr, "RULE r { WHEN score >= 90 THEN pass() }", "pass_90", NULL, NULL);

	rpt = cdsl_manager_execute(mgr, vm, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute updated ruleset");
	TEST_ASSERT_INT(rpt->total_passed, 1, "score 85 passes without triggering score >= 90");
	cdsl_ruleset_report_free(rpt);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_manager_free(mgr);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main(void)
{
	printf("Running Rule Manager & RCU hot reload tests...\n");
	test_manager_basic_deploy_and_acquire();
	test_manager_hot_reload_and_rollback();
	test_manager_execute_helper();
	TEST_SUMMARY();
	TEST_EXIT();
}
