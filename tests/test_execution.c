#include "test.h"
#include "ast.h"
#include "abstract.h"
#include "execution.h"

static int action_called = 0;
static char last_action_name[64] = {0};

static void
test_action_cb(const char* action_name, cdsl_arg_node_t* args, void* ud)
{
	action_called = 1;
	strncpy(last_action_name, action_name, sizeof(last_action_name) - 1);
}

static cdsl_schema_t*
make_test_schema(void)
{
	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(s, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_var(s, "user.active", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(s, "score.value", CDSL_TYPE_FLOAT);
	cdsl_schema_register_action(s, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(s, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
	cdsl_schema_register_action(
	    s, "fail_metric", CDSL_TYPE_VOID, 2, CDSL_TYPE_INT, CDSL_TYPE_STRING);
	return s;
}

void
test_context_set_get(void)
{
	TEST_BEGIN("context set and get values");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "user.age", 25);
	cdsl_context_set_string(ctx, "user.name", "Alice");
	cdsl_context_set_bool(ctx, "user.active", 1);

	cdsl_context_entry_t* e = ctx->entries;
	int found_age = 0, found_name = 0, found_active = 0;
	while (e) {
		if (strcmp(e->name, "user.age") == 0 && e->value.type == CDSL_TYPE_INT &&
		    e->value.data.int_val == 25) {
			found_age = 1;
		}
		if (strcmp(e->name, "user.name") == 0 && e->value.type == CDSL_TYPE_STRING &&
		    strcmp(e->value.data.string_val, "Alice") == 0) {
			found_name = 1;
		}
		if (strcmp(e->name, "user.active") == 0 && e->value.type == CDSL_TYPE_BOOL &&
		    e->value.data.bool_val == 1) {
			found_active = 1;
		}
		e = e->next;
	}
	TEST_ASSERT(found_age, "age set correctly");
	TEST_ASSERT(found_name, "name set correctly");
	TEST_ASSERT(found_active, "active set correctly");

	/* Test context getters */
	TEST_ASSERT_INT(cdsl_context_get_int(ctx, "user.age", 0), 25, "get_int age ok");
	TEST_ASSERT_STR(
	    cdsl_context_get_string(ctx, "user.name", NULL), "Alice", "get_string name ok");
	TEST_ASSERT_INT(cdsl_context_get_bool(ctx, "user.active", 0), 1, "get_bool active ok");

	/* Test default values */
	TEST_ASSERT_INT(cdsl_context_get_int(ctx, "nonexistent", 42), 42, "get_int default ok");
	TEST_ASSERT_STR(cdsl_context_get_string(ctx, "nonexistent", "default"),
			"default",
			"get_string default ok");

	/* Test type conversions */
	cdsl_context_set_float(ctx, "score.value", 95.5);
	TEST_ASSERT_INT(
	    cdsl_context_get_int(ctx, "score.value", 0), 95, "get_int float conversion ok");
	TEST_ASSERT(cdsl_context_get_float(ctx, "user.age", 0.0) == 25.0,
		    "get_float int conversion ok");

	/* Test context remove */
	int removed = cdsl_context_remove(ctx, "user.age");
	TEST_ASSERT_INT(removed, 1, "remove age ok");
	TEST_ASSERT_INT(cdsl_context_get_int(ctx, "user.age", -1), -1, "age removed ok");
	TEST_ASSERT_INT(cdsl_context_remove(ctx, "nonexistent"), 0, "remove nonexistent ok");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_context_load_json(void)
{
	TEST_BEGIN("context load from JSON");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_context_t* ctx = cdsl_context_create(schema);
	int ok = cdsl_context_load_json(
	    ctx, "{\"user\":{\"age\":30,\"name\":\"Bob\",\"active\":false}}");
	TEST_ASSERT_INT(ok, 1, "json load ok");

	int found = 0;
	for (cdsl_context_entry_t* e = ctx->entries; e; e = e->next) {
		if (strcmp(e->name, "user.age") == 0 && e->value.data.int_val == 30) {
			found = 1;
		}
	}
	TEST_ASSERT(found, "age loaded from json");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_simple_rule_pass(void)
{
	TEST_BEGIN("simple rule WHEN false -> PASSED");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_rule_t* rule = cdsl_parse_string("RULE r1 { META { description = \"test\" } WHEN "
					      "user.age > 100 THEN block(\"too_old\") }");
	TEST_ASSERT_NOT_NULL(rule, "parsed");

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "block", test_action_cb);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "user.age", 25);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "status PASSED");
	TEST_ASSERT_INT(rpt->total_obtained_score, 100, "score 100");

	cdsl_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_simple_rule_fail(void)
{
	TEST_BEGIN("simple rule WHEN true -> FAILED");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r1 { META { description = \"test\" } WHEN user.age > 18 THEN block(\"adult\") }");
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "block", test_action_cb);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "user.age", 25);

	action_called = 0;
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "status FAILED");
	TEST_ASSERT_INT(action_called, 1, "action triggered");

	cdsl_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_metric_rule_scoring(void)
{
	TEST_BEGIN("metric rule scoring");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_rule_t* rule = cdsl_parse_string("RULE scoring {"
					      "  META { description = \"score test\" "
					      "pass_threshold = \"80\" partial_threshold = \"50\" }"
					      "  METRIC m1 {"
					      "    META { description = \"m1\" weight = \"60\" }"
					      "    CASE user.age >= 18 THEN score(60)"
					      "    DEFAULT score(0)"
					      "  }"
					      "  METRIC m2 {"
					      "    META { description = \"m2\" weight = \"40\" }"
					      "    CASE user.active == true THEN score(40)"
					      "    DEFAULT score(0)"
					      "  }"
					      "}");
	TEST_ASSERT_NOT_NULL(rule, "parsed");

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "score", test_action_cb);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "user.age", 20);
	cdsl_context_set_bool(ctx, "user.active", 1);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "status PASSED");
	TEST_ASSERT_INT(rpt->total_obtained_score, 100, "score 100/100");
	TEST_ASSERT_INT(rpt->metric_count, 2, "2 metrics");

	cdsl_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_metric_rule_partial(void)
{
	TEST_BEGIN("metric rule partial pass");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_rule_t* rule = cdsl_parse_string("RULE scoring {"
					      "  META { description = \"score test\" "
					      "pass_threshold = \"80\" partial_threshold = \"50\" }"
					      "  METRIC m1 {"
					      "    META { description = \"m1\" weight = \"60\" }"
					      "    CASE user.age >= 18 THEN score(60)"
					      "    DEFAULT score(0)"
					      "  }"
					      "  METRIC m2 {"
					      "    META { description = \"m2\" weight = \"40\" }"
					      "    CASE user.active == true THEN score(40)"
					      "    DEFAULT score(0)"
					      "  }"
					      "}");
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "score", test_action_cb);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "user.age", 20);
	cdsl_context_set_bool(ctx, "user.active", 0);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PARTIALLY_PASSED, "status PARTIAL");
	TEST_ASSERT_INT(rpt->total_obtained_score, 60, "score 60/100");

	cdsl_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_critical_metric_veto(void)
{
	TEST_BEGIN("critical metric veto -> FAILED");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE veto {"
	    "  META { description = \"veto test\" pass_threshold = \"50\" partial_threshold = "
	    "\"20\" }"
	    "  METRIC m1 {"
	    "    META { description = \"m1\" weight = \"50\" is_critical = \"true\" }"
	    "    CASE user.active == true THEN score(50)"
	    "    DEFAULT fail_metric(0, \"inactive\")"
	    "  }"
	    "  METRIC m2 {"
	    "    META { description = \"m2\" weight = \"50\" }"
	    "    CASE user.age >= 0 THEN score(50)"
	    "    DEFAULT score(0)"
	    "  }"
	    "}");
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "score", test_action_cb);
	cdsl_vm_register_action(vm, "fail_metric", test_action_cb);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "user.age", 25);
	cdsl_context_set_bool(ctx, "user.active", 0);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "status FAILED (critical veto)");

	cdsl_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_string_comparison(void)
{
	TEST_BEGIN("string comparison in expression");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_rule_t* rule = cdsl_parse_string("RULE strcmp { META { desc = \"str\" } WHEN "
					      "user.name == \"Alice\" THEN block(\"match\") }");
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "block", test_action_cb);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_string(ctx, "user.name", "Alice");

	action_called = 0;
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "status FAILED");
	TEST_ASSERT_INT(action_called, 1, "action triggered on string match");

	cdsl_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_ruleset_batch(void)
{
	TEST_BEGIN("ruleset batch execution");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_ruleset_t* set = cdsl_ruleset_create();

	cdsl_rule_t* r1 = cdsl_parse_string(
	    "RULE a { META { d = \"a\" } WHEN user.age > 100 THEN block(\"a\") }");
	cdsl_rule_t* r2 =
	    cdsl_parse_string("RULE b { META { d = \"b\" } WHEN user.age < 0 THEN block(\"b\") }");
	cdsl_ruleset_add(set, r1, 1);
	cdsl_ruleset_add(set, r2, 2);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_register_action(vm, "block", test_action_cb);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "user.age", 25);

	cdsl_ruleset_report_t* rpt = cdsl_vm_execute_ruleset(vm, set, ctx);
	TEST_ASSERT_INT(rpt->rule_count, 2, "2 rules");
	TEST_ASSERT_INT(rpt->total_passed, 2, "2 passed");
	TEST_ASSERT_INT(rpt->total_failed, 0, "0 failed");

	cdsl_ruleset_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_ruleset_free(set);
	cdsl_schema_free(schema);
	TEST_END();
}

void
test_verify_detailed(void)
{
	TEST_BEGIN("detailed verification finds unknown variable");
	cdsl_schema_t* schema = make_test_schema();
	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { META { d = \"x\" } WHEN unknown.var == 1 THEN block(\"x\") }");
	cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
	TEST_ASSERT(errors->count > 0, "errors found for unknown var");
	cdsl_error_list_free(errors);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main(void)
{
	printf("========================================\n");
	printf("  Execution Unit Tests\n");
	printf("========================================\n");

	test_context_set_get();
	test_context_load_json();
	test_simple_rule_pass();
	test_simple_rule_fail();
	test_metric_rule_scoring();
	test_metric_rule_partial();
	test_critical_metric_veto();
	test_string_comparison();
	test_ruleset_batch();
	test_verify_detailed();

	TEST_SUMMARY();
	TEST_EXIT();
}
