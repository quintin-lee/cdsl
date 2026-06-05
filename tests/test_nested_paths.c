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
test_json_nested_loading(void)
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
test_dotted_identifier_execution(void)
{
	TEST_BEGIN("Dotted identifier execution");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.profile.score", CDSL_TYPE_INT);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_set_int(ctx, "user.profile.score", 95);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE r1 { WHEN user.profile.score > 90 THEN block() }", NULL);
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

static void
test_deep_nesting(void)
{
	TEST_BEGIN("Deep nesting 4+ levels");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_context_t* ctx = cdsl_context_create(schema);

	const char* json =
	    "{\"a\": {\"b\": {\"c\": {\"d\": {\"e\": 42, \"f\": \"deep\"}}, \"g\": 99}}}";
	int ok = cdsl_context_load_json(ctx, json);
	TEST_ASSERT(ok, "JSON should load");

	TEST_ASSERT_INT(
	    cdsl_context_get_int(ctx, "a.b.c.d.e", 0), 42, "Should access 4-level deep int");
	TEST_ASSERT_STR(cdsl_context_get_string(ctx, "a.b.c.d.f", ""),
			"deep",
			"Should access 4-level deep string");
	TEST_ASSERT_INT(
	    cdsl_context_get_int(ctx, "a.b.g", 0), 99, "Should access 2-level deep sibling");
	TEST_ASSERT_INT(cdsl_context_get_int(ctx, "a.b.c.d.e.missing", -1),
			-1,
			"Missing deep path returns default");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_nested_json_execution(void)
{
	TEST_BEGIN("Dotted identifier from JSON context");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "org.dept.budget", CDSL_TYPE_INT);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_load_json(ctx,
			       "{\"org\": {\"dept\": {\"budget\": 50000, \"name\": \"eng\"}}}");

	// budget > 40000 -> true -> FAILED
	cdsl_rule_t* rule1 =
	    cdsl_parse_string("RULE r1 { WHEN org.dept.budget > 40000 THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule1, "Parse rule1");

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule1, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "budget > 40000 -> true -> FAILED");
	cdsl_report_free(rpt);
	cdsl_free_rule(rule1);

	// budget > 60000 -> false -> PASSED
	cdsl_rule_t* rule2 =
	    cdsl_parse_string("RULE r2 { WHEN org.dept.budget > 60000 THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule2, "Parse rule2");

	rpt = cdsl_vm_execute(vm, rule2, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "budget > 60000 -> false -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(rule2);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_nested_json_mixed_types(void)
{
	TEST_BEGIN("Nested JSON mixed types in expressions");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "item.details.price", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "item.details.quantity", CDSL_TYPE_INT);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_load_json(ctx,
			       "{\"item\": {\"details\": {\"price\": 19.99, \"quantity\": 3}}}");

	// price * quantity >= 50 -> true -> FAILED
	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { WHEN item.details.price * item.details.quantity >= 50 THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse rule with mixed types");

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "19.99*3 >= 50 -> true (59.97) -> FAILED");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_nested_string_compare(void)
{
	TEST_BEGIN("Nested string comparison");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.role", CDSL_TYPE_STRING);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_set_string(ctx, "user.role", "admin");

	// Test string equality == true -> FAILED
	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE r { WHEN user.role == \"admin\" THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse string compare rule");

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "role==admin -> true -> FAILED");
	cdsl_report_free(rpt);
	cdsl_free_rule(rule);

	// Test string equality == false -> PASSED
	rule = cdsl_parse_string("RULE r { WHEN user.role == \"superuser\" THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Parse second string rule");

	rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "role==superuser -> false -> PASSED");
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
	test_deep_nesting();
	test_nested_json_execution();
	test_nested_json_mixed_types();
	test_nested_string_compare();
	TEST_SUMMARY();
	TEST_EXIT();
}
