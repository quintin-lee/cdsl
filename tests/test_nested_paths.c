/**
 * @file test_nested_paths.c
 * @brief Unit tests for nested JSON path access in identifiers.
 */

#include "test.h"
#include <cdsl/cdsl.h>
#include <cdsl/context.h>
#include <cdsl/schema.h>
#include <cdsl/execution.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static void
test_json_nested_loading()
{
	TEST_BEGIN("JSON nested path loading");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_context_t* ctx = cdsl_context_create(schema);

	const char* json =
	    "{\"user\": {\"profile\": {\"age\": 25, \"name\": \"Alice\"}, \"active\": true}}";
	int ok = cdsl_context_load_json(ctx, json);
	TEST_ASSERT(ok, "JSON should load");

	TEST_ASSERT_INT(
	    cdsl_context_get_int(ctx, "user.profile.age", 0), 25, "Should access nested age");
	TEST_ASSERT_STR(cdsl_context_get_string(ctx, "user.profile.name", ""),
			"Alice",
			"Should access nested name");
	TEST_ASSERT_INT(
	    cdsl_context_get_bool(ctx, "user.active", 0), 1, "Should access nested boolean");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_dotted_identifier_execution()
{
	TEST_BEGIN("Dotted identifier execution");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.profile.score", CDSL_TYPE_INT);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_set_int(ctx, "user.profile.score", 95);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE r1 { WHEN user.profile.score > 90 THEN block() }");
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse dotted identifier");

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(
	    rpt->status, CDSL_STATUS_FAILED, "Rule should trigger on dotted identifier");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main()
{
	printf("Running nested path tests...\n");
	test_json_nested_loading();
	test_dotted_identifier_execution();
	TEST_SUMMARY();
	TEST_EXIT();
}
