/**
 * @file test_api_unification.c
 * @brief Tests for the unified/simplified API layer (cdsl_ctx_t, cdsl_set/get, api.h).
 */

#include "test.h"
#include <cdsl/api.h>
#include <cdsl/vm.h>
#include <cdsl/context.h>
#include <cdsl/ruleset.h>
#include <cdsl/report.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ---- Context handle lifecycle ---- */

static void
test_api_ctx_create_free(void)
{
	TEST_BEGIN("api: ctx create/free with NULL schema");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	TEST_ASSERT_NOT_NULL(c, "cdsl_ctx_create(NULL) should return non-NULL");
	TEST_ASSERT_NOT_NULL(c->vm, "ctx->vm should be non-NULL");
	TEST_ASSERT_NOT_NULL(c->schema, "ctx->schema should be non-NULL (default created)");
	TEST_ASSERT_NOT_NULL(c->ctx, "ctx->ctx should be non-NULL");
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_ctx_create_free_null_safe(void)
{
	TEST_BEGIN("api: ctx free NULL-safe");
	cdsl_ctx_free(NULL);
	TEST_END();
}

static void
test_api_ctx_create_with_schema(void)
{
	TEST_BEGIN("api: ctx create with provided schema");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_ctx_t* c = cdsl_ctx_create(schema);
	TEST_ASSERT_NOT_NULL(c, "cdsl_ctx_create(schema) should return non-NULL");
	TEST_ASSERT_INT(c->schema, schema, "ctx->schema should be the provided schema");
	/* cdsl_ctx_free takes ownership of the schema */
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_ctx_accessors(void)
{
	TEST_BEGIN("api: ctx accessors return correct pointers");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	TEST_ASSERT_NOT_NULL(cdsl_ctx_vm(c), "cdsl_ctx_vm should return non-NULL");
	TEST_ASSERT_NOT_NULL(cdsl_ctx_schema(c), "cdsl_ctx_schema should return non-NULL");
	TEST_ASSERT_NOT_NULL(cdsl_ctx_context(c), "cdsl_ctx_context should return non-NULL");
	TEST_ASSERT_NULL(cdsl_ctx_vm(NULL), "cdsl_ctx_vm(NULL) == NULL");
	TEST_ASSERT_NULL(cdsl_ctx_schema(NULL), "cdsl_ctx_schema(NULL) == NULL");
	TEST_ASSERT_NULL(cdsl_ctx_context(NULL), "cdsl_ctx_context(NULL) == NULL");
	cdsl_ctx_free(c);
	TEST_END();
}

/* ---- cdsl_set / cdsl_get _Generic macros ---- */

static void
test_api_set_get_int(void)
{
	TEST_BEGIN("api: cdsl_set/get int via _Generic");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	cdsl_set(c, "age", 42);
	TEST_ASSERT_INT(cdsl_get(c, "age", -1), 42, "get int should return 42");
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_set_get_double(void)
{
	TEST_BEGIN("api: cdsl_set/get double via _Generic");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	cdsl_set(c, "score", 3.14);
	TEST_ASSERT_INT(
	    (int)(cdsl_get(c, "score", 0.0) * 100), 314, "get double should return ~3.14");
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_set_get_bool(void)
{
	TEST_BEGIN("api: cdsl_set/get bool via _Generic");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	cdsl_set(c, "flag", 1);
	TEST_ASSERT_INT(cdsl_get(c, "flag", 0), 1, "get bool should return 1");
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_set_get_string(void)
{
	TEST_BEGIN("api: cdsl_set/get string via _Generic");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	cdsl_set(c, "name", "hello");
	TEST_ASSERT_STR(cdsl_get(c, "name", "default"), "hello", "get string should return hello");
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_set_get_default_on_missing(void)
{
	TEST_BEGIN("api: cdsl_get returns default for missing key");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	TEST_ASSERT_INT(cdsl_get(c, "missing", 999), 999, "get missing int should return default");
	TEST_ASSERT_STR(cdsl_get(c, "missing", "fallback"),
			"fallback",
			"get missing string should return default");
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_set_get_long(void)
{
	TEST_BEGIN("api: cdsl_set/get int64_t via _Generic");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	cdsl_set(c, "big", (int64_t)9223372036854775807LL);
	TEST_ASSERT_INT(cdsl_get(c, "big", (int64_t)0),
			9223372036854775807LL,
			"get int64 should return max value");
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_set_get_time_t(void)
{
	TEST_BEGIN("api: cdsl_set/get time_t via _Generic");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	time_t now = (time_t)1700000000;
	cdsl_set(c, "ts", now);
	TEST_ASSERT_INT(cdsl_get(c, "ts", (int64_t)0), now, "get time_t should return set value");
	cdsl_ctx_free(c);
	TEST_END();
}

/* ---- Handle-level convenience functions ---- */

static void
test_api_ctx_register_action(void)
{
	TEST_BEGIN("api: cdsl_ctx_register_action");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	cdsl_ctx_register_action(c, "block", NULL);
	cdsl_ctx_free(c);
	TEST_ASSERT(1, "registration should not crash");
	TEST_END();
}

static void
test_api_ctx_register_function(void)
{
	TEST_BEGIN("api: cdsl_ctx_register_function");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	cdsl_ctx_register_function(c, "my_func", NULL);
	cdsl_ctx_free(c);
	TEST_ASSERT(1, "registration should not crash");
	TEST_END();
}

static void
test_api_ctx_execute_single_rule(void)
{
	TEST_BEGIN("api: cdsl_ctx_execute single rule");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	cdsl_rule_t* rule = cdsl_parse_string("RULE r { WHEN 1 == 1 THEN block(\"x\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse rule");
	cdsl_rule_report_t* rpt = cdsl_ctx_execute(c, rule);
	TEST_ASSERT_NOT_NULL(rpt, "execute should return report");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "1==1 -> FAILED (block not registered)");
	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_ctx_execute_ruleset(void)
{
	TEST_BEGIN("api: cdsl_ctx_execute_ruleset");
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	cdsl_ruleset_t* rs = cdsl_ruleset_create();
	TEST_ASSERT_NOT_NULL(rs, "ruleset create");
	cdsl_rule_t* rule = cdsl_parse_string("RULE r { WHEN true THEN block(\"x\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse rule");
	cdsl_ruleset_add(rs, rule, 1);
	cdsl_ruleset_report_t* rpt = cdsl_ctx_execute_ruleset(c, rs, 1);
	TEST_ASSERT_NOT_NULL(rpt, "execute_ruleset should return report");
	cdsl_ruleset_report_free(rpt);
	cdsl_ruleset_free(rs);
	cdsl_ctx_free(c);
	TEST_END();
}

static void
test_api_backward_compat(void)
{
	TEST_BEGIN("api: existing cdsl_vm_* / cdsl_context_* API still works");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "x", 10);
	TEST_ASSERT_INT(cdsl_context_get_int(ctx, "x", -1), 10, "old API still works");
	cdsl_vm_register_action(vm, "act", NULL);
	cdsl_vm_register_function(vm, "fn", NULL);
	cdsl_vm_set_debug(vm, 1);
	TEST_ASSERT_INT(cdsl_vm_get_max_expr_depth(vm), CDSL_MAX_EXPR_DEPTH, "depth getter");
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_api_api_header_compiles(void)
{
	TEST_BEGIN("api: cdsl/api.h umbrella header compiles");
	/* If we reach here, the header compiled fine */
	cdsl_ctx_t* c = cdsl_ctx_create(NULL);
	TEST_ASSERT_NOT_NULL(c, "api.h provides cdsl_ctx_create");
	cdsl_ctx_free(c);
	TEST_END();
}

int
main()
{
	printf("Running API unification tests...\n");
	test_api_ctx_create_free();
	test_api_ctx_create_free_null_safe();
	test_api_ctx_create_with_schema();
	test_api_ctx_accessors();
	test_api_set_get_int();
	test_api_set_get_double();
	test_api_set_get_bool();
	test_api_set_get_string();
	test_api_set_get_default_on_missing();
	test_api_set_get_long();
	test_api_set_get_time_t();
	test_api_ctx_register_action();
	test_api_ctx_register_function();
	test_api_ctx_execute_single_rule();
	test_api_ctx_execute_ruleset();
	test_api_backward_compat();
	test_api_api_header_compiles();
	TEST_SUMMARY();
	TEST_EXIT();
}
